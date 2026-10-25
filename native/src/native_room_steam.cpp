#include "native_room.h"
#include "room_clock.h"
#include <steam/steam_api.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

// Native room transport for the offline build's opt-in multiplayer test modes
// (ONEVS100_ROOM_MODE = local-host | local-join | steam-host | steam-join).
//
// One host, up to kMaxPeers clients, reliable ordered messages. A worker
// thread owns all Steam calls; the game's Lua thread only touches the queues.
//
// Lua-facing operations (NativeRoomOperation):
//   mode            -> the configured mode ("" when disabled)
//   status          -> host: "host:<peers>" / client: "connecting", "connected",
//                      "disconnected:<reason>" / "error:<reason>"
//   sendto  "<peer>|<payload>"  host: peer>0 one client, 0 every client;
//                               client: peer is ignored (always the host)
//   recv            -> "" or "<peer>|<kind>|<payload>"; kind m=message,
//                      j=connected (payload: remote identity), l=left (reason).
//                      A client reports the host as peer 0.
//   utc / utcms     -> this machine's UTC clock (ISO-8601 with ms / epoch ms)
//   self            -> "steam:<SteamID64>" or "local"
//   party           -> Steam modes: "<name>|<name>|..." of the party (Steam lobby)
//   invite          -> Steam modes: open the Steam overlay invite dialog for the party
//
// Parties (Steam modes): the host creates a friends-only Steam lobby of up to
// ONEVS100_PARTY_SIZE players (default 4) and accepts room connections only
// from its members. A joining copy uses ONEVS100_ROOM_PEER (host SteamID64) or
// ONEVS100_ROOM_LOBBY when given; otherwise it joins the first Steam friend
// found hosting a 1 vs 100 party, or a party whose Steam invite it accepts.
//   send / poll     -> legacy single-stream forms used by native_room_test:
//                      send = sendto 0, poll = next message payload only.
namespace {
constexpr size_t kLimit = 65536;
constexpr size_t kQueueLimit = 1024;
constexpr int kMaxPeers = 15;  // local test rooms; Steam rooms are capped by the party size
std::string Env(const char* name) { auto* value = std::getenv(name); return value ? value : ""; }


struct Outgoing { int peer; std::string payload; };
struct Incoming { int peer; char kind; std::string payload; };

class Room {
 public:
  std::mutex lock;
  std::deque<Outgoing> outgoing;
  std::deque<Incoming> incoming;
  std::string status = "disabled";
  std::string self = "local";
  std::jthread worker;
  // Worker-thread state.
  bool host = false, local = false;
  HSteamListenSocket listener = k_HSteamListenSocket_Invalid;
  HSteamNetPollGroup group = k_HSteamNetPollGroup_Invalid;
  HSteamNetConnection server = k_HSteamNetConnection_Invalid;  // client side
  bool serverConnected = false;
  std::map<HSteamNetConnection, int> peers;  // host side
  std::set<int> connectedPeers;
  int nextPeer = 1;
  std::set<uint64> allowed;
  uint64 hostId = 0;
  std::chrono::steady_clock::time_point retryAt{};
  // Party (Steam lobby) state, worker thread.
  CSteamID lobby;
  int partySize = 4;
  bool searching = false;
  std::set<uint64> rejectedLobbies;
  std::chrono::steady_clock::time_point scanAt{};
  std::atomic<bool> inviteRequested{false};
  std::string partyNames;  // guarded by lock
  CCallResult<Room, LobbyCreated_t> lobbyCreated;
  CCallResult<Room, LobbyEnter_t> lobbyEntered;
  void OnLobbyCreated(LobbyCreated_t* result, bool failure);
  void OnLobbyEntered(LobbyEnter_t* result, bool failure);

