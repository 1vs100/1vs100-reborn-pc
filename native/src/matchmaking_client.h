#pragma once
// Gameserver matchmaking client (reborn's MatchmakingClient) on a host thread.
// It joins this PC's player to a group of up to four consoles, keeps the roster
// of the other members, and relays QNet payloads and Avatars between them.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct MatchmakingRemotePeer {
  uint64_t xuid;
  std::string gamertag;
};

struct RelayedPayload {
  uint64_t from;
  std::vector<uint8_t> data;
};

// Starts the client thread; joins at once. Gameserver mode only.
void StartMatchmakingClient();

// Leave the roster while out of a game; resume rejoins.
void MatchmakingPause();
void MatchmakingResume();

std::vector<MatchmakingRemotePeer> MatchmakingPeers();
// The XUID this PC joined as.
uint64_t MatchmakingSelfXuid();

// Queues a QNet payload for the other consoles of the group. No targets means all
// others. False when not joined, the payload is too large or the queue is full.
bool MatchmakingRelaySend(std::span<const uint64_t> targets, std::span<const uint8_t> data);
std::optional<RelayedPayload> MatchmakingRelayReceive();
