// Gameserver mode: XHttp on host sockets. The runtime only stubs XHttp, and the
// title's Grid client needs it. Semantics follow WinHTTP's asynchronous mode:
// completions queue up and XHttpDoWork delivers them to the title's status
// callback on the title's thread. Content encodings pass through unchanged
// (the title inflates gzip itself); chunked transfer coding is removed.
#include "onevs100_init.h"
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/system/xthread.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>
#include "online_service.h"
#include "runtime_export.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {
using rex::memory::load_and_swap;
using rex::memory::store_and_swap;

namespace xhttp {
constexpr uint32_t kStatusHandleClosing = 0x00000800;
constexpr uint32_t kStatusHeadersAvailable = 0x00020000;
constexpr uint32_t kStatusReadComplete = 0x00080000;
constexpr uint32_t kStatusWriteComplete = 0x00100000;
constexpr uint32_t kStatusRequestError = 0x00200000;
constexpr uint32_t kStatusSendRequestComplete = 0x00400000;
constexpr uint32_t kApiReceiveResponse = 1;

constexpr uint32_t kQueryContentLength = 9;
constexpr uint32_t kQueryRawHeadersCrlf = 22;
constexpr uint32_t kQueryStatusCode = 65534;
constexpr uint32_t kQueryCustom = 65535;
constexpr uint32_t kQueryFlagRequestHeaders = 0x80000000;
constexpr uint32_t kQueryFlagNumber = 0x20000000;
constexpr uint32_t kQueryLevelMask = 0x0000FFFF;

constexpr uint32_t kErrorInvalidHandle = 6;
constexpr uint32_t kErrorInsufficientBuffer = 122;
constexpr uint32_t kErrorInvalidParameter = 87;
constexpr uint32_t kErrorTimeout = 12002;
constexpr uint32_t kErrorIncorrectHandleState = 12019;
constexpr uint32_t kErrorCannotConnect = 12029;
constexpr uint32_t kErrorConnectionError = 12030;
constexpr uint32_t kErrorHeaderNotFound = 12150;
constexpr uint32_t kErrorInvalidServerResponse = 12152;
}  // namespace xhttp

// Header names of the XHTTP_QUERY_* levels the title may ask for.
const std::map<uint32_t, std::string> kQueryHeaderNames = {
    {0, "accept"}, {1, "accept-charset"}, {2, "accept-encoding"}, {3, "accept-language"},
    {4, "accept-ranges"}, {5, "allow"}, {6, "cache-control"}, {7, "connection"},
    {8, "content-language"}, {9, "content-length"}, {10, "content-transfer-encoding"},
    {11, "content-type"}, {12, "date"}, {13, "expires"}, {15, "host"}, {21, "last-modified"},
    {30, "server"}, {35, "transfer-encoding"}, {39, "x-delay"}, {40, "x-delayflags"},
    {41, "x-err"}, {42, "content-range"}};

std::string Lower(std::string text) {
  for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}
std::string GuestString(uint8_t* base, uint32_t address) {
  if (!address) return {};
  return reinterpret_cast<const char*>(base + address);
}

struct Notification {
  uint32_t status;
  std::vector<uint8_t> info;  // copied to guest memory for the callback
  uint32_t infoAddress = 0;   // or a guest address the title gave
  uint32_t infoLength = 0;
};

struct Response {
  uint32_t statusCode = 0;
  std::string statusLine;
  std::vector<std::pair<std::string, std::string>> headers;  // name lower-case
  std::string rawHeaders;
  std::string body;
};

struct Handle {
  enum class Kind { Session, Connect, Request } kind;
  uint32_t session = 0;  // owning session handle; itself for a session
  // Connect and request
  std::string host;
  uint16_t port = 80;
  // Request
  std::string verb, path, requestHeaders, requestBody;
  uint32_t expectedBodyLength = 0;
  uint32_t context = 0;
  uint32_t callback = 0;
  uint32_t notificationFlags = 0;
  bool exchangeStarted = false;
  std::optional<Response> response;
  size_t readOffset = 0;
  std::deque<Notification> pending;
};

