#pragma once

// 3C miss classification (Hill & Smith):
//   compulsory  first reference to the block ever
//   capacity    would also miss in a fully-associative LRU cache of the same size
//   conflict    misses here but hits in that fully-associative shadow cache
//
// The shadow cache sees exactly the same demand stream as the real level.

#include <cstdint>
#include <list>
#include <unordered_map>
#include <unordered_set>

namespace cachesim {

enum class MissKind { Compulsory, Capacity, Conflict };

class ShadowFullyAssociative {
 public:
  explicit ShadowFullyAssociative(uint64_t capacity_blocks) : capacity_(capacity_blocks) {}
  // Accesses the block (LRU update / fill). Returns true on hit.
  bool access(uint64_t block);
  uint64_t size() const { return map_.size(); }

 private:
  uint64_t capacity_;
  std::list<uint64_t> lru_;  // front = most recent
  std::unordered_map<uint64_t, std::list<uint64_t>::iterator> map_;
};

class MissClassifier {
 public:
  explicit MissClassifier(uint64_t capacity_blocks) : shadow_(capacity_blocks) {}

  // Must be called for every demand access to the level, hit or miss.
  // Returns the class of the miss when `real_hit` is false.
  MissKind observe(uint64_t block, bool real_hit);

 private:
  std::unordered_set<uint64_t> seen_;
  ShadowFullyAssociative shadow_;
};

}  // namespace cachesim
