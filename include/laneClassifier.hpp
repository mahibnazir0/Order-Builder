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
//   2. the reservation: no truck reserved means no route; one truck reserved,
//      or demand above the reservation, is a selection problem -> knapsack
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

#include "pipeline.hpp"
#include "reservationBook.hpp"
#include "selectionItem.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ob {

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
    // Groups of this lane drawing on the same reservation. Above 1, the reservation is not yet
    // divided between them.
    std::size_t groupsSharingReservation = 1;
    std::vector<std::size_t> itemIndices; // into SelectionItems::items, in selection order
    double unitLoads = 0.0;
    std::size_t distinctProducts = 0;
    // The fewest trucks the items could need, on positions, weight or cube alone.
    long long demandTrucks = 0;
    bool demandAboveReservation = false;
    bool shortLane = false;          // demand below the reservation: trucks will go unused
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

// Throws std::invalid_argument for settings outside their ranges, or when the items do not
// belong to this run (a group index out of range).
LaneClassification classifyLanes(const PipelineResult& run, const ReservationBook& book,
                                 const SelectionItems& items,
                                 const LaneClassifierSettings& settings);

// "knapsack", "patternPacker", ... Plain ASCII.
const char* solveMethodName(SolveMethod method);

} // namespace ob
