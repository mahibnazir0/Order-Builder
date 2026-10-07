#include "doctest.h"
#include "pipeline.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
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

const string kShippedParamsPath = "config/orderBuilderParams.json";

// The shipped params file with its first `from` replaced by `to`, written to path.
string writeEditedParams(const string& path, const string& from, const string& to) {
    ifstream shipped(kShippedParamsPath);
    stringstream text;
    text << shipped.rdbuf();
    string content = text.str();
    const size_t at = content.find(from);
    REQUIRE(at != string::npos);
    content.replace(at, from.size(), to);
    ofstream(path) << content;
    return path;
}

// One stackable line, 16 cases of 8 per unit load at 2 lb a case, with a DATFR_TA but no
// DATTO_TA, so dueBy cannot judge it.
PipelineResult runOneLine(const string& demandRule, const string& palletId = "TLD",
                          const string& paramsPath = kShippedParamsPath) {
    const string productPath = "tests/importer/_tmp_m3_products.csv";
    const string demandPath = "tests/importer/_tmp_m3_demand.json";
    const string placeholderPath = "tests/importer/_tmp_m3_placeholder.json";
    ofstream(productPath) << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
                             "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
                             "GOOD,Good,10,10,10,5,CS,2,4,2,8," << palletId << "\n";
    ofstream(demandPath) << R"({"REQUEST_ID":"t","CTL":[],"DNM":[],"STR":[{"LOCFRNO":"1",)"
                            R"("LOCTONO":"2","MATNR":"GOOD","DATFR_TA":"2026-08-17",)"
                            R"("SHIP_COND":"TL","TRANS":16.0,"UNITOFMEAS":"CS"}]})";
    ofstream(placeholderPath) << R"({"PHOLDER":[]})";
    PipelineInputs inputs;
    inputs.product_path = productPath;
    inputs.demand_path = demandPath;
    inputs.placeholder_path = placeholderPath;
    inputs.paramsPath = paramsPath;
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

TEST_CASE("pipelineM3: a rule that selects no line of a non-empty extract makes the run incomplete") {
    const PipelineResult run = Pipeline::run(realInputs("dueBy:2020-01-01"));
    CHECK(run.demandSelection.selectedLines == 0);
    CHECK(run.demandSelection.undatedLines.empty());
    CHECK(run.floorPlan.excludedLines.empty());
    CHECK_FALSE(isRunComplete(run));
}

TEST_CASE("pipelineM3: a line the demand rule cannot date makes the run incomplete") {
    const PipelineResult run = runOneLine("dueBy:2026-08-19");
    CHECK(run.stackReport.isComplete());
    CHECK(run.demandSelection.undatedLines == vector<size_t>{0});
    CHECK_FALSE(isRunComplete(run));
}

TEST_CASE("pipelineM3: the same line under wholeExtract is counted and the run is complete") {
    const PipelineResult run = runOneLine("wholeExtract");
    CHECK(run.demandSelection.undatedLines.empty());
    CHECK(run.floorPlan.floorTrucks == 1);
    CHECK(isRunComplete(run));
}

TEST_CASE("pipelineM3: with no trailer named, the run plans against the largest one listed") {
    const string paramsPath = writeEditedParams(
        "tests/importer/_tmp_m3_two_trailers.json", "\"trailers\": [",
        R"("trailers": [ { "trailerCode": "48FT_NA", "interiorLengthIn": 570,)"
        R"( "interiorWidthIn": 100, "stackHeightCeilingIn": 100, "weightLimitLb": 40000,)"
        R"( "stackPositions": 28, "maxStackDepth": null },)");
    PipelineInputs inputs = realInputs("dueBy:2026-08-19");
    inputs.paramsPath = paramsPath;
    const PipelineResult run = Pipeline::run(inputs);
    remove(paramsPath.c_str());
    CHECK(run.trailer.trailerCode == "53FT_NA");
    CHECK(run.trailerChoice == TrailerChoice::Largest);
    CHECK(run.floorPlan.floorTrucks == 53);
}

TEST_CASE("pipelineM3: a trailer named on the command line is recorded as named") {
    PipelineInputs inputs = realInputs("dueBy:2026-08-19");
    inputs.trailerCode = "53FT_NA";
    CHECK(Pipeline::run(inputs).trailerChoice == TrailerChoice::Named);
}

TEST_CASE("pipelineM3: Milestone 1 weighs pallets from the params file's pallet table") {
    const string paramsPath = writeEditedParams(
        "tests/importer/_tmp_m3_ptl65.json", R"("palletId": "PTL", "addedWeightLb": 60)",
        R"("palletId": "PTL", "addedWeightLb": 65)");
    const PipelineResult run = runOneLine("wholeExtract", "PTL", paramsPath);
    remove(paramsPath.c_str());
    REQUIRE(run.weight_per_line.size() == 1);
    CHECK(run.weight_per_line[0] == doctest::Approx(2.0 * (2.0 * 8.0 + 65.0)));
    CHECK(run.validation.unrecognized_pallet_id == 0);
}

TEST_CASE("pipelineM3: a pallet type only the params file lists is not reported as unrecognized") {
    const string paramsPath = writeEditedParams(
        "tests/importer/_tmp_m3_eur.json", "\"pallets\": [",
        R"("pallets": [ { "palletId": "EUR", "addedWeightLb": 55, "addedHeightIn": 5.7,)"
        R"( "footprintLengthIn": 47.2, "footprintWidthIn": 31.5 },)");
    const PipelineResult run = runOneLine("wholeExtract", "EUR", paramsPath);
    remove(paramsPath.c_str());
    CHECK(run.validation.unrecognized_pallet_id == 0);
    REQUIRE(run.weight_per_line.size() == 1);
    CHECK(run.weight_per_line[0] == doctest::Approx(2.0 * (2.0 * 8.0 + 55.0)));
}
