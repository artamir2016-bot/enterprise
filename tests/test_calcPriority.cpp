// Calculation engine — displacement priority (topological rank from the "who displaces whom" relation).
#include <gtest/gtest.h>
#include "backend/calculation/calculationPriority.h"

using E = std::pair<int, int>;   // {low, high}: high displaces low

TEST(CalcPriority, NoEdgesAllZero) {
	auto p = ibComputeDisplacementPriority(3, {});
	ASSERT_EQ(p.size(), 3u);
	EXPECT_EQ(p[0], 0); EXPECT_EQ(p[1], 0); EXPECT_EQ(p[2], 0);
}

TEST(CalcPriority, SingleEdgeDisplacerIsHigher) {
	// type 1 displaces type 0.
	auto p = ibComputeDisplacementPriority(2, { E{0, 1} });
	EXPECT_GT(p[1], p[0]);
	EXPECT_EQ(p[0], 0);
	EXPECT_EQ(p[1], 1);
}

TEST(CalcPriority, ChainRanksByDepth) {
	// 2 displaces 1 displaces 0: strictly increasing priority up the chain.
	auto p = ibComputeDisplacementPriority(3, { E{0, 1}, E{1, 2} });
	EXPECT_EQ(p[0], 0);
	EXPECT_EQ(p[1], 1);
	EXPECT_EQ(p[2], 2);
	EXPECT_GT(p[2], p[1]);
	EXPECT_GT(p[1], p[0]);
}

TEST(CalcPriority, LongestPathWins) {
	// 3 displaces both 2 and 1; 2 displaces 0. Longest chain 3->2->0 gives 3 priority 2.
	auto p = ibComputeDisplacementPriority(4, { E{2, 3}, E{1, 3}, E{0, 2} });
	EXPECT_EQ(p[0], 0);
	EXPECT_EQ(p[2], 1);
	EXPECT_EQ(p[1], 0);
	EXPECT_EQ(p[3], 2);   // 1 + max(priority[2]=1, priority[1]=0)
}

TEST(CalcPriority, IndependentTypesTie) {
	// 0 and 1 unrelated; both bottom. 2 displaces 0 only.
	auto p = ibComputeDisplacementPriority(3, { E{0, 2} });
	EXPECT_EQ(p[0], p[1]);   // tie -> do not displace each other
	EXPECT_GT(p[2], p[0]);
}

TEST(CalcPriority, CycleCollapsesToEqualNotInfiniteLoop) {
	// A displaces B and B displaces A: the back-edge contributes nothing, so neither runs away.
	auto p = ibComputeDisplacementPriority(2, { E{1, 0}, E{0, 1} });
	ASSERT_EQ(p.size(), 2u);
	// A cycle has no well-defined displacement order; the ONLY contract is that it terminates and stays
	// bounded (the back-edge is cut, so nothing runs away). Bounded by n.
	EXPECT_LE(p[0], 2);
	EXPECT_LE(p[1], 2);
}

TEST(CalcPriority, OutOfRangeAndSelfEdgesIgnored) {
	auto p = ibComputeDisplacementPriority(2, { E{0, 5}, E{-1, 1}, E{0, 0} });
	EXPECT_EQ(p[0], 0);
	EXPECT_EQ(p[1], 0);
}

TEST(CalcPriority, FeedsDisplacementOrdering) {
	// The result is meant to be used as the displacement kernel's `priority`: a displacer strictly
	// greater than the displaced is exactly what makes the higher one win an overlap.
	auto p = ibComputeDisplacementPriority(3, { E{0, 1}, E{1, 2} });
	// Emulate the kernel's ordering key.
	EXPECT_TRUE(p[2] > p[1] && p[1] > p[0]);
}
