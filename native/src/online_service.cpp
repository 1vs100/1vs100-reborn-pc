#include "online_service.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <string>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

REXCVAR_DEFINE_STRING(online_service, "offline", "Online",
                      "offline: shows with bots and Steam parties; gameserver: the 1 vs 100 gameserver");
REXCVAR_DEFINE_STRING(gameserver_host, "", "Online", "Gameserver IPv4 address or host name");
REXCVAR_DEFINE_STRING(guest_bind_address, "", "Online",
                      "Host IPv4 address for guest sockets bound to any address; empty binds to any");

namespace {
OnlineService service = OnlineService::Offline;
GameserverAddress address{};
std::string hostName;
std::optional<GuestBindAddress> bindAddress;

bool ResolveIpv4(const std::string& host, GameserverAddress& out) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  addrinfo* result = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) return false;
  const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
  out.value = ntohl(ipv4->sin_addr.s_addr);
  freeaddrinfo(result);
  return true;
}
}  // namespace

bool LoadOnlineService() {
  const std::string mode = REXCVAR_GET(online_service);
  if (mode == "offline") {
    service = OnlineService::Offline;
    return true;
  }
  if (mode != "gameserver") {
    REXKRNL_ERROR("online_service \"{}\" is neither offline nor gameserver", mode);
    return false;
  }
#ifdef _WIN32
  // Host sockets are used before the title starts Winsock, and from host threads.
  WSADATA winsock{};
  if (const int error = WSAStartup(MAKEWORD(2, 2), &winsock); error != 0) {
    REXKRNL_ERROR("Gameserver mode: WSAStartup failed with error {}", error);
    return false;
  }
#endif
  const std::string host = REXCVAR_GET(gameserver_host);
  if (host.empty() || !ResolveIpv4(host, address)) {
    REXKRNL_ERROR("gameserver_host \"{}\" does not resolve to an IPv4 address", host);
    return false;
  }
  if (const std::string bind = REXCVAR_GET(guest_bind_address); !bind.empty()) {
    in_addr parsed{};
    if (inet_pton(AF_INET, bind.c_str(), &parsed) != 1) {
      REXKRNL_ERROR("guest_bind_address \"{}\" is not an IPv4 address", bind);
      return false;
    }
    bindAddress = GuestBindAddress{ntohl(parsed.s_addr)};
  }
  hostName = host;
  service = OnlineService::Gameserver;
  REXKRNL_INFO("Online service: gameserver at {} ({}.{}.{}.{})", host, address.value >> 24,
               (address.value >> 16) & 0xFF, (address.value >> 8) & 0xFF, address.value & 0xFF);
  return true;
}
OnlineService CurrentOnlineService() { return service; }
GameserverAddress CurrentGameserverAddress() { return address; }
std::optional<GuestBindAddress> CurrentGuestBindAddress() { return bindAddress; }
const std::string& CurrentGameserverHost() { return hostName; }
