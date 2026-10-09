#pragma once

#include "json.hpp"
#include "pipeline.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// The four Customer2 extracts: 17 Aug (flat in tests/importer) and the September
// days, which keep the client's own directory layout under tests/importer/crossDay.
// Every file is confidential client data and gitignored; none is ever committed.
//
// Each expectation below is one figure across the four days, in dayFiles() order,
// so it reads the same way as the measured table it was taken from.
namespace crossDayTests {

using namespace std;

constexpr size_t kDayCount = 4;

template <typename T>
using PerDay = array<T, kDayCount>;

// REQUEST_ID of a demand file; empty when the file is not a JSON object carrying one.
inline string requestIdOf(const filesystem::path& path) {
    ifstream input(path, ios::binary);
    const nlohmann::json root = nlohmann::json::parse(input, nullptr, false);
    if (!root.is_object()) return "";
    const auto found = root.find("REQUEST_ID");
    return found != root.end() && found->is_string() ? found->get<string>() : "";
}

// The one .json file in `directory` whose REQUEST_ID is `requestId`, whatever it is named:
// Demand-N.json until mid September, 100-STR-<uuid>.json since. Throws naming the directory
// when it is missing or holds no such file, or more than one.
inline string findDemandFile(const string& directory, const string& requestId) {
    if (!filesystem::is_directory(directory)) {
        throw runtime_error(directory + ": demand directory not found");
    }
    vector<string> matches;
    for (const auto& entry : filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json"
            && requestIdOf(entry.path()) == requestId) {
            matches.push_back(entry.path().generic_string());
        }
    }
    if (matches.size() != 1) {
        throw runtime_error(directory + ": " + to_string(matches.size())
                            + " demand file(s) have REQUEST_ID " + requestId
                            + ", expected exactly 1");
    }
    return matches.front();
}

struct DayFiles {
    string label;
    string productPath;
    string demandDirectory;
    // The demand file is found by this, never by its name. The client has renamed the files
    // once already, and two days' product masters can be byte-identical (1 and 2 Oct, 6 and
    // 7 Oct), so a fixture keyed by name or date alone could load the wrong day and still
    // look right. The request ID is the one thing in the files that names the day.
    string requestId;
    string placeholderDirectory;
    // Exactly as the client shipped it: "PlaceHolder-1.json" in August,
    // "Placeholder-N.json" in September, "100-PLACEHOLDER-<uuid>.json" since. Not
    // normalised; the case breaks on Linux. Placeholders carry no request ID, so the name
    // is the only key.
    string placeholderFileName;

    string demandPath() const { return findDemandFile(demandDirectory, requestId); }
    string placeholderPath() const { return placeholderDirectory + "/" + placeholderFileName; }
};

inline const PerDay<DayFiles>& dayFiles() {
    static const PerDay<DayFiles> files{{
        {"17 Aug", "tests/importer/Customer2-Product-Data.csv", "tests/importer",
         "#STR_PA4400_20260817164454#", "tests/importer", "PlaceHolder-1.json"},
        {"02 Sep #1", "tests/importer/crossDay/20260902/Product-Data/Customer2-Product-Data.csv",
         "tests/importer/crossDay/20260902/Demands", "#STR_PA4400_20260831155005#",
         "tests/importer/crossDay/20260902/PlaceHolder", "Placeholder-1.json"},
        {"02 Sep #2", "tests/importer/crossDay/20260902/Product-Data/Customer2-Product-Data.csv",
         "tests/importer/crossDay/20260902/Demands", "#STR_PA4400_20260901153549#",
         "tests/importer/crossDay/20260902/PlaceHolder", "Placeholder-2.json"},
        {"03 Sep", "tests/importer/crossDay/20260903/Product-Data/Customer2-Product-Data.csv",
         "tests/importer/crossDay/20260903/Demands", "#STR_PA4400_20260902110529#",
         "tests/importer/crossDay/20260903/PlaceHolder", "Placeholder-1.json"},
    }};
    return files;
}

// One line for each extract file a test needs and cannot find. The extracts are
// confidential and gitignored, so a fresh clone has none of them. The tests that read them
// are skipped then, and the "extract fixtures are present" test fails listing these lines,
// so a clone without the data never reports a green suite that ran none of them.
inline const vector<string>& missingExtracts() {
    static const vector<string> missing = [] {
        vector<string> problems;
        for (const DayFiles& day : dayFiles()) {
            for (const string& path : {day.productPath, day.placeholderPath()}) {
                if (!filesystem::is_regular_file(path)) {
                    problems.push_back(day.label + ": " + path + " not found");
                }
            }
            try {
                day.demandPath();
            } catch (const exception& error) {
                problems.push_back(day.label + ": " + error.what());
            }
        }
        return problems;
    }();
    return missing;
}

inline bool allExtractsPresent() { return missingExtracts().empty(); }

