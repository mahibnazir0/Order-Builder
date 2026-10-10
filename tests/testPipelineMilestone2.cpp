#include "doctest.h"
#include "importer/crossDayFixtures.hpp"
#include "pipeline.hpp"
#include "validator.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace std;

using namespace ob;

namespace {

PipelineInputs realInputs(bool withParams) {
    PipelineInputs inputs;
    inputs.product_path = "tests/importer/Customer2-Product-Data.csv";
    inputs.demand_path = "tests/importer/Demand-1.json";
    inputs.placeholder_path = "tests/importer/PlaceHolder-1.json";
    if (withParams) {
        inputs.paramsPath = "config/orderBuilderParams.json";
        inputs.palletPath = crossDayTests::kPalletTableForOlderMasters;
    }
    return inputs;
}

const string kPalletTableHeader = "ID,Footprint_Length,Footprint_Width,Height,Weight\n";

string writePalletTable(const string& path, const string& rows) {
    ofstream(path) << kPalletTableHeader << rows;
    return path;
}

ValidationIssue issue(ValidationIssue::Severity severity, const std::string& rule, int lineIndex) {
    ValidationIssue validationIssue;
    validationIssue.severity = severity;
    validationIssue.rule = rule;
    validationIssue.line_index = lineIndex;
    return validationIssue;
}

// Runs the pipeline on one product row and one demand line, written to scratch files, under
// the shipped params and a pallet table holding `palletRows`.
const string kProductHeader = "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
                              "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID";

PipelineResult runOneLine(const std::string& name, const std::string& productRow,
                          const std::string& demandLine, const std::string& palletRows,
                          const string& productHeader = kProductHeader) {
    const std::string productPath = "tests/importer/_tmp_" + name + "_products.csv";
    const std::string demandPath = "tests/importer/_tmp_" + name + "_demand.json";
    const std::string placeholderPath = "tests/importer/_tmp_" + name + "_placeholder.json";
    const string palletPath = writePalletTable("tests/importer/_tmp_" + name + "_pallets.csv", palletRows);
    std::ofstream(productPath) << productHeader << "\n" << productRow << "\n";
    std::ofstream(demandPath) << R"({"REQUEST_ID":"t","CTL":[],"DNM":[],"STR":[)" << demandLine << "]}";
    std::ofstream(placeholderPath) << R"({"PHOLDER":[]})";
    PipelineInputs inputs;
    inputs.product_path = productPath;
    inputs.demand_path = demandPath;
    inputs.placeholder_path = placeholderPath;
    inputs.paramsPath = "config/orderBuilderParams.json";
    inputs.palletPath = palletPath;
    PipelineResult result = Pipeline::run(inputs);
    for (const auto& path : {productPath, demandPath, placeholderPath, palletPath}) std::remove(path.c_str());
    return result;
}

string ptlPalletWeighing(int weightLb) {
    return "PTL,48,40,5.5," + to_string(weightLb) + "\n";
}

const string kTldPallet = "TLD,48,40,0.1,1\n";

const std::string kOnePalletLine =
    R"({"LOCFRNO":"1","LOCTONO":"2","MATNR":"P","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":1.0,"UNITOFMEAS":"PAL"})";

} // namespace

TEST_CASE("validator: lines with errors or the zero-dimension ruling are flagged as excluded") {
    ValidationReport report;
    report.issues = {issue(ValidationIssue::Severity::Error, "unknown_uom", 0),
                     issue(ValidationIssue::Severity::Warning, "zero_dimension", 1),
                     issue(ValidationIssue::Severity::Warning, "ambiguous_pallet", 2)};
    const std::vector<bool> excluded = Validator::excludedLineFlags(report, 4);
    CHECK(excluded == std::vector<bool>{true, true, false, false});
}

TEST_CASE("validator: excluded flags ignore issues outside the line range") {
    ValidationReport report;
    report.issues = {issue(ValidationIssue::Severity::Error, "unknown_uom", -1),
                     issue(ValidationIssue::Severity::Error, "unknown_uom", 3)};
    CHECK(Validator::excludedLineFlags(report, 3) == std::vector<bool>{false, false, false});
    CHECK(Validator::excludedLineFlags(report, 0).empty());
}

