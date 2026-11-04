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

#include "onevs100_init.h"

#include "precise_sleep.h"
#include "render/display_settings.h"
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
constexpr uint32_t kResolveSourceMask = 0x7;
constexpr uint32_t kResolveDepthStencil = 0x4;
constexpr uint32_t kResolveClearRenderTarget = 0x100;
constexpr uint32_t kResolveClearDepthStencil = 0x200;
constexpr int kResolveExponentBiasShift = 26;

}  // namespace

ClearRecord MakeClear(const guest::D3DDevice* device, const ClearArgs& a) {
  ClearRecord record;
  for (uint32_t i = 0; i < 4; ++i) {
    if (a.flags & (kClearTarget0 << i)) {
      record.color[i] = onevs100::render::tracker::Surface(device->render_targets[i], ResourceKind::kColorTarget);
    }
  }
  record.clear_depth = (a.flags & kClearZBuffer) != 0;
  record.clear_stencil = (a.flags & kClearStencil) != 0;
  if (record.clear_depth || record.clear_stencil) {
    record.depth = onevs100::render::tracker::Surface(device->depth_stencil, ResourceKind::kDepthTarget);
  }
  record.color_value = a.color;
  record.depth_value = a.z;
  record.stencil_value = a.stencil & 0xFF;
  record.rects = a.rects;
  return record;
}

}  // namespace onevs100::render::capture

namespace {

namespace capture = onevs100::render::capture;
namespace cmdbuf = onevs100::render::cmdbuf;
namespace draw = onevs100::render::draw;
namespace display = onevs100::render::display;
namespace thread = onevs100::thread;

using onevs100::render::ClearRecord;
using onevs100::render::ResolveRecord;
using onevs100::render::ResourceKind;
using onevs100::render::Rect;
using onevs100::render::SwapRecord;
using onevs100::render::capture::ClearArgs;
using onevs100::render::capture::MakeClear;
using onevs100::render::guest::D3DDevice;
using onevs100::render::HostFormat;
using onevs100::render::FrameIndex;
using onevs100::render::EndFrame;
using onevs100::render::Submit;
using onevs100::render::WaitForWarmup;

using capture::kResolveClearDepthStencil;
using capture::kResolveClearRenderTarget;
using capture::kResolveDepthStencil;
using capture::kResolveExponentBiasShift;
using capture::kResolveSourceMask;

template <typename T>
const T* Guest(uint32_t address) {
  return REX_KERNEL_MEMORY()->TranslateVirtual<const T*>(address);
}

// D3DRECT {x1, y1, x2, y2}, LONGs.
Rect ReadRect(uint32_t address) {
  const auto* v = Guest<rex::be<int32_t>>(address);
  return Rect{v[0], v[1], v[2], v[3]};
}

Rect ClampRect(Rect r, uint32_t width, uint32_t height) {
  r.left = std::clamp<int32_t>(r.left, 0, int32_t(width));
  r.right = std::clamp<int32_t>(r.right, r.left, int32_t(width));
  r.top = std::clamp<int32_t>(r.top, 0, int32_t(height));
  r.bottom = std::clamp<int32_t>(r.bottom, r.top, int32_t(height));
  return r;
}

void Once(std::atomic<bool>& flag, const char* what, uint32_t value) {
  if (!flag.exchange(true)) REXGPU_ERROR("[gpu] {} ({:#x})", what, value);
}

}  // namespace

// D3DDevice_ClearF 0x822E5C58 (pDevice, Flags, pRect, pColor, Z, Stencil). The
// colour is a D3DVECTOR4, so a clear sends the vector and Clear's packed colour
// expansion does not apply.
REX_HOOK_RAW(sub_822E5C58) {
  draw::FinishPending();
  const uint32_t device_address = ctx.r3.u32;
  ClearArgs a;
  a.flags = ctx.r4.u32;
  if (ctx.r5.u32) a.rects.push_back(ReadRect(ctx.r5.u32));
  if (ctx.r6.u32) {
    const auto* color = Guest<rex::be<float>>(ctx.r6.u32);
    a.color = {color[0], color[1], color[2], color[3]};
  }
  a.z = float(ctx.f1.f64);
  a.stencil = ctx.r8.u32;
  cmdbuf::NotePending(device_address);
  if (cmdbuf::Recording(device_address)) {
    cmdbuf::RecordClear(device_address, a);
  } else {
    Submit(MakeClear(Guest<D3DDevice>(device_address), a));
  }
  __imp__sub_822E5C58(ctx, base);
}

