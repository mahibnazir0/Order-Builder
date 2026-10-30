#pragma once
// ============================================================================
// patternPacker.hpp - M4 module C6. Packs a lane of few products and many
// units into trucks by column generation over truck patterns: the client's
// "build an ideal truck and repeat it".
//
// Items that are the same size and weight (in grains) are one type. A pattern
// is how many of each type one truck carries, within its floor area and
// payload. The LP relaxation, "fewest trucks with every type covered", is
// solved over a growing set of patterns: each round, the type values (duals)
// price a new pattern by a bounded knapsack, and it joins the LP if it is
// worth more than one truck. When no pattern is, the LP bound is proven.
//
// The integer answer takes each LP pattern as many whole times as the LP uses
// it, then fills what is left truck by truck with the pattern covering the
// most floor and payload. Items are dealt into the trucks most urgent first,
// and never more trucks are kept than reserved: what the last trucks would
// carry is left over, the least urgent goods.
//
// Capacity is floor area and weight, rounded as knapsack rounds them (items up,
// capacity down); floorPattern confirms each truck's rows.
// ============================================================================

#include "knapsack.hpp"

#include <cstddef>
#include <vector>

namespace ob {

// No defaults: each comes from the run's settings.
struct PatternPackerSettings {
    double weightGrainLb = 0.0;      // as for knapsack
    double areaGrainSqIn = 0.0;
    int maxPatterns = 0;             // column-generation rounds before stopping unconverged
    long long pricingNodeLimit = 0;  // nodes per pricing knapsack
};

struct PackedTruck {
    std::vector<std::size_t> items;  // input indices, ascending
    std::size_t pattern = 0;         // into PatternPacking::patterns
    double areaUsedSqIn = 0.0;
    double weightUsedLb = 0.0;
};

struct PatternPacking {
    // The distinct loads the trucks carry: how many of each type, parallel to typeCount.
    std::vector<std::vector<int>> patterns;
    std::size_t typeCount = 0;
    std::vector<PackedTruck> trucks;     // at most the reserved count
    std::vector<std::size_t> leftOver;   // ascending: too tall, too large, or over the reservation

    // The fewest trucks the LP proves are needed, rounded up. Valid only when converged.
    long long lpTrucks = 0;
    double lpValue = 0.0;
    // A bound that always holds: total floor or weight over one truck's, rounded up.
    long long volumeBoundTrucks = 0;
    long long integerTrucks = 0;         // trucks the rounding built, before the reservation cap
    long long gapToLp = 0;               // integerTrucks - lpTrucks, when converged
    bool converged = false;              // no pattern prices above one truck
    // The trucks built equal a proven lower bound (the LP's when converged, else the volume
    // bound), so no packing needs fewer, whether or not the LP converged.
    bool provenOptimal = false;
    bool unstable = false;               // the LP or pricing could not finish: fell back
    std::size_t patternsGenerated = 0;   // columns the LP was given, starting ones included
};

// Throws std::invalid_argument as prepareKnapsack does for bad items or capacity, for settings
// with a grain that is not positive and finite, a pattern or node limit below 1, or a reserved
// count below 0.
PatternPacking packByPatterns(const std::vector<KnapsackItem>& items,
                              const KnapsackCapacity& capacity,
                              const PatternPackerSettings& settings, int reservedTrucks);

} // namespace ob
