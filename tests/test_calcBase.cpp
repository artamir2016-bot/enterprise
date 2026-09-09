// Calculation engine — base computation (GetBase) kernel.
#include <gtest/gtest.h>
#include "backend/calculation/calculationBase.h"
#include "backend/calculation/actionPeriodDisplacement.h"

namespace {
using I = ibActionInterval;
}

TEST(CalcBase, FullyInsideQueryContributesWholeValue) {
	// Base record actual [10,20) fully inside query [0,100): overlap == total, weight 1.
	auto c = ibComputeBaseContributions(0, 100, { { { 10, 20 } } });
	ASSERT_EQ(c.size(), 1u);
	EXPECT_EQ(c[0].totalLength, 10);
	EXPECT_EQ(c[0].overlapLength, 10);
}

TEST(CalcBase, PartialOverlapIsProportional) {
	// Actual [0,20), query [10,100): overlap [10,20) = 10 of 20 -> weight 1/2.
	auto c = ibComputeBaseContributions(10, 100, { { { 0, 20 } } });
	EXPECT_EQ(c[0].totalLength, 20);
	EXPECT_EQ(c[0].overlapLength, 10);
}

TEST(CalcBase, NoOverlapContributesZero) {
	auto c = ibComputeBaseContributions(100, 200, { { { 0, 20 } } });
	EXPECT_EQ(c[0].totalLength, 20);
	EXPECT_EQ(c[0].overlapLength, 0);
}

TEST(CalcBase, MultipleSubIntervalsSumBoth) {
	// A base record split by displacement into [0,5) and [15,20); query [3,17).
	// total = 5 + 5 = 10; overlap = [3,5)=2 + [15,17)=2 = 4.
	auto c = ibComputeBaseContributions(3, 17, { { { 0, 5 }, { 15, 20 } } });
	EXPECT_EQ(c[0].totalLength, 10);
	EXPECT_EQ(c[0].overlapLength, 4);
}

TEST(CalcBase, FullyDisplacedBaseRecordHasZeroTotal) {
	// Empty actual period (fully displaced base record): total 0 -> caller weight 0.
	auto c = ibComputeBaseContributions(0, 100, { { } });
	EXPECT_EQ(c[0].totalLength, 0);
	EXPECT_EQ(c[0].overlapLength, 0);
}

TEST(CalcBase, EmptyQueryPeriodYieldsZeroOverlap) {
	auto c = ibComputeBaseContributions(50, 50, { { { 0, 100 } } });
	EXPECT_EQ(c[0].totalLength, 100);
	EXPECT_EQ(c[0].overlapLength, 0);
}

TEST(CalcBase, ComposesWithDisplacementKernel) {
	// End-to-end: two base records overlap in time; the higher displaces the lower, then GetBase reads
	// the ACTUAL periods. base rec0 (low, [0,20)) displaced by rec1 (high, [5,15)) -> actual [0,5)+[15,20).
	auto actual = ibComputeActionPeriodDisplacement({
		{ /*prio*/ 1, 0, 20 },
		{ /*prio*/ 9, 5, 15 },
	});
	// Query base period [0,100) covers everything.
	auto c = ibComputeBaseContributions(0, 100, actual);
	EXPECT_EQ(c[0].totalLength, 10);   // [0,5)+[15,20)
	EXPECT_EQ(c[0].overlapLength, 10);
	EXPECT_EQ(c[1].totalLength, 10);   // [5,15)
	EXPECT_EQ(c[1].overlapLength, 10);
}
