// Guest Winsock calls in gameserver mode, on host BSD sockets. The runtime's own
// implementation mistranslates addresses, select sets and ioctl codes (see
// docs/gameserver-mode.md). Offline mode denies socket creation and leaves every
// other call to the runtime.
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xevent.h>
#include <rex/system/xthread.h>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <list>
#include <mutex>
#include <thread>
#include <optional>
#include <unordered_map>
#include <vector>
#include "online_service.h"
#include "runtime_export.h"
#ifdef _WIN32
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
using HostSocket = SOCKET;
using HostSockLen = int;
constexpr HostSocket kInvalidHostSocket = INVALID_SOCKET;
constexpr int SHUT_RDWR = SD_BOTH;
constexpr int kHostWouldBlock = WSAEWOULDBLOCK;
// WSAPoll rejects POLLPRI; out-of-band data is reported as POLLRDBAND.
constexpr short kPollUrgent = POLLRDBAND;
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
using HostSocket = int;
using HostSockLen = socklen_t;
constexpr HostSocket kInvalidHostSocket = -1;
constexpr int kHostWouldBlock = EWOULDBLOCK;
constexpr short kPollUrgent = POLLPRI;
#endif

namespace {
using rex::memory::load_and_swap;
using rex::memory::store_and_swap;

constexpr uint32_t kSocketError = 0xFFFFFFFF;
constexpr uint32_t kInvalidGuestSocket = 0xFFFFFFFF;

// Winsock values the guest uses.
namespace wsa {
constexpr uint32_t kWouldBlock = 10035;
constexpr uint32_t kAlready = 10037;
constexpr uint32_t kNotSocket = 10038;
constexpr uint32_t kMsgSize = 10040;
constexpr uint32_t kNoProtocolOption = 10042;
constexpr uint32_t kAfNotSupported = 10047;
constexpr uint32_t kAddressInUse = 10048;
constexpr uint32_t kAddressNotAvailable = 10049;
constexpr uint32_t kNetUnreachable = 10051;
constexpr uint32_t kConnectionAborted = 10053;
constexpr uint32_t kConnectionReset = 10054;
constexpr uint32_t kNoBuffers = 10055;
constexpr uint32_t kIsConnected = 10056;
constexpr uint32_t kNotConnected = 10057;
constexpr uint32_t kShutdown = 10058;
constexpr uint32_t kTimedOut = 10060;
constexpr uint32_t kConnectionRefused = 10061;
constexpr uint32_t kHostUnreachable = 10065;
constexpr uint32_t kInvalidArgument = 10022;
constexpr uint32_t kInterrupted = 10004;
constexpr uint32_t kAccess = 10013;
constexpr uint32_t kNetDown = 10050;
constexpr uint32_t kOperationAborted = 995;
constexpr uint32_t kIoIncomplete = 996;
constexpr uint32_t kIoPending = 997;
constexpr uint32_t kOperationNotSupported = 10045;

constexpr uint32_t kSolSocket = 0xFFFF;
constexpr uint32_t kSoReuseAddr = 0x0004;
constexpr uint32_t kSoKeepAlive = 0x0008;
constexpr uint32_t kSoBroadcast = 0x0020;
constexpr uint32_t kSoLinger = 0x0080;
constexpr uint32_t kSoSendBuffer = 0x1001;
constexpr uint32_t kSoReceiveBuffer = 0x1002;
constexpr uint32_t kSoSendTimeout = 0x1005;
constexpr uint32_t kSoReceiveTimeout = 0x1006;
constexpr uint32_t kSoError = 0x1007;
constexpr uint32_t kSoType = 0x1008;
// Xbox secure-network options; meaningless on host sockets.
constexpr uint32_t kSoMarkInsecure = 0x5801;
constexpr uint32_t kSoPrivate = 0x5802;
constexpr uint32_t kIpProtoTcp = 6;
constexpr uint32_t kTcpNoDelay = 0x0001;

constexpr uint32_t kFionbio = 0x8004667E;
constexpr uint32_t kFionread = 0x4004667F;

constexpr uint16_t kAfInet = 2;
constexpr uint32_t kSockStream = 1;
constexpr uint32_t kSockDgram = 2;
constexpr uint32_t kFdSetSize = 64;
}  // namespace wsa

struct GuestSocketTable {
  std::mutex lock;
  std::unordered_map<uint32_t, HostSocket> sockets;
  uint32_t next = 0;
};
GuestSocketTable& Table() { static GuestSocketTable table; return table; }

// Guest handles stay clear of kernel object handles.
constexpr uint32_t kGuestHandleBase = 0x5C000000;

uint32_t Register(HostSocket host) {
  auto& table = Table();
  std::lock_guard guard(table.lock);
  uint32_t handle;
  do { handle = kGuestHandleBase | ((table.next++ & 0x00FFFFFF) << 2); } while (table.sockets.count(handle));
  table.sockets[handle] = host;
  return handle;
}
std::optional<HostSocket> Lookup(uint32_t handle) {
  auto& table = Table();
  std::lock_guard guard(table.lock);
  const auto it = table.sockets.find(handle);
  if (it == table.sockets.end()) return std::nullopt;
  return it->second;
}
std::optional<HostSocket> Unregister(uint32_t handle) {
  auto& table = Table();
  std::lock_guard guard(table.lock);
  const auto it = table.sockets.find(handle);
  if (it == table.sockets.end()) return std::nullopt;
  const HostSocket host = it->second;
  table.sockets.erase(it);
  return host;
}

uint32_t TranslateHostError(int error) {
#ifdef _WIN32
  return static_cast<uint32_t>(error);  // Winsock codes already.
#else
  switch (error) {
    case EWOULDBLOCK: return wsa::kWouldBlock;
    case EINPROGRESS: return wsa::kWouldBlock;  // Winsock reports a pending connect this way.
    case EALREADY: return wsa::kAlready;
    case ENOTSOCK: return wsa::kNotSocket;
    case EMSGSIZE: return wsa::kMsgSize;
    case ENOPROTOOPT: return wsa::kNoProtocolOption;
    case EAFNOSUPPORT: return wsa::kAfNotSupported;
    case EADDRINUSE: return wsa::kAddressInUse;
    case EADDRNOTAVAIL: return wsa::kAddressNotAvailable;
    case ENETUNREACH: return wsa::kNetUnreachable;
    case ECONNABORTED: return wsa::kConnectionAborted;
    case ECONNRESET: return wsa::kConnectionReset;
    case EPIPE: return wsa::kConnectionReset;
    case ENOBUFS: return wsa::kNoBuffers;
    case EISCONN: return wsa::kIsConnected;
    case ENOTCONN: return wsa::kNotConnected;
    case ESHUTDOWN: return wsa::kShutdown;
    case ETIMEDOUT: return wsa::kTimedOut;
    case ECONNREFUSED: return wsa::kConnectionRefused;
    case EHOSTUNREACH: return wsa::kHostUnreachable;
    case EOPNOTSUPP: return wsa::kOperationNotSupported;
    case EINTR: return wsa::kInterrupted;
    case ENETDOWN: return wsa::kNetDown;
    case EACCES: return wsa::kAccess;
    default: return wsa::kInvalidArgument;
  }
#endif
}
int LastHostError() {
#ifdef _WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}
bool WouldBlock(int error) {
#ifdef _WIN32
  return error == WSAEWOULDBLOCK;
#else
  return error == EWOULDBLOCK || error == EAGAIN;
#endif
}
int PollHost(pollfd* polls, size_t count, int timeoutMs) {
#ifdef _WIN32
  // WSAPoll rejects an empty set.
  if (!count) { Sleep(static_cast<DWORD>(std::max(timeoutMs, 0))); return 0; }
  return WSAPoll(polls, static_cast<ULONG>(count), timeoutMs);
#else
  return ::poll(polls, count, timeoutMs);
#endif
}
void Fail(PPCContext& ctx, uint32_t wsaError) {
  rex::system::XThread::SetLastError(wsaError);
  ctx.r3.u64 = kSocketError;
}
void FailFromHost(PPCContext& ctx) { Fail(ctx, TranslateHostError(LastHostError())); }
void Succeed(PPCContext& ctx, uint32_t value) { ctx.r3.u64 = value; }

// Guest sockaddr_in: u16 family (big-endian), then port and address in network order.
std::optional<sockaddr_in> ReadSockaddr(uint8_t* base, uint32_t address, uint32_t length) {
  if (!address || length < 8 || load_and_swap<uint16_t>(base + address) != wsa::kAfInet) return std::nullopt;
  sockaddr_in host{};
  host.sin_family = AF_INET;
  std::memcpy(&host.sin_port, base + address + 2, 2);
  std::memcpy(&host.sin_addr, base + address + 4, 4);
  return host;
}
// Writes when the guest gave room; lengthAddress holds a big-endian length.
void WriteSockaddr(uint8_t* base, uint32_t address, uint32_t lengthAddress, const sockaddr_in& host) {
  if (!address) return;
  if (lengthAddress && load_and_swap<uint32_t>(base + lengthAddress) < 16) return;
  std::memset(base + address, 0, 16);
  store_and_swap<uint16_t>(base + address, wsa::kAfInet);
  std::memcpy(base + address + 2, &host.sin_port, 2);
  std::memcpy(base + address + 4, &host.sin_addr, 4);
  if (lengthAddress) store_and_swap<uint32_t>(base + lengthAddress, 16);
}

int SendFlags(uint32_t guestFlags) {
#ifdef _WIN32
  return static_cast<int>(guestFlags);
#else
  return static_cast<int>(guestFlags) | MSG_NOSIGNAL;  // A broken stream reports an error, not SIGPIPE.
#endif
}

void CloseHost(HostSocket host) {
#ifdef _WIN32
  closesocket(host);
#else
  close(host);
#endif
}

bool Gameserver() { return CurrentOnlineService() == OnlineService::Gameserver; }

// Completes every pending overlapped request on the socket as aborted.
void CancelPending(uint8_t* base, uint32_t handle);
}  // namespace

#define OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(name)                \
  if (!Gameserver()) {                                           \
    static PPCFunc* const original = RuntimeExport(#name);       \
    original(ctx, base);                                         \
    return;                                                      \
  }
#define HOST_SOCKET_OR_FAIL(var, reg)                            \
  const auto var##Lookup = Lookup(ctx.reg.u32);                  \
  if (!var##Lookup) { Fail(ctx, wsa::kNotSocket); return; }      \
  const HostSocket var = *var##Lookup

// socket(caller, af, type, protocol)
REX_HOOK_RAW(__imp__NetDll_socket) {
  if (!Gameserver()) {
    REXKRNL_WARN("Offline restoration: denied guest socket creation");
    rex::system::XThread::SetLastError(wsa::kNetDown);
    ctx.r3.u64 = kInvalidGuestSocket;
    return;
  }
  const uint32_t af = ctx.r4.u32, type = ctx.r5.u32, protocol = ctx.r6.u32;
  if (af != wsa::kAfInet || (type != wsa::kSockStream && type != wsa::kSockDgram)) {
    rex::system::XThread::SetLastError(wsa::kAfNotSupported);
    ctx.r3.u64 = kInvalidGuestSocket;
    return;
  }
  // Protocol 0 picks the default; VDP (254) is UDP carrying game data.
  const int hostProtocol = protocol == 0 ? 0 : (type == wsa::kSockStream ? IPPROTO_TCP : IPPROTO_UDP);
  const HostSocket host = ::socket(AF_INET, type == wsa::kSockStream ? SOCK_STREAM : SOCK_DGRAM, hostProtocol);
  if (host == kInvalidHostSocket) {
    rex::system::XThread::SetLastError(TranslateHostError(LastHostError()));
    ctx.r3.u64 = kInvalidGuestSocket;
    return;
  }
#ifdef _WIN32
  // Otherwise an ICMP port unreachable fails the socket's next receive.
  if (type == wsa::kSockDgram) {
    BOOL report = FALSE;
    DWORD returned = 0;
    WSAIoctl(host, SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0, &returned, nullptr, nullptr);
  }
#endif
  ctx.r3.u64 = Register(host);
  REXKRNL_DEBUG("Guest socket {:08X} type={} protocol={}", ctx.r3.u32, type, protocol);
}

// closesocket(caller, s)
REX_HOOK_RAW(__imp__NetDll_closesocket) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_closesocket);
  const auto host = Unregister(ctx.r4.u32);
  if (!host) { Fail(ctx, wsa::kNotSocket); return; }
  CancelPending(base, ctx.r4.u32);
  // close() alone leaves a call blocked on this socket in another thread waiting.
  ::shutdown(*host, SHUT_RDWR);
  CloseHost(*host);
  Succeed(ctx, 0);
}

// shutdown(caller, s, how): SD_RECEIVE 0, SD_SEND 1, SD_BOTH 2, as on the host.
REX_HOOK_RAW(__imp__NetDll_shutdown) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_shutdown);
  HOST_SOCKET_OR_FAIL(host, r4);
  if (::shutdown(host, static_cast<int>(ctx.r5.u32)) != 0) { FailFromHost(ctx); return; }
  Succeed(ctx, 0);
}

