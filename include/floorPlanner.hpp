#pragma once

#include "floorBound.hpp"
#include "joiner.hpp"
#include "paramsTypes.hpp"
#include "segregationTypes.hpp"
#include "unitLoadMetrics.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ob {

// A grouped line that does not count toward the floor, and why. Reported, never a silent zero.
struct FloorExcludedLine {
    std::size_t lineIndex = 0;
    // None when the metrics are fine but the unit load is taller than the trailer ceiling.
    UnitLoadMetricsError error = UnitLoadMetricsError::None;
    bool overCeiling = false;
};

struct GroupFloor {
    FloorTotals totals;
    FloorBoundResult bound;
    std::size_t linesCounted = 0;
};

struct LaneFloor {
    std::string locationFrom;
    std::string locationTo;
    std::string shipCondition;
    // Indices into FloorPlan::groups (and SegregationResult::groups), in segregation order.
    std::vector<std::size_t> groupIndices;
    FloorTotals totals;
    // Sum of the groups' fractional bounds; never a bound computed across the lane.
    double boundTrucks = 0.0;
    long long floorTrucks = 0;
    long long noStackingBaselineTrucks = 0;
};

struct FloorPlan {
    FloorRoundingPoint roundingPoint = FloorRoundingPoint::Group;
    std::vector<GroupFloor> groups; // parallel to SegregationResult::groups
    std::vector<LaneFloor> lanes;   // in order of each lane's first group
    FloorTotals totals;
    double boundTrucks = 0.0;
    long long floorTrucks = 0;      // always the sum of the lane floors
    long long noStackingBaselineTrucks = 0;
    std::vector<FloorExcludedLine> excludedLines;
    std::size_t casesPerUnitLoadMismatchLines = 0;
};

// Applies floorBound to every segregated group and sums. The bound is always taken per group,
// never across a lane, because segregated groups cannot share trucks. The rounding point only
// decides where fractional trucks are rounded up: per group before summing (the confirmed
// rule) or once per lane. The no-stacking baseline is always rounded per group.
// Lines with a metrics error, or a unit load taller than the ceiling, are left out of the
// totals and listed in excludedLines; removing demand can only lower the bound, so it stays
// valid. Throws std::invalid_argument for a group index outside lines (a caller bug), and
// whatever floorBound throws for an unusable trailer.
FloorPlan planFloor(const SegregationResult& segregation, const std::vector<JoinedLine>& lines,
                    const M2Params& params, const TrailerSpec& trailer);

// Stable plain-ASCII name for reports.
const char* floorRoundingPointName(FloorRoundingPoint roundingPoint);

} // namespace ob
