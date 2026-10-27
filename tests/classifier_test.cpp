#include <gtest/gtest.h>

#include "cachesim/classifier.h"
#include "cachesim/hierarchy.h"

using namespace cachesim;

TEST(Shadow, LRUCapacity) {
  ShadowFullyAssociative s(2);
  EXPECT_FALSE(s.access(1));
  EXPECT_FALSE(s.access(2));
  EXPECT_TRUE(s.access(1));
  EXPECT_FALSE(s.access(3));  // evicts 2
  EXPECT_FALSE(s.access(2));
}

TEST(Classifier, ThreeCs) {
  MissClassifier m(2);
  EXPECT_EQ(m.observe(1, false), MissKind::Compulsory);
  EXPECT_EQ(m.observe(2, false), MissKind::Compulsory);
  EXPECT_EQ(m.observe(1, false), MissKind::Conflict);   // FA would have hit
  EXPECT_EQ(m.observe(3, false), MissKind::Compulsory);
  EXPECT_EQ(m.observe(2, false), MissKind::Capacity);   // FA of 2 evicted it
}

// Two blocks that collide in a direct-mapped cache but fit trivially: every
// miss after the first two is a conflict miss.
TEST(Classifier, PingPongIsAllConflict) {
  HierarchyConfig h;
  CacheConfig c;
  c.size_bytes = 1024;
  c.block_bytes = 64;
  c.assoc = 1;
  h.levels = {c};
  Hierarchy sim(h);
  for (int i = 0; i < 100; ++i) {
    sim.access(0, 8, false);
    sim.access(1024, 8, false);
  }
  const auto& s = sim.stats().levels[0];
  EXPECT_EQ(s.misses(), 200u);
  EXPECT_EQ(s.compulsory, 2u);
  EXPECT_EQ(s.conflict, 198u);
  EXPECT_EQ(s.capacity, 0u);
}

// Cyclic sweep over more blocks than the cache holds: LRU misses every time,
// and the misses are capacity misses even in a fully associative cache.
TEST(Classifier, CyclicOverflowIsCapacity) {
  HierarchyConfig h;
  CacheConfig c;
  c.size_bytes = 1024;  // 16 blocks
  c.block_bytes = 64;
  c.assoc = 0;
  h.levels = {c};
  Hierarchy sim(h);
  for (int pass = 0; pass < 10; ++pass)
    for (int b = 0; b < 17; ++b) sim.access(uint64_t(b) * 64, 8, false);
  const auto& s = sim.stats().levels[0];
  EXPECT_EQ(s.compulsory, 17u);
  EXPECT_EQ(s.capacity, 17u * 9);
  EXPECT_EQ(s.conflict, 0u);
}