// ioctlsocket(caller, s, cmd, argp)
REX_HOOK_RAW(__imp__NetDll_ioctlsocket) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_ioctlsocket);
  HOST_SOCKET_OR_FAIL(host, r4);
  const uint32_t command = ctx.r5.u32, argument = ctx.r6.u32;
  if (!argument) { Fail(ctx, wsa::kInvalidArgument); return; }
  if (command == wsa::kFionbio) {
    const bool nonBlocking = load_and_swap<uint32_t>(base + argument) != 0;
#ifdef _WIN32
    u_long mode = nonBlocking ? 1 : 0;
    if (::ioctlsocket(host, FIONBIO, &mode) != 0) { FailFromHost(ctx); return; }
#else
    const int flags = fcntl(host, F_GETFL, 0);
    if (flags < 0 || fcntl(host, F_SETFL, nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK)) != 0) {
      FailFromHost(ctx);
      return;
    }
#endif
    Succeed(ctx, 0);
    return;
  }
  if (command == wsa::kFionread) {
#ifdef _WIN32
    u_long available = 0;
    if (::ioctlsocket(host, FIONREAD, &available) != 0) { FailFromHost(ctx); return; }
#else
    int available = 0;
    if (::ioctl(host, FIONREAD, &available) != 0) { FailFromHost(ctx); return; }
#endif
    store_and_swap<uint32_t>(base + argument, static_cast<uint32_t>(available));
    Succeed(ctx, 0);
    return;
  }
  REXKRNL_WARN("Guest ioctlsocket command {:08X} not supported", command);
  Fail(ctx, wsa::kInvalidArgument);
}

