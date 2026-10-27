#include <gtest/gtest.h>

#include <sstream>

#include "cachesim/trace.h"

using namespace cachesim;

TEST(Trace, LackeyFormat) {
  std::istringstream in(
      "==123== Lackey banner\n"
      "I  0400d7d4,8\n"
      " L 04f6b868,8\n"
      " S 7ff000398,4\n"
      " M 0421c7f0,4\n");
  std::vector<MemAccess> got;
  uint64_t n = forEachAccess(in, {}, [&](const MemAccess& a) { got.push_back(a); });
  ASSERT_EQ(n, 4u);  // I skipped by default, M -> 2
  EXPECT_EQ(got[0].kind, MemAccess::Kind::Read);
  EXPECT_EQ(got[0].addr, 0x04f6b868u);
  EXPECT_EQ(got[0].size, 8u);
  EXPECT_EQ(got[1].kind, MemAccess::Kind::Write);
  EXPECT_EQ(got[1].addr, 0x7ff000398u);
  EXPECT_EQ(got[2].kind, MemAccess::Kind::Read);
  EXPECT_EQ(got[3].kind, MemAccess::Kind::Write);
}

TEST(Trace, SimpleFormatAndInstructions) {
  std::istringstream in("# comment\nr 0x1000 4\nW 2000\nI 3000 2\n\n");
  std::vector<MemAccess> got;
  TraceOptions o;
  o.include_instructions = true;
  forEachAccess(in, o, [&](const MemAccess& a) { got.push_back(a); });
  ASSERT_EQ(got.size(), 3u);
  EXPECT_EQ(got[0].addr, 0x1000u);
  EXPECT_EQ(got[0].size, 4u);
  EXPECT_EQ(got[1].kind, MemAccess::Kind::Write);
  EXPECT_EQ(got[1].size, 1u);
  EXPECT_EQ(got[2].kind, MemAccess::Kind::Instr);
}

TEST(Trace, RejectsGarbage) {
  std::istringstream in("r 0x10\nX 0x20\n");
  EXPECT_THROW(forEachAccess(in, {}, [](const MemAccess&) {}), std::runtime_error);
}
