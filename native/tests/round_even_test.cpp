#include <cmath>
#include <cfenv>
#include <cstdio>
#include <limits>
#include <initializer_list>
extern "C" float roundevenf(float);
int main() {
  auto function = &roundevenf;
  const float cases[][2] = {{0,0},{0.5f,0},{1.5f,2},{2.5f,2},{3.5f,4},
    {-0.5f,-0.0f},{-1.5f,-2},{-2.5f,-2},{0.49f,0},{0.51f,1},
    {-0.49f,-0.0f},{-0.51f,-1},{8388608,8388608}};
  int checks=0;
  for (int mode : {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO}) {
    std::fesetround(mode);
    for (auto& test : cases) {
      float got = function(test[0]);
      if (got != test[1] || (got == 0 && std::signbit(got) != std::signbit(test[0]))) return 1;
      ++checks;
    }
    if (!std::isnan(function(std::numeric_limits<float>::quiet_NaN()))) return 2;
    if (function(std::numeric_limits<float>::infinity()) != std::numeric_limits<float>::infinity()) return 3;
    checks+=2;
  }
  std::fesetround(FE_TONEAREST);
  std::printf("PASS: %d rounding checks across four host rounding modes\n",checks);
}
