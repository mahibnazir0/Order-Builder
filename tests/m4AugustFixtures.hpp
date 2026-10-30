#pragma once

#include "importer/crossDayFixtures.hpp"
#include "pipeline.hpp"
#include "reservationBook.hpp"
#include "selectionItem.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <cstddef>
#include <string>

// The 4-6 August runs as the M4 modules see them: the pipeline through Milestone 2, the
// reservation book, and the selection items. Confidential and gitignored, so a fresh clone
// skips these tests rather than failing them. Built once per test binary.
namespace m4AugustTests {

struct AugustRunFiles {
    std::string demandPath;
    std::string placeholderPath;
};

inline const std::array<AugustRunFiles, 3> kAugustRunFiles{{
    {"tests/importer/august/inputs/Aug4/Demand-Aug5-2.json", "tests/importer/august/inputs/Aug4/PH-Aug4.json"},
    {"tests/importer/august/inputs/Aug5/Demand-Aug5.json", "tests/importer/august/inputs/Aug5/PH-Aug5.json"},
    {"tests/importer/august/inputs/Aug6/Demand-Aug6.json", "tests/importer/august/inputs/Aug6/PH-Aug6.json"},
}};
inline const std::string kAugustProduct = "tests/importer/august/inputs/shared/Product-Data.csv";

// Truck Builder's solved loads for each run, the client's own answer to the same demand.
inline const std::array<std::string, 3> kAugustSolutionDirectories{
    "tests/importer/august/results/Aug4/raw-json", "tests/importer/august/results/Aug5/raw-json",
    "tests/importer/august/results/Aug6/raw-json"};

inline bool augustSolutionsPresent() {
    static const bool present = [] {
        for (const std::string& directory : kAugustSolutionDirectories) {
            if (!std::filesystem::is_directory(directory)) {
                std::cerr << "[fixtures] " << directory << " not found: the client comparison is skipped\n";
                return false;
            }
        }
        return true;
    }();
    return present;
}

inline bool augustPresent() {
    static const bool present = crossDayTests::filesPresent(
        {kAugustProduct, crossDayTests::kPalletTableForOlderMasters,
         crossDayTests::kLayerSizesForOlderMasters, kAugustRunFiles[0].demandPath,
         kAugustRunFiles[0].placeholderPath, kAugustRunFiles[1].demandPath,
         kAugustRunFiles[1].placeholderPath, kAugustRunFiles[2].demandPath,
         kAugustRunFiles[2].placeholderPath},
        "4-6 Aug M4 tests are");
    return present;
}

struct AugustItems {
    ob::PipelineResult run;
    ob::ReservationBook book;
    ob::SelectionItems built;
};

inline const AugustItems& augustItems(std::size_t runIndex) {
    static const std::array<AugustItems, 3> runs = [] {
        std::array<AugustItems, 3> loaded;
        for (std::size_t index = 0; index < loaded.size(); ++index) {
            ob::PipelineInputs inputs;
            inputs.product_path = kAugustProduct;
            inputs.demand_path = kAugustRunFiles[index].demandPath;
            inputs.placeholder_path = kAugustRunFiles[index].placeholderPath;
            inputs.paramsPath = crossDayTests::kStrictParamsPath;
            inputs.palletPath = crossDayTests::kPalletTableForOlderMasters;
            inputs.layerSizePath = crossDayTests::kLayerSizesForOlderMasters;
            loaded[index].run = ob::Pipeline::run(inputs);
            loaded[index].book = ob::buildReservationBook(
                loaded[index].run.placeholders.placeholders, loaded[index].run.demand.ctl,
                ob::planDateFromRequestId(loaded[index].run.demand.request_id), {"53F", {"53F"}});
            loaded[index].built = ob::buildSelectionItems(loaded[index].run, loaded[index].book.window);
        }
        return loaded;
    }();
    return runs[runIndex];
}

} // namespace m4AugustTests