struct XHttpState {
  std::mutex lock;
  std::condition_variable changed;
  std::map<uint32_t, std::shared_ptr<Handle>> handles;
  uint32_t next = 0;
};
XHttpState& State() { static XHttpState state; return state; }

// Distinct from kernel object handles and guest sockets.
constexpr uint32_t kHandleBase = 0x5D000000;

uint32_t AddHandle(std::shared_ptr<Handle> handle) {
  auto& state = State();
  std::lock_guard guard(state.lock);
  uint32_t value;
  do { value = kHandleBase | ((state.next++ & 0x00FFFFFF) << 2); } while (state.handles.count(value));
  if (handle->kind == Handle::Kind::Session) handle->session = value;
  state.handles[value] = std::move(handle);
  return value;
}
std::shared_ptr<Handle> FindHandle(uint32_t value) {
  auto& state = State();
  std::lock_guard guard(state.lock);
  const auto it = state.handles.find(value);
  return it == state.handles.end() ? nullptr : it->second;
}

void Post(Handle& request, Notification notification) {
  // Caller holds the state lock.
  if (!(request.notificationFlags & notification.status)) return;
  request.pending.push_back(std::move(notification));
  State().changed.notify_all();
}
Notification ErrorNotification(uint32_t api, uint32_t error) {
  Notification n{xhttp::kStatusRequestError, std::vector<uint8_t>(8)};
  // WINHTTP_ASYNC_RESULT: dwResult (the failed API), dwError.
  for (int i = 0; i < 4; ++i) {
    n.info[i] = static_cast<uint8_t>(api >> (24 - 8 * i));
    n.info[4 + i] = static_cast<uint8_t>(error >> (24 - 8 * i));
  }
  n.infoLength = 8;
  return n;
}

bool Fail(PPCContext& ctx, uint32_t error) {
  rex::system::XThread::SetLastError(error);
  ctx.r3.u64 = 0;
  return false;
}

#ifdef _WIN32
using HostSocket = SOCKET;
void CloseHost(HostSocket s) { closesocket(s); }
constexpr HostSocket kInvalidHostSocket = INVALID_SOCKET;
#else
using HostSocket = int;
void CloseHost(HostSocket s) { close(s); }
constexpr HostSocket kInvalidHostSocket = -1;
#endif

// Removes chunked transfer coding; nullopt when malformed.
std::optional<std::string> Dechunk(const std::string& data) {
  std::string out;
  size_t pos = 0;
  while (true) {
    const size_t lineEnd = data.find("\r\n", pos);
    if (lineEnd == std::string::npos) return std::nullopt;
    const size_t size = std::strtoul(data.substr(pos, lineEnd - pos).c_str(), nullptr, 16);
    pos = lineEnd + 2;
    if (size == 0) return out;
    if (pos + size > data.size()) return std::nullopt;
    out.append(data, pos, size);
    pos += size + 2;
  }
}

// True once the received bytes hold a whole response framed by Content-Length
// or chunked coding; a response without either ends when the server closes.
bool ResponseComplete(const std::string& data) {
  const size_t headerEnd = data.find("\r\n\r\n");
  if (headerEnd == std::string::npos) return false;
  const std::string head = Lower(data.substr(0, headerEnd + 2));
  const size_t bodyStart = headerEnd + 4;
  if (const size_t encoding = head.find("\r\ntransfer-encoding:"); encoding != std::string::npos) {
    const size_t valueEnd = head.find("\r\n", encoding + 2);
    if (head.substr(encoding, valueEnd - encoding).find("chunked") != std::string::npos) {
      // The last chunk, then optional trailer fields, then an empty line.
      const size_t last = data.compare(bodyStart, 3, "0\r\n") == 0 ? bodyStart : data.find("\r\n0\r\n", headerEnd);
      return last != std::string::npos && data.find("\r\n\r\n", last + 1) != std::string::npos;
    }
  }
  const size_t length = head.find("\r\ncontent-length:");
  if (length == std::string::npos) return false;
  const size_t expected = std::strtoul(head.c_str() + length + 17, nullptr, 10);
  return data.size() - bodyStart >= expected;
}

