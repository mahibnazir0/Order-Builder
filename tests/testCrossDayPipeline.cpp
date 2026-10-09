#include "doctest.h"
#include "importer/crossDayFixtures.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace std;
using namespace ob;
using namespace crossDayTests;

namespace {

// The measured figures were read off the printed report, which rounds pallets to one
// decimal and weight to whole pounds.
bool matchesPrinted(double actual, double printed, double printedStep) {
    return std::fabs(actual - printed) <= printedStep / 2.0;
}

std::string oneDecimal(double value) {
    std::ostringstream formatted;
    formatted << std::fixed << std::setprecision(1) << value;
    return formatted.str();
}

// Demand files under the September naming, in their own temporary folder.
class DemandFolder {
public:
    DemandFolder() : directory_(filesystem::temp_directory_path() / "obCrossDayDemands") {
        filesystem::remove_all(directory_);
        filesystem::create_directories(directory_);
    }
    ~DemandFolder() { filesystem::remove_all(directory_); }
    DemandFolder(const DemandFolder&) = delete;
    DemandFolder& operator=(const DemandFolder&) = delete;

    void write(const string& name, const string& content) const {
        ofstream(directory_ / name) << content;
    }
    string path() const { return directory_.generic_string(); }

private:
    filesystem::path directory_;
};

} // namespace

// The extract tests are skipped without the confidential files, so this one fails instead:
// a clone without them must never pass a suite that ran none of them.
TEST_CASE("crossDay: the extract fixtures are present, so the extract tests ran") {
    for (const string& problem : missingExtracts()) {
        FAIL_CHECK(problem << " (confidential and gitignored; see README, Test data)");
    }
}

