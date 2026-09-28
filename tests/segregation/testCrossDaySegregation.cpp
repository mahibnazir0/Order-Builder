#include "doctest.h"
#include "segregation.hpp"
#include "../importer/crossDayFixtures.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace ob;
using namespace crossDayTests;

namespace {

using PlannerSite = std::pair<std::string, std::string>;

struct ActivePair {
    PlannerSite plannerAtSite;
    PerDay<std::size_t> flaggedLines;
};

// The only 4 of the 22 do-not-mix pairs with demand on any of the four days.
const std::array<ActivePair, 4> activePairs{{
    {{"S45", "2028"}, {1680, 1409, 1388, 1397}},
    {{"S01", "2028"}, {902, 649, 649, 666}},
    {{"S03", "2028"}, {142, 140, 154, 138}},
    {{"S20", "2027"}, {125, 91, 92, 78}},
}};

std::set<PlannerSite> doNotMixPairs(const DemandFile& demand) {
    std::set<PlannerSite> pairs;
    for (const auto& pair : demand.dnm) pairs.emplace(pair.planner_snp, pair.locfrno);
    return pairs;
}

std::map<PlannerSite, std::size_t> flaggedLinesByPair(const PipelineResult& run) {
    const auto pairs = doNotMixPairs(run.demand);
    std::map<PlannerSite, std::size_t> counts;
    for (const auto& joined : run.join.lines) {
        const PlannerSite plannerAtSite{joined.str->planner_snp, joined.str->locfrno};
        if (pairs.count(plannerAtSite) != 0) ++counts[plannerAtSite];
    }
    return counts;
}

void checkDay(std::size_t dayIndex, SegregationReading reading) {
    const bool strict = reading == SegregationReading::Strict;
    const auto& run = pipelineRuns()[dayIndex];
    const auto result = segregate(run.join.lines, run.demand.dnm, reading,
                                  std::vector<bool>(run.join.lines.size(), false));
    const auto pairs = doNotMixPairs(run.demand);
    CAPTURE(dayFiles()[dayIndex].label);
    CAPTURE(strict);

    CHECK(result.linesIn == expectedM1::demandLines[dayIndex]);
    CHECK(result.lanesIn == expectedM2::lanesWithDemand[dayIndex]);
    CHECK(result.doNotMixPairsLoaded == 22);
    CHECK(result.linesSegregated == expectedM2::linesSegregated[dayIndex]);
    CHECK(result.groups.size() == (strict ? expectedM2::strictGroups[dayIndex]
                                          : expectedM2::flaggedVsNormalGroups[dayIndex]));
    CHECK(result.lanesSplit == (strict ? expectedM2::strictLanesSplit
                                       : expectedM2::flaggedVsNormalLanesSplit[dayIndex]));

    std::vector<bool> seen(run.join.lines.size(), false);
    std::set<std::string> planners;
    std::size_t lineTotal = 0;
    std::size_t largest = 0;
    std::size_t emptyGroups = 0;
    std::size_t invalidIndices = 0;
    std::size_t repeatedIndices = 0;
    std::size_t mixedPlannerGroups = 0;
    std::size_t incorrectlyKeyedLines = 0;
    std::size_t sameSiteGroups = 0;
    for (const auto& group : result.groups) {
        lineTotal += group.lineIndices.size();
        largest = std::max(largest, group.lineIndices.size());
        if (group.lineIndices.empty()) ++emptyGroups;
        if (group.sameSiteFlag) ++sameSiteGroups;
        std::set<std::string> flaggedPlannersInGroup;
        for (const auto index : group.lineIndices) {
            if (index >= seen.size()) { ++invalidIndices; continue; }
            if (seen[index]) ++repeatedIndices;
            seen[index] = true;
            const auto& line = *run.join.lines[index].str;
            planners.insert(line.planner_snp);
            const bool flagged = pairs.count({line.planner_snp, line.locfrno}) != 0;
            if (flagged) flaggedPlannersInGroup.insert(line.planner_snp);
            const std::string expectedSegregant = flagged && strict ? line.planner_snp : std::string{};
            if (group.key.locationFrom != line.locfrno || group.key.locationTo != line.loctono
                || group.key.shipCondition != line.ship_cond || group.key.isSegregated != flagged
                || group.key.segregant != expectedSegregant
                || group.splitReason != (flagged ? SplitReason::DoNotMix : SplitReason::None)) {
                ++incorrectlyKeyedLines;
            }
        }
        if (strict && flaggedPlannersInGroup.size() > 1) ++mixedPlannerGroups;
    }
    CHECK(lineTotal == expectedM1::demandLines[dayIndex]);
    CHECK(emptyGroups == 0);
    CHECK(invalidIndices == 0);
    CHECK(repeatedIndices == 0);
    CHECK(std::count(seen.begin(), seen.end(), false) == 0);
    CHECK(mixedPlannerGroups == 0);
    CHECK(incorrectlyKeyedLines == 0);
    CHECK(sameSiteGroups == 0);
    CHECK(planners.size() == expectedRecovered::distinctPlanners[dayIndex]);
    CHECK(largest == (strict ? expectedRecovered::strictLargestGroup[dayIndex]
                             : expectedM2::flaggedVsNormalLargestGroup[dayIndex]));

    std::cout << "Cross-day " << dayFiles()[dayIndex].label
              << (strict ? " Strict" : " FlaggedVsNormal") << ": lines=" << result.linesIn
              << " lanes=" << result.lanesIn << " planners=" << planners.size()
              << " pairs=" << result.doNotMixPairsWithDemand << '/' << result.doNotMixPairsLoaded
              << " segregated=" << result.linesSegregated << " groups=" << result.groups.size()
              << " split=" << result.lanesSplit << " largest=" << largest << '\n';
}

} // namespace

