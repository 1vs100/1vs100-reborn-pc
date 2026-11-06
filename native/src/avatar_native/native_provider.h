#pragma once
#include "onevs100_init.h"
#include <filesystem>
#include <optional>
#include <vector>
const std::vector<std::filesystem::path>& NativeAvatarPacks();
bool NativeAvatarEnabled();
void NativeAvatarInitialize(PPCContext&,uint8_t*);
void NativeAvatarSizes(PPCContext&,uint8_t*);
void NativeAvatarAssets(PPCContext&,uint8_t*);
// Remote Avatars through the gameserver matchmaking relay.
#include <string>
std::string NativeAvatarManifestHex(const std::string& xuid);
std::string NativeAvatarSetRemote(const std::string& value);
// A remote player's checked manifest (kManifestBytes), as stored by NativeAvatarSetRemote.
bool NativeAvatarRemoteManifest(uint64_t xuid,uint8_t* out);
