#pragma once

#include <memory>

namespace rex::system {
class IGraphicsSystem;
}

namespace onevs100::gpu {

// The title's own D3D12 renderer for desktop Windows, in place of the
// rexgpu-xenos plugin. Selected with the native_renderer setting.
std::unique_ptr<rex::system::IGraphicsSystem> CreateNativeGraphicsSystem();

}  // namespace onevs100::gpu
