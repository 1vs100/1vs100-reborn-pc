// Gameserver mode: the title's account/locale request (reborn's "local account
// locale", site 8246A590) completes at once from the configured country and
// language instead of waiting on Live. The XAM country and language queries the
// runtime stubs answer from the same settings.
#include "onevs100_init.h"
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/system/flags.h>
#include <cstring>
#include <string_view>
#include "online_service.h"
#include "runtime_export.h"

namespace rex::kernel::xam {
uint8_t xeXamGetLocaleFromCountry(uint8_t country);
uint8_t xeXamGetOnlineCountryFromLocale(uint8_t locale);
}

namespace {
using rex::memory::store_and_swap;

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorInvalidParameter = 87;
constexpr uint32_t kErrorInsufficientBuffer = 122;
constexpr uint32_t kErrorInvalidData = 13;
constexpr uint32_t kErrorNotLoggedOn = 1245;
constexpr uint8_t kOnlineCountryUnknown = 0;

// GET_USER_INFO_RESPONSE is packed: {u16 length, u32 pointer} pairs for first
// name, last name, the six address fields and e-mail, then language and bytes.
constexpr uint32_t kResponseSize = 0x3C;
constexpr uint32_t kFirstName = 0x00, kLastName = 0x06, kStreet1 = 0x0C, kStreet2 = 0x12,
                   kCity = 0x18, kDistrict = 0x1E, kState = 0x24, kPostalCode = 0x2A,
                   kEmail = 0x30, kLanguage = 0x36, kCountry = 0x38, kAge = 0x3B;
// XAccountGetUserInfoResponseSize(): the struct plus room for every string.
constexpr uint32_t kRequiredSize = kResponseSize + 2 * (64 + 64 + 129 + 128 + 128 + 64 + 64 + 64 + 16);

// XGetLanguage values that differ from the XONLINE_LANGUAGE_* numbering.
uint16_t OnlineLanguage(uint32_t language) {
  switch (language) {
    case 0x0D: return 17;  // Swedish
    case 0x0E: return 36;  // Turkish
    case 0x11: return 10;  // Simplified Chinese
    default: return static_cast<uint16_t>(language);
  }
}

struct ResponseWriter {
  uint8_t* base;
  uint32_t response;
  uint32_t offset;
  // Appends a big-endian UTF-16 string after the struct and points the field at it.
  void Put(uint32_t field, std::string_view text) {
    const uint32_t address = response + offset;
    for (size_t i = 0; i < text.size(); ++i)
      store_and_swap<uint16_t>(base + address + 2 * i, static_cast<uint8_t>(text[i]));
    store_and_swap<uint16_t>(base + address + 2 * text.size(), 0);
    store_and_swap<uint16_t>(base + response + field, static_cast<uint16_t>(text.size()));
    store_and_swap<uint32_t>(base + response + field + 2, address);
    offset += static_cast<uint32_t>(2 * (text.size() + 1));
  }
};

uint8_t ConfiguredOnlineCountry() {
  const auto locale = rex::kernel::xam::xeXamGetLocaleFromCountry(static_cast<uint8_t>(REXCVAR_GET(user_country)));
  return rex::kernel::xam::xeXamGetOnlineCountryFromLocale(locale);
}
}  // namespace

// XamUserGetOnlineCountryFromXUID(xuid) -> online country of any player.
REX_HOOK_RAW(__imp__XamUserGetOnlineCountryFromXUID) {
  static PPCFunc* const original = RuntimeExport("__imp__XamUserGetOnlineCountryFromXUID");
  if (CurrentOnlineService() != OnlineService::Gameserver) { original(ctx, base); return; }
  ctx.r3.u64 = ConfiguredOnlineCountry();
}
// XamGetLanguage() -> XC_LANGUAGE_*
REX_HOOK_RAW(__imp__XamGetLanguage) {
  static PPCFunc* const original = RuntimeExport("__imp__XamGetLanguage");
  if (CurrentOnlineService() != OnlineService::Gameserver) { original(ctx, base); return; }
  ctx.r3.u64 = static_cast<uint32_t>(REXCVAR_GET(user_language));
}


// AccountGetUserInfo(user, size, response, overlapped)
REX_HOOK_RAW(sub_8246A590) {
  if (CurrentOnlineService() != OnlineService::Gameserver) { __imp__sub_8246A590(ctx, base); return; }
  const uint32_t user = ctx.r3.u32, size = ctx.r4.u32, response = ctx.r5.u32, overlapped = ctx.r6.u32;
  if (user >= 4 || !response) { ctx.r3.u64 = kErrorInvalidParameter; return; }
  if (size < kRequiredSize) { ctx.r3.u64 = kErrorInsufficientBuffer; return; }
  // Only user 0 is signed in (offline_profile.cpp).
  if (user != 0) { ctx.r3.u64 = kErrorNotLoggedOn; return; }
  const uint8_t country = ConfiguredOnlineCountry();
  if (country == kOnlineCountryUnknown) { ctx.r3.u64 = kErrorInvalidData; return; }

  std::memset(base + response, 0, kResponseSize);
  ResponseWriter writer{base, response, kResponseSize};
  writer.Put(kFirstName, "Player");
  writer.Put(kLastName, "OneVs100");
  writer.Put(kStreet1, "1 Offline Way");
  writer.Put(kStreet2, "Apartment 100");
  writer.Put(kCity, "Seattle");
  writer.Put(kDistrict, "King");
  writer.Put(kState, "WA");
  writer.Put(kPostalCode, "98052");
  writer.Put(kEmail, "player@example.invalid");
  store_and_swap<uint16_t>(base + response + kLanguage,
                           OnlineLanguage(static_cast<uint32_t>(REXCVAR_GET(user_language))));
  base[response + kCountry] = country;
  base[response + kAge] = 18;
  if (overlapped) {
    // XAM_OVERLAPPED: result, length, ..., extended_error at +0x18. Completed synchronously, as on the console.
    store_and_swap<uint32_t>(base + overlapped, kErrorSuccess);
    store_and_swap<uint32_t>(base + overlapped + 4, 0);
    store_and_swap<uint32_t>(base + overlapped + 0x18, kErrorSuccess);
  }
  REXKRNL_INFO("Gameserver mode: account locale language={} country={}", REXCVAR_GET(user_language), country);
  ctx.r3.u64 = kErrorSuccess;
}
