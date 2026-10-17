#pragma once

namespace ob {

// The one tolerance for comparing computed doubles (quantities, heights, truck ratios) with a
// limit. Values come from sums and products of decimal inputs, so a figure that is exactly on
// a limit can land a hair either side of it; anything within this distance counts as on it.
constexpr double kQuantityEpsilon = 1e-9;

} // namespace ob
