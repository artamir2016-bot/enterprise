#ifndef __CALCULATION_PRIORITY_H__
#define __CALCULATION_PRIORITY_H__

// Calculation engine — the displacement PRIORITY, derived from the "who displaces whom" relation.
//
// A calculation type's displacement priority is not stored as a number: it is IMPLIED by the per-record
// predefined "displacing calculation types" lists (which types displace this one). This header turns that
// relation into a total priority ordering the displacement kernel (actionPeriodDisplacement.h) consumes:
// a displacer always gets a STRICTLY GREATER priority than the type it displaces, so higher priority wins
// over overlapping action periods — exactly the rule the sweep applies.
//
// Pure integer graph math, storage- and metadata-free, so it is deterministic and unit-tested. The caller
// maps calculation-type ids to dense indices [0, n) and the priority back onto records; here it is only
// the ranking. The interim "priority = read order" used by the record-set write hook is replaced by
// feeding this the real displacing edges once the predefined chart data is available — nothing else in
// the displacement path changes.

#include "backend.h"   // BACKEND_API

#include <cstdint>
#include <utility>
#include <vector>

// Compute a displacement priority per calculation type.
//   n           — number of calculation types, indexed [0, n).
//   displacedBy — edges {low, high}: type `high` DISPLACES type `low` (so priority[high] > priority[low]).
// Returns priority[i] for each type: the length of the longest chain of types it displaces (directly or
// transitively). A type that displaces nothing gets 0; a type at the top of a displacement chain of
// length k gets k. Ties (types with no displacement relation between them) share a priority and — per the
// kernel — do not displace each other. Cycles are tolerated: a back-edge contributes nothing, so a mutual
// "A displaces B, B displaces A" collapses to equal priority rather than looping forever. Out-of-range or
// self edges are ignored.
BACKEND_API std::vector<int64_t>
ibComputeDisplacementPriority(size_t n, const std::vector<std::pair<int, int>>& displacedBy);

#endif
