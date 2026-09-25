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
