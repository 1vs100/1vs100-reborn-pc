#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif

namespace onevs100::render {

// memcpy for snapshot buffers the CPU does not read again before the upload: non-temporal stores skip the cache
// line reads a plain copy makes for its destination.
inline void StreamCopy(void* to, const void* from, size_t size) {
#if !defined(__SSE2__) && !defined(_M_X64)
  std::memcpy(to, from, size);
#else
  auto* d = static_cast<uint8_t*>(to);
  const auto* s = static_cast<const uint8_t*>(from);
  const size_t head = (16 - (reinterpret_cast<uintptr_t>(d) & 15)) & 15;
  if (size < head + 64) {
    std::memcpy(d, s, size);
    return;
  }
  std::memcpy(d, s, head);
  d += head, s += head, size -= head;
  for (; size >= 64; d += 64, s += 64, size -= 64) {
    const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
    const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 16));
    const __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 32));
    const __m128i e = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 48));
    _mm_stream_si128(reinterpret_cast<__m128i*>(d), a);
    _mm_stream_si128(reinterpret_cast<__m128i*>(d + 16), b);
    _mm_stream_si128(reinterpret_cast<__m128i*>(d + 32), c);
    _mm_stream_si128(reinterpret_cast<__m128i*>(d + 48), e);
  }
  std::memcpy(d, s, size);
  // Other threads read the snapshot after a lock hand-off; the fence orders the streamed stores before it.
  _mm_sfence();
#endif
}

}  // namespace onevs100::render
