#include "floorPlanner.hpp"

#include <stdexcept>
#include <unordered_map>

using namespace std;

namespace ob {

namespace {

void addTotals(FloorTotals& into, const FloorTotals& from) {
    into.totalWeightLb += from.totalWeightLb;
    into.stackedInches += from.stackedInches;
    into.unitLoads += from.unitLoads;
}

// The unit separator cannot appear in a location or ship-condition code, so distinct lanes
// never share a key.
string laneKey(const GroupKey& key) {
    return key.locationFrom + '\x1f' + key.locationTo + '\x1f' + key.shipCondition;
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
        const UnitLoadMetrics metrics =
            unitLoadMetricsFor(lines[lineIndex], params.pallets, params.floorDeckHeight);
        if (metrics.error != UnitLoadMetricsError::None) {
            plan.excludedLines.push_back({lineIndex, metrics.error, false});
            continue;
        }
        if (metrics.casesPerUnitLoadMismatch) ++plan.casesPerUnitLoadMismatchLines;
        if (exceedsCeiling(metrics, trailer)) {
            plan.excludedLines.push_back({lineIndex, UnitLoadMetricsError::None, true});
            continue;
        }
        addTotals(floor.totals, {metrics.weightLb, metrics.stackedInches, metrics.unitLoads});
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
    plan.selector = selection.selector;
    plan.roundingPoint = params.floorRoundingPoint;
    plan.groups.reserve(segregation.groups.size());

    unordered_map<string, size_t> laneIndexByKey;
    laneIndexByKey.reserve(segregation.groups.size());
    for (size_t groupIndex = 0; groupIndex < segregation.groups.size(); ++groupIndex) {
        const SegregatedGroup& group = segregation.groups[groupIndex];
        plan.groups.push_back(groupFloor(group, lines, selection, params, trailer, plan));
        const GroupFloor& floor = plan.groups.back();
        if (floor.linesSelected == 0) continue;

        const auto inserted = laneIndexByKey.emplace(laneKey(group.key), plan.lanes.size());
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

const char* floorRoundingPointName(FloorRoundingPoint roundingPoint) {
    switch (roundingPoint) {
    case FloorRoundingPoint::Group: return "group";
    case FloorRoundingPoint::Lane: return "lane";
    }
    return "unknown";
}

} // namespace ob
