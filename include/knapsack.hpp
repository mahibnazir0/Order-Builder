#pragma once
// ============================================================================
// knapsack.hpp - M4 module C5. Exact selection of what one truck carries, and
// truck by truck for a lane whose demand exceeds its reservation.
//
// The objective is lexicographic by urgency: items are classed by rank date
// (rankDateOf: the ship date, unless priority ranks first among goods past
// due), then priority, then ship date, and a selection is better when it
// carries more unit loads of the most urgent class, then of the next, and so
// on; between selections equal on every class, the one covering more floor
// wins. So, age first, an overdue item always beats an on-time one, and
// priority 0 always beats 11 on the same date, whatever either's size, while
// the truck is still filled as far as it will go.
//
// Capacity is floor area and weight. Each item's height is checked against the
// ceiling on its own; with every item under it, cube is bounded by floor area
// and needs no constraint of its own. Floor area is a necessary condition, not
// an arrangement, so the caller may pass TruckLimits - the selector passes the
// truck's rows and its unit-load cap - and the search then chooses only loads
// they accept. A proof is then a proof of the load the truck carries, not of a
// load some later step must trim.
//
// Rows are not monotone: a load whose rows do not fit can hold a larger one
// whose rows do (a pinwheel short of one stack, or the sorted arrangement
// grouping neighbours differently; Tom Moore, 10 Nov 2026: 22 unit loads
// proven where 23 fit). So the search never cuts a branch because the rows
// refused it. It cuts only on limits that can only grow as goods are added -
// floor, payload, and the caller's ItemLimits - and asks the rows only of a
// load that would beat the best so far.
//
// Weights and areas are counted in whole grains, each item rounded UP and the
// capacity DOWN, so rounding can only reject a load that would physically fit,
// never pass one that does not (E143). Both solvers work on the same grains, so
// they can be checked against each other exactly.
//
// Between selections of equal value the first reached wins. The search takes
// each item before leaving it out, except where the rows refused the load with
// it, so callers pass items in shipsBefore order and ties go to earlier goods.
// ============================================================================

#include "selectionItem.hpp"
#include "trailerSpec.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace ob {

struct KnapsackItem {
    double areaSqIn = 0.0;   // footprint the item covers
    double weightLb = 0.0;
    double heightIn = 0.0;
    double unitLoads = 0.0;
    std::string shipDate;
    int priority = 0;        // 0 most urgent
    std::string productNumber;
    // What a truck's rows see of the item, so goods the search treats as copies of one another
    // stand on the floor alike too.
    double longSideIn = 0.0;
    double shortSideIn = 0.0;
    int levels = 1;
    // The date it ranks by on its truck (rankDateOf). Empty ranks it by its ship date.
    std::string rankDate{};
};

KnapsackItem knapsackItemFrom(const SelectionItem& item);

// Whether `left` ships before `right`: the earlier rank date, then the more urgent priority, then
// the earlier ship date, then the lower product number, so equal goods order the same on every run.
bool moreUrgent(const KnapsackItem& left, const KnapsackItem& right);

// The order to keep a lane's trucks in when it has more than its reservation, as indices into
// `trucks` (each a list of indices into `items`): the truck whose goods are most urgent first,
// compared item by item from each truck's most urgent. How full a truck is never decides it;
// trucks equally urgent keep the order given. Shared by patternPacker and binPacker so both drop
// the same way.
std::vector<std::size_t> trucksByUrgency(const std::vector<KnapsackItem>& items,
                                         const std::vector<std::vector<std::size_t>>& trucks);

struct KnapsackCapacity {
    double floorAreaSqIn = 0.0;
    double weightLb = 0.0;
    double ceilingIn = 0.0;
};

KnapsackCapacity capacityOf(const TrailerSpec& trailer);

// The most grains a capacity, or all of a problem's items together, may count, and the most
// thousandths of a unit load. Every sum the solvers form stays under it, so none overflows long
// long and each is exact in a double. The 53 ft trailer's floor is 6.3e7 grains at 0.001 sq in.
constexpr long long kMaxGrains = 1LL << 53;

// No defaults: each comes from the run's settings.
struct KnapsackSettings {
    double weightGrainLb = 0.0;  // positive, and no larger than the lightest item
    double areaGrainSqIn = 0.0;  // positive, and no larger than the smallest item
    long long nodeLimit = 0;     // branch-and-bound nodes before it stops with the best found
};

// The shared, integer form both solvers work on. `candidates` are the input items that fit an
// empty truck on their own, in solve order: class, then input order.
struct KnapsackProblem {
    std::vector<std::size_t> candidates;
    std::vector<std::size_t> classOf;        // parallel to candidates; 0 is most urgent
    std::vector<long long> amount;           // unit loads in thousandths, parallel to candidates
    std::vector<long long> areaGrains;       // parallel to candidates
    std::vector<long long> weightGrains;     // parallel to candidates
    std::size_t classCount = 0;
    long long areaCapacity = 0;
    long long weightCapacity = 0;
    std::vector<std::size_t> tooTall;        // over the ceiling: never chosen
    std::vector<std::size_t> tooLarge;       // over the floor or payload on their own
};

