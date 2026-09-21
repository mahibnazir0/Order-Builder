#pragma once
// ============================================================================
// stackReporter.hpp - Prints the M2 groups and stacks so the boundaries can be
// checked without reading code.
//
// Presentation only. It adds up figures that earlier modules already computed
// and formats them; it decides nothing and calls no stacking rule.
// ============================================================================

#include "bindingConstraint.hpp"
#include "paramsTypes.hpp"
#include "segregationTypes.hpp"
#include "stackBuilder.hpp"

#include <cstddef>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace ob {

struct StackReportRow {
    std::string lane;          // "2027 -> 2500 TL"
    std::string stream;        // "normal", the planner code, or "flagged (all planners)"
    std::size_t demandLines = 0;
    double pallets = 0.0;
    double weightLb = 0.0;
    BindingConstraint binding = BindingConstraint::Cube;
    StackMethod method = StackMethod::Natural;
    double floorPositions = 0.0;
    // Pallet-equivalents riding in stacks of each height, keyed by height (1 = single-high).
    std::map<std::size_t, double> palletsByStackHeight;
};

struct StackReport {
    std::size_t groups = 0;
    std::size_t lanes = 0;
    std::size_t lanesSplit = 0;
    std::size_t linesSegregated = 0;
    std::size_t cubeBoundGroups = 0;
    std::size_t weightBoundGroups = 0;
    std::size_t groupsAllSingleHigh = 0;
    double totalPallets = 0.0;
    double totalFloorPositions = 0.0;
    std::size_t excludedLines = 0;
    std::size_t excludedInvalidQuantityLines = 0;
    std::vector<std::string> defaultedKeys;
    std::vector<std::string> paramWarnings;
    std::vector<StackReportRow> rows;   // largest floor use first
};

class StackReporter {
public:
    // The three results must describe the same groups, in the same order.
    // Throws std::invalid_argument when they do not, which is a caller bug.
    static StackReport build(const SegregationResult& segregation, const BindingResult& binding,
                             const StackingResult& stacking, const M2Params& params);

    // maxRows limits the group table; 0 prints every group.
    static void print(const StackReport& report, std::ostream& out, std::size_t maxRows = 0);
};

} // namespace ob
