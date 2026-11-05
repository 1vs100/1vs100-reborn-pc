#include "lan_address.h"

#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.networking.connectivity.h>
#include <winrt/windows.networking.h>

namespace onboarding {

std::vector<std::string> LanIPv4Addresses() {
  using winrt::Windows::Networking::HostNameType;
  using winrt::Windows::Networking::Connectivity::NetworkInformation;

  // The adapter of the active connection comes first: consoles also list internal adapters.
  winrt::guid connected{};
  try {
    if (const auto profile = NetworkInformation::GetInternetConnectionProfile()) {
      if (const auto adapter = profile.NetworkAdapter()) connected = adapter.NetworkAdapterId();
    }
  } catch (const winrt::hresult_error&) {
    // Without a connection profile the order stays as listed.
  }
  std::vector<std::string> addresses;
  for (const auto& host : NetworkInformation::GetHostNames()) {
    if (host.Type() != HostNameType::Ipv4) continue;
    std::string address = winrt::to_string(host.CanonicalName());
    if (address.rfind("127.", 0) == 0 || address.rfind("169.254.", 0) == 0) continue;
    bool primary = false;
    try {
      const auto info = host.IPInformation();
      primary = info && info.NetworkAdapter() && info.NetworkAdapter().NetworkAdapterId() == connected;
    } catch (const winrt::hresult_error&) {
    }
    addresses.insert(primary ? addresses.begin() : addresses.end(), address);
  }
  return addresses;
}

}  // namespace onboarding
