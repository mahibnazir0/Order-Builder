#pragma once

#include "demandSelector.hpp"
#include "floorBound.hpp"
#include "joiner.hpp"
#include "paramsTypes.hpp"
#include "placeholder_types.hpp"
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
    std::size_t linesSelected = 0;
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
    std::size_t linesSelected = 0;
    // Below linesSelected when lines were left out: the floor is understated, and with no line
    // counted it is not measurable at all rather than a floor of zero.
    std::size_t linesCounted = 0;
    // Placeholder trucks on this lane for the plan's ship date (matchReservations).
    long long reservedTrucks = 0;

    bool isMeasurable() const { return linesCounted > 0; }
    bool isUnderstated() const { return linesCounted < linesSelected; }
};

struct FloorPlan {
    std::string shipDate; // the date the plan floors; empty under wholeExtract: every date
    FloorRoundingPoint roundingPoint = FloorRoundingPoint::Group;
    std::vector<GroupFloor> groups; // parallel to SegregationResult::groups
    // In order of each lane's first group. A group with no selected line joins no lane, so a
    // lane with no demand under the rule never appears as a zero-floor row.
    std::vector<LaneFloor> lanes;
    FloorTotals totals;
    double boundTrucks = 0.0;
    long long floorTrucks = 0;      // always the sum of the lane floors
    long long noStackingBaselineTrucks = 0;
    // Grouped lines the demand rule leaves out; they are not errors and are not listed.
    std::size_t linesNotSelected = 0;
    std::vector<FloorExcludedLine> excludedLines;
    std::size_t casesPerUnitLoadMismatchLines = 0;
    // Counted lines whose product has no layer size, so the pallet's footprint stood in.
    std::size_t linesWithoutLayerSize = 0;
    // The plan's ship date's placeholders (every placeholder under wholeExtract), the
    // Validator's rejected ones left out: matchReservations.
    long long reservedTrucks = 0;
    long long reservedTrucksWithoutDemand = 0; // on lanes with no line selected
    std::size_t lanesFloorAboveReserved = 0;   // the floor needs more trucks than were reserved
};

// One ship date's floor: the lines it counts and the plan built from them.
struct ShipDateFloor {
    DemandSelection selection;
    FloorPlan plan;
};

// A run's floors: one per ship date the demand rule covers, in date order, or a single one for
// wholeExtract. Each is computed and matched to its placeholders on its own, never summed into
// another day's (client, 27 Oct).
struct FloorRun {
    DemandSelector selector;
    ShipDateCoverage coverage;
    std::vector<ShipDateFloor> floors;

    long long floorTrucks() const;   // the daily floors added up
    long long reservedTrucks() const;
    std::size_t selectedLines() const;
    std::size_t linesLeftOut() const; // every floor's excludedLines
};

// Applies floorBound to every segregated group and sums, counting only the lines the demand
// rule selected: there is no floor without a rule. The bound is always taken per group,
// never across a lane, because segregated groups cannot share trucks. The rounding point only
// decides where fractional trucks are rounded up: per group before summing (the confirmed
// rule) or once per lane. The no-stacking baseline is always rounded per group.
// Lines with a metrics error, or a unit load taller than the ceiling, are left out of the
// totals and listed in excludedLines; removing demand can only lower the bound, so it stays
// valid. Throws std::invalid_argument for a group index outside lines or a selection that is
// not parallel to lines (caller bugs), and whatever floorBound throws for an unusable trailer.
FloorPlan planFloor(const SegregationResult& segregation, const std::vector<JoinedLine>& lines,
                    const DemandSelection& selection, const M2Params& params,
                    const TrailerSpec& trailer);

// Sets each lane's reserved trucks from the placeholders shipping on plan.shipDate (all of
// them when it is empty), and the plan's reservation totals. placeholderExcluded is parallel to
// placeholders (Validator::excludedPlaceholderFlags); an excluded placeholder reserves nothing.
// Throws std::invalid_argument when it is not parallel.
void matchReservations(FloorPlan& plan, const std::vector<PlaceholderRecord>& placeholders,
                       const std::vector<bool>& placeholderExcluded);

// Covers the selector's ship dates (coverShipDates, the calendar rule reading demand.ctl), then
// plans and matches a floor for each. lines must be the join of demand.str. Throws
// std::invalid_argument for the reasons coverShipDates, planFloor and matchReservations do.
FloorRun planFloorRun(const DemandSelector& selector, const DemandFile& demand,
                      const SegregationResult& segregation, const std::vector<JoinedLine>& lines,
                      const std::vector<PlaceholderRecord>& placeholders,
                      const std::vector<bool>& placeholderExcluded, const M2Params& params,
                      const TrailerSpec& trailer);

// Stable plain-ASCII name for reports.
const char* floorRoundingPointName(FloorRoundingPoint roundingPoint);

} // namespace ob
