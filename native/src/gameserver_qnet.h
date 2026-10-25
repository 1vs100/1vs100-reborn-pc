#pragma once
#include "onevs100_init.h"
// Removes every native remote member from the session; the QNet leave handler
// calls it first, as leaving with them still present faults the host query.
void RemoveNativeMembersBeforeLeave(PPCContext& ctx, uint8_t* base, uint32_t session);
