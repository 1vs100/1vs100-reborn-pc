#include "../../work/native-port/src/native_room.h"
#include <chrono>
#include <thread>
#include <iostream>
int main() {
  auto mode = NativeRoomOperation("mode", "");
  bool host = mode == "local-host";
  bool sent = false;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
  while (std::chrono::steady_clock::now() < deadline) {
    auto status = NativeRoomOperation("status", "");
    if (status.starts_with("error:") || status.starts_with("disconnected:")) { std::cerr << status << '\n'; return 2; }
    if (!host && !sent && status == "connected") {
      sent = NativeRoomOperation("send", "original-store-probe") == "queued";
    }
    auto message = NativeRoomOperation("poll", "");
    if (!message.empty()) {
      if (host && message == "original-store-probe") {
        if (NativeRoomOperation("send", "host-store-reply") != "queued") return 3;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::cout << "PASS native host transport\n"; return 0;
      }
      if (!host && message == "host-store-reply") { std::cout << "PASS native client transport\n"; return 0; }
      return 4;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::cerr << "timeout status=" << NativeRoomOperation("status", "") << '\n'; return 5;
}
