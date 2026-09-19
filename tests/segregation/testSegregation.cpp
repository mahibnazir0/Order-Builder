#include "doctest.h"
#include "pipeline.hpp"
#include "segregation.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace ob;

namespace {

const PipelineResult& realFixture() {
    static const PipelineResult fixture = [] {
        PipelineInputs inputs;
        inputs.product_path = "tests/importer/Customer2-Product-Data.csv";
        inputs.demand_path = "tests/importer/Demand-1.json";
        inputs.placeholder_path = "tests/importer/PlaceHolder-1.json";
        inputs.planning_day = "2026-08-17";
        return Pipeline::run(inputs);
    }();
    return fixture;
}

STRRecord syntheticLine(const std::string& planner, const std::string& origin = "2028",
                        const std::string& destination = "2299",
                        const std::string& condition = "TL") {
    STRRecord line;
    line.planner_snp = planner;
    line.locfrno = origin;
    line.loctono = destination;
    line.ship_cond = condition;
    return line;
}

SegregationResult syntheticResult(const std::vector<STRRecord>& demand,
                                 const std::vector<DoNotMixPair>& pairs,
                                 SegregationReading reading) {
    std::vector<JoinedLine> lines;
    lines.reserve(demand.size());
    for (const auto& line : demand) lines.push_back({&line, nullptr, false, false});
    return segregate(lines, pairs, reading);
}

std::string groupStateLabel(const GroupKey& key) {
    if (!key.isSegregated) return "normal";
    return key.segregant.empty() ? "segregated" : "segregated " + key.segregant;
}

std::string laneLabel(const GroupKey& key) {
    return key.locationFrom + " -> " + key.locationTo + " " + key.shipCondition
        + " [" + groupStateLabel(key) + "]";
}

// Per-line and per-group invariants are tallied and asserted once rather than asserted
// inside the loop: over 24,357 demand lines a per-line CHECK buries a real failure in
// thousands of near-identical lines, and the assertion count grows with the fixture.
// Each tally keeps the first offender so a failure still says where to look.
struct LineTally {
    std::size_t count = 0;
    std::size_t firstLineIndex = 0;

    void record(std::size_t lineIndex) {
        if (count == 0) firstLineIndex = lineIndex;
        ++count;
    }
};

struct GroupTally {
    std::size_t count = 0;
    std::string firstLane;