  STEAM_CALLBACK(Room, Changed, SteamNetConnectionStatusChangedCallback_t);
  STEAM_CALLBACK(Room, JoinRequested, GameLobbyJoinRequested_t);
  STEAM_CALLBACK(Room, PartyChanged, LobbyChatUpdate_t);
  Room() {
    if (!Env("ONEVS100_ROOM_MODE").empty())
      worker = std::jthread([this](std::stop_token stop) { Run(stop); });
  }
  ~Room() { if (worker.joinable()) { worker.request_stop(); worker.join(); } }
  void SetStatus(std::string value) { std::lock_guard guard(lock); status = std::move(value); }
  void Push(int peer, char kind, std::string payload) {
    std::lock_guard guard(lock);
    if (incoming.size() < kQueueLimit) incoming.push_back({peer, kind, std::move(payload)});
    else status = "error:receive-queue";
  }
  void HostStatus() { SetStatus("host:" + std::to_string(connectedPeers.size())); }
  static std::string Identity(const SteamNetConnectionInfo_t& info) {
    if (info.m_identityRemote.GetSteamID64()) return "steam:" + std::to_string(info.m_identityRemote.GetSteamID64());
    char address[SteamNetworkingIPAddr::k_cchMaxString] = {};
    info.m_addrRemote.ToString(address, sizeof(address), true);
    return std::string("ip:") + address;
  }
  bool IsPartyMember(uint64 id) {
    if (!lobby.IsValid()) return false;
    auto* matchmaking = SteamMatchmaking();
    for (int i = 0, n = matchmaking->GetNumLobbyMembers(lobby); i < n; ++i)
      if (matchmaking->GetLobbyMemberByIndex(lobby, i).ConvertToUint64() == id) return true;
    return false;
  }
  void UpdateParty() {
    std::string names;
    if (lobby.IsValid()) {
      auto* matchmaking = SteamMatchmaking();
      for (int i = 0, n = matchmaking->GetNumLobbyMembers(lobby); i < n; ++i) {
        const char* name = SteamFriends()->GetFriendPersonaName(matchmaking->GetLobbyMemberByIndex(lobby, i));
        if (!names.empty()) names += "|";
        names += name ? name : "?";
      }
    }
    std::lock_guard guard(lock); partyNames = names;
  }
  void JoinParty(CSteamID id) {
    if (!id.IsValid() || rejectedLobbies.count(id.ConvertToUint64())) return;
    searching = false;
    SetStatus("joining-party");
    lobbyEntered.Set(SteamMatchmaking()->JoinLobby(id), this, &Room::OnLobbyEntered);
  }
  // Steam friends currently in a joinable lobby of this app (480) are candidates.
  void ScanFriends() {
    auto* friends = SteamFriends();
    for (int i = 0, n = friends->GetFriendCount(k_EFriendFlagImmediate); i < n; ++i) {
      FriendGameInfo_t info{};
      const CSteamID id = friends->GetFriendByIndex(i, k_EFriendFlagImmediate);
      if (friends->GetFriendGamePlayed(id, &info) && info.m_gameID.AppID() == 480 && info.m_steamIDLobby.IsValid() &&
          !rejectedLobbies.count(info.m_steamIDLobby.ConvertToUint64())) {
        JoinParty(info.m_steamIDLobby);
        return;
      }
    }
  }
  void Connect() {
    auto* sockets = SteamNetworkingSockets();
    if (local) {
      SteamNetworkingIPAddr address;
      address.Clear(); address.SetIPv4(0x7f000001, 27100);
      server = sockets->ConnectByIPAddress(address, 0, nullptr);
    } else {
      SteamNetworkingIdentity identity; identity.SetSteamID64(hostId);
      server = sockets->ConnectP2P(identity, 100, 0, nullptr);
    }
    serverConnected = false;
    SetStatus(server ? "connecting" : "error:connect");
  }
  void Run(std::stop_token stop) {
    auto mode = Env("ONEVS100_ROOM_MODE");
    if (mode != "local-host" && mode != "local-join" && mode != "steam-host" && mode != "steam-join") { SetStatus("error:invalid-mode"); return; }
    host = mode == "local-host" || mode == "steam-host";
    local = mode.starts_with("local-");
    SteamErrMsg error;
    if (SteamAPI_InitEx(&error) != k_ESteamAPIInitResult_OK) { SetStatus(std::string("error:") + error); return; }
    if (SteamUtils()->GetAppID() != 480) { SetStatus("error:appid"); SteamAPI_Shutdown(); return; }
    { std::lock_guard guard(lock); self = "steam:" + std::to_string(SteamUser()->GetSteamID().ConvertToUint64()); }
    if (const int size = std::atoi(Env("ONEVS100_PARTY_SIZE").c_str()); size >= 2 && size <= 16) partySize = size;
    auto* sockets = SteamNetworkingSockets();
    if (!local) SteamNetworkingUtils()->InitRelayNetworkAccess();
    if (host) {
      // Optional allowlist for internet hosting: ONEVS100_ROOM_ALLOW=id,id,...
      std::stringstream list(Env("ONEVS100_ROOM_ALLOW"));
      for (std::string item; std::getline(list, item, ',');)
        if (auto id = std::strtoull(item.c_str(), nullptr, 10)) allowed.insert(id);
      group = sockets->CreatePollGroup();
      if (local) {
        SteamNetworkingIPAddr address;
        address.Clear(); address.SetIPv4(0x7f000001, 27100);
        listener = sockets->CreateListenSocketIP(address, 0, nullptr);
      } else {
        listener = sockets->CreateListenSocketP2P(100, 0, nullptr);
      }
      if (!listener || !group) { SetStatus("error:open"); SteamAPI_Shutdown(); return; }
      HostStatus();
      if (!local) lobbyCreated.Set(SteamMatchmaking()->CreateLobby(k_ELobbyTypeFriendsOnly, partySize), this, &Room::OnLobbyCreated);
    } else {
      if (local) Connect();
      else if ((hostId = std::strtoull(Env("ONEVS100_ROOM_PEER").c_str(), nullptr, 10))) Connect();
      else if (auto lobbyId = std::strtoull(Env("ONEVS100_ROOM_LOBBY").c_str(), nullptr, 10)) JoinParty(CSteamID(static_cast<uint64>(lobbyId)));
      else { searching = true; SetStatus("searching"); }
    }
    while (!stop.stop_requested()) {
      SteamAPI_RunCallbacks();
      std::deque<Outgoing> sending;
      { std::lock_guard guard(lock); sending.swap(outgoing); }
      for (auto& item : sending) {
        auto send = [&](HSteamNetConnection connection) {
          sockets->SendMessageToConnection(connection, item.payload.data(), static_cast<uint32>(item.payload.size()),
                                           k_nSteamNetworkingSend_Reliable, nullptr);
        };
        if (!host) { if (serverConnected) send(server); continue; }
        for (auto& [connection, peer] : peers)
          if (connectedPeers.count(peer) && (item.peer == 0 || item.peer == peer)) send(connection);
      }
      SteamNetworkingMessage_t* messages[32];
      int count = host ? sockets->ReceiveMessagesOnPollGroup(group, messages, 32)
                       : (server ? sockets->ReceiveMessagesOnConnection(server, messages, 32) : 0);
      for (int i = 0; i < count; ++i) {
        auto* message = messages[i];
        int peer = 0;
        if (host) { auto found = peers.find(message->m_conn); peer = found == peers.end() ? -1 : found->second; }
        if (peer >= 0 && message->m_cbSize > 0 && message->m_cbSize <= static_cast<int>(kLimit))
          Push(peer, 'm', std::string(static_cast<const char*>(message->m_pData), message->m_cbSize));
        message->Release();
      }
      const auto clock = std::chrono::steady_clock::now();
      // A client keeps retrying the host every 3 seconds after a disconnect.
      if (!host && !server && (local || hostId) && clock >= retryAt) Connect();
      // Without a host yet, look for a friend's party every 5 seconds.
      if (!host && searching && clock >= scanAt) { scanAt = clock + std::chrono::seconds(5); ScanFriends(); }
      if (inviteRequested.exchange(false) && lobby.IsValid()) SteamFriends()->ActivateGameOverlayInviteDialog(lobby);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (server) sockets->CloseConnection(server, 0, "Room stopped", false);
    for (auto& [connection, peer] : peers) sockets->CloseConnection(connection, 0, "Room stopped", false);
    if (listener) sockets->CloseListenSocket(listener);
    if (group) sockets->DestroyPollGroup(group);
    if (lobby.IsValid()) SteamMatchmaking()->LeaveLobby(lobby);
    SteamAPI_Shutdown();
  }
};

void Room::Changed(SteamNetConnectionStatusChangedCallback_t* event) {
  auto* sockets = SteamNetworkingSockets();
  const auto state = event->m_info.m_eState;
  const bool closed = state == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                      state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally;
  if (host) {
    if (state == k_ESteamNetworkingConnectionState_Connecting && event->m_info.m_hListenSocket) {
      const uint64 remote = event->m_info.m_identityRemote.GetSteamID64();
      // Steam rooms: only members of this party (or the explicit allowlist), and
      // no more than the party size; local test rooms: anyone on this PC.
      const bool member = local || allowed.count(remote) || (allowed.empty() && IsPartyMember(remote));
      const int limit = local ? kMaxPeers : partySize - 1;
      if (static_cast<int>(peers.size()) >= limit || !member) {
        sockets->CloseConnection(event->m_hConn, 0, "Room full or not allowed", false); return;
      }
      if (sockets->AcceptConnection(event->m_hConn) != k_EResultOK) { sockets->CloseConnection(event->m_hConn, 0, "Accept failed", false); return; }
      sockets->SetConnectionPollGroup(event->m_hConn, group);
      peers[event->m_hConn] = nextPeer++;
      return;
    }
    auto found = peers.find(event->m_hConn);
    if (found == peers.end()) return;
    const int peer = found->second;
    if (state == k_ESteamNetworkingConnectionState_Connected) {
      connectedPeers.insert(peer); HostStatus();
      Push(peer, 'j', Identity(event->m_info));
    } else if (closed || state == k_ESteamNetworkingConnectionState_None) {
      const bool wasConnected = connectedPeers.erase(peer) > 0;
      sockets->CloseConnection(event->m_hConn, 0, nullptr, false);
      peers.erase(found); HostStatus();
      if (wasConnected) Push(peer, 'l', event->m_info.m_szEndDebug);
    }
    return;
  }
  if (event->m_hConn != server) return;
  if (state == k_ESteamNetworkingConnectionState_Connected) {
    serverConnected = true; SetStatus("connected");
    Push(0, 'j', Identity(event->m_info));
  } else if (closed) {
    const bool wasConnected = serverConnected;
    sockets->CloseConnection(server, 0, nullptr, false);
    server = k_HSteamNetConnection_Invalid; serverConnected = false;
    retryAt = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    SetStatus(std::string("disconnected:") + event->m_info.m_szEndDebug);
    if (wasConnected) Push(0, 'l', event->m_info.m_szEndDebug);
  }
}

void Room::OnLobbyCreated(LobbyCreated_t* result, bool failure) {
  if (failure || result->m_eResult != k_EResultOK) { SetStatus("error:party-create"); return; }
  lobby = CSteamID(static_cast<uint64>(result->m_ulSteamIDLobby));
  auto* matchmaking = SteamMatchmaking();
  matchmaking->SetLobbyData(lobby, "onevs100", "1");
  matchmaking->SetLobbyData(lobby, "host", std::to_string(SteamUser()->GetSteamID().ConvertToUint64()).c_str());
  SteamFriends()->SetRichPresence("status", "Hosting a 1 vs 100 party");
  UpdateParty(); HostStatus();
}
void Room::OnLobbyEntered(LobbyEnter_t* result, bool failure) {
  const CSteamID entered(static_cast<uint64>(result->m_ulSteamIDLobby));
  const char* tag = failure ? "" : SteamMatchmaking()->GetLobbyData(entered, "onevs100");
  if (failure || result->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess || !tag || std::string(tag) != "1") {
    // Not a 1 vs 100 party, or full: forget it and keep looking.
    rejectedLobbies.insert(entered.ConvertToUint64());
    if (!failure && result->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess) SteamMatchmaking()->LeaveLobby(entered);
    searching = true; SetStatus(result->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseFull ? "party-full" : "searching");
    return;
  }
  lobby = entered;
  hostId = SteamMatchmaking()->GetLobbyOwner(lobby).ConvertToUint64();
  UpdateParty();
  Connect();
}
void Room::JoinRequested(GameLobbyJoinRequested_t* event) {
  // The player accepted a Steam invite (overlay or friends list) while running.
  if (host || lobby.IsValid()) return;
  rejectedLobbies.erase(event->m_steamIDLobby.ConvertToUint64());
  JoinParty(event->m_steamIDLobby);
}
void Room::PartyChanged(LobbyChatUpdate_t* event) {
  if (lobby.IsValid() && event->m_ulSteamIDLobby == lobby.ConvertToUint64()) UpdateParty();
}

Room& TheRoom() { static Room room; return room; }
// Steam normally starts on the room's first use (a few seconds after launch),
// after SDL has opened the controller. Starting it during static
// initialization (15:47 build) let Steam Input take the USB DualSense for
// App 480 before SDL saw it: every Steam-mode log after that showed "SDL input
// driver initialized" but no "OnControllerDeviceAdded". Early start (for the
// Steam overlay's invite dialog) is now opt-in only: ONEVS100_EARLY_STEAM=1.
const bool kEarlySteam = [] {
  if (Env("ONEVS100_EARLY_STEAM") == "1" && Env("ONEVS100_ROOM_MODE").starts_with("steam-")) TheRoom();
  return true;
}();
}  // namespace

std::string NativeRoomOperation(const std::string& operation, const std::string& value) {
  if (operation == "utc") return UtcIso();
  if (operation == "utcms") return std::to_string(UtcMs());
  auto& room = TheRoom();  // first use starts the worker in room modes
  if (operation == "mode") return Env("ONEVS100_ROOM_MODE");
  std::lock_guard guard(room.lock);
  if (operation == "status") return room.status;
  if (operation == "self") return room.self;
  if (operation == "party") return room.partyNames;
  if (operation == "invite") { room.inviteRequested = true; return "ok"; }
  if (operation == "sendto" || operation == "send") {
    int peer = 0;
    std::string payload = value;
    if (operation == "sendto") {
      const auto bar = value.find('|');
      if (bar == std::string::npos || bar > 6) return "error:format";
      peer = std::atoi(value.substr(0, bar).c_str());
      payload = value.substr(bar + 1);
    }
    if (room.status.starts_with("error") || room.status == "disabled") return "error:" + room.status;
    if (Env("ONEVS100_ROOM_MODE").ends_with("-join") && room.status != "connected") return "error:not-connected";
    if (payload.empty() || payload.size() > kLimit || room.outgoing.size() >= kQueueLimit) return "error:send-limit";
    room.outgoing.push_back({peer, std::move(payload)});
    return "queued";
  }
  if (operation == "recv") {
    if (room.incoming.empty()) return "";
    auto item = std::move(room.incoming.front()); room.incoming.pop_front();
    return std::to_string(item.peer) + "|" + item.kind + "|" + item.payload;
  }
  if (operation == "poll") {
    while (!room.incoming.empty()) {
      auto item = std::move(room.incoming.front()); room.incoming.pop_front();
      if (item.kind == 'm') return item.payload;
    }
    return "";
  }
  return "error:operation";
}
