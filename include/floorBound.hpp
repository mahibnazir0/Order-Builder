#pragma once

#include "trailerSpec.hpp"

#include <optional>

namespace ob {

// Which bound term set the floor. None only for an empty group, where nothing binds.
enum class FloorTerm { None, Weight, StackedHeight, StackDepth };

// What one group (or any set of groups) puts on the floor. Totals, not per-line figures.
struct FloorTotals {
    double totalWeightLb = 0.0;
    double stackedInches = 0.0;
    double unitLoads = 0.0;
};

struct FloorBoundResult {
    // Each term is a fractional truck count; the floor is the largest, rounded up.
    double weightTrucks = 0.0;
    double stackedHeightTrucks = 0.0;
    // Empty when the trailer has no maximum stack depth, so the term does not exist.
    std::optional<double> stackDepthTrucks;
    FloorTerm binding = FloorTerm::None;
    double boundTrucks = 0.0;
    long long floorTrucks = 0;
    // unitLoads / stackPositions. Not a lower bound once stacking is allowed; it is printed
    // beside the floor for comparison and must never be called a floor.
    double noStackingBaselineTrucks = 0.0;
    long long noStackingBaselineRounded = 0;
};

// The lower bound on trucks for one set of totals. A truck cannot hold more weight than its
// payload, more stacked height than positions x ceiling, or (where a depth limit exists) more
// unit loads than positions x depth, whatever the stacking pattern, so every term is a valid
// bound and so is their maximum. Pure: no I/O, no logging.
// Throws std::invalid_argument naming the field for an unusable trailer (non-positive or
// non-finite payload or ceiling, non-positive positions or depth) or a negative or non-finite
// total; a bad input is never turned into a plausible-looking truck count.
FloorBoundResult floorBound(const FloorTotals& totals, const TrailerSpec& trailer);

// Rounds a fractional truck count up to whole trucks. Exposed so a caller that sums
// boundTrucks across groups (rounding at a coarser level) rounds the same way.
long long roundUpTrucks(double fractionalTrucks);

// Stable plain-ASCII name for reports.
const char* floorTermName(FloorTerm term);

} // namespace ob