TEST_CASE("pipeline: without a params file only Milestone 1 runs" * doctest::skip(!crossDayTests::august17Present())) {
    const PipelineResult result = Pipeline::run(realInputs(false));
    CHECK_FALSE(result.ranMilestone2);
    CHECK(result.segregation.groups.empty());
    CHECK(result.stacking.groups.empty());
    CHECK(result.summary.total_demand_lines == 24357);
}

TEST_CASE("pipeline: with a params file every stage runs on the real day" * doctest::skip(!crossDayTests::august17StackingPresent())) {
    const PipelineResult result = Pipeline::run(realInputs(true));
    REQUIRE(result.ranMilestone2);
    CHECK(result.missingPalletIds.empty());
    CHECK(result.segregation.linesIn == 24357);
    CHECK(result.segregation.linesExcluded == 0);
    CHECK(result.segregation.groups.size() == 387);
    CHECK(result.segregation.lanesSplit == 17);
    CHECK(result.binding.groups.size() == 387);
    CHECK(result.binding.cubeBoundGroups == 383);
    CHECK(result.binding.weightBoundGroups == 4);
    CHECK(result.stacking.groups.size() == 387);
    CHECK(result.stackReport.groups == 387);
}

TEST_CASE("pipeline: a line the validator excluded carries no pallets into stacking") {
    const std::string productPath = "tests/importer/_tmp_m2_products.csv";
    const std::string demandPath = "tests/importer/_tmp_m2_demand.json";
    const std::string placeholderPath = "tests/importer/_tmp_m2_placeholder.json";
    {
        std::ofstream products(productPath);
        products << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
                    "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
                 << "GOOD,Good,10,10,10,5,CS,2,4,2,8,TLD\n"
                 << "RAW,Raw material,0,0,0,5,CS,2,4,2,4,TLD\n";
        std::ofstream demand(demandPath);
        demand << R"({"REQUEST_ID":"t","CTL":[],"DNM":[],"STR":[)"
               << R"({"LOCFRNO":"1","LOCTONO":"2","MATNR":"GOOD","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":16.0,"UNITOFMEAS":"CS"},)"
               << R"({"LOCFRNO":"1","LOCTONO":"2","MATNR":"RAW","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":8.0,"UNITOFMEAS":"CS"}]})";
        std::ofstream placeholder(placeholderPath);
        placeholder << R"({"PHOLDER":[{"LOCFRNO":"1","LOCTONO":"2","SHIP_COND":"TL","NO_OF_LOADS":1}]})";
    }
    PipelineInputs inputs;
    inputs.product_path = productPath;
    inputs.demand_path = demandPath;
    inputs.placeholder_path = placeholderPath;
    inputs.paramsPath = "config/orderBuilderParams.json";
    inputs.palletPath = writePalletTable("tests/importer/_tmp_m2_pallets.csv", kTldPallet);
    const PipelineResult result = Pipeline::run(inputs);
    std::remove(inputs.palletPath.c_str());
    std::remove(productPath.c_str());
    std::remove(demandPath.c_str());
    std::remove(placeholderPath.c_str());

    REQUIRE(result.palletsForStacking.size() == 2);
    CHECK(result.pallets_per_line[1] == doctest::Approx(2.0));
    CHECK(result.palletsForStacking[0] == doctest::Approx(2.0));
    CHECK(result.palletsForStacking[1] == 0.0);
    CHECK(result.weightForStacking[1] == 0.0);
    CHECK(result.binding.groups[0].totalPallets == doctest::Approx(2.0));
}

