#pragma once
// ============================================================================
// knapsack.hpp - M4 module C5. Exact selection of what one truck carries, and
// truck by truck for a lane whose demand exceeds its reservation.
//
// The objective is lexicographic by urgency: items are classed by ship date
// and then priority, and a selection is better when it carries more unit loads
// of the most urgent class, then of the next, and so on; between selections
// equal on every class, the one covering more floor wins. So an overdue item
// always beats an on-time one, and priority 0 always beats 11, whatever
// either's size, while the truck is still filled as far as it will go.
//
// Capacity is floor area and weight. Each item's height is checked against the
// ceiling on its own; with every item under it, cube is bounded by floor area
// and needs no constraint of its own. Floor area is a necessary condition, not
// an arrangement: floorPattern confirms the rows.
//
// Weights and areas are counted in whole grains, each item rounded UP and the
// capacity DOWN, so rounding can only reject a load that would physically fit,
// never pass one that does not (E143). Both solvers work on the same grains, so
// they can be checked against each other exactly.
//
// Between selections of equal value, the one taking the earliest items in the
// given order wins, so callers pass items in shipsBefore order.
// ============================================================================

#include "selectionItem.hpp"
#include "trailerSpec.hpp"

#include <cstddef>
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
};

KnapsackItem knapsackItemFrom(const SelectionItem& item);

struct KnapsackCapacity {
    double floorAreaSqIn = 0.0;
    double weightLb = 0.0;
    double ceilingIn = 0.0;
};

KnapsackCapacity capacityOf(const TrailerSpec& trailer);

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
// figure, no unit loads, a ship date that is not a real date or a priority outside
// 0..kMaxPriority; a capacity that is negative or non-finite, or a ceiling that is not
// positive; or settings with a grain that is not positive and finite, larger than the
// lightest item's weight or smallest item's area, or a node limit below 1.
KnapsackProblem prepareKnapsack(const std::vector<KnapsackItem>& items,
                                const KnapsackCapacity& capacity, const KnapsackSettings& settings);

// Branch and bound: the method the selector runs.
KnapsackSelection selectForTruck(const std::vector<KnapsackItem>& items,
                                 const KnapsackCapacity& capacity, const KnapsackSettings& settings);

// Dynamic programming over a table of floor and weight grains. Exact, but its table grows with
// the capacity in grains, so it is for cross-checking: it throws std::invalid_argument when
// the table would exceed kMaxKnapsackTableCells.
constexpr long long kMaxKnapsackTableCells = 4'000'000;
KnapsackSelection selectByTable(const std::vector<KnapsackItem>& items,
                                const KnapsackCapacity& capacity, const KnapsackSettings& settings);

// Takes items in solve order while they fit. The warm start, and the baseline the exact
// methods are measured against.
KnapsackSelection selectGreedy(const std::vector<KnapsackItem>& items,
                               const KnapsackCapacity& capacity, const KnapsackSettings& settings);

struct LaneSelection {
    std::vector<KnapsackSelection> trucks; // chosen indices are into the caller's items
    std::vector<std::size_t> leftOver;     // ascending
    bool proven = true;                    // every truck's search finished
};

// Fills `trucks` trucks one after another, each exactly by selectForTruck from what the trucks
// before it left. Exact per truck, not across them.
LaneSelection selectForTrucks(const std::vector<KnapsackItem>& items,
                              const KnapsackCapacity& capacity, const KnapsackSettings& settings,
                              int trucks);

const char* bindingLimitName(BindingLimit limit);

} // namespace ob
