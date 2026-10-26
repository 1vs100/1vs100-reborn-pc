#include "matchmaking_client.h"
#include <rex/logging.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include "Protocol.h"
#include "avatar_native/native_provider.h"
#include "local_profile.h"
#include "online_service.h"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {
#ifdef _WIN32
using HostSocket = SOCKET;
constexpr HostSocket kNoSocket = INVALID_SOCKET;
constexpr int kSendFlags = 0;
void CloseSocket(HostSocket s) { closesocket(s); }
int LastError() { return WSAGetLastError(); }
void SetTimeouts(HostSocket s, int ms) {
  const DWORD value = static_cast<DWORD>(ms);
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value), sizeof(value));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&value), sizeof(value));
}
#else
using HostSocket = int;
constexpr HostSocket kNoSocket = -1;
constexpr int kSendFlags = MSG_NOSIGNAL;
void CloseSocket(HostSocket s) { ::close(s); }
int LastError() { return errno; }
void SetTimeouts(HostSocket s, int ms) {
  timeval timeout{ms / 1000, (ms % 1000) * 1000};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}
#endif

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

// The 1149 gameserver's matchmaking port.
constexpr uint16_t kMatchmakingPort = 1001;
constexpr auto kTickInterval = milliseconds(100);
constexpr auto kHeartbeatInterval = milliseconds(15000);
constexpr auto kRetryInterval = milliseconds(2000);
// A rejection is a deployment mistake: retry slowly.
constexpr auto kRejectedRetryInterval = milliseconds(60000);
constexpr auto kAvatarInterval = milliseconds(2000);
constexpr int kSocketTimeoutMs = 3000;
constexpr size_t kMaxFrame = 1536;
constexpr size_t kRelayFrame = MATCHMAKING_RELAY_MAX_BYTES + 256;
constexpr size_t kRelayQueueLength = 16;
constexpr int kRelaySendsPerTick = 8;

struct Rejection {
  std::optional<uint8_t> reason;  // absent when the REJECT carried no payload
  uint8_t serverVersion;
};

// One TCP connection to the matchmaking port; requests block for their response.
class Connection {
 public:
  ~Connection() { Close(); }
  bool Open(uint32_t address) {
    Close();
    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == kNoSocket) return false;
    SetTimeouts(socket_, kSocketTimeoutMs);
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(kMatchmakingPort);
    peer.sin_addr.s_addr = htonl(address);
    if (::connect(socket_, reinterpret_cast<sockaddr*>(&peer), sizeof(peer)) != 0) {
      REXKRNL_INFO("Matchmaking: connect to port {} failed, host error {}", kMatchmakingPort, LastError());
      Close();
      return false;
    }
    return true;
  }
  void Close() {
    if (socket_ != kNoSocket) CloseSocket(socket_);
    socket_ = kNoSocket;
  }
  bool Send(uint8_t type, std::span<const uint8_t> payload) {
    std::vector<uint8_t> frame(MM_HEADER_SIZE + payload.size());
    const uint32_t length = MmFrame(frame.data(), static_cast<uint32_t>(frame.size()), type, payload.data(),
                                    static_cast<uint32_t>(payload.size()));
    return length != 0 && SendAll(frame.data(), length);
  }
  // Sends a request and reads frames until its response; announcements in
  // between go to `onAnnounce`. On a REJECT, `rejection` holds the reason.
  template <typename OnAnnounce>
  std::optional<std::vector<uint8_t>> Request(uint8_t requestType, uint8_t responseType,
                                              std::span<const uint8_t> payload, size_t responseCap,
                                              OnAnnounce&& onAnnounce) {
    rejection.reset();
    if (!Send(requestType, payload)) return std::nullopt;
    for (;;) {
      uint8_t header[MM_HEADER_SIZE];
      if (!ReceiveAll(header, sizeof(header))) return std::nullopt;
      uint8_t version, type;
      uint32_t length;
      MmParseHeader(header, &version, &type, &length);
      if (version != MM_PROTOCOL_VERSION) {
        rejection = Rejection{MM_REJECT_VERSION, version};
        return std::nullopt;
      }
      if (length > responseCap) return std::nullopt;
      std::vector<uint8_t> response(length);
      if (!ReceiveAll(response.data(), length)) return std::nullopt;
      if (type == MM_TYPE_REJECT) {
        rejection = Rejection{response.empty() ? std::nullopt : std::optional<uint8_t>(response[0]), MM_PROTOCOL_VERSION};
        return std::nullopt;
      }
      if (type != MM_TYPE_ANNOUNCE) {
        if (type == responseType) return response;
        REXKRNL_WARN("Matchmaking: response type {:02X} to request {:02X}", type, requestType);
        return std::nullopt;
      }
      onAnnounce(response);
    }
  }
  std::optional<Rejection> rejection;

 private:
  bool SendAll(const uint8_t* data, size_t length) {
    while (length) {
      const auto sent = ::send(socket_, reinterpret_cast<const char*>(data), static_cast<int>(length), kSendFlags);
      if (sent <= 0) return false;
      data += sent;
      length -= static_cast<size_t>(sent);
    }
    return true;
  }
  bool ReceiveAll(uint8_t* out, size_t length) {
    while (length) {
      const auto got = ::recv(socket_, reinterpret_cast<char*>(out), static_cast<int>(length), 0);
      if (got <= 0) return false;
      out += got;
      length -= static_cast<size_t>(got);
    }
    return true;
  }
  HostSocket socket_ = kNoSocket;
};

