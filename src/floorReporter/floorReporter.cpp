#include "floorReporter.hpp"

#include "reportFormat.hpp"

#include <algorithm>
#include <cmath>
#include <ostream>
#include <stdexcept>
#include <vector>

using namespace std;

namespace ob {

namespace {

constexpr size_t kExcludedLinesListed = 20;
const char* const kRule = "----------------------------------------------------------------\n";

string printable(const string& text) {
    string result = text;
    for (char& character : result) {
        const auto code = static_cast<unsigned char>(character);
        if (code < 0x20 || code > 0x7e) character = '?';
    }
    return result;
}

string count(size_t value) {
    return grouped(static_cast<double>(value));
}

// Whole figures print without decimals, so "108 in" is not shown as "108.00 in".
string measure(double value) {
    return grouped(value, value == floor(value) ? 0 : 2);
}

void printLabel(ostream& out, const string& label) {
    out << "  ";
    pad_right(out, label, 26);
}

bool sameSelector(const DemandSelector& first, const DemandSelector& second) {
    return first.rule == second.rule && first.fromDate == second.fromDate
        && first.toDate == second.toDate;
}

string palletWeights(const M2Params& params) {
    string text;
    for (const PalletSpec& pallet : params.pallets) {
        if (!text.empty()) text += ", ";
        text += printable(pallet.palletId) + " " + measure(pallet.addedWeightLb) + " lb";
    }
    return text.empty() ? "none listed" : text;
}

string roundingPointText(FloorRoundingPoint roundingPoint) {
    return roundingPoint == FloorRoundingPoint::Group
        ? "per group, before summing (floorRoundingPoint = Group; client ruling 1 Oct)"
        : "once per lane (floorRoundingPoint = Lane; not the client ruling)";
}

string trailerChoiceText(TrailerChoice trailerChoice) {
    return trailerChoice == TrailerChoice::Named
        ? "named with --trailer"
        : "largest listed on payload, height, positions and depth";
}

void printBasis(ostream& out, const FloorPlan& plan, const M2Params& params,
                const TrailerSpec& trailer, TrailerChoice trailerChoice,
                const string& extractLabel) {
    out << kRule << " BASIS - every figure below holds only under these\n" << kRule;
    printLabel(out, "Extract");
    out << (extractLabel.empty() ? "(not named)" : printable(extractLabel)) << "\n";
    printLabel(out, "Demand rule");
    out << describeDemandSelector(plan.selector) << "  (open: question 1; no default)\n";
    printLabel(out, "Trailer");
    out << printable(trailer.trailerCode) << "  (" << trailerChoiceText(trailerChoice) << ")\n";
    printLabel(out, "Weight limit");
    out << measure(trailer.weightLimitLb) << " lb\n";
    printLabel(out, "Stack positions");
    out << trailer.stackPositions << "  (open: question 7)\n";
    printLabel(out, "Interior height");
    out << measure(trailer.stackHeightCeilingIn)
        << " in  (a unit load exactly at it fits; only taller is excluded)\n";
    printLabel(out, "Max stack depth");
    if (trailer.maxStackDepth) {
        out << *trailer.maxStackDepth << "\n";
    } else {
        out << "no limit configured\n";
    }
    printLabel(out, "Pallet weight");
    out << palletWeights(params) << "  (open: question 5)\n";
    printLabel(out, "  source");
    out << (params.sourcePath.empty() ? "params not read from a file"
                                      : printable(params.sourcePath))
        << "\n";
    printLabel(out, "Deck height");
    out << (params.floorDeckHeight == DeckHeightRule::Included
                ? "added to unit-load height (floorDeckHeight = Included)"
                : "not added to unit-load height (floorDeckHeight = Excluded)")
        << "  (open: questions 6 and 21)\n";
    printLabel(out, "Rounding point");
    out << roundingPointText(plan.roundingPoint) << "\n";
    printLabel(out, "Unit-load equivalents");
    out << "fractional (cases / Cases_Unit_Load), not physical pallets; a partial\n";
    printLabel(out, "");
    out << "pallet is scaled by its fraction in weight and height (can only lower the floor)\n\n";
}

struct TermTotals {
    double weightTrucks = 0.0;
    double stackedHeightTrucks = 0.0;
    double stackDepthTrucks = 0.0;
    size_t weightBoundGroups = 0;
    size_t stackedHeightBoundGroups = 0;
    size_t stackDepthBoundGroups = 0;
    size_t groupsWithDemand = 0;
};

TermTotals termTotals(const FloorPlan& plan) {
    TermTotals totals;
    for (const GroupFloor& group : plan.groups) {
        if (group.linesSelected == 0) continue;
        ++totals.groupsWithDemand;
        totals.weightTrucks += group.bound.weightTrucks;
        totals.stackedHeightTrucks += group.bound.stackedHeightTrucks;
        if (group.bound.stackDepthTrucks) totals.stackDepthTrucks += *group.bound.stackDepthTrucks;
        switch (group.bound.binding) {
        case FloorTerm::Weight: ++totals.weightBoundGroups; break;
        case FloorTerm::StackedHeight: ++totals.stackedHeightBoundGroups; break;
        case FloorTerm::StackDepth: ++totals.stackDepthBoundGroups; break;
        case FloorTerm::None: break;
        }
    }
    return totals;
}

void printTerm(ostream& out, const string& label, double trucks, size_t boundGroups) {
    printLabel(out, label);
    out << grouped(trucks, 2) << " trucks; binds on " << count(boundGroups) << " group(s)\n";
}

string excludedReason(const FloorExcludedLine& line) {
    return line.overCeiling ? "unit load taller than the trailer ceiling"
                            : unitLoadMetricsErrorName(line.error);
}

size_t lanesNotMeasurable(const FloorPlan& plan) {
    return static_cast<size_t>(count_if(plan.lanes.begin(), plan.lanes.end(),
        [](const LaneFloor& lane) { return !lane.isMeasurable(); }));
}

size_t lanesUnderstated(const FloorPlan& plan) {
    return static_cast<size_t>(count_if(plan.lanes.begin(), plan.lanes.end(),
        [](const LaneFloor& lane) { return lane.isMeasurable() && lane.isUnderstated(); }));
}

void printSectionB(ostream& out, const FloorPlan& plan, const DemandSelection& selection,
                   const TrailerSpec& trailer) {
    const TermTotals terms = termTotals(plan);
    const string ruleText = describeDemandSelector(plan.selector);
    out << kRule << " B. THE FLOOR\n" << kRule;
    printLabel(out, "Demand lines selected");
    out << count(selection.selectedLines) << " of " << count(selection.selected.size())
        << " in the extract  (" << ruleText << ")\n";
    printLabel(out, "  overdue");
    out << count(selection.overdueLines)
        << "  (selected, latest arrival before the planning date)\n";
    printLabel(out, "  undated");
    out << count(selection.undatedLines.size())
        << "  (date blank or malformed; cannot be judged, not selected)\n";
    printLabel(out, "  grouped, not selected");
    out << count(plan.linesNotSelected) << "\n";
    printLabel(out, "Lanes");
    out << count(plan.lanes.size()) << "  (with demand under the rule)\n";
    printLabel(out, "  not measurable");
    out << count(lanesNotMeasurable(plan))
        << "  (every selected line left out; no floor, not a floor of 0)\n";
    printLabel(out, "  understated");
    out << count(lanesUnderstated(plan)) << "  (some selected lines left out; marked * below)\n";
    printLabel(out, "Groups");
    out << count(terms.groupsWithDemand) << "  (segregated; groups never share a truck)\n";
    printLabel(out, "Unit-load equivalents");
    out << grouped(plan.totals.unitLoads, 1) << "  (fractional; see basis)\n";
    printLabel(out, "Total weight");
    out << grouped(plan.totals.totalWeightLb) << " lb\n";
    printLabel(out, "Stacked height");
    out << grouped(plan.totals.stackedInches, 1) << " in\n\n";

    out << "  Bound terms, in trucks, summed over groups. Each is a valid lower bound;\n"
           "  per group the largest one binds.\n";
    printTerm(out, "  weight", terms.weightTrucks, terms.weightBoundGroups);
    printTerm(out, "  stacked height", terms.stackedHeightTrucks, terms.stackedHeightBoundGroups);
    if (trailer.maxStackDepth) {
        printTerm(out, "  stack depth", terms.stackDepthTrucks, terms.stackDepthBoundGroups);
    } else {
        printLabel(out, "  stack depth");
        out << "not applied (no depth limit configured)\n";
    }
    printLabel(out, "Bound before rounding");
    out << grouped(plan.boundTrucks, 2) << " trucks\n";
    printLabel(out, "FLOOR");
    out << grouped(static_cast<double>(plan.floorTrucks)) << " trucks  (" << ruleText
        << ", rounded " << floorRoundingPointName(plan.roundingPoint) << " level, "
        << printable(trailer.trailerCode) << ")\n";
    printLabel(out, "");
    out << "NOT YET VALIDATED: exceeds Truck Builder's achieved loads on some lane-days\n";
    printLabel(out, "");
    out << "(stack positions is not yet an upper bound for every footprint; open: question 7)\n";
    printLabel(out, "No-stacking baseline");
    out << grouped(static_cast<double>(plan.noStackingBaselineTrucks))
        << " trucks  NOT a floor: assumes nothing stacks (unit loads / stack positions)\n\n";

    printLabel(out, "Lines left out of floor");
    out << count(plan.excludedLines.size());
    if (plan.excludedLines.empty()) {
        out << "\n";
    } else {
        out << "  (removing demand only lowers the bound, so the floor stays valid)\n";
        const size_t listed = min(plan.excludedLines.size(), kExcludedLinesListed);
        for (size_t index = 0; index < listed; ++index) {
            out << "      line " << plan.excludedLines[index].lineIndex << ": "
                << excludedReason(plan.excludedLines[index]) << "\n";
        }
        if (listed < plan.excludedLines.size()) {
            out << "      ... and " << count(plan.excludedLines.size() - listed) << " more\n";
        }
    }
    printLabel(out, "Cases/unit-load mismatch");
    out << count(plan.casesPerUnitLoadMismatchLines)
        << " line(s)  (unit loads and weight follow Cases_Unit_Load;"
           " height follows Layers_Unit_Load)\n\n";
}

string laneFloorText(const LaneFloor& lane) {
    if (!lane.isMeasurable()) return "not measurable";
    const string floorText = grouped(static_cast<double>(lane.floorTrucks));
    return lane.isUnderstated() ? floorText + "*" : floorText;
}

string laneLabel(const LaneFloor& lane) {
    return printable(lane.locationFrom) + " -> " + printable(lane.locationTo) + " "
        + printable(lane.shipCondition);
}

// A lane's groups can bind on different terms; "mixed" says so rather than picking one.
string laneBinding(const LaneFloor& lane, const FloorPlan& plan) {
    FloorTerm binding = FloorTerm::None;
    for (const size_t groupIndex : lane.groupIndices) {
        const FloorTerm groupBinding = plan.groups[groupIndex].bound.binding;
        if (groupBinding == FloorTerm::None) continue;
        if (binding != FloorTerm::None && binding != groupBinding) return "mixed";
        binding = groupBinding;
    }
    return floorTermName(binding);
}

void printSectionC(ostream& out, const FloorPlan& plan, const TrailerSpec& trailer,
                   size_t maxLanes) {
    vector<size_t> laneOrder(plan.lanes.size());
    for (size_t laneIndex = 0; laneIndex < laneOrder.size(); ++laneIndex) {
        laneOrder[laneIndex] = laneIndex;
    }
    stable_sort(laneOrder.begin(), laneOrder.end(), [&plan](size_t first, size_t second) {
        return plan.lanes[first].floorTrucks > plan.lanes[second].floorTrucks;
    });
    const size_t shown =
        maxLanes > 0 && maxLanes < laneOrder.size() ? maxLanes : laneOrder.size();

    out << kRule << " C. FLOOR BY LANE  (" << describeDemandSelector(plan.selector)
        << ", rounded " << floorRoundingPointName(plan.roundingPoint) << " level, "
        << printable(trailer.trailerCode) << ")";
    if (shown < laneOrder.size()) {
        out << "  top " << count(shown) << " of " << count(laneOrder.size()) << " by floor";
    }
    out << "\n" << kRule;
    pad_right(out, "  Lane", 24);
    pad_left(out, "Groups", 7);
    pad_left(out, "Unit-load eq", 14);
    pad_left(out, "Stacked in", 14);
    pad_right(out, "  Binds", 18);
    pad_left(out, "Floor", 15);
    out << "\n";
    for (size_t rank = 0; rank < shown; ++rank) {
        const LaneFloor& lane = plan.lanes[laneOrder[rank]];
        pad_right(out, "  " + laneLabel(lane), 24);
        pad_left(out, count(lane.groupIndices.size()), 7);
        pad_left(out, grouped(lane.totals.unitLoads, 1), 14);
        pad_left(out, grouped(lane.totals.stackedInches, 1), 14);
        pad_right(out, "  " + laneBinding(lane, plan), 18);
        pad_left(out, laneFloorText(lane), 15);
        out << "\n";
    }
    if (shown < laneOrder.size()) {
        out << "  ... " << count(laneOrder.size() - shown) << " more lane(s)\n";
    }
    if (lanesUnderstated(plan) > 0) {
        out << "  * lane has selected lines left out of the floor; its floor is understated\n";
    }
}

} // anonymous namespace

void printFloorReport(ostream& out, const FloorPlan& plan, const DemandSelection& selection,
                      const M2Params& params, const TrailerSpec& trailer,
                      TrailerChoice trailerChoice, const string& extractLabel, size_t maxLanes) {
    if (!sameSelector(plan.selector, selection.selector)) {
        throw invalid_argument(
            "floorReporter: the selection does not carry the plan's demand rule");
    }
    out << "\n================================================================\n";
    out << " ORDER BUILDER - MILESTONE 3 TRUCK FLOOR\n";
    out << "================================================================\n\n";
    printBasis(out, plan, params, trailer, trailerChoice, extractLabel);
    printSectionB(out, plan, selection, trailer);
    printSectionC(out, plan, trailer, maxLanes);
}

} // namespace ob
