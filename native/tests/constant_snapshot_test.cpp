// FloatConstantSnapshots with register masks against the full-block capture it replaced: same registers for every draw, and the time per
// draw for both on a lobby-like draw sequence.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include "render/gpu_constant_snapshot.h"

using onevs100::render::FloatConstantBlock;
using onevs100::render::FloatConstantSnapshots;
using Mask = FloatConstantSnapshots::Mask;

namespace {

// The capture before register masks compared and copied the whole 4 KB block: an all-registers mask does the same.
constexpr Mask kAll = {~0ull, ~0ull, ~0ull, ~0ull};

Mask Range(uint32_t first, uint32_t count) {
  Mask mask{};
  for (uint32_t reg = first; reg < first + count; ++reg) mask[reg / 64] |= uint64_t(1) << (reg % 64);
  return mask;
}

struct Draw {
  Mask vs, ps;
  bool rewrite;  // the title sets new per-object constants before this draw
};

// Avatars and set pieces: a few shader pairs, per-object matrices rewritten before most draws.
std::vector<Draw> DrawSequence(size_t count) {
  const Mask shaders[][2] = {
      {Range(0, 12), Range(0, 4)},                         // static mesh
      {Range(0, 72), Range(0, 6)},                         // skinned avatar
      {Range(0, 8) , Range(0, 2)},                         // UI quad
  };
  std::mt19937 rng(7);
  std::vector<Draw> draws(count);
  for (auto& d : draws) {
    const auto& s = shaders[rng() % 3];
    d.vs = s[0], d.ps = s[1];
    d.rewrite = rng() % 10 < 8;
  }
  return draws;
}

bool Matches(const FloatConstantBlock& block, const uint32_t* source, const Mask& used) {
  for (uint32_t reg = 0; reg < 256; ++reg) {
    if (!(used[reg / 64] >> (reg % 64) & 1)) continue;
    if (std::memcmp(&block.words[reg * 4], &source[reg * 4], 16) != 0) return false;
  }
  return true;
}

}  // namespace

int main() {
  constexpr size_t kDraws = 1800, kFrames = 200;
  const auto draws = DrawSequence(kDraws);
  alignas(64) uint32_t vs[1024], ps[1024];
  std::mt19937 rng(11);
  for (auto& w : vs) w = rng();
  for (auto& w : ps) w = rng();

  // Equivalence: every captured block holds the source registers the draw reads.
  {
    FloatConstantSnapshots masked;
    std::vector<std::shared_ptr<const FloatConstantBlock>> keep;
    for (const auto& d : draws) {
      if (d.rewrite) vs[4 + rng() % 8] = rng(), ps[rng() % 2] = rng();
      auto v = masked.Capture(0, vs, d.vs), p = masked.Capture(1, ps, d.ps);
      if (!Matches(*v, vs, d.vs) || !Matches(*p, ps, d.ps)) {
        std::printf("FAIL: a captured block differs from the source on a register the draw reads\n");
        return 1;
      }
      keep.push_back(v), keep.push_back(p);
    }
    std::printf("PASS: %zu draws captured the registers they read\n", draws.size());
  }

  // Speed, per draw, over many frames of the same sequence.
  auto time = [&](auto&& capture) {
    std::mt19937 local(11);
    const auto start = std::chrono::steady_clock::now();
    size_t sink = 0;
    for (size_t f = 0; f < kFrames; ++f) {
      for (const auto& d : draws) {
        if (d.rewrite) vs[4 + local() % 8] = local(), ps[local() % 2] = local();
        sink += capture(d);
      }
    }
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
    if (sink == 1) std::printf(" ");
    return ns / double(kFrames * kDraws);
  };
  FloatConstantSnapshots full, masked;
  const double full_ns = time([&](const Draw&) {
    return size_t(full.Capture(0, vs, kAll).get() != nullptr) + size_t(full.Capture(1, ps, kAll).get() != nullptr);
  });
  const double masked_ns = time([&](const Draw& d) {
    return size_t(masked.Capture(0, vs, d.vs).get() != nullptr) + size_t(masked.Capture(1, ps, d.ps).get() != nullptr);
  });
  std::printf("full block: %.0f ns/draw, used registers: %.0f ns/draw (%.1fx)\n", full_ns, masked_ns,
              full_ns / masked_ns);
  return masked_ns < full_ns ? 0 : 2;
}
