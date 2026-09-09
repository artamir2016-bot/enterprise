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
	if (n == 0)
		return out;

	// ⭐ TIERED SWEEP instead of the O(n²) pairwise scan. Records are visited by DESCENDING priority, and
	// a single running `cover` — the merged action periods of every STRICTLY-higher tier seen so far — is
	// carried forward. A record's actual period is just its span minus that cover; equal-priority records
	// share a tier and never see each other (they do not displace), so the cover is grown only AFTER a
	// whole tier is computed. With periods that coalesce (the usual case) the cover stays tiny and the
	// whole pass is ~O(n log n) — versus the previous re-collect-sort-merge of all higher records per row.
	std::vector<size_t> order(n);
	for (size_t i = 0; i < n; ++i) order[i] = i;
	std::sort(order.begin(), order.end(),
		[&records](size_t a, size_t b) { return records[a].priority > records[b].priority; });

	std::vector<ibActionInterval> cover;   // merged, sorted; all strictly-higher tiers so far
	size_t coverN = 0;

	size_t idx = 0;
	while (idx < n) {
		const int64_t prio = records[order[idx]].priority;
		const size_t tierBegin = idx;
		while (idx < n && records[order[idx]].priority == prio)
			++idx;

		// Actuals for this tier against the strictly-higher cover.
		for (size_t k = tierBegin; k < idx; ++k) {
			const ibActionPeriodRecord& r = records[order[k]];
			if (r.end > r.start)
				SubtractCoverInto(r.start, r.end, cover, coverN, out[order[k]]);
		}

		// Fold this tier into the cover for the tiers below it (append the tier's non-empty periods, then
		// re-merge). Calculation types are a small set, so the number of tiers is small and this is cheap.
		if (idx < n) {   // no need to grow the cover after the LAST (lowest) tier
			cover.resize(coverN);
			for (size_t k = tierBegin; k < idx; ++k) {
				const ibActionPeriodRecord& r = records[order[k]];
				if (r.end > r.start)
					cover.push_back({ r.start, r.end });
			}
			coverN = MergeInPlace(cover);
		}
	}
	return out;
}
