#ifndef __ACTION_PERIOD_DISPLACEMENT_H__
#define __ACTION_PERIOD_DISPLACEMENT_H__

// Calculation engine — displacement by period of action (вытеснение по периоду действия).
//
// This is the mathematical kernel of a calculation register's recalculation engine. A calculation
// record is in force over an INTERVAL [start, end) — its action period. When several records of
// competing calculation types overlap in time, higher-priority records DISPLACE lower-priority ones:
// a lower record is only actually in force over the parts of its action period NOT covered by any
// higher-priority record. That remaining span is the record's ACTUAL action period (фактический
// период действия), and it is what the base sum and the result are computed over.
//
// This header is the PURE interval math, independent of storage, the record-set object, and where the
// priority comes from (the chart of calculation types' displacing lists). Those wire it up in later
// increments; here it is a deterministic, unit-tested function so the hard part is correct first.

#include "backend.h"   // BACKEND_API

#include <cstdint>
#include <vector>

// A calculation record's action period + its displacement priority. Ticks are engine date ticks
// (any monotonic int64 unit); [start, end) is half-open, so an empty period is start == end.
struct ibActionPeriodRecord {
	int64_t priority;   // HIGHER value displaces LOWER; equal priorities coexist (never displace)
	int64_t start;      // inclusive
	int64_t end;        // exclusive; caller guarantees end >= start
};

// A half-open interval [start, end).
struct ibActionInterval {
	int64_t start;
	int64_t end;
};

// For each input record (result[i] pairs with records[i]), the sub-intervals of its action period
// that remain after subtracting the union of every STRICTLY-higher-priority record's action period.
// An empty result[i] means the record is fully displaced (no actual action period).
BACKEND_API std::vector<std::vector<ibActionInterval>>
ibComputeActionPeriodDisplacement(const std::vector<ibActionPeriodRecord>& records);

#endif