    void record(const GroupKey& key) {
        if (count == 0) firstLane = laneLabel(key);
        ++count;
    }
};

void checkNoOffendingLines(const LineTally& tally, const std::string& invariant) {
    CHECK_MESSAGE(tally.count == 0, invariant << ": " << tally.count
        << " lines, first at line index " << tally.firstLineIndex);
}

void checkNoOffendingGroups(const GroupTally& tally, const std::string& invariant) {
    CHECK_MESSAGE(tally.count == 0, invariant << ": " << tally.count
        << " groups, first is lane " << tally.firstLane);
}

// Groups must partition the demand lines: every index in range, none shared by two
// groups, none missing, and no group empty.
void checkGroupsPartitionLines(const std::vector<SegregatedGroup>& groups,
                               std::size_t lineCount) {
    GroupTally emptyGroups;
    LineTally outOfRangeIndices;
    LineTally duplicateIndices;
    LineTally uncoveredLines;
    std::size_t indicesTotal = 0;
    std::vector<bool> seen(lineCount, false);
    for (const auto& group : groups) {
        if (group.lineIndices.empty()) emptyGroups.record(group.key);
        indicesTotal += group.lineIndices.size();
        for (const auto index : group.lineIndices) {
            if (index >= lineCount) {
                outOfRangeIndices.record(index);
                continue;
            }
            if (seen[index]) duplicateIndices.record(index);
            seen[index] = true;
        }
    }
    for (std::size_t index = 0; index < lineCount; ++index) {
        if (!seen[index]) uncoveredLines.record(index);
    }
    // Out-of-range indices abort the case: callers dereference these indices afterwards.
    REQUIRE_MESSAGE(outOfRangeIndices.count == 0, "line index outside demand: "
        << outOfRangeIndices.count << " lines, first is " << outOfRangeIndices.firstLineIndex);
    checkNoOffendingLines(duplicateIndices, "line index held by two groups");
    checkNoOffendingLines(uncoveredLines, "line index held by no group");
    checkNoOffendingGroups(emptyGroups, "group holds no lines");
    CHECK(indicesTotal == lineCount);
}

void checkRealReading(SegregationReading reading, std::size_t expectedGroups,
                      std::size_t expectedSplitLanes) {
    const auto& fixture = realFixture();
    const auto result = segregate(fixture.join.lines, fixture.demand.dnm, reading);
    CHECK(fixture.demand.request_id == "#STR_PA4400_20260817164454#");
    CHECK(result.linesIn == 24357);
    CHECK(result.doNotMixPairsLoaded == 22);
    CHECK(result.doNotMixPairsWithDemand == 4);
    CHECK(result.linesSegregated == 2849);
    // Only demand lanes count here: M1's 371 also includes 11 placeholder-only lanes.
    CHECK(result.lanesIn == 360);
    CHECK(result.groups.size() == expectedGroups);
    CHECK(result.lanesSplit == expectedSplitLanes);

    checkGroupsPartitionLines(result.groups, fixture.join.lines.size());

    const bool strictReading = reading == SegregationReading::Strict;
    std::vector<std::size_t> sizes;
    sizes.reserve(result.groups.size());
    std::set<std::pair<std::string, std::string>> expectedFlaggedPlannerSites;
    for (const auto& pair : fixture.demand.dnm) {
        if (!pair.planner_snp.empty()) {
            expectedFlaggedPlannerSites.emplace(pair.planner_snp, pair.locfrno);
        }
    }
    std::set<std::string> planners;
    std::map<std::pair<std::string, std::string>, std::size_t> flaggedCounts;
    std::map<std::string, std::size_t> exampleLane;
    std::size_t blankPlanners = 0;
    GroupTally sameSiteFlaggedGroups;
    GroupTally mixedFlaggedPlannerGroups;
    LineTally originMismatches;
    LineTally destinationMismatches;
    LineTally shipConditionMismatches;
    LineTally splitReasonMismatches;
    LineTally segregantMismatches;
    for (const auto& group : result.groups) {
        if (group.sameSiteFlag) sameSiteFlaggedGroups.record(group.key);
        std::set<std::string> flaggedPlanners;
        sizes.push_back(group.lineIndices.size());
        if (group.key.locationFrom == "2028" && group.key.locationTo == "2299"
            && group.key.shipCondition == "TL") {
            exampleLane.emplace(groupStateLabel(group.key), group.lineIndices.size());
        }
        for (const auto index : group.lineIndices) {
            const auto& line = *fixture.join.lines[index].str;
            if (group.key.locationFrom != line.locfrno) originMismatches.record(index);
            if (group.key.locationTo != line.loctono) destinationMismatches.record(index);
            if (group.key.shipCondition != line.ship_cond) shipConditionMismatches.record(index);
            planners.insert(line.planner_snp);
            if (line.planner_snp.empty()) ++blankPlanners;
            const bool flagged = expectedFlaggedPlannerSites.count(
                {line.planner_snp, line.locfrno}) != 0;
            if (group.splitReason != (flagged ? SplitReason::DoNotMix : SplitReason::None)) {
                splitReasonMismatches.record(index);
            }
            const bool segregantMatches = flagged && strictReading
                ? group.key.segregant == line.planner_snp
                : group.key.segregant.empty();
            if (group.key.isSegregated != flagged || !segregantMatches) {
                segregantMismatches.record(index);
            }
            if (flagged) {
                flaggedPlanners.insert(line.planner_snp);
                ++flaggedCounts[{line.planner_snp, line.locfrno}];
            }
        }
        if (strictReading && flaggedPlanners.size() > 1) {
            mixedFlaggedPlannerGroups.record(group.key);
        }
    }
    checkNoOffendingLines(originMismatches, "group origin differs from its line");
    checkNoOffendingLines(destinationMismatches, "group destination differs from its line");
    checkNoOffendingLines(shipConditionMismatches, "group ship condition differs from its line");
    checkNoOffendingLines(splitReasonMismatches, "group split reason wrong for its line");
    checkNoOffendingLines(segregantMismatches,
                          "group isSegregated or segregant wrong for its line");
    checkNoOffendingGroups(sameSiteFlaggedGroups, "group carries sameSiteFlag");
    if (strictReading) {
        checkNoOffendingGroups(mixedFlaggedPlannerGroups,
                               "group holds two different flagged PLANNER_SNP values");
    }
    CHECK(planners.size() == 50);
    CHECK(planners.count("") == 1);
    CHECK(blankPlanners == 1);
    const std::map<std::pair<std::string, std::string>, std::size_t> expectedCounts{
        {{"S45", "2028"}, 1680}, {{"S01", "2028"}, 902},
        {{"S03", "2028"}, 142}, {{"S20", "2027"}, 125}};
    CHECK(flaggedCounts == expectedCounts);
    const std::map<std::string, std::size_t> expectedExample = strictReading
        ? std::map<std::string, std::size_t>{{"segregated S01", 269},
                                             {"segregated S45", 478},
                                             {"segregated S03", 30}}
        : std::map<std::string, std::size_t>{{"segregated", 777}};
    CHECK(exampleLane == expectedExample);
    CHECK(exampleLane.count("normal") == 0);
    const auto& largestGroup = *std::max_element(result.groups.begin(), result.groups.end(),
        [](const SegregatedGroup& left, const SegregatedGroup& right) {
            return left.lineIndices.size() < right.lineIndices.size();
        });
    std::sort(sizes.begin(), sizes.end());
    REQUIRE_FALSE(sizes.empty());
    const auto singleLineGroups = std::count(sizes.begin(), sizes.end(), std::size_t{1});
    if (strictReading) {
        CHECK(sizes.back() == 478);
        CHECK(laneLabel(largestGroup.key) == "2028 -> 2299 TL [segregated S45]");
        CHECK(sizes[sizes.size() / 2] == 22);
        CHECK(singleLineGroups == 27);
    } else {
        CHECK(sizes.back() == 781);
        CHECK(laneLabel(largestGroup.key) == "2028 -> 2508 TF [segregated]");
        CHECK(sizes[sizes.size() / 2] == 24);
        CHECK(singleLineGroups == 25);
    }
    std::cout << "Segregation " << (strictReading ? "Strict" : "FlaggedVsNormal")
              << ": lines=" << result.linesIn << " pairsLoaded=" << result.doNotMixPairsLoaded
              << " pairsWithDemand=" << result.doNotMixPairsWithDemand
              << " linesSegregated=" << result.linesSegregated << " planners=" << planners.size()
              << " blankPlanners=" << blankPlanners << " lanes=" << result.lanesIn
              << " groups=" << result.groups.size() << " lanesSplit=" << result.lanesSplit
              << " largest=" << sizes.back() << " (" << laneLabel(largestGroup.key) << ")"
              << " median=" << sizes[sizes.size() / 2]
              << " singleLineGroups=" << singleLineGroups << '\n';
    for (const auto& entry : flaggedCounts) {
        std::cout << "Pair " << entry.first.first << '+' << entry.first.second << ": " << entry.second << '\n';
    }
    for (const auto& entry : exampleLane) {
        std::cout << "Lane 2028 -> 2299 TL " << entry.first << ": " << entry.second << '\n';
    }
}

void checkIdlePair(const std::string& planner, const std::string& origin) {
    const auto& fixture = realFixture();
    CHECK(std::any_of(fixture.demand.dnm.begin(), fixture.demand.dnm.end(),
        [&](const DNMRecord& pair) { return pair.planner_snp == planner && pair.locfrno == origin; }));
    CHECK_FALSE(std::any_of(fixture.join.lines.begin(), fixture.join.lines.end(),
        [&](const JoinedLine& line) { return line.str->planner_snp == planner && line.str->locfrno == origin; }));
    for (const auto reading : {SegregationReading::Strict, SegregationReading::FlaggedVsNormal}) {
        const auto result = syntheticResult({syntheticLine(planner, origin), syntheticLine("UNFLAGGED", origin)},
                                            fixture.demand.dnm, reading);
        CHECK(result.linesIn == 2);
        CHECK(result.linesSegregated == 1);
        CHECK(result.doNotMixPairsWithDemand == 1);
        CHECK(result.lanesIn == 1);
        CHECK(result.lanesSplit == 1);
        REQUIRE(result.groups.size() == 2);
        for (const auto& group : result.groups) {
            REQUIRE(group.lineIndices.size() == 1);
            const bool flagged = group.lineIndices.front() == 0;
            CHECK(group.key.isSegregated == flagged);
            CHECK(group.key.segregant
                  == (flagged && reading == SegregationReading::Strict ? planner : ""));
            CHECK(group.splitReason == (flagged ? SplitReason::DoNotMix : SplitReason::None));
        }
    }
}

} // namespace

