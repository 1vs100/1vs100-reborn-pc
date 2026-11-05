// Copying captured vertex streams into snapshot buffers: memcpy against non-temporal stores, on a frame's worth of
// skinned meshes (about 140 streams of 80 KB from an 11 MB area, into a pool the CPU does not read back soon).
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "render/gpu_stream_copy.h"

int main() {
  constexpr size_t kStreams = 144, kSize = 80 * 1024, kFrames = 60;
  std::vector<uint8_t> source(kStreams * kSize + 64), pool(kStreams * kSize + 64);
  for (size_t i = 0; i < source.size(); ++i) source[i] = uint8_t(i * 2654435761u >> 24);

  // Equivalence across alignments and sizes, including the unaligned head and tail.
  for (size_t offset : {0u, 1u, 7u, 15u, 16u, 33u}) {
    for (size_t size : {0u, 1u, 15u, 16u, 17u, 63u, 64u, 65u, 4095u, 80000u}) {
      std::vector<uint8_t> to(size + 64, 0xCC);
      onevs100::render::StreamCopy(to.data() + (offset % 16), source.data() + offset, size);
      if (std::memcmp(to.data() + (offset % 16), source.data() + offset, size) != 0 ||
          to[offset % 16 + size] != 0xCC) {
        std::printf("FAIL: copy of %zu bytes at offset %zu differs or overruns\n", size, offset);
        return 1;
      }
    }
  }
  std::printf("PASS: copies match memcpy across alignments and sizes\n");

  auto time = [&](auto copy) {
    const auto start = std::chrono::steady_clock::now();
    for (size_t f = 0; f < kFrames; ++f) {
      for (size_t s = 0; s < kStreams; ++s) copy(pool.data() + s * kSize, source.data() + s * kSize + 8, kSize);
    }
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / kFrames;
  };
  const double plain = time([](void* to, const void* from, size_t n) { std::memcpy(to, from, n); });
  const double streamed = time([](void* to, const void* from, size_t n) { onevs100::render::StreamCopy(to, from, n); });
  std::printf("memcpy: %.2f ms/frame, streaming stores: %.2f ms/frame (%.2fx)\n", plain, streamed, plain / streamed);
  return 0;
}
