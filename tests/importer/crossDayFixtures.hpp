#pragma once

#include "pipeline.hpp"

#include <array>
#include <string>

namespace crossDayTests {

struct DayExpectation {
    std::string label;
    std::string productPath;
    std::string demandPath;
    std::string placeholderPath;
    std::size_t lines;
    std::size_t lanes;
    std::size_t planners;
    std::size_t blankPlanners;
    std::size_t segregatedLines;
    std::size_t strictGroups;
    std::size_t strictLargest;
    std::size_t combinedGroups;
    std::size_t combinedSplitLanes;
    std::size_t combinedLargest;
    std::array<std::size_t, 4> pairCounts;
    std::size_t site2028Lines;
    std::size_t placeholderEntries;
    int trucks;
    int productRows;
    std::size_t productIds;
};

inline const std::array<DayExpectation, 4>& days() {
    static const std::array<DayExpectation, 4> expectations{{
        {"17 Aug", "tests/importer/Customer2-Product-Data.csv", "tests/importer/Demand-1.json",
         "tests/importer/PlaceHolder-1.json", 24357,360,50,1,2849,387,478,365,5,781,
         {1680,902,142,125},2724,189,372,20201,20183},
        {"02 Sep #1", "tests/importer/crossDay/20260902/Product-Data/Customer2-Product-Data.csv",
         "tests/importer/crossDay/20260902/Demands/Demand-1.json",
         "tests/importer/crossDay/20260902/PlaceHolder/Placeholder-1.json",
         23462,368,51,0,2289,395,501,374,6,718,{1409,649,140,91},2198,170,329,20317,20299},
        {"02 Sep #2", "tests/importer/crossDay/20260902/Product-Data/Customer2-Product-Data.csv",
         "tests/importer/crossDay/20260902/Demands/Demand-2.json",
         "tests/importer/crossDay/20260902/PlaceHolder/Placeholder-2.json",
         23003,367,50,0,2283,394,567,373,6,704,{1388,649,154,92},2191,181,349,20317,20299},
        {"03 Sep", "tests/importer/crossDay/20260903/Product-Data/Customer2-Product-Data.csv",
         "tests/importer/crossDay/20260903/Demands/Demand-1.json",
         "tests/importer/crossDay/20260903/PlaceHolder/Placeholder-1.json",
         21909,369,50,0,2279,397,476,375,6,679,{1397,666,138,78},2201,200,397,20317,20299}
    }};
    return expectations;
}

inline const std::array<ob::PipelineResult, 4>& fixtures() {
    static const auto results = [] {
        std::array<ob::PipelineResult, 4> loaded;
        for (std::size_t dayIndex = 0; dayIndex < days().size(); ++dayIndex) {
            const auto& day = days()[dayIndex];
            ob::PipelineInputs inputs;
            inputs.product_path = day.productPath;
            inputs.demand_path = day.demandPath;
            inputs.placeholder_path = day.placeholderPath;
            inputs.planning_day = day.label;
            loaded[dayIndex] = ob::Pipeline::run(inputs);
        }
        return loaded;
    }();
    return results;
}

} // namespace crossDayTests