std::string Narrow(const MmChar16* text, size_t capacity) {
  std::string out;
  for (size_t i = 0; i < capacity && text[i]; ++i) out += text[i] < 0x80 ? static_cast<char>(text[i]) : '?';
  return out;
}

std::optional<std::vector<uint8_t>> HexBytes(const std::string& hex) {
  auto nibble = [](char c) -> int {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
  };
  if (hex.size() % 2) return std::nullopt;
  std::vector<uint8_t> bytes(hex.size() / 2);
  for (size_t i = 0; i < bytes.size(); ++i) {
    const int high = nibble(hex[2 * i]), low = nibble(hex[2 * i + 1]);
    if (high < 0 || low < 0) return std::nullopt;
    bytes[i] = static_cast<uint8_t>(high << 4 | low);
  }
  return bytes;
}
std::string BytesHex(std::span<const uint8_t> bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string hex;
  for (const uint8_t byte : bytes) {
    hex += kDigits[byte >> 4];
    hex += kDigits[byte & 15];
  }
  return hex;
}

class Client {
 public:
  void Run();

  std::mutex lock;  // guards everything below that title threads read
  std::vector<MatchmakingRemotePeer> peers;
  std::deque<std::pair<std::vector<uint64_t>, std::vector<uint8_t>>> outgoing;
  std::deque<RelayedPayload> incoming;
  std::atomic<bool> paused{false};
  std::atomic<bool> resumed{false};
  std::atomic<bool> joined{false};
  std::atomic<uint64_t> selfXuid{0};

 private:
  template <typename Writer>
  std::optional<std::vector<uint8_t>> Request(uint8_t requestType, uint8_t responseType, size_t payloadCap,
                                              size_t responseCap, Writer&& write);
  void ApplyRoster(std::span<const uint8_t> payload);
  void NoteRejection(const Rejection& rejection);
  void Drop();
  void AttemptJoin();
  bool RefreshRoster();
  void SendHeartbeat();
  void SendLeave();
  void PumpRelay();
  void UploadAvatar();
  void FetchPeerAvatars();

  Connection connection_;
  Clock::time_point nextAttempt_{}, nextHeartbeat_{}, nextRoster_{}, nextAvatar_{};
  std::optional<Rejection> loggedRejection_;
  bool avatarStored_ = false;
  std::set<uint64_t> avatarsFetched_;
};

Client& TheClient() {
  static Client* client = new Client;
  return *client;
}

