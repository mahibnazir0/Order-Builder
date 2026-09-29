#include "doctest.h"
#include "pipeline.hpp"
#include "validator.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace ob;

namespace {

PipelineInputs realInputs(bool withParams) {
    PipelineInputs inputs;
    inputs.product_path = "tests/importer/Customer2-Product-Data.csv";
    inputs.demand_path = "tests/importer/Demand-1.json";
    inputs.placeholder_path = "tests/importer/PlaceHolder-1.json";
    if (withParams) inputs.paramsPath = "config/orderBuilderParams.json";
    return inputs;
}

ValidationIssue issue(ValidationIssue::Severity severity, const std::string& rule, int lineIndex) {
    ValidationIssue validationIssue;
    validationIssue.severity = severity;
    validationIssue.rule = rule;
    validationIssue.line_index = lineIndex;
    return validationIssue;
}

// Runs the pipeline on one product row and one demand line, written to scratch files.
PipelineResult runOneLine(const std::string& name, const std::string& productRow,
                          const std::string& demandLine, const std::string& paramsJson) {
    const std::string productPath = "tests/importer/_tmp_" + name + "_products.csv";
    const std::string demandPath = "tests/importer/_tmp_" + name + "_demand.json";
    const std::string placeholderPath = "tests/importer/_tmp_" + name + "_placeholder.json";
    const std::string paramsPath = "tests/importer/_tmp_" + name + "_params.json";
    std::ofstream(productPath) << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
                                  "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
                               << productRow << "\n";
    std::ofstream(demandPath) << R"({"REQUEST_ID":"t","CTL":[],"DNM":[],"STR":[)" << demandLine << "]}";
    std::ofstream(placeholderPath) << R"({"PHOLDER":[]})";
    std::ofstream(paramsPath) << paramsJson;
    PipelineInputs inputs;
    inputs.product_path = productPath;
    inputs.demand_path = demandPath;
    inputs.placeholder_path = placeholderPath;
    inputs.paramsPath = paramsPath;
    PipelineResult result = Pipeline::run(inputs);
    for (const auto& path : {productPath, demandPath, placeholderPath, paramsPath}) std::remove(path.c_str());
    return result;
}

std::string paramsWithPtlWeight(int ptlAddedWeightLb) {
    return R"({"criSafeLimitLb":[5,299,549,799,1149,1499,1849,2199,3099,3599],"pallets":[)"
           R"({"palletId":"PTL","addedWeightLb":)" + std::to_string(ptlAddedWeightLb)
         + R"(,"addedHeightIn":5.5,"footprintLengthIn":48,"footprintWidthIn":40}],)"
           R"("trailers":[{"trailerCode":"53FT_NA","interiorLengthIn":630,"interiorWidthIn":100,)"
           R"("stackHeightCeilingIn":108,"weightLimitLb":45000,"stackPositions":32}],)"
           R"("doNotMixReading":"Strict","pass2AttemptCap":4,"maxStackHeight":2,"blankCriIsStackable":false})";
}

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

TEST_CASE("pipeline: without a params file only Milestone 1 runs") {
    const PipelineResult result = Pipeline::run(realInputs(false));
    CHECK_FALSE(result.ranMilestone2);
    CHECK(result.segregation.groups.empty());
    CHECK(result.stacking.groups.empty());
    CHECK(result.summary.total_demand_lines == 24357);
}

TEST_CASE("pipeline: with a params file every stage runs on the real day") {
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
    const PipelineResult result = Pipeline::run(inputs);
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
    const PipelineResult result = Pipeline::run(inputs);
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
    const PipelineResult result = Pipeline::run(inputs);
    std::remove(productPath.c_str());
    std::remove(demandPath.c_str());
    std::remove(placeholderPath.c_str());

    CHECK(result.validation.missing_fields == 2);
    CHECK(result.segregation.groups.size() == 2);
    CHECK(result.segregation.lanesIn == 2);
    CHECK(result.segregation.linesExcluded == 2);
}

TEST_CASE("pipeline: the trailer can be chosen by code") {
    PipelineInputs inputs = realInputs(true);
    inputs.trailerCode = "53FT_NA";
    CHECK(Pipeline::run(inputs).ranMilestone2);
    inputs.trailerCode = "NO_SUCH_TRAILER";
    CHECK_THROWS_AS(Pipeline::run(inputs), std::runtime_error);
}

TEST_CASE("pipeline: an unreadable params file stops the run") {
    PipelineInputs inputs = realInputs(true);
    inputs.paramsPath = "does/not/exist.json";
    CHECK_THROWS_AS(Pipeline::run(inputs), std::runtime_error);
}

TEST_CASE("pipeline: the printed Milestone 2 report matches the run") {
    const PipelineResult result = Pipeline::run(realInputs(true));
    std::ostringstream out;
    StackReporter::print(result.stackReport, out, 5);
    CHECK(out.str().find("Groups                    387") != std::string::npos);
    CHECK(out.str().find("top 5 of 387") != std::string::npos);
}

TEST_CASE("pipeline: M2 weighs pallets with the configured pallet weight, M1 keeps its own") {
    const PipelineResult result = runOneLine("m2_pallet_weight", "P,Heavy,10,10,10,5,CS,133,10,1,10,PTL",
                                             kOnePalletLine, paramsWithPtlWeight(100));
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

TEST_CASE("pipeline: at the shipped 60 lb pallet weight the same line stays cube-bound") {
    const PipelineResult result = runOneLine("m2_pallet_weight_default", "P,Heavy,10,10,10,5,CS,133,10,1,10,PTL",
                                             kOnePalletLine, paramsWithPtlWeight(60));
    CHECK(result.weightForStacking[0] == doctest::Approx(result.weight_per_line[0]));
    CHECK(result.binding.groups[0].binding == BindingConstraint::Cube);
}

TEST_CASE("pipeline: a pallet taller than the trailer ceiling is reported and uses no floor") {
    const PipelineResult result = runOneLine("m2_over_height", "P,Tall,10,10,120,5,CS,10,1,1,1,PTL",
                                             kOnePalletLine, paramsWithPtlWeight(60));
    CHECK(result.stacking.overHeightLines == std::vector<std::size_t>{0});
    CHECK(result.stackReport.overHeightLines == 1);
    CHECK(result.stackReport.totalFloorPositions == 0.0);
}

TEST_CASE("pipeline: a clean one-line run is complete") {
    const PipelineResult result = runOneLine("m2_complete", "P,Good,10,10,10,5,CS,10,1,1,1,PTL",
                                             kOnePalletLine, paramsWithPtlWeight(60));
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
                                                 kOnePalletLine, paramsWithPtlWeight(60));
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
                                             kOnePalletLine, paramsWithPtlWeight(60));
    CHECK(result.stackReport.linesNotStacked == 1);
    CHECK_FALSE(isRunComplete(result));
}

