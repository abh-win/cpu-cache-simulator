#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cachesim {

enum class PolicyKind { LRU, FIFO, TreePLRU, Random };

// Write-hit behaviour.
enum class WriteHit { WriteBack, WriteThrough };
// Write-miss behaviour.
enum class WriteMiss { WriteAllocate, NoWriteAllocate };

// Relationship between adjacent levels.
//   Inclusive: every block in level i is also in level i+1; an eviction from
//              i+1 back-invalidates the block in all upper levels.
//   Exclusive: a block lives in at most one level; misses fill only L1, and
//              blocks reach lower levels only as victims evicted from above.
//   NINE:      non-inclusive non-exclusive; misses fill every level on the
//              way up, evictions never back-invalidate.
enum class Inclusion { Inclusive, Exclusive, NINE };

struct CacheConfig {
  std::string name = "L1";
  uint64_t size_bytes = 32 * 1024;
  uint32_t block_bytes = 64;
  uint32_t assoc = 8;  // 0 = fully associative
  PolicyKind policy = PolicyKind::LRU;
  WriteHit write_hit = WriteHit::WriteBack;
  WriteMiss write_miss = WriteMiss::WriteAllocate;
  uint32_t hit_latency = 1;  // cycles
  uint64_t seed = 1;         // for Random replacement

  uint64_t numBlocks() const { return size_bytes / block_bytes; }
  uint32_t ways() const { return assoc == 0 ? static_cast<uint32_t>(numBlocks()) : assoc; }
  uint64_t numSets() const { return numBlocks() / ways(); }

  // Throws std::invalid_argument describing the first problem found.
  void validate() const;
};

struct HierarchyConfig {
  std::vector<CacheConfig> levels;  // levels[0] is closest to the CPU
  Inclusion inclusion = Inclusion::NINE;
  uint32_t memory_latency = 100;  // cycles
  // When true, write-backs and write-through traffic are absorbed by a write
  // buffer and add no latency to the access; otherwise they stall the access.
  bool write_buffer = true;
  bool classify_misses = true;

  void validate() const;
};

std::string toString(PolicyKind p);
std::string toString(Inclusion i);
std::string writePolicyString(WriteHit h, WriteMiss m);

PolicyKind parsePolicy(const std::string& s);
Inclusion parseInclusion(const std::string& s);
// Accepts "wb-wa", "wb-nwa", "wt-wa", "wt-nwa".
void parseWritePolicy(const std::string& s, WriteHit* hit, WriteMiss* miss);
// Parses sizes such as "32K", "2M", "4096".
uint64_t parseSize(const std::string& s);

// Parses a compact level spec "size:block:assoc:policy[:latency]",
// e.g. "32K:64:8:lru:1" (assoc "full" = fully associative).
CacheConfig parseLevelSpec(const std::string& name, const std::string& spec);

}  // namespace cachesim