TEST_CASE("pipeline: a line the validator rejected forms no segregation group") {
    const std::string productPath = "tests/importer/_tmp_m2_rejected_group_products.csv";
    const std::string demandPath = "tests/importer/_tmp_m2_rejected_group_demand.json";
    const std::string placeholderPath = "tests/importer/_tmp_m2_rejected_group_placeholder.json";
    {
        std::ofstream products(productPath);
        products << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
                    "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
                 << "GOOD,Good,10,10,10,5,CS,2,4,2,8,TLD\n";
        std::ofstream demand(demandPath);
        demand << R"({"REQUEST_ID":"t","CTL":[],"DNM":[],"STR":[)"
               << R"({"LOCFRNO":"1","LOCTONO":"2","MATNR":"GOOD","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":8.0,"UNITOFMEAS":"CS"},)"
               << R"({"LOCFRNO":"1","LOCTONO":"2","MATNR":"GOOD","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":16.0,"UNITOFMEAS":"CS"},)"
               << R"({"LOCFRNO":"","LOCTONO":"3","MATNR":"GOOD","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":8.0,"UNITOFMEAS":"CS"}]})";
        std::ofstream placeholder(placeholderPath);
        placeholder << R"({"PHOLDER":[]})";
    }
    PipelineInputs inputs;
    inputs.product_path = productPath;
    inputs.demand_path = demandPath;
    inputs.placeholder_path = placeholderPath;
    inputs.paramsPath = "config/orderBuilderParams.json";
    inputs.palletPath = writePalletTable("tests/importer/_tmp_m2_rejected_group_pallets.csv", kTldPallet);
    const PipelineResult result = Pipeline::run(inputs);
    std::remove(inputs.palletPath.c_str());
    std::remove(productPath.c_str());
    std::remove(demandPath.c_str());
    std::remove(placeholderPath.c_str());

    CHECK(result.validation.missing_fields == 1);
    CHECK(result.segregation.groups.size() == 1);
    CHECK(result.segregation.lanesIn == 1);
    CHECK(result.segregation.linesExcluded == 1);
    CHECK(result.stackReport.linesExcludedByValidator == 1);
    for (const auto& row : result.stackReport.rows) {
        CHECK(row.lane.rfind(" ->", 0) != 0);
    }
}

TEST_CASE("pipeline: rejected lines from different lanes do not merge into one group") {
    const std::string productPath = "tests/importer/_tmp_m2_rejected_lanes_products.csv";
    const std::string demandPath = "tests/importer/_tmp_m2_rejected_lanes_demand.json";
    const std::string placeholderPath = "tests/importer/_tmp_m2_rejected_lanes_placeholder.json";
    {
        std::ofstream products(productPath);
        products << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
                    "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
                 << "GOOD,Good,10,10,10,5,CS,2,4,2,8,TLD\n";
        std::ofstream demand(demandPath);
        demand << R"({"REQUEST_ID":"t","CTL":[],"DNM":[],"STR":[)"
               << R"({"LOCFRNO":"1","LOCTONO":"2","MATNR":"GOOD","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":8.0,"UNITOFMEAS":"CS"},)"
               << R"({"LOCFRNO":"3","LOCTONO":"4","MATNR":"GOOD","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":8.0,"UNITOFMEAS":"CS"},)"
               << R"({"LOCFRNO":"","LOCTONO":"8","MATNR":"GOOD","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":8.0,"UNITOFMEAS":"CS"},)"
               << R"({"LOCFRNO":"","LOCTONO":"9","MATNR":"GOOD","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":8.0,"UNITOFMEAS":"CS"}]})";
        std::ofstream placeholder(placeholderPath);
        placeholder << R"({"PHOLDER":[]})";
    }
    PipelineInputs inputs;
    inputs.product_path = productPath;
    inputs.demand_path = demandPath;
    inputs.placeholder_path = placeholderPath;
    inputs.paramsPath = "config/orderBuilderParams.json";
    inputs.palletPath = writePalletTable("tests/importer/_tmp_m2_rejected_lanes_pallets.csv", kTldPallet);
    const PipelineResult result = Pipeline::run(inputs);
    std::remove(inputs.palletPath.c_str());
    std::remove(productPath.c_str());
    std::remove(demandPath.c_str());
    std::remove(placeholderPath.c_str());

    CHECK(result.validation.missing_fields == 2);
    CHECK(result.segregation.groups.size() == 2);
    CHECK(result.segregation.lanesIn == 2);
    CHECK(result.segregation.linesExcluded == 2);
}

TEST_CASE("pipeline: the trailer can be chosen by code" * doctest::skip(!crossDayTests::august17StackingPresent())) {
    PipelineInputs inputs = realInputs(true);
    inputs.trailerCode = "53FT_NA";
    CHECK(Pipeline::run(inputs).ranMilestone2);
    inputs.trailerCode = "NO_SUCH_TRAILER";
    CHECK_THROWS_AS(Pipeline::run(inputs), std::runtime_error);
}

