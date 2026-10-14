#include <steam/steam_api.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

// Transport diagnostic only. It does not publish game state or modify assets.
class Peer {
 public:
  HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
  uint64 allowed = 0;
  bool connected = false;
  STEAM_CALLBACK(Peer, Changed, SteamNetConnectionStatusChangedCallback_t);
};
void Peer::Changed(SteamNetConnectionStatusChangedCallback_t* event) {
  std::cout << "connection=" << event->m_hConn << " state=" << event->m_info.m_eState
            << " reason=" << event->m_info.m_szEndDebug << std::endl;
  if (event->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting &&
      event->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid) {
    if (event->m_info.m_identityRemote.GetSteamID64() != allowed || connection != k_HSteamNetConnection_Invalid) {
      SteamNetworkingSockets()->CloseConnection(event->m_hConn, 0, "Peer not invited", false);
      return;
    }
    connection = event->m_hConn;
    SteamNetworkingSockets()->AcceptConnection(connection);
  }
  if (event->m_hConn == connection)
    connected = event->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected;
}
int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "Usage: steam_transport_test loopback | udp-loopback | host <invitedSteamID> | join <hostSteamID>\n"; return 2; }
  std::string mode = argv[1];
  const bool local = mode == "loopback" || mode == "udp-loopback";
  if (!local && mode != "host" && mode != "join") return 2;
  uint64 remote = 0;
  if (!local) {
    if (argc != 3) return 2;
    char* end = nullptr;
    remote = std::strtoull(argv[2], &end, 10);
    if (!remote || !end || *end) return 2;
  }
  SteamErrMsg error;
  if (SteamAPI_InitEx(&error) != k_ESteamAPIInitResult_OK) { std::cerr << "Steam init failed: " << error << '\n'; return 3; }
  std::cout << "app=" << SteamUtils()->GetAppID() << " steamid=" << SteamUser()->GetSteamID().ConvertToUint64() << std::endl;
  if (SteamUtils()->GetAppID() != 480) { SteamAPI_Shutdown(); return 4; }
  auto* sockets = SteamNetworkingSockets();
  int result = 5;
  {
    Peer peer;
    peer.allowed = remote;
    HSteamListenSocket listener = k_HSteamListenSocket_Invalid;
    HSteamNetConnection partner = k_HSteamNetConnection_Invalid;
    if (local) {
      peer.connected = sockets->CreateSocketPair(&peer.connection, &partner, mode == "udp-loopback", nullptr, nullptr);
    } else if (mode == "host") {
      SteamNetworkingUtils()->InitRelayNetworkAccess();
      listener = sockets->CreateListenSocketP2P(100, 0, nullptr);
    } else {
      SteamNetworkingUtils()->InitRelayNetworkAccess();
      SteamNetworkingIdentity identity;
      identity.SetSteamID64(remote);
      peer.connection = sockets->ConnectP2P(identity, 100, 0, nullptr);
    }
    const char payload[] = "ONEVS100_TRANSPORT_V1_PING";
    const char reply[] = "ONEVS100_TRANSPORT_V1_PONG";
    const char ack[] = "ONEVS100_TRANSPORT_V1_ACK";
    bool sent = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(local ? 5 : 60);
    while (std::chrono::steady_clock::now() < deadline) {
      SteamAPI_RunCallbacks();
      if (peer.connected && !sent && mode != "host") {
        auto code = sockets->SendMessageToConnection(peer.connection, payload, sizeof(payload), k_nSteamNetworkingSend_Reliable, nullptr);
        std::cout << "send=" << code << std::endl;
        sent = code == k_EResultOK;
      }
      SteamNetworkingMessage_t* message = nullptr;
      auto receiving = local ? partner : peer.connection;
      if (receiving != k_HSteamNetConnection_Invalid && sockets->ReceiveMessagesOnConnection(receiving, &message, 1) == 1) {
        bool ping = message->m_cbSize == sizeof(payload) && !std::memcmp(message->m_pData, payload, sizeof(payload));
        bool pong = message->m_cbSize == sizeof(reply) && !std::memcmp(message->m_pData, reply, sizeof(reply));
        bool acknowledged = message->m_cbSize == sizeof(ack) && !std::memcmp(message->m_pData, ack, sizeof(ack));
        message->Release();
        if (ping && mode != "join") {
          auto code = sockets->SendMessageToConnection(receiving, reply, sizeof(reply), k_nSteamNetworkingSend_Reliable, nullptr);
          if (code != k_EResultOK) { result = 7; break; }
        } else if (pong && mode == "join") {
          auto code = sockets->SendMessageToConnection(receiving, ack, sizeof(ack), k_nSteamNetworkingSend_Reliable, nullptr);
          sockets->FlushMessagesOnConnection(receiving);
          result = code == k_EResultOK ? 0 : 7; break;
        } else if (acknowledged && mode == "host") { result = 0; break; }
        else { result = 6; break; }
      }
      if (local && sockets->ReceiveMessagesOnConnection(peer.connection, &message, 1) == 1) {
        bool valid = message->m_cbSize == sizeof(reply) && !std::memcmp(message->m_pData, reply, sizeof(reply));
        message->Release(); result = valid ? 0 : 6; break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (peer.connection) sockets->CloseConnection(peer.connection, 0, "Test complete", true);
    if (partner) sockets->CloseConnection(partner, 0, "Test complete", true);
    if (listener) sockets->CloseListenSocket(listener);
  }
  SteamAPI_Shutdown();
  std::cout << (result == 0 ? "PASS" : "FAIL") << " transport test; gameplay not tested\n";
  return result;
}
