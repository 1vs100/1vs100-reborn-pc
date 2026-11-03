// XMA decoder wake, title-side.
//
// The guest frees output-ring space with plain memory writes, which the
// hardware notices. The runtime's decoder worker only wakes on a context kick
// and otherwise blocks with no timeout, so a context that ran out of output
// space stays parked until an unrelated sound kicks it. A 1 ms thread kicks it
// from outside the SDK.
//
// The runtime's kick is private; writing a context-kick register with an empty
// mask reaches the same signal with both kick loops skipped.

#include "xma_kick.h"
#include "precise_sleep.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <rex/audio/audio_system.h>
#include <rex/audio/xma/decoder.h>
#include <rex/audio/xma/register_file.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

namespace rex::audio {

namespace {

XmaDecoder* LiveDecoder() {
  Runtime* runtime = Runtime::instance();
  auto* kernel_state = runtime ? runtime->kernel_state() : nullptr;
  auto* emulator = kernel_state ? kernel_state->emulator() : nullptr;
  auto* audio_system = emulator ? static_cast<AudioSystem*>(emulator->audio_system()) : nullptr;
  return audio_system ? audio_system->xma_decoder() : nullptr;
}

std::mutex g_kicker_mutex;
std::atomic<bool> g_kicker_run{false};
std::thread g_kicker;

}  // namespace

void KickXmaDecoder() {
  if (auto* decoder = LiveDecoder()) {
    decoder->WriteRegister(XmaRegister::Context0Kick * 4, 0);
  }
}

// Sweep the decoder every millisecond for as long as the title runs.
void StartXmaKicker() {
  std::lock_guard<std::mutex> guard(g_kicker_mutex);
  if (g_kicker_run.load()) {
    return;
  }
  g_kicker_run.store(true);
  g_kicker = std::thread([] {
    while (g_kicker_run.load(std::memory_order_relaxed)) {
      KickXmaDecoder();
      onevs100::thread::SleepPrecise(std::chrono::milliseconds(1));
    }
  });
  REXLOG_DEBUG("[xma] 1 ms decoder kicker running");
}

// Must run before the audio system goes away: the kick dereferences it.
void StopXmaKicker() {
  std::lock_guard<std::mutex> guard(g_kicker_mutex);
  if (!g_kicker_run.exchange(false)) {
    return;
  }
  if (g_kicker.joinable()) {
    g_kicker.join();
  }
}

}  // namespace rex::audio
