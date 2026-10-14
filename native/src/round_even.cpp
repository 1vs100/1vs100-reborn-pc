#include <cmath>

// Clang's round-even builtin lowers to this C symbol, absent from Windows CRT.
// Compute ties-to-even independently of the host floating-point rounding mode.
extern "C" float roundevenf(float value) {
  if (!std::isfinite(value) || std::fabs(value) >= 8388608.0f) return value;
  const float lower = std::floor(value);
  const float fraction = value - lower;
  float result = lower;
  if (fraction > 0.5f || (fraction == 0.5f && std::fmod(lower, 2.0f) != 0.0f))
    result = lower + 1.0f;
  return result == 0.0f ? std::copysign(0.0f, value) : result;
}
