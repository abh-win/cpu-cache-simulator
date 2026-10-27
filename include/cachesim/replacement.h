#pragma once

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "cachesim/config.h"

namespace cachesim {

// Per-set replacement state. The cache always fills an invalid way first and
// asks the policy for a victim only when every way in the set is valid.
class ReplacementPolicy {
 public:
  virtual ~ReplacementPolicy() = default;
  virtual void onHit(uint32_t way) = 0;
  virtual void onFill(uint32_t way) = 0;
  virtual void onInvalidate(uint32_t way) { (void)way; }
  virtual uint32_t victim() = 0;
  virtual const char* name() const = 0;
};

// True least-recently-used via per-way timestamps.
class LRUPolicy final : public ReplacementPolicy {
 public:
  explicit LRUPolicy(uint32_t ways) : stamp_(ways, 0) {}
  void onHit(uint32_t way) override { stamp_[way] = ++clock_; }
  void onFill(uint32_t way) override { stamp_[way] = ++clock_; }
  void onInvalidate(uint32_t way) override { stamp_[way] = 0; }
  uint32_t victim() override;
  const char* name() const override { return "LRU"; }

 private:
  std::vector<uint64_t> stamp_;
  uint64_t clock_ = 0;
};

// First-in first-out: hits do not refresh a line.
class FIFOPolicy final : public ReplacementPolicy {
 public:
  explicit FIFOPolicy(uint32_t ways) : stamp_(ways, 0) {}
  void onHit(uint32_t) override {}
  void onFill(uint32_t way) override { stamp_[way] = ++clock_; }
  void onInvalidate(uint32_t way) override { stamp_[way] = 0; }
  uint32_t victim() override;
  const char* name() const override { return "FIFO"; }

 private:
  std::vector<uint64_t> stamp_;
  uint64_t clock_ = 0;
};

// Binary-tree pseudo-LRU: ways-1 bits; each internal node points toward the
// less recently used half. An access flips the nodes on its path away from
// itself; the victim is found by following the pointers from the root.
class TreePLRUPolicy final : public ReplacementPolicy {
 public:
  explicit TreePLRUPolicy(uint32_t ways);  // ways must be a power of two
  void onHit(uint32_t way) override { touch(way); }
  void onFill(uint32_t way) override { touch(way); }
  uint32_t victim() override;
  const char* name() const override { return "PLRU"; }

 private:
  void touch(uint32_t way);
  uint32_t ways_;
  std::vector<uint8_t> bits_;  // heap-ordered: node n has children 2n+1, 2n+2
};

class RandomPolicy final : public ReplacementPolicy {
 public:
  RandomPolicy(uint32_t ways, uint64_t seed) : ways_(ways), rng_(seed) {}
  void onHit(uint32_t) override {}
  void onFill(uint32_t) override {}
  uint32_t victim() override { return static_cast<uint32_t>(rng_() % ways_); }
  const char* name() const override { return "Random"; }

 private:
  uint32_t ways_;
  std::mt19937_64 rng_;
};

// Builds one policy object sized to the set's associativity.
std::unique_ptr<ReplacementPolicy> makePolicy(PolicyKind kind, uint32_t ways, uint64_t seed);

// Builds the per-set policy array for a whole cache geometry.
std::vector<std::unique_ptr<ReplacementPolicy>> makePolicies(const CacheConfig& cfg);

}  // namespace cachesim
