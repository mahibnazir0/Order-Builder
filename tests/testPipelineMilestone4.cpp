#include "doctest.h"
#include "paramsFields.hpp"
#include "pipeline.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;
namespace fs = filesystem;

namespace {

const string kShippedParamsPath = "config/orderBuilderParams.json";

// A day of two products on one lane, planned on Tuesday 4 Aug for 6-7 Aug, with two trucks
// reserved on 6 Aug. Written to a folder of its own and removed with it.
struct SmallDay {
    fs::path directory;
    PipelineInputs inputs;

    explicit SmallDay(const string& name, const string& extraLines = "") {
        directory = fs::temp_directory_path() / ("obM4Pipeline_" + name);
        fs::remove_all(directory);
        fs::create_directories(directory);
        const string product = (directory / "products.csv").string();
        const string demand = (directory / "demand.json").string();
        const string placeholder = (directory / "placeholder.json").string();
        const string pallets = (directory / "pallets.csv").string();
        ofstream(pallets) << "ID,Footprint_Length,Footprint_Width,Height,Weight\nTLD,48,40,0.1,1\n";
        ofstream(product) << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
                             "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
                             "GOOD,Good,10,10,10,5,CS,20,4,2,8,TLD\n"
                             "OTHER,Other,10,10,10,5,CS,30,4,2,8,TLD\n";
        ofstream(demand) << R"({"REQUEST_ID":"#STR_PA4400_20260804100000#","DNM":[],)"
                            R"("CTL":[{"ZDATE":"","ZDAY":"TUESDAY","LEVEL_LOAD_START":2,"LEVEL_LOAD_END":3,"AUTO_O2":2}],)"
                            R"("STR":[)"
                         << line("GOOD", 240, "TL") << "," << line("OTHER", 160, "TL") << extraLines << "]}";
        ofstream(placeholder) << R"({"PHOLDER":[{"LOCFRNO":"1","LOCTONO":"2","SHIP_COND":"TL","DATFR_TA":"2026-08-06",)"
                                 R"("DATTO_TA":"2026-08-08","ZZNA_EQUIP_SIZE":"53F","NO_OF_LOADS":2,"EBELN":""}]})";
        inputs.product_path = product;
        inputs.demand_path = demand;
        inputs.placeholder_path = placeholder;
        inputs.palletPath = pallets;
        inputs.paramsPath = kShippedParamsPath;
        inputs.demandSelector = parseDemandSelector("calendar:2026-08-04");
        inputs.runSelection = true;
    }
    ~SmallDay() {
        error_code ignored;
        fs::remove_all(directory, ignored);
    }
    SmallDay(const SmallDay&) = delete;
    SmallDay& operator=(const SmallDay&) = delete;

    static string line(const string& matnr, int cases, const string& shipCondition) {
        return string(R"({"IDPR":"1","BNFPO":10,"LOCFRNO":"1","LOCTONO":"2","MATNR":")") + matnr
               + R"(","DATFR_TA":"2026-08-06","DATTO_TA":"2026-08-08","SHIP_COND":")" + shipCondition
               + R"(","PLANNER_SNP":"S01","AVAIL_DATE":"2026-08-04","TPRIO":0,"AVAIL_QTY":0,"TRANS":)"
               + to_string(cases) + R"(,"UNITOFMEAS":"CS"})";
    }
};

// The shipped params with one change made to the "selection" block, written beside `day`.
string editedParams(const SmallDay& day, const string& key, const nlohmann::json& value) {
    nlohmann::json root = readParamsFile(kShippedParamsPath);
    root["selection"]["knapsack"][key] = value;
    root["selection"]["patternPacker"][key] = value;
    root["selection"]["binPacker"][key] = value;
    const string path = (day.directory / "params.json").string();
    ofstream(path) << root.dump(2);
    return path;
}

} // anonymous namespace

TEST_CASE("pipelineM4: a selection run fills the reserved trucks and keeps every invariant") {
    const SmallDay day("fills");
    const PipelineResult result = Pipeline::run(day.inputs);
    REQUIRE(result.ranSelection);
    CHECK(result.reservationBook.reservedTrucks == 2);
    CHECK(result.reservationBook.window.firstShipDate == "2026-08-06");
    CHECK(result.selectionViolations.empty());
    CHECK_FALSE(result.selectionPlan.trucks.empty());
    CHECK(result.selectionPlan.trucks.size() <= 2);
    double unitLoads = 0.0;
    for (const PlannedTruck& truck : result.selectionPlan.trucks) unitLoads += truck.unitLoads;
    CHECK(unitLoads == doctest::Approx((240.0 + 160.0) / 8.0));
    CHECK(runOutcome(result) == RunOutcome::Complete);
}

TEST_CASE("pipelineM4: upstreamUnchanged - M1, M2 and M3 figures are the same with or without M4") {
    const SmallDay day("upstream");
    PipelineInputs withoutSelection = day.inputs;
    withoutSelection.runSelection = false;
    const PipelineResult with = Pipeline::run(day.inputs);
    const PipelineResult without = Pipeline::run(withoutSelection);
    CHECK_FALSE(without.ranSelection);
    CHECK(with.summary.hash_total == doctest::Approx(without.summary.hash_total));
    CHECK(with.summary.total_pallet_equiv == doctest::Approx(without.summary.total_pallet_equiv));
    CHECK(with.summary.total_weight_lb == doctest::Approx(without.summary.total_weight_lb));
    CHECK(with.pallets_per_line == without.pallets_per_line);
    CHECK(with.weight_per_line == without.weight_per_line);
    REQUIRE(with.stacking.groups.size() == without.stacking.groups.size());
    for (size_t group = 0; group < with.stacking.groups.size(); ++group) {
        CHECK(with.stacking.groups[group].best.floorPositions == without.stacking.groups[group].best.floorPositions);
    }
    CHECK(with.floorRun.selectedLines() == without.floorRun.selectedLines());
    CHECK(runOutcome(with) == runOutcome(without));
}