template <typename Writer>
std::optional<std::vector<uint8_t>> Client::Request(uint8_t requestType, uint8_t responseType, size_t payloadCap,
                                                    size_t responseCap, Writer&& write) {
  std::vector<uint8_t> payload(payloadCap);
  MmWriter writer;
  MmWriter_Init(&writer, payload.data(), static_cast<uint32_t>(payload.size()));
  write(&writer);
  if (writer.overflow) {
    REXKRNL_WARN("Matchmaking: request {:02X} too large, dropped", requestType);
    return std::nullopt;
  }
  payload.resize(MmWriter_Length(&writer));
  auto response = connection_.Request(requestType, responseType, payload, responseCap,
                                      [this](std::span<const uint8_t> announce) { ApplyRoster(announce); });
  if (!response && connection_.rejection) NoteRejection(*connection_.rejection);
  return response;
}

void Client::ApplyRoster(std::span<const uint8_t> payload) {
  MatchmakingPeer next[MATCHMAKING_MAX_REMOTE_PEERS]{};
  uint32_t count = 0, group = 0;
  if (!MmDecodeRoster(payload.data(), static_cast<uint32_t>(payload.size()), selfXuid, next,
                      MATCHMAKING_MAX_REMOTE_PEERS, &count, &group)) {
    REXKRNL_WARN("Matchmaking: roster malformed, ignored");
    return;
  }
  std::vector<MatchmakingRemotePeer> roster;
  for (uint32_t i = 0; i < count; ++i)
    roster.push_back({next[i].xuid, Narrow(next[i].gamertag, std::size(next[i].gamertag))});
  std::erase_if(avatarsFetched_, [&](uint64_t xuid) {
    return std::none_of(roster.begin(), roster.end(), [&](const auto& peer) { return peer.xuid == xuid; });
  });
  {
    std::lock_guard guard(lock);
    peers = std::move(roster);
  }
  REXKRNL_INFO("Matchmaking: group {}, remote roster peers={}", group, count);
}

void Client::NoteRejection(const Rejection& rejection) {
  nextAttempt_ = Clock::now() + kRejectedRetryInterval;
  if (loggedRejection_ && loggedRejection_->reason == rejection.reason &&
      loggedRejection_->serverVersion == rejection.serverVersion)
    return;
  loggedRejection_ = rejection;
  switch (rejection.reason.value_or(0)) {
    case MM_REJECT_VERSION:
      REXKRNL_ERROR("Matchmaking: server speaks protocol {}, this client {}", rejection.serverVersion,
                    MM_PROTOCOL_VERSION);
      break;
    case MM_REJECT_MALFORMED_JOIN: REXKRNL_ERROR("Matchmaking: server rejected the JOIN as malformed"); break;
    case MM_REJECT_UNKNOWN_TYPE: REXKRNL_ERROR("Matchmaking: server rejected a message type as unknown"); break;
    default:
      if (rejection.reason) REXKRNL_ERROR("Matchmaking: server rejected a request (reason {})", *rejection.reason);
      else REXKRNL_ERROR("Matchmaking: server rejected a request without a reason");
      break;
  }
}

// The roster stays: a dropped connection rejoins after the retry interval.
void Client::Drop() {
  connection_.Close();
  joined = false;
  avatarStored_ = false;
  avatarsFetched_.clear();
  nextAttempt_ = std::max(nextAttempt_, Clock::now() + kRetryInterval);
}

void Client::AttemptJoin() {
  MatchmakingLocalPlayer player{};
  player.xuid = LocalXuid();
  std::strncpy(player.gamertag, LocalGamertag().c_str(), sizeof(player.gamertag) - 1);
  player.user = 0;
  if (!player.xuid) return;
  selfXuid = player.xuid;
  if (!connection_.Open(CurrentGameserverAddress().value)) return;
  auto ack = Request(MM_TYPE_JOIN, MM_TYPE_JOIN_ACK, kMaxFrame, kMaxFrame,
                     [&](MmWriter* w) { MmEncodeJoin(w, &player, 1); });
  if (!ack) {
    REXKRNL_INFO("Matchmaking: join failed");
    Drop();
    return;
  }
  joined = true;
  loggedRejection_.reset();
  uint32_t population = 0, group = 0;
  if (!MmDecodeJoinAck(ack->data(), static_cast<uint32_t>(ack->size()), &population, &group))
    REXKRNL_WARN("Matchmaking: JOIN_ACK short ({} bytes)", ack->size());
  if (!RefreshRoster()) return;
  const auto now = Clock::now();
  nextHeartbeat_ = nextRoster_ = now + kHeartbeatInterval;
  REXKRNL_INFO("Matchmaking: joined as {} ({:016X}), group {}, population {}", player.gamertag, player.xuid, group,
               population);
}