// setsockopt(caller, s, level, optname, optval, optlen)
REX_HOOK_RAW(__imp__NetDll_setsockopt) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_setsockopt);
  HOST_SOCKET_OR_FAIL(host, r4);
  const uint32_t level = ctx.r5.u32, name = ctx.r6.u32, value = ctx.r7.u32, length = ctx.r8.u32;
  if (level == wsa::kSolSocket && (name == wsa::kSoMarkInsecure || name == wsa::kSoPrivate)) {
    Succeed(ctx, 0);
    return;
  }
  if (!value || length < 4) { Fail(ctx, wsa::kInvalidArgument); return; }
  const uint32_t guestValue = load_and_swap<uint32_t>(base + value);
  int hostLevel = 0, hostName = 0;
  int intValue = static_cast<int>(guestValue);
  const void* hostValue = &intValue;
  HostSockLen hostLength = sizeof(intValue);
#ifndef _WIN32
  timeval timeout{};
#endif
  linger lingerValue{};
  if (level == wsa::kSolSocket) {
    hostLevel = SOL_SOCKET;
    switch (name) {
      case wsa::kSoReuseAddr: hostName = SO_REUSEADDR; break;
      case wsa::kSoKeepAlive: hostName = SO_KEEPALIVE; break;
      case wsa::kSoBroadcast: hostName = SO_BROADCAST; break;
      case wsa::kSoSendBuffer: hostName = SO_SNDBUF; break;
      case wsa::kSoReceiveBuffer: hostName = SO_RCVBUF; break;
      case wsa::kSoLinger:
        // Winsock linger: two u16 fields, onoff then seconds.
        hostName = SO_LINGER;
        lingerValue.l_onoff = load_and_swap<uint16_t>(base + value);
        lingerValue.l_linger = load_and_swap<uint16_t>(base + value + 2);
        hostValue = &lingerValue;
        hostLength = sizeof(lingerValue);
        break;
      case wsa::kSoSendTimeout:
      case wsa::kSoReceiveTimeout:
        hostName = name == wsa::kSoSendTimeout ? SO_SNDTIMEO : SO_RCVTIMEO;
#ifndef _WIN32
        // Winsock takes milliseconds.
        timeout.tv_sec = guestValue / 1000;
        timeout.tv_usec = (guestValue % 1000) * 1000;
        hostValue = &timeout;
        hostLength = sizeof(timeout);
#endif
        break;
      default: break;
    }
  } else if (level == wsa::kIpProtoTcp && name == wsa::kTcpNoDelay) {
    hostLevel = IPPROTO_TCP;
    hostName = TCP_NODELAY;
  }
  if (!hostName) {
    REXKRNL_WARN("Guest setsockopt level {:X} option {:X} not supported", level, name);
    Fail(ctx, wsa::kNoProtocolOption);
    return;
  }
  if (::setsockopt(host, hostLevel, hostName, static_cast<const char*>(hostValue), hostLength) != 0) {
    FailFromHost(ctx);
    return;
  }
  Succeed(ctx, 0);
}

