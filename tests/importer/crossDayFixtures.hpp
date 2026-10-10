#pragma once

#include "json.hpp"
#include "pipeline.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// The four Customer2 extracts: 17 Aug (flat in tests/importer) and the September
// days, which keep the client's own directory layout under tests/importer/crossDay.
// Every file is confidential client data and gitignored; none is ever committed.
//
// Each expectation below is one figure across the four days, in dayFiles() order,
// so it reads the same way as the measured table it was taken from. The 29 Sep - 5 Oct
// extracts are in newExtracts at the end of this file.
namespace crossDayTests {

using namespace std;

constexpr size_t kDayCount = 4;

// The masters before 29 Sep carry no Pallet_* columns and those extracts shipped no pallet
// table, so the 29 Sep table stands in for them. It is byte-identical on every day from
// 29 Sep to 5 Oct.
const string kPalletTableForOlderMasters =
    "tests/importer/crossDay/20260929/Product-Data/Customer2-Pallet-Data.csv";

// True when every path is a file. Otherwise names the first missing one, so a skipped test
// reads as skipped rather than as a pass.
inline bool filesPresent(initializer_list<string> paths, const string& skippedWhat) {
    for (const string& path : paths) {
        if (!filesystem::is_regular_file(path)) {
            cerr << "[fixtures] " << path << " not found: " << skippedWhat << " skipped\n";
            return false;
        }
    }
    return true;
}

// The 17 Aug files, for tests that read them directly. Confidential and gitignored, so a fresh
// clone skips these tests rather than failing them.
inline bool august17Present() {
    static const bool present = filesPresent(
        {"tests/importer/Customer2-Product-Data.csv", "tests/importer/Demand-1.json",
         "tests/importer/PlaceHolder-1.json"},
        "17 Aug tests are");
    return present;
}

// 17 Aug plus the pallet table its stacks are built with.
inline bool august17StackingPresent() {
    static const bool present = august17Present()
        && filesPresent({kPalletTableForOlderMasters}, "17 Aug stacking tests are");
    return present;
}

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
        if (!filesystem::is_regular_file(kPalletTableForOlderMasters)) {
            problems.push_back("pallet table: " + kPalletTableForOlderMasters + " not found");
        }
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
// Added to the tally by Milestone 2: lines whose product exceeds its own CRI limit.
constexpr PerDay<int> exceedsOwnCriWarnings{2, 0, 0, 0};
constexpr PerDay<size_t> placeholderEntries{189, 170, 181, 200};
constexpr PerDay<long long> trucksRequested{372, 329, 349, 397};
constexpr PerDay<int> productRows{20201, 20317, 20317, 20317};
constexpr PerDay<size_t> uniqueProductIds{20183, 20299, 20299, 20299};
} // namespace expectedM1

