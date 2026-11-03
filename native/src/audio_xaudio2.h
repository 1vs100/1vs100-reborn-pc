// XAudio2 audio backend for Windows and UWP, in place of the SDK's SDL backend.
// Copied from CrashCourse2Recomp (ported there from masterspike52/reDAHM, based on Xenia's driver).
// The game's audio callbacks are paced by XAudio2 buffer completion.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>
#include <xaudio2.h>

#include <deque>
#include <functional>
#include <mutex>
#include <stack>

#include <rex/audio/audio_driver.h>
#include <rex/audio/audio_system.h>

namespace xaudio2 {

class XAudio2AudioDriver : public rex::audio::AudioDriver, public IXAudio2VoiceCallback {
 public:
  XAudio2AudioDriver(rex::memory::Memory* memory, IXAudio2* xaudio2, uint8_t device_channels,
                     std::function<void()> on_buffer_completed);
  ~XAudio2AudioDriver() override;

  bool Initialize();
  void Shutdown();

  void SubmitFrame(uint32_t frame_ptr) override;

  STDMETHOD_(void, OnBufferEnd)(void* buffer_context) override;
  STDMETHOD_(void, OnBufferStart)(void* buffer_context) override {}
  STDMETHOD_(void, OnLoopEnd)(void* buffer_context) override {}
  STDMETHOD_(void, OnStreamEnd)() override {}
  STDMETHOD_(void, OnVoiceError)(void* buffer_context, HRESULT error) override {}
  STDMETHOD_(void, OnVoiceProcessingPassEnd)() override {}
  STDMETHOD_(void, OnVoiceProcessingPassStart)(UINT32 bytes_required) override {}

 private:
  static constexpr int frame_frequency_ = 48000;
  static constexpr uint8_t frame_channels_ = 6;
  static constexpr size_t channel_samples_ = 256;
  static constexpr size_t frame_samples_ = channel_samples_ * frame_channels_;

  IXAudio2* xaudio2_ = nullptr;
  IXAudio2SourceVoice* source_voice_ = nullptr;
  uint8_t device_channels_ = frame_channels_;
  std::function<void()> on_buffer_completed_;

  std::mutex frames_mutex_;
  std::deque<float*> frames_queued_;
  std::stack<float*> frames_unused_;
};

class XAudio2AudioSystem : public rex::audio::AudioSystem {
 public:
  explicit XAudio2AudioSystem(rex::runtime::FunctionDispatcher* function_dispatcher);
  ~XAudio2AudioSystem() override;

  static std::unique_ptr<rex::system::IAudioSystem> Create(
      rex::runtime::FunctionDispatcher* dispatcher);

 protected:
  void Initialize() override;

  rex::X_STATUS CreateDriver(size_t index, rex::thread::Semaphore* semaphore,
                             rex::audio::AudioDriver** out_driver) override;
  void DestroyDriver(rex::audio::AudioDriver* driver) override;

 private:
  // Creates the device and mastering voice on first use; false if that failed.
  bool EnsureEngine();

  Microsoft::WRL::ComPtr<IXAudio2> xaudio2_;
  IXAudio2MasteringVoice* mastering_voice_ = nullptr;
};

}  // namespace xaudio2