TEST_CASE("pipeline: an unreadable params file stops the run" * doctest::skip(!crossDayTests::august17StackingPresent())) {
    PipelineInputs inputs = realInputs(true);
    inputs.paramsPath = "does/not/exist.json";
    CHECK_THROWS_AS(Pipeline::run(inputs), std::runtime_error);
}

TEST_CASE("pipeline: the printed Milestone 2 report matches the run" * doctest::skip(!crossDayTests::august17StackingPresent())) {
    const PipelineResult result = Pipeline::run(realInputs(true));
    std::ostringstream out;
    StackReporter::print(result.stackReport, out, 5);
    CHECK(out.str().find("Groups                    387") != std::string::npos);
    CHECK(out.str().find("top 5 of 387") != std::string::npos);
}

TEST_CASE("pipeline: M2 weighs pallets with the pallet table's weight, M1 keeps its own") {
    const PipelineResult result = runOneLine("m2_pallet_weight", "P,Heavy,10,10,10,5,CS,133,10,1,10,PTL",
                                             kOnePalletLine, ptlPalletWeighing(100));
    REQUIRE(result.ranMilestone2);
    CHECK(result.weight_per_line[0] == doctest::Approx(1390.0));
    CHECK(result.weightForStacking[0] == doctest::Approx(1430.0));
    REQUIRE(result.binding.groups.size() == 1);
    CHECK(result.binding.groups[0].totalWeightLb == doctest::Approx(1430.0));
    CHECK(result.binding.groups[0].binding == BindingConstraint::Weight);
    REQUIRE(result.stackReport.rows.size() == 1);
    CHECK(result.stackReport.rows[0].weightLb == doctest::Approx(1430.0));
    CHECK(result.stackReport.rows[0].binding == BindingConstraint::Weight);
}

TEST_CASE("pipeline: a master row's own pallet weight wins over the pallet table's") {
    const PipelineResult result = runOneLine(
        "m2_row_pallet_weight",
        "P,Heavy,10,10,10,5,CS,133,10,1,10,PTL,100,5.5,48,40", kOnePalletLine, ptlPalletWeighing(60),
        kProductHeader + ",Pallet_Weight,Pallet_Height,Pallet_Footprint_Length,Pallet_Footprint_Width");
    REQUIRE(result.ranMilestone2);
    CHECK(result.weightForStacking[0] == doctest::Approx(1430.0));
    CHECK(result.binding.groups[0].binding == BindingConstraint::Weight);
}

TEST_CASE("pipeline: a params file still listing pallets stops the run") {
    const string paramsPath = "tests/importer/_tmp_m2_old_params.json";
    ofstream(paramsPath) << R"({"criSafeLimitLb":[5,299,549,799,1149,1499,1849,2199,3099,3599],)"
                            R"("pallets":[],"trailers":[{"trailerCode":"53FT_NA","interiorLengthIn":630,)"
                            R"("interiorWidthIn":100,"stackHeightCeilingIn":108,"weightLimitLb":45000,)"
                            R"("stackPositions":32}]})";
    const string productPath = "tests/importer/_tmp_m2_old_params_products.csv";
    const string demandPath = "tests/importer/_tmp_m2_old_params_demand.json";
    const string placeholderPath = "tests/importer/_tmp_m2_old_params_placeholder.json";
    ofstream(productPath) << kProductHeader << "\nP,Good,10,10,10,5,CS,10,1,1,1,PTL\n";
    ofstream(demandPath) << R"({"REQUEST_ID":"t","CTL":[],"DNM":[],"STR":[)" << kOnePalletLine << "]}";
    ofstream(placeholderPath) << R"({"PHOLDER":[]})";
    PipelineInputs inputs;
    inputs.product_path = productPath;
    inputs.demand_path = demandPath;
    inputs.placeholder_path = placeholderPath;
    inputs.paramsPath = paramsPath;
    CHECK_THROWS_WITH_AS(Pipeline::run(inputs), doctest::Contains("'pallets' is no longer read"),
                         runtime_error);
    for (const auto& path : {productPath, demandPath, placeholderPath, paramsPath}) remove(path.c_str());
}

