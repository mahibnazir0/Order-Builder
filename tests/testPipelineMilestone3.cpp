#include "doctest.h"
#include "pipeline.hpp"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;

namespace {

PipelineInputs realInputs(const string& demandRule) {
    PipelineInputs inputs;
    inputs.product_path = "tests/importer/Customer2-Product-Data.csv";
    inputs.demand_path = "tests/importer/Demand-1.json";
    inputs.placeholder_path = "tests/importer/PlaceHolder-1.json";
    inputs.paramsPath = "config/orderBuilderParams.json";
    if (!demandRule.empty()) inputs.demandSelector = parseDemandSelector(demandRule);
    return inputs;
}

const PipelineResult& wholeExtractRun() {
    static const PipelineResult run = Pipeline::run(realInputs("wholeExtract"));
    return run;
}

// One stackable line with a DATFR_TA but no DATTO_TA, so dueBy cannot judge it.
PipelineResult runLineWithoutDueDate(const string& demandRule) {
    const string productPath = "tests/importer/_tmp_m3_products.csv";
    const string demandPath = "tests/importer/_tmp_m3_demand.json";
    const string placeholderPath = "tests/importer/_tmp_m3_placeholder.json";
    ofstream(productPath) << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
                             "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
                             "GOOD,Good,10,10,10,5,CS,2,4,2,8,TLD\n";
    ofstream(demandPath) << R"({"REQUEST_ID":"t","CTL":[],"DNM":[],"STR":[{"LOCFRNO":"1",)"
                            R"("LOCTONO":"2","MATNR":"GOOD","DATFR_TA":"2026-08-17",)"
                            R"("SHIP_COND":"TL","TRANS":16.0,"UNITOFMEAS":"CS"}]})";
    ofstream(placeholderPath) << R"({"PHOLDER":[]})";
    PipelineInputs inputs;
    inputs.product_path = productPath;
    inputs.demand_path = demandPath;
    inputs.placeholder_path = placeholderPath;
    inputs.paramsPath = "config/orderBuilderParams.json";
    inputs.demandSelector = parseDemandSelector(demandRule);
    PipelineResult result = Pipeline::run(inputs);
    for (const auto& path : {productPath, demandPath, placeholderPath}) remove(path.c_str());
    return result;
}

} // namespace

TEST_CASE("pipelineM3: without a demand rule no floor is planned") {
    const PipelineResult run = Pipeline::run(realInputs(""));
    CHECK(run.ranMilestone2);
    CHECK_FALSE(run.ranFloor);
    CHECK(run.floorPlan.groups.empty());
    CHECK(run.floorPlan.floorTrucks == 0);
}

TEST_CASE("pipelineM3: a demand rule without a params file throws") {
    PipelineInputs inputs = realInputs("wholeExtract");
    inputs.paramsPath.clear();
    CHECK_THROWS_AS(Pipeline::run(inputs), runtime_error);
}

TEST_CASE("pipelineM3: the floor is planFloor on the run's own groups, lines and trailer") {
    const PipelineResult& run = wholeExtractRun();
    REQUIRE(run.ranFloor);
    const DemandSelection selection =
        selectDemand(run.demand.str, parseDemandSelector("wholeExtract"));
    const FloorPlan expected =
        planFloor(run.segregation, run.join.lines, selection, run.params, run.trailer);
    CHECK(run.floorPlan.floorTrucks == expected.floorTrucks);
    CHECK(run.floorPlan.boundTrucks == doctest::Approx(expected.boundTrucks));
    CHECK(run.floorPlan.lanes.size() == expected.lanes.size());
    CHECK(run.floorPlan.groups.size() == run.segregation.groups.size());
}

TEST_CASE("pipelineM3: the real day's whole-extract floor is pinned") {
    const PipelineResult& run = wholeExtractRun();
    CHECK(run.demandSelection.selectedLines == 24357);
    CHECK(run.floorPlan.lanes.size() == 360);
    CHECK(run.floorPlan.floorTrucks == 3900);
    CHECK(run.floorPlan.noStackingBaselineTrucks == 4981);
    CHECK(isRunComplete(run));
}

TEST_CASE("pipelineM3: the floor counts exactly the lines segregation grouped") {
    const PipelineResult& run = wholeExtractRun();
    size_t groupedLines = 0;
    for (const SegregatedGroup& group : run.segregation.groups) groupedLines += group.lineIndices.size();
    size_t selectedInGroups = 0;
    for (const GroupFloor& group : run.floorPlan.groups) selectedInGroups += group.linesSelected;
    CHECK(selectedInGroups + run.floorPlan.linesNotSelected == groupedLines);
}

TEST_CASE("pipelineM3: planning the floor changes no Milestone 2 figure") {
    const PipelineResult withoutFloor = Pipeline::run(realInputs(""));
    const PipelineResult& withFloor = wholeExtractRun();
    CHECK(withFloor.segregation.groups.size() == withoutFloor.segregation.groups.size());
    REQUIRE(withFloor.stacking.groups.size() == withoutFloor.stacking.groups.size());
    for (size_t groupIndex = 0; groupIndex < withFloor.stacking.groups.size(); ++groupIndex) {
        CHECK(withFloor.stacking.groups[groupIndex].best.floorPositions
              == withoutFloor.stacking.groups[groupIndex].best.floorPositions);
    }
    CHECK(withFloor.stackReport.isComplete() == withoutFloor.stackReport.isComplete());
}

TEST_CASE("pipelineM3: the floor is planned against the trailer the run selected") {
    PipelineInputs inputs = realInputs("dueBy:2026-08-19");
    inputs.trailerCode = "53FT_NA";
    const PipelineResult run = Pipeline::run(inputs);
    CHECK(run.trailer.trailerCode == "53FT_NA");
    CHECK(run.floorPlan.floorTrucks == 53);
}

TEST_CASE("pipelineM3: a narrower demand rule selects fewer lines and a lower floor") {
    const PipelineResult dueBy = Pipeline::run(realInputs("dueBy:2026-08-19"));
    CHECK(dueBy.demandSelection.selectedLines == 94);
    CHECK(dueBy.floorPlan.floorTrucks < wholeExtractRun().floorPlan.floorTrucks);
}

TEST_CASE("pipelineM3: a line the demand rule cannot date makes the run incomplete") {
    const PipelineResult run = runLineWithoutDueDate("dueBy:2026-08-19");
    CHECK(run.stackReport.isComplete());
    CHECK(run.demandSelection.undatedLines == vector<size_t>{0});
    CHECK_FALSE(isRunComplete(run));
}

TEST_CASE("pipelineM3: the same line under wholeExtract is counted and the run is complete") {
    const PipelineResult run = runLineWithoutDueDate("wholeExtract");
    CHECK(run.demandSelection.undatedLines.empty());
    CHECK(run.floorPlan.floorTrucks == 1);
    CHECK(isRunComplete(run));
}
