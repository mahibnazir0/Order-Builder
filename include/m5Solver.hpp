#pragma once
// ============================================================================
// m5Solver.hpp - M5's scalar cutting-stock core: bounds, first-fit decreasing,
// column generation with integer completion, an exact small solver, the FX-GAP
// escalation rule and the acceptance-adapter model.
//
// Four numbers are never confused (v7 Section 12): the LP value over the
// columns generated, a valid integer lower bound, the restricted-master
// integer optimum over those columns, and a checked incumbent. LP convergence
// proves nothing about the integer answer (X-CG-18); a proof is an incumbent
// equal to a valid bound of the same model, or an exhausted exact search.
//
// The LP and pricing are M4's own (columnGeneration: solveCoveringLp,
// PatternPricer), extended here, never copied (U-CG-08). The Farley bound
// sum(d u) / max(1, V) uses the duals only when pricing was exact.
// ============================================================================

#include "m5Types.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ob {
namespace m5 {

// Items of whole-number size; groups never share a bin; each group may have its own capacity.
struct ScalarModel {
    std::vector<long long> sizes;
    long long capacity = 0;
    std::vector<std::size_t> groupOf;          // empty: one group
    std::vector<long long> capacityOfGroup;    // empty: every group has `capacity`
};

struct ScalarPacking {
    std::vector<std::vector<std::size_t>> bins;  // item indices
};

struct ScalarLimits {
    std::size_t exactMaxItems = 30;
    long long exactNodeLimit = 2'000'000;
    int maxColumnRounds = 200;
    long long pricingNodeLimit = 200'000;
    long long simplexIterationLimit = 100'000;
    long long restrictedIntegerNodeLimit = 200'000;
};

struct ScalarResult {
    std::optional<ScalarPacking> packing;  // every packable item, or none when stopped first
    std::vector<std::size_t> impossible;   // larger than their group's capacity: reported, never packed
    long long ffdBins = 0;
    SolverEvidence evidence;
};

using SolverStop = std::function<bool()>;

// The larger of the L1 bound ceil(total / capacity) and the count of items over half the
// capacity, per group, summed (X-CG-02, X-CG-13). Impossible items are left out.
long long scalarLowerBound(const ScalarModel& model);
ScalarPacking scalarFirstFitDecreasing(const ScalarModel& model);
// Branch and bound over bins; empty when it stops at its node limit before proving.
std::optional<ScalarPacking> scalarExact(const ScalarModel& model, long long nodeLimit);
ScalarResult solveCuttingStock(const ScalarModel& model, const ScalarLimits& limits, const SolverStop& stop = SolverStop());

// FX-GAP and the production rule (Q6: F > B): escalate when F - B >= gapTrucks with allowance left.
bool shouldEscalate(long long quick, long long bound, long long gapTrucks, bool allowanceLeft);
// "2 trucks, 20.0% above bound (of 10)", or N/A with no positive bound (X-CG-17).
std::string gapText(long long result, std::optional<long long> bound);

// Pricing for one round (X-CG-05): the best pattern at these duals and its value.
struct PricedPattern {
    std::vector<int> counts;
    double value = 0.0;
    bool exact = true;
};
PricedPattern pricePattern(const std::vector<long long>& sizes, long long capacity, const std::vector<double>& duals,
                           const std::vector<long long>& available, long long nodeLimit);

// Rejects a column with a negative count, a count for an unknown type or over capacity (X-CG-16).
bool validColumn(const std::vector<int>& column, const std::vector<long long>& sizes, long long capacity);

// The acceptance-adapter model (Section 8.5, X-CG-19, X-REB-04): every nonempty bin within cube
// and weight capacity and accepted (weight > x OR cube > y, strict), demand by equality. Decided by
// exhaustive enumeration, so only for small models.
struct AcceptedModel {
    std::vector<double> cube;
    std::vector<double> weight;
    double cubeCapacity = 0.0;
    double weightCapacity = 0.0;
    double xWeight = 0.0;
    double yCube = 0.0;
    std::size_t bins = 0;  // slots available
};

struct AcceptedResult {
    ProofStatus allGoods = ProofStatus::NotAttempted;  // ProvenOptimal or ProvenInfeasible
    std::optional<ScalarPacking> allGoodsPacking;
    ScalarPacking partial;          // most cube shipped in accepted bins: partial service
    double partialCube = 0.0;
    long long capacityBound = 0;    // a bound of the capacity-only model, reported as a relaxation
};

constexpr std::size_t kAcceptedModelMaxItems = 10;
// Throws std::invalid_argument for a model past kAcceptedModelMaxItems items.
AcceptedResult solveAccepted(const AcceptedModel& model);

} // namespace m5
} // namespace ob
