#include "segregation.hpp"

#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

using namespace std;

namespace ob {

bool GroupKey::operator<(const GroupKey& other) const {
    // isSegregated precedes segregant so a lane's normal group sorts before its
    // segregated ones, which is the order the reporter wants.
    return tie(locationFrom, locationTo, shipCondition, isSegregated, segregant)
         < tie(other.locationFrom, other.locationTo, other.shipCondition,
                    other.isSegregated, other.segregant);
}

SegregationResult segregate(const vector<JoinedLine>& lines,
                            const vector<DoNotMixPair>& doNotMixPairs,
                            SegregationReading reading,
                            const vector<bool>& excludedLines) {
    using PlannerSite = pair<string, string>;
    using LaneKey = tuple<string, string, string>;

    if (excludedLines.size() != lines.size()) {
        throw invalid_argument("segregate: excludedLines must be parallel to lines");
    }

    SegregationResult result;
    result.doNotMixPairsLoaded = doNotMixPairs.size();

    // A blank planner cannot name a segregated stream, so blank pairs never flag.
    set<PlannerSite> flaggedPlannerSites;
    for (const auto& pair : doNotMixPairs) {
        if (!pair.planner_snp.empty()) flaggedPlannerSites.emplace(pair.planner_snp, pair.locfrno);
    }

    set<PlannerSite> plannerSitesWithDemand;
    map<GroupKey, SegregatedGroup> groups;
    map<LaneKey, size_t> groupsPerLane;

    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        if (excludedLines[lineIndex]) {
            // Only usable demand contributes to segregation figures: a do-not-mix pair
            // whose only line is rejected correctly reports as having no demand.
            ++result.linesExcluded;
            continue;
        }
        const STRRecord& demand = *lines[lineIndex].str;
        PlannerSite plannerSite{demand.planner_snp, demand.locfrno};
        const bool isFlagged = flaggedPlannerSites.count(plannerSite) != 0;
        const bool keepsPlannerApart = isFlagged && reading == SegregationReading::Strict;

        GroupKey key{demand.locfrno, demand.loctono, demand.ship_cond, isFlagged,
                     keepsPlannerApart ? demand.planner_snp : string{}};
        auto [groupEntry, isNewGroup] = groups.try_emplace(key);
        SegregatedGroup& group = groupEntry->second;
        if (isNewGroup) {
            group.key = std::move(key);
            group.splitReason = isFlagged ? SplitReason::DoNotMix : SplitReason::None;
            const size_t groupsInLane =
                ++groupsPerLane[LaneKey{demand.locfrno, demand.loctono, demand.ship_cond}];
            if (groupsInLane == 1) ++result.lanesIn;
            if (groupsInLane == 2) ++result.lanesSplit;
        }
        group.lineIndices.push_back(lineIndex);

        ++result.linesIn;
        if (isFlagged) {
            ++result.linesSegregated;
            if (plannerSitesWithDemand.insert(std::move(plannerSite)).second) {
                ++result.doNotMixPairsWithDemand;
            }
        }
    }

    result.groups.reserve(groups.size());
    for (auto& entry : groups) result.groups.push_back(std::move(entry.second));
    return result;
}

} // namespace ob
