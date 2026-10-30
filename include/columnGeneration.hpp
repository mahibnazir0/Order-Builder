#pragma once
// ============================================================================
// columnGeneration.hpp - the LP and pricing machinery that patternPacker and
// binPacker both solve over: a covering LP over truck patterns, and the
// bounded knapsack that prices a new pattern from its duals.
// ============================================================================

#include <cstddef>
#include <vector>

namespace ob {

// Pivot and reduced-cost tolerance for the LP. Coarser than kQuantityEpsilon because the
// tableau accumulates rounding over many pivots.
constexpr double kLpEpsilon = 1e-9;

// One kind of item a pattern can hold, in floor and weight grains.
struct PricingType {
    long long areaGrains = 0;   // positive
    long long weightGrains = 0; // zero or more
};

struct CoveringLpSolution {
    bool solved = false;
    double value = 0.0;               // trucks the relaxation needs
    std::vector<double> typeValues;   // the duals that price new patterns
    std::vector<double> patternUses;  // trucks of each pattern the relaxation uses
};

// Fewest trucks with every type covered `demand` times, over the given patterns (each a count
// per type). Solved through its dual: maximise sum(demand x value) with every pattern's value
// at most one truck. Its slack basis is feasible from the start, and the pattern uses come back
// as the duals of its rows. Dense tableau with Bland's rule, so it cannot cycle; an iteration
// cap guards the run anyway, and returns solved = false. Every type with demand must appear in
// some pattern, or the LP is unbounded and comes back unsolved.
CoveringLpSolution solveCoveringLp(const std::vector<std::vector<int>>& patterns,
                                   const std::vector<long long>& demand);

// The truck load worth most at the given values: a bounded knapsack over the types, at most
// `available` of each, within the floor and payload, and never two types named as conflicting
// both in it. Depth-first over types by value per floor grain, the rest bounded by the smaller
// of the fractional fills of the floor and the payload left.
class PatternPricer {
public:
    // conflicts, when given, lists for each type the types it may not share a truck with.
    PatternPricer(const std::vector<PricingType>& types, long long areaCapacity,
                  long long weightCapacity, long long nodeLimit,
                  const std::vector<std::vector<std::size_t>>* conflicts = nullptr);

    struct Priced {
        std::vector<int> counts;
        double value = 0.0;
        bool exact = true;  // false when the node limit stopped the search: the best found
    };

    Priced best(const std::vector<double>& values, const std::vector<long long>& available);

private:
    double fractionalFill(const std::vector<std::size_t>& byDensity, std::size_t position,
                          long long capacityLeft, bool floor) const;
    void search(std::size_t position, long long areaLeft, long long weightLeft, double value);

    const std::vector<PricingType>& types_;
    long long areaCapacity_;
    long long weightCapacity_;
    long long nodeLimit_;
    const std::vector<std::vector<std::size_t>>* conflicts_;
    const std::vector<double>* values_ = nullptr;
    const std::vector<long long>* available_ = nullptr;
    std::vector<std::size_t> order_;        // types by value per floor grain
    std::vector<std::size_t> weightOrder_;  // the same types by value per weight grain
    std::vector<std::size_t> rank_;         // each type's place in order_
    std::vector<int> counts_;
    std::vector<int> bestCounts_;
    double bestValue_ = 0.0;
    long long nodes_ = 0;
    bool exact_ = true;
};

} // namespace ob
