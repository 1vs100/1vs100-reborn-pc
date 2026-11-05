#include "avatar_view.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

#include <fmt/format.h>
#include <rex/platform.h>

#include "../avatar_native/local_avatar.h"
#include "../avatar_native/xna_avatar_format.h"
#include "../local_profile.h"
#include <rex/logging.h>

namespace a = xe::kernel::xna::avatar;
namespace xe::kernel::xna {
a::Catalog* XnaAvatarCatalog();
}

namespace avatar_view {
namespace {

constexpr uint16_t kStart = 0x10, kBack = 0x20, kButtonB = 0x2000;
constexpr uint16_t kCombo = kStart | kBack;
constexpr uint32_t kWidth = 384, kHeight = 512;
constexpr int16_t kDeadZone = 8000;
constexpr float kTurnRate = 3.0f;  // radians per second at full deflection

std::atomic<bool> g_open{false};

// Never destroyed: the detached worker may still wait on it while statics are torn down at exit.
struct State {
  std::mutex mutex;
  std::condition_variable wake;
  float yaw = 0.0f;
  uint64_t avatar = 1;     // bumped when the stored Avatar may have changed
  uint64_t requested = 1;  // bumped for every preview the view needs
  bool worker = false;
  Frame frame;
  std::string source;
  std::string note;
  // The scene is rebuilt only when the stored Avatar changes.
  std::mutex render_mutex;
  uint64_t built = 0;
  bool stored = false;
  a::Scene scene;
  std::mutex store_mutex;  // one upload at a time: they share a temporary file
  std::mutex pad_mutex;    // several guest threads poll the pads
};
State& g = *new State;

bool Render(uint64_t avatar, float yaw, Frame* frame, std::string* source) {
  auto* catalog = xe::kernel::xna::XnaAvatarCatalog();
  if (!catalog || !catalog->loaded()) {
    *source = "The avatar asset pack is not installed.";
    return false;
  }
  std::lock_guard<std::mutex> lock(g.render_mutex);
  if (avatar != g.built) {
    a::Description description;
    g.stored = local_avatar::Load(catalog, &description);
    if (!g.stored) description = local_avatar::Default(*catalog, LocalXuid());
    g.scene = a::BuildScene(*catalog, description);
    g.built = avatar;
  }
  *source = g.stored ? "Your stored avatar." : "No avatar stored yet: this is the default one.";
  a::Matrix local[a::kMaxJoints];
  a::BindPose(g.scene.skeleton, local);
  // The first preset clip is the standing idle; a still from it reads better than the bind pose.
  const auto idle = catalog->LoadClip(uint32_t(a::PresetClip(0)));
  if (idle) a::SamplePose(*idle, g.scene.skeleton, 0.5f, local);
  frame->width = kWidth;
  frame->height = kHeight;
  a::RenderPreview(g.scene, local, nullptr, a::Expression{}, kWidth, kHeight, yaw, &frame->rgba);
  return true;
}

void Worker() {
  uint64_t rendered = 0;
  std::unique_lock<std::mutex> lock(g.mutex);
  for (;;) {
    g.wake.wait(lock, [&] { return g_open && g.requested != rendered; });
    const uint64_t request = g.requested, avatar = g.avatar;
    const float yaw = g.yaw;
    lock.unlock();
    Frame frame;
    std::string source;
    Render(avatar, yaw, &frame, &source);
    lock.lock();
    rendered = request;
    frame.serial = request;
    g.frame = std::move(frame);
    g.source = std::move(source);
  }
}

void Request(bool avatar_changed) {
  std::lock_guard<std::mutex> lock(g.mutex);
  if (avatar_changed) ++g.avatar;
  ++g.requested;
  if (!g.worker) {
    g.worker = true;
    std::thread(Worker).detach();
  }
  g.wake.notify_one();
}

}  // namespace

bool IsOpen() { return g_open; }

bool FilterPad(uint32_t user, uint16_t buttons, int16_t stick_x) {
  using Clock = std::chrono::steady_clock;
  if (user >= 4) return false;
  std::lock_guard<std::mutex> pad_lock(g.pad_mutex);
  static uint16_t prior[4] = {};
  static bool held_after_close[4] = {};
  static Clock::time_point last = Clock::now();
  const bool combo = (buttons & kCombo) == kCombo && (prior[user] & kCombo) != kCombo;
  const bool back_out = (buttons & kButtonB) && !(prior[user] & kButtonB);
  prior[user] = buttons;
  if (combo || (g_open && back_out)) {
    const bool open = !g_open && combo;
    g_open = open;
    REXKRNL_INFO("Avatar view {} (pad {})", open ? "opened" : "closed", user);
    // The view reads the file again each time it opens: a desktop player copies it in by hand.
    if (open) {
      last = Clock::now();
      Request(true);
    } else {
      for (bool& held : held_after_close) held = true;
    }
    return true;
  }
  if (!g_open) {
    // The buttons that closed the view stay away from the game until released.
    if (held_after_close[user] && (buttons & (kCombo | kButtonB))) return true;
    held_after_close[user] = false;
    return false;
  }
  if (std::abs(int(stick_x)) > kDeadZone) {
    const auto now = Clock::now();
    const float seconds = std::min(std::chrono::duration<float>(now - last).count(), 0.25f);
    last = now;
    {
      std::lock_guard<std::mutex> lock(g.mutex);
      g.yaw += float(stick_x) / 32767.0f * kTurnRate * seconds;
    }
    Request(false);
  } else {
    last = Clock::now();
  }
  return true;
}

bool TakeFrame(uint64_t serial, Frame* out) {
  std::lock_guard<std::mutex> lock(g.mutex);
  if (g.frame.serial == serial || g.frame.rgba.empty()) return false;
  *out = g.frame;
  return true;
}

std::vector<std::string> StatusLines() {
  std::lock_guard<std::mutex> lock(g.mutex);
  std::vector<std::string> lines;
  if (!g.source.empty()) lines.push_back(g.source);
  if (!g.note.empty()) lines.push_back(g.note);
  return lines;
}

StoreResult Store(const uint8_t* bytes, size_t size) {
  std::lock_guard<std::mutex> store_lock(g.store_mutex);
  StoreResult result;
  auto* catalog = xe::kernel::xna::XnaAvatarCatalog();
  if (!catalog || !catalog->loaded()) {
    result.message = "The avatar asset pack is not installed yet.";
  } else if (size != a::kManifestBytes && size != a::kDescriptionBytes) {
    result.message = fmt::format("Not an avatar manifest: {} bytes, expected {}.", size, a::kManifestBytes);
  } else {
    a::Description description;
    if (!a::ParseAnyDescription(catalog, bytes, size, &description)) {
      result.message = "Not an avatar manifest.";
    } else {
      // Marketplace items this console lacks become similar standard items, as for party members.
      const size_t similar = a::SubstituteMissingItems(*catalog, &description);
      if (!local_avatar::Save(description)) {
        result.message = "The avatar could not be saved.";
      } else {
        result.ok = true;
        result.message = similar ? fmt::format("Avatar saved. {} item(s) this console lacks became similar "
                                               "standard items.",
                                               similar)
                                 : std::string("Avatar saved.");
      }
    }
  }
  {
    std::lock_guard<std::mutex> lock(g.mutex);
    g.note = result.message;
  }
  Request(result.ok);
  return result;
}

std::vector<uint8_t> PreviewPng() {
  uint64_t avatar;
  {
    std::lock_guard<std::mutex> lock(g.mutex);
    avatar = g.avatar;
  }
  Frame frame;
  std::string source;
  if (!Render(avatar, 0.0f, &frame, &source)) return {};
  return a::EncodePng(frame.width, frame.height, frame.rgba);
}

#if !REX_PLATFORM_WINRT
void StartUploadServer() {}
std::string UploadHint() {
  return fmt::format("Copy avatarmanifest.bin to {}", local_avatar::SavePath().string());
}
std::string UploadUrl() { return {}; }
#endif

}  // namespace avatar_view
