#include "room_clock.h"
#include <chrono>
#include <cstdio>
#include <ctime>

int64_t UtcMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
std::string UtcIso() {
  const int64_t ms = UtcMs();
  const std::time_t seconds = static_cast<std::time_t>(ms / 1000);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &seconds);
#else
  gmtime_r(&seconds, &utc);
#endif
  char text[40];
  std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", utc.tm_year + 1900,
                utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec,
                static_cast<int>(ms % 1000));
  return text;
}
