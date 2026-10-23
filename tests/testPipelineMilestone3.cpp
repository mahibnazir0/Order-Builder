#include "doctest.h"
#include "importer/crossDayFixtures.hpp"
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
    inputs.palletPath = crossDayTests::kPalletTableForOlderMasters;
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

const string kTldPallet = "TLD,48,40,0.1,1\n";

// One stackable line of `casesDemanded` cases, 8 per 20-inch unit load at 2 lb a case, with a
// DATFR_TA but no DATTO_TA, so dueBy cannot judge it. The pallet table holds `palletRows`.
PipelineResult runOneLine(const string& demandRule, const string& palletId = "TLD",
                          const string& palletRows = kTldPallet, double casesDemanded = 16.0) {
    const string productPath = "tests/importer/_tmp_m3_products.csv";
    const string demandPath = "tests/importer/_tmp_m3_demand.json";
    const string placeholderPath = "tests/importer/_tmp_m3_placeholder.json";
    const string palletPath = "tests/importer/_tmp_m3_pallets.csv";
    ofstream(palletPath) << "ID,Footprint_Length,Footprint_Width,Height,Weight\n" << palletRows;
    ofstream(productPath) << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
                             "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
                             "GOOD,Good,10,10,10,5,CS,2,4,2,8," << palletId << "\n";
    ofstream(demandPath) << R"({"REQUEST_ID":"t","CTL":[],"DNM":[],"STR":[{"LOCFRNO":"1",)"
                            R"("LOCTONO":"2","MATNR":"GOOD","DATFR_TA":"2026-08-17",)"
                            R"("SHIP_COND":"TL","TRANS":)" << casesDemanded
                         << R"(,"UNITOFMEAS":"CS"}]})";
    ofstream(placeholderPath) << R"({"PHOLDER":[]})";
    PipelineInputs inputs;
    inputs.product_path = productPath;
    inputs.demand_path = demandPath;
    inputs.placeholder_path = placeholderPath;
    inputs.paramsPath = kShippedParamsPath;
    inputs.palletPath = palletPath;
    inputs.demandSelector = parseDemandSelector(demandRule);
    PipelineResult result = Pipeline::run(inputs);
    for (const auto& path : {productPath, demandPath, placeholderPath, palletPath}) remove(path.c_str());
    return result;
}


const string kLayerDonorHeader = "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
                                 "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID,"
                                 "Layer_Length,Layer_Width\n";

// The one-line run of runOneLine on a TLD pallet, two unit loads, with the product's own layer
// cells (blank for none) and a donor master to borrow layer sizes from.
PipelineResult runOneLineBorrowingLayers(const string& ownLayerCells, const string& donorText) {
    const string productPath = "tests/importer/_tmp_m3_layer_products.csv";
    const string donorPath = "tests/importer/_tmp_m3_layer_donor.csv";
    const string demandPath = "tests/importer/_tmp_m3_layer_demand.json";
    const string placeholderPath = "tests/importer/_tmp_m3_layer_placeholder.json";
    const string palletPath = "tests/importer/_tmp_m3_layer_pallets.csv";
    ofstream(palletPath) << "ID,Footprint_Length,Footprint_Width,Height,Weight\n" << kTldPallet;
    ofstream(productPath) << kLayerDonorHeader << "GOOD,Good,10,10,10,5,CS,2,4,2,8,TLD,"
                          << ownLayerCells << "\n";
    ofstream(donorPath) << donorText;
    ofstream(demandPath) << R"({"REQUEST_ID":"t","CTL":[],"DNM":[],"STR":[{"LOCFRNO":"1",)"
                            R"("LOCTONO":"2","MATNR":"GOOD","DATFR_TA":"2026-08-17",)"
                            R"("SHIP_COND":"TL","TRANS":16,"UNITOFMEAS":"CS"}]})";
    ofstream(placeholderPath) << R"({"PHOLDER":[]})";
    PipelineInputs inputs;
    inputs.product_path = productPath;
    inputs.demand_path = demandPath;
    inputs.placeholder_path = placeholderPath;
    inputs.paramsPath = kShippedParamsPath;
    inputs.palletPath = palletPath;
    inputs.layerSizePath = donorPath;
    inputs.demandSelector = parseDemandSelector("wholeExtract");
    struct RemoveFiles {
        vector<string> paths;
        ~RemoveFiles() { for (const auto& path : paths) remove(path.c_str()); }
    } cleanup{{productPath, donorPath, demandPath, placeholderPath, palletPath}};
    return Pipeline::run(inputs);
}

} // namespace

