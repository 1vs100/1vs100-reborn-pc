#include <rex/hook.h>

// Optional camera hardware is unavailable. Never report successful capture.
#define CAMERA_UNAVAILABLE(name) REX_HOOK_RAW(__imp__##name) { \
  REXKRNL_WARN("{} unavailable in native restoration", #name); \
  ctx.r3.u64 = 1167; /* ERROR_DEVICE_NOT_CONNECTED */ \
}
CAMERA_UNAVAILABLE(XUsbcamCreate)
CAMERA_UNAVAILABLE(XUsbcamDestroy)
CAMERA_UNAVAILABLE(XUsbcamReset)
// This entry returns a connection state, rather than an error status.
REX_HOOK_RAW(__imp__XUsbcamGetState) { ctx.r3.u64 = 0; }
CAMERA_UNAVAILABLE(XUsbcamGetConfig)
CAMERA_UNAVAILABLE(XUsbcamSetConfig)
CAMERA_UNAVAILABLE(XUsbcamSetView)
CAMERA_UNAVAILABLE(XUsbcamGetView)
CAMERA_UNAVAILABLE(XUsbcamSetCaptureMode)
CAMERA_UNAVAILABLE(XUsbcamReadFrame)
CAMERA_UNAVAILABLE(XUsbcamSnapshot)

