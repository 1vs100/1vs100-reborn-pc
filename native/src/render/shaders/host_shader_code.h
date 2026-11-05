// A precompiled host shader in every bytecode the backends take; the renderer picks one by the device's format.
#pragma once

#include <cstdint>
#include <span>

#include "plume_render_interface.h"

namespace onevs100::render::shaders {

struct HostShaderCode {
  std::span<const uint8_t> dxil;   // D3D12, shader model 6
  std::span<const uint8_t> dxbc;   // D3D12, shader model 5.1
  std::span<const uint8_t> spirv;  // Vulkan

  std::span<const uint8_t> For(plume::RenderShaderFormat format) const {
    switch (format) {
      case plume::RenderShaderFormat::DXBC: return dxbc;
      case plume::RenderShaderFormat::SPIRV: return spirv;
      default: return dxil;
    }
  }
};

}  // namespace onevs100::render::shaders
