#include <cstdarg>
#include <cstdio>
#include <string>

#include "../../xna_avatar_format.h"

extern "C" void xenia_log(const char* fmt, ...) {
  char buffer[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  xe::kernel::xna::avatar::Warn(std::string("mspack: ") + buffer);
}
