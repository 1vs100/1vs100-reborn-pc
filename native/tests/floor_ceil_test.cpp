// The title's CRT floor and ceil (sub_824B2CF0, sub_824B0960) against std::floor and onevs100::GuestCeil, which
// replace them.
// The guest sequence is modelled instruction by instruction: fctidz truncates and saturates, fsel picks its second
// operand when the first is >= 0 and its third otherwise (also for NaN).
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "guest_math.h"

namespace {

double Fctidz(double x) {
  if (std::isnan(x)) return double(std::numeric_limits<int64_t>::min());
  if (x >= 9223372036854775807.0) return double(std::numeric_limits<int64_t>::max());
  if (x <= -9223372036854775808.0) return double(std::numeric_limits<int64_t>::min());
  return double(int64_t(x));  // fcfid of the truncated integer
}
double Fsel(double a, double b, double c) { return a >= 0.0 ? b : c; }

constexpr double kOne = 1.0, kTwo52 = 4503599627370496.0;

// fctidz f12,f1; fabs f11,f1; fcfid f12; fsub f0,2^52,|x|; fneg f11; fsub f10,x,trunc; fsub f13,trunc,1;
// fsel f13,f10,trunc,f13; fsel f0,f0,f13,x; fsel f1,-|x|,x,f0
double ModelFloor(double x) {
  const double t = Fctidz(x), a = std::fabs(x);
  const double f13 = Fsel(x - t, t, t - kOne);
  const double f0 = Fsel(kTwo52 - a, f13, x);
  return Fsel(-a, x, f0);
}
// the same with fsub f10,trunc,x and fadd f13,trunc,1
double ModelCeil(double x) {
  const double t = Fctidz(x), a = std::fabs(x);
  const double f13 = Fsel(t - x, t, t + kOne);
  const double f0 = Fsel(kTwo52 - a, f13, x);
  return Fsel(-a, x, f0);
}

bool Same(double a, double b) {
  return (std::isnan(a) && std::isnan(b)) || std::memcmp(&a, &b, sizeof(a)) == 0;
}

}  // namespace

int main() {
  std::vector<double> values = {0.0, -0.0, 0.5, -0.5, 1.0, -1.0, 1.5, -1.5, 2.5, -2.5, 1e-300, -1e-300,
                                kTwo52 - 0.5, -(kTwo52 - 0.5), kTwo52, -kTwo52, 1e300, -1e300,
                                std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::denorm_min()};
  std::mt19937_64 rng(5);
  std::uniform_real_distribution<double> wide(-1e6, 1e6);
  for (int i = 0; i < 1000000; ++i) values.push_back(wide(rng));
  for (int i = 0; i < 100000; ++i) values.push_back(std::ldexp(wide(rng), int(rng() % 120) - 60));
  for (double v : values) {
    if (!Same(ModelFloor(v), std::floor(v)) || !Same(ModelCeil(v), onevs100::GuestCeil(v))) {
      std::printf("FAIL: %.17g floor %.17g/%.17g ceil %.17g/%.17g\n", v, ModelFloor(v), std::floor(v), ModelCeil(v),
                  onevs100::GuestCeil(v));
      return 1;
    }
  }
  std::printf("PASS: %zu values give the same floor and ceil, bit for bit\n", values.size());
  return 0;
}