TEST_CASE("pipeline: at a 60 lb pallet weight the same line stays cube-bound") {
    const PipelineResult result = runOneLine("m2_pallet_weight_default", "P,Heavy,10,10,10,5,CS,133,10,1,10,PTL",
                                             kOnePalletLine, ptlPalletWeighing(60));
    CHECK(result.weightForStacking[0] == doctest::Approx(result.weight_per_line[0]));
    CHECK(result.binding.groups[0].binding == BindingConstraint::Cube);
}

TEST_CASE("pipeline: a pallet taller than the trailer ceiling is reported and uses no floor") {
    const PipelineResult result = runOneLine("m2_over_height", "P,Tall,10,10,120,5,CS,10,1,1,1,PTL",
                                             kOnePalletLine, ptlPalletWeighing(60));
    CHECK(result.stacking.overHeightLines == std::vector<std::size_t>{0});
    CHECK(result.stackReport.overHeightLines == 1);
    CHECK(result.stackReport.totalFloorPositions == 0.0);
}

TEST_CASE("pipeline: a clean one-line run is complete") {
    const PipelineResult result = runOneLine("m2_complete", "P,Good,10,10,10,5,CS,10,1,1,1,PTL",
                                             kOnePalletLine, ptlPalletWeighing(60));
    CHECK(result.stackReport.linesNotStacked == 0);
    CHECK(isRunComplete(result));
}

TEST_CASE("pipeline: a line left out of stacking makes the run incomplete") {
    struct Case {
        std::string name;
        std::string productRow;
        UnitLoadError expectedError;
    };
    const std::vector<Case> cases{
        {"strength_above_range", "P,Bad,10,10,10,11,CS,10,1,1,1,PTL", UnitLoadError::InvalidData},
        {"strength_negative", "P,Bad,10,10,10,-1,CS,10,1,1,1,PTL", UnitLoadError::InvalidData},
        {"strength_unreadable", "P,Bad,10,10,10,x,CS,10,1,1,1,PTL", UnitLoadError::InvalidData},
        {"weight_zero", "P,Light,10,10,10,5,CS,0,1,1,1,PTL", UnitLoadError::InvalidData},
        {"missing_pallet_spec", "P,Odd,10,10,10,5,CS,10,1,1,1,GMA", UnitLoadError::MissingPalletSpec},
    };
    for (const auto& testCase : cases) {
        CAPTURE(testCase.name);
        const PipelineResult result = runOneLine("m2_incomplete_" + testCase.name, testCase.productRow,
                                                 kOnePalletLine, ptlPalletWeighing(60));
        CHECK(result.validation.errors == 0);
        REQUIRE(result.stacking.excludedLines.size() == 1);
        CHECK(result.stacking.excludedLines[0].error == testCase.expectedError);
        CHECK(result.stackReport.linesNotStacked == 1);
        CHECK(result.stackReport.totalFloorPositions == 0.0);
        CHECK_FALSE(isRunComplete(result));
    }
}

TEST_CASE("pipeline: an over-height line makes the run incomplete") {
    const PipelineResult result = runOneLine("m2_incomplete_over_height", "P,Tall,10,10,120,5,CS,10,1,1,1,PTL",
                                             kOnePalletLine, ptlPalletWeighing(60));
    CHECK(result.stackReport.linesNotStacked == 1);
    CHECK_FALSE(isRunComplete(result));
}

TEST_CASE("pipeline: demand that converts to no pallets builds no stack and is incomplete") {
    const std::string eachesLine =
        R"({"LOCFRNO":"1","LOCTONO":"2","MATNR":"P","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":5.0,"UNITOFMEAS":"EA"})";
    const PipelineResult result = runOneLine("m2_no_stacks", "P,Good,10,10,10,5,CS,10,1,1,1,PTL",
                                             eachesLine, ptlPalletWeighing(60));
    CHECK(result.validation.errors == 0);
    CHECK(result.stacking.zeroQuantityLines == std::vector<std::size_t>{0});
    CHECK(result.stackReport.linesNotStacked == 1);
    CHECK(result.stackReport.builtNoStacks);
    CHECK_FALSE(isRunComplete(result));
}

