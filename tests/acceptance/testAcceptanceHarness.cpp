#include "doctest.h"
#include "../importer/crossDayFixtures.hpp"
#include "acceptanceHarness.hpp"
#include "paramsLoader.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;
using namespace crossDayTests;
using namespace acceptanceTests;

namespace {

// The September days with Truck Builder solutions: dayFiles() index and solution folder.
struct SolvedDay {
    size_t dayIndex;
    string solutionDirectory;
};
const array<SolvedDay, 3> kSolvedDays{{
    {1, "tests/importer/crossDay/20260902/Pushed-Solutions"},
    {2, "tests/importer/crossDay/20260902/Pushed-Solutions"},
    {3, "tests/importer/crossDay/20260903/Pushed-Solutions"},
}};

// Counted directly from the *_valid.json files, independently of this code: solution files,
// lane-days and loads on ship conditions TF and TL, per request.
constexpr array<size_t, 3> kSolutionFiles{41, 40, 40};
constexpr array<size_t, 3> kLaneDays{150, 140, 150};
constexpr array<size_t, 3> kAchievedLoads{290, 271, 319};

// Lane-days on which the no-stacking baseline exceeds Truck Builder's achieved loads, measured
// 17 Oct 2026 with capacity taken from floor area and the client's footprint rule.
constexpr array<size_t, 3> kBaselineExceeds{37, 38, 47};

// Truck Builder solution folders a solved-day test needs and cannot find. Like the extracts
// they are confidential and gitignored.
vector<string> missingSolutionFolders() {
    vector<string> missing;
    for (const SolvedDay& day : kSolvedDays) {
        if (!filesystem::is_directory(day.solutionDirectory)) {
            missing.push_back(day.solutionDirectory + " not found");
        }
    }
    return missing;
}

bool solvedDaysPresent() { return allExtractsPresent() && missingSolutionFolders().empty(); }

const TrailerSpec& shippedTrailer() {
    static const M2Params params = loadParams(kStrictParamsPath);
    return selectTrailer(params.trailers, params.sourcePath, "53FT_NA");
}

const AchievedRequest& achievedFor(size_t solvedIndex) {
    static const array<AchievedRequest, 3> achieved = [] {
        array<AchievedRequest, 3> loaded;
        for (size_t index = 0; index < kSolvedDays.size(); ++index) {
            const SolvedDay& day = kSolvedDays[index];
            loaded[index] = readAchievedLoads(day.solutionDirectory,
                                              pipelineRuns()[day.dayIndex].demand.request_id);
        }
        return loaded;
    }();
    return achieved[solvedIndex];
}

ValidityResult validityFor(size_t solvedIndex, const TrailerSpec& trailer) {
    return checkValidity(pipelineRuns()[kSolvedDays[solvedIndex].dayIndex],
                         achievedFor(solvedIndex), trailer);
}

// One solution file in its own temporary folder; removed when the test ends.
class SolutionFolder {
public:
    explicit SolutionFolder(const string& solutionJson)
        : directory_(filesystem::temp_directory_path() / "obAcceptanceHarnessTest") {
        filesystem::remove_all(directory_);
        filesystem::create_directories(directory_);
        ofstream(directory_ / "1_valid.json") << solutionJson;
    }
    ~SolutionFolder() { filesystem::remove_all(directory_); }
    SolutionFolder(const SolutionFolder&) = delete;
    SolutionFolder& operator=(const SolutionFolder&) = delete;

    string path() const { return directory_.string(); }

private:
    filesystem::path directory_;
};

string solutionWithLines(const string& linesJson, const string& shipCondition = "TL") {
    return R"({"REQUEST_ID":"R1","LANES":[{"LOCFRNO":"2027","LOCTONO":"2500","SHIP_COND":")"
        + shipCondition + R"(","LOADS":[{"LOAD_OPT_SCORE":"90","LINES":)" + linesJson
        + "}]}]}";
}

string readErrorMessage(const string& solutionJson) {
    const SolutionFolder folder(solutionJson);
    try {
        readAchievedLoads(folder.path(), "R1");
    } catch (const runtime_error& error) {
        return error.what();
    }
    return "";
}

} // namespace