// Blocking HTTP/1.1 exchange with Connection: close. Returns an XHttp error code on failure.
std::variant<Response, uint32_t> Exchange(const Handle& request) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* address = nullptr;
  if (getaddrinfo(request.host.c_str(), std::to_string(request.port).c_str(), &hints, &address) != 0 || !address)
    return xhttp::kErrorCannotConnect;
  const HostSocket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == kInvalidHostSocket) { freeaddrinfo(address); return xhttp::kErrorCannotConnect; }
#ifdef _WIN32
  DWORD timeout = 30000;
#else
  timeval timeout{30, 0};
#endif
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  const int connected = ::connect(s, address->ai_addr, static_cast<int>(address->ai_addrlen));
  freeaddrinfo(address);
  if (connected != 0) { CloseHost(s); return xhttp::kErrorCannotConnect; }

  std::string message = request.verb + " " + request.path + " HTTP/1.1\r\nHost: " + request.host;
  if (request.port != 80) message += ":" + std::to_string(request.port);
  message += "\r\n";
  std::string headers = request.requestHeaders;
  if (!headers.empty() && headers.size() >= 2 && headers.compare(headers.size() - 2, 2, "\r\n") != 0) headers += "\r\n";
  message += headers;
  if (!request.requestBody.empty() || request.verb == "POST" || request.verb == "PUT")
    message += "Content-Length: " + std::to_string(request.requestBody.size()) + "\r\n";
  message += "Connection: close\r\n\r\n";
  message += request.requestBody;
  for (size_t sent = 0; sent < message.size();) {
    const auto n = ::send(s, message.data() + sent, static_cast<int>(message.size() - sent), 0);
    if (n <= 0) { CloseHost(s); return xhttp::kErrorConnectionError; }
    sent += static_cast<size_t>(n);
  }
  // The response ends at Content-Length, at the last chunk, or else when the
  // server closes; a server may keep the connection open.
  std::string data;
  char chunk[16384];
  while (!ResponseComplete(data)) {
    const auto n = ::recv(s, chunk, sizeof(chunk), 0);
    if (n == 0) break;
    if (n < 0) { CloseHost(s); return data.empty() ? xhttp::kErrorTimeout : xhttp::kErrorConnectionError; }
    data.append(chunk, static_cast<size_t>(n));
  }
  CloseHost(s);

  const size_t headerEnd = data.find("\r\n\r\n");
  if (headerEnd == std::string::npos || data.compare(0, 5, "HTTP/") != 0) return xhttp::kErrorInvalidServerResponse;
  Response response;
  response.rawHeaders = data.substr(0, headerEnd + 4);
  const size_t lineEnd = data.find("\r\n");
  response.statusLine = data.substr(0, lineEnd);
  const size_t space = response.statusLine.find(' ');
  if (space == std::string::npos) return xhttp::kErrorInvalidServerResponse;
  response.statusCode = static_cast<uint32_t>(std::strtoul(response.statusLine.c_str() + space + 1, nullptr, 10));
  bool chunked = false;
  for (size_t pos = lineEnd + 2; pos < headerEnd;) {
    const size_t end = data.find("\r\n", pos);
    const std::string line = data.substr(pos, end - pos);
    pos = end + 2;
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string value = line.substr(colon + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    const std::string name = Lower(line.substr(0, colon));
    if (name == "transfer-encoding" && Lower(value).find("chunked") != std::string::npos) chunked = true;
    response.headers.emplace_back(name, value);
  }
  response.body = data.substr(headerEnd + 4);
  if (chunked) {
    auto body = Dechunk(response.body);
    if (!body) return xhttp::kErrorInvalidServerResponse;
    response.body = std::move(*body);
  }
  return response;
}