// getsockopt(caller, s, level, optname, optval, optlen*)
REX_HOOK_RAW(__imp__NetDll_getsockopt) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_getsockopt);
  HOST_SOCKET_OR_FAIL(host, r4);
  const uint32_t level = ctx.r5.u32, name = ctx.r6.u32, value = ctx.r7.u32, lengthAddress = ctx.r8.u32;
  if (!value || !lengthAddress || load_and_swap<uint32_t>(base + lengthAddress) < 4) {
    Fail(ctx, wsa::kInvalidArgument);
    return;
  }
  int hostName = 0;
  if (level == wsa::kSolSocket) {
    switch (name) {
      case wsa::kSoError: hostName = SO_ERROR; break;
      case wsa::kSoType: hostName = SO_TYPE; break;
      case wsa::kSoSendBuffer: hostName = SO_SNDBUF; break;
      case wsa::kSoReceiveBuffer: hostName = SO_RCVBUF; break;
      default: break;
    }
  }
  if (!hostName) {
    REXKRNL_WARN("Guest getsockopt level {:X} option {:X} not supported", level, name);
    Fail(ctx, wsa::kNoProtocolOption);
    return;
  }
  int result = 0;
  HostSockLen resultLength = sizeof(result);
  if (::getsockopt(host, SOL_SOCKET, hostName, reinterpret_cast<char*>(&result), &resultLength) != 0) {
    FailFromHost(ctx);
    return;
  }
  uint32_t guestResult = static_cast<uint32_t>(result);
  if (hostName == SO_ERROR && result) guestResult = TranslateHostError(result);
  if (hostName == SO_TYPE) guestResult = result == SOCK_STREAM ? wsa::kSockStream : wsa::kSockDgram;
  store_and_swap<uint32_t>(base + value, guestResult);
  store_and_swap<uint32_t>(base + lengthAddress, 4);
  Succeed(ctx, 0);
}

// bind(caller, s, name, namelen)
REX_HOOK_RAW(__imp__NetDll_bind) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_bind);
  HOST_SOCKET_OR_FAIL(host, r4);
  auto address = ReadSockaddr(base, ctx.r5.u32, ctx.r6.u32);
  if (!address) { Fail(ctx, wsa::kAfNotSupported); return; }
  if (const auto bindAddress = CurrentGuestBindAddress(); bindAddress && address->sin_addr.s_addr == htonl(INADDR_ANY))
    address->sin_addr.s_addr = htonl(bindAddress->value);
  if (::bind(host, reinterpret_cast<const sockaddr*>(&*address), sizeof(*address)) != 0) {
    REXKRNL_WARN("Guest bind to port {} failed, host error {}", ntohs(address->sin_port), LastHostError());
    FailFromHost(ctx);
    return;
  }
  Succeed(ctx, 0);
}

// connect(caller, s, name, namelen)
REX_HOOK_RAW(__imp__NetDll_connect) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_connect);
  HOST_SOCKET_OR_FAIL(host, r4);
  const auto address = ReadSockaddr(base, ctx.r5.u32, ctx.r6.u32);
  if (!address) { Fail(ctx, wsa::kAfNotSupported); return; }
  if (::connect(host, reinterpret_cast<const sockaddr*>(&*address), sizeof(*address)) != 0) { FailFromHost(ctx); return; }
  Succeed(ctx, 0);
}

// listen(caller, s, backlog)
REX_HOOK_RAW(__imp__NetDll_listen) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_listen);
  HOST_SOCKET_OR_FAIL(host, r4);
  if (::listen(host, static_cast<int>(ctx.r5.u32)) != 0) { FailFromHost(ctx); return; }
  Succeed(ctx, 0);
}