// The solved-day tests are skipped without the solution folders, so this one fails instead.
TEST_CASE("acceptanceHarness: the Truck Builder solution fixtures are present, so the validity tests ran") {
    for (const string& problem : missingSolutionFolders()) {
        FAIL_CHECK(problem << " (confidential and gitignored; see README, Test data)");
    }
}

TEST_CASE("acceptanceHarness: a solution's TF and TL loads and shipped lines are read") {
    const SolutionFolder folder(solutionWithLines(
        R"([{"IDPR":"A","MATNR":"M1","MENGE":10,"MEINS":"CS"},)"
        R"({"IDPR":"A","MATNR":"M1","MENGE":5,"MEINS":"CS"}])"));
    const AchievedRequest achieved = readAchievedLoads(folder.path(), "R1");
    REQUIRE(achieved.laneDays.size() == 1);
    CHECK(achieved.laneDays[0].loads == 1);
    CHECK(achieved.shippedByIdpr.at("A").quantity == doctest::Approx(15.0));
}

TEST_CASE("acceptanceHarness: suffixed ship conditions are left out of the acceptance set") {
    const SolutionFolder folder(solutionWithLines(
        R"([{"IDPR":"A","MATNR":"M1","MENGE":10,"MEINS":"CS"}])", "TL_N102"));
    const AchievedRequest achieved = readAchievedLoads(folder.path(), "R1");
    CHECK(achieved.laneDays.empty());
    CHECK(achieved.shippedByIdpr.empty());
    CHECK(achieved.scoredLoadsOutsideSet == 1);
}

TEST_CASE("acceptanceHarness: a request with no solution is an error, not zero loads") {
    const SolutionFolder folder(solutionWithLines("[]"));
    CHECK_THROWS_AS(readAchievedLoads(folder.path(), "OTHER"), runtime_error);
}

TEST_CASE("acceptanceHarness: a malformed solution file is an error naming the file") {
    CHECK(readErrorMessage("{not json").find("1_valid.json") != string::npos);
    CHECK(readErrorMessage("[]").find("not a JSON object") != string::npos);
}

TEST_CASE("acceptanceHarness: a block present with the wrong type is an error, not absent") {
    CHECK(readErrorMessage(R"({"REQUEST_ID":"R1","LANES":{}})").find("LANES") != string::npos);
    CHECK(readErrorMessage(R"({"REQUEST_ID":"R1","LANES":[{"LOCFRNO":"2027","LOCTONO":"2500",)"
                           R"("SHIP_COND":"TL","LOADS":"none"}]})")
              .find("LANES[0].LOADS") != string::npos);
    CHECK(readErrorMessage(solutionWithLines("{}")).find("LOADS[0].LINES") != string::npos);
}

TEST_CASE("acceptanceHarness: a missing or wrong-type field is an error naming it") {
    CHECK(readErrorMessage(R"({"LANES":[]})").find("REQUEST_ID") != string::npos);
    CHECK(readErrorMessage(R"({"REQUEST_ID":7,"LANES":[]})").find("REQUEST_ID") != string::npos);
    CHECK(readErrorMessage(solutionWithLines(R"([{"MATNR":"M1","MENGE":1,"MEINS":"CS"}])"))
              .find("LINES[0].IDPR") != string::npos);
    CHECK(readErrorMessage(solutionWithLines(
              R"([{"IDPR":"A","MATNR":"M1","MENGE":"1","MEINS":"CS"}])"))
              .find("LINES[0].MENGE") != string::npos);
}

TEST_CASE("acceptanceHarness: a negative shipped quantity is an error") {
    CHECK(readErrorMessage(solutionWithLines(
              R"([{"IDPR":"A","MATNR":"M1","MENGE":-1,"MEINS":"CS"}])"))
              .find("non-negative finite") != string::npos);
}

TEST_CASE("acceptanceHarness: one IDPR shipped as two materials is an error") {
    CHECK(readErrorMessage(solutionWithLines(
              R"([{"IDPR":"A","MATNR":"M1","MENGE":1,"MEINS":"CS"},)"
              R"({"IDPR":"A","MATNR":"M2","MENGE":1,"MEINS":"CS"}])"))
              .find("different material or unit") != string::npos);
}

