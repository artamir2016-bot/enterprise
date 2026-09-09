#include "calculationPriority.h"

#include <algorithm>

// priority[high] = 1 + max(priority[low]) over everything `high` displaces, by longest path over the
// adjacency high -> [lows]. Memoised DFS with a "visiting" guard so a cycle's back-edge contributes 0
// (the mutual pair collapses to equal priority instead of recursing forever).
namespace {

int64_t Longest(int node, const std::vector<std::vector<int>>& displaces,
                std::vector<int64_t>& memo, std::vector<char>& state)
{
	// state: 0 = unvisited, 1 = on the current stack (visiting), 2 = done.
	if (state[node] == 2)
		return memo[node];
	if (state[node] == 1)
		return 0;   // back-edge into a node still being computed -> break the cycle, contribute nothing

	state[node] = 1;
	int64_t best = 0;
	for (const int low : displaces[node])
		best = std::max(best, 1 + Longest(low, displaces, memo, state));
	memo[node] = best;
	state[node] = 2;
	return best;
}

} // namespace

std::vector<int64_t>
ibComputeDisplacementPriority(size_t n, const std::vector<std::pair<int, int>>& displacedBy)
{
	std::vector<std::vector<int>> displaces(n);   // high -> [lows it displaces]
	for (const auto& e : displacedBy) {
		const int low = e.first, high = e.second;
		if (low < 0 || high < 0 || (size_t)low >= n || (size_t)high >= n || low == high)
			continue;
		displaces[high].push_back(low);
	}

	std::vector<int64_t> memo(n, 0);
	std::vector<char> state(n, 0);
	std::vector<int64_t> priority(n, 0);
	for (size_t i = 0; i < n; ++i)
		priority[i] = Longest((int)i, displaces, memo, state);
	return priority;
}