TEST_CASE("pipelineM3: without a demand rule no floor is planned" * doctest::skip(!crossDayTests::august17StackingPresent())) {
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

TEST_CASE("pipelineM3: 200 unit loads of 20 in on 48 x 40 pallets floor at 2 trucks, bound by volume") {
    const PipelineResult run = runOneLine("wholeExtract", "TLD", kTldPallet, 1600.0);
    REQUIRE(run.ranFloor);
    CHECK(run.floorPlan.totals.unitLoads == doctest::Approx(200.0));
    CHECK(run.floorPlan.totals.stackedInches == doctest::Approx(4000.0));
    CHECK(run.floorPlan.totals.totalWeightLb == doctest::Approx(200.0 * (8 * 2.0 + 1.0)));
    REQUIRE(run.floorPlan.groups.size() == 1);
    CHECK(run.floorPlan.groups[0].bound.binding == FloorTerm::Volume);
    CHECK(run.floorPlan.totals.floorAreaSqIn == doctest::Approx(200.0 * 48.0 * 40.0));
    CHECK(run.floorPlan.boundTrucks == doctest::Approx(200.0 * 48.0 * 40.0 * 20.0 / (630.0 * 100.0 * 108.0)));
    CHECK(run.floorPlan.floorTrucks == 2);
    CHECK(run.floorPlan.noStackingBaselineTrucks == 7);
}

// A regression pin, not an independent measurement: these are the figures this code printed for
// 17 Aug when the floor was signed off, so any change to them has to be explained.
TEST_CASE("pipelineM3: the real day's whole-extract floor matches the signed-off run" * doctest::skip(!crossDayTests::august17StackingPresent())) {
    const PipelineResult& run = wholeExtractRun();
    CHECK(run.demandSelection.selectedLines == 24357);
    CHECK(run.floorPlan.lanes.size() == 360);
    // 3,900 when wood pallets weighed the configured 60 lb; the pallet table's 65 lb added one.
    // Counting the 6 in wood deck (client ruling, 12 Oct) added 80 more, to 3,981. Taking
    // capacity from floor area and volume instead of 32 positions (client, 12 Oct) gave 3,886.
    // Reading DIS quantities as cases (client, 20 Oct) gave 3,796; dividing PAL by
    // Cases_Unit_Load as well gives 3,754.
    CHECK(run.floorPlan.floorTrucks == 3754);
    CHECK(run.floorPlan.noStackingBaselineTrucks == 4627);
    CHECK(runOutcome(run) == RunOutcome::Complete);
}

TEST_CASE("pipelineM3: the floor counts exactly the lines segregation grouped" * doctest::skip(!crossDayTests::august17StackingPresent())) {
    const PipelineResult& run = wholeExtractRun();
    size_t groupedLines = 0;
    for (const SegregatedGroup& group : run.segregation.groups) groupedLines += group.lineIndices.size();
    size_t selectedInGroups = 0;
    for (const GroupFloor& group : run.floorPlan.groups) selectedInGroups += group.linesSelected;
    CHECK(selectedInGroups + run.floorPlan.linesNotSelected == groupedLines);
}

TEST_CASE("pipelineM3: planning the floor changes no Milestone 2 figure" * doctest::skip(!crossDayTests::august17StackingPresent())) {
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

TEST_CASE("pipelineM3: the floor is planned against the trailer the run selected" * doctest::skip(!crossDayTests::august17StackingPresent())) {
    PipelineInputs inputs = realInputs("dueBy:2026-08-19");
    inputs.trailerCode = "53FT_NA";
    const PipelineResult run = Pipeline::run(inputs);
    CHECK(run.trailer.trailerCode == "53FT_NA");
    CHECK(run.floorPlan.floorTrucks == 53);
}

TEST_CASE("pipelineM3: a narrower demand rule selects fewer lines and a lower floor" * doctest::skip(!crossDayTests::august17StackingPresent())) {
    const PipelineResult dueBy = Pipeline::run(realInputs("dueBy:2026-08-19"));
    CHECK(dueBy.demandSelection.selectedLines == 94);
    CHECK(dueBy.floorPlan.floorTrucks < wholeExtractRun().floorPlan.floorTrucks);
}

TEST_CASE("pipelineM3: a rule that selects no line of a non-empty extract makes the run incomplete" * doctest::skip(!crossDayTests::august17StackingPresent())) {
    const PipelineResult run = Pipeline::run(realInputs("dueBy:2020-01-01"));
    CHECK(run.demandSelection.selectedLines == 0);
    CHECK(run.demandSelection.undatedLines.empty());
    CHECK(run.floorPlan.excludedLines.empty());
    CHECK(runOutcome(run) == RunOutcome::NothingSelected);
}

TEST_CASE("pipelineM3: a line the demand rule cannot date makes the run incomplete") {
    const PipelineResult run = runOneLine("dueBy:2026-08-19");
    CHECK(run.stackReport.isComplete());
    CHECK(run.demandSelection.undatedLines == vector<size_t>{0});
    CHECK(runOutcome(run) == RunOutcome::FloorIncomplete);
}

TEST_CASE("pipelineM3: the same line under wholeExtract is counted and the run is complete") {
    const PipelineResult run = runOneLine("wholeExtract");
    CHECK(run.demandSelection.undatedLines.empty());
    CHECK(run.floorPlan.floorTrucks == 1);
    CHECK(runOutcome(run) == RunOutcome::Complete);
}

TEST_CASE("pipelineM3: with no trailer named, the run plans against the largest one listed" * doctest::skip(!crossDayTests::august17StackingPresent())) {
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

TEST_CASE("pipelineM3: a trailer named on the command line is recorded as named" * doctest::skip(!crossDayTests::august17StackingPresent())) {
    PipelineInputs inputs = realInputs("dueBy:2026-08-19");
    inputs.trailerCode = "53FT_NA";
    CHECK(Pipeline::run(inputs).trailerChoice == TrailerChoice::Named);
}

TEST_CASE("pipelineM3: the Milestone 2 weight total equals the floor's total weight" * doctest::skip(!crossDayTests::august17StackingPresent())) {
    const PipelineResult& run = wholeExtractRun();
    CHECK(run.stackReport.totalWeightLb == doctest::Approx(run.floorPlan.totals.totalWeightLb).epsilon(1e-12));
}

TEST_CASE("pipelineM3: the floor and Milestone 1 both weigh pallets from the pallet table") {
    const PipelineResult run = runOneLine("wholeExtract", "PTL", "PTL,48,40,6,70\n");
    REQUIRE(run.weight_per_line.size() == 1);
    CHECK(run.weight_per_line[0] == doctest::Approx(2.0 * (2.0 * 8.0 + 70.0)));
    CHECK(run.floorPlan.totals.totalWeightLb == doctest::Approx(2.0 * (2.0 * 8.0 + 70.0)));
    CHECK(run.floorPlan.totals.totalWeightLb == doctest::Approx(run.weightForStacking[0]));
}

TEST_CASE("pipelineM3: a pallet type only the pallet table lists reaches the floor") {
    const PipelineResult run = runOneLine("wholeExtract", "EUR", "EUR,47.2,31.5,5.7,55\n");
    CHECK(run.floorPlan.excludedLines.empty());
    CHECK(run.floorPlan.totals.totalWeightLb == doctest::Approx(2.0 * (2.0 * 8.0 + 55.0)));
    CHECK(run.floorPlan.floorTrucks == 1);
}

TEST_CASE("pipelineM3: a product with no layer size borrows one by ID and Pallet_ID") {
    const PipelineResult run = runOneLineBorrowingLayers(
        ",", kLayerDonorHeader + "GOOD,Good,10,10,10,5,CS,2,4,2,8,TLD,30,20\n");
    CHECK(run.layerSizesBorrowed == 1);
    CHECK(run.floorPlan.linesWithoutLayerSize == 0);
    CHECK(run.floorPlan.totals.floorAreaSqIn == doctest::Approx(2.0 * 30.0 * 20.0));
}

TEST_CASE("pipelineM3: a donor row on another pallet type lends nothing") {
    const PipelineResult run = runOneLineBorrowingLayers(
        ",", kLayerDonorHeader + "GOOD,Good,10,10,10,5,CS,2,4,2,8,PTL,30,20\n");
    CHECK(run.layerSizesBorrowed == 0);
    CHECK(run.floorPlan.linesWithoutLayerSize == 1);
    CHECK(run.floorPlan.totals.floorAreaSqIn == doctest::Approx(2.0 * 48.0 * 40.0));
}

TEST_CASE("pipelineM3: a product's own layer size is kept over the donor's") {
    const PipelineResult run = runOneLineBorrowingLayers(
        "36,24", kLayerDonorHeader + "GOOD,Good,10,10,10,5,CS,2,4,2,8,TLD,30,20\n");
    CHECK(run.layerSizesBorrowed == 0);
    CHECK(run.floorPlan.totals.floorAreaSqIn == doctest::Approx(2.0 * 36.0 * 24.0));
}

TEST_CASE("pipelineM3: a layer-size file with no layer sizes stops the run") {
    CHECK_THROWS_AS(runOneLineBorrowingLayers(
                        ",", "ID,Description,Length,Width,Height,Strength,UoM,Weight,Cases_Layer,"
                             "Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
                             "GOOD,Good,10,10,10,5,CS,2,4,2,8,TLD\n"),
                    runtime_error);
}

TEST_CASE("pipelineM3: a borrowed layer size that is unreadable leaves the line out of the floor") {
    const PipelineResult run = runOneLineBorrowingLayers(
        ",", kLayerDonorHeader + "GOOD,Good,10,10,10,5,CS,2,4,2,8,TLD,wide,20\n");
    CHECK(run.layerSizesBorrowed == 1);
    REQUIRE(run.floorPlan.excludedLines.size() == 1);
    CHECK(run.floorPlan.excludedLines[0].error == UnitLoadMetricsError::InvalidLayerSize);
}