TEST_CASE("acceptanceHarness: solutions for every solved day match the raw files" * doctest::skip(!solvedDaysPresent())) {
    for (size_t index = 0; index < kSolvedDays.size(); ++index) {
        CAPTURE(dayFiles()[kSolvedDays[index].dayIndex].label);
        const AchievedRequest& achieved = achievedFor(index);
        CHECK(achieved.solutionFiles == kSolutionFiles[index]);
        CHECK(achieved.laneDays.size() == kLaneDays[index]);
        CHECK(achieved.totalLoads() == kAchievedLoads[index]);
        CHECK(achieved.scoredLoadsOutsideSet == 0);
        CHECK(achieved.unscoredLoadsInSet == 0);
    }
}

TEST_CASE("acceptanceHarness: every shipped line is floored on every solved day" * doctest::skip(!solvedDaysPresent())) {
    for (size_t index = 0; index < kSolvedDays.size(); ++index) {
        CAPTURE(dayFiles()[kSolvedDays[index].dayIndex].label);
        const ValidityResult validity = validityFor(index, shippedTrailer());
        CHECK(validity.laneDays.size() == kLaneDays[index]);
        CHECK(validity.achievedLoads == kAchievedLoads[index]);
        CHECK(validity.shippedLinesNotInDemand == 0);
        CHECK(validity.shippedLinesDisagreeing == 0);
        CHECK(validity.shippedLinesNotGrouped == 0);
        CHECK(validity.shippedLinesLeftOutOfFloor == 0);
    }
}

TEST_CASE("acceptanceHarness: a lane-day floored above its achieved loads is counted" * doctest::skip(!solvedDaysPresent())) {
    AchievedRequest oneLoadShort = achievedFor(0);
    REQUIRE(!oneLoadShort.laneDays.empty());
    const ValidityResult before = validityFor(0, shippedTrailer());
    REQUIRE(before.laneDays[0].floorTrucks > 0);
    REQUIRE(before.laneDays[0].floorTrucks
            <= static_cast<long long>(before.laneDays[0].achievedLoads));
    oneLoadShort.laneDays[0].loads = static_cast<size_t>(before.laneDays[0].floorTrucks) - 1;
    const ValidityResult after = checkValidity(pipelineRuns()[kSolvedDays[0].dayIndex],
                                               oneLoadShort, shippedTrailer());
    CHECK(after.floorExceedsAchieved == before.floorExceedsAchieved + 1);
}

// The validity criterion itself: the floor is a lower bound, so no lane-day may need more
// trucks than Truck Builder used. It is not marked as expected to fail. While the bound is
// invalid (M3 section 2.5, edge case 31) this test fails, and the suite with it. The count is
// printed on every run, pass or fail, so the defect is tracked.
TEST_CASE("acceptanceHarness: the floor never exceeds Truck Builder's achieved loads" * doctest::skip(!solvedDaysPresent())) {
    for (size_t index = 0; index < kSolvedDays.size(); ++index) {
        const string& label = dayFiles()[kSolvedDays[index].dayIndex].label;
        CAPTURE(label);
        const size_t exceeds = validityFor(index, shippedTrailer()).floorExceedsAchieved;
        MESSAGE(label << ": floor exceeds achieved loads on " << exceeds << " of "
                      << kLaneDays[index] << " lane-days");
        CHECK(exceeds == 0);
    }
}

TEST_CASE("acceptanceHarness: the no-stacking baseline exceeds achieved loads far more often than the floor" * doctest::skip(!solvedDaysPresent())) {
    for (size_t index = 0; index < kSolvedDays.size(); ++index) {
        CAPTURE(dayFiles()[kSolvedDays[index].dayIndex].label);
        const ValidityResult validity = validityFor(index, shippedTrailer());
        CHECK(validity.baselineExceedsAchieved == kBaselineExceeds[index]);
        CHECK(validity.floorExceedsAchieved < validity.baselineExceedsAchieved);
    }
}

