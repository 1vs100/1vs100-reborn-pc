#include "audio_xaudio2.h"

#include <mmreg.h>  // WAVEFORMATEXTENSIBLE

#include <cstring>
#include <set>

#include <rex/assert.h>
#include <rex/audio/conversion.h>
#include <rex/audio/downmix.h>
#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/types.h>

// The `audio_mute` cvar is defined by the SDL backend
// (sdl_audio_driver.cpp); reuse it here rather than redefining.
REXCVAR_DECLARE(bool, audio_mute);

namespace xaudio2 {



XAudio2AudioDriver::XAudio2AudioDriver(rex::memory::Memory* memory, IXAudio2* xaudio2,
                                       uint8_t device_channels,
                                       std::function<void()> on_buffer_completed)
    : AudioDriver(memory),
      xaudio2_(xaudio2),
      device_channels_(device_channels),
      on_buffer_completed_(std::move(on_buffer_completed)) {}

XAudio2AudioDriver::~XAudio2AudioDriver() {
  assert_true(frames_queued_.empty());
  assert_true(frames_unused_.empty());
}

bool XAudio2AudioDriver::Initialize() {
  // WAVEFORMATEXTENSIBLE with an explicit speaker mask: plain WAVEFORMATEX
  // leaves the >2ch channel->speaker mapping to the audio driver's guess.
  static constexpr GUID kIeeeFloatSubformat = {
      0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
  WAVEFORMATEXTENSIBLE fmt = {};
  fmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  fmt.Format.nChannels = device_channels_;
  fmt.Format.nSamplesPerSec = frame_frequency_;
  fmt.Format.wBitsPerSample = 32;
  fmt.Format.nBlockAlign = fmt.Format.nChannels * (fmt.Format.wBitsPerSample / 8);
  fmt.Format.nAvgBytesPerSec = fmt.Format.nSamplesPerSec * fmt.Format.nBlockAlign;
  fmt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  fmt.Samples.wValidBitsPerSample = 32;
  // SPEAKER_FRONT_LEFT..._BACK_RIGHT (5.1) = 0x3F; FL|FR (stereo) = 0x3.
  fmt.dwChannelMask = device_channels_ == 6 ? 0x3Fu : 0x3u;
  fmt.SubFormat = kIeeeFloatSubformat;


  HRESULT hr = xaudio2_->CreateSourceVoice(&source_voice_, &fmt.Format, 0,
                                           XAUDIO2_DEFAULT_FREQ_RATIO, this);
  if (FAILED(hr)) {
    REXAPU_ERROR("XAudio2: CreateSourceVoice failed: {:08X}", static_cast<uint32_t>(hr));
    return false;
  }

  // Follows guest clock scaling. The scalar is normally 1.0, but a scaled
  // guest clock must pitch the source voice with it or the queue drifts into
  // under/overrun.
  source_voice_->SetFrequencyRatio(
      static_cast<float>(rex::chrono::Clock::guest_time_scalar()));

  hr = source_voice_->Start(0);
  if (FAILED(hr)) {
    REXAPU_ERROR("XAudio2: Start failed: {:08X}", static_cast<uint32_t>(hr));
    source_voice_->DestroyVoice();
    source_voice_ = nullptr;
    return false;
  }

  return true;
}

void XAudio2AudioDriver::Shutdown() {
  if (source_voice_) {
    source_voice_->Stop(0);
    source_voice_->FlushSourceBuffers();
    source_voice_->DestroyVoice();
    source_voice_ = nullptr;
  }
  // DestroyVoice has returned, so no callback can run. OnBufferEnd may still
  // have recycled a flushed buffer, so free each pointer once.
  std::set<float*> frames;
  std::unique_lock<std::mutex> guard(frames_mutex_);
  while (!frames_unused_.empty()) {
    frames.insert(frames_unused_.top());
    frames_unused_.pop();
  }
  while (!frames_queued_.empty()) {
    frames.insert(frames_queued_.front());
    frames_queued_.pop_front();
  }
  guard.unlock();
  for (float* frame : frames) {
    delete[] frame;
  }
}

void XAudio2AudioDriver::SubmitFrame(uint32_t frame_ptr) {
  const auto input_frame = memory_->TranslateVirtual<float*>(frame_ptr);

  float* converted;
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    if (frames_unused_.empty()) {
      converted = new float[frame_samples_];
    } else {
      converted = frames_unused_.top();
      frames_unused_.pop();
    }
  }

  if (REXCVAR_GET(audio_mute)) {
    std::memset(converted, 0, frame_samples_ * sizeof(float));
  } else {
    // Snapshot once. A change mid-frame would split it across two mixes.
    const rex::audio::StereoFold fold = rex::audio::GetStereoFold();
    const rex::audio::SurroundMix mix = rex::audio::GetSurroundMix();
    const float gain = rex::audio::GetOutputGain();
    switch (device_channels_) {
      case 2:
        rex::audio::conversion::sequential_6_BE_to_interleaved_2_LE(converted, input_frame,
                                                                    channel_samples_, fold, gain);
        break;
      case 6:
        rex::audio::conversion::sequential_6_BE_to_interleaved_6_LE(converted, input_frame,
                                                                    channel_samples_, mix, gain);
        break;
      default:
        assert_unhandled_case(device_channels_);
        break;
    }
  }

  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    frames_queued_.push_back(converted);
  }

