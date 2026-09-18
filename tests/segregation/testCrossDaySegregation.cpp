#include "doctest.h"
#include "segregation.hpp"
#include "../importer/crossDayFixtures.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

using namespace ob;
using namespace crossDayTests;

namespace {

using PlannerSite = std::pair<std::string, std::string>;
const std::array<PlannerSite, 4> activePairs{{
    {"S45", "2028"}, {"S01", "2028"}, {"S03", "2028"}, {"S20", "2027"}}};

std::set<PlannerSite> flaggedPairs(const DemandFile& demand) {
    std::set<PlannerSite> pairs;
    for (const auto& pair : demand.dnm) {
        if (!pair.planner_snp.empty()) pairs.emplace(pair.planner_snp, pair.locfrno);
    }
    return pairs;
}

void checkDay(std::size_t dayIndex, SegregationReading reading) {
    const auto& day = days()[dayIndex];
    const auto& fixture = fixtures()[dayIndex];
    const auto result = segregate(fixture.join.lines, fixture.demand.dnm, reading);
    const bool strict = reading == SegregationReading::Strict;
    CAPTURE(day.label);
    CHECK(result.linesIn == day.lines);
    CHECK(result.lanesIn == day.lanes);
    CHECK(result.doNotMixPairsLoaded == 22);
    CHECK(result.doNotMixPairsWithDemand == 4);
    CHECK(result.linesSegregated == day.segregatedLines);
    CHECK(result.groups.size() == (strict ? day.strictGroups : day.combinedGroups));
    CHECK(result.lanesSplit == (strict ? 17 : day.combinedSplitLanes));
    const auto pairs = flaggedPairs(fixture.demand);
    std::vector<bool> seen(fixture.join.lines.size(), false);
    std::set<std::string> planners;
    std::map<PlannerSite, std::size_t> pairCounts;
    std::size_t blankPlanners = 0;
    std::size_t lineTotal = 0;
    std::size_t largest = 0;
    std::size_t emptyGroups = 0;
    std::size_t invalidIndices = 0;
    std::size_t repeatedIndices = 0;
    std::size_t mixedPlannerGroups = 0;
    std::size_t incorrectLines = 0;
    std::size_t sameSiteGroups = 0;
    for (const auto& group : result.groups) {
        lineTotal += group.lineIndices.size();
        largest = std::max(largest, group.lineIndices.size());
        if (group.lineIndices.empty()) ++emptyGroups;
        if (group.sameSiteFlag) ++sameSiteGroups;
        std::set<std::string> groupPlanners;
        for (const auto index : group.lineIndices) {
            if (index >= seen.size()) { ++invalidIndices; continue; }
            if (seen[index]) ++repeatedIndices;
            seen[index] = true;
            const auto& line = *fixture.join.lines[index].str;
            planners.insert(line.planner_snp);
            if (line.planner_snp.empty()) ++blankPlanners;
            const PlannerSite pair{line.planner_snp, line.locfrno};
            const bool flagged = pairs.count(pair) != 0;
            if (flagged) {
                ++pairCounts[pair];
                groupPlanners.insert(line.planner_snp);
            }
            const auto expectedSegregant = flagged && strict ? line.planner_snp : std::string{};
            if (group.key.locationFrom != line.locfrno || group.key.locationTo != line.loctono
                || group.key.shipCondition != line.ship_cond || group.key.isSegregated != flagged
                || group.key.segregant != expectedSegregant
                || group.splitReason != (flagged ? SplitReason::DoNotMix : SplitReason::None)) {
                ++incorrectLines;
            }
        }
        if (strict && groupPlanners.size() > 1) ++mixedPlannerGroups;
    }
    CHECK(lineTotal == day.lines);
    CHECK(emptyGroups == 0);
    CHECK(invalidIndices == 0);
    CHECK(repeatedIndices == 0);
    CHECK(std::count(seen.begin(), seen.end(), false) == 0);
    CHECK(mixedPlannerGroups == 0);
    CHECK(incorrectLines == 0);
    CHECK(sameSiteGroups == 0);
    CHECK(planners.size() == day.planners);
    CHECK(blankPlanners == day.blankPlanners);
    CHECK(largest == (strict ? day.strictLargest : day.combinedLargest));
    std::map<PlannerSite, std::size_t> expectedCounts;
    for (std::size_t pairIndex = 0; pairIndex < activePairs.size(); ++pairIndex) {
        expectedCounts.emplace(activePairs[pairIndex], day.pairCounts[pairIndex]);
    }
    CHECK(pairCounts == expectedCounts);
    std::cout << "Cross-day " << day.label << (strict ? " Strict" : " FlaggedVsNormal")
              << ": lines=" << result.linesIn << " lanes=" << result.lanesIn
              << " planners=" << planners.size() << " blanks=" << blankPlanners
              << " pairs=" << result.doNotMixPairsWithDemand << '/' << result.doNotMixPairsLoaded
              << " flagged=" << result.linesSegregated << " groups=" << result.groups.size()
              << " split=" << result.lanesSplit << " largest=" << largest << '\n';
    for (const auto& entry : pairCounts) {
        std::cout << entry.first.first << '+' << entry.first.second << '=' << entry.second << ' ';
    }
    std::cout << '\n';
}

} // namespace