namespace {

// The 4-6 August runs the 1,219 figure was quoted on: the request each Truck Builder solution
// set answers, and the demand file that request planned (client README, 13 Oct: the run
// labelled Aug4 planned Demand-Aug5-2). Placeholders carry no request ID, so they pair by the
// supplied day label only. No historical config or pallet table exists, so the run uses the
// shipped params and the 29 Sep pallet table, as the client's own 13 Oct check did.
struct AugustRun {
    string label;
    string requestId;
    string demandPath;
    string placeholderPath;
    string solutionDirectory;
};
const string kAugustDirectory = "tests/importer/august";
const string kAugustProductPath = kAugustDirectory + "/Product-Data.csv";

const array<AugustRun, 3> kAugustRuns{{
    {"Aug4", "#STR_PA4400_20260804154458#", kAugustDirectory + "/Aug4/demand/Demand-Aug5-2.json",
     kAugustDirectory + "/Aug4/PH-Aug4.json", kAugustDirectory + "/Aug4/solutions"},
    {"Aug5", "#STR_PA4400_20260805153651#", kAugustDirectory + "/Aug5/demand/Demand-Aug5.json",
     kAugustDirectory + "/Aug5/PH-Aug5.json", kAugustDirectory + "/Aug5/solutions"},
    {"Aug6", "#STR_PA4400_20260806165912#", kAugustDirectory + "/Aug6/demand/Demand-Aug6.json",
     kAugustDirectory + "/Aug6/PH-Aug6.json", kAugustDirectory + "/Aug6/solutions"},
}};

// From the client's README (13 Oct), counted independently of this code. Solution files are
// for the main request only: the README's 40 and 41 for Aug4 and Aug5 include one
// supplementary #q request each.
constexpr array<size_t, 3> kAugustSolutionFiles{39, 40, 39};
constexpr array<size_t, 3> kAugustLaneDays{134, 158, 162};
constexpr array<size_t, 3> kAugustAchievedLoads{282, 492, 534};
constexpr size_t kAugustTotalLaneDays = 454;
constexpr size_t kAugustTotalAchievedLoads = 1308;

// The acceptance figure the client quoted. Its definition is not confirmed.
constexpr long long kAcceptedTrucks = 1219;

bool augustPresent() {
    if (!filesystem::is_regular_file(kAugustProductPath)
        || !filesystem::is_regular_file(kLayerSizesForOlderMasters)) {
        return false;
    }
    for (const AugustRun& run : kAugustRuns) {
        if (!filesystem::is_regular_file(run.demandPath)
            || !filesystem::is_regular_file(run.placeholderPath)
            || !filesystem::is_directory(run.solutionDirectory)) {
            return false;
        }
    }
    return true;
}

const PipelineResult& augustPipelineRun(size_t runIndex) {
    static const array<PipelineResult, 3> runs = [] {
        array<PipelineResult, 3> loaded;
        for (size_t index = 0; index < kAugustRuns.size(); ++index) {
            PipelineInputs inputs;
            inputs.product_path = kAugustProductPath;
            inputs.demand_path = kAugustRuns[index].demandPath;
            inputs.placeholder_path = kAugustRuns[index].placeholderPath;
            inputs.planning_day = kAugustRuns[index].label;
            inputs.paramsPath = kStrictParamsPath;
            inputs.palletPath = kPalletTableForOlderMasters;
            inputs.layerSizePath = kLayerSizesForOlderMasters;
            loaded[index] = Pipeline::run(inputs);
        }
        return loaded;
    }();
    return runs[runIndex];
}

const AchievedRequest& augustAchieved(size_t runIndex) {
    static const array<AchievedRequest, 3> achieved = [] {
        array<AchievedRequest, 3> loaded;
        for (size_t index = 0; index < kAugustRuns.size(); ++index) {
            loaded[index] = readAchievedLoads(kAugustRuns[index].solutionDirectory,
                                              kAugustRuns[index].requestId);
        }
        return loaded;
    }();
    return achieved[runIndex];
}

ValidityResult augustValidity(size_t runIndex, const TrailerSpec& trailer) {
    return checkValidity(augustPipelineRun(runIndex), augustAchieved(runIndex), trailer);
}

} // namespace

TEST_CASE("acceptanceHarness: each August demand file is the request its solutions answer" * doctest::skip(!augustPresent())) {
    for (size_t index = 0; index < kAugustRuns.size(); ++index) {
        CAPTURE(kAugustRuns[index].label);
        CHECK(augustPipelineRun(index).demand.request_id == kAugustRuns[index].requestId);
    }
}

