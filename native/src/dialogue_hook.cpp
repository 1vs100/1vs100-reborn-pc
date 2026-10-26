#include "onevs100_init.h"
#include <rex/hook.h>
#include "dialogue_audio.h"
#include <string>

// SoundXACT:Play(bank, cue). The registration table at 8200EFE0 maps Play
// to 821A1A40; its argument readers are copied from that original wrapper.
// Bank -100 is exclusively the local WAV route, never an Xbox XACT bank.
extern "C" REX_FUNC(native_dialogue_play) {
  const auto lua = ctx.r3.u32;
  rex::CallFrame frame(ctx);
  frame.ctx.r3.u64 = lua;
  frame.ctx.r4.s64 = 2;
  sub_82138140(frame.ctx, base);
  if (frame.ctx.r3.s32 != -100) {
    sub_821A1A40(ctx, base);
    return;
  }
  frame.ctx.r3.u64 = lua;
  frame.ctx.r4.s64 = 3;
  frame.ctx.r5.s64 = 0;
  sub_82137F68(frame.ctx, base);
  const auto address = frame.ctx.r3.u32;
  std::string cue;
  if (address) {
    for (unsigned i = 0; i <= 128; ++i) {
      const char c = static_cast<char>(base[address + i]);
      if (!c) break;
      cue.push_back(c);
    }
  }
  if (cue == "StopLocalDialogue") StopLocalDialogue();
  else {
    const bool started = PlayLocalDialogue(cue);
    REXKRNL_INFO("Local dialogue cue={} started={}", cue, started);
  }
  ctx.r3.s64 = 0; // Original Play returns no Lua values.
}
