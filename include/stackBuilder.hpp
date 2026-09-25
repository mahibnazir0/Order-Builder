#pragma once

#include "bindingConstraint.hpp"
#include "joiner.hpp"
#include "paramsTypes.hpp"
#include "segregationTypes.hpp"
#include "stackTypes.hpp"

#include <cstddef>
#include <vector>

namespace ob {

// Named after Truck Builder's five methods. The definitions are Order Builder's own
// reading of each name, pending Tom's confirmation; see stackBuilder.cpp.
enum class StackMethod { Natural, Target, TallAndHeavy, BaseAndTop, TryHard };

// One stack template: line indices bottom to top, repeated once per level a line occupies.
// `quantity` is how many such stacks, in pallet-equivalents (fractional, like the pallets).
struct BuiltStack {
    std::vector<std::size_t> lineIndices;
    double quantity = 0.0;
};

struct StackSet {
    StackMethod method = StackMethod::Natural;
    std::vector<BuiltStack> stacks;
    double floorPositions = 0.0;    // sum of stack quantities; what a trailer floor must hold
    double heaviestStackLb = 0.0;
};

struct MethodOutcome {
    StackMethod method = StackMethod::Natural;
    double floorPositions = 0.0;
};

struct GroupStacking {
    StackSet best;
    std::vector<MethodOutcome> outcomes;   // every method tried, in the order tried
};

struct ExcludedLine {
    std::size_t lineIndex = 0;
    UnitLoadError error = UnitLoadError::None;
};

struct StackingResult {
    std::vector<GroupStacking> groups;     // parallel to SegregationResult::groups
    // Lines that could not become a unit load; they are left out of stacking and reported.
    std::vector<ExcludedLine> excludedLines;
    // Lines with a negative or non-finite pallet quantity. A quantity of exactly 0 is skipped quietly.
    std::size_t excludedInvalidQuantityLines = 0;
};

// Pass 2: within each group, builds stack sets with every method and keeps the one that
// uses the fewest floor positions. Ties go to the lower heaviest stack when the group is
// weight-bound, otherwise to the method tried first. Stack rules come only from canStack.
// Throws std::invalid_argument for a caller bug: a non-positive ceiling, or vectors that do
// not line up with the groups and lines.
StackingResult buildStacks(const SegregationResult& segregation,
                           const std::vector<JoinedLine>& lines,
                           const std::vector<double>& palletsPerLine,
                           const BindingResult& binding,
                           const M2Params& params,
                           const TrailerSpec& trailer);

} // namespace ob
