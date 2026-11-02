#include "local_profile.h"
#include "local_profile_internal.h"
#include <rex/logging.h>

// No profile window on this platform; set the gamertag in the player data folder.
bool EditLocalProfile() {
  REXKRNL_WARN("Profile: no profile editor on this platform; set the gamertag in {}",
               (PlayerDataFolder() / "profiles" / "local-name.txt").string());
  return false;
}
