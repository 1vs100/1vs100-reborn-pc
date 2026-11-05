#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace onevs100::render {

struct alignas(64) FloatConstantBlock {
  std::array<uint32_t, 256 * 4> words;
  // Registers this block holds; the others are stale and no shader drawn with it reads them.
  std::array<uint64_t, 4> valid;
};

// Capture compares the registers the draw's shader reads before sharing a block; published blocks stay immutable
// until all readers retire.
class FloatConstantSnapshots {
 public:
  using Mask = std::array<uint64_t, 4>;

  // Calls op(first register, count) for each run of set bits; stops when op returns false. Shaders read contiguous
  // register ranges, so runs are few.
  template <typename Op>
  static bool ForRuns(const Mask& used, Op op) {
    for (uint32_t word = 0; word < 4; ++word) {
      uint64_t bits = used[word];
      while (bits) {
        const uint32_t start = uint32_t(std::countr_zero(bits));
        const uint32_t length = uint32_t(std::countr_one(bits >> start));
        if (!op(word * 64 + start, length)) return false;
        bits = length + start >= 64 ? 0 : bits & ~(((uint64_t(1) << length) - 1) << start);
      }
    }
    return true;
  }


  std::shared_ptr<const FloatConstantBlock> Capture(size_t stage, const void* source, const Mask& used) {
    const auto* words = static_cast<const uint32_t*>(source);
    auto& previous = previous_.at(stage);
    if (previous && Covers(previous->valid, used) && Equal(previous->words.data(), words, used)) return previous;
    auto block = Allocate();
    Copy(block->words.data(), words, used);
    block->valid = used;
    previous = std::move(block);
    return previous;
  }

 private:
  static bool Covers(const Mask& have, const Mask& need) {
    for (size_t i = 0; i < need.size(); ++i) {
      if (need[i] & ~have[i]) return false;
    }
    return true;
  }

  static bool Equal(const uint32_t* a, const uint32_t* b, const Mask& used) {
    return ForRuns(used, [&](uint32_t reg, uint32_t count) {
      return std::memcmp(a + reg * 4, b + reg * 4, count * 16) == 0;
    });
  }

  static void Copy(uint32_t* to, const uint32_t* from, const Mask& used) {
    ForRuns(used, [&](uint32_t reg, uint32_t count) {
      std::memcpy(to + reg * 4, from + reg * 4, count * 16);
      return true;
    });
  }

  static std::shared_ptr<FloatConstantBlock> Allocate() {
    struct Chunk { std::array<FloatConstantBlock, 16> items; };
    thread_local std::vector<std::shared_ptr<Chunk>> pool;
    thread_local std::shared_ptr<Chunk> chunk;
    thread_local size_t next = 16, cursor = 0;
    if (next == 16) {
      chunk.reset();
      for (size_t checked = 0; checked < pool.size(); ++checked) {
        cursor = (cursor + 1) % pool.size();
        if (pool[cursor].use_count() == 1) { chunk = pool[cursor]; break; }
      }
      if (!chunk) {
        chunk = std::make_shared<Chunk>();
        if (pool.size() < 128) pool.push_back(chunk);
      }
      next = 0;
    }
    return std::shared_ptr<FloatConstantBlock>(chunk, &chunk->items[next++]);
  }

  std::array<std::shared_ptr<const FloatConstantBlock>, 2> previous_;
};

}  // namespace onevs100::render