TEST_CASE("segregation: cross-day 17 Aug Strict" * doctest::skip(!crossDayTests::allExtractsPresent())) { checkDay(0, SegregationReading::Strict); }
TEST_CASE("segregation: cross-day 17 Aug FlaggedVsNormal" * doctest::skip(!crossDayTests::allExtractsPresent())) { checkDay(0, SegregationReading::FlaggedVsNormal); }
TEST_CASE("segregation: cross-day 02 Sep first Strict" * doctest::skip(!crossDayTests::allExtractsPresent())) { checkDay(1, SegregationReading::Strict); }
TEST_CASE("segregation: cross-day 02 Sep first FlaggedVsNormal" * doctest::skip(!crossDayTests::allExtractsPresent())) { checkDay(1, SegregationReading::FlaggedVsNormal); }
TEST_CASE("segregation: cross-day 02 Sep second Strict" * doctest::skip(!crossDayTests::allExtractsPresent())) { checkDay(2, SegregationReading::Strict); }
TEST_CASE("segregation: cross-day 02 Sep second FlaggedVsNormal" * doctest::skip(!crossDayTests::allExtractsPresent())) { checkDay(2, SegregationReading::FlaggedVsNormal); }
TEST_CASE("segregation: cross-day 03 Sep Strict" * doctest::skip(!crossDayTests::allExtractsPresent())) { checkDay(3, SegregationReading::Strict); }
TEST_CASE("segregation: cross-day 03 Sep FlaggedVsNormal" * doctest::skip(!crossDayTests::allExtractsPresent())) { checkDay(3, SegregationReading::FlaggedVsNormal); }

// Why the synthetic segregation fixtures exist: 18 of the 22 pairs never see demand.
TEST_CASE("segregation: cross-day the same 4 of 22 do-not-mix pairs have demand every day" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    std::set<PlannerSite> expected;
    for (const auto& active : activePairs) expected.insert(active.plannerAtSite);
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const auto& run = pipelineRuns()[dayIndex];
        std::set<PlannerSite> pairsWithDemand;
        for (const auto& entry : flaggedLinesByPair(run)) pairsWithDemand.insert(entry.first);
        CHECK(run.demand.dnm.size() == 22);
        CHECK(doNotMixPairs(run.demand).size() == 22);
        CHECK(pairsWithDemand == expected);
        CHECK(run.segregation.doNotMixPairsWithDemand == 4);
    }
}

