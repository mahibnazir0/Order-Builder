#pragma once

#include "pipeline.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

// The four Customer2 extracts: 17 Aug (flat in tests/importer) and the September
// days, which keep the client's own directory layout under tests/importer/crossDay.
// Every file is confidential client data and gitignored; none is ever committed.
//
// Each expectation below is one figure across the four days, in dayFiles() order,
// so it reads the same way as the measured table it was taken from.
namespace crossDayTests {

constexpr std::size_t kDayCount = 4;

template <typename T>
using PerDay = std::array<T, kDayCount>;

struct DayFiles {
    std::string label;
    std::string productPath;
    std::string demandPath;
    std::string placeholderDirectory;
    // Exactly as the client shipped it: "PlaceHolder-1.json" in August,
    // "Placeholder-N.json" in September. Not normalised; the case breaks on Linux.
    std::string placeholderFileName;

    std::string placeholderPath() const { return placeholderDirectory + "/" + placeholderFileName; }
};

inline const PerDay<DayFiles>& dayFiles() {
    static const PerDay<DayFiles> files{{
        {"17 Aug", "tests/importer/Customer2-Product-Data.csv", "tests/importer/Demand-1.json",
         "tests/importer", "PlaceHolder-1.json"},
        {"02 Sep #1", "tests/importer/crossDay/20260902/Product-Data/Customer2-Product-Data.csv",
         "tests/importer/crossDay/20260902/Demands/Demand-1.json",
         "tests/importer/crossDay/20260902/PlaceHolder", "Placeholder-1.json"},
        {"02 Sep #2", "tests/importer/crossDay/20260902/Product-Data/Customer2-Product-Data.csv",
         "tests/importer/crossDay/20260902/Demands/Demand-2.json",
         "tests/importer/crossDay/20260902/PlaceHolder", "Placeholder-2.json"},
        {"03 Sep", "tests/importer/crossDay/20260903/Product-Data/Customer2-Product-Data.csv",
         "tests/importer/crossDay/20260903/Demands/Demand-1.json",
         "tests/importer/crossDay/20260903/PlaceHolder", "Placeholder-1.json"},
    }};
    return files;
}

// The extracts are confidential and gitignored, so a fresh clone has none of the
// September days. Tests that need all four are skipped rather than failed there;
// the notice keeps the skip visible instead of reading as a pass.
inline bool allExtractsPresent() {
    static const bool present = [] {
        for (const DayFiles& day : dayFiles()) {
            for (const std::string& path : {day.productPath, day.demandPath, day.placeholderPath()}) {
                if (!std::filesystem::is_regular_file(path)) {
                    std::cerr << "[crossDay] " << path
                              << " not found: cross-day tests are skipped\n";
                    return false;
                }
            }
        }
        return true;
    }();
    return present;
}

// Milestone 1, measured through the CLI on 26 Sep 2026.
namespace expectedM1 {
constexpr PerDay<std::size_t> demandLines{24357, 23462, 23003, 21909};
constexpr PerDay<double> hashTotal{8708934, 8275859, 8106348, 7778017};
constexpr PerDay<double> palletEquivalents{152911.2, 149391.3, 146023.0, 139678.5};
constexpr PerDay<double> totalWeightLb{103005833, 98793082, 95668859, 91591555};
constexpr PerDay<int> lanesUnion{371, 380, 383, 377};
constexpr PerDay<int> validationErrors{0, 0, 0, 0};
constexpr PerDay<int> validationWarnings{161, 204, 207, 169};
constexpr PerDay<std::size_t> placeholderEntries{189, 170, 181, 200};
constexpr PerDay<long long> trucksRequested{372, 329, 349, 397};
constexpr PerDay<int> productRows{20201, 20317, 20317, 20317};
constexpr PerDay<std::size_t> uniqueProductIds{20183, 20299, 20299, 20299};
} // namespace expectedM1

// Milestone 2, Strict reading, measured through the CLI on 26 Sep 2026.
namespace expectedM2 {
constexpr PerDay<std::size_t> lanesWithDemand{360, 368, 367, 369};
constexpr PerDay<std::size_t> strictGroups{387, 395, 394, 397};
// A per-day measurement, not a property of the customer: 17 on each of these four days,
// but 19 or 20 on every extract from 29 Sep to 5 Oct.
constexpr PerDay<std::size_t> strictLanesSplit{17, 17, 17, 17};
constexpr PerDay<std::size_t> linesSegregated{2849, 2289, 2283, 2279};
// Pass 1 cube/weight split. Depends on the trailer's stackPositions, which was 30
// until the client corrected it to 32; these figures hold only at 32.
constexpr int stackPositionsMeasuredAt = 32;
constexpr PerDay<std::size_t> cubeBoundGroups{383, 394, 392, 397};
constexpr PerDay<std::size_t> weightBoundGroups{4, 1, 2, 0};
constexpr PerDay<std::size_t> groupsAllSingleHigh{234, 232, 228, 234};
constexpr PerDay<std::size_t> site2028Lines{2724, 2198, 2191, 2201};
// A one-off in the 17 Aug extract, not a property of the customer.
constexpr PerDay<std::size_t> blankPlannerLines{1, 0, 0, 0};
// FlaggedVsNormal reading, measured from the raw files on 27 Sep 2026. Lines segregated
// is the same under both readings: the reading changes grouping, never flagging.
constexpr PerDay<std::size_t> flaggedVsNormalGroups{365, 374, 373, 375};
constexpr PerDay<std::size_t> flaggedVsNormalLanesSplit{5, 6, 6, 6};
constexpr PerDay<std::size_t> flaggedVsNormalLargestGroup{781, 718, 704, 679};
} // namespace expectedM2

// Figures from the pre-rewrite cross-day tests (feature/m2-module-2-segregation),
// not part of the 26 Sep re-measurement.
namespace expectedRecovered {
constexpr PerDay<std::size_t> distinctPlanners{50, 51, 50, 50};
constexpr PerDay<std::size_t> strictLargestGroup{478, 501, 567, 476};
} // namespace expectedRecovered

const std::string kStrictParamsPath = "config/orderBuilderParams.json";

inline ob::PipelineInputs dayInputs(std::size_t dayIndex, const std::string& paramsPath) {
    const DayFiles& day = dayFiles()[dayIndex];
    ob::PipelineInputs inputs;
    inputs.product_path = day.productPath;
    inputs.demand_path = day.demandPath;
    inputs.placeholder_path = day.placeholderPath();
    inputs.planning_day = day.label;
    inputs.paramsPath = paramsPath;
    return inputs;
}

inline PerDay<ob::PipelineResult> runAllDays(const std::string& paramsPath) {
    PerDay<ob::PipelineResult> loaded;
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
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
inline std::string writeFlaggedVsNormalParams() {
    std::ifstream shipped(kStrictParamsPath);
    std::stringstream buffer;
    buffer << shipped.rdbuf();
    std::string text = buffer.str();
    const std::string strictSetting = "\"doNotMixReading\": \"Strict\"";
    const auto position = text.find(strictSetting);
    if (position == std::string::npos) {
        throw std::runtime_error("crossDayFixtures: " + kStrictParamsPath + " has no " + strictSetting);
    }
    text.replace(position, strictSetting.size(), "\"doNotMixReading\": \"FlaggedVsNormal\"");
    const auto path = std::filesystem::temp_directory_path() / "obFlaggedVsNormalParams.json";
    std::ofstream(path) << text;
    return path.string();
}

inline const PerDay<ob::PipelineResult>& flaggedVsNormalRuns() {
    static const auto results = [] {
        const std::string paramsPath = writeFlaggedVsNormalParams();
        auto loaded = runAllDays(paramsPath);
        std::filesystem::remove(paramsPath);
        return loaded;
    }();
    return results;
}

} // namespace crossDayTests
