#include <gtest/gtest.h>

#include "cachesim/cache.h"

using namespace cachesim;

namespace {
CacheConfig cfg(uint64_t size, uint32_t block, uint32_t assoc, PolicyKind p = PolicyKind::LRU) {
  CacheConfig c;
  c.size_bytes = size;
  c.block_bytes = block;
  c.assoc = assoc;
  c.policy = p;
  return c;
}
}  // namespace

TEST(Cache, Geometry) {
  Cache c(cfg(32 * 1024, 64, 8));
  EXPECT_EQ(c.numSets(), 64u);
  EXPECT_EQ(c.ways(), 8u);
  Cache fa(cfg(1024, 64, 0));
  EXPECT_EQ(fa.numSets(), 1u);
  EXPECT_EQ(fa.ways(), 16u);
}

TEST(Cache, RejectsBadGeometry) {
  EXPECT_THROW(Cache(cfg(3000, 64, 1)), std::invalid_argument);
  EXPECT_THROW(Cache(cfg(4096, 48, 1)), std::invalid_argument);
  EXPECT_THROW(Cache(cfg(64 * 6 * 4, 64, 6, PolicyKind::TreePLRU)), std::invalid_argument);
}

TEST(Cache, DirectMappedConflict) {
  Cache c(cfg(1024, 64, 1));  // 16 sets
  EXPECT_FALSE(c.access(0, false));
  EXPECT_FALSE(c.fill(0, false).has_value());
  EXPECT_TRUE(c.access(0, false));
  auto v = c.fill(16, true);  // same set as block 0
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->block, 0u);
  EXPECT_FALSE(v->dirty);
  EXPECT_FALSE(c.contains(0));
  EXPECT_TRUE(c.contains(16));
}

TEST(Cache, DirtyTrackingAndInvalidate) {
  Cache c(cfg(1024, 64, 2));
  c.fill(5, false);
  EXPECT_TRUE(c.access(5, true));
  auto d = c.invalidate(5);
  ASSERT_TRUE(d.has_value());
  EXPECT_TRUE(*d);
  EXPECT_FALSE(c.invalidate(5).has_value());
  EXPECT_FALSE(c.markDirty(5));
}

TEST(Cache, LRUWithinSet) {
  Cache c(cfg(64 * 2 * 4, 64, 2));  // 4 sets, 2 ways
  c.fill(0, false);
  c.fill(4, false);
  c.access(0, false);       // 4 is now LRU
  auto v = c.fill(8, false);
  ASSERT_TRUE(v);
  EXPECT_EQ(v->block, 4u);
}
