// Calculation engine — displacement by period of action (kernel).
#include <gtest/gtest.h>
#include "backend/calculation/actionPeriodDisplacement.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace {

using I = ibActionInterval;
using R = ibActionPeriodRecord;

// ---- OLD (pre-optimization) implementation, kept HERE only to benchmark against the new one ---------
// A faithful copy of the original: a fresh `higher` vector per record, MergeIntervals returns a new
// vector, SubtractCover returns a new vector -> ~3 allocations per record.
std::vector<I> Naive_Merge(std::vector<I> ints) {
	std::vector<I> out;
	ints.erase(std::remove_if(ints.begin(), ints.end(),
		[](const I& i) { return i.end <= i.start; }), ints.end());
	std::sort(ints.begin(), ints.end(),
		[](const I& a, const I& b) { return a.start != b.start ? a.start < b.start : a.end < b.end; });
	for (const I& i : ints) {
		if (!out.empty() && i.start <= out.back().end) out.back().end = std::max(out.back().end, i.end);
		else out.push_back(i);
	}
	return out;
}
std::vector<I> Naive_Subtract(int64_t s, int64_t e, const std::vector<I>& cover) {
	std::vector<I> result;
	if (e <= s) return result;
	int64_t cur = s;
	for (const I& c : cover) {
		if (c.end <= cur) continue;
		if (c.start >= e) break;
		if (c.start > cur) result.push_back({ cur, std::min(c.start, e) });
		cur = std::max(cur, c.end);
		if (cur >= e) break;
	}
	if (cur < e) result.push_back({ cur, e });
	return result;
}
std::vector<std::vector<I>> NaiveDisplacement(const std::vector<R>& records) {
	std::vector<std::vector<I>> out(records.size());
	for (size_t i = 0; i < records.size(); ++i) {
		const R& r = records[i];
		std::vector<I> higher;
		for (size_t j = 0; j < records.size(); ++j)
			if (j != i && records[j].priority > r.priority)
				higher.push_back({ records[j].start, records[j].end });
		out[i] = Naive_Subtract(r.start, r.end, Naive_Merge(std::move(higher)));
	}
	return out;
}

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

// Benchmark: old (naive, ~3 allocs/record) vs new (scratch reuse + in-place + early-outs). DISABLED by
// default; run with: oes_tests --gtest_also_run_disabled_tests --gtest_filter=*CalcDisplacementBench*
// Also asserts the two implementations produce IDENTICAL results on the dataset (equivalence, not just
// speed). This is how the optimization is verified: same output, measured time.
TEST(CalcDisplacement, DISABLED_CalcDisplacementBench) {
	// A realistic-ish set: many records, tiered priorities, overlapping action periods.
	const int N = 3000, TIERS = 40, ITERS = 40;
	std::vector<R> recs;
	recs.reserve(N);
	for (int i = 0; i < N; ++i) {
		int64_t start = (int64_t)(i % 500);          // heavy overlap across records
		recs.push_back({ /*priority*/ (int64_t)(i % TIERS), start, start + 50 });
	}

	// Equivalence on this dataset.
	{
		auto a = NaiveDisplacement(recs);
		auto b = ibComputeActionPeriodDisplacement(recs);
		ASSERT_EQ(a.size(), b.size());
		for (size_t i = 0; i < a.size(); ++i) {
			ASSERT_EQ(a[i].size(), b[i].size()) << "row " << i;
			for (size_t k = 0; k < a[i].size(); ++k) {
				EXPECT_EQ(a[i][k].start, b[i][k].start);
				EXPECT_EQ(a[i][k].end,   b[i][k].end);
			}
		}
	}

	using clock = std::chrono::steady_clock;
	volatile size_t sink = 0;

	auto t0 = clock::now();
	for (int it = 0; it < ITERS; ++it) { auto r = NaiveDisplacement(recs); sink += r.size(); }
	auto t1 = clock::now();
	for (int it = 0; it < ITERS; ++it) { auto r = ibComputeActionPeriodDisplacement(recs); sink += r.size(); }
	auto t2 = clock::now();

	const double oldMs = std::chrono::duration<double, std::milli>(t1 - t0).count() / ITERS;
	const double newMs = std::chrono::duration<double, std::milli>(t2 - t1).count() / ITERS;
	std::printf("\n[CalcDisplacementBench] N=%d tiers=%d iters=%d\n  OLD (naive): %.3f ms/call\n  NEW (opt):   %.3f ms/call\n  speedup:     %.2fx\n",
		N, TIERS, ITERS, oldMs, newMs, oldMs / (newMs > 0 ? newMs : 1e-9));
	(void)sink;
	SUCCEED();
}
