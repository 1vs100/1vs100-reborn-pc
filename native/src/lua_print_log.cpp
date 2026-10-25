// Retail Lua print builds its line and drops it. With lua_print_log the
// arguments go to the log, as reborn's diagnostics build does.
#include "onevs100_init.h"
#include <rex/cvar.h>
#include <rex/hook.h>
#include <string>

REXCVAR_DEFINE_BOOL(lua_print_log, false, "Debug", "Write the title's Lua print output to the log");

// Lua print sink (lua_State*).
REX_HOOK_RAW(sub_8215D748) {
  if (REXCVAR_GET(lua_print_log)) {
    const uint32_t lua = ctx.r3.u32;
    rex::CallFrame count(ctx);
    count.ctx.r3.u64 = lua;
    sub_82135438(count.ctx, base);  // lua_gettop
    const uint32_t arguments = count.ctx.r3.u32;
    std::string line;
    for (uint32_t i = 1; i <= arguments && i <= 32; ++i) {
      rex::CallFrame text(ctx);
      text.ctx.r3.u64 = lua;
      text.ctx.r4.u64 = i;
      text.ctx.r5.u64 = 0;
      sub_82135A98(text.ctx, base);  // lua_tolstring
      if (i > 1) line += '\t';
      line += text.ctx.r3.u32 ? reinterpret_cast<const char*>(base + text.ctx.r3.u32) : "<non-string>";
    }
    REXKRNL_INFO("[lua] {}", line);
  }
  __imp__sub_8215D748(ctx, base);
}
