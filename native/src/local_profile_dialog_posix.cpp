#include "local_profile.h"
#include "local_profile_internal.h"
#include <rex/logging.h>

// No profile window on this platform: the gamertag comes from the Steam name
// or from profiles/local-name.txt in the player data folder.
bool EditLocalProfile() {
  NoteAvatarEditorStamp();
  REXKRNL_WARN("Profile: no profile editor on this platform; set the gamertag in {}",
               (PlayerDataFolder() / "profiles" / "local-name.txt").string());
  return false;
}
