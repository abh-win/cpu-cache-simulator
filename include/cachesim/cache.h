#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "cachesim/config.h"
#include "cachesim/replacement.h"

namespace cachesim {

// One set-associative cache level: tag store + replacement state only (no
// data). All addresses passed in are *block* addresses (byte address >>
// log2(block size)).
class Cache {
 public:
  explicit Cache(const CacheConfig& cfg);

  struct Victim {
    uint64_t block;
    bool dirty;
  };

  // Looks the block up; on a hit updates replacement state (and marks the
  // line dirty when `make_dirty`). Returns true on hit.
  bool access(uint64_t block, bool make_dirty);

  // True if present; no side effects.
  bool contains(uint64_t block) const;

  // Installs a block that is known to be absent. Returns the line it
  // displaced, if any.
  std::optional<Victim> fill(uint64_t block, bool dirty);

  // Removes the block if present; returns its dirty bit.
  std::optional<bool> invalidate(uint64_t block);

  // Marks a present block dirty; returns false if absent.
  bool markDirty(uint64_t block);

  const CacheConfig& config() const { return cfg_; }
  uint64_t numSets() const { return sets_; }
  uint32_t ways() const { return ways_; }
  uint64_t validLines() const;

 private:
  struct Line {
    uint64_t block = 0;  // full block address doubles as the tag
    bool valid = false;
    bool dirty = false;
  };

  uint64_t setOf(uint64_t block) const { return block & (sets_ - 1); }
  Line* find(uint64_t block, uint32_t* way);
  const Line* find(uint64_t block) const;

  CacheConfig cfg_;
  uint64_t sets_;
  uint32_t ways_;
  std::vector<Line> lines_;  // sets_ * ways_, set-major
  std::vector<std::unique_ptr<ReplacementPolicy>> policies_;
};

}  // namespace cachesim