  XAUDIO2_BUFFER buffer = {};
  buffer.AudioBytes = static_cast<UINT32>(channel_samples_ * device_channels_ * sizeof(float));
  buffer.pAudioData = reinterpret_cast<const BYTE*>(converted);
  buffer.pContext = converted;

  if (source_voice_) {
    HRESULT hr = source_voice_->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
      REXAPU_ERROR("XAudio2: SubmitSourceBuffer failed: {:08X}", static_cast<uint32_t>(hr));
      std::unique_lock<std::mutex> guard(frames_mutex_);
      assert_true(frames_queued_.back() == converted);
      frames_queued_.pop_back();
      frames_unused_.push(converted);
    }
  }
}

STDMETHODIMP_(void) XAudio2AudioDriver::OnBufferEnd(void* buffer_context) {
  auto* buffer = static_cast<float*>(buffer_context);
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    if (!frames_queued_.empty() && frames_queued_.front() == buffer) {
      frames_queued_.pop_front();
    }
    frames_unused_.push(buffer);
  }
  if (on_buffer_completed_) {
    on_buffer_completed_();
  }
}


// The X_STATUS_* macros expand to a cast to (unqualified) X_STATUS.
using rex::X_STATUS;

XAudio2AudioSystem::XAudio2AudioSystem(rex::runtime::FunctionDispatcher* function_dispatcher)
    : AudioSystem(function_dispatcher) {}

XAudio2AudioSystem::~XAudio2AudioSystem() {
  if (mastering_voice_) {
    mastering_voice_->DestroyVoice();
    mastering_voice_ = nullptr;
  }
  xaudio2_.Reset();
}

std::unique_ptr<rex::system::IAudioSystem> XAudio2AudioSystem::Create(
    rex::runtime::FunctionDispatcher* dispatcher) {
  return std::make_unique<XAudio2AudioSystem>(dispatcher);
}

void XAudio2AudioSystem::Initialize() {
  // No COM or device creation here: this runs on the audio worker thread
  // during early boot. EnsureEngine() opens the device on first CreateDriver().
  AudioSystem::Initialize();
}

bool XAudio2AudioSystem::EnsureEngine() {
  if (xaudio2_) {
    return true;
  }

  // Modern XAudio2 (2.8+/2.9) does not require COM, but enter an MTA
  // defensively. It is not undone: this runs on the render driver thread while
  // the system is destroyed on another, and CoUninitialize must run on the
  // thread that called CoInitializeEx. RPC_E_CHANGED_MODE means the thread is
  // already in an apartment, which is fine.
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    REXAPU_ERROR("XAudio2: CoInitializeEx failed: {:08X}", static_cast<uint32_t>(hr));
    return false;
  }

  hr = XAudio2Create(xaudio2_.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR);
  if (FAILED(hr)) {
    REXAPU_ERROR("XAudio2Create failed: {:08X}", static_cast<uint32_t>(hr));
    xaudio2_.Reset();
    return false;
  }

  hr = xaudio2_->CreateMasteringVoice(&mastering_voice_);
  if (FAILED(hr)) {
    REXAPU_ERROR("XAudio2: CreateMasteringVoice failed: {:08X}", static_cast<uint32_t>(hr));
    xaudio2_.Reset();
    return false;
  }

  REXAPU_DEBUG("XAudio2: device + mastering voice created (lazy)");
  return true;
}

rex::X_STATUS XAudio2AudioSystem::CreateDriver(size_t index, rex::thread::Semaphore* semaphore,
                                               rex::audio::AudioDriver** out_driver) {
  if (!EnsureEngine()) {
    return X_STATUS_UNSUCCESSFUL;
  }

  uint8_t device_channels = 6;
  if (mastering_voice_) {
    XAUDIO2_VOICE_DETAILS details = {};
    mastering_voice_->GetVoiceDetails(&details);
    if (details.InputChannels < 6) {
      device_channels = 2;
    }
  }

  auto on_completed = [semaphore]() { semaphore->Release(1, nullptr); };

  auto* driver = new XAudio2AudioDriver(memory_, xaudio2_.Get(), device_channels,
                                        std::move(on_completed));
  if (!driver->Initialize()) {
    delete driver;
    return X_STATUS_UNSUCCESSFUL;
  }

  *out_driver = driver;
  return X_STATUS_SUCCESS;
}

void XAudio2AudioSystem::DestroyDriver(rex::audio::AudioDriver* driver) {
  auto* d = static_cast<XAudio2AudioDriver*>(driver);
  d->Shutdown();
  delete d;
}

}  // namespace xaudio2