// Runs the guest status callback with a scratch area for the information block.
void Deliver(PPCContext& ctx, uint8_t* base, uint32_t requestHandle, const Handle& request,
             const Notification& n) {
  rex::CallFrame frame(ctx);
  constexpr uint32_t kFrameSize = 0x200;
  constexpr uint32_t kInfoOffset = 0x80;  // above the callee's frame, inside ours
  frame.ctx.r1.u32 -= kFrameSize;
  uint32_t info = n.infoAddress;
  if (!n.info.empty()) {
    info = frame.ctx.r1.u32 + kInfoOffset;
    std::memcpy(base + info, n.info.data(), std::min<size_t>(n.info.size(), kFrameSize - kInfoOffset));
  }
  frame.ctx.r3.u64 = requestHandle;
  frame.ctx.r4.u64 = request.context;
  frame.ctx.r5.u64 = n.status;
  frame.ctx.r6.u64 = info;
  frame.ctx.r7.u64 = n.infoLength;
  frame.ctx.ctr.u64 = request.callback;
  {
    // The macro calls through the names ctx and base.
    PPCContext& ctx = frame.ctx;
    REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
  }
}

bool Gameserver() { return CurrentOnlineService() == OnlineService::Gameserver; }
}  // namespace

#define OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(name)          \
  if (!Gameserver()) {                                     \
    static PPCFunc* const original = RuntimeExport(#name); \
    original(ctx, base);                                   \
    return;                                                \
  }

// Every import takes the XNet caller type first, in r3.

REX_HOOK_RAW(__imp__NetDll_XHttpStartup) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpStartup);
  ctx.r3.u64 = 1;
}
REX_HOOK_RAW(__imp__NetDll_XHttpShutdown) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpShutdown);
}

// XHttpOpen(userAgent, accessType, proxy, proxyBypass, flags)
REX_HOOK_RAW(__imp__NetDll_XHttpOpen) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpOpen);
  auto session = std::make_shared<Handle>();
  session->kind = Handle::Kind::Session;
  ctx.r3.u64 = AddHandle(std::move(session));
}

// XHttpConnect(session, serverName, port, flags)
REX_HOOK_RAW(__imp__NetDll_XHttpConnect) {
  if (!Gameserver()) {
    REXKRNL_WARN("Denied guest HTTP without gameserver");
    rex::system::XThread::SetLastError(xhttp::kErrorCannotConnect);
    ctx.r3.u64 = 0;
    return;
  }
  const auto session = FindHandle(ctx.r4.u32);
  if (!session || session->kind != Handle::Kind::Session) { Fail(ctx, xhttp::kErrorInvalidHandle); return; }
  auto connect = std::make_shared<Handle>();
  connect->kind = Handle::Kind::Connect;
  connect->session = ctx.r4.u32;
  connect->host = GuestString(base, ctx.r5.u32);
  // INTERNET_DEFAULT_PORT (0) is HTTP's port for this plain-HTTP client.
  connect->port = ctx.r6.u32 ? static_cast<uint16_t>(ctx.r6.u32) : 80;
  REXKRNL_INFO("Gameserver mode: XHttpConnect {}:{}", connect->host, connect->port);
  ctx.r3.u64 = AddHandle(std::move(connect));
}

namespace {
void OpenRequest(PPCContext& ctx, uint8_t* base, uint32_t connectHandle, uint32_t verb, uint32_t object) {
  const auto connect = FindHandle(connectHandle);
  if (!connect || connect->kind != Handle::Kind::Connect) { Fail(ctx, xhttp::kErrorInvalidHandle); return; }
  auto request = std::make_shared<Handle>();
  request->kind = Handle::Kind::Request;
  request->session = connect->session;
  request->host = connect->host;
  request->port = connect->port;
  request->verb = verb ? GuestString(base, verb) : "GET";
  request->path = GuestString(base, object);
  if (request->path.empty() || request->path[0] != '/') request->path = "/" + request->path;
  // A request inherits the session's callback.
  if (const auto session = FindHandle(connect->session)) {
    request->callback = session->callback;
    request->notificationFlags = session->notificationFlags;
  }
  ctx.r3.u64 = AddHandle(std::move(request));
}
}  // namespace

// XHttpOpenRequest(connect, verb, object, version, referrer, reserved, flags)
REX_HOOK_RAW(__imp__NetDll_XHttpOpenRequest) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpOpenRequest);
  OpenRequest(ctx, base, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
}
// XHttpOpenRequestUsingMemory(connect, verb, object, version, referrer, reserved,
// memory, then size and flags on the stack). The request lives host-side.
REX_HOOK_RAW(__imp__NetDll_XHttpOpenRequestUsingMemory) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpOpenRequestUsingMemory);
  OpenRequest(ctx, base, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
}