bool Client::RefreshRoster() {
  auto roster = Request(MM_TYPE_ROSTER, MM_TYPE_ROSTER_REPLY, 0, kMaxFrame, [](MmWriter*) {});
  if (!roster) {
    REXKRNL_INFO("Matchmaking: roster refresh failed, rejoining");
    Drop();
    return false;
  }
  ApplyRoster(*roster);
  return true;
}

void Client::SendHeartbeat() {
  auto ack = Request(MM_TYPE_HEARTBEAT, MM_TYPE_HEARTBEAT_ACK, kMaxFrame, kMaxFrame,
                     [this](MmWriter* w) { MmEncodeXuid(w, selfXuid); });
  if (!ack) {
    REXKRNL_INFO("Matchmaking: heartbeat failed, rejoining");
    Drop();
  }
}

void Client::SendLeave() {
  std::vector<uint8_t> payload(16);
  MmWriter writer;
  MmWriter_Init(&writer, payload.data(), static_cast<uint32_t>(payload.size()));
  MmEncodeXuid(&writer, selfXuid);
  connection_.Send(MM_TYPE_LEAVE, std::span(payload.data(), MmWriter_Length(&writer)));
  REXKRNL_INFO("Matchmaking: left the group");
}

void Client::PumpRelay() {
  for (int sent = 0; sent < kRelaySendsPerTick && joined; ++sent) {
    std::pair<std::vector<uint64_t>, std::vector<uint8_t>> item;
    {
      std::lock_guard guard(lock);
      if (outgoing.empty()) break;
      item = std::move(outgoing.front());
      outgoing.pop_front();
    }
    const auto& [targets, data] = item;
    if (!Request(MM_TYPE_RELAY_PUT, MM_TYPE_RELAY_ACK, kRelayFrame, kMaxFrame, [&](MmWriter* w) {
          MmEncodeRelayPut(w, selfXuid, targets.data(), static_cast<uint32_t>(targets.size()), data.data(),
                           static_cast<uint32_t>(data.size()));
        })) {
      REXKRNL_INFO("Matchmaking: relay send failed");
      Drop();
      return;
    }
  }
  bool anyPeer;
  {
    std::lock_guard guard(lock);
    anyPeer = !peers.empty();
  }
  if (!joined || !anyPeer) return;
  auto reply = Request(MM_TYPE_RELAY_POLL, MM_TYPE_RELAY_REPLY, kMaxFrame, kRelayFrame,
                       [this](MmWriter* w) { MmEncodeXuid(w, selfXuid); });
  if (!reply) {
    REXKRNL_INFO("Matchmaking: relay poll failed");
    Drop();
    return;
  }
  MmDecodeRelayReply(
      reply->data(), static_cast<uint32_t>(reply->size()),
      [](void* self, uint64_t from, const uint8_t* data, uint32_t length) {
        auto& client = *static_cast<Client*>(self);
        std::lock_guard guard(client.lock);
        if (client.incoming.size() < kRelayQueueLength)
          client.incoming.push_back({from, std::vector<uint8_t>(data, data + length)});
      },
      this);
}

void Client::UploadAvatar() {
  if (avatarStored_) return;
  const std::string hex = NativeAvatarManifestHex(std::to_string(selfXuid.load()));
  if (hex.size() != 2 * AVATAR_METADATA_BYTES) return;
  const auto blob = HexBytes(hex);
  if (!blob) return;
  auto ack = Request(MM_TYPE_AVATAR_PUT, MM_TYPE_AVATAR_ACK, kMaxFrame, kMaxFrame,
                     [&](MmWriter* w) { MmEncodeAvatarPut(w, selfXuid, blob->data()); });
  if (!ack) {
    REXKRNL_INFO("Matchmaking: Avatar upload failed");
    Drop();
    return;
  }
  avatarStored_ = !ack->empty() && (*ack)[0] == 1;
}