// Unit loads per class, most urgent first, then floor grains covered. Compared as a vector.
using KnapsackValue = std::vector<long long>;

enum class BindingLimit { None, FloorArea, Weight, Both };

struct KnapsackSelection {
    std::vector<std::size_t> chosen;   // input indices, ascending
    std::vector<std::size_t> leftOver; // every other input index, ascending
    KnapsackValue value;
    double areaUsedSqIn = 0.0;
    double weightUsedLb = 0.0;
    bool everythingFit = false;  // nothing was left: a fit, not an optimisation
    bool proven = true;          // false when the node limit stopped the search
    // Which limit stopped the smallest item left from fitting. None when nothing that fits an
    // empty truck was left.
    BindingLimit binding = BindingLimit::None;
    bool weightFull = false;     // weight at 100% of payload: the truck is full
    long long nodes = 0;
};

// Throws std::invalid_argument naming the problem for an item with a non-finite or negative
// figure, no unit loads, unit loads past kMaxGrains thousandths, a ship or rank date that is not
// a real date or a priority outside 0..kMaxPriority; a capacity that is negative or non-finite,
// or a ceiling that is not positive; settings with a grain that is not positive and finite,
// larger than the lightest item's weight or smallest item's area, or a node limit below 1; or
// a floor, payload or the candidates' total that counts past kMaxGrains grains. An item too
// large to count is too large for the truck, and set aside as such.
KnapsackProblem prepareKnapsack(const std::vector<KnapsackItem>& items,
                                const KnapsackCapacity& capacity, const KnapsackSettings& settings);

// A limit on one truck beyond floor area and weight, judged on the goods chosen for it (indices
// into the caller's items, in no particular order). It may refuse a load and accept a larger one.
using TruckCheck = std::function<bool(const std::vector<std::size_t>& chosen)>;

// A capacity each item takes a fixed share of, so a load over it stays over it with more goods:
// the unit-load cap, or a lower bound on the trailer length an item's row gives it.
struct ItemLimit {
    std::vector<double> perItem;  // parallel to the items; not negative or NaN; infinite never fits
    double capacity = 0.0;        // finite and not negative
};

// What one truck may carry beyond floor area and weight. Every load `fits` accepts must be within
// every limit: the search cuts on the limits and asks `fits` of whole loads only. Empty accepts
// every load.
struct TruckLimits {
    TruckCheck fits;
    std::vector<ItemLimit> limits;
};

// Branch and bound: the method the selector runs. Exact over the loads `truck` accepts. Throws
// std::invalid_argument, as prepareKnapsack does, and for a limit not one share per item or with
// a share or capacity out of range.
KnapsackSelection selectForTruck(const std::vector<KnapsackItem>& items,
                                 const KnapsackCapacity& capacity, const KnapsackSettings& settings,
                                 const TruckLimits& truck = TruckLimits());

// Dynamic programming over a table of floor and weight grains. Exact, but its table grows with
// the capacity in grains, so it is for cross-checking selectForTruck in tests; no run calls it.
// Throws std::invalid_argument when the table would exceed maxTableCells cells (at least 1).
KnapsackSelection selectByTable(const std::vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                                const KnapsackSettings& settings, long long maxTableCells);

// Takes items in solve order while they fit. The warm start, and the baseline the exact
// methods are measured against.
KnapsackSelection selectGreedy(const std::vector<KnapsackItem>& items,
                               const KnapsackCapacity& capacity, const KnapsackSettings& settings);

struct LaneSelection {
    std::vector<KnapsackSelection> trucks; // chosen indices are into the caller's items
    std::vector<std::size_t> leftOver;     // ascending
    bool proven = true;                    // every truck's search finished
};

// The best truck selectForTruck fills from the items at `remaining` (distinct indices into
// `items`), judged by `truck`, whose checks and shares index `items`. Its chosen and left-over
// indices are into `items`; left over holds only the remaining. Throws as selectForTruck does,
// and for an index out of range.
KnapsackSelection selectNextTruck(const std::vector<KnapsackItem>& items, const std::vector<std::size_t>& remaining,
                                  const KnapsackCapacity& capacity, const KnapsackSettings& settings,
                                  const TruckLimits& truck = TruckLimits());

// Fills `trucks` trucks one after another, each by selectNextTruck from what the trucks before it
// left, every one judged by `truck`. Exact per truck, not across them.
LaneSelection selectForTrucks(const std::vector<KnapsackItem>& items,
                              const KnapsackCapacity& capacity, const KnapsackSettings& settings,
                              int trucks, const TruckLimits& truck = TruckLimits());

const char* bindingLimitName(BindingLimit limit);

} // namespace ob