TEST_CASE("segregation: cross-day 02 Sep first Strict") { checkDay(1, SegregationReading::Strict); }
TEST_CASE("segregation: cross-day 02 Sep first FlaggedVsNormal") { checkDay(1, SegregationReading::FlaggedVsNormal); }
TEST_CASE("segregation: cross-day 02 Sep second Strict") { checkDay(2, SegregationReading::Strict); }
TEST_CASE("segregation: cross-day 02 Sep second FlaggedVsNormal") { checkDay(2, SegregationReading::FlaggedVsNormal); }
TEST_CASE("segregation: cross-day 03 Sep Strict") { checkDay(3, SegregationReading::Strict); }
TEST_CASE("segregation: cross-day 03 Sep FlaggedVsNormal") { checkDay(3, SegregationReading::FlaggedVsNormal); }

TEST_CASE("segregation: cross-day same four of 22 pairs have demand") {
    const std::set<PlannerSite> expected(activePairs.begin(), activePairs.end());
    for (std::size_t dayIndex = 0; dayIndex < days().size(); ++dayIndex) {
        CAPTURE(days()[dayIndex].label);
        const auto& fixture = fixtures()[dayIndex];
        const auto pairs = flaggedPairs(fixture.demand);
        std::set<PlannerSite> found;
        for (const auto& joined : fixture.join.lines) {
            const PlannerSite pair{joined.str->planner_snp, joined.str->locfrno};
            if (pairs.count(pair) != 0) found.insert(pair);
        }
        CHECK(fixture.demand.dnm.size() == 22);
        CHECK(pairs.size() == 22);
        CHECK(found == expected);
    }
}

TEST_CASE("segregation: cross-day site 2028 demand is entirely flagged") {
    for (std::size_t dayIndex = 0; dayIndex < days().size(); ++dayIndex) {
        CAPTURE(days()[dayIndex].label);
        const auto& fixture = fixtures()[dayIndex];
        const auto pairs = flaggedPairs(fixture.demand);
        std::size_t siteLines = 0;
        std::size_t unflaggedLines = 0;
        for (const auto& joined : fixture.join.lines) {
            const auto& line = *joined.str;
            if (line.locfrno != "2028") continue;
            ++siteLines;
            if (pairs.count({line.planner_snp, line.locfrno}) == 0) ++unflaggedLines;
        }
        CHECK(siteLines == days()[dayIndex].site2028Lines);
        CHECK(unflaggedLines == 0);
        std::cout << "Cross-day " << days()[dayIndex].label << " site2028=" << siteLines
                  << " unflagged=" << unflaggedLines << '\n';
    }
}

TEST_CASE("segregation: cross-day Strict always splits exactly 17 lanes") {
    for (std::size_t dayIndex = 0; dayIndex < days().size(); ++dayIndex) {
        CAPTURE(days()[dayIndex].label);
        const auto& fixture = fixtures()[dayIndex];
        const auto result = segregate(fixture.join.lines, fixture.demand.dnm, SegregationReading::Strict);
        CHECK(result.lanesIn == days()[dayIndex].lanes);
        CHECK(result.lanesSplit == 17);
    }
}

TEST_CASE("cross-day placeholders: load exact client filenames") {
    for (const auto& day : days()) {
        CAPTURE(day.label);
        const std::filesystem::path clientPath(day.placeholderPath);
        bool exactFilenameFound = false;
        for (const auto& entry : std::filesystem::directory_iterator(clientPath.parent_path())) {
            if (entry.path().filename().string() == clientPath.filename().string()) {
                exactFilenameFound = true;
                break;
            }
        }
        REQUIRE(exactFilenameFound);
        const auto loaded = PlaceholderImporter::load(day.placeholderPath);
        CHECK(loaded.placeholders.size() == day.placeholderEntries);
        CHECK(loaded.total_loads == day.trucks);
        std::cout << "Cross-day " << day.label << " placeholders=" << loaded.placeholders.size()
                  << " trucks=" << loaded.total_loads << " path=" << day.placeholderPath << '\n';
    }
}