// XHttpSetStatusCallback(handle, callback, flags, reserved) -> previous callback
REX_HOOK_RAW(__imp__NetDll_XHttpSetStatusCallback) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpSetStatusCallback);
  const auto handle = FindHandle(ctx.r4.u32);
  if (!handle) { Fail(ctx, xhttp::kErrorInvalidHandle); return; }
  std::lock_guard guard(State().lock);
  const uint32_t previous = handle->callback;
  handle->callback = ctx.r5.u32;
  handle->notificationFlags = ctx.r6.u32;
  ctx.r3.u64 = previous;
}

// XHttpSetOption(handle, option, buffer, length): accepted, nothing to apply on host sockets.
REX_HOOK_RAW(__imp__NetDll_XHttpSetOption) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpSetOption);
  if (!FindHandle(ctx.r4.u32)) { Fail(ctx, xhttp::kErrorInvalidHandle); return; }
  REXKRNL_DEBUG("Gameserver mode: XHttpSetOption {} ignored", ctx.r5.u32);
  ctx.r3.u64 = 1;
}
REX_HOOK_RAW(__imp__NetDll_XHttpQueryOption) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpQueryOption);
  REXKRNL_WARN("Gameserver mode: XHttpQueryOption {} not supported", ctx.r5.u32);
  Fail(ctx, xhttp::kErrorInvalidParameter);
}

// XHttpSendRequest(request, headers, headersLength, optional, optionalLength, totalLength, context)
REX_HOOK_RAW(__imp__NetDll_XHttpSendRequest) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpSendRequest);
  const auto request = FindHandle(ctx.r4.u32);
  if (!request || request->kind != Handle::Kind::Request) { Fail(ctx, xhttp::kErrorInvalidHandle); return; }
  std::lock_guard guard(State().lock);
  if (const uint32_t headers = ctx.r5.u32) {
    const uint32_t length = ctx.r6.u32;
    // 0xFFFFFFFF: NUL-terminated.
    request->requestHeaders = length == 0xFFFFFFFF ? GuestString(base, headers)
                                                   : std::string(reinterpret_cast<const char*>(base + headers), length);
  }
  if (ctx.r7.u32 && ctx.r8.u32)
    request->requestBody.assign(reinterpret_cast<const char*>(base + ctx.r7.u32), ctx.r8.u32);
  request->expectedBodyLength = std::max(ctx.r9.u32, ctx.r8.u32);
  request->context = ctx.r10.u32;
  REXKRNL_INFO("Gameserver mode: XHttp {} http://{}:{}{}", request->verb, request->host, request->port, request->path);
  Post(*request, {xhttp::kStatusSendRequestComplete});
  ctx.r3.u64 = 1;
}

// XHttpWriteData(request, buffer, length, written*)
REX_HOOK_RAW(__imp__NetDll_XHttpWriteData) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpWriteData);
  const auto request = FindHandle(ctx.r4.u32);
  if (!request || request->kind != Handle::Kind::Request) { Fail(ctx, xhttp::kErrorInvalidHandle); return; }
  std::lock_guard guard(State().lock);
  if (request->exchangeStarted) { Fail(ctx, xhttp::kErrorIncorrectHandleState); return; }
  const uint32_t length = ctx.r6.u32;
  request->requestBody.append(reinterpret_cast<const char*>(base + ctx.r5.u32), length);
  if (ctx.r7.u32) store_and_swap<uint32_t>(base + ctx.r7.u32, length);
  Notification n{xhttp::kStatusWriteComplete, {static_cast<uint8_t>(length >> 24), static_cast<uint8_t>(length >> 16),
                                               static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length)}};
  n.infoLength = 4;
  Post(*request, std::move(n));
  ctx.r3.u64 = 1;
}