// accept(caller, s, addr, addrlen*)
REX_HOOK_RAW(__imp__NetDll_accept) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_accept);
  HOST_SOCKET_OR_FAIL(host, r4);
  sockaddr_in peer{};
  HostSockLen peerLength = sizeof(peer);
  const HostSocket accepted = ::accept(host, reinterpret_cast<sockaddr*>(&peer), &peerLength);
  if (accepted == kInvalidHostSocket) {
    rex::system::XThread::SetLastError(TranslateHostError(LastHostError()));
    ctx.r3.u64 = kInvalidGuestSocket;
    return;
  }
  WriteSockaddr(base, ctx.r5.u32, ctx.r6.u32, peer);
  ctx.r3.u64 = Register(accepted);
}

// recv(caller, s, buf, len, flags)
REX_HOOK_RAW(__imp__NetDll_recv) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_recv);
  HOST_SOCKET_OR_FAIL(host, r4);
  const auto received = ::recv(host, reinterpret_cast<char*>(base + ctx.r5.u32), ctx.r6.u32, static_cast<int>(ctx.r7.u32));
  if (received < 0) { FailFromHost(ctx); return; }
  Succeed(ctx, static_cast<uint32_t>(received));
}

// recvfrom(caller, s, buf, len, flags, from, fromlen*)
REX_HOOK_RAW(__imp__NetDll_recvfrom) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_recvfrom);
  HOST_SOCKET_OR_FAIL(host, r4);
  sockaddr_in from{};
  HostSockLen fromLength = sizeof(from);
  const auto received = ::recvfrom(host, reinterpret_cast<char*>(base + ctx.r5.u32), ctx.r6.u32,
                                   static_cast<int>(ctx.r7.u32), reinterpret_cast<sockaddr*>(&from), &fromLength);
  if (received < 0) { FailFromHost(ctx); return; }
  WriteSockaddr(base, ctx.r8.u32, ctx.r9.u32, from);
  Succeed(ctx, static_cast<uint32_t>(received));
}

// send(caller, s, buf, len, flags)
REX_HOOK_RAW(__imp__NetDll_send) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_send);
  HOST_SOCKET_OR_FAIL(host, r4);
  const auto sent = ::send(host, reinterpret_cast<const char*>(base + ctx.r5.u32), ctx.r6.u32, SendFlags(ctx.r7.u32));
  if (sent < 0) { FailFromHost(ctx); return; }
  Succeed(ctx, static_cast<uint32_t>(sent));
}

// sendto(caller, s, buf, len, flags, to, tolen)
REX_HOOK_RAW(__imp__NetDll_sendto) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_sendto);
  HOST_SOCKET_OR_FAIL(host, r4);
  const auto to = ReadSockaddr(base, ctx.r8.u32, ctx.r9.u32);
  const auto sent = to ? ::sendto(host, reinterpret_cast<const char*>(base + ctx.r5.u32), ctx.r6.u32,
                                  SendFlags(ctx.r7.u32), reinterpret_cast<const sockaddr*>(&*to), sizeof(*to))
                       : ::send(host, reinterpret_cast<const char*>(base + ctx.r5.u32), ctx.r6.u32, SendFlags(ctx.r7.u32));
  if (sent < 0) { FailFromHost(ctx); return; }
  Succeed(ctx, static_cast<uint32_t>(sent));
}

// getsockname(caller, s, name, namelen*) / getpeername
REX_HOOK_RAW(__imp__NetDll_getsockname) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_getsockname);
  HOST_SOCKET_OR_FAIL(host, r4);
  sockaddr_in local{};
  HostSockLen length = sizeof(local);
  if (::getsockname(host, reinterpret_cast<sockaddr*>(&local), &length) != 0) { FailFromHost(ctx); return; }
  WriteSockaddr(base, ctx.r5.u32, ctx.r6.u32, local);
  Succeed(ctx, 0);
}
REX_HOOK_RAW(__imp__NetDll_getpeername) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_getpeername);
  HOST_SOCKET_OR_FAIL(host, r4);
  sockaddr_in peer{};
  HostSockLen length = sizeof(peer);
  if (::getpeername(host, reinterpret_cast<sockaddr*>(&peer), &length) != 0) { FailFromHost(ctx); return; }
  WriteSockaddr(base, ctx.r5.u32, ctx.r6.u32, peer);
  Succeed(ctx, 0);
}

