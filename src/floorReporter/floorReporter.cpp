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
constexpr double kCubicInchesPerCubicFoot = 1728.0;
const char* const kRule = "----------------------------------------------------------------\n";

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

// "2026-08-06" or, under wholeExtract, every ship date.
string shipDateText(const string& shipDate) {
    return shipDate.empty() ? "every ship date" : printable(shipDate);
}

string shipDatesText(const ShipDateCoverage& coverage) {
    if (coverage.shipDates.empty()) return "every ship date in the extract, as one floor";
    string text;
    for (const string& shipDate : coverage.shipDates) {
        text += (text.empty() ? "" : ", ") + printable(shipDate);
    }
    return text;
}

string shipDatesSource(const FloorRun& run) {
    switch (run.selector.rule) {
    case DemandRule::WholeExtract: return "a check on the extract, not a daily floor";
    case DemandRule::ShipDates: return "as given on the command line";
    case DemandRule::Calendar: break;
    }
    const PlanningWindow& window = *run.coverage.calendarWindow;
    return "CTL row " + count(window.ctlRowIndex)
        + (window.fromDatedRow ? ", dated the plan date: overrides the weekday row"
                               : ", the plan date's weekday");
}

string palletWeights(const M2Params& params) {
    string text;
    for (const PalletSpec& pallet : params.pallets) {
        if (!text.empty()) text += ", ";
        text += printable(pallet.palletId) + " " + measure(pallet.addedWeightLb) + " lb";
    }
    return text.empty() ? "the product row's Pallet_Weight (no pallet table given)"
                        : "the product row's Pallet_Weight, else the pallet table: " + text;
}

string roundingPointText(FloorRoundingPoint roundingPoint) {
    return roundingPoint == FloorRoundingPoint::Group
        ? "per group, before summing (floorRoundingPoint = Group; client ruling 1 Oct)"
        : "once per lane (floorRoundingPoint = Lane; not the client ruling)";
}

string trailerChoiceText(TrailerChoice trailerChoice) {
    return trailerChoice == TrailerChoice::Named
        ? "named with --trailer"
        : "largest listed on payload, height, volume and stacked floor area";
}

void printBasis(ostream& out, const FloorRun& run, const M2Params& params,
                const TrailerSpec& trailer, TrailerChoice trailerChoice,
                const string& extractLabel) {
    out << kRule << " BASIS - every figure below holds only under these\n" << kRule;
    printLabel(out, "Extract");
    out << (extractLabel.empty() ? "(not named)" : printable(extractLabel)) << "\n";
    printLabel(out, "Demand rule");
    out << describeDemandSelector(run.selector)
        << "  (a floor per ship date, DATFR_TA; client 24 and 27 Oct)\n";
    printLabel(out, "Ship dates");
    out << shipDatesText(run.coverage) << "\n";
    printLabel(out, "");
    out << "(" << shipDatesSource(run) << ")\n";
    printLabel(out, "Trailer");
    out << printable(trailer.trailerCode) << "  (" << trailerChoiceText(trailerChoice) << ")\n";
    printLabel(out, "Weight limit");
    out << measure(trailer.weightLimitLb) << " lb\n";
    printLabel(out, "Interior floor");
    out << measure(trailer.interiorLengthIn) << " x " << measure(trailer.interiorWidthIn)
        << " in  (" << grouped(trailer.interiorLengthIn * trailer.interiorWidthIn)
        << " sq in; capacity is floor area and volume, client 12 Oct)\n";
    printLabel(out, "Interior height");
    out << measure(trailer.stackHeightCeilingIn)
        << " in  (a unit load exactly at it fits; only taller is excluded)\n";
    printLabel(out, "Max stack depth");
    if (trailer.maxStackDepth) {
        out << *trailer.maxStackDepth << "\n";
    } else {
        out << "no limit configured\n";
    }
    printLabel(out, "Footprint");
    out << "layer size; on a PTL or PGM pallet, the larger of layer and pallet per side\n";
    printLabel(out, "");
    out << "(client, 15 Oct); the pallet's footprint where a product has no layer size\n";
    printLabel(out, "Pallet weight");
    out << palletWeights(params) << "\n";
    printLabel(out, "  source");
    out << (params.sourcePath.empty() ? "params not read from a file"
                                      : printable(params.sourcePath))
        << "\n";
    printLabel(out, "Deck height");
    out << (params.floorDeckHeight == DeckHeightRule::Included
                ? "added to unit-load height (floorDeckHeight = Included)"
                : "not added to unit-load height (floorDeckHeight = Excluded)")
        << "\n";
    printLabel(out, "");
    out << (params.floorDeckHeight == DeckHeightRule::Included
                ? "client ruling (12 Oct): a wood deck counts toward the 108 in ceiling\n"
                : "not the client's ruling (12 Oct), which counts a wood deck; Excluded\n");
    if (params.floorDeckHeight == DeckHeightRule::Excluded) {
        printLabel(out, "");
        out << "can only lower the floor, so it stays a valid lower bound\n";
    }
    printLabel(out, "Rounding point");
    out << roundingPointText(params.floorRoundingPoint) << "\n";
    printLabel(out, "Unit-load equivalents");
    out << "fractional (cases / Cases_Unit_Load), not physical pallets; a partial\n";
    printLabel(out, "");
    out << "pallet is scaled by its fraction in weight and height (can only lower the floor)\n\n";
}

