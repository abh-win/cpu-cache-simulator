#include <gtest/gtest.h>

#include <random>
#include <set>

#include "cachesim/hierarchy.h"

using namespace cachesim;

namespace {

CacheConfig level(const char* name, uint64_t size, uint32_t assoc, uint32_t lat,
                  WriteHit wh = WriteHit::WriteBack, WriteMiss wm = WriteMiss::WriteAllocate) {
  CacheConfig c;
  c.name = name;
  c.size_bytes = size;
  c.block_bytes = 64;
  c.assoc = assoc;
  c.hit_latency = lat;
  c.write_hit = wh;
  c.write_miss = wm;
  return c;
}

HierarchyConfig twoLevel(Inclusion inc) {
  HierarchyConfig h;
  h.levels = {level("L1", 1024, 2, 1), level("L2", 4096, 4, 10)};
  h.inclusion = inc;
  h.memory_latency = 100;
  return h;
}

}  // namespace

TEST(Hierarchy, AMATForAllMissesAndAllHits) {
  HierarchyConfig h;
  h.levels = {level("L1", 1024, 1, 1)};
  Hierarchy sim(h);
  // Ping-pong between two conflicting blocks: every access misses.
  for (int i = 0; i < 100; ++i) {
    sim.access(0, 8, false);
    sim.access(1024, 8, false);
  }
  EXPECT_DOUBLE_EQ(sim.stats().measuredAMAT(), 101.0);
  EXPECT_DOUBLE_EQ(sim.analyticAMAT(), 101.0);

  Hierarchy hot(h);
  for (int i = 0; i < 1000; ++i) hot.access(0, 8, false);
  EXPECT_NEAR(hot.stats().measuredAMAT(), 1.1, 1e-9);  // 1 miss + 999 hits
}

TEST(Hierarchy, TwoLevelLatencyComposition) {
  Hierarchy sim(twoLevel(Inclusion::NINE));
  EXPECT_EQ(sim.access(0, 8, false), 1u + 10u + 100u);  // cold: L1 miss, L2 miss, memory
  EXPECT_EQ(sim.access(0, 8, false), 1u);
  // Evict block 0 from the 2-way L1 set (8 sets -> blocks 0, 8, 16 collide).
  sim.access(8 * 64, 8, false);
  sim.access(16 * 64, 8, false);
  EXPECT_EQ(sim.access(0, 8, false), 1u + 10u);  // L1 miss, L2 hit
}

TEST(Hierarchy, AccessSpanningTwoBlocks) {
  Hierarchy sim(twoLevel(Inclusion::NINE));
  sim.access(60, 8, false);  // bytes 60..67 cross a 64-byte boundary
  EXPECT_EQ(sim.stats().block_accesses, 2u);
  EXPECT_EQ(sim.stats().cpu_accesses, 1u);
}

TEST(Hierarchy, WriteBackVsWriteThroughTraffic) {
  HierarchyConfig wb;
  wb.levels = {level("L1", 1024, 2, 1)};
  Hierarchy a(wb);
  for (int i = 0; i < 50; ++i) a.access(0, 8, true);
  EXPECT_EQ(a.stats().memory_reads, 1u);
  EXPECT_EQ(a.stats().memory_writes, 0u);  // still dirty in cache

  HierarchyConfig wt = wb;
  wt.levels[0].write_hit = WriteHit::WriteThrough;
  Hierarchy b(wt);
  for (int i = 0; i < 50; ++i) b.access(0, 8, true);
  EXPECT_EQ(b.stats().memory_reads, 1u);   // write-allocate fetch
  EXPECT_EQ(b.stats().memory_writes, 50u); // every store goes through
}

TEST(Hierarchy, DirtyEvictionWritesBack) {
  HierarchyConfig h;
  h.levels = {level("L1", 1024, 1, 1)};
  Hierarchy sim(h);
  sim.access(0, 8, true);
  sim.access(1024, 8, false);  // evicts dirty block 0
  EXPECT_EQ(sim.stats().levels[0].writebacks, 1u);
  EXPECT_EQ(sim.stats().memory_writes, 1u);
}

TEST(Hierarchy, NoWriteAllocateDoesNotFill) {
  HierarchyConfig h;
  h.levels = {level("L1", 1024, 2, 1, WriteHit::WriteBack, WriteMiss::NoWriteAllocate)};
  Hierarchy sim(h);
  sim.access(0, 8, true);
  EXPECT_FALSE(sim.level(0).contains(0));
  EXPECT_EQ(sim.stats().memory_writes, 1u);
  sim.access(0, 8, false);
  EXPECT_EQ(sim.stats().levels[0].read_misses, 1u);
  EXPECT_TRUE(sim.level(0).contains(0));
}