// select(caller, nfds, readfds, writefds, exceptfds, timeout). Guest fd_set:
// u32 count, then up to 64 u32 handles. Guest timeval: s32 seconds, s32 microseconds.
REX_HOOK_RAW(__imp__NetDll_select) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_select);
  const uint32_t sets[3] = {ctx.r5.u32, ctx.r6.u32, ctx.r7.u32};
  const short events[3] = {POLLIN, POLLOUT, kPollUrgent};
  // One poll entry per socket: Wine's WSAPoll loses events when a socket is listed twice.
  struct Entry { uint32_t handle; size_t set; size_t poll; };
  std::vector<pollfd> polls;
  std::vector<Entry> entries;
  for (size_t set = 0; set < 3; ++set) {
    if (!sets[set]) continue;
    const uint32_t count = std::min(load_and_swap<uint32_t>(base + sets[set]), wsa::kFdSetSize);
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t handle = load_and_swap<uint32_t>(base + sets[set] + 4 + 4 * i);
      const auto host = Lookup(handle);
      if (!host) { Fail(ctx, wsa::kNotSocket); return; }
      auto poll = std::find_if(polls.begin(), polls.end(), [&](const pollfd& p) { return p.fd == *host; });
      if (poll == polls.end()) poll = polls.insert(polls.end(), pollfd{*host, 0, 0});
      poll->events |= events[set];
      entries.push_back({handle, set, static_cast<size_t>(poll - polls.begin())});
    }
  }
  int timeoutMs = -1;
  if (const uint32_t timeout = ctx.r8.u32) {
    const int64_t seconds = static_cast<int32_t>(load_and_swap<uint32_t>(base + timeout));
    const int64_t micro = static_cast<int32_t>(load_and_swap<uint32_t>(base + timeout + 4));
    timeoutMs = static_cast<int>(std::clamp<int64_t>(seconds * 1000 + micro / 1000, 0, INT32_MAX));
  }
  const int ready = PollHost(polls.data(), polls.size(), timeoutMs);
  if (ready < 0) { FailFromHost(ctx); return; }
  // Rewrite each set with the ready handles only. As in Winsock, a failed
  // connect is reported in exceptfds and never as writable; an error or hangup
  // makes a socket readable.
  uint32_t total = 0;
  uint32_t kept[3] = {};
  for (const auto& entry : entries) {
    const short revents = polls[entry.poll].revents;
    const size_t set = entry.set;
    bool isReady = false;
    switch (set) {
      case 0: isReady = revents & (POLLIN | POLLERR | POLLHUP); break;
      case 1: isReady = (revents & POLLOUT) && !(revents & POLLERR); break;
      case 2: isReady = revents & (kPollUrgent | POLLERR); break;
    }
    if (!isReady) continue;
    store_and_swap<uint32_t>(base + sets[set] + 4 + 4 * kept[set], entry.handle);
    ++kept[set];
    ++total;
  }
  for (size_t set = 0; set < 3; ++set)
    if (sets[set]) store_and_swap<uint32_t>(base + sets[set], kept[set]);
  Succeed(ctx, total);
}

// Event-driven Winsock calls are not implemented for host sockets; a call
// fails and is logged.
#define UNSUPPORTED_IN_GAMESERVER_MODE(name)                                      \
  REX_HOOK_RAW(__imp__##name) {                                                   \
    OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__##name);                           \
    REXKRNL_ERROR("Gameserver mode: guest " #name " is not supported");          \
    Fail(ctx, wsa::kOperationNotSupported);                                       \
  }
UNSUPPORTED_IN_GAMESERVER_MODE(NetDll_WSAEventSelect)

namespace {
// Guest WSABUF array: {u32 length, u32 buffer} per entry.
struct GuestBuffers {
  std::vector<std::pair<uint32_t, uint32_t>> entries;
  size_t total = 0;
};
GuestBuffers ReadBuffers(uint8_t* base, uint32_t array, uint32_t count) {
  GuestBuffers buffers;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t length = load_and_swap<uint32_t>(base + array + 8 * i);
    const uint32_t buffer = load_and_swap<uint32_t>(base + array + 8 * i + 4);
    buffers.entries.emplace_back(length, buffer);
    buffers.total += length;
  }
  return buffers;
}
bool RejectOverlapped(PPCContext& ctx, const char* name) {
  if (!ctx.r9.u32 && !ctx.r10.u32) return false;
  REXKRNL_ERROR("Gameserver mode: overlapped guest {} is not supported", name);
  Fail(ctx, wsa::kOperationNotSupported);
  return true;
}
}  // namespace

// WSASend(caller, s, buffers, count, bytesSent*, flags, overlapped, completion)
REX_HOOK_RAW(__imp__NetDll_WSASend) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_WSASend);
  HOST_SOCKET_OR_FAIL(host, r4);
  if (RejectOverlapped(ctx, "WSASend")) return;
  const auto buffers = ReadBuffers(base, ctx.r5.u32, ctx.r6.u32);
  std::vector<char> gathered;
  gathered.reserve(buffers.total);
  for (const auto& [length, buffer] : buffers.entries)
    gathered.insert(gathered.end(), base + buffer, base + buffer + length);
  const auto sent = ::send(host, gathered.data(), gathered.size(), SendFlags(ctx.r8.u32));
  if (sent < 0) { FailFromHost(ctx); return; }
  if (ctx.r7.u32) store_and_swap<uint32_t>(base + ctx.r7.u32, static_cast<uint32_t>(sent));
  Succeed(ctx, 0);
}

// WSARecv(caller, s, buffers, count, bytesReceived*, flags*, overlapped, completion)
REX_HOOK_RAW(__imp__NetDll_WSARecv) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_WSARecv);
  HOST_SOCKET_OR_FAIL(host, r4);
  if (RejectOverlapped(ctx, "WSARecv")) return;
  const auto buffers = ReadBuffers(base, ctx.r5.u32, ctx.r6.u32);
  const int flags = ctx.r8.u32 ? static_cast<int>(load_and_swap<uint32_t>(base + ctx.r8.u32)) : 0;
  std::vector<char> received(buffers.total);
  const auto got = ::recv(host, received.data(), received.size(), flags);
  if (got < 0) { FailFromHost(ctx); return; }
  size_t offset = 0;
  for (const auto& [length, buffer] : buffers.entries) {
    if (offset >= static_cast<size_t>(got)) break;
    const size_t part = std::min<size_t>(length, got - offset);
    std::memcpy(base + buffer, received.data() + offset, part);
    offset += part;
  }
  if (ctx.r7.u32) store_and_swap<uint32_t>(base + ctx.r7.u32, static_cast<uint32_t>(got));
  if (ctx.r8.u32) store_and_swap<uint32_t>(base + ctx.r8.u32, 0);
  Succeed(ctx, 0);
}

