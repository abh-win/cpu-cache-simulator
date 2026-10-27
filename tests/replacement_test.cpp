#include <gtest/gtest.h>

#include "cachesim/replacement.h"

using namespace cachesim;

TEST(LRU, EvictsLeastRecentlyUsed) {
  LRUPolicy p(4);
  for (uint32_t w = 0; w < 4; ++w) p.onFill(w);
  EXPECT_EQ(p.victim(), 0u);
  p.onHit(0);
  EXPECT_EQ(p.victim(), 1u);
  p.onHit(1);
  p.onHit(2);
  EXPECT_EQ(p.victim(), 3u);
}

TEST(FIFO, IgnoresHits) {
  FIFOPolicy p(4);
  for (uint32_t w = 0; w < 4; ++w) p.onFill(w);
  p.onHit(0);
  EXPECT_EQ(p.victim(), 0u);
  p.onFill(0);  // replaced
  EXPECT_EQ(p.victim(), 1u);
}

TEST(TreePLRU, MatchesHandTrace4Way) {
  TreePLRUPolicy p(4);
  // Fill A0 B1 C2 D3, then touch 0: victim must be in right half and not the
  // most recent of that half (3) -> 2.
  for (uint32_t w = 0; w < 4; ++w) p.onFill(w);
  EXPECT_EQ(p.victim(), 0u);  // last touch was 3 -> root points left, left node points to 0
  p.onHit(0);
  EXPECT_EQ(p.victim(), 2u);
  p.onHit(2);
  EXPECT_EQ(p.victim(), 1u);
}

TEST(TreePLRU, NeverEvictsMostRecent) {
  for (uint32_t ways : {2u, 4u, 8u, 16u}) {
    TreePLRUPolicy p(ways);
    std::mt19937 rng(ways);
    for (int i = 0; i < 1000; ++i) {
      uint32_t w = rng() % ways;
      p.onHit(w);
      EXPECT_NE(p.victim(), w);
    }
  }
}

TEST(TreePLRU, RejectsNonPowerOfTwo) { EXPECT_THROW(TreePLRUPolicy(6), std::invalid_argument); }

TEST(Random, InRangeAndDeterministic) {
  RandomPolicy a(8, 42), b(8, 42);
  for (int i = 0; i < 100; ++i) {
    uint32_t v = a.victim();
    EXPECT_LT(v, 8u);
    EXPECT_EQ(v, b.victim());
  }
}

TEST(Factory, BuildsOnePolicyPerSetOfRightKind) {
  CacheConfig c;
  c.size_bytes = 4096;
  c.block_bytes = 64;
  c.assoc = 4;
  c.policy = PolicyKind::TreePLRU;
  auto ps = makePolicies(c);
  ASSERT_EQ(ps.size(), 16u);
  EXPECT_STREQ(ps[0]->name(), "PLRU");
  EXPECT_STREQ(makePolicy(PolicyKind::FIFO, 2, 0)->name(), "FIFO");
}
