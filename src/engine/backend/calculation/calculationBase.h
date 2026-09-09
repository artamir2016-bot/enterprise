#ifndef __CALCULATION_BASE_H__
#define __CALCULATION_BASE_H__

// Calculation engine — the base computation (ПолучитьБазу / GetBase), interval kernel.
//
// A dependent calculation reads its BASE from base-register records whose ACTUAL action period (after
// displacement — see actionPeriodDisplacement.h) falls inside the dependent record's BASE period. Each
// base record contributes its resource in proportion to how much of its actual period lies in the query
// base period: contribution = value * overlap / total. 1C calls this the proportional-by-period base.
//
// This header is the PURE interval math — it returns, per base record, the {total, overlap} durations;
// the caller applies them to the exact-decimal resource value (ibNumber) and picks the resolution
// (proportional weight overlap/total, or a plain "counts if it overlaps" test). Kept storage- and
// decimal-free so it is deterministic and unit-tested, exactly like the displacement kernel it composes
// with.

#include "backend.h"                 // BACKEND_API
#include "actionPeriodDisplacement.h" // ibActionInterval

#include <cstdint>
#include <vector>

// For one base record: the total length of its actual action period, and how much of it overlaps the
// query base period. total == 0 means the record has no actual period (fully displaced) — the caller
// treats its proportional weight as 0.
struct ibBaseContribution {
	int64_t totalLength;     // sum of the base record's actual sub-interval lengths (>= 0)
	int64_t overlapLength;   // portion of totalLength inside the query base period (0 .. totalLength)
};

// Per base record (result[i] pairs with baseActualPeriods[i]), the {total, overlap} against the query
// base period [queryStart, queryEnd). A base record's actual period may be several sub-intervals (the
// displacement kernel can split it), so both totals sum across the sub-intervals.
BACKEND_API std::vector<ibBaseContribution>
ibComputeBaseContributions(int64_t queryStart, int64_t queryEnd,
                           const std::vector<std::vector<ibActionInterval>>& baseActualPeriods);

#endif