namespace {
// WSAOVERLAPPED: Internal (status), InternalHigh (bytes), Offset, OffsetHigh, hEvent.
constexpr uint32_t kOverlappedStatus = 0x00, kOverlappedBytes = 0x04, kOverlappedEvent = 0x10;
constexpr uint32_t kStatusPending = 0x103;

uint32_t StackArgument(PPCContext& ctx, uint8_t* base, uint32_t index) {
  return load_and_swap<uint32_t>(base + ctx.r1.u32 + 0x54 + 8 * index);
}

void ResetEvent(uint32_t handle) {
  if (!handle) return;
  if (auto event = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XEvent>(handle)) event->Reset();
}
void SignalEvent(uint32_t handle) {
  if (!handle) return;
  if (auto event = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XEvent>(handle)) event->Set(0, false);
}

struct ReceiveRequest {
  uint32_t socket;
  HostSocket host;
  GuestBuffers buffers;
  uint32_t from, fromLength, overlapped;
};

// Receives into the guest buffers; the host error code on failure.
struct ReceiveResult { int64_t bytes; int error; };
ReceiveResult Receive(uint8_t* base, const ReceiveRequest& request, int flags) {
  std::vector<char> data(std::max<size_t>(request.buffers.total, 1));
  sockaddr_in from{};
  HostSockLen fromLength = sizeof(from);
  const int64_t got = ::recvfrom(request.host, data.data(), static_cast<int>(request.buffers.total), flags,
                                 reinterpret_cast<sockaddr*>(&from), &fromLength);
  if (got < 0) return {got, LastHostError()};
  size_t offset = 0;
  for (const auto& [length, buffer] : request.buffers.entries) {
    if (offset >= static_cast<size_t>(got)) break;
    const size_t part = std::min<size_t>(length, got - offset);
    std::memcpy(base + buffer, data.data() + offset, part);
    offset += part;
  }
  WriteSockaddr(base, request.from, request.fromLength, from);
  return {got, 0};
}
// Receives only data already queued, also on a blocking socket.
ReceiveResult ReceiveQueued(uint8_t* base, const ReceiveRequest& request, int flags) {
#ifdef _WIN32
  // Windows has no MSG_DONTWAIT; the poll stands in for it. A receive on the same
  // socket from another thread between the two calls makes this one block.
  pollfd poll{request.host, POLLIN, 0};
  if (PollHost(&poll, 1, 0) == 0) return {-1, kHostWouldBlock};
  return Receive(base, request, flags);
#else
  return Receive(base, request, flags | MSG_DONTWAIT);
#endif
}

// Finishes an overlapped request and wakes the guest waiting on its event.
void Complete(uint8_t* base, uint32_t overlapped, uint32_t status, uint32_t bytes) {
  store_and_swap<uint32_t>(base + overlapped + kOverlappedBytes, bytes);
  store_and_swap<uint32_t>(base + overlapped + kOverlappedStatus, status);
  SignalEvent(load_and_swap<uint32_t>(base + overlapped + kOverlappedEvent));
}

// Pending overlapped receives, completed by one host thread polling their sockets.
class PendingReceives {
 public:
  void Add(uint8_t* base, ReceiveRequest request) {
    std::lock_guard guard(lock_);
    base_ = base;
    requests_.push_back(std::move(request));
    if (!worker_.joinable()) worker_ = std::thread([this] { Run(); });
    changed_.notify_all();
  }
  void Cancel(uint8_t* base, uint32_t socket) {
    std::lock_guard guard(lock_);
    for (auto it = requests_.begin(); it != requests_.end();) {
      if (it->socket != socket) { ++it; continue; }
      Complete(base, it->overlapped, wsa::kOperationAborted, 0);
      it = requests_.erase(it);
    }
    changed_.notify_all();
  }
  // Waits until the request on `overlapped` is no longer pending.
  void Wait(uint8_t* base, uint32_t overlapped) {
    std::unique_lock guard(lock_);
    changed_.wait(guard, [&] { return load_and_swap<uint32_t>(base + overlapped + kOverlappedStatus) != kStatusPending; });
  }

 private:
  void Run() {
    std::unique_lock guard(lock_);
    for (;;) {
      changed_.wait(guard, [&] { return !requests_.empty(); });
      std::vector<pollfd> polls;
      for (const auto& request : requests_) polls.push_back({request.host, POLLIN, 0});
      guard.unlock();
      PollHost(polls.data(), polls.size(), 20);
      guard.lock();
      // Requests may have been added or cancelled meanwhile; complete the first
      // request of each readable socket that is still pending.
      for (const auto& poll : polls) {
        if (!(poll.revents & (POLLIN | POLLERR | POLLHUP))) continue;
        const auto it = std::find_if(requests_.begin(), requests_.end(), [&](const auto& r) { return r.host == poll.fd; });
        if (it == requests_.end()) continue;
        const auto result = ReceiveQueued(base_, *it, 0);
        if (result.bytes < 0 && WouldBlock(result.error)) continue;
        if (result.bytes < 0) Complete(base_, it->overlapped, TranslateHostError(result.error), 0);
        else Complete(base_, it->overlapped, 0, static_cast<uint32_t>(result.bytes));
        requests_.erase(it);
        changed_.notify_all();
      }
    }
  }

  std::mutex lock_;
  std::condition_variable changed_;
  std::list<ReceiveRequest> requests_;
  std::thread worker_;
  uint8_t* base_ = nullptr;
};
PendingReceives& Pending() { static PendingReceives* pending = new PendingReceives; return *pending; }

void CancelPending(uint8_t* base, uint32_t handle) { Pending().Cancel(base, handle); }
}  // namespace