void Client::FetchPeerAvatars() {
  std::vector<MatchmakingRemotePeer> roster;
  {
    std::lock_guard guard(lock);
    roster = peers;
  }
  for (const auto& peer : roster) {
    if (avatarsFetched_.count(peer.xuid)) continue;
    auto reply = Request(MM_TYPE_AVATAR_GET, MM_TYPE_AVATAR_REPLY, kMaxFrame, kMaxFrame,
                         [&](MmWriter* w) { MmEncodeAvatarGet(w, peer.xuid); });
    if (!reply) {
      REXKRNL_INFO("Matchmaking: Avatar fetch failed");
      Drop();
      return;
    }
    const uint8_t* blob = nullptr;
    if (!MmDecodeAvatarReply(reply->data(), static_cast<uint32_t>(reply->size()), &blob)) continue;
    // The Avatar provider checks the manifest and rebuilds it from the catalog.
    const std::string result =
        NativeAvatarSetRemote(std::to_string(peer.xuid) + "|" + BytesHex(std::span(blob, AVATAR_METADATA_BYTES)));
    REXKRNL_INFO("Matchmaking: Avatar of {} ({:016X}): {}", peer.gamertag, peer.xuid, result);
    avatarsFetched_.insert(peer.xuid);
  }
}

void Client::Run() {
  for (;; std::this_thread::sleep_for(kTickInterval)) {
    if (paused) {
      if (joined) {
        SendLeave();
        Drop();
        REXKRNL_INFO("Matchmaking: paused, out of the game");
      }
      // Also after a dropped connection: no stale peer may join the next session.
      std::lock_guard guard(lock);
      peers.clear();
      continue;
    }
    const auto now = Clock::now();
    // A resume rejoins at once, unless a rejection asked to wait.
    if (resumed.exchange(false) && !loggedRejection_) nextAttempt_ = now;
    if (!joined) {
      if (now < nextAttempt_) continue;
      nextAttempt_ = now + kRetryInterval;
      AttemptJoin();
      continue;
    }
    if (now >= nextHeartbeat_) {
      nextHeartbeat_ = now + kHeartbeatInterval;
      SendHeartbeat();
    }
    if (joined && now >= nextRoster_) {
      nextRoster_ = now + kHeartbeatInterval;
      RefreshRoster();
    }
    if (joined) PumpRelay();
    if (joined && now >= nextAvatar_) {
      nextAvatar_ = now + kAvatarInterval;
      UploadAvatar();
      if (joined) FetchPeerAvatars();
    }
  }
}
}  // namespace

void StartMatchmakingClient() {
  static std::once_flag started;
  std::call_once(started, [] {
    REXKRNL_INFO("Matchmaking: client started");
    std::thread([] { TheClient().Run(); }).detach();
  });
}

void MatchmakingPause() { TheClient().paused = true; }
void MatchmakingResume() {
  if (TheClient().paused.exchange(false)) TheClient().resumed = true;
}

std::vector<MatchmakingRemotePeer> MatchmakingPeers() {
  std::lock_guard guard(TheClient().lock);
  return TheClient().peers;
}

uint64_t MatchmakingSelfXuid() { return TheClient().selfXuid; }

bool MatchmakingRelaySend(std::span<const uint64_t> targets, std::span<const uint8_t> data) {
  auto& client = TheClient();
  if (!client.joined || data.empty() || data.size() > MATCHMAKING_RELAY_MAX_BYTES ||
      targets.size() > MATCHMAKING_RELAY_MAX_TARGETS)
    return false;
  std::lock_guard guard(client.lock);
  if (client.outgoing.size() >= kRelayQueueLength) return false;
  client.outgoing.emplace_back(std::vector<uint64_t>(targets.begin(), targets.end()),
                               std::vector<uint8_t>(data.begin(), data.end()));
  return true;
}

std::optional<RelayedPayload> MatchmakingRelayReceive() {
  auto& client = TheClient();
  std::lock_guard guard(client.lock);
  if (client.incoming.empty()) return std::nullopt;
  RelayedPayload payload = std::move(client.incoming.front());
  client.incoming.pop_front();
  return payload;
}
