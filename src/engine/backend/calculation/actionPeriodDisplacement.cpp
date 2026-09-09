#include "actionPeriodDisplacement.h"

#include <algorithm>

namespace {

// Sort `iv` and compact it IN PLACE into a minimal non-overlapping cover; returns the logical size of
// the merged prefix [0, N). Adjacent intervals ([a,b)+[b,c)=[a,c)) merge too — a gap needs a strictly
// positive width to survive, which is what makes "the parts NOT covered" come out right. In place so no
// second vector is allocated per record (this runs once per displaced record on the write path).
size_t MergeInPlace(std::vector<ibActionInterval>& iv)
{
	// Drop empty intervals, compacting toward the front.
	size_t w = 0;
	for (size_t r = 0; r < iv.size(); ++r)
		if (iv[r].end > iv[r].start)
			iv[w++] = iv[r];
	if (w <= 1)
		return w;

	std::sort(iv.begin(), iv.begin() + w,
		[](const ibActionInterval& a, const ibActionInterval& b) {
			return a.start != b.start ? a.start < b.start : a.end < b.end;
		});

	size_t m = 0;   // last kept merged index
	for (size_t r = 1; r < w; ++r) {
		if (iv[r].start <= iv[m].end) {            // overlap or touch -> extend
			if (iv[r].end > iv[m].end) iv[m].end = iv[r].end;
		} else {
			iv[++m] = iv[r];
		}
	}
	return m + 1;
}

// Subtract the merged cover in cover[0, coverN) from base [s, e), APPENDING the remaining sub-intervals
// straight into `out` (no temporary vector, no return copy).
void SubtractCoverInto(int64_t s, int64_t e, const std::vector<ibActionInterval>& cover, size_t coverN,
                       std::vector<ibActionInterval>& out)
{
	if (e <= s)
		return;   // empty base -> nothing remains
	int64_t cur = s;
	for (size_t k = 0; k < coverN; ++k) {
		const ibActionInterval& c = cover[k];
		if (c.end <= cur)          continue;   // entirely before the cursor
		if (c.start >= e)          break;      // sorted -> the rest are past the base
		if (c.start > cur)
			out.push_back({ cur, (c.start < e ? c.start : e) });
		if (c.end > cur) cur = c.end;
		if (cur >= e)              break;      // base fully consumed
	}
	if (cur < e)
		out.push_back({ cur, e });
}

} // namespace

std::vector<std::vector<ibActionInterval>>
ibComputeActionPeriodDisplacement(const std::vector<ibActionPeriodRecord>& records)
{
	const size_t n = records.size();
	std::vector<std::vector<ibActionInterval>> out(n);

	// ONE scratch cover, reused across records (cleared, not reallocated) — the per-record allocation of
	// the old code (a fresh `higher`, a merged copy, a subtracted copy) is gone. `out[i]` is written into
	// directly. The pairwise scan stays O(n²) — inherent to "each record against every higher one" — but a
	// record set posted by one document is small, and the hot cost was the allocations, not the compares.
	std::vector<ibActionInterval> higher;
	for (size_t i = 0; i < n; ++i) {
		const ibActionPeriodRecord& r = records[i];
		if (r.end <= r.start)
			continue;   // empty action period -> no actual period (out[i] stays empty)

		higher.clear();
		for (size_t j = 0; j < n; ++j)
			if (j != i && records[j].priority > r.priority && records[j].end > records[j].start)
				higher.push_back({ records[j].start, records[j].end });

		if (higher.empty()) {          // nothing displaces it (the common case) -> whole period stands
			out[i].push_back({ r.start, r.end });
			continue;
		}

		const size_t coverN = MergeInPlace(higher);
		SubtractCoverInto(r.start, r.end, higher, coverN, out[i]);
	}
	return out;
}