TEST_CASE("segregation: cross-day flagged lines per pair sum to each day's segregated total" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const auto& run = pipelineRuns()[dayIndex];
        const auto counts = flaggedLinesByPair(run);
        std::map<PlannerSite, std::size_t> expected;
        std::size_t expectedSum = 0;
        for (const auto& active : activePairs) {
            expected.emplace(active.plannerAtSite, active.flaggedLines[dayIndex]);
            expectedSum += active.flaggedLines[dayIndex];
        }
        CHECK(counts == expected);
        CHECK(expectedSum == expectedM2::linesSegregated[dayIndex]);
        CHECK(run.segregation.linesSegregated == expectedSum);
        std::cout << "Cross-day " << dayFiles()[dayIndex].label << " pairs:";
        for (const auto& entry : counts) {
            std::cout << ' ' << entry.first.first << '+' << entry.first.second << '=' << entry.second;
        }
        std::cout << '\n';
    }
}

// Why Strict versus FlaggedVsNormal is a real question: at 2028 there is no normal stock
// for segregated product to be kept apart from, only other flagged planners.
TEST_CASE("segregation: cross-day site 2028 has no unsegregated demand on any day" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const auto& run = pipelineRuns()[dayIndex];
        const auto result = segregate(run.join.lines, run.demand.dnm, SegregationReading::Strict,
                                      std::vector<bool>(run.join.lines.size(), false));
        std::size_t siteLines = 0;
        std::size_t unsegregatedSiteLines = 0;
        for (const auto& group : result.groups) {
            if (group.key.locationFrom != "2028") continue;
            siteLines += group.lineIndices.size();
            if (!group.key.isSegregated) unsegregatedSiteLines += group.lineIndices.size();
        }
        CHECK(siteLines == expectedM2::site2028Lines[dayIndex]);
        CHECK(unsegregatedSiteLines == 0);
        std::cout << "Cross-day " << dayFiles()[dayIndex].label << " site2028=" << siteLines
                  << " unsegregated=" << unsegregatedSiteLines << '\n';
    }
}

TEST_CASE("segregation: cross-day Strict splits exactly 17 lanes every day despite different lane counts" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    std::set<std::size_t> distinctLaneCounts;
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const auto& run = pipelineRuns()[dayIndex];
        const auto result = segregate(run.join.lines, run.demand.dnm, SegregationReading::Strict,
                                      std::vector<bool>(run.join.lines.size(), false));
        distinctLaneCounts.insert(result.lanesIn);
        CHECK(result.lanesIn == expectedM2::lanesWithDemand[dayIndex]);
        CHECK(result.lanesSplit == expectedM2::strictLanesSplit);
    }
    CHECK(distinctLaneCounts.size() == kDayCount);
}

// A one-off in the 17 Aug extract, not an invariant: the September days have none.
TEST_CASE("segregation: a blank PLANNER_SNP line stays in its lane's normal group" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const auto& run = pipelineRuns()[dayIndex];
        const auto result = segregate(run.join.lines, run.demand.dnm, SegregationReading::Strict,
                                      std::vector<bool>(run.join.lines.size(), false));
        std::size_t blankPlannerLines = 0;
        std::size_t blankPlannerLinesSegregated = 0;
        for (const auto& group : result.groups) {
            for (const auto index : group.lineIndices) {
                if (!run.join.lines[index].str->planner_snp.empty()) continue;
                ++blankPlannerLines;
                if (group.key.isSegregated) ++blankPlannerLinesSegregated;
            }
        }
        CHECK(blankPlannerLines == expectedM2::blankPlannerLines[dayIndex]);
        CHECK(blankPlannerLinesSegregated == 0);
    }
}
