#pragma once

#include <cmath>

namespace onevs100 {

// The title's ceil: ceil(-0.5) is +0 there, because the value passes through an integer; std::ceil gives -0.
inline double GuestCeil(double x) {
  const double r = std::ceil(x);
  return r == 0.0 && x != 0.0 ? 0.0 : r;
}

}  // namespace onevs100