// D3DDevice_Resolve 0x822E74D0 (pDevice, Flags, pSourceRect, pDestTexture,
// pDestPoint, DestLevel, DestSliceOrFace, pClearColor, ClearZ, ClearStencil,
// pParameters); ClearStencil follows ClearZ's GPR slot.
REX_HOOK_RAW(sub_822E74D0) {
  draw::FinishPending();
  static std::atomic<bool> s_depth_once{false};
  static std::atomic<bool> s_level_once{false};
  const auto* device = Guest<D3DDevice>(ctx.r3.u32);
  const uint32_t flags = ctx.r4.u32;
  const uint32_t source_index = flags & kResolveSourceMask;
  ResolveRecord record;
  record.source = source_index == kResolveDepthStencil
                      ? onevs100::render::tracker::Surface(device->depth_stencil, ResourceKind::kDepthTarget)
                      : onevs100::render::tracker::Surface(device->render_targets[source_index & 3],
                                         ResourceKind::kColorTarget);
  record.dest = onevs100::render::tracker::Texture(ctx.r6.u32);
  const uint32_t level = ctx.r8.u32;
  const uint32_t slice = ctx.r9.u32;
  if (level || slice) {
    Once(s_level_once, "resolve into a mip level or slice other than 0 is not handled yet and is skipped",
         level | (slice << 16));
  }
  if (source_index == kResolveDepthStencil && record.dest.format != HostFormat::kR32Float) {
    Once(s_depth_once, "depth resolve into a non-depth texture is not handled yet; its destination shows magenta",
         flags);
  }
  const bool usable = record.source.id && record.dest.id && !level && !slice;
  if (usable) {
    const Rect full{0, 0, int32_t(record.source.width), int32_t(record.source.height)};
    record.source_rect =
        ClampRect(ctx.r5.u32 ? ReadRect(ctx.r5.u32) : full, record.source.width, record.source.height);
    if (ctx.r7.u32) {
      const auto* point = Guest<rex::be<int32_t>>(ctx.r7.u32);
      record.dest_x = point[0];
      record.dest_y = point[1];
    }
    const int bias = int32_t(flags) >> kResolveExponentBiasShift;
    record.scale = std::ldexp(1.0f, bias);
    record.clear_color = (flags & kResolveClearRenderTarget) && source_index != kResolveDepthStencil;
    if (record.clear_color && ctx.r10.u32) {
      const auto* v = Guest<rex::be<float>>(ctx.r10.u32);
      record.clear_color_value = {v[0], v[1], v[2], v[3]};
    }
    if (flags & kResolveClearDepthStencil) {
      record.clear_depth_target = onevs100::render::tracker::Surface(device->depth_stencil, ResourceKind::kDepthTarget);
      record.clear_z = float(ctx.f1.f64);
      record.clear_stencil = ctx.r9.u32 & 0xFF;
    }
    Submit(std::move(record));
  }
  __imp__sub_822E74D0(ctx, base);
}

// D3DDevice_Swap 0x822F50C0 (pDevice, pFrontBuffer, pParameters). Without a GPU
// nothing throttles the title, so the frame limit paces it here; a frame that
// falls a whole period behind restarts the deadline instead of catching up.
REX_HOOK_RAW(sub_822F50C0) {
  draw::FinishPending();
  using Clock = std::chrono::steady_clock;
  const int32_t cap = display::FrameLimit();
  if (cap > 0) {
    const auto kFrame = std::chrono::nanoseconds(1'000'000'000 / cap);
    static Clock::time_point deadline = Clock::now();
    deadline += kFrame;
    const auto now = Clock::now();
    if (deadline > now) {
      thread::SleepPrecise(std::chrono::duration_cast<std::chrono::microseconds>(deadline - now));
    } else if (now - deadline > kFrame) {
      deadline = now;
    }
  }
  SwapRecord record;
  record.front = onevs100::render::tracker::Texture(ctx.r4.u32);
  record.frame = FrameIndex();
  WaitForWarmup();
  Submit(std::move(record));
  EndFrame();
  __imp__sub_822F50C0(ctx, base);
}
