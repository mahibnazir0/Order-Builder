#include "floorPlanner.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

using namespace std;

namespace ob {

namespace {

void addTotals(FloorTotals& into, const FloorTotals& from) {
    into.totalWeightLb += from.totalWeightLb;
    into.stackedInches += from.stackedInches;
    into.unitLoads += from.unitLoads;
    into.floorAreaSqIn += from.floorAreaSqIn;
    into.cargoCubicIn += from.cargoCubicIn;
}

// The unit separator cannot appear in a location or ship-condition code, so distinct lanes
// never share a key.
string laneKey(const string& locationFrom, const string& locationTo, const string& shipCondition) {
    return locationFrom + '\x1f' + locationTo + '\x1f' + shipCondition;
}

GroupFloor groupFloor(const SegregatedGroup& group, const vector<JoinedLine>& lines,
                      const DemandSelection& selection, const M2Params& params,
                      const TrailerSpec& trailer, FloorPlan& plan) {
    GroupFloor floor;
    for (const size_t lineIndex : group.lineIndices) {
        if (lineIndex >= lines.size()) {
            throw invalid_argument("floorPlanner: group line index out of range");
        }
        if (!selection.selected[lineIndex]) {
            ++plan.linesNotSelected;
            continue;
        }
        ++floor.linesSelected;
        const UnitLoadMetrics metrics = unitLoadMetricsFor(lines[lineIndex], params);
        if (metrics.error != UnitLoadMetricsError::None) {
            plan.excludedLines.push_back({lineIndex, metrics.error, false});
            continue;
        }
        if (metrics.casesPerUnitLoadMismatch) ++plan.casesPerUnitLoadMismatchLines;
        if (metrics.footprintFromPalletOnly) ++plan.linesWithoutLayerSize;
        if (exceedsCeiling(metrics, trailer)) {
            plan.excludedLines.push_back({lineIndex, UnitLoadMetricsError::None, true});
            continue;
        }
        addTotals(floor.totals, {metrics.weightLb, metrics.stackedInches, metrics.unitLoads,
                                 metrics.floorAreaSqIn, metrics.cargoCubicIn});
        ++floor.linesCounted;
    }
    floor.bound = floorBound(floor.totals, trailer);
    return floor;
}

} // anonymous namespace

FloorPlan planFloor(const SegregationResult& segregation, const vector<JoinedLine>& lines,
                    const DemandSelection& selection, const M2Params& params,
                    const TrailerSpec& trailer) {
    if (selection.selected.size() != lines.size()) {
        throw invalid_argument("floorPlanner: demand selection is not parallel to the lines");
    }
    FloorPlan plan;
    plan.shipDate = selection.shipDate;
    plan.roundingPoint = params.floorRoundingPoint;
    plan.groups.reserve(segregation.groups.size());

    unordered_map<string, size_t> laneIndexByKey;
    laneIndexByKey.reserve(segregation.groups.size());
    for (size_t groupIndex = 0; groupIndex < segregation.groups.size(); ++groupIndex) {
        const SegregatedGroup& group = segregation.groups[groupIndex];
        plan.groups.push_back(groupFloor(group, lines, selection, params, trailer, plan));
        const GroupFloor& floor = plan.groups.back();
        if (floor.linesSelected == 0) continue;

        const auto inserted = laneIndexByKey.emplace(
            laneKey(group.key.locationFrom, group.key.locationTo, group.key.shipCondition),
            plan.lanes.size());
        if (inserted.second) {
            LaneFloor& newLane = plan.lanes.emplace_back();
            newLane.locationFrom = group.key.locationFrom;
            newLane.locationTo = group.key.locationTo;
            newLane.shipCondition = group.key.shipCondition;
        }
        LaneFloor& lane = plan.lanes[inserted.first->second];
        lane.groupIndices.push_back(groupIndex);
        addTotals(lane.totals, floor.totals);
        lane.boundTrucks += floor.bound.boundTrucks;
        lane.noStackingBaselineTrucks += floor.bound.noStackingBaselineRounded;
        lane.linesSelected += floor.linesSelected;
        lane.linesCounted += floor.linesCounted;
        if (plan.roundingPoint == FloorRoundingPoint::Group) {
            lane.floorTrucks += floor.bound.floorTrucks;
        }
    }

    for (LaneFloor& lane : plan.lanes) {
        if (plan.roundingPoint == FloorRoundingPoint::Lane) {
            lane.floorTrucks = roundUpTrucks(lane.boundTrucks);
        }
        addTotals(plan.totals, lane.totals);
        plan.boundTrucks += lane.boundTrucks;
        plan.floorTrucks += lane.floorTrucks;
        plan.noStackingBaselineTrucks += lane.noStackingBaselineTrucks;
    }
    return plan;
}

void matchReservations(FloorPlan& plan, const vector<PlaceholderRecord>& placeholders,
                       const vector<bool>& placeholderExcluded) {
    if (placeholderExcluded.size() != placeholders.size()) {
        throw invalid_argument("floorPlanner: placeholder flags are not parallel to the"
                               " placeholders");
    }
    unordered_map<string, LaneFloor*> laneByKey;
    laneByKey.reserve(plan.lanes.size());
    for (LaneFloor& lane : plan.lanes) {
        lane.reservedTrucks = 0;
        laneByKey.emplace(laneKey(lane.locationFrom, lane.locationTo, lane.shipCondition), &lane);
    }
    plan.reservedTrucks = 0;
    plan.reservedTrucksWithoutDemand = 0;
    for (size_t placeholderIndex = 0; placeholderIndex < placeholders.size(); ++placeholderIndex) {
        const PlaceholderRecord& placeholder = placeholders[placeholderIndex];
        if (placeholderExcluded[placeholderIndex]) continue;
        if (!plan.shipDate.empty() && placeholder.datfr_ta != plan.shipDate) continue;
        plan.reservedTrucks += placeholder.no_of_loads;
        const auto lane = laneByKey.find(
            laneKey(placeholder.locfrno, placeholder.loctono, placeholder.ship_cond));
        if (lane == laneByKey.end()) {
            plan.reservedTrucksWithoutDemand += placeholder.no_of_loads;
        } else {
            lane->second->reservedTrucks += placeholder.no_of_loads;
        }
    }
    plan.lanesFloorAboveReserved = static_cast<size_t>(count_if(plan.lanes.begin(),
        plan.lanes.end(), [](const LaneFloor& lane) { return lane.floorTrucks > lane.reservedTrucks; }));
}

FloorRun planFloorRun(const DemandSelector& selector, const DemandFile& demand,
                      const SegregationResult& segregation, const vector<JoinedLine>& lines,
                      const vector<PlaceholderRecord>& placeholders,
                      const vector<bool>& placeholderExcluded, const M2Params& params,
                      const TrailerSpec& trailer) {
    FloorRun run;
    run.selector = selector;
    run.coverage = coverShipDates(selector, demand.ctl);
    countCoverage(run.coverage, demand.str, placeholders, placeholderExcluded);

    const vector<string> wholeExtract{""};
    const vector<string>& shipDates =
        run.coverage.shipDates.empty() ? wholeExtract : run.coverage.shipDates;
    run.floors.reserve(shipDates.size());
    for (const string& shipDate : shipDates) {
        ShipDateFloor& floor = run.floors.emplace_back();
        floor.selection = selectShipDate(demand.str, shipDate);
        floor.plan = planFloor(segregation, lines, floor.selection, params, trailer);
        matchReservations(floor.plan, placeholders, placeholderExcluded);
    }
    return run;
}

long long FloorRun::floorTrucks() const {
    long long trucks = 0;
    for (const ShipDateFloor& floor : floors) trucks += floor.plan.floorTrucks;
    return trucks;
}

long long FloorRun::reservedTrucks() const {
    long long trucks = 0;
    for (const ShipDateFloor& floor : floors) trucks += floor.plan.reservedTrucks;
    return trucks;
}

size_t FloorRun::selectedLines() const {
    size_t lines = 0;
    for (const ShipDateFloor& floor : floors) lines += floor.selection.selectedLines;
    return lines;
}

size_t FloorRun::linesLeftOut() const {
    size_t lines = 0;
    for (const ShipDateFloor& floor : floors) lines += floor.plan.excludedLines.size();
    return lines;
}

const char* floorRoundingPointName(FloorRoundingPoint roundingPoint) {
    switch (roundingPoint) {
    case FloorRoundingPoint::Group: return "group";
    case FloorRoundingPoint::Lane: return "lane";
    }
    return "unknown";
}

} // namespace ob
