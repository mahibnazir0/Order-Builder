#pragma once

#include "trailerSpec.hpp"

#include <optional>

namespace ob {

// Which bound term set the floor. None only for an empty group, where nothing binds.
enum class FloorTerm { None, Weight, Volume, StackDepth };

// What one group (or any set of groups) puts on the floor. Totals, not per-line figures.
struct FloorTotals {
    double totalWeightLb = 0.0;
    double stackedInches = 0.0;   // reported only; no term divides by it
    double unitLoads = 0.0;
    double floorAreaSqIn = 0.0;   // pallet footprint x unit loads
    double cargoCubicIn = 0.0;    // pallet footprint x unit-load height x unit loads
};

struct FloorBoundResult {
    // Each term is a fractional truck count; the floor is the largest, rounded up.
    double weightTrucks = 0.0;
    double volumeTrucks = 0.0;
    // Empty when the trailer has no maximum stack depth, so the term does not exist.
    std::optional<double> stackDepthTrucks;
    FloorTerm binding = FloorTerm::None;
    double boundTrucks = 0.0;
    long long floorTrucks = 0;
    // floorAreaSqIn / interior floor area. Not a lower bound once stacking is allowed; it is
    // printed beside the floor for comparison and must never be called a floor.
    double noStackingBaselineTrucks = 0.0;
    long long noStackingBaselineRounded = 0;
};

// The lower bound on trucks for one set of totals. Capacity is the trailer's floor area and
// volume, not a count of pallet positions (client, 12 Oct): positions vary with footprint, so
// a fixed count is not an upper bound. A truck cannot hold more weight than its payload or more
// cargo volume (footprint x unit-load height) than length x width x ceiling, whatever the
// stacking pattern. Where a depth limit exists, a column of at most depth unit loads covers at
// least each member's footprint, so the floor area needed is at least footprint area / depth.
// Every term is a valid bound and so is their maximum. Pure: no I/O, no logging.
// Throws std::invalid_argument naming the field for an unusable trailer (non-positive or
// non-finite payload, length, width or ceiling, non-positive depth) or a negative or
// non-finite total; a bad input is never turned into a plausible-looking truck count.
FloorBoundResult floorBound(const FloorTotals& totals, const TrailerSpec& trailer);

// Rounds a fractional truck count up to whole trucks. Exposed so a caller that sums
// boundTrucks across groups (rounding at a coarser level) rounds the same way.
long long roundUpTrucks(double fractionalTrucks);

// Stable plain-ASCII name for reports.
const char* floorTermName(FloorTerm term);

} // namespace ob
