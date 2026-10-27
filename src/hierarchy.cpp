#include "cachesim/hierarchy.h"

#include <iomanip>
#include <sstream>

namespace cachesim {

Hierarchy::Hierarchy(const HierarchyConfig& cfg) : cfg_(cfg) {
  cfg_.validate();
  block_shift_ = 0;
  while ((1u << block_shift_) < cfg_.levels[0].block_bytes) ++block_shift_;
  for (const auto& lc : cfg_.levels) {
    caches_.push_back(std::make_unique<Cache>(lc));
    classifiers_.push_back(cfg_.classify_misses ? std::make_unique<MissClassifier>(lc.numBlocks())
                                                : nullptr);
  }
  stats_.levels.resize(caches_.size());
}

uint64_t Hierarchy::access(uint64_t addr, uint32_t size, bool is_write) {
  if (size == 0) size = 1;
  stats_.cpu_accesses++;
  (is_write ? stats_.cpu_writes : stats_.cpu_reads)++;
  uint64_t first = addr >> block_shift_;
  uint64_t last = (addr + size - 1) >> block_shift_;
  uint64_t cycles = 0;
  for (uint64_t b = first; b <= last; ++b) {
    stats_.block_accesses++;
    cycles += demand(0, b, is_write);
  }
  stats_.total_cycles += cycles;
  return cycles;
}

// A demand access (CPU reference, fetch for an upper level, or a forwarded
// write) arriving at level i.
uint64_t Hierarchy::demand(size_t i, uint64_t block, bool write) {
  const CacheConfig& c = cfg_.levels[i];
  LevelStats& st = stats_.levels[i];
  const bool write_back = c.write_hit == WriteHit::WriteBack;
  uint64_t cycles = c.hit_latency;

  bool hit = caches_[i]->access(block, write && write_back);
  (write ? st.writes : st.reads)++;
  if (classifiers_[i]) {
    MissKind k = classifiers_[i]->observe(block, hit);
    if (!hit) {
      if (k == MissKind::Compulsory) st.compulsory++;
      else if (k == MissKind::Capacity) st.capacity++;
      else st.conflict++;
    }
  }

  if (hit) {
    if (write && !write_back) cycles += forwardWrite(i + 1, block);
    return cycles;
  }
  (write ? st.write_misses : st.read_misses)++;

  if (write && c.write_miss == WriteMiss::NoWriteAllocate) {
    cycles += forwardWrite(i + 1, block);
    return cycles;
  }

  bool dirty_in = false;
  cycles += fetch(i + 1, block, &dirty_in);
  cycles += install(i, block, dirty_in || (write && write_back));
  if (write && !write_back) cycles += forwardWrite(i + 1, block);
  return cycles;
}

// Obtains `block` for level j-1 from level j (j == N means main memory).
uint64_t Hierarchy::fetch(size_t j, uint64_t block, bool* dirty_out) {
  *dirty_out = false;
  if (j == caches_.size()) {
    stats_.memory_reads++;
    return cfg_.memory_latency;
  }
  if (cfg_.inclusion != Inclusion::Exclusive) return demand(j, block, false);

  // Exclusive: a hit moves the line up (and out of this level); a miss goes
  // further down without allocating here.
  LevelStats& st = stats_.levels[j];
  uint64_t cycles = cfg_.levels[j].hit_latency;
  bool hit = caches_[j]->contains(block);
  st.reads++;
  if (classifiers_[j]) {
    MissKind k = classifiers_[j]->observe(block, hit);
    if (!hit) {
      if (k == MissKind::Compulsory) st.compulsory++;
      else if (k == MissKind::Capacity) st.capacity++;
      else st.conflict++;
    }
  }
  if (hit) {
    *dirty_out = *caches_[j]->invalidate(block);
    return cycles;
  }
  st.read_misses++;
  return cycles + fetch(j + 1, block, dirty_out);
}

// Write-through / no-write-allocate traffic from level j-1 to level j.
uint64_t Hierarchy::forwardWrite(size_t j, uint64_t block) {
  stats_.levels[j - 1].writes_forwarded++;
  if (j == caches_.size()) {
    stats_.memory_writes++;
    return offPath(cfg_.memory_latency);
  }
  return offPath(demand(j, block, true));
}

// Dirty data evicted from level j-1 is written to level j. Write-backs do not
// update replacement state in the receiving level.
uint64_t Hierarchy::writeback(size_t j, uint64_t block) {
  stats_.levels[j - 1].writebacks++;
  if (j == caches_.size()) {
    stats_.memory_writes++;
    return offPath(cfg_.memory_latency);
  }
  const CacheConfig& c = cfg_.levels[j];
  if (caches_[j]->contains(block)) {
    if (c.write_hit == WriteHit::WriteBack) {
      caches_[j]->markDirty(block);
      return 0;
    }
    return writeback(j + 1, block);  // write-through: pass it on
  }
  if (c.write_miss == WriteMiss::WriteAllocate)
    return offPath(install(j, block, c.write_hit == WriteHit::WriteBack)) +
           (c.write_hit == WriteHit::WriteThrough ? writeback(j + 1, block) : 0);
  return writeback(j + 1, block);
}

uint64_t Hierarchy::install(size_t i, uint64_t block, bool dirty) {
  auto victim = caches_[i]->fill(block, dirty);
  if (!victim) return 0;
  stats_.levels[i].evictions++;
  return handleVictim(i, *victim);
}

uint64_t Hierarchy::handleVictim(size_t i, Cache::Victim v) {
  switch (cfg_.inclusion) {
    case Inclusion::Inclusive:
      // Enforce inclusion: upper levels may not keep a block we no longer hold.
      for (size_t k = 0; k < i; ++k) {
        if (auto d = caches_[k]->invalidate(v.block)) {
          stats_.levels[i].back_invalidations++;
          v.dirty = v.dirty || *d;
        }
      }
      return v.dirty ? writeback(i + 1, v.block) : 0;
    case Inclusion::NINE:
      return v.dirty ? writeback(i + 1, v.block) : 0;
    case Inclusion::Exclusive:
      if (i + 1 < caches_.size()) {
        stats_.levels[i + 1].victim_fills++;
        return offPath(install(i + 1, v.block, v.dirty));
      }
      if (v.dirty) {
        stats_.levels[i].writebacks++;
        stats_.memory_writes++;
        return offPath(cfg_.memory_latency);
      }
      return 0;
  }
  return 0;
}

double Hierarchy::analyticAMAT() const {
  double t = cfg_.memory_latency;
  for (size_t i = caches_.size(); i-- > 0;)
    t = cfg_.levels[i].hit_latency + stats_.levels[i].missRate() * t;
  return t;
}

std::string Hierarchy::report() const {
  std::ostringstream o;
  o << std::fixed << std::setprecision(4);
  o << "Hierarchy: " << caches_.size() << " level(s), " << toString(cfg_.inclusion)
    << ", memory " << cfg_.memory_latency << " cycles"
    << (cfg_.write_buffer ? ", write buffer" : ", writes stall") << "\n";
  for (size_t i = 0; i < caches_.size(); ++i) {
    const CacheConfig& c = cfg_.levels[i];
    const LevelStats& s = stats_.levels[i];
    o << "  " << c.name << ": " << c.size_bytes / 1024 << " KiB, " << c.block_bytes << " B blocks, "
      << (c.assoc == 0 ? std::string("fully-assoc") : std::to_string(c.assoc) + "-way") << ", "
      << toString(c.policy) << ", " << writePolicyString(c.write_hit, c.write_miss) << ", "
      << c.hit_latency << " cyc\n";
    o << "      accesses " << s.accesses() << " (R " << s.reads << " / W " << s.writes << ")"
      << "  misses " << s.misses() << " (R " << s.read_misses << " / W " << s.write_misses << ")"
      << "  local miss rate " << s.missRate() << "\n";
    if (cfg_.classify_misses)
      o << "      3C: compulsory " << s.compulsory << "  capacity " << s.capacity << "  conflict "
        << s.conflict << "\n";
    o << "      evictions " << s.evictions << "  writebacks " << s.writebacks << "  forwarded writes "
      << s.writes_forwarded;
    if (cfg_.inclusion == Inclusion::Inclusive) o << "  back-invalidations " << s.back_invalidations;
    if (cfg_.inclusion == Inclusion::Exclusive) o << "  victim fills " << s.victim_fills;
    o << "\n";
  }
  o << "  memory: reads " << stats_.memory_reads << "  writes " << stats_.memory_writes << "\n";
  o << std::setprecision(3);
  o << "  references " << stats_.cpu_accesses << " (R " << stats_.cpu_reads << " / W "
    << stats_.cpu_writes << ")\n";
  o << "  AMAT measured " << stats_.measuredAMAT() << " cycles, analytic " << analyticAMAT()
    << " cycles\n";
  return o.str();
}

}  // namespace cachesim