TEST(Hierarchy, WriteBufferControlsStoreLatency) {
  HierarchyConfig h;
  h.levels = {level("L1", 1024, 2, 1, WriteHit::WriteThrough, WriteMiss::NoWriteAllocate)};
  h.write_buffer = true;
  Hierarchy buffered(h);
  EXPECT_EQ(buffered.access(0, 8, true), 1u);
  h.write_buffer = false;
  Hierarchy stalled(h);
  EXPECT_EQ(stalled.access(0, 8, true), 101u);
}

class InclusionProperty : public ::testing::TestWithParam<Inclusion> {};

TEST_P(InclusionProperty, InvariantHoldsUnderRandomTraffic) {
  Inclusion inc = GetParam();
  // Fully associative L1 (16 blocks) over a 2-way L2 (32 blocks): L1-resident
  // blocks regularly become L2 victims, exercising back-invalidation.
  HierarchyConfig h;
  h.levels = {level("L1", 1024, 0, 1), level("L2", 2048, 2, 10)};
  h.inclusion = inc;
  Hierarchy sim(h);
  std::mt19937_64 rng(3);
  std::set<uint64_t> touched;
  for (int i = 0; i < 20000; ++i) {
    uint64_t block = rng() % 200;  // 200 blocks > L2 capacity (64)
    touched.insert(block);
    sim.access(block * 64, 8, rng() % 4 == 0);
    if (i % 97 != 0) continue;
    for (uint64_t b : touched) {
      bool in1 = sim.level(0).contains(b), in2 = sim.level(1).contains(b);
      if (inc == Inclusion::Inclusive) {
        ASSERT_TRUE(!in1 || in2) << "block " << b << " only in L1";
      }
      if (inc == Inclusion::Exclusive) {
        ASSERT_FALSE(in1 && in2) << "block " << b << " in both";
      }
    }
  }
  const auto& l2 = sim.stats().levels[1];
  if (inc == Inclusion::Inclusive) {
    EXPECT_GT(l2.back_invalidations, 0u);
  }
  if (inc == Inclusion::Exclusive) {
    EXPECT_GT(l2.victim_fills, 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(All, InclusionProperty,
                         ::testing::Values(Inclusion::Inclusive, Inclusion::Exclusive, Inclusion::NINE));

TEST(Hierarchy, ExclusiveHoldsMoreUniqueBlocks) {
  // Working set of 70 blocks: exceeds the 64-block L2 alone but fits in
  // L1 (16) + L2 (64) when levels are exclusive.
  auto run = [](Inclusion inc) {
    Hierarchy sim(twoLevel(inc));
    for (int pass = 0; pass < 20; ++pass)
      for (uint64_t b = 0; b < 70; ++b) sim.access(b * 64, 8, false);
    return sim.stats().memory_reads;
  };
  EXPECT_LT(run(Inclusion::Exclusive), run(Inclusion::Inclusive));
}

TEST(Hierarchy, InclusiveBackInvalidatesDirtyL1Copy) {
  // L1: 2 blocks fully associative. L2: direct mapped, 4 sets.
  HierarchyConfig h;
  h.levels = {level("L1", 128, 0, 1), level("L2", 256, 1, 10)};
  h.inclusion = Inclusion::Inclusive;
  Hierarchy sim(h);
  sim.access(0, 8, true);        // block 0 dirty in L1, clean in L2
  sim.access(4 * 64, 8, false);  // block 4 maps to L2 set 0 -> evicts block 0 from L2
  EXPECT_FALSE(sim.level(0).contains(0)) << "inclusion must remove block 0 from L1";
  EXPECT_EQ(sim.stats().levels[1].back_invalidations, 1u);
  EXPECT_EQ(sim.stats().memory_writes, 1u) << "dirty L1 data must reach memory";

  h.inclusion = Inclusion::NINE;
  Hierarchy nine(h);
  nine.access(0, 8, true);
  nine.access(4 * 64, 8, false);
  EXPECT_TRUE(nine.level(0).contains(0));
  EXPECT_EQ(nine.stats().memory_writes, 0u);
}

TEST(Hierarchy, ExclusiveRequiresWriteBack) {
  HierarchyConfig h = twoLevel(Inclusion::Exclusive);
  h.levels[0].write_hit = WriteHit::WriteThrough;
  EXPECT_THROW(Hierarchy{h}, std::invalid_argument);
}