TEST_CASE("acceptanceHarness: the August solutions give the client's 454 lane-days and 1,308 loads" * doctest::skip(!augustPresent())) {
    size_t laneDays = 0;
    size_t loads = 0;
    for (size_t index = 0; index < kAugustRuns.size(); ++index) {
        CAPTURE(kAugustRuns[index].label);
        const AchievedRequest& achieved = augustAchieved(index);
        CHECK(achieved.solutionFiles == kAugustSolutionFiles[index]);
        CHECK(achieved.laneDays.size() == kAugustLaneDays[index]);
        CHECK(achieved.totalLoads() == kAugustAchievedLoads[index]);
        CHECK(achieved.scoredLoadsOutsideSet == 0);
        CHECK(achieved.unscoredLoadsInSet == 0);
        laneDays += achieved.laneDays.size();
        loads += achieved.totalLoads();
    }
    CHECK(laneDays == kAugustTotalLaneDays);
    CHECK(loads == kAugustTotalAchievedLoads);
}

TEST_CASE("acceptanceHarness: every August shipped line is floored" * doctest::skip(!augustPresent())) {
    for (size_t index = 0; index < kAugustRuns.size(); ++index) {
        CAPTURE(kAugustRuns[index].label);
        const ValidityResult validity = augustValidity(index, shippedTrailer());
        CHECK(validity.laneDays.size() == kAugustLaneDays[index]);
        CHECK(validity.achievedLoads == kAugustAchievedLoads[index]);
        CHECK(validity.shippedLinesNotInDemand == 0);
        CHECK(validity.shippedLinesDisagreeing == 0);
        CHECK(validity.shippedLinesNotGrouped == 0);
        CHECK(validity.shippedLinesLeftOutOfFloor == 0);
    }
}

TEST_CASE("acceptanceHarness: Aug6's placeholder is missing a SHIP_COND and only Aug6 fails validation" * doctest::skip(!augustPresent())) {
    CHECK(augustPipelineRun(0).validation.errors == 0);
    CHECK(augustPipelineRun(1).validation.errors == 0);
    CHECK(augustPipelineRun(2).validation.errors == 1);
    CHECK(augustPipelineRun(2).validation.missing_ship_cond == 1);
}

// A regression pin, not an acceptance figure: what this code floors the August shipped volume
// to on 17 Oct 2026, with capacity taken from floor area and volume and each footprint from
// the client's layer-size rule, so any change to it has to be explained.
TEST_CASE("acceptanceHarness: the August floor matches the measured run" * doctest::skip(!augustPresent())) {
    constexpr array<long long, 3> floorTrucks{291, 469, 509};
    constexpr array<long long, 3> noStackingBaselineTrucks{370, 557, 605};
    for (size_t index = 0; index < kAugustRuns.size(); ++index) {
        CAPTURE(kAugustRuns[index].label);
        const ValidityResult validity = augustValidity(index, shippedTrailer());
        CHECK(validity.floorTrucks == floorTrucks[index]);
        CHECK(validity.noStackingBaselineTrucks == noStackingBaselineTrucks[index]);
    }
}

// The validity criterion on the August data, as for the September days above: not marked as
// expected to fail, so while the bound is invalid the suite fails and prints the counts.
TEST_CASE("acceptanceHarness: the August floor never exceeds Truck Builder's achieved loads" * doctest::skip(!augustPresent())) {
    for (size_t index = 0; index < kAugustRuns.size(); ++index) {
        CAPTURE(kAugustRuns[index].label);
        const size_t exceeds = augustValidity(index, shippedTrailer()).floorExceedsAchieved;
        MESSAGE(kAugustRuns[index].label << ": floor exceeds achieved loads on " << exceeds
                << " of " << kAugustLaneDays[index] << " lane-days");
        CHECK(exceeds == 0);
    }
}

// Acceptance: 1,219 trucks across the 454 August lane-days, the milestone's agreed figure. The
// client describes it as the trucks reserved in advance (15 Oct); the August placeholder files
// reserve 1,503, so how it was counted is still open. The run prints the floor beside it.
TEST_CASE("acceptanceHarness: the August floor reproduces 1,219 trucks across 454 lane-days" * doctest::skip(!augustPresent())) {
    long long floorTrucks = 0;
    for (size_t index = 0; index < kAugustRuns.size(); ++index) {
        floorTrucks += augustValidity(index, shippedTrailer()).floorTrucks;
    }
    MESSAGE("August floor " << floorTrucks << " trucks against the accepted " << kAcceptedTrucks
            << " and " << kAugustTotalAchievedLoads << " achieved");
    CHECK(floorTrucks == kAcceptedTrucks);
}
