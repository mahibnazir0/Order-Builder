#include "segregation.hpp"

#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace ob {

bool GroupKey::operator<(const GroupKey& other) const {
    // isSegregated precedes segregant so a lane's normal group sorts before its
    // segregated ones, which is the order the reporter wants.
    return std::tie(locationFrom, locationTo, shipCondition, isSegregated, segregant)
         < std::tie(other.locationFrom, other.locationTo, other.shipCondition,
                    other.isSegregated, other.segregant);
}

SegregationResult segregate(const std::vector<JoinedLine>& lines,
                            const std::vector<DoNotMixPair>& doNotMixPairs,
                            SegregationReading reading) {
    using PlannerSite = std::pair<std::string, std::string>;
    using LaneKey = std::tuple<std::string, std::string, std::string>;

    SegregationResult result;
    result.doNotMixPairsLoaded = doNotMixPairs.size();

    // A blank planner cannot name a segregated stream, so blank pairs never flag.
    std::set<PlannerSite> flaggedPlannerSites;
    for (const auto& pair : doNotMixPairs) {
        if (!pair.planner_snp.empty()) flaggedPlannerSites.emplace(pair.planner_snp, pair.locfrno);
    }

    std::set<PlannerSite> plannerSitesWithDemand;
    std::map<GroupKey, SegregatedGroup> groups;
    std::map<LaneKey, std::size_t> groupsPerLane;

    for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const STRRecord& demand = *lines[lineIndex].str;
        PlannerSite plannerSite{demand.planner_snp, demand.locfrno};
        const bool isFlagged = flaggedPlannerSites.count(plannerSite) != 0;
        const bool keepsPlannerApart = isFlagged && reading == SegregationReading::Strict;

        GroupKey key{demand.locfrno, demand.loctono, demand.ship_cond, isFlagged,
                     keepsPlannerApart ? demand.planner_snp : std::string{}};
        auto [groupEntry, isNewGroup] = groups.try_emplace(key);
        SegregatedGroup& group = groupEntry->second;
        if (isNewGroup) {
            group.key = std::move(key);
            group.splitReason = isFlagged ? SplitReason::DoNotMix : SplitReason::None;
            const std::size_t groupsInLane =
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
