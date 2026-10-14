#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <limits>

// Host-side primitive for the original GameState SET/GET and result SUM store.
// Transport must supply the authenticated sender, never a packet's claimed ID.
// Question answers and scoring validation belong above this primitive.
class RoomAuthority {
 public:
  explicit RoomAuthority(uint64_t host) : host_(host) { members_.insert(host); }
  bool Invite(uint64_t sender, uint64_t member) {
    if (sender != host_ || !member) return false;
    members_.insert(member); return true;
  }
  bool Publish(uint64_t sender, uint64_t revision, const std::string& state) {
    if (sender != host_ || revision != revision_ + 1 || state.size() > 65536) return false;
    state_ = state; revision_ = revision; return true;
  }
  bool Read(uint64_t sender, uint64_t& revision, std::string& state) const {
    if (!members_.count(sender)) return false;
    revision = revision_; state = state_; return true;
  }
  bool Contribute(uint64_t sender, uint64_t message, const std::string& key, int64_t value) {
    if (!members_.count(sender) || !message || key.empty() || key.size() > 256 || sums_.size() > 4096) return false;
    auto id = std::make_pair(sender, message);
    auto found = seen_.find(id);
    // Retry is accepted only if its original content is identical.
    if (found != seen_.end()) return found->second.first == key && found->second.second == value;
    if (seen_.size() >= 100000) return false;
    auto old = sums_[key];
    if ((value > 0 && old > std::numeric_limits<int64_t>::max() - value) ||
        (value < 0 && old < std::numeric_limits<int64_t>::min() - value)) return false;
    sums_[key] = old + value; seen_[id] = {key, value}; return true;
  }
  int64_t Sum(const std::string& key) const {
    auto found = sums_.find(key); return found == sums_.end() ? 0 : found->second;
  }
 private:
  uint64_t host_, revision_ = 0;
  std::string state_;
  std::set<uint64_t> members_;
  std::map<std::string, int64_t> sums_;
  std::map<std::pair<uint64_t,uint64_t>, std::pair<std::string,int64_t>> seen_;
};
