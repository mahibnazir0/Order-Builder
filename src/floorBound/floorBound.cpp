#include "floorBound.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

using namespace std;

namespace ob {

namespace {

// Totals are sums of many per-line floats, so a group that exactly fills N trucks can land a
// hair above N. Rounding that up would add a phantom truck; shaving the tolerance first only
// ever lowers the result, so the floor stays a valid lower bound.
constexpr double kRoundingTolerance = 1e-9;

void requirePositiveFinite(double value, const char* field) {
    if (!isfinite(value) || value <= 0.0) {
        throw invalid_argument(string("floorBound: ") + field
            + " must be positive and finite (got " + to_string(value) + ")");
    }
}

void requireNonNegativeFinite(double value, const char* field) {
    if (!isfinite(value) || value < 0.0) {
        throw invalid_argument(string("floorBound: ") + field
            + " must be non-negative and finite (got " + to_string(value) + ")");
    }
}

void requireUsableTrailer(const TrailerSpec& trailer) {
    requirePositiveFinite(trailer.weightLimitLb, "trailer.weightLimitLb");
    requirePositiveFinite(trailer.stackHeightCeilingIn, "trailer.stackHeightCeilingIn");
    if (trailer.stackPositions <= 0) {
        throw invalid_argument("floorBound: trailer.stackPositions must be positive (got "
            + to_string(trailer.stackPositions) + ")");
    }
    if (trailer.maxStackDepth && *trailer.maxStackDepth <= 0) {
        throw invalid_argument("floorBound: trailer.maxStackDepth must be positive (got "
            + to_string(*trailer.maxStackDepth) + ")");
    }
}

} // anonymous namespace

long long roundUpTrucks(double fractionalTrucks) {
    requireNonNegativeFinite(fractionalTrucks, "fractional truck count");
    const double rounded = ceil(fractionalTrucks - kRoundingTolerance);
    if (rounded >= static_cast<double>(numeric_limits<long long>::max())) {
        throw invalid_argument("floorBound: truck count " + to_string(fractionalTrucks)
            + " is too large to represent");
    }
    return rounded <= 0.0 ? 0 : static_cast<long long>(rounded);
}

FloorBoundResult floorBound(const FloorTotals& totals, const TrailerSpec& trailer) {
    requireUsableTrailer(trailer);
    requireNonNegativeFinite(totals.totalWeightLb, "totalWeightLb");
    requireNonNegativeFinite(totals.stackedInches, "stackedInches");
    requireNonNegativeFinite(totals.unitLoads, "unitLoads");

    const double positions = trailer.stackPositions;
    FloorBoundResult result;
    result.weightTrucks = totals.totalWeightLb / trailer.weightLimitLb;
    result.stackedHeightTrucks = totals.stackedInches
        / (trailer.stackHeightCeilingIn * positions);
    if (trailer.maxStackDepth) {
        result.stackDepthTrucks = totals.unitLoads / (positions * *trailer.maxStackDepth);
    }
    result.noStackingBaselineTrucks = totals.unitLoads / positions;

    // Ties go to stacked height, the usual limit here; another term binds only when it is
    // strictly larger, so the reported binding term is deterministic.
    result.boundTrucks = result.stackedHeightTrucks;
    result.binding = FloorTerm::StackedHeight;
    if (result.weightTrucks > result.boundTrucks) {
        result.boundTrucks = result.weightTrucks;
        result.binding = FloorTerm::Weight;
    }
    if (result.stackDepthTrucks && *result.stackDepthTrucks > result.boundTrucks) {
        result.boundTrucks = *result.stackDepthTrucks;
        result.binding = FloorTerm::StackDepth;
    }
    if (result.boundTrucks == 0.0) result.binding = FloorTerm::None;

    // Finite totals over a tiny but positive trailer figure can still overflow.
    requireNonNegativeFinite(result.boundTrucks, "bound truck count");
    requireNonNegativeFinite(result.noStackingBaselineTrucks, "no-stacking baseline");
    result.floorTrucks = roundUpTrucks(result.boundTrucks);
    result.noStackingBaselineRounded = roundUpTrucks(result.noStackingBaselineTrucks);
    return result;
}

const char* floorTermName(FloorTerm term) {
    switch (term) {
    case FloorTerm::None: return "none";
    case FloorTerm::Weight: return "weight";
    case FloorTerm::StackedHeight: return "stacked_height";
    case FloorTerm::StackDepth: return "stack_depth";
    }
    return "unknown";
}

} // namespace ob