void printSectionA(ostream& out, const FloorRun& run) {
    const ShipDateCoverage& coverage = run.coverage;
    out << kRule << " A. SHIP DATES AND DEMAND\n" << kRule;
    printLabel(out, "Lines on the run's dates");
    out << count(coverage.linesOnShipDates) << "  (each counts toward its own ship date's floor)\n";
    printLabel(out, "  before the first date");
    out << count(coverage.linesBeforeFirstShipDate)
        << "  (due to ship earlier; in no floor here)\n";
    printLabel(out, "  after the last date");
    out << count(coverage.linesAfterLastShipDate)
        << "  (future demand the planner can see; in no floor here)\n";
    printLabel(out, "  undated");
    out << count(coverage.undatedLines.size())
        << "  (DATFR_TA blank or not a date; in no floor)\n";
    printLabel(out, "Reserved, other dates");
    out << grouped(static_cast<double>(coverage.reservedTrucksOutsideShipDates))
        << " truck(s)  (placeholders for dates the run does not cover; set against no floor)\n";
    printLabel(out, "Floor, all ship dates");
    out << grouped(static_cast<double>(run.floorTrucks()))
        << " trucks  (the daily floors added up; each day is floored on its own)\n";
    printLabel(out, "Reserved, all ship dates");
    out << grouped(static_cast<double>(run.reservedTrucks())) << " trucks\n";
    printLabel(out, "Pulling demand forward");
    out << "not applied: a later stage decides what may ship early\n\n";
}

struct TermTotals {
    double weightTrucks = 0.0;
    double volumeTrucks = 0.0;
    double stackDepthTrucks = 0.0;
    size_t weightBoundGroups = 0;
    size_t volumeBoundGroups = 0;
    size_t stackDepthBoundGroups = 0;
    size_t groupsWithDemand = 0;
};

