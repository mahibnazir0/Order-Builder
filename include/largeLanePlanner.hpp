#pragma once
// ============================================================================
// largeLanePlanner.hpp - the seam between M4's selector and M5 (large lanes).
//
// The selector routes a lane-date to M5 when its reservation is above
// m5Threshold (laneClassifier rule 4). With a planner registered in
// SelectorSettings::largeLanePlanner, the selector hands it every group of that
// lane-date at once - they share one reservation pool - and writes back what it
// returns: trucks, one outcome per route and every item not loaded with its
// reason. With none registered the selector leaves the goods over as
// EscalatedToM5, exactly as before M5.
//
// The interface holds only M4 types, so M4 has no compile dependency on M5's
// implementation (m5Integration implements it).
// ============================================================================

#include "laneClassifier.hpp"
#include "selectionItem.hpp"
#include "selector.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace ob {

struct PipelineResult;

// One escalated lane-date, gathered from the classification (M5 handover P3).
struct LargeLaneRequest {
    std::string origin;
    std::string destination;
    std::string shipCondition;
    std::string shipDate;
    long long reservedTrucks = 0;  // the lane-date's pool, shared by every group
    std::vector<std::string> slotEquipment;  // each reserved truck's equipment code, as the reservation gives it
    // Parallel, one entry per route (group) of the lane-date, in route order.
    std::vector<std::size_t> routeIndices;
    std::vector<std::size_t> groupIndices;
    std::vector<std::vector<std::size_t>> itemIndicesByRoute;  // into SelectionItems::items
    std::vector<std::optional<SolveMethod>> methodBeforeEscalation;
    // How goods past due on the lane-date rank against its own (per lane, M5 handover P9).
    PastDueOrder pastDueOrder = PastDueOrder::AgeFirst;
};

// A lane-date: origin, destination, ship condition, ship date.
using LaneDateKey = std::tuple<std::string, std::string, std::string, std::string>;

// The classification's routes by lane-date, in key order: the bundles the selector solves, each
// the groups that share one reservation.
std::map<LaneDateKey, std::vector<std::size_t>> routesByLaneDate(const PipelineResult& run,
                                                                 const LaneClassification& classification);

// The request for the routes of one lane-date, as the selector hands them to M5. `routeIndices`
// is not empty and names routes of one lane-date.
LargeLaneRequest largeLaneRequestOf(const PipelineResult& run, const LaneClassification& classification,
                                    const std::vector<std::size_t>& routeIndices);

// Every lane-date the classifier escalated to M5, one request each in key order, gathered without a
// planner and without solving anything (M5 handover P3).
std::vector<LargeLaneRequest> collectEscalatedLaneDates(const PipelineResult& run, const LaneClassification& classification);

// What the selector writes back for the lane-date. Every offered item is on exactly one truck or
// in leftOver; trucks never number more than reservedTrucks.
struct LargeLaneResult {
    std::vector<PlannedTruck> trucks;
    std::vector<RouteOutcome> routes;  // one per route of the request
    std::vector<LeftOverItem> leftOver;
    long long reservationsUsed = 0;
    LargeLaneReport report;
};

class LargeLanePlanner {
public:
    virtual ~LargeLanePlanner() = default;
    // Called once per escalated lane-date, from a selector worker thread, so it must be reentrant.
    // Mutates none of its arguments. Reports failure in the result, never by throwing for bad input.
    virtual LargeLaneResult plan(const LargeLaneRequest& request, const PipelineResult& run,
                                 const SelectionItems& items, const SelectorSettings& settings) const = 0;
};

} // namespace ob
