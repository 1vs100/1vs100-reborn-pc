// Deny guest network creation during restoration; never contact retired services.
#include <rex/hook.h>
#include <rex/system/xthread.h>
#include <rex/memory/utils.h>

REX_HOOK_RAW(__imp__NetDll_socket) {
  REXKRNL_WARN("Offline restoration: denied guest socket creation");
  rex::system::XThread::SetLastError(10050); // WSAENETDOWN
  ctx.r3.u64 = 0xFFFFFFFF;
}
REX_HOOK_RAW(__imp__NetDll_XHttpConnect) {
  REXKRNL_WARN("Offline restoration: denied guest HTTP connection");
  rex::system::XThread::SetLastError(12029); // ERROR_INTERNET_CANNOT_CONNECT
  ctx.r3.u64 = 0;
}
REX_HOOK_RAW(__imp__NetDll_XNetDnsLookup) {
  REXKRNL_WARN("Offline restoration: denied guest DNS lookup");
  if (ctx.r6.u32) {
    rex::memory::store_and_swap<uint32_t>(base + ctx.r6.u32, 0);
  }
  ctx.r3.u64 = 10050; // WSAENETDOWN; no asynchronous request was started.
}
