#include "cachesim/cache.h"

namespace cachesim {

namespace {
const CacheConfig& validated(const CacheConfig& cfg) {
  cfg.validate();
  return cfg;
}
}  // namespace

Cache::Cache(const CacheConfig& cfg)
    : cfg_(validated(cfg)),
      sets_(cfg_.numSets()),
      ways_(cfg_.ways()),
      lines_(sets_ * ways_),
      policies_(makePolicies(cfg_)) {}

Cache::Line* Cache::find(uint64_t block, uint32_t* way) {
  Line* base = &lines_[setOf(block) * ways_];
  for (uint32_t w = 0; w < ways_; ++w) {
    if (base[w].valid && base[w].block == block) {
      *way = w;
      return &base[w];
    }
  }
  return nullptr;
}

const Cache::Line* Cache::find(uint64_t block) const {
  const Line* base = &lines_[setOf(block) * ways_];
  for (uint32_t w = 0; w < ways_; ++w)
    if (base[w].valid && base[w].block == block) return &base[w];
  return nullptr;
}

bool Cache::access(uint64_t block, bool make_dirty) {
  uint32_t way;
  Line* l = find(block, &way);
  if (l == nullptr) return false;
  policies_[setOf(block)]->onHit(way);
  if (make_dirty) l->dirty = true;
  return true;
}

bool Cache::contains(uint64_t block) const { return find(block) != nullptr; }

std::optional<Cache::Victim> Cache::fill(uint64_t block, bool dirty) {
  uint64_t set = setOf(block);
  Line* base = &lines_[set * ways_];
  ReplacementPolicy& policy = *policies_[set];

  uint32_t way = ways_;
  for (uint32_t w = 0; w < ways_; ++w) {
    if (!base[w].valid) {
      way = w;
      break;
    }
  }
  std::optional<Victim> victim;
  if (way == ways_) {
    way = policy.victim();
    victim = Victim{base[way].block, base[way].dirty};
  }
  base[way] = Line{block, true, dirty};
  policy.onFill(way);
  return victim;
}

std::optional<bool> Cache::invalidate(uint64_t block) {
  uint32_t way;
  Line* l = find(block, &way);
  if (l == nullptr) return std::nullopt;
  bool dirty = l->dirty;
  *l = Line{};
  policies_[setOf(block)]->onInvalidate(way);
  return dirty;
}

bool Cache::markDirty(uint64_t block) {
  uint32_t way;
  Line* l = find(block, &way);
  if (l == nullptr) return false;
  l->dirty = true;
  return true;
}

uint64_t Cache::validLines() const {
  uint64_t n = 0;
  for (const auto& l : lines_) n += l.valid;
  return n;
}

}  // namespace cachesim
