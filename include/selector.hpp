#pragma once
// ============================================================================
// selector.hpp - M4 module C8. Runs each route through its method, falls back
// when a method fails, and assembles the day's trucks. It solves
// nothing itself.
//
// Work is done per lane and ship date: the groups of a lane split by
// segregation share that date's reservation, so they are solved in group
// order, each allowed only what the groups before it left. Groups never share
// a truck, however much room is left. Lane-dates are independent, so they run
// on worker threads, and results are kept by position: the plan is the same at
// any thread count.
//
// For each group:
//   1. its method, then the next one down when it throws: knapsack -> greedy
//      knapsack; patternPacker -> binPacker -> first-fit decreasing; binPacker
//      -> first-fit decreasing. A group is unsolved only when every method threw.
//      Which method is kept depends on counted node limits and errors only, never
//      on elapsed time, so a plan replays the same on any machine; a method that
//      overruns the budget is kept and the route flagged overBudget
//   2. floor: floorPattern arranges every truck; one that does not fit its rows
//      drops its least urgent goods until it does
//   3. fill: goods left over, by the method or the floor, go most urgent first
//      into room left in its trucks, kept only where the rows still fit
//   4. weight: on its trucks, heavy goods are swapped from the heaviest truck to
//      the lightest while that lowers the heavier and both still fit the floor,
//      so no truck quietly weighs out while another has room
// A truck left empty is not built: its reservation is reported unused.
//
// Every item ends on exactly one truck or in leftOver with its reason, and
// every reservation is reported with the trucks built against it.
// ============================================================================

#include "binPacker.hpp"
#include "floorPattern.hpp"
#include "knapsack.hpp"
#include "laneClassifier.hpp"
#include "patternPacker.hpp"
#include "reservationBook.hpp"
#include "selectionItem.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ob {

enum class PackingMethod { Knapsack, GreedyKnapsack, PatternPacker, BinPacker, FirstFitDecreasing };

// No defaults for the figures: each comes from the run's settings.
struct SelectorSettings {
    KnapsackSettings knapsack;
    PatternPackerSettings patternPacker;
    BinPackerSettings binPacker;
    FloorPatternSettings floor;
    double secondsPerLane = 0.0;  // a method that takes longer is kept, and its route flagged
    int threads = 0;              // at least 1
    // Refusals in a row before the fill pass stops trying a truck (bounds floor arrangements).
    int fillMissesPerTruck = 0;
    // Weight-spreading swaps tried, per truck on the route.
    int weightSpreadRoundsPerTruck = 0;
    // No truck is planned over this many unit loads: the client's own check stops a run at 64,
    // 32 positions two high, though short unit loads stack three high within the ceiling.
    double maxUnitLoadsPerTruck = 0.0;
    // Seconds on a steady clock. Empty uses std::chrono::steady_clock; tests pass their own.
    std::function<double()> clock;
};

enum class AttemptOutcome {
    Used,
    Threw,
    OverBudget,  // kept, but took longer than secondsPerLane
};

struct MethodAttempt {
    PackingMethod method = PackingMethod::Knapsack;
    AttemptOutcome outcome = AttemptOutcome::Used;
    double seconds = 0.0;
    std::string error;  // what it threw
};

enum class RouteStatus {
    Solved,
    Escalated,  // routed to M5: no method is called
    Unrouted,   // demand with no truck reserved on that date
    NoDemand,   // trucks reserved and nothing to load
    Unsolved,   // every method threw
};

struct RouteOutcome {
    std::size_t routeIndex = 0;  // into LaneClassification::routes
    RouteStatus status = RouteStatus::Solved;
    std::vector<MethodAttempt> attempts;      // in the order tried
    std::optional<PackingMethod> methodUsed;  // the one whose trucks were kept
    bool proven = false;                      // that method proved its result optimal
    bool overBudget = false;  // the kept method overran secondsPerLane; the run is incomplete
    bool lpIterationCapHit = false;  // its LP stopped at the pivot cap; the run is incomplete
    long long trucksAllowed = 0;  // what the lane-date's reservation still allowed this group
    long long trucksBuilt = 0;
    // Judged on this route's trucks: weight when they carry a larger share of their payload
    // than of their floor, otherwise floor area. None when nothing was built.
    BindingLimit binding = BindingLimit::None;
};

struct PlannedTruck {
    std::size_t routeIndex = 0;
    std::size_t groupIndex = 0;
    std::string shipDate;
    std::vector<std::size_t> items;  // into SelectionItems::items, ascending: most urgent first
    double areaSqIn = 0.0;
    double weightLb = 0.0;
    double unitLoads = 0.0;
    bool weightFull = false;  // at 100% of payload
    // The rows, indexed by position in `items`.
    FloorLayout floor;
};

enum class LeftOverReason {
    AfterWindow,          // ship date after the window (from the classifier)
    UnavailableInWindow,  // not available within the window (from the classifier)
    NoReservation,        // no truck reserved on its date
    EscalatedToM5,
    Unfittable,           // too tall, or over the floor or payload on its own
    OverReservation,      // the reserved trucks were full
    FloorEviction,        // its truck's rows did not fit with it
    UnitLoadCap,          // its truck was over maxUnitLoadsPerTruck with it
    LaneUnsolved,         // every method threw
};

struct LeftOverItem {
    std::size_t itemIndex = 0;
    LeftOverReason reason = LeftOverReason::OverReservation;
};

// One reservation of the book, and what was built against it.
struct ReservationUse {
    std::size_t reservationIndex = 0;  // into ReservationBook::reservations
    long long reserved = 0;
    long long built = 0;
    // Left to M5. Its trucks still carry nothing, so they are unused and in penalty exposure;
    // the report names them apart from trucks with nothing to load.
    bool escalated = false;
    long long unused() const { return reserved - built; }
};

struct SelectionPlan {
    std::vector<PlannedTruck> trucks;     // by route, then fullest first
    std::vector<RouteOutcome> routes;     // parallel to LaneClassification::routes
    std::vector<LeftOverItem> leftOver;   // ascending by item
    std::vector<ReservationUse> reservations;  // parallel to ReservationBook::reservations
    long long penaltyExposureTrucks = 0;  // reserved trucks left unused, escalated ones included
};

// One group's route as the selector solves it - the method chain, then the floor, fill and
// weight passes - but alone, with `chain` in place of its routed methods and `trucksAllowed`
// in place of what its lane-date has left. For comparing methods on the same route. Throws as
// selectLoads does, and for a route index out of range or an empty chain.
struct RouteSolution {
    RouteOutcome outcome;
    std::vector<PlannedTruck> trucks;
    std::vector<LeftOverItem> leftOver;
};
RouteSolution solveRoute(const PipelineResult& run, const SelectionItems& items,
                         const LaneClassification& classification, std::size_t routeIndex,
                         const std::vector<PackingMethod>& chain, long long trucksAllowed,
                         const SelectorSettings& settings);

// Throws std::invalid_argument for settings out of range (a budget that is not positive and
// finite, fewer than one thread), or a classification that does not belong to these items.
// A method's own settings are checked when it runs: a bad one makes it throw, which is
// recorded, and the next method down is tried.
SelectionPlan selectLoads(const PipelineResult& run, const ReservationBook& book,
                          const SelectionItems& items, const LaneClassification& classification,
                          const SelectorSettings& settings);

// "knapsack", "firstFitDecreasing", ... Plain ASCII.
const char* packingMethodName(PackingMethod method);
const char* leftOverReasonName(LeftOverReason reason);

} // namespace ob