// WSARecvFrom(caller, s, buffers, count, bytesReceived*, flags*, from, fromlen*, overlapped, completion)
REX_HOOK_RAW(__imp__NetDll_WSARecvFrom) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_WSARecvFrom);
  HOST_SOCKET_OR_FAIL(host, r4);
  const uint32_t overlapped = StackArgument(ctx, base, 0);
  if (StackArgument(ctx, base, 1)) {
    REXKRNL_ERROR("Gameserver mode: WSARecvFrom completion routines are not supported");
    Fail(ctx, wsa::kOperationNotSupported);
    return;
  }
  ReceiveRequest request{ctx.r4.u32, host, ReadBuffers(base, ctx.r5.u32, ctx.r6.u32), ctx.r9.u32, ctx.r10.u32, overlapped};
  const uint32_t bytesAddress = ctx.r7.u32, flagsAddress = ctx.r8.u32;
  const int flags = flagsAddress ? static_cast<int>(load_and_swap<uint32_t>(base + flagsAddress)) : 0;
  const auto result = overlapped ? ReceiveQueued(base, request, flags) : Receive(base, request, flags);
  if (result.bytes < 0 && overlapped && WouldBlock(result.error)) {
    store_and_swap<uint32_t>(base + overlapped + kOverlappedStatus, kStatusPending);
    ResetEvent(load_and_swap<uint32_t>(base + overlapped + kOverlappedEvent));
    Pending().Add(base, std::move(request));
    Fail(ctx, wsa::kIoPending);
    return;
  }
  if (result.bytes < 0) { Fail(ctx, TranslateHostError(result.error)); return; }
  if (bytesAddress) store_and_swap<uint32_t>(base + bytesAddress, static_cast<uint32_t>(result.bytes));
  if (flagsAddress) store_and_swap<uint32_t>(base + flagsAddress, 0);
  if (overlapped) Complete(base, overlapped, 0, static_cast<uint32_t>(result.bytes));
  Succeed(ctx, 0);
}

// WSASendTo(caller, s, buffers, count, bytesSent*, flags, to, tolen, overlapped, completion).
// Datagram sends finish at once; an overlapped send is completed before returning.
REX_HOOK_RAW(__imp__NetDll_WSASendTo) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_WSASendTo);
  HOST_SOCKET_OR_FAIL(host, r4);
  const uint32_t overlapped = StackArgument(ctx, base, 0);
  if (StackArgument(ctx, base, 1)) {
    REXKRNL_ERROR("Gameserver mode: WSASendTo completion routines are not supported");
    Fail(ctx, wsa::kOperationNotSupported);
    return;
  }
  const auto buffers = ReadBuffers(base, ctx.r5.u32, ctx.r6.u32);
  std::vector<char> gathered;
  gathered.reserve(buffers.total);
  for (const auto& [length, buffer] : buffers.entries)
    gathered.insert(gathered.end(), base + buffer, base + buffer + length);
  const auto to = ReadSockaddr(base, ctx.r9.u32, ctx.r10.u32);
  const auto sent = to ? ::sendto(host, gathered.data(), gathered.size(), SendFlags(ctx.r8.u32),
                                  reinterpret_cast<const sockaddr*>(&*to), sizeof(*to))
                       : ::send(host, gathered.data(), gathered.size(), SendFlags(ctx.r8.u32));
  if (sent < 0) { FailFromHost(ctx); return; }
  if (ctx.r7.u32) store_and_swap<uint32_t>(base + ctx.r7.u32, static_cast<uint32_t>(sent));
  if (overlapped) Complete(base, overlapped, 0, static_cast<uint32_t>(sent));
  Succeed(ctx, 0);
}

// WSAGetOverlappedResult(caller, s, overlapped, bytes*, wait, flags*) -> BOOL
REX_HOOK_RAW(__imp__NetDll_WSAGetOverlappedResult) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_WSAGetOverlappedResult);
  const uint32_t overlapped = ctx.r5.u32, bytesAddress = ctx.r6.u32, wait = ctx.r7.u32, flagsAddress = ctx.r8.u32;
  if (!overlapped) { rex::system::XThread::SetLastError(wsa::kInvalidArgument); ctx.r3.u64 = 0; return; }
  if (load_and_swap<uint32_t>(base + overlapped + kOverlappedStatus) == kStatusPending) {
    if (!wait) { rex::system::XThread::SetLastError(wsa::kIoIncomplete); ctx.r3.u64 = 0; return; }
    Pending().Wait(base, overlapped);
  }
  const uint32_t status = load_and_swap<uint32_t>(base + overlapped + kOverlappedStatus);
  if (bytesAddress) store_and_swap<uint32_t>(base + bytesAddress, load_and_swap<uint32_t>(base + overlapped + kOverlappedBytes));
  if (flagsAddress) store_and_swap<uint32_t>(base + flagsAddress, 0);
  if (status != 0) { rex::system::XThread::SetLastError(status); ctx.r3.u64 = 0; return; }
  ctx.r3.u64 = 1;
}

// WSACancelOverlappedIO(caller, s)
REX_HOOK_RAW(__imp__NetDll_WSACancelOverlappedIO) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_WSACancelOverlappedIO);
  if (!Lookup(ctx.r4.u32)) { Fail(ctx, wsa::kNotSocket); return; }
  Pending().Cancel(base, ctx.r4.u32);
  Succeed(ctx, 0);
}
