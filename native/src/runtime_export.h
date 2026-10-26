#pragma once
#include <rex/ppc/function.h>

// The runtime's own implementation of a kernel export this executable overrides.
PPCFunc* RuntimeExport(const char* name);
