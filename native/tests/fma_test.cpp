// REX_FMA without hardware FMA: the guest's fmadds as double(float(a * b + c)) against the fused
// double(float(std::fma(a, b, c))), and its speed against the C runtime's software fma.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

int main() {
  std::mt19937 rng(3);
  std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
  std::uniform_int_distribution<int> exponent(-20, 20);
  constexpr size_t kCount = 4000000;
  std::vector<float> a(kCount), b(kCount), c(kCount);
  for (size_t i = 0; i < kCount; ++i) {
    a[i] = std::ldexp(unit(rng), exponent(rng)), b[i] = std::ldexp(unit(rng), exponent(rng));
    c[i] = std::ldexp(unit(rng), exponent(rng));
  }
  size_t differ = 0;
  for (size_t i = 0; i < kCount; ++i) {
    const double fused = double(float(std::fma(double(a[i]), double(b[i]), double(c[i]))));
    const double split = double(float(double(a[i]) * double(b[i]) + double(c[i])));
    if (std::memcmp(&fused, &split, sizeof(fused)) != 0) ++differ;
  }
  std::printf("fmadds: %zu of %zu results differ in the last bit (%.6f%%)\n", differ, kCount, 100.0 * differ / kCount);

  volatile double sink = 0;
  auto time = [&](auto op) {
    const auto start = std::chrono::steady_clock::now();
    double acc = 0;
    for (size_t i = 0; i < kCount; ++i) acc += op(double(a[i]), double(b[i]), double(c[i]));
    sink = acc;
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / kCount;
  };
  const double fused_ns = time([](double x, double y, double z) { return std::fma(x, y, z); });
  const double split_ns = time([](double x, double y, double z) { return x * y + z; });
  std::printf("std::fma: %.2f ns, a * b + c: %.2f ns per operation on this build machine\n", fused_ns, split_ns);
  return differ * 1000000 < kCount ? 0 : 1;  // more than one in a million would contradict the comment on REX_FMA
}
