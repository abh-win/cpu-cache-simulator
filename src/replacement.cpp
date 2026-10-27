#include "cachesim/replacement.h"

#include <algorithm>
#include <stdexcept>

namespace cachesim {

uint32_t LRUPolicy::victim() {
  return static_cast<uint32_t>(std::min_element(stamp_.begin(), stamp_.end()) - stamp_.begin());
}

uint32_t FIFOPolicy::victim() {
  return static_cast<uint32_t>(std::min_element(stamp_.begin(), stamp_.end()) - stamp_.begin());
}

TreePLRUPolicy::TreePLRUPolicy(uint32_t ways) : ways_(ways), bits_(ways > 1 ? ways - 1 : 1, 0) {
  if (ways == 0 || (ways & (ways - 1)) != 0)
    throw std::invalid_argument("tree-PLRU requires power-of-two associativity");
}

// Bit semantics: 0 = victim lies in the left subtree, 1 = right subtree.
void TreePLRUPolicy::touch(uint32_t way) {
  if (ways_ == 1) return;
  uint32_t node = 0, lo = 0, hi = ways_;
  while (hi - lo > 1) {
    uint32_t mid = (lo + hi) / 2;
    if (way < mid) {
      bits_[node] = 1;  // accessed left -> point victim search right
      node = 2 * node + 1;
      hi = mid;
    } else {
      bits_[node] = 0;
      node = 2 * node + 2;
      lo = mid;
    }
  }
}

uint32_t TreePLRUPolicy::victim() {
  if (ways_ == 1) return 0;
  uint32_t node = 0, lo = 0, hi = ways_;
  while (hi - lo > 1) {
    uint32_t mid = (lo + hi) / 2;
    if (bits_[node] == 0) {
      node = 2 * node + 1;
      hi = mid;
    } else {
      node = 2 * node + 2;
      lo = mid;
    }
  }
  return lo;
}

std::unique_ptr<ReplacementPolicy> makePolicy(PolicyKind kind, uint32_t ways, uint64_t seed) {
  switch (kind) {
    case PolicyKind::LRU: return std::make_unique<LRUPolicy>(ways);
    case PolicyKind::FIFO: return std::make_unique<FIFOPolicy>(ways);
    case PolicyKind::TreePLRU: return std::make_unique<TreePLRUPolicy>(ways);
    case PolicyKind::Random: return std::make_unique<RandomPolicy>(ways, seed);
  }
  throw std::invalid_argument("unknown policy");
}

std::vector<std::unique_ptr<ReplacementPolicy>> makePolicies(const CacheConfig& cfg) {
  std::vector<std::unique_ptr<ReplacementPolicy>> out;
  uint64_t sets = cfg.numSets();
  out.reserve(sets);
  for (uint64_t s = 0; s < sets; ++s)
    out.push_back(makePolicy(cfg.policy, cfg.ways(), cfg.seed * 0x9E3779B97F4A7C15ull + s));
  return out;
}

}  // namespace cachesim
