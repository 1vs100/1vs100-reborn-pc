#pragma once
#include <cstdint>
#include <string>

// Where the game's online features go, chosen by the online_service setting.
enum class OnlineService { Offline, Gameserver };

// IPv4 address of the gameserver, most significant byte first.
struct GameserverAddress {
  uint32_t value;
};

// Reads and checks the settings; false (logged) when gameserver mode is set
// without a usable gameserver_host. Call once before the title starts.
bool LoadOnlineService();
OnlineService CurrentOnlineService();
// Valid only in gameserver mode.
GameserverAddress CurrentGameserverAddress();
const std::string& CurrentGameserverHost();
