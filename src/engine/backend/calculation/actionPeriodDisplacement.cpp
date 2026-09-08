#include "actionPeriodDisplacement.h"

#include <algorithm>

namespace {

// Merge a set of half-open intervals into a minimal sorted, non-overlapping cover. Adjacent
// intervals ([a,b) + [b,c) = [a,c)) merge too — a gap needs a strictly-positive width to survive,
// which is what makes "the parts NOT covered" come out right.
std::vector<ibActionInterval> MergeIntervals(std::vector<ibActionInterval> ints)
{
	std::vector<ibActionInterval> out;
	ints.erase(std::remove_if(ints.begin(), ints.end(),
		[](const ibActionInterval& i) { return i.end <= i.start; }), ints.end());
	std::sort(ints.begin(), ints.end(),
		[](const ibActionInterval& a, const ibActionInterval& b) {
			return a.start != b.start ? a.start < b.start : a.end < b.end;
		});
	for (const ibActionInterval& i : ints) {
		if (!out.empty() && i.start <= out.back().end)
			out.back().end = std::max(out.back().end, i.end);   // overlap or touch -> extend
		else
			out.push_back(i);
	}
	return out;
}

// Subtract a sorted, merged cover from a base interval [s, e): the remaining sub-intervals.
std::vector<ibActionInterval> SubtractCover(int64_t s, int64_t e,
                                            const std::vector<ibActionInterval>& cover)
{
	std::vector<ibActionInterval> result;
	if (e <= s)
		return result;   // empty base -> nothing remains
	int64_t cur = s;
	for (const ibActionInterval& c : cover) {
		if (c.end <= cur)          continue;   // entirely before the cursor
		if (c.start >= e)          break;      // sorted -> the rest are past the base
		if (c.start > cur)
			result.push_back({ cur, std::min(c.start, e) });
		cur = std::max(cur, c.end);
		if (cur >= e)              break;      // base fully consumed
	}
	if (cur < e)
		result.push_back({ cur, e });
	return result;
}

} // namespace

std::vector<std::vector<ibActionInterval>>
ibComputeActionPeriodDisplacement(const std::vector<ibActionPeriodRecord>& records)
{
	std::vector<std::vector<ibActionInterval>> out(records.size());
	for (size_t i = 0; i < records.size(); ++i) {
		const ibActionPeriodRecord& r = records[i];
		// The displacers: every record with STRICTLY higher priority (equal priorities coexist).
		std::vector<ibActionInterval> higher;
		for (size_t j = 0; j < records.size(); ++j) {
			if (j == i) continue;
			if (records[j].priority > r.priority)
				higher.push_back({ records[j].start, records[j].end });
		}
		out[i] = SubtractCover(r.start, r.end, MergeIntervals(std::move(higher)));
	}
	return out;
}