TermTotals termTotals(const FloorPlan& plan) {
    TermTotals totals;
    for (const GroupFloor& group : plan.groups) {
        if (group.linesSelected == 0) continue;
        ++totals.groupsWithDemand;
        totals.weightTrucks += group.bound.weightTrucks;
        totals.volumeTrucks += group.bound.volumeTrucks;
        if (group.bound.stackDepthTrucks) totals.stackDepthTrucks += *group.bound.stackDepthTrucks;
        switch (group.bound.binding) {
        case FloorTerm::Weight: ++totals.weightBoundGroups; break;
        case FloorTerm::Volume: ++totals.volumeBoundGroups; break;
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

void printSectionB(ostream& out, const ShipDateFloor& floor, const TrailerSpec& trailer) {
    const FloorPlan& plan = floor.plan;
    const DemandSelection& selection = floor.selection;
    const TermTotals terms = termTotals(plan);
    const string shipDate = shipDateText(plan.shipDate);
    out << kRule << " B. THE FLOOR - "
        << (plan.shipDate.empty() ? "EVERY SHIP DATE AS ONE FLOOR (wholeExtract)"
                                  : "SHIP DATE " + shipDate)
        << "\n" << kRule;
    printLabel(out, "Demand lines selected");
    out << count(selection.selectedLines) << " of " << count(selection.selected.size())
        << " in the extract  (shipping on " << shipDate << ")\n";
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
    printLabel(out, "Cargo volume");
    out << grouped(plan.totals.cargoCubicIn / kCubicInchesPerCubicFoot, 1)
        << " cu ft  (footprint x unit-load height)\n";
    printLabel(out, "  no layer size");
    out << count(plan.linesWithoutLayerSize)
        << " line(s)  (pallet footprint stood in; pass --layer-sizes)\n\n";

    out << "  Bound terms, in trucks, summed over groups. Each is a valid lower bound;\n"
           "  per group the largest one binds.\n";
    printTerm(out, "  weight", terms.weightTrucks, terms.weightBoundGroups);
    printTerm(out, "  volume", terms.volumeTrucks, terms.volumeBoundGroups);
    if (trailer.maxStackDepth) {
        printTerm(out, "  stack depth", terms.stackDepthTrucks, terms.stackDepthBoundGroups);
    } else {
        printLabel(out, "  stack depth");
        out << "not applied (no depth limit configured)\n";
    }
    printLabel(out, "Bound before rounding");
    out << grouped(plan.boundTrucks, 2) << " trucks\n";
    printLabel(out, "FLOOR");
    out << grouped(static_cast<double>(plan.floorTrucks)) << " trucks  (" << shipDate
        << ", rounded " << floorRoundingPointName(plan.roundingPoint) << " level, "
        << printable(trailer.trailerCode) << ")\n";
    printLabel(out, "");
    out << "a lower bound: checked against Truck Builder's solutions for 2-3 Sep and\n";
    printLabel(out, "");
    out << "4-6 Aug, never above the trucks used on any lane and ship date (acceptance tests)\n";
    printLabel(out, "No-stacking baseline");
    out << grouped(static_cast<double>(plan.noStackingBaselineTrucks))
        << " trucks  NOT a floor: assumes nothing stacks (pallet floor area / interior floor)\n";
    printLabel(out, "Reserved");
    out << grouped(static_cast<double>(plan.reservedTrucks)) << " trucks  (placeholders shipping on "
        << shipDate << ")\n";
    printLabel(out, "  on lanes without demand");
    out << grouped(static_cast<double>(plan.reservedTrucksWithoutDemand)) << " trucks\n";
    printLabel(out, "  lanes floored above it");
    out << count(plan.lanesFloorAboveReserved)
        << "  (the floor needs more trucks than were reserved)\n\n";

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

    out << kRule << " C. FLOOR BY LANE  (" << shipDateText(plan.shipDate)
        << ", rounded " << floorRoundingPointName(plan.roundingPoint) << " level, "
        << printable(trailer.trailerCode) << ")";
    if (shown < laneOrder.size()) {
        out << "  top " << count(shown) << " of " << count(laneOrder.size()) << " by floor";
    }
    out << "\n" << kRule;
    pad_right(out, "  Lane", 24);
    pad_left(out, "Groups", 7);
    pad_left(out, "Unit-load eq", 14);
    pad_left(out, "Cargo cu ft", 14);
    pad_right(out, "  Binds", 18);
    pad_left(out, "Floor", 15);
    pad_left(out, "Reserved", 10);
    out << "\n";
    for (size_t rank = 0; rank < shown; ++rank) {
        const LaneFloor& lane = plan.lanes[laneOrder[rank]];
        pad_right(out, "  " + laneLabel(lane), 24);
        pad_left(out, count(lane.groupIndices.size()), 7);
        pad_left(out, grouped(lane.totals.unitLoads, 1), 14);
        pad_left(out, grouped(lane.totals.cargoCubicIn / kCubicInchesPerCubicFoot, 1), 14);
        pad_right(out, "  " + laneBinding(lane, plan), 18);
        pad_left(out, laneFloorText(lane), 15);
        pad_left(out, grouped(static_cast<double>(lane.reservedTrucks)), 10);
        out << "\n";
    }
    if (shown < laneOrder.size()) {
        out << "  ... " << count(laneOrder.size() - shown) << " more lane(s)\n";
    }
    if (lanesUnderstated(plan) > 0) {
        out << "  * lane has selected lines left out of the floor; its floor is understated\n";
    }
    out << "\n";
}

} // anonymous namespace

void printFloorReport(ostream& out, const FloorRun& run, const M2Params& params,
                      const TrailerSpec& trailer, TrailerChoice trailerChoice,
                      const string& extractLabel, size_t maxLanes) {
    for (const ShipDateFloor& floor : run.floors) {
        if (floor.selection.shipDate != floor.plan.shipDate) {
            throw invalid_argument("floorReporter: a floor's selection and plan are for"
                                   " different ship dates");
        }
    }
    out << "\n================================================================\n";
    out << " ORDER BUILDER - MILESTONE 3 TRUCK FLOOR\n";
    out << "================================================================\n\n";
    printBasis(out, run, params, trailer, trailerChoice, extractLabel);
    printSectionA(out, run);
    for (const ShipDateFloor& floor : run.floors) {
        printSectionB(out, floor, trailer);
        printSectionC(out, floor.plan, trailer, maxLanes);
    }
}

} // namespace ob