const std::string kFiveEachesLine =
    R"({"LOCFRNO":"1","LOCTONO":"2","MATNR":"P","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":5.0,"UNITOFMEAS":"EA"})";

TEST_CASE("pipeline: an unsupported unit beside a valid line makes the run incomplete") {
    const PipelineResult result = runOneLine("m2_mixed_units", "P,Good,10,10,10,5,CS,10,1,1,1,PTL",
                                             kOnePalletLine + "," + kFiveEachesLine, ptlPalletWeighing(60));
    CHECK(result.validation.errors == 0);
    CHECK(result.stackReport.totalFloorPositions == 1.0);
    CHECK_FALSE(result.stackReport.builtNoStacks);
    CHECK(result.stacking.zeroQuantityLines == std::vector<std::size_t>{1});
    REQUIRE(result.stackReport.unstackedLines.size() == 1);
    CHECK(result.stackReport.unstackedLines[0].lineIndex == 1);
    CHECK(result.stackReport.unstackedLines[0].matnr == "P");
    CHECK(result.stackReport.unstackedLines[0].reason.find("unit of measure 'EA' cannot be converted")
          != std::string::npos);
    CHECK_FALSE(isRunComplete(result));
}

TEST_CASE("pipeline: an unsupported-unit line is named with its reason in the printed report") {
    const PipelineResult result = runOneLine("m2_mixed_units_report", "P,Good,10,10,10,5,CS,10,1,1,1,PTL",
                                             kOnePalletLine + "," + kFiveEachesLine, ptlPalletWeighing(60));
    std::ostringstream out;
    StackReporter::print(result.stackReport, out);
    CHECK(out.str().find("INCOMPLETE: 1 demand line(s) are in no stack") != std::string::npos);
    CHECK(out.str().find("line 1, material P: unit of measure 'EA' cannot be converted") != std::string::npos);
}

TEST_CASE("pipeline: a raw-material line the validator skips is reported and makes the run incomplete") {
    const PipelineResult result = runOneLine("m2_raw_material", "P,Raw,0,0,0,5,CS,10,1,1,1,PTL",
                                             kOnePalletLine, ptlPalletWeighing(60));
    CHECK(result.validation.errors == 0);
    CHECK(result.validation.zero_dimension == 1);
    REQUIRE(result.stackReport.unstackedLines.size() == 1);
    CHECK(result.stackReport.unstackedLines[0].reason.find("zero_dimension") != std::string::npos);
    CHECK_FALSE(isRunComplete(result));
}

TEST_CASE("pipeline: a line rejected by validation is listed among the unstacked lines") {
    const PipelineResult result = runOneLine("m2_rejected_listed", "P,Good,10,10,10,5,CS,10,1,1,1,PTL",
        kOnePalletLine + "," + R"({"LOCFRNO":"1","LOCTONO":"2","MATNR":"NOT_IN_MASTER","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":1.0,"UNITOFMEAS":"PAL"})",
        ptlPalletWeighing(60));
    REQUIRE(result.stackReport.unstackedLines.size() == 1);
    CHECK(result.stackReport.unstackedLines[0].lineIndex == 1);
    CHECK(result.stackReport.unstackedLines[0].matnr == "NOT_IN_MASTER");
    CHECK(result.stackReport.unstackedLines[0].reason.find("unmatched_product") != std::string::npos);
    CHECK_FALSE(isRunComplete(result));
}

TEST_CASE("pipeline: the real day is complete and reports its provisional rules" * doctest::skip(!crossDayTests::august17StackingPresent())) {
    const PipelineResult result = Pipeline::run(realInputs(true));
    CHECK(isRunComplete(result));
    CHECK(result.stackReport.ambiguousPalletLines == 144);
    CHECK(result.stackReport.stackWholePallets);
    CHECK(result.stackReport.totalPalletsStacked >= result.stackReport.totalPallets);
    std::ostringstream out;
    StackReporter::print(result.stackReport, out);
    CHECK(result.stackReport.unstackedLines.empty());
    CHECK(out.str().find("complete: every demand line is in a stack") != std::string::npos);
    CHECK(out.str().find("144 line(s) chose a pallet type by preference order") != std::string::npos);
    CHECK(out.str().find("equivalence to T3 not demonstrated") != std::string::npos);
}
