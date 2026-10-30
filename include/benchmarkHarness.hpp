#pragma once
// ============================================================================
// benchmarkHarness.hpp - M4 module C10. Decides which methods land in M4 by
// running each of them on the same routes, and checks every plan against the
// invariants.
//
// A route is benchmarked by solving it alone with each method, exactly as the
// selector would (solveRoute: the method, then the floor, fill and weight
// passes), allowed the trucks it reserved. A method is ranked only when it ran
// and finished within the per-lane budget; among those the best ships the most
// unit loads (fullness beats truck count), then uses the fewest trucks, then
// leaves the least rear waste. Ties go to the earlier method in
// benchmarkedMethods().
//
// From many routes it derives what the classifier needs: the variety ratio
// where patternPacker and binPacker swap (that ratio is the routing rule), and
// the smallest reservation where the method a route is sent to misses the budget
// (the M5 threshold sits below it).
//
// checkInvariants recounts everything from the trucks themselves rather than
// trusting the plan's own totals.
// ============================================================================

#include "laneClassifier.hpp"
#include "reservationBook.hpp"
#include "selectionItem.hpp"
#include "selector.hpp"

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace ob {

// By reference only, so pipeline.hpp can hold M4's results without including itself.
struct PipelineResult;

// knapsack, patternPacker, binPacker, first-fit decreasing: every method a route can be
// solved with, in tie-break order.
const std::vector<PackingMethod>& benchmarkedMethods();

struct MethodScore {
    PackingMethod method = PackingMethod::Knapsack;
    bool ran = false;            // did not throw
    std::string error;           // what it threw
    double seconds = 0.0;
    bool withinBudget = false;
    bool proven = false;
    long long trucks = 0;
    double unitLoadsShipped = 0.0;
    std::size_t stacksLeft = 0;
    double averageCube = 0.0;          // share of the trailer's cube, over its trucks
    double averageRearWasteIn = 0.0;
};

struct RouteBenchmark {
    std::size_t routeIndex = 0;
    SolveMethod routedTo = SolveMethod::Unrouted;  // what the classifier chose
    long long reserved = 0;
    std::size_t products = 0;
    double unitLoads = 0.0;
    double unitLoadsPerProduct = 0.0;
    std::vector<MethodScore> scores;               // parallel to benchmarkedMethods()
    std::optional<PackingMethod> best;             // none when no method ran within budget
};

// Whether `left` ranks above `right` by the rule above.
bool scoresBetter(const MethodScore& left, const MethodScore& right);

// Every method on one route. Throws std::invalid_argument for a route index out of range, or
// for settings selectLoads would reject.
RouteBenchmark benchmarkRoute(const PipelineResult& run, const SelectionItems& items,
                              const LaneClassification& classification, std::size_t routeIndex,
                              const SelectorSettings& settings);

struct VarietyCrossover {
    double ratio = 0.0;            // patternPacker above it, binPacker at or below
    std::size_t lanesCompared = 0; // routes where the two ran within budget and one ranked higher
    std::size_t lanesMisrouted = 0;
};

// The ratio that sends the fewest compared routes to the method that ranked lower; the
// smallest such ratio on a tie. None when no route separates the two methods.
std::optional<VarietyCrossover> varietyCrossover(const std::vector<RouteBenchmark>& benchmarks);

// The smallest reservation among routes whose routed method ran over the budget; none when
// every routed method finished in time.
std::optional<long long> smallestReservationOverBudget(const std::vector<RouteBenchmark>& benchmarks);

struct InvariantViolation {
    std::string invariant;  // "I1", "I2", ...
    std::string detail;
};

// I1/I10 every item on exactly one truck or left over; I2 no truck over its floor area,
// payload, ceiling or rows; I3 every truck one group's goods; I4 no lane-date given more trucks
// than it reserved, and no truck on a date with none; I6 no truck over maxUnitLoadsPerTruck
// unit loads; I12 every reservation reported, its trucks recounted from the trucks themselves.
std::vector<InvariantViolation> checkInvariants(const PipelineResult& run, const ReservationBook& book,
                                                const SelectionItems& items, const SelectionPlan& plan,
                                                double maxUnitLoadsPerTruck);

// I7: the same trucks with the same goods, the same goods left over for the same reasons, and
// the same reservations used.
bool samePlan(const SelectionPlan& left, const SelectionPlan& right);

// One row per route: its lane, reservation and variety, each method's trucks, unit loads
// shipped, cube and time, and the best. Plain ASCII.
void printBenchmark(std::ostream& out, const PipelineResult& run, const LaneClassification& classification,
                    const std::vector<RouteBenchmark>& benchmarks);

} // namespace ob
