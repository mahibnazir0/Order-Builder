#pragma once
// ============================================================================
// binPacker.hpp - M4 module C7. Packs a lane of many distinct products into
// the fewest trucks by branch and price.
//
// Each item is its own row of a covering LP over truck patterns, solved by
// column generation as patternPacker does. Where the LP is fractional, the
// search branches on a pair of items the LP half-puts together (Ryan-Foster):
// one branch makes them one item, the other forbids them sharing a truck, and
// pricing honours both. A branch whose LP bound cannot beat the best packing
// found is cut. At every node the LP's patterns are rounded and the rest
// packed first-fit decreasing, which keeps the best packing current.
//
// First-fit decreasing is the warm start and the fallback, never the answer:
// the result says whether branch and price improved on it, matched it, or
// could not run.
//
// Never more trucks than reserved: trucks are kept fullest first and what the
// rest would carry is left over. Capacity is floor area and weight, rounded as
// knapsack rounds them; floorPattern confirms each truck's rows.
// ============================================================================

#include "knapsack.hpp"

#include <cstddef>
#include <vector>

namespace ob {

// No defaults: each comes from the run's settings.
struct BinPackerSettings {
    double weightGrainLb = 0.0;      // as for knapsack
    double areaGrainSqIn = 0.0;
    int columnRoundsPerNode = 0;     // column-generation rounds at one branch node
    long long pricingNodeLimit = 0;  // nodes per pricing knapsack
    long long branchNodeLimit = 0;   // branch nodes before stopping with the best found
    // The LP holds a row per item, so above this many items that fit a truck the search does
    // not start: first-fit decreasing stands and the result says it fell back.
    int maxSearchItems = 0;
};

struct BinTruck {
    std::vector<std::size_t> items;  // input indices, ascending
    double areaUsedSqIn = 0.0;
    double weightUsedLb = 0.0;
};

struct BinPacking {
    std::vector<BinTruck> trucks;          // at most the reserved count, fullest first
    std::vector<std::size_t> leftOver;     // ascending: unfittable, or over the reservation
    std::vector<std::size_t> unfittable;   // too tall, or over the floor or payload alone

    long long ffdTrucks = 0;       // the warm start
    long long bestTrucks = 0;      // the best packing found, before the reservation cap
    // The most trucks proven necessary: the volume bound, or the root LP's when it converged.
    long long lowerBound = 0;
    bool proven = false;           // bestTrucks equals lowerBound, or the tree was searched out
    bool improvedOnWarmStart = false;
    // First-fit decreasing stands: the lane was over maxSearchItems or its LP could not be solved.
    bool fellBack = false;
    long long branchNodes = 0;
    bool overReservation = false;  // the best packing needs more trucks than reserved
};

// groupIndices, when given, is parallel to items: a call holding two segregated groups is
// rejected, since their goods may never share a truck. Throws std::invalid_argument for that,
// for bad items or capacity as prepareKnapsack does, for a grain that is not positive and
// finite, a round, node or item limit below 1, or a reserved count below 0.
BinPacking packBins(const std::vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                    const BinPackerSettings& settings, int reservedTrucks,
                    const std::vector<std::size_t>& groupIndices = {});

// First-fit decreasing alone, by the larger of each item's share of the floor and of the
// payload: the warm start, exposed so branch and price can be measured against it.
BinPacking packFirstFitDecreasing(const std::vector<KnapsackItem>& items,
                                  const KnapsackCapacity& capacity,
                                  const BinPackerSettings& settings, int reservedTrucks);

} // namespace ob