// XHttpReceiveResponse(request, reserved): the exchange runs on a host thread.
REX_HOOK_RAW(__imp__NetDll_XHttpReceiveResponse) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpReceiveResponse);
  const auto request = FindHandle(ctx.r4.u32);
  if (!request || request->kind != Handle::Kind::Request) { Fail(ctx, xhttp::kErrorInvalidHandle); return; }
  {
    std::lock_guard guard(State().lock);
    if (request->exchangeStarted) { Fail(ctx, xhttp::kErrorIncorrectHandleState); return; }
    request->exchangeStarted = true;
  }
  Handle snapshot;
  {
    std::lock_guard guard(State().lock);
    snapshot = *request;
  }
  std::thread([request, snapshot = std::move(snapshot)] {
    auto result = Exchange(snapshot);
    std::lock_guard guard(State().lock);
    if (auto* response = std::get_if<Response>(&result)) {
      REXKRNL_INFO("Gameserver mode: XHttp {} {} -> {} ({} bytes)", request->verb, request->path,
                   response->statusCode, response->body.size());
      request->response = std::move(*response);
      Post(*request, {xhttp::kStatusHeadersAvailable});
    } else {
      const uint32_t error = std::get<uint32_t>(result);
      REXKRNL_WARN("Gameserver mode: XHttp {} {} failed, error {}", request->verb, request->path, error);
      Post(*request, ErrorNotification(xhttp::kApiReceiveResponse, error));
    }
  }).detach();
  ctx.r3.u64 = 1;
}

// XHttpQueryHeaders(request, infoLevel, name, buffer, length*, index*)
REX_HOOK_RAW(__imp__NetDll_XHttpQueryHeaders) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpQueryHeaders);
  const auto request = FindHandle(ctx.r4.u32);
  if (!request || request->kind != Handle::Kind::Request) { Fail(ctx, xhttp::kErrorInvalidHandle); return; }
  const uint32_t infoLevel = ctx.r5.u32, buffer = ctx.r7.u32, lengthAddress = ctx.r8.u32, indexAddress = ctx.r9.u32;
  const uint32_t level = infoLevel & xhttp::kQueryLevelMask;
  const bool number = infoLevel & xhttp::kQueryFlagNumber;
  if (!lengthAddress) { Fail(ctx, xhttp::kErrorInvalidParameter); return; }
  std::lock_guard guard(State().lock);
  if (!request->response || (infoLevel & xhttp::kQueryFlagRequestHeaders)) {
    Fail(ctx, request->response ? xhttp::kErrorHeaderNotFound : xhttp::kErrorIncorrectHandleState);
    return;
  }
  const Response& response = *request->response;
  const uint32_t index = indexAddress ? load_and_swap<uint32_t>(base + indexAddress) : 0;
  std::optional<std::string> value;
  if (level == xhttp::kQueryStatusCode) {
    value = std::to_string(response.statusCode);
  } else if (level == xhttp::kQueryRawHeadersCrlf) {
    value = response.rawHeaders;
  } else {
    std::string name;
    if (level == xhttp::kQueryCustom) name = Lower(GuestString(base, ctx.r6.u32));
    else if (const auto it = kQueryHeaderNames.find(level); it != kQueryHeaderNames.end()) name = it->second;
    else REXKRNL_WARN("Gameserver mode: XHttpQueryHeaders level {} not mapped", level);
    // Content-Length of the delivered body: chunked coding was removed.
    if (level == xhttp::kQueryContentLength && index == 0) value = std::to_string(response.body.size());
    uint32_t seen = 0;
    for (const auto& [headerName, headerValue] : response.headers) {
      if (value || name.empty()) break;
      if (headerName == name && seen++ == index) value = headerValue;
    }
  }
  if (!value) { Fail(ctx, xhttp::kErrorHeaderNotFound); return; }
  const uint32_t capacity = load_and_swap<uint32_t>(base + lengthAddress);
  if (number) {
    // An 8-byte buffer receives a 64-bit number (the title reads Content-Length that way).
    if (!buffer || capacity < 4) { store_and_swap<uint32_t>(base + lengthAddress, 4); Fail(ctx, xhttp::kErrorInsufficientBuffer); return; }
    const uint64_t parsed = std::strtoull(value->c_str(), nullptr, 10);
    if (capacity >= 8) {
      store_and_swap<uint64_t>(base + buffer, parsed);
      store_and_swap<uint32_t>(base + lengthAddress, 8);
    } else {
      store_and_swap<uint32_t>(base + buffer, static_cast<uint32_t>(parsed));
      store_and_swap<uint32_t>(base + lengthAddress, 4);
    }
  } else {
    const uint32_t needed = static_cast<uint32_t>(value->size()) + 1;
    if (!buffer || capacity < needed) { store_and_swap<uint32_t>(base + lengthAddress, needed); Fail(ctx, xhttp::kErrorInsufficientBuffer); return; }
    std::memcpy(base + buffer, value->c_str(), needed);
    store_and_swap<uint32_t>(base + lengthAddress, needed - 1);
  }
  if (indexAddress) store_and_swap<uint32_t>(base + indexAddress, index + 1);
  ctx.r3.u64 = 1;
}

