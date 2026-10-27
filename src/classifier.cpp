#include "cachesim/classifier.h"

namespace cachesim {

bool ShadowFullyAssociative::access(uint64_t block) {
  auto it = map_.find(block);
  if (it != map_.end()) {
    lru_.splice(lru_.begin(), lru_, it->second);
    return true;
  }
  if (map_.size() == capacity_) {
    map_.erase(lru_.back());
    lru_.pop_back();
  }
  lru_.push_front(block);
  map_[block] = lru_.begin();
  return false;
}

MissKind MissClassifier::observe(uint64_t block, bool real_hit) {
  bool first = seen_.insert(block).second;
  bool shadow_hit = shadow_.access(block);
  if (real_hit) return MissKind::Conflict;  // unused by callers on hits
  if (first) return MissKind::Compulsory;
  return shadow_hit ? MissKind::Conflict : MissKind::Capacity;
}

}  // namespace cachesim
