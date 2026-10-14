#pragma once
#include "generated/default-v4/onevs100_init.h"
bool NativeAvatarEnabled();
void NativeAvatarInitialize(PPCContext&,uint8_t*);
void NativeAvatarSizes(PPCContext&,uint8_t*);
void NativeAvatarAssets(PPCContext&,uint8_t*);
// Party Avatars (NativeRoom.lua through NativeRoomTransport):
// "avatar" <xuid>: hex of this player's Avatar manifest when <xuid> is the local
// profile, else "". "avatarof" <xuid>|<hex>: a party member's manifest, checked
// and rebuilt from the catalog, then served for that XUID. "ok" or "error:...".
#include <string>
std::string NativeAvatarManifestHex(const std::string& xuid);
std::string NativeAvatarSetRemote(const std::string& value);
