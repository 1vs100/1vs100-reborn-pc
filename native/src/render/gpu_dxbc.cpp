#include "render/gpu_dxbc.h"

#include <bit>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <rex/graphics/pipeline/shader/dxbc.h>
#include <rex/graphics/pipeline/shader/dxbc_geometry_shader.h>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/graphics/registers.h>
#include <rex/logging.h>
#include <rex/string/buffer.h>


namespace onevs100::render::dxbc {

namespace gfx = rex::graphics;
namespace reg = rex::graphics::reg;
namespace xenos = rex::graphics::xenos;
using Modification = gfx::DxbcShaderTranslator::Modification;

struct GuestShader {
  std::unique_ptr<gfx::DxbcShader> shader;
  ShaderStage stage = ShaderStage::kVertex;
  std::map<uint64_t, std::unique_ptr<ShaderEntry>> translations;  // by modification
};

namespace {

bool g_active = false;
std::mutex g_mutex;
std::unordered_map<uint64_t, std::unique_ptr<GuestShader>> g_shaders;  // by stage and microcode hash
std::unordered_map<uint32_t, std::unique_ptr<ShaderEntry>> g_geometry;   // by DxbcGeometryShaderKey
uint32_t g_next_id = 0x20000000u;

gfx::DxbcShaderTranslator& Translator() {
  // Bindless, host render targets (no ROV); the vendor only steers a switch-statement workaround for Intel.
  static gfx::DxbcShaderTranslator translator(rex::ui::GraphicsProvider::GpuVendorID::kAMD, true, false);
  return translator;
}

uint8_t Dimension(xenos::FetchOpDimension dimension) {
  switch (dimension) {
    case xenos::FetchOpDimension::k3DOrStacked: return 2;
    case xenos::FetchOpDimension::kCube: return 3;
    default: return 1;
  }
}

// Bindings in bindless descriptor index order: the b4 slot of each is its index in this list.
std::vector<PackBinding> Bindings(const gfx::DxbcShader& shader) {
  const auto& textures = shader.GetTextureBindingsAfterTranslation();
  const auto& samplers = shader.GetSamplerBindingsAfterTranslation();
  std::vector<PackBinding> out(textures.size() + samplers.size());
  for (const auto& t : textures) {
    if (t.bindless_descriptor_index >= out.size()) continue;
    auto& b = out[t.bindless_descriptor_index];
    b.sampler = 0;
    b.fetch_constant = uint8_t(t.fetch_constant);
    b.dimension = Dimension(t.dimension);
    b.is_signed = t.is_signed ? 1 : 0;
  }
  for (const auto& s : samplers) {
    if (s.bindless_descriptor_index >= out.size()) continue;
    auto& b = out[s.bindless_descriptor_index];
    b.sampler = 1;
    b.fetch_constant = uint8_t(s.fetch_constant);
    b.mag = uint8_t(s.mag_filter);
    b.min = uint8_t(s.min_filter);
    b.mip = uint8_t(s.mip_filter);
    b.aniso = uint8_t(s.aniso_filter);
  }
  return out;
}

const ShaderEntry* TranslateOne(GuestShader& guest, Modification modification) {
  auto& slot = guest.translations[modification.value];
  if (slot) return slot->code.empty() ? nullptr : slot.get();
  slot = std::make_unique<ShaderEntry>();
  auto* translation = guest.shader->GetOrCreateTranslation(modification.value);
  if (!Translator().TranslateAnalyzedShader(*translation) || !translation->is_valid()) {
    REXGPU_ERROR("[gpu] dxbc translation of {} {:016X} modification {:016X} failed",
                 guest.stage == ShaderStage::kPixel ? "ps" : "vs", guest.shader->ucode_data_hash(),
                 modification.value);
    return nullptr;
  }
  auto& e = *slot;
  e.id = g_next_id++;
  e.stage = guest.stage;
  e.runtime_hash = guest.shader->ucode_data_hash();
  e.code = translation->translated_binary();
  e.bindings = Bindings(*guest.shader);
  const auto& map = guest.shader->constant_register_map();
  if (!map.float_dynamic_addressing) {
    for (uint32_t i = 0; i < 256; ++i) {
      if (map.float_bitmap[i >> 6] & (uint64_t(1) << (i & 63))) e.packed_floats.push_back(uint8_t(i));
    }
  }
  REXGPU_DEBUG("[gpu] dxbc {} {:016X} modification {:016X}: {} bytes, {} bindings, {} floats",
              guest.stage == ShaderStage::kPixel ? "ps" : "vs", e.runtime_hash, modification.value, e.code.size(),
              e.bindings.size(), e.packed_floats.size());
  return &e;
}

const ShaderEntry* GeometryShader(GeometryExpansion geometry, Modification vm, Modification pm) {
  const auto type = geometry == GeometryExpansion::kPointList ? gfx::DxbcGeometryShaderType::kPointList
                                                                : gfx::DxbcGeometryShaderType::kRectangleList;
  gfx::DxbcGeometryShaderKey key;
  if (!gfx::GetDxbcGeometryShaderKey(type, vm, pm, key)) return nullptr;
  auto& slot = g_geometry[key.key];
  if (slot) return slot.get();
  std::vector<uint32_t> code;
  gfx::CreateDxbcGeometryShader(key, code);
  slot = std::make_unique<ShaderEntry>();
  slot->id = g_next_id++;
  slot->stage = ShaderStage::kGeometry;
  slot->runtime_hash = key.key;
  slot->code.resize(code.size() * sizeof(uint32_t));
  std::memcpy(slot->code.data(), code.data(), slot->code.size());
  REXGPU_INFO("[gpu] dxbc {} list geometry shader {:08X}: {} bytes",
              geometry == GeometryExpansion::kPointList ? "point" : "rectangle", key.key, slot->code.size());
  return slot.get();
}

}  // namespace

void SetActive(bool active) { g_active = active; }
bool Active() { return g_active; }

const GuestShader* Analyze(ShaderStage stage, uint64_t hash, std::span<const uint32_t> ucode) {
  std::lock_guard lock(g_mutex);
  const uint64_t key = hash ^ (stage == ShaderStage::kPixel ? 0x5053000000000000ull : 0);
  auto& slot = g_shaders[key];
  if (slot) return slot.get();
  auto guest = std::make_unique<GuestShader>();
  guest->stage = stage;
  guest->shader = std::make_unique<gfx::DxbcShader>(
      stage == ShaderStage::kPixel ? xenos::ShaderType::kPixel : xenos::ShaderType::kVertex, hash, ucode.data(),
      ucode.size());
  rex::string::StringBuffer disasm;
  guest->shader->AnalyzeUcode(disasm);
  if (!guest->shader->is_ucode_analyzed()) return nullptr;
  slot = std::move(guest);
  return slot.get();
}

bool FloatRegisters(const GuestShader* shader, std::array<uint64_t, 4>& out) {
  if (!shader) return false;
  const auto& map = shader->shader->constant_register_map();
  if (map.float_dynamic_addressing) return false;
  for (size_t i = 0; i < out.size(); ++i) out[i] = map.float_bitmap[i];
  return true;
}

DrawShaders Translate(const GuestShader* vs, const GuestShader* ps, const DrawShaderState& state) {
  if (!vs) return {};
  std::lock_guard lock(g_mutex);
  auto& translator = Translator();
  reg::SQ_PROGRAM_CNTL program;
  program.value = state.program_cntl;
  reg::SQ_CONTEXT_MISC misc;
  misc.value = state.context_misc;
  reg::PA_CL_CLIP_CNTL clip;
  clip.value = state.clip_cntl;
  reg::SQ_INTERPOLATOR_CNTL interpolators;
  interpolators.value = state.interpolator_cntl;
  const gfx::Shader& vs_shader = *vs->shader;

  uint32_t param_gen_pos = UINT32_MAX;
  const uint32_t interpolator_mask =
      ps ? vs_shader.writes_interpolators() & ps->shader->GetInterpolatorInputMask(program, misc, param_gen_pos) : 0;

  Modification vm(translator.GetDefaultVertexShaderModification(
      vs_shader.GetDynamicAddressableRegisterCount(program.vs_num_reg)));
  vm.vertex.interpolator_mask = interpolator_mask;
  const uint32_t planes = clip.clip_disable ? 0 : clip.ucp_ena;
  vm.vertex.user_clip_plane_count = std::popcount(planes);
  vm.vertex.user_clip_plane_cull = planes && clip.ucp_cull_only_ena ? 1 : 0;
  vm.vertex.point_ps_ucp_mode = clip.ps_ucp_mode;
  vm.vertex.vertex_kill_and = (vs_shader.writes_point_size_edge_flag_kill_vertex() & 0b100) && !clip.vtx_kill_or;
  const bool point_list = state.geometry == GeometryExpansion::kPointList;
  vm.vertex.output_point_size = (vs_shader.writes_point_size_edge_flag_kill_vertex() & 0b001) && point_list;

  DrawShaders out;
  out.vs = TranslateOne(const_cast<GuestShader&>(*vs), vm);
  if (!out.vs) return out;
  if (!ps) {
    if (state.geometry != GeometryExpansion::kNone) {
      // As the SDK: no pixel shader, no interpolators.
      Modification pm(0);
      out.gs = GeometryShader(state.geometry, vm, pm);
      if (!out.gs) out.vs = nullptr;
    }
    return out;
  }

  const gfx::Shader& ps_shader = *ps->shader;
  Modification pm(translator.GetDefaultPixelShaderModification(
      ps_shader.GetDynamicAddressableRegisterCount(program.ps_num_reg)));
  pm.pixel.interpolator_mask = interpolator_mask;
  pm.pixel.interpolators_centroid =
      interpolator_mask & ~xenos::GetInterpolatorSamplingPattern(xenos::MsaaSamples(state.msaa_samples),
                                                                 misc.sc_sample_cntl,
                                                                 interpolators.sampling_pattern);
  if (param_gen_pos < xenos::kMaxInterpolators) {
    pm.pixel.param_gen_enable = 1;
    pm.pixel.param_gen_interpolator = param_gen_pos;
    pm.pixel.param_gen_point = point_list ? 1 : 0;
  }
  using DepthStencilMode = Modification::DepthStencilMode;
  pm.pixel.depth_stencil_mode =
      ps_shader.implicit_early_z_write_allowed() && (!ps_shader.writes_color_target(0) || !state.coverage_depends_on_alpha)
          ? DepthStencilMode::kEarlyHint
          : DepthStencilMode::kNoModifiers;
  out.ps = TranslateOne(const_cast<GuestShader&>(*ps), pm);
  if (out.ps && state.geometry != GeometryExpansion::kNone) out.gs = GeometryShader(state.geometry, vm, pm);
  if (!out.ps || (state.geometry != GeometryExpansion::kNone && !out.gs)) out.vs = nullptr;
  return out;
}

size_t SystemConstantsSize() { return sizeof(gfx::DxbcShaderTranslator::SystemConstants); }

void WriteSystemConstants(const SystemConstants& native, const DxbcSystemExtras& extras, void* out) {
  gfx::DxbcShaderTranslator::SystemConstants s{};
  s.flags = native.flags | extras.flags;
  s.line_loop_closing_index = native.line_loop_closing_index;
  s.vertex_index_endian = xenos::Endian(native.vertex_index_endian);
  s.vertex_index_offset = native.vertex_index_offset;
  s.vertex_index_min = native.vertex_index_min;
  s.vertex_index_max = native.vertex_index_max;
  std::memcpy(s.user_clip_planes, extras.user_clip_planes, sizeof(s.user_clip_planes));
  std::memcpy(s.ndc_scale, native.ndc_scale, sizeof(s.ndc_scale));
  std::memcpy(s.ndc_offset, native.ndc_offset, sizeof(s.ndc_offset));
  // The native constants hold twice the diameter that the SDK's point geometry shader takes.
  s.point_vertex_diameter_min = native.point_size[2] * 0.5f;
  s.point_vertex_diameter_max = native.point_size[3] * 0.5f;
  s.point_constant_diameter[0] = native.point_size[0] * 0.5f;
  s.point_constant_diameter[1] = native.point_size[1] * 0.5f;
  std::memcpy(s.point_screen_diameter_to_ndc_radius, extras.point_screen_diameter_to_ndc_radius,
              sizeof(s.point_screen_diameter_to_ndc_radius));
  std::memcpy(s.texture_swizzled_signs, extras.texture_swizzled_signs, sizeof(s.texture_swizzled_signs));
  std::memcpy(s.sample_count_log2, extras.sample_count_log2, sizeof(s.sample_count_log2));
  s.alpha_test_reference = native.alpha_test_reference;
  s.alpha_to_mask = extras.alpha_to_mask;
  std::memcpy(s.color_exp_bias, native.color_exp_bias, sizeof(s.color_exp_bias));
  std::memcpy(out, &s, sizeof(s));
}

}  // namespace onevs100::render::dxbc
