#include "calculationBase.h"

#include <algorithm>

std::vector<ibBaseContribution>
ibComputeBaseContributions(int64_t queryStart, int64_t queryEnd,
                           const std::vector<std::vector<ibActionInterval>>& baseActualPeriods)
{
	std::vector<ibBaseContribution> out(baseActualPeriods.size());
	const bool emptyQuery = (queryEnd <= queryStart);

	for (size_t i = 0; i < baseActualPeriods.size(); ++i) {
		int64_t total = 0, overlap = 0;
		for (const ibActionInterval& iv : baseActualPeriods[i]) {
			if (iv.end <= iv.start)
				continue;
			total += iv.end - iv.start;
			if (!emptyQuery) {
				const int64_t lo = std::max(iv.start, queryStart);
				const int64_t hi = std::min(iv.end,   queryEnd);
				if (hi > lo)
					overlap += hi - lo;
			}
		}
		out[i] = { total, overlap };
	}
	return out;
}
