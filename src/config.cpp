#include "cachesim/config.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace cachesim {
namespace {

bool isPow2(uint64_t x) { return x != 0 && (x & (x - 1)) == 0; }

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, sep)) out.push_back(item);
  return out;
}

}  // namespace

void CacheConfig::validate() const {
  auto fail = [&](const std::string& why) { throw std::invalid_argument(name + ": " + why); };
  if (!isPow2(block_bytes)) fail("block size must be a power of two");
  if (size_bytes == 0 || size_bytes % block_bytes != 0) fail("size must be a multiple of block size");
  if (numBlocks() % ways() != 0) fail("number of blocks must be divisible by associativity");
  if (!isPow2(numSets())) fail("number of sets must be a power of two");
  if (policy == PolicyKind::TreePLRU && !isPow2(ways()))
    fail("tree-PLRU requires a power-of-two associativity");
  if (hit_latency == 0) fail("hit latency must be >= 1");
}

void HierarchyConfig::validate() const {
  if (levels.empty()) throw std::invalid_argument("hierarchy needs at least one level");
  for (const auto& l : levels) l.validate();
  for (size_t i = 1; i < levels.size(); ++i) {
    if (levels[i].block_bytes != levels[0].block_bytes)
      throw std::invalid_argument("all levels must share one block size");
  }
  if (inclusion == Inclusion::Exclusive) {
    for (const auto& l : levels)
      if (l.write_hit != WriteHit::WriteBack || l.write_miss != WriteMiss::WriteAllocate)
        throw std::invalid_argument("exclusive hierarchies require write-back/write-allocate levels");
  }
}

std::string toString(PolicyKind p) {
  switch (p) {
    case PolicyKind::LRU: return "LRU";
    case PolicyKind::FIFO: return "FIFO";
    case PolicyKind::TreePLRU: return "PLRU";
    case PolicyKind::Random: return "Random";
  }
  return "?";
}

std::string toString(Inclusion i) {
  switch (i) {
    case Inclusion::Inclusive: return "inclusive";
    case Inclusion::Exclusive: return "exclusive";
    case Inclusion::NINE: return "nine";
  }
  return "?";
}

std::string writePolicyString(WriteHit h, WriteMiss m) {
  return std::string(h == WriteHit::WriteBack ? "WB" : "WT") + "-" +
         (m == WriteMiss::WriteAllocate ? "WA" : "NWA");
}

PolicyKind parsePolicy(const std::string& s) {
  std::string l = lower(s);
  if (l == "lru") return PolicyKind::LRU;
  if (l == "fifo") return PolicyKind::FIFO;
  if (l == "plru" || l == "tree-plru" || l == "treeplru") return PolicyKind::TreePLRU;
  if (l == "random" || l == "rand") return PolicyKind::Random;
  throw std::invalid_argument("unknown replacement policy: " + s);
}

Inclusion parseInclusion(const std::string& s) {
  std::string l = lower(s);
  if (l == "inclusive" || l == "incl") return Inclusion::Inclusive;
  if (l == "exclusive" || l == "excl") return Inclusion::Exclusive;
  if (l == "nine" || l == "non-inclusive") return Inclusion::NINE;
  throw std::invalid_argument("unknown inclusion policy: " + s);
}

void parseWritePolicy(const std::string& s, WriteHit* hit, WriteMiss* miss) {
  std::string l = lower(s);
  auto parts = split(l, '-');
  if (parts.size() != 2) throw std::invalid_argument("write policy must look like wb-wa: " + s);
  if (parts[0] == "wb") *hit = WriteHit::WriteBack;
  else if (parts[0] == "wt") *hit = WriteHit::WriteThrough;
  else throw std::invalid_argument("write-hit policy must be wb or wt: " + s);
  if (parts[1] == "wa") *miss = WriteMiss::WriteAllocate;
  else if (parts[1] == "nwa") *miss = WriteMiss::NoWriteAllocate;
  else throw std::invalid_argument("write-miss policy must be wa or nwa: " + s);
}

uint64_t parseSize(const std::string& s) {
  if (s.empty()) throw std::invalid_argument("empty size");
  size_t pos = 0;
  uint64_t v = std::stoull(s, &pos);
  std::string suffix = lower(s.substr(pos));
  if (suffix.empty() || suffix == "b") return v;
  if (suffix == "k" || suffix == "kb") return v << 10;
  if (suffix == "m" || suffix == "mb") return v << 20;
  if (suffix == "g" || suffix == "gb") return v << 30;
  throw std::invalid_argument("bad size suffix: " + s);
}

CacheConfig parseLevelSpec(const std::string& name, const std::string& spec) {
  auto parts = split(spec, ':');
  if (parts.size() < 4 || parts.size() > 5)
    throw std::invalid_argument("level spec must be size:block:assoc:policy[:latency]: " + spec);
  CacheConfig c;
  c.name = name;
  c.size_bytes = parseSize(parts[0]);
  c.block_bytes = static_cast<uint32_t>(parseSize(parts[1]));
  c.assoc = lower(parts[2]) == "full" ? 0 : static_cast<uint32_t>(std::stoul(parts[2]));
  c.policy = parsePolicy(parts[3]);
  if (parts.size() == 5) c.hit_latency = static_cast<uint32_t>(std::stoul(parts[4]));
  return c;
}

}  // namespace cachesim