TEST_CASE("pipeline: demand that converts to no pallets builds no stack and is incomplete") {
    const std::string eachesLine =
        R"({"LOCFRNO":"1","LOCTONO":"2","MATNR":"P","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":5.0,"UNITOFMEAS":"EA"})";
    const PipelineResult result = runOneLine("m2_no_stacks", "P,Good,10,10,10,5,CS,10,1,1,1,PTL",
                                             eachesLine, paramsWithPtlWeight(60));
    CHECK(result.validation.errors == 0);
    CHECK(result.stackReport.linesNotStacked == 0);
    CHECK(result.stackReport.builtNoStacks);
    CHECK_FALSE(isRunComplete(result));
}

TEST_CASE("pipeline: the real day is complete and reports its provisional rules") {
    const PipelineResult result = Pipeline::run(realInputs(true));
    CHECK(isRunComplete(result));
    CHECK(result.stackReport.ambiguousPalletLines == 144);
    CHECK(result.stackReport.stackWholePallets);
    CHECK(result.stackReport.totalPalletsStacked >= result.stackReport.totalPallets);
    std::ostringstream out;
    StackReporter::print(result.stackReport, out);
    CHECK(out.str().find("complete: every line that passed validation is in a stack") != std::string::npos);
    CHECK(out.str().find("144 line(s) chose a pallet type by preference order") != std::string::npos);
    CHECK(out.str().find("equivalence to T3 not demonstrated") != std::string::npos);
}