// Milestone 2, Strict reading, measured through the CLI on 26 Sep 2026.
namespace expectedM2 {
constexpr PerDay<size_t> lanesWithDemand{360, 368, 367, 369};
constexpr PerDay<size_t> strictGroups{387, 395, 394, 397};
// A per-day measurement, not a property of the customer: 17 on each of these four days,
// but 19 or 20 on every extract from 29 Sep to 5 Oct (newExtracts::lanesSplit).
constexpr PerDay<size_t> strictLanesSplit{17, 17, 17, 17};
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
    inputs.palletPath = kPalletTableForOlderMasters;
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

// The five extracts of 29 Sep - 5 Oct, in the client's layout: crossDay/<yyyymmdd>/ with
// Demands/100-STR-<uuid>.json, PlaceHolder/100-PLACEHOLDER-<uuid>.json and Product-Data/
// holding the 88-column master and the pallet table. Each day is checked on its own, so a
// machine holding only some of them still tests those.
namespace newExtracts {

constexpr size_t kDayCount = 5;

template <typename T>
using PerDay = array<T, kDayCount>;

const PerDay<string> labels{"29 Sep", "30 Sep", "01 Oct", "02 Oct", "05 Oct"};
const PerDay<string> directories{
    "tests/importer/crossDay/20260929", "tests/importer/crossDay/20260930",
    "tests/importer/crossDay/20261001", "tests/importer/crossDay/20261002",
    "tests/importer/crossDay/20261005"};

// Measured independently from the raw JSON and CSV (M2 open items, 13 Oct 2026).
namespace expected {
constexpr PerDay<size_t> demandLines{21832, 21746, 20659, 18236, 22287};
constexpr PerDay<size_t> lanesWithDemand{361, 372, 372, 361, 358};
constexpr PerDay<size_t> strictGroups{389, 401, 401, 391, 387};
constexpr PerDay<size_t> lanesSplit{19, 20, 20, 20, 20};
constexpr PerDay<size_t> linesSegregated{2765, 2875, 2766, 2536, 3053};
constexpr PerDay<size_t> placeholderEntries{181, 283, 275, 228, 299};
constexpr PerDay<long long> trucksRequested{339, 604, 527, 411, 618};
constexpr PerDay<int> productRows{21319, 21319, 21351, 21351, 21355};
// The same on every day.
constexpr size_t masterColumns = 88;
constexpr int duplicatedProductIds = 18;
constexpr size_t doNotMixPairs = 22;
constexpr size_t doNotMixPairsWithDemand = 4;
} // namespace expected

// The one file in `directory` whose name starts with `prefix`; empty if there is none.
// More than one is an error: the test would otherwise pick one silently.
inline string singleFileStartingWith(const string& directory, const string& prefix) {
    string found;
    if (!filesystem::is_directory(directory)) return found;
    for (const auto& entry : filesystem::directory_iterator(directory)) {
        const string name = entry.path().filename().string();
        if (!entry.is_regular_file() || name.rfind(prefix, 0) != 0) continue;
        if (!found.empty()) throw runtime_error("newExtracts: two " + prefix + " files in " + directory);
        found = entry.path().generic_string();
    }
    return found;
}

inline string productPath(size_t day) {
    return directories[day] + "/Product-Data/Customer2-Product-Data.csv";
}
inline string palletPath(size_t day) {
    return directories[day] + "/Product-Data/Customer2-Pallet-Data.csv";
}

// Inputs for one day; the demand and placeholder paths are empty when it is not on this machine.
inline ob::PipelineInputs inputsFor(size_t day) {
    ob::PipelineInputs inputs;
    inputs.product_path = productPath(day);
    inputs.demand_path = singleFileStartingWith(directories[day] + "/Demands", "100-STR-");
    inputs.placeholder_path = singleFileStartingWith(directories[day] + "/PlaceHolder", "100-PLACEHOLDER-");
    inputs.planning_day = labels[day];
    inputs.paramsPath = kStrictParamsPath;
    inputs.palletPath = palletPath(day);
    return inputs;
}

inline bool dayPresent(size_t day) {
    static const PerDay<bool> present = [] {
        PerDay<bool> flags{};
        for (size_t index = 0; index < kDayCount; ++index) {
            const ob::PipelineInputs inputs = inputsFor(index);
            flags[index] = !inputs.demand_path.empty() && !inputs.placeholder_path.empty()
                && filesPresent({inputs.product_path, inputs.palletPath},
                                "the " + labels[index] + " extract is");
            if (inputs.demand_path.empty() || inputs.placeholder_path.empty()) {
                cerr << "[fixtures] " << directories[index]
                     << " has no 100-STR / 100-PLACEHOLDER file: the " << labels[index]
                     << " extract is skipped\n";
            }
        }
        return flags;
    }();
    return present[day];
}

inline bool anyDayPresent() {
    for (size_t day = 0; day < kDayCount; ++day) {
        if (dayPresent(day)) return true;
    }
    return false;
}

// One Strict run per present day, shared by every test; null for a day not on this machine.
inline const ob::PipelineResult* run(size_t day) {
    static const auto runs = [] {
        PerDay<unique_ptr<ob::PipelineResult>> results;
        for (size_t index = 0; index < kDayCount; ++index) {
            if (dayPresent(index)) {
                results[index] = make_unique<ob::PipelineResult>(ob::Pipeline::run(inputsFor(index)));
            }
        }
        return results;
    }();
    return runs[day].get();
}

} // namespace newExtracts

} // namespace crossDayTests
