// Calculation engine — displacement by period of action (kernel).
#include <gtest/gtest.h>
#include "backend/calculation/actionPeriodDisplacement.h"

namespace {

using I = ibActionInterval;
using R = ibActionPeriodRecord;

// Convenience: assert result[i] equals the given intervals.
void ExpectIntervals(const std::vector<I>& got, const std::vector<I>& want, const char* what) {
	ASSERT_EQ(got.size(), want.size()) << what;
	for (size_t k = 0; k < want.size(); ++k) {
		EXPECT_EQ(got[k].start, want[k].start) << what << " [" << k << "].start";
		EXPECT_EQ(got[k].end,   want[k].end)   << what << " [" << k << "].end";
	}
}

} // namespace

TEST(CalcDisplacement, DisjointRecordsAreUntouched) {
	// Two records that do not overlap keep their whole action period regardless of priority.
	auto r = ibComputeActionPeriodDisplacement({
		{ /*prio*/ 10, /*[*/ 0, /*)*/ 10 },
		{ /*prio*/ 20, /*[*/ 20, /*)*/ 30 },
	});
	ExpectIntervals(r[0], { { 0, 10 } }, "low disjoint");
	ExpectIntervals(r[1], { { 20, 30 } }, "high disjoint");
}

TEST(CalcDisplacement, HigherPriorityDisplacesLowerOverOverlap) {
	// High [5,15) carves a hole in low [0,20): low keeps [0,5) and [15,20).
	auto r = ibComputeActionPeriodDisplacement({
		{ /*low*/  1, 0, 20 },
		{ /*high*/ 9, 5, 15 },
	});
	ExpectIntervals(r[0], { { 0, 5 }, { 15, 20 } }, "low split by high");
	ExpectIntervals(r[1], { { 5, 15 } }, "high full");
}

TEST(CalcDisplacement, FullyCoveredRecordIsCompletelyDisplaced) {
	auto r = ibComputeActionPeriodDisplacement({
		{ 1, 5, 10 },     // low, fully inside high
		{ 9, 0, 100 },    // high covers everything
	});
	EXPECT_TRUE(r[0].empty()) << "low fully displaced";
	ExpectIntervals(r[1], { { 0, 100 } }, "high full");
}

TEST(CalcDisplacement, EqualPriorityRecordsCoexist) {
	// Same priority never displaces — both overlapping records keep their full period.
	auto r = ibComputeActionPeriodDisplacement({
		{ 5, 0, 10 },
		{ 5, 5, 15 },
	});
	ExpectIntervals(r[0], { { 0, 10 } }, "equal a");
	ExpectIntervals(r[1], { { 5, 15 } }, "equal b");
}

TEST(CalcDisplacement, MultipleHigherRecordsUnionThenSubtract) {
	// Low [0,30) displaced by two higher: [2,8) and [7,12) merge to [2,12); low keeps [0,2) and [12,30).
	auto r = ibComputeActionPeriodDisplacement({
		{ 1, 0, 30 },
		{ 9, 2, 8 },
		{ 9, 7, 12 },
	});
	ExpectIntervals(r[0], { { 0, 2 }, { 12, 30 } }, "low minus union");
}

TEST(CalcDisplacement, AdjacentHigherIntervalsMergeAndLeaveNoGap) {
	// [0,5) and [5,10) touch: their union is [0,10), so low [0,10) is fully displaced (no [5,5) sliver).
	auto r = ibComputeActionPeriodDisplacement({
		{ 1, 0, 10 },
		{ 9, 0, 5 },
		{ 9, 5, 10 },
	});
	EXPECT_TRUE(r[0].empty()) << "adjacent cover leaves no gap";
}

TEST(CalcDisplacement, ThreeTierPriorityChain) {
	// Priorities 1 < 5 < 9 all over [0,30); only the top survives whole, middle keeps what top leaves,
	// bottom keeps what both leave.
	auto r = ibComputeActionPeriodDisplacement({
		{ 1, 0, 30 },   // bottom
		{ 5, 0, 20 },   // middle
		{ 9, 10, 15 },  // top
	});
	ExpectIntervals(r[2], { { 10, 15 } }, "top whole");
	ExpectIntervals(r[1], { { 0, 10 }, { 15, 20 } }, "middle minus top");
	// bottom minus union(middle[0,20), top[10,15]) = minus [0,20) -> keeps [20,30)
	ExpectIntervals(r[0], { { 20, 30 } }, "bottom minus higher");
}

TEST(CalcDisplacement, EmptyInput) {
	auto r = ibComputeActionPeriodDisplacement({});
	EXPECT_TRUE(r.empty());
}

TEST(CalcDisplacement, EmptyActionPeriodRecordYieldsNothing) {
	// A record with a zero-length action period ([5,5)) has no actual period; a normal record beside it
	// (even with lower priority) is untouched by that empty one.
	auto r = ibComputeActionPeriodDisplacement({
		{ 9, 5, 5 },     // high but empty
		{ 1, 0, 10 },    // low, non-empty
	});
	EXPECT_TRUE(r[0].empty()) << "empty base -> no actual period";
	ExpectIntervals(r[1], { { 0, 10 } }, "empty high does not displace");
}

TEST(CalcDisplacement, SingleRecordKeepsWholePeriod) {
	auto r = ibComputeActionPeriodDisplacement({ { 7, 100, 200 } });
	ExpectIntervals(r[0], { { 100, 200 } }, "lone record");
}
