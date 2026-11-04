// Commit-point capture: Clear, Resolve and Swap turn the runtime's arguments and the
// device state into immutable records for the render thread, then call the runtime so
// its own bookkeeping (ring cursor, dirty masks, pending fences) stays intact.
//
// Argument registers follow the XDK d3d9.h prototypes; float arguments still take a
// GPR slot, so a parameter after one lands one register later than the count suggests.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

#include "render/gpu.h"
#include "render/gpu_cmdbuf.h"
#include "render/gpu_tracker.h"
#include "render/guest/d3d_device.h"

namespace onevs100::render::capture {

namespace {

// D3DCLEAR_* and D3DRESOLVE_* from the XDK's d3d9types.h.
constexpr uint32_t kClearTarget0 = 0x1;
constexpr uint32_t kClearZBuffer = 0x10;
constexpr uint32_t kClearStencil = 0x20;

}  // namespace

ClearRecord MakeClear(const guest::D3DDevice* device, const ClearArgs& a) {
  ClearRecord record;
  for (uint32_t i = 0; i < 4; ++i) {
    if (a.flags & (kClearTarget0 << i)) {
      record.color[i] = tracker::Surface(device->render_targets[i], ResourceKind::kColorTarget);
    }
  }
  record.clear_depth = (a.flags & kClearZBuffer) != 0;
  record.clear_stencil = (a.flags & kClearStencil) != 0;
  if (record.clear_depth || record.clear_stencil) {
    record.depth = tracker::Surface(device->depth_stencil, ResourceKind::kDepthTarget);
  }
  record.color_value = a.color;
  record.depth_value = a.z;
  record.stencil_value = a.stencil & 0xFF;
  record.rects = a.rects;
  return record;
}

}  // namespace onevs100::render::capture