TEST_CASE("segregation: real fixture Strict") {
    checkRealReading(SegregationReading::Strict, 387, 17);
}

TEST_CASE("segregation: real fixture FlaggedVsNormal") {
    checkRealReading(SegregationReading::FlaggedVsNormal, 365, 5);
}
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S02_2028") { checkIdlePair("S02", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S20_2028") { checkIdlePair("S20", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S21_2028") { checkIdlePair("S21", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S22_2028") { checkIdlePair("S22", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S23_2028") { checkIdlePair("S23", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S24_2028") { checkIdlePair("S24", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S25_2028") { checkIdlePair("S25", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S28_2028") { checkIdlePair("S28", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S30_2028") { checkIdlePair("S30", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S32_2028") { checkIdlePair("S32", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S32_2048") { checkIdlePair("S32", "2048"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S38_2028") { checkIdlePair("S38", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S46_2028") { checkIdlePair("S46", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S47_2028") { checkIdlePair("S47", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S49_2028") { checkIdlePair("S49", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S51_2028") { checkIdlePair("S51", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S97_2028") { checkIdlePair("S97", "2028"); }
TEST_CASE("segregation: idleDoNotMixPairSynthetic_S98_2028") { checkIdlePair("S98", "2028"); }

TEST_CASE("segregation: synthetic S32 sites are independent pairs") {
    const std::vector<STRRecord> demand{syntheticLine("S32", "2028"), syntheticLine("S32", "2048")};
    for (const auto reading : {SegregationReading::Strict, SegregationReading::FlaggedVsNormal}) {
        for (const auto& origin : {"2028", "2048"}) {
            const auto result = syntheticResult(demand, {{"S32", origin}}, reading);
            CHECK(result.linesSegregated == 1);
            CHECK(result.doNotMixPairsWithDemand == 1);
            REQUIRE(result.groups.size() == 2);
            for (const auto& group : result.groups) {
                CHECK(group.splitReason == (group.key.locationFrom == origin
                    ? SplitReason::DoNotMix : SplitReason::None));
            }
        }
        const auto both = syntheticResult(demand, {{"S32", "2028"}, {"S32", "2048"}}, reading);
        CHECK(both.linesSegregated == 2);
        CHECK(both.doNotMixPairsWithDemand == 2);
        CHECK(both.lanesIn == 2);
        CHECK(both.lanesSplit == 0);
    }
}

// Regression guard for the sentinel collision. When the flagged/unflagged distinction
// lived in `segregant` alongside real planner codes, a planner literally named NORMAL
// built the same key as the lane's unflagged lines and the two merged into one group.
TEST_CASE("segregation: planner code NORMAL stays out of the unflagged group") {
    const std::vector<STRRecord> demand{syntheticLine("NORMAL"), syntheticLine("S01")};
    const auto result = syntheticResult(demand, {{"NORMAL", "2028"}}, SegregationReading::Strict);
    CHECK(result.linesIn == 2);
    CHECK(result.linesSegregated == 1);
    CHECK(result.doNotMixPairsWithDemand == 1);
    CHECK(result.lanesIn == 1);
    CHECK(result.lanesSplit == 1);
    REQUIRE(result.groups.size() == 2);
    CHECK_FALSE(result.groups[0].key.isSegregated);
    CHECK(result.groups[0].key.segregant.empty());
    CHECK(result.groups[0].splitReason == SplitReason::None);
    CHECK(result.groups[0].lineIndices == std::vector<std::size_t>{1});
    CHECK(result.groups[1].key.isSegregated);
    CHECK(result.groups[1].key.segregant == "NORMAL");
    CHECK(result.groups[1].splitReason == SplitReason::DoNotMix);
    CHECK(result.groups[1].lineIndices == std::vector<std::size_t>{0});
}

TEST_CASE("segregation: planner code SEGREGATED stays out of the unflagged group") {
    const std::vector<STRRecord> demand{syntheticLine("SEGREGATED"), syntheticLine("S01")};
    for (const auto reading : {SegregationReading::Strict, SegregationReading::FlaggedVsNormal}) {
        const auto result = syntheticResult(demand, {{"SEGREGATED", "2028"}}, reading);
        CHECK(result.linesSegregated == 1);
        CHECK(result.lanesSplit == 1);
        REQUIRE(result.groups.size() == 2);
        CHECK_FALSE(result.groups[0].key.isSegregated);
        CHECK(result.groups[0].key.segregant.empty());
        CHECK(result.groups[0].lineIndices == std::vector<std::size_t>{1});
        CHECK(result.groups[1].key.isSegregated);
        CHECK(result.groups[1].key.segregant
              == (reading == SegregationReading::Strict ? "SEGREGATED" : ""));
        CHECK(result.groups[1].lineIndices == std::vector<std::size_t>{0});
    }
}

TEST_CASE("segregation: synthetic absent planner or origin does not flag demand") {
    for (const auto& pair : std::vector<DoNotMixPair>{{"ABSENT", "2028"}, {"S01", "ABSENT"}}) {
        for (const auto reading : {SegregationReading::Strict, SegregationReading::FlaggedVsNormal}) {
            const auto result = syntheticResult({syntheticLine("S01")}, {pair}, reading);
            CHECK(result.linesSegregated == 0);
            CHECK(result.doNotMixPairsWithDemand == 0);
            REQUIRE(result.groups.size() == 1);
            CHECK_FALSE(result.groups.front().key.isSegregated);
        }
    }
}

TEST_CASE("segregation: synthetic blank planner never matches a blank pair") {
    for (const auto reading : {SegregationReading::Strict, SegregationReading::FlaggedVsNormal}) {
        const auto result = syntheticResult({syntheticLine("")}, {{"", "2028"}}, reading);
        CHECK(result.doNotMixPairsLoaded == 1);
        CHECK(result.doNotMixPairsWithDemand == 0);
        CHECK(result.linesSegregated == 0);
        REQUIRE(result.groups.size() == 1);
        CHECK_FALSE(result.groups.front().key.isSegregated);
        CHECK(result.groups.front().splitReason == SplitReason::None);
    }
}

TEST_CASE("segregation: synthetic duplicate pairs count demand once") {
    for (const auto reading : {SegregationReading::Strict, SegregationReading::FlaggedVsNormal}) {
        const auto result = syntheticResult({syntheticLine("S01"), syntheticLine("S01")},
                                            {{"S01", "2028"}, {"S01", "2028"}}, reading);
        CHECK(result.doNotMixPairsLoaded == 2);
        CHECK(result.doNotMixPairsWithDemand == 1);
        CHECK(result.linesIn == 2);
        CHECK(result.linesSegregated == 2);
        CHECK(result.lanesSplit == 0);
        REQUIRE(result.groups.size() == 1);
        CHECK(result.groups.front().lineIndices == std::vector<std::size_t>{0, 1});
    }
}

TEST_CASE("segregation: real fixture empty pair list leaves 360 unsplit demand lanes") {
    const auto& fixture = realFixture();
    for (const auto reading : {SegregationReading::Strict, SegregationReading::FlaggedVsNormal}) {
        const auto result = segregate(fixture.join.lines, {}, reading);
        CHECK(result.groups.size() == 360);
        CHECK(result.lanesIn == 360);
        CHECK(result.lanesSplit == 0);
        CHECK(result.linesIn == 24357);
        CHECK(result.linesSegregated == 0);
        CHECK(result.doNotMixPairsLoaded == 0);
        CHECK(result.doNotMixPairsWithDemand == 0);
        GroupTally segregatedGroups;
        GroupTally splitGroups;
        for (const auto& group : result.groups) {
            if (group.key.isSegregated) segregatedGroups.record(group.key);
            if (group.splitReason != SplitReason::None) splitGroups.record(group.key);
        }
        checkNoOffendingGroups(segregatedGroups, "group segregated without a do-not-mix pair");
        checkNoOffendingGroups(splitGroups, "group split without a do-not-mix pair");
        checkGroupsPartitionLines(result.groups, fixture.join.lines.size());
    }
}

TEST_CASE("segregation: empty demand has zero groups and totals") {
    for (const auto reading : {SegregationReading::Strict, SegregationReading::FlaggedVsNormal}) {
        const auto result = segregate({}, {{"S01", "2028"}}, reading);
        CHECK(result.groups.empty());
        CHECK(result.linesIn == 0);
        CHECK(result.linesSegregated == 0);
        CHECK(result.lanesIn == 0);
        CHECK(result.lanesSplit == 0);
        CHECK(result.doNotMixPairsLoaded == 1);
        CHECK(result.doNotMixPairsWithDemand == 0);
    }
}

TEST_CASE("segregation: synthetic single-line lane is flagged without being split") {
    for (const auto reading : {SegregationReading::Strict, SegregationReading::FlaggedVsNormal}) {
        const auto result = syntheticResult({syntheticLine("S01")}, {{"S01", "2028"}}, reading);
        CHECK(result.linesIn == 1);
        CHECK(result.linesSegregated == 1);
        CHECK(result.lanesIn == 1);
        CHECK(result.lanesSplit == 0);
        REQUIRE(result.groups.size() == 1);
        CHECK(result.groups.front().lineIndices == std::vector<std::size_t>{0});
        CHECK(result.groups.front().splitReason == SplitReason::DoNotMix);
    }
}

TEST_CASE("segregation: synthetic planner and origin matching is exact") {
    const std::vector<STRRecord> demand{syntheticLine("s01"), syntheticLine("S01 "),
        syntheticLine(" S01"), syntheticLine("S01", "2028 "), syntheticLine("S01")};
    for (const auto reading : {SegregationReading::Strict, SegregationReading::FlaggedVsNormal}) {
        const auto result = syntheticResult(demand, {{"S01", "2028"}}, reading);
        CHECK(result.linesIn == 5);
        CHECK(result.linesSegregated == 1);
        CHECK(result.doNotMixPairsWithDemand == 1);
        CHECK(result.groups.size() == 3);
        CHECK(result.lanesIn == 2);
        CHECK(result.lanesSplit == 1);
        for (const auto& group : result.groups) {
            if (group.splitReason == SplitReason::DoNotMix) {
                CHECK(group.lineIndices == std::vector<std::size_t>{4});
            }
        }
    }
}

TEST_CASE("segregation: synthetic lane identity includes origin destination and condition") {
    const auto result = syntheticResult({syntheticLine("S01"), syntheticLine("S01", "2048"),
        syntheticLine("S01", "2028", "2300"), syntheticLine("S01", "2028", "2299", "TF")},
        {}, SegregationReading::Strict);
    CHECK(result.groups.size() == 4);
    CHECK(result.lanesIn == 4);
    CHECK(result.lanesSplit == 0);
    for (std::size_t index = 1; index < result.groups.size(); ++index) {
        CHECK(result.groups[index - 1].key < result.groups[index].key);
    }
}

TEST_CASE("segregation: synthetic copied result owns indices after demand is destroyed") {
    static_assert(std::is_aggregate<SegregatedGroup>::value, "Groups remain aggregates");
    static_assert(std::is_aggregate<SegregationResult>::value, "Results remain aggregates");
    SegregationResult copy;
    {
        const auto original = syntheticResult({syntheticLine("S01"), syntheticLine("S03")},
            {{"S01", "2028"}, {"S03", "2028"}}, SegregationReading::Strict);
        copy = original;
    }
    REQUIRE(copy.groups.size() == 2);
    CHECK(copy.groups[0].key.isSegregated);
    CHECK(copy.groups[0].key.segregant == "S01");
    CHECK(copy.groups[0].lineIndices == std::vector<std::size_t>{0});
    CHECK(copy.groups[1].key.segregant == "S03");
    CHECK(copy.groups[1].lineIndices == std::vector<std::size_t>{1});
}