TEST_CASE("crossDay: a demand file is found by its REQUEST_ID, not by its name") {
    const DemandFolder folder;
    folder.write("100-STR-01a0ede5-ec6b-74a8-bb24-b9666e5f46ea.json", R"({"REQUEST_ID":"#A#","STR":[]})");
    folder.write("100-STR-01a0f82a-db76-7d60-984d-993498fa1a3e.json", R"({"REQUEST_ID":"#B#","STR":[]})");
    folder.write("100-PLACEHOLDER-01a0edd8-265c-7ee2-a9dd-181f8df2fa9b.json", R"({"PHOLDER":[]})");
    folder.write("notes.txt", R"({"REQUEST_ID":"#B#"})");
    CHECK(findDemandFile(folder.path(), "#B#")
          == folder.path() + "/100-STR-01a0f82a-db76-7d60-984d-993498fa1a3e.json");
    CHECK(findDemandFile(folder.path(), "#A#")
          == folder.path() + "/100-STR-01a0ede5-ec6b-74a8-bb24-b9666e5f46ea.json");
}

TEST_CASE("crossDay: a demand directory with no file, or two files, for the request is an error") {
    const DemandFolder folder;
    folder.write("Demand-1.json", R"({"REQUEST_ID":"#A#","STR":[]})");
    folder.write("100-STR-copy.json", R"({"REQUEST_ID":"#A#","STR":[]})");
    folder.write("broken.json", "{not json");
    CHECK_THROWS_WITH_AS(findDemandFile(folder.path(), "#MISSING#"),
                         doctest::Contains("0 demand file(s) have REQUEST_ID #MISSING#"),
                         runtime_error);
    CHECK_THROWS_WITH_AS(findDemandFile(folder.path(), "#A#"),
                         doctest::Contains("2 demand file(s) have REQUEST_ID #A#"),
                         runtime_error);
    CHECK_THROWS_WITH_AS(findDemandFile(folder.path() + "/absent", "#A#"),
                         doctest::Contains("demand directory not found"), runtime_error);
}

TEST_CASE("pipeline: cross-day Milestone 1 figures for all four extracts" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const auto& run = pipelineRuns()[dayIndex];
        const DaySummary& summary = run.summary;
        CHECK(static_cast<std::size_t>(summary.total_demand_lines) == expectedM1::demandLines[dayIndex]);
        CHECK(summary.hash_total == expectedM1::hashTotal[dayIndex]);
        CHECK(matchesPrinted(summary.total_pallet_equiv, expectedM1::palletEquivalents[dayIndex], 0.1));
        CHECK(matchesPrinted(summary.total_weight_lb, expectedM1::totalWeightLb[dayIndex], 1.0));
        CHECK(summary.lanes_total == expectedM1::lanesUnion[dayIndex]);
        CHECK(static_cast<std::size_t>(summary.lanes_with_demand) == expectedM2::lanesWithDemand[dayIndex]);
        CHECK(run.validation.errors == expectedM1::validationErrors[dayIndex]);
        CHECK(run.validation.warnings == expectedM1::validationWarnings[dayIndex]);
        CHECK(run.placeholders.placeholders.size() == expectedM1::placeholderEntries[dayIndex]);
        CHECK(summary.trucks_requested == expectedM1::trucksRequested[dayIndex]);
        CHECK(run.products.rows_read == expectedM1::productRows[dayIndex]);
        CHECK(run.index.size() == expectedM1::uniqueProductIds[dayIndex]);
        std::cout << "Cross-day " << dayFiles()[dayIndex].label << " M1: lines="
                  << summary.total_demand_lines << " hash=" << static_cast<long long>(summary.hash_total)
                  << " pallets=" << oneDecimal(summary.total_pallet_equiv)
                  << " weightLb=" << static_cast<long long>(std::llround(summary.total_weight_lb))
                  << " lanes=" << summary.lanes_total << " errors=" << run.validation.errors
                  << " warnings=" << run.validation.warnings
                  << " placeholders=" << run.placeholders.placeholders.size()
                  << " trucks=" << summary.trucks_requested << " productRows=" << run.products.rows_read
                  << " uniqueIds=" << run.index.size() << '\n';
    }
}

TEST_CASE("pipeline: cross-day Milestone 2 groups, binding split and single-high groups" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const auto& run = pipelineRuns()[dayIndex];
        REQUIRE(run.ranMilestone2);
        CHECK(run.missingPalletIds.empty());
        CHECK(run.params.doNotMixReading == SegregationReading::Strict);
        CHECK(run.segregation.lanesIn == expectedM2::lanesWithDemand[dayIndex]);
        CHECK(run.segregation.groups.size() == expectedM2::strictGroups[dayIndex]);
        CHECK(run.segregation.lanesSplit == expectedM2::strictLanesSplit[dayIndex]);
        CHECK(run.segregation.linesSegregated == expectedM2::linesSegregated[dayIndex]);
        CHECK(run.stackReport.groupsAllSingleHigh == expectedM2::groupsAllSingleHigh[dayIndex]);
        std::cout << "Cross-day " << dayFiles()[dayIndex].label << " M2: lanes="
                  << run.segregation.lanesIn << " groups=" << run.segregation.groups.size()
                  << " split=" << run.segregation.lanesSplit
                  << " segregated=" << run.segregation.linesSegregated
                  << " cubeBound=" << run.binding.cubeBoundGroups
                  << " weightBound=" << run.binding.weightBoundGroups
                  << " allSingleHigh=" << run.stackReport.groupsAllSingleHigh << '\n';
    }
}

// A change to stackPositions moves groups between cube- and weight-bound. If this test
// fails on the first REQUIRE, re-measure the split rather than treat it as a defect.
TEST_CASE("pipeline: cross-day cube/weight split at the configured stackPositions" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const auto& run = pipelineRuns()[dayIndex];
        REQUIRE_FALSE(run.params.trailers.empty());
        REQUIRE(run.params.trailers.front().stackPositions == expectedM2::stackPositionsMeasuredAt);
        CHECK(run.binding.cubeBoundGroups == expectedM2::cubeBoundGroups[dayIndex]);
        CHECK(run.binding.weightBoundGroups == expectedM2::weightBoundGroups[dayIndex]);
        CHECK(run.binding.cubeBoundGroups + run.binding.weightBoundGroups
              == expectedM2::strictGroups[dayIndex]);
    }
}

TEST_CASE("pipeline: cross-day FlaggedVsNormal from the params file regroups but flags the same lines" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const auto& flaggedVsNormal = flaggedVsNormalRuns()[dayIndex];
        const auto& strict = pipelineRuns()[dayIndex];
        REQUIRE(flaggedVsNormal.ranMilestone2);
        REQUIRE(flaggedVsNormal.params.doNotMixReading == SegregationReading::FlaggedVsNormal);
        const SegregationResult& segregation = flaggedVsNormal.segregation;
        std::size_t largestGroup = 0;
        for (const auto& group : segregation.groups) {
            largestGroup = std::max(largestGroup, group.lineIndices.size());
        }
        CHECK(segregation.groups.size() == expectedM2::flaggedVsNormalGroups[dayIndex]);
        CHECK(segregation.lanesSplit == expectedM2::flaggedVsNormalLanesSplit[dayIndex]);
        CHECK(largestGroup == expectedM2::flaggedVsNormalLargestGroup[dayIndex]);
        CHECK(segregation.linesSegregated == expectedM2::linesSegregated[dayIndex]);
        CHECK(segregation.linesSegregated == strict.segregation.linesSegregated);
        CHECK(flaggedVsNormal.stackReport.groups == segregation.groups.size());
        std::cout << "Cross-day " << dayFiles()[dayIndex].label << " FlaggedVsNormal M2: groups="
                  << segregation.groups.size() << " split=" << segregation.lanesSplit
                  << " largest=" << largestGroup << " segregated=" << segregation.linesSegregated
                  << " cubeBound=" << flaggedVsNormal.binding.cubeBoundGroups
                  << " weightBound=" << flaggedVsNormal.binding.weightBoundGroups
                  << " allSingleHigh=" << flaggedVsNormal.stackReport.groupsAllSingleHigh << '\n';
    }
}