// Milestone 1, measured through the CLI on 26 Sep 2026.
namespace expectedM1 {
constexpr PerDay<size_t> demandLines{24357, 23462, 23003, 21909};
constexpr PerDay<double> hashTotal{8708934, 8275859, 8106348, 7778017};
constexpr PerDay<double> palletEquivalents{152911.2, 149391.3, 146023.0, 139678.5};
constexpr PerDay<double> totalWeightLb{103005833, 98793082, 95668859, 91591555};
constexpr PerDay<int> lanesUnion{371, 380, 383, 377};
constexpr PerDay<int> validationErrors{0, 0, 0, 0};
constexpr PerDay<int> validationWarnings{161, 204, 207, 169};
constexpr PerDay<size_t> placeholderEntries{189, 170, 181, 200};
constexpr PerDay<long long> trucksRequested{372, 329, 349, 397};
constexpr PerDay<int> productRows{20201, 20317, 20317, 20317};
constexpr PerDay<size_t> uniqueProductIds{20183, 20299, 20299, 20299};
} // namespace expectedM1

// Milestone 2, Strict reading, measured through the CLI on 26 Sep 2026.
namespace expectedM2 {
constexpr PerDay<size_t> lanesWithDemand{360, 368, 367, 369};
constexpr PerDay<size_t> strictGroups{387, 395, 394, 397};
constexpr size_t strictLanesSplit = 17;
constexpr PerDay<size_t> linesSegregated{2849, 2289, 2283, 2279};
// Pass 1 cube/weight split. Depends on the trailer's stackPositions, which was 30
// until the client corrected it to 32; these figures hold only at 32.
constexpr int stackPositionsMeasuredAt = 32;
constexpr PerDay<size_t> cubeBoundGroups{383, 394, 392, 397};
constexpr PerDay<size_t> weightBoundGroups{4, 1, 2, 0};
constexpr PerDay<size_t> groupsAllSingleHigh{234, 232, 228, 234};
constexpr PerDay<size_t> site2028Lines{2724, 2198, 2191, 2201};
// A one-off in the 17 Aug extract, not a property of the customer.
constexpr PerDay<size_t> blankPlannerLines{1, 0, 0, 0};
// FlaggedVsNormal reading, measured from the raw files on 27 Sep 2026. Lines segregated
// is the same under both readings: the reading changes grouping, never flagging.
constexpr PerDay<size_t> flaggedVsNormalGroups{365, 374, 373, 375};
constexpr PerDay<size_t> flaggedVsNormalLanesSplit{5, 6, 6, 6};
constexpr PerDay<size_t> flaggedVsNormalLargestGroup{781, 718, 704, 679};
} // namespace expectedM2

// Figures from the pre-rewrite cross-day tests (feature/m2-module-2-segregation),
// not part of the 26 Sep re-measurement.
namespace expectedRecovered {
constexpr PerDay<size_t> distinctPlanners{50, 51, 50, 50};
constexpr PerDay<size_t> strictLargestGroup{478, 501, 567, 476};
} // namespace expectedRecovered

const string kStrictParamsPath = "config/orderBuilderParams.json";

inline ob::PipelineInputs dayInputs(size_t dayIndex, const string& paramsPath) {
    const DayFiles& day = dayFiles()[dayIndex];
    ob::PipelineInputs inputs;
    inputs.product_path = day.productPath;
    inputs.demand_path = day.demandPath();
    inputs.placeholder_path = day.placeholderPath();
    inputs.planning_day = day.label;
    inputs.paramsPath = paramsPath;
    return inputs;
}

inline PerDay<ob::PipelineResult> runAllDays(const string& paramsPath) {
    PerDay<ob::PipelineResult> loaded;
    for (size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        loaded[dayIndex] = ob::Pipeline::run(dayInputs(dayIndex, paramsPath));
    }
    return loaded;
}

// Each Pipeline::run with params takes seconds, so every test shares one run per day.
inline const PerDay<ob::PipelineResult>& pipelineRuns() {
    static const auto results = runAllDays(kStrictParamsPath);
    return results;
}

// The shipped params with only doNotMixReading changed. Throws rather than silently
// running Strict if the key is not found in the expected form.
inline string writeFlaggedVsNormalParams() {
    ifstream shipped(kStrictParamsPath);
    stringstream buffer;
    buffer << shipped.rdbuf();
    string text = buffer.str();
    const string strictSetting = "\"doNotMixReading\": \"Strict\"";
    const auto position = text.find(strictSetting);
    if (position == string::npos) {
        throw runtime_error("crossDayFixtures: " + kStrictParamsPath + " has no " + strictSetting);
    }
    text.replace(position, strictSetting.size(), "\"doNotMixReading\": \"FlaggedVsNormal\"");
    const auto path = filesystem::temp_directory_path() / "obFlaggedVsNormalParams.json";
    ofstream(path) << text;
    return path.string();
}

inline const PerDay<ob::PipelineResult>& flaggedVsNormalRuns() {
    static const auto results = [] {
        const string paramsPath = writeFlaggedVsNormalParams();
        auto loaded = runAllDays(paramsPath);
        filesystem::remove(paramsPath);
        return loaded;
    }();
    return results;
}

} // namespace crossDayTests
