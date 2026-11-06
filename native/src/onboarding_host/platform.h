// The WinRT half of the onboarding platform: pickers, USB presence, LAN
// addresses and the socket listener. The view itself lives in
// native/third_party/deployment-onboarding-ui.
#pragma once

#include "onboarding/platform.h"

namespace onboarding {

Platform MakeWinrtPlatform();

}  // namespace onboarding
