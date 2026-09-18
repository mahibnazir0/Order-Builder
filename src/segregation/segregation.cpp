#include "segregation.hpp"

#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace ob {

bool GroupKey::operator<(const GroupKey& other) const {
    // isSegregated precedes segregant so a lane's unflagged group sorts before its
    // segregated ones, which is the order the reporter wants.
    return std::tie(locationFrom, locationTo, shipCondition, isSegregated, segregant)
         < std::tie(other.locationFrom, other.locationTo, other.shipCondition,
                    other.isSegregated, other.segregant);
}

SegregationResult segregate(const std::vector<JoinedLine>& lines,
                            const std::vector<DoNotMixPair>& doNotMixPairs,
                            SegregationReading reading) {
    SegregationResult result;
    result.doNotMixPairsLoaded = doNotMixPairs.size();
    std::set<std::pair<std::string, std::string>> flaggedPlannerSites;
    for (const auto& pair : doNotMixPairs) {
        if (!pair.planner_snp.empty()) {
            flaggedPlannerSites.emplace(pair.planner_snp, pair.locfrno);
        }
    }

    std::set<std::pair<std::string, std::string>> plannerSitesWithDemand;
    std::map<GroupKey, SegregatedGroup> groups;
    std::map<std::tuple<std::string, std::string, std::string>, std::size_t> laneGroupCounts;
    for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const auto& line = *lines[lineIndex].str;
        const auto plannerSite = std::make_pair(line.planner_snp, line.locfrno);
        const bool flagged = flaggedPlannerSites.count(plannerSite) != 0;
        const bool keepsPlannerCode = flagged && reading == SegregationReading::Strict;
        GroupKey key{line.locfrno, line.loctono, line.ship_cond, flagged,
                     keepsPlannerCode ? line.planner_snp : std::string{}};
        auto insertion = groups.try_emplace(key);
        auto& group = insertion.first->second;
        if (insertion.second) {
            group.key = std::move(key);
            group.splitReason = flagged ? SplitReason::DoNotMix : SplitReason::None;
            auto& groupCount = laneGroupCounts[
                std::make_tuple(line.locfrno, line.loctono, line.ship_cond)];
            ++groupCount;
            if (groupCount == 1) ++result.lanesIn;
            if (groupCount == 2) ++result.lanesSplit;
        }
        group.lineIndices.push_back(lineIndex);
        ++result.linesIn;
        if (flagged) {
            ++result.linesSegregated;
            if (plannerSitesWithDemand.insert(plannerSite).second) {
                ++result.doNotMixPairsWithDemand;
            }
        }
    }

    result.groups.reserve(groups.size());
    for (auto& entry : groups) result.groups.push_back(std::move(entry.second));
    return result;
}

} // namespace ob
