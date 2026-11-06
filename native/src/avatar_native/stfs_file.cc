#include "stfs_file.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>

namespace xe::kernel::xna::avatar {
namespace {

constexpr uint32_t kBlockSize = 0x1000;
constexpr uint32_t kBlocksPerHashLevel[3] = {170, 28900, 4913000};
constexpr uint32_t kEndOfChain = 0xFFFFFF;
constexpr uint32_t kHashEntryBytes = 0x18;
constexpr uint32_t kDirectoryEntryBytes = 0x40;
constexpr uint64_t kMaxFileBytes = 256ull << 20;
// Header fields (file offsets).
constexpr size_t kHeaderSizeOffset = 0x340;
constexpr size_t kDescriptorOffset = 0x379;
constexpr size_t kVolumeTypeOffset = 0x3A9;
constexpr size_t kHeaderBytes = 0x3AD;

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t Le24(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16; }

class Package {
 public:
  bool Open(const std::filesystem::path& path) {
    file_.open(path, std::ios::binary);
    std::array<uint8_t, kHeaderBytes> header{};
    if (!Read(0, header.data(), header.size())) return false;
    if (Be32(&header[kVolumeTypeOffset]) != 0) return false;  // SVOD
    const uint8_t* descriptor = &header[kDescriptorOffset];
    if (descriptor[0] != 0x24) return false;
    const uint8_t flags = descriptor[2];
    read_only_ = flags & 1;
    root_active_ = flags & 2;
    table_blocks_ = uint16_t(descriptor[3] | descriptor[4] << 8);
    table_block_ = Le24(descriptor + 5);
    total_blocks_ = Be32(descriptor + 0x1C);
    data_start_ = (uint64_t(Be32(&header[kHeaderSizeOffset])) + kBlockSize - 1) & ~uint64_t(kBlockSize - 1);
    tables_per_block_ = read_only_ ? 1 : 2;
    step_[0] = kBlocksPerHashLevel[0] + tables_per_block_;
    step_[1] = kBlocksPerHashLevel[1] + (kBlocksPerHashLevel[0] + 1) * tables_per_block_;
    return true;
  }

  bool ReadRootFile(std::string_view name, std::vector<uint8_t>* out) {
    uint32_t block = table_block_;
    for (uint32_t n = 0; n < table_blocks_ && block != kEndOfChain; ++n) {
      std::array<uint8_t, kBlockSize> directory{};
      if (!Read(BlockOffset(block), directory.data(), directory.size())) return false;
      for (uint32_t m = 0; m < kBlockSize / kDirectoryEntryBytes; ++m) {
        const uint8_t* entry = &directory[m * kDirectoryEntryBytes];
        if (!entry[0]) break;
        const uint8_t entry_flags = entry[0x28];
        const std::string_view entry_name(reinterpret_cast<const char*>(entry), entry_flags & 0x3F);
        const bool directory_entry = entry_flags & 0x80;
        if (directory_entry || Be16(entry + 0x32) != 0xFFFF || entry_name != name) continue;
        return ReadChain(Le24(entry + 0x2F), Be32(entry + 0x34), out);
      }
      const auto next = NextBlock(block);
      if (!next) return false;
      block = *next;
    }
    return false;
  }

 private:
  bool Read(uint64_t offset, uint8_t* data, size_t size) {
    file_.clear();
    file_.seekg(std::streamoff(offset));
    return file_.read(reinterpret_cast<char*>(data), std::streamsize(size)).good();
  }

  // Data blocks interleave with the hash tables that cover them.
  uint64_t BlockOffset(uint64_t index) const {
    uint64_t base = kBlocksPerHashLevel[0];
    uint64_t block = index;
    for (int level = 0; level < 3; ++level) {
      block += ((index + base) / base) * tables_per_block_;
      if (index < base) break;
      base *= kBlocksPerHashLevel[0];
    }
    return data_start_ + (block << 12);
  }

  uint64_t HashTableOffset(uint32_t index, int level) const {
    uint64_t block = 0;
    if (level == 0) {
      if (index >= kBlocksPerHashLevel[0]) {
        block = uint64_t(index / kBlocksPerHashLevel[0]) * step_[0];
        block += uint64_t(index / kBlocksPerHashLevel[1] + 1) * tables_per_block_;
        if (index >= kBlocksPerHashLevel[1]) block += tables_per_block_;
      }
    } else if (level == 1) {
      block = index < kBlocksPerHashLevel[1] ? step_[0]
                                             : uint64_t(index / kBlocksPerHashLevel[1]) * step_[1] + tables_per_block_;
    } else {
      block = step_[1];
    }
    return data_start_ + (block << 12);
  }

  std::optional<uint32_t> HashInfo(uint64_t table, uint32_t record) {
    uint8_t entry[kHashEntryBytes];
    if (!Read(table + uint64_t(record) * kHashEntryBytes, entry, sizeof(entry))) return std::nullopt;
    return Be32(entry + 0x14);
  }

  // A writable package keeps two copies of each hash table; the level above
  // says which one is current.
  std::optional<uint32_t> NextBlock(uint32_t index) {
    uint64_t secondary = (!read_only_ && root_active_) ? kBlockSize : 0;
    if (!read_only_ && total_blocks_ > kBlocksPerHashLevel[0]) {
      if (total_blocks_ > kBlocksPerHashLevel[1]) {
        const auto info = HashInfo(HashTableOffset(index, 2) + secondary,
                                   (index / kBlocksPerHashLevel[1]) % kBlocksPerHashLevel[0]);
        if (!info) return std::nullopt;
        secondary = (*info & 0x40000000) ? kBlockSize : 0;
      }
      const auto info = HashInfo(HashTableOffset(index, 1) + secondary,
                                 (index / kBlocksPerHashLevel[0]) % kBlocksPerHashLevel[0]);
      if (!info) return std::nullopt;
      secondary = (*info & 0x40000000) ? kBlockSize : 0;
    }
    const auto info = HashInfo(HashTableOffset(index, 0) + secondary, index % kBlocksPerHashLevel[0]);
    if (!info) return std::nullopt;
    return *info & 0xFFFFFF;
  }

  bool ReadChain(uint32_t block, uint32_t length, std::vector<uint8_t>* out) {
    if (!length || length > kMaxFileBytes) return false;
    std::vector<uint8_t> bytes(length);
    for (size_t done = 0; done < length;) {
      if (block == kEndOfChain) return false;
      const size_t size = std::min<size_t>(kBlockSize, length - done);
      if (!Read(BlockOffset(block), bytes.data() + done, size)) return false;
      done += size;
      if (done < length) {
        const auto next = NextBlock(block);
        if (!next) return false;
        block = *next;
      }
    }
    *out = std::move(bytes);
    return true;
  }

  std::ifstream file_;
  bool read_only_ = false;
  bool root_active_ = false;
  uint32_t table_blocks_ = 0;
  uint32_t table_block_ = 0;
  uint32_t total_blocks_ = 0;
  uint64_t data_start_ = 0;
  uint32_t tables_per_block_ = 1;
  uint64_t step_[2] = {};
};

}  // namespace

bool ReadStfsRootFile(const std::filesystem::path& package, std::string_view name, std::vector<uint8_t>* out) {
  Package reader;
  return reader.Open(package) && reader.ReadRootFile(name, out);
}

}  // namespace xe::kernel::xna::avatar