TEST_CASE("pipelineM4: selection needs a params file with a selection block") {
    SmallDay day("needsParams");
    PipelineInputs noParams = day.inputs;
    noParams.paramsPath.clear();
    noParams.demandSelector.reset();
    CHECK_THROWS_AS(Pipeline::run(noParams), runtime_error);

    nlohmann::json root = readParamsFile(kShippedParamsPath);
    root.erase("selection");
    const string path = (day.directory / "noSelection.json").string();
    ofstream(path) << root.dump(2);
    PipelineInputs noBlock = day.inputs;
    noBlock.paramsPath = path;
    try {
        Pipeline::run(noBlock);
        FAIL("a params file with no selection block ran");
    } catch (const runtime_error& error) {
        CHECK(string(error.what()).find("selection") != string::npos);
    }
}

TEST_CASE("pipelineM4: a grain coarser than the lightest stack stops the run, named") {
    SmallDay day("coarseGrain");
    PipelineInputs inputs = day.inputs;
    inputs.paramsPath = editedParams(day, "weightGrainLb", 100000.0);
    try {
        Pipeline::run(inputs);
        FAIL("a coarse grain ran");
    } catch (const runtime_error& error) {
        CHECK(string(error.what()).find("weightGrainLb") != string::npos);
    }
}

TEST_CASE("pipelineM4: a lane with demand under two ship conditions is named as a combo lane") {
    const SmallDay day("combo", "," + SmallDay::line("GOOD", 16, "TF"));
    const PipelineResult result = Pipeline::run(day.inputs);
    REQUIRE(result.selectionWarnings.size() == 1);
    CHECK(result.selectionWarnings[0].find("combo lane 1 -> 2") != string::npos);
    CHECK(result.selectionWarnings[0].find("TF and TL") != string::npos);
}

TEST_CASE("pipelineM4: solveRecreatedFromRecord - a run from its recorded inputs plans the same") {
    SmallDay day("record");
    PipelineInputs recorded = day.inputs;
    recorded.recordDirectory = (day.directory / "record").string();
    const PipelineResult original = Pipeline::run(recorded);

    const fs::path record = recorded.recordDirectory;
    ifstream manifest(record / "record.txt");
    PipelineInputs rerun = day.inputs;
    for (string role, file; manifest >> role >> file;) {
        const string path = (record / file).string();
        if (role == "product") rerun.product_path = path;
        if (role == "demand") rerun.demand_path = path;
        if (role == "placeholder") rerun.placeholder_path = path;
        if (role == "params") rerun.paramsPath = path;
        if (role == "pallets") rerun.palletPath = path;
    }
    CHECK(rerun.demand_path.find("record") != string::npos);
    CHECK(rerun.paramsPath.find("record") != string::npos);
    const PipelineResult again = Pipeline::run(rerun);
    CHECK(samePlan(original.selectionPlan, again.selectionPlan));
}

TEST_CASE("pipelineM4: a broken plan fails the run with exit 7") {
    SmallDay day("broken");
    PipelineResult result = Pipeline::run(day.inputs);
    REQUIRE(runOutcome(result) == RunOutcome::Complete);
    result.selectionViolations.push_back({"I4", "a lane-date given more trucks than reserved"});
    CHECK(runOutcome(result) == RunOutcome::SelectionIncomplete);
    CHECK(static_cast<int>(RunOutcome::SelectionIncomplete) == 7);
}

TEST_CASE("pipelineM4: noCustomerNameInCode - no source file tests a customer's name") {
    // Comments may name the customer whose data a rule was measured on, and the report may say
    // "the customer"; code may not branch on which customer it is. A literal naming one
    // ("Customer 2", "Company F") is where such a branch would show.
    const regex kCustomerIdentity(R"((Customer|Company)\s?([0-9]+|[A-Z])\b)");
    REQUIRE(regex_search(string("customer == \"Customer 2\""), kCustomerIdentity));
    REQUIRE(regex_search(string("Company F"), kCustomerIdentity));
    REQUIRE_FALSE(regex_search(string("not yet confirmed by the customer"), kCustomerIdentity));
    size_t filesRead = 0;
    for (const string root : {"src", "include"}) {
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            const string extension = entry.path().extension().string();
            if (!entry.is_regular_file() || (extension != ".cpp" && extension != ".hpp")) continue;
            ++filesRead;
            ifstream source(entry.path());
            size_t lineNumber = 0;
            for (string text; getline(source, text);) {
                ++lineNumber;
                const size_t comment = text.find("//");
                const string code = comment == string::npos ? text : text.substr(0, comment);
                for (size_t open = code.find('"'); open != string::npos; open = code.find('"', open + 1)) {
                    const size_t close = code.find('"', open + 1);
                    if (close == string::npos) break;
                    const string literal = code.substr(open + 1, close - open - 1);
                    if (regex_search(literal, kCustomerIdentity)) {
                        FAIL_CHECK(entry.path().string() << ":" << lineNumber << " names \"" << literal << "\"");
                    }
                    open = close;
                }
            }
        }
    }
    CHECK(filesRead > 50);
}
