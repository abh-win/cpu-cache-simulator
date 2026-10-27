#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cachesim/cache.h"
#include "cachesim/classifier.h"
#include "cachesim/config.h"

namespace cachesim {

struct LevelStats {
  uint64_t reads = 0, writes = 0;            // demand accesses arriving at this level
  uint64_t read_misses = 0, write_misses = 0;
  uint64_t compulsory = 0, capacity = 0, conflict = 0;
  uint64_t evictions = 0;
  uint64_t writebacks = 0;          // dirty lines sent to the next level / memory
  uint64_t writes_forwarded = 0;    // write-through or no-write-allocate traffic sent down
  uint64_t back_invalidations = 0;  // inclusive: upper-level copies killed by our evictions
  uint64_t victim_fills = 0;        // exclusive: lines received as victims from above

  uint64_t accesses() const { return reads + writes; }
  uint64_t misses() const { return read_misses + write_misses; }
  uint64_t hits() const { return accesses() - misses(); }
  double missRate() const { return accesses() ? double(misses()) / double(accesses()) : 0.0; }
};

struct HierarchyStats {
  std::vector<LevelStats> levels;
  uint64_t cpu_accesses = 0;  // trace references (an access spanning two blocks counts once)
  uint64_t cpu_reads = 0, cpu_writes = 0;
  uint64_t block_accesses = 0;
  uint64_t total_cycles = 0;
  uint64_t memory_reads = 0, memory_writes = 0;

  // Mean cycles per trace reference, as simulated.
  double measuredAMAT() const { return cpu_accesses ? double(total_cycles) / double(cpu_accesses) : 0; }
};

// Multi-level cache hierarchy (1..N levels in front of main memory).
class Hierarchy {
 public:
  explicit Hierarchy(const HierarchyConfig& cfg);

  // Simulates one CPU reference of `size` bytes; returns its latency in cycles.
  uint64_t access(uint64_t addr, uint32_t size, bool is_write);

  const HierarchyStats& stats() const { return stats_; }
  const HierarchyConfig& config() const { return cfg_; }
  const Cache& level(size_t i) const { return *caches_[i]; }
  size_t numLevels() const { return caches_.size(); }

  // Hierarchical AMAT from local miss rates:
  //   AMAT = t1 + m1 * (t2 + m2 * (... + mN * t_mem))
  double analyticAMAT() const;

  // Human-readable report.
  std::string report() const;

 private:
  uint64_t demand(size_t i, uint64_t block, bool write);
  uint64_t fetch(size_t j, uint64_t block, bool* dirty_out);
  uint64_t forwardWrite(size_t j, uint64_t block);
  uint64_t writeback(size_t j, uint64_t block);
  uint64_t install(size_t i, uint64_t block, bool dirty);
  uint64_t handleVictim(size_t i, Cache::Victim v);
  uint64_t offPath(uint64_t cycles) const { return cfg_.write_buffer ? 0 : cycles; }

  HierarchyConfig cfg_;
  uint32_t block_shift_;
  std::vector<std::unique_ptr<Cache>> caches_;
  std::vector<std::unique_ptr<MissClassifier>> classifiers_;
  HierarchyStats stats_;
};

}  // namespace cachesim
