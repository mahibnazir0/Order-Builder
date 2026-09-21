#pragma once

#include "paramsTypes.hpp"
#include "segregationTypes.hpp"

#include <cstddef>
#include <vector>

namespace ob {

enum class BindingConstraint { Cube, Weight };

// Pass 1 estimate for one group. The truck figures are fractional, and are only
// compared with each other; they are not a truck count.
struct GroupBinding {
    BindingConstraint binding = BindingConstraint::Cube;
    double totalPallets = 0.0;
    double totalWeightLb = 0.0;
    double trucksIfWeight = 0.0;
    double trucksIfCube = 0.0;
};

struct BindingResult {
    std::vector<GroupBinding> groups;   // parallel to SegregationResult::groups
    std::size_t cubeBoundGroups = 0;
    std::size_t weightBoundGroups = 0;
    // Lines with a negative or non-finite pallet or weight figure are left out of
    // the totals and counted here, so a bad line is visible instead of skewing a group.
    std::size_t excludedInvalidLines = 0;
};

// Decides per group whether cube or weight will limit the load, before any stack exists.
// palletsPerLine and weightPerLine are parallel to the joined lines the groups index into.
// Every line counts, stackable or not: a single-high product still takes a floor position.
// Throws std::invalid_argument for an unusable trailer spec, mismatched vectors, or a group
// index outside them; these are caller bugs, not data problems.
BindingResult assessBinding(const SegregationResult& segregation,
                            const std::vector<double>& palletsPerLine,
                            const std::vector<double>& weightPerLine,
                            const TrailerSpec& trailer);

} // namespace ob
