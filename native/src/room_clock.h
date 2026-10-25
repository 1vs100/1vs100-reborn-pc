#pragma once
#include <cstdint>
#include <string>

// This machine's UTC clock: epoch milliseconds, and ISO-8601 with milliseconds.
int64_t UtcMs();
std::string UtcIso();
