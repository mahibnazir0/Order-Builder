#pragma once
// ============================================================================
// laneClassifier.hpp - M4 module C4. Routes each group, per ship date, to the
// method that will solve it.
//
// One route per segregated group and reserved ship date: reservations are per
// lane and date and are never summed across dates, and groups are classified
// one by one, never per lane. The order of the rules matters:
//   1. segregation: a group split from its lane by a live do-not-mix pair, or
//      any group of a lane split that way, takes the conflicts variant of its
//      method (its trucks never carry another group)
//   2. the reservation: no truck reserved means no route. The groups of a lane
//      share its lane-date reservation, dealt out one estimated truckload at a
//      time to the most urgent goods (overdue, ship date, priority) and, among
//      equally urgent, the fullest load; what is left goes to the group with the
//      most urgent load. One truck allocated, or demand above the allocation,
//      is a selection problem -> knapsack
//   3. variety: unit loads per distinct product above varietyRatio (few
//      products, many units) -> patternPacker, otherwise binPacker. Measured
//      lanes of 1-3 products fall on the first side and of 11+ on the second;
//      the ratio decides, so the 13-product largest lane still goes to
//      patternPacker
//   4. size: reserved trucks above m5Threshold -> M5, whatever the method
//      would have been; the route keeps that method so the report can say
//
// An item is routed to its ship date, or to the day it becomes available when
// that is later. An overdue item is routed to its lane's first reserved date in
// the window, so it rides the first truck rather than none. Items after the
// window, or not available within it, are left out and listed.
// ============================================================================

#include "reservationBook.hpp"
#include "selectionItem.hpp"
#include "trailerSpec.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ob {

// By reference only, so pipeline.hpp can hold M4's results without including itself.
struct PipelineResult;

enum class SolveMethod {
    Knapsack,
    PatternPacker,
    BinPacker,
    M5,          // escalated: no solver is called in M4
    Unrouted,    // demand with no truck reserved on that date: no truck is built
    NoDemand,    // trucks reserved and nothing to load: penalty exposure
};

enum class ConflictReason {
    None,
    DoNotMix,          // this group is split from its lane by a live do-not-mix pair
    SegregationSplit,  // another group of this lane is; trucks never mix the two
};

// No defaults: every figure comes from the run's settings (benchmarked, per the spec).
struct LaneClassifierSettings {
    int m5Threshold = 0;        // at least 2
    double varietyRatio = 0.0;  // unit loads per product; positive and finite
};

struct LaneRoute {
    std::size_t groupIndex = 0;     // into PipelineResult::segregation.groups
    std::string shipDate;
    long long reservedTrucks = 0;   // the lane's reservation on shipDate
    // Groups of this lane drawing on the same reservation.
    std::size_t groupsSharingReservation = 1;
    // This group's share of reservedTrucks (rule 2). The shares of a lane-date add up to its
    // reservation.
    long long trucksAllocated = 0;
    std::vector<std::size_t> itemIndices; // into SelectionItems::items, in selection order
    double unitLoads = 0.0;
    std::size_t distinctProducts = 0;
    // The fewest trucks the items could need, on floor area or payload alone: the limits the
    // solvers pack to (Rev 8: a stack is judged by the floor it takes).
    long long demandTrucks = 0;
    bool demandAboveReservation = false;  // above trucksAllocated
    bool shortLane = false;          // demand below trucksAllocated: trucks will go unused
    ConflictReason conflict = ConflictReason::None;
    SolveMethod method = SolveMethod::Unrouted;
    // For a route escalated to M5, the method it would otherwise have used.
    std::optional<SolveMethod> methodBeforeEscalation;
};

enum class UnroutedItemReason {
    AfterWindow,         // ship date after the window: left for M6
    UnavailableInWindow, // not available by the window's last ship date
};

struct UnroutedItem {
    std::size_t itemIndex = 0;
    UnroutedItemReason reason = UnroutedItemReason::AfterWindow;
};

struct LaneClassification {
    // Sorted by group, then ship date.
    std::vector<LaneRoute> routes;
    std::vector<UnroutedItem> itemsLeftOut;
};

// Where each item goes: the ship date of its route, or empty for an item in no route.
struct ItemRouting {
    std::vector<std::string> routeDates;  // parallel to SelectionItems::items
    std::vector<UnroutedItem> itemsLeftOut;
};

// Each item's route date by the rule above, or empty for an item left out, listed with its
// reason. Throws std::invalid_argument when the items do not belong to this run.
ItemRouting routeItems(const PipelineResult& run, const ReservationBook& book, const SelectionItems& items);

// Throws std::invalid_argument for settings outside their ranges, or when the items do not
// belong to this run (a group index out of range).
LaneClassification classifyLanes(const PipelineResult& run, const ReservationBook& book,
                                 const SelectionItems& items,
                                 const LaneClassifierSettings& settings);

// The same, with each item routed to routing.routeDates instead: an item with an empty date is
// in no route, and routing.itemsLeftOut is the classification's. Throws as above, and when the
// dates are not one per item.
LaneClassification classifyLanes(const PipelineResult& run, const ReservationBook& book,
                                 const SelectionItems& items, const LaneClassifierSettings& settings,
                                 const ItemRouting& routing);

// What one item takes of a truck on the limits demand is estimated on: floor area and payload,
// the limits the solvers pack to. Cube is not one of them: each stack is under the ceiling, so
// floor bounds it, and counting a truck full at fullCubeFraction of cube would estimate more
// trucks than the solvers build.
struct TruckShares {
    double floor = 0.0;
    double weight = 0.0;

    double largest() const { return floor > weight ? floor : weight; }
    TruckShares& operator+=(const TruckShares& other) {
        floor += other.floor;
        weight += other.weight;
        return *this;
    }
};

TruckShares truckSharesOf(const SelectionItem& item, const TrailerSpec& trailer);

// "knapsack", "patternPacker", ... Plain ASCII.
const char* solveMethodName(SolveMethod method);

} // namespace ob
