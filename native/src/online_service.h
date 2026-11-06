#pragma once
#include <cstdint>
#include <optional>
#include <string>

// Gameserver selected by the online_service setting.
enum class OnlineService { Gameserver };

// IPv4 address of the gameserver, most significant byte first.
struct GameserverAddress {
  uint32_t value;
};

// Reads settings; false when gameserver_host is unusable. Call once before start.
// On false, error holds the reason; it must not be null.
bool LoadOnlineService(std::string* error);
OnlineService CurrentOnlineService();
// Valid only in gameserver mode.
GameserverAddress CurrentGameserverAddress();
// Host IPv4 address, most significant byte first, that guest sockets bound to
// any address use instead; lets several instances share one host.
struct GuestBindAddress {
  uint32_t value;
};
std::optional<GuestBindAddress> CurrentGuestBindAddress();
const std::string& CurrentGameserverHost();
