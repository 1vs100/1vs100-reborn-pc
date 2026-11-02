// Guest DNS answers madserver.net with the gameserver and other names via host.
// Sockets: guest_sockets.cpp; HTTP: gameserver_xhttp.cpp.
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xevent.h>
#include <rex/system/xthread.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "online_service.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace {
using rex::memory::store_and_swap;

constexpr uint32_t kWsaNetDown = 10050;
constexpr uint32_t kWsaInvalidArgument = 10022;
constexpr uint32_t kWsaHostNotFound = 11001;
// XNDNS: s32 status, u32 address count, then up to eight IPv4 addresses in network order.
constexpr uint32_t kDnsStatus = 0, kDnsCount = 4, kDnsAddresses = 8, kDnsMaxAddresses = 8;
constexpr uint32_t kDnsSize = kDnsAddresses + 4 * kDnsMaxAddresses;
constexpr std::string_view kMassiveDomain = "madserver.net";

bool IsMassiveHost(std::string_view host) {
  return host == kMassiveDomain ||
         (host.size() > kMassiveDomain.size() && host.ends_with(kMassiveDomain) &&
          host[host.size() - kMassiveDomain.size() - 1] == '.');
}

// IPv4 addresses, most significant byte first.
std::vector<uint32_t> Resolve(const std::string& host) {
  if (IsMassiveHost(host)) return {CurrentGameserverAddress().value};
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* result = nullptr;
  std::vector<uint32_t> addresses;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0) return addresses;
  for (const addrinfo* entry = result; entry && addresses.size() < kDnsMaxAddresses; entry = entry->ai_next)
    addresses.push_back(ntohl(reinterpret_cast<const sockaddr_in*>(entry->ai_addr)->sin_addr.s_addr));
  freeaddrinfo(result);
  return addresses;
}
}  // namespace

// XNetDnsLookup(caller, name, event, result): the result comes from the system
// heap, which the runtime's XNetDnsRelease frees.
REX_HOOK_RAW(__imp__NetDll_XNetDnsLookup) {
  const uint32_t resultAddress = ctx.r6.u32;
  if (CurrentOnlineService() != OnlineService::Gameserver) {
    if (resultAddress) store_and_swap<uint32_t>(base + resultAddress, 0);
    ctx.r3.u64 = kWsaNetDown;
    return;
  }
  if (!ctx.r4.u32) { ctx.r3.u64 = kWsaInvalidArgument; return; }
  const std::string host(reinterpret_cast<const char*>(base + ctx.r4.u32));
  const auto addresses = Resolve(host);
  REXKRNL_INFO("DNS {} -> {} address(es)", host, addresses.size());
  if (resultAddress) {
    const uint32_t dns = REX_KERNEL_MEMORY()->SystemHeapAlloc(kDnsSize);
    std::memset(base + dns, 0, kDnsSize);
    store_and_swap<uint32_t>(base + dns + kDnsStatus, addresses.empty() ? kWsaHostNotFound : 0);
    store_and_swap<uint32_t>(base + dns + kDnsCount, static_cast<uint32_t>(addresses.size()));
    for (size_t i = 0; i < addresses.size(); ++i)
      store_and_swap<uint32_t>(base + dns + kDnsAddresses + 4 * static_cast<uint32_t>(i), addresses[i]);
    store_and_swap<uint32_t>(base + resultAddress, dns);
  }
  if (const uint32_t event = ctx.r5.u32)
    if (auto ev = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XEvent>(event)) ev->Set(0, false);
  ctx.r3.u64 = 0;
}
