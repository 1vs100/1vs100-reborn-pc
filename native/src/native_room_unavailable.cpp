#include "native_room.h"
#include "room_clock.h"
#include <cstdlib>

// Room transport of a build without the Steamworks SDK. Every room mode needs
// Steam networking, so a configured mode reports it unavailable.
std::string NativeRoomOperation(const std::string& operation, const std::string&) {
  if (operation == "utc") return UtcIso();
  if (operation == "utcms") return std::to_string(UtcMs());
  const char* mode = std::getenv("ONEVS100_ROOM_MODE");
  if (operation == "mode") return mode ? mode : "";
  if (operation == "status") return mode && *mode ? "error:steam-unavailable" : "disabled";
  if (operation == "self" || operation == "party") return "";
  if (operation == "invite" || operation == "sendto" || operation == "send") return "error:steam-unavailable";
  if (operation == "recv" || operation == "poll") return "";
  return "error:operation";
}
