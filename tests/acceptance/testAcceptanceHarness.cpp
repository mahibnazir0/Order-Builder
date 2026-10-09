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

// Question 7: the client corrected 30 to 32 positions and said 34 also occurs, depending on
// footprint. Validity is measured under both.
constexpr int kAlternativeStackPositions = 34;

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

// Lane-days on which the no-stacking baseline exceeds Truck Builder's achieved loads,
// measured 7 Oct 2026.
constexpr array<size_t, 3> kBaselineExceedsAt32{62, 69, 75};

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

TrailerSpec trailerWithPositions(int stackPositions) {
    TrailerSpec trailer = shippedTrailer();
    trailer.stackPositions = stackPositions;
    return trailer;
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
// invalid (M3 section 2.5, edge case 31) this test fails, and the suite with it. The counts
// at 32 and 34 positions are printed on every run, pass or fail, so the defect is tracked.
TEST_CASE("acceptanceHarness: the floor never exceeds Truck Builder's achieved loads" * doctest::skip(!solvedDaysPresent())) {
    const TrailerSpec alternativePositions = trailerWithPositions(kAlternativeStackPositions);
    for (size_t index = 0; index < kSolvedDays.size(); ++index) {
        const string& label = dayFiles()[kSolvedDays[index].dayIndex].label;
        CAPTURE(label);
        const size_t exceedsAt32 = validityFor(index, shippedTrailer()).floorExceedsAchieved;
        const size_t exceedsAt34 = validityFor(index, alternativePositions).floorExceedsAchieved;
        MESSAGE(label << ": floor exceeds achieved loads on " << exceedsAt32 << " of "
                      << kLaneDays[index] << " lane-days at " << shippedTrailer().stackPositions
                      << " positions, " << exceedsAt34 << " at " << kAlternativeStackPositions);
        CHECK(exceedsAt32 == 0);
    }
}

TEST_CASE("acceptanceHarness: the no-stacking baseline exceeds achieved loads far more often than the floor" * doctest::skip(!solvedDaysPresent())) {
    for (size_t index = 0; index < kSolvedDays.size(); ++index) {
        CAPTURE(dayFiles()[kSolvedDays[index].dayIndex].label);
        const ValidityResult validity = validityFor(index, shippedTrailer());
        CHECK(validity.baselineExceedsAchieved == kBaselineExceedsAt32[index]);
        CHECK(validity.floorExceedsAchieved < validity.baselineExceedsAchieved);
    }
}

// Acceptance: 1,219 trucks across 454 lane-days on the 4-6 August data. We hold neither the
// August demand extracts nor their configuration (question 2), and the planning-day rule is
// not agreed (question 1), so this runs only once the inputs are supplied, and then fails
// until the run is written against the agreed rule.
TEST_CASE("acceptanceHarness: the August run reproduces 1,219 trucks across 454 lane-days" * doctest::skip(!filesystem::is_directory("tests/importer/august"))) {
    constexpr long long kAcceptedTrucks = 1219;
    constexpr size_t kAcceptedLaneDays = 454;
    FAIL("August inputs found: write this run against the agreed planning-day rule (question 1)"
         " and configuration (question 2), expecting " << kAcceptedTrucks << " trucks across "
         << kAcceptedLaneDays << " lane-days");
}