// XHttpReadData(request, buffer, length, read*): the whole body is already here.
REX_HOOK_RAW(__imp__NetDll_XHttpReadData) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpReadData);
  const auto request = FindHandle(ctx.r4.u32);
  if (!request || request->kind != Handle::Kind::Request) { Fail(ctx, xhttp::kErrorInvalidHandle); return; }
  std::lock_guard guard(State().lock);
  if (!request->response) { Fail(ctx, xhttp::kErrorIncorrectHandleState); return; }
  const std::string& body = request->response->body;
  const uint32_t buffer = ctx.r5.u32;
  const uint32_t count = static_cast<uint32_t>(std::min<size_t>(ctx.r6.u32, body.size() - request->readOffset));
  std::memcpy(base + buffer, body.data() + request->readOffset, count);
  request->readOffset += count;
  if (ctx.r7.u32) store_and_swap<uint32_t>(base + ctx.r7.u32, count);
  Notification n{xhttp::kStatusReadComplete};
  n.infoAddress = buffer;
  n.infoLength = count;
  Post(*request, std::move(n));
  ctx.r3.u64 = 1;
}

// XHttpDoWork(session, milliseconds): delivers the session's queued notifications.
REX_HOOK_RAW(__imp__NetDll_XHttpDoWork) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpDoWork);
  const uint32_t session = ctx.r4.u32;
  const uint32_t waitMs = ctx.r5.u32;
  std::vector<std::tuple<uint32_t, std::shared_ptr<Handle>, Notification>> due;
  {
    std::unique_lock guard(State().lock);
    auto collect = [&] {
      for (auto& [value, handle] : State().handles) {
        if (handle->kind != Handle::Kind::Request || handle->session != session) continue;
        while (!handle->pending.empty()) {
          due.emplace_back(value, handle, std::move(handle->pending.front()));
          handle->pending.pop_front();
        }
      }
      return !due.empty();
    };
    if (!collect() && waitMs)
      State().changed.wait_for(guard, std::chrono::milliseconds(std::min<uint32_t>(waitMs, 100)), collect);
  }
  // Callbacks run without the lock: they call back into XHttp.
  for (const auto& [value, handle, notification] : due)
    if (handle->callback) Deliver(ctx, base, value, *handle, notification);
  ctx.r3.u64 = 1;
}

// XHttpCloseHandle(handle)
REX_HOOK_RAW(__imp__NetDll_XHttpCloseHandle) {
  OUTSIDE_GAMESERVER_MODE_USE_RUNTIME(__imp__NetDll_XHttpCloseHandle);
  const uint32_t value = ctx.r4.u32;
  std::shared_ptr<Handle> handle;
  {
    std::lock_guard guard(State().lock);
    const auto it = State().handles.find(value);
    if (it == State().handles.end()) { Fail(ctx, xhttp::kErrorInvalidHandle); return; }
    handle = it->second;
    State().handles.erase(it);
  }
  if (handle->callback && (handle->notificationFlags & xhttp::kStatusHandleClosing)) {
    Notification n{xhttp::kStatusHandleClosing,
                   {static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
                    static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)}};
    n.infoLength = 4;
    Deliver(ctx, base, value, *handle, n);
  }
  ctx.r3.u64 = 1;
}
