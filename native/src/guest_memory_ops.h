#pragma once

namespace onevs100 {

// Applies the guest_atomic_window_lock and guest_keep_flush_mode settings. Runs before any guest thread starts.
void ConfigureGuestRuntime();

}  // namespace onevs100
