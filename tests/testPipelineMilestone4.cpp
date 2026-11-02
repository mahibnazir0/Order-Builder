#include "doctest.h"
#include "m4SmallDay.hpp"
#include "paramsFields.hpp"
#include "pipeline.hpp"

#include <algorithm>
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
using m4SmallDay::kShippedParamsPath;
using m4SmallDay::SmallDay;

namespace {

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

// The shipped params with top-level "selection" settings changed, written beside `day`.
string paramsWith(const SmallDay& day, const nlohmann::json& changes) {
    nlohmann::json root = readParamsFile(kShippedParamsPath);
    root["selection"].update(changes);
    const string path = (day.directory / "selectionParams.json").string();
    ofstream(path) << root.dump(2);
    return path;
}

// Unit loads on the trucks from lines of this TPRIO.
double unitLoadsShipped(const PipelineResult& result, int tprio) {
    double shipped = 0.0;
    for (const PlannedTruck& truck : result.selectionPlan.trucks) {
        for (const size_t index : truck.items) {
            const SelectionItem& item = result.selectionItems.items[index];
            if (item.tprio == tprio) shipped += item.unitLoads;
        }
    }
    return shipped;
}

bool shortfallNaming(const PipelineResult& result, const string& text) {
    const vector<string> shortfalls = selectionShortfalls(result);
    return any_of(shortfalls.begin(), shortfalls.end(),
                  [&](const string& shortfall) { return shortfall.find(text) != string::npos; });
}

bool hasI10(const vector<InvariantViolation>& violations) {
    return any_of(violations.begin(), violations.end(),
                  [](const InvariantViolation& violation) { return violation.invariant == "I10"; });
}

vector<InvariantViolation> invariantsOf(const PipelineResult& result, const SelectionItems& items,
                                        const SelectionPlan& plan) {
    return checkInvariants(result, result.reservationBook, items, plan, result.selectionParams.selector.maxUnitLoadsPerTruck);
}

string reportOf(const PipelineResult& result) {
    SelectionReportContext context;
    context.routing = result.selectionParams.routing;
    context.settings = result.selectionParams.selector;
    context.shortfalls = selectionShortfalls(result);
    context.provisional = selectionProvisional(result);
    context.priorityZeroIsMostUrgent = result.selectionParams.priorityZeroIsMostUrgent;
    context.maxDaysPulledForward = result.selectionParams.maxDaysPulledForward;
    context.defaultEquipment = result.selectionParams.reservations.defaultEquipment;
    ostringstream out;
    printSelectionReport(out, result, result.reservationBook, result.selectionItems, result.laneClassification,
                         result.selectionPlan, context);
    return out.str();
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

TEST_CASE("pipelineM4: a lane with demand under two ship conditions stops the run, named as a combo lane") {
    const SmallDay day("combo", "," + SmallDay::line("GOOD", 16, "TF"));
    try {
        Pipeline::run(day.inputs);
        FAIL("a combo lane was planned");
    } catch (const runtime_error& error) {
        const string message = error.what();
        CHECK(message.find("combo lanes") != string::npos);
        CHECK(message.find("1 -> 2 under TF and TL") != string::npos);
    }
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

TEST_CASE("pipelineM4: a clean run exits 0 and reports no shortfall") {
    const SmallDay day("clean");
    const PipelineResult result = Pipeline::run(day.inputs);
    CHECK(selectionShortfalls(result).empty());
    CHECK(runOutcome(result) == RunOutcome::Complete);
}

TEST_CASE("pipelineM4: a lane above m5Threshold is escalated and fails the run with exit 7") {
    nlohmann::json laneThree = SmallDay::lineJson("GOOD", 16, "TL");
    laneThree["LOCFRNO"] = "3";
    const SmallDay day("escalated", "," + laneThree.dump(), "," + SmallDay::placeholderJson("3", 30).dump());
    const PipelineResult result = Pipeline::run(day.inputs);
    CHECK(runOutcome(result) == RunOutcome::SelectionIncomplete);
    CHECK(shortfallNaming(result, "escalated to M5"));
    for (const ReservationUse& use : result.selectionPlan.reservations) {
        if (result.reservationBook.reservations[use.reservationIndex].origin != "3") continue;
        CHECK(use.escalated);
        CHECK(use.unused() == 30);
    }
    CHECK(reportOf(result).find("unfilled, escalated to M5") != string::npos);
}

TEST_CASE("pipelineM4: a stack left over as unfittable, unsolved or escalated fails the run with exit 7") {
    for (const LeftOverReason reason :
         {LeftOverReason::Unfittable, LeftOverReason::LaneUnsolved, LeftOverReason::EscalatedToM5}) {
        CAPTURE(leftOverReasonName(reason));
        SmallDay day("dropped");
        PipelineResult result = Pipeline::run(day.inputs);
        REQUIRE(runOutcome(result) == RunOutcome::Complete);
        result.selectionPlan.leftOver.push_back({0, reason});
        CHECK(runOutcome(result) == RunOutcome::SelectionIncomplete);
        CHECK(shortfallNaming(result, leftOverReasonName(reason)));
    }
}

TEST_CASE("pipelineM4: a stack left outside the window or without a reservation leaves the run complete") {
    for (const LeftOverReason reason :
         {LeftOverReason::AfterWindow, LeftOverReason::UnavailableInWindow, LeftOverReason::NoReservation}) {
        CAPTURE(leftOverReasonName(reason));
        SmallDay day("overflow");
        PipelineResult result = Pipeline::run(day.inputs);
        result.selectionPlan.leftOver.push_back({0, reason});
        CHECK(runOutcome(result) == RunOutcome::Complete);
    }
}

TEST_CASE("pipelineM4: a stack left over the reservation fails the run only where its lane-date left trucks unbuilt") {
    SmallDay day("overReservation");
    PipelineResult result = Pipeline::run(day.inputs);
    const vector<LaneRoute>& lanes = result.laneClassification.routes;
    const auto loaded = find_if(lanes.begin(), lanes.end(), [](const LaneRoute& lane) { return !lane.itemIndices.empty(); });
    REQUIRE(loaded != lanes.end());
    RouteOutcome& route = result.selectionPlan.routes[static_cast<size_t>(loaded - lanes.begin())];
    result.selectionPlan.leftOver.push_back({loaded->itemIndices.front(), LeftOverReason::OverReservation});
    route.trucksAllowed = loaded->reservedTrucks;
    route.trucksBuilt = loaded->reservedTrucks;
    CHECK(runOutcome(result) == RunOutcome::Complete);
    route.trucksBuilt = loaded->reservedTrucks - 1;
    route.trucksAllowed = route.trucksBuilt;
    CHECK(runOutcome(result) == RunOutcome::SelectionIncomplete);
    CHECK(shortfallNaming(result, "while the lane-date had reserved trucks unbuilt"));
}

TEST_CASE("pipelineM4: a floor eviction fails the run only where its route left trucks unbuilt") {
    SmallDay day("evicted");
    PipelineResult result = Pipeline::run(day.inputs);
    const vector<LaneRoute>& lanes = result.laneClassification.routes;
    const auto loaded = find_if(lanes.begin(), lanes.end(), [](const LaneRoute& lane) { return !lane.itemIndices.empty(); });
    REQUIRE(loaded != lanes.end());
    RouteOutcome& route = result.selectionPlan.routes[static_cast<size_t>(loaded - lanes.begin())];
    result.selectionPlan.leftOver.push_back({loaded->itemIndices.front(), LeftOverReason::FloorEviction});
    route.trucksBuilt = route.trucksAllowed;
    CHECK(runOutcome(result) == RunOutcome::Complete);
    route.trucksBuilt = route.trucksAllowed - 1;
    CHECK(runOutcome(result) == RunOutcome::SelectionIncomplete);
}

TEST_CASE("pipelineM4: a stack pulled forward beyond the limit fails the run with exit 7, named") {
    SmallDay day("pulledTooFar");
    PipelineResult result = Pipeline::run(day.inputs);
    REQUIRE(runOutcome(result) == RunOutcome::Complete);
    REQUIRE_FALSE(result.selectionItems.items.empty());
    SelectionItem item = result.selectionItems.items.front();
    item.daysPulledForward = result.selectionParams.maxDaysPulledForward + 1;
    result.selectionItems.rejected.push_back({item, item.lineIndices.front(), ItemRejection::PullForwardBeyondLimit});
    CHECK(runOutcome(result) == RunOutcome::SelectionIncomplete);
    CHECK(shortfallNaming(result, "pulling lines forward beyond the limit"));
    CHECK_FALSE(shortfallNaming(result, "bad line data"));
    CHECK(reportOf(result).find("pulled forward beyond the limit: 11 days") != string::npos);
}

TEST_CASE("pipelineM4: a route whose method overran the budget fails the run with exit 7") {
    SmallDay day("slow");
    PipelineResult result = Pipeline::run(day.inputs);
    result.selectionPlan.routes.front().overBudget = true;
    CHECK(runOutcome(result) == RunOutcome::SelectionIncomplete);
    CHECK(shortfallNaming(result, "time budget"));
}

TEST_CASE("pipelineM4: a route whose LP stopped at its iteration limit fails the run with exit 7") {
    SmallDay day("lpCapped");
    PipelineResult result = Pipeline::run(day.inputs);
    result.selectionPlan.routes.front().lpIterationCapHit = true;
    CHECK(runOutcome(result) == RunOutcome::SelectionIncomplete);
    CHECK(shortfallNaming(result, "simplex iteration"));
}

// Lane 3 -> 2 with 50 stacks for its one reserved truck: a knapsack lane whose search has to
// choose what rides.
nlohmann::json overfullLaneLine() {
    nlohmann::json overfull = SmallDay::lineJson("GOOD", 2000, "TL");
    overfull["LOCFRNO"] = "3";
    return overfull;
}

TEST_CASE("pipelineM4: a knapsack stopped at its node limit makes the run provisional, exit 8, named by lane and date") {
    SmallDay day("nodeLimit", "," + overfullLaneLine().dump(), "," + SmallDay::placeholderJson("3", 1).dump());
    nlohmann::json knapsack = readParamsFile(kShippedParamsPath)["selection"]["knapsack"];
    knapsack["nodeLimit"] = 1;
    day.inputs.paramsPath = paramsWith(day, {{"knapsack", knapsack}});
    const PipelineResult result = Pipeline::run(day.inputs);
    CHECK(static_cast<int>(runOutcome(result)) == 8);
    const string report = reportOf(result);
    CHECK(report.find("Result: PROVISIONAL (exit 8)") != string::npos);
    CHECK(report.find("3 -> 2 TL, 2026-08-06: knapsack stopped at its node limit of 1") != string::npos);
}

TEST_CASE("pipelineM4: the same knapsack lane searched within its node limit exits 0") {
    const SmallDay day("withinNodeLimit", "," + overfullLaneLine().dump(), "," + SmallDay::placeholderJson("3", 1).dump());
    const PipelineResult result = Pipeline::run(day.inputs);
    CHECK(runOutcome(result) == RunOutcome::Complete);
    CHECK(reportOf(result).find("Result: complete") != string::npos);
}

TEST_CASE("pipelineM4: a line rejected as an item fails the run with exit 7 and is printed") {
    nlohmann::json priorityTwelve = SmallDay::lineJson("GOOD", 16, "TL");
    priorityTwelve["TPRIO"] = 12;
    nlohmann::json noPriority = SmallDay::lineJson("OTHER", 16, "TL");
    noPriority.erase("TPRIO");
    nlohmann::json blankAvail = SmallDay::lineJson("GOOD", 8, "TL");
    blankAvail["AVAIL_DATE"] = "";
    for (const nlohmann::json& bad : {priorityTwelve, noPriority, blankAvail}) {
        CAPTURE(bad.dump());
        const SmallDay day("rejected", "," + bad.dump());
        const PipelineResult result = Pipeline::run(day.inputs);
        REQUIRE(result.ranSelection);
        CHECK_FALSE(result.selectionItems.rejected.empty());
        CHECK(runOutcome(result) == RunOutcome::SelectionIncomplete);
        CHECK(shortfallNaming(result, "rejected"));
        CHECK(reportOf(result).find(itemRejectionName(result.selectionItems.rejected.front().reason)) != string::npos);
    }
}

TEST_CASE("pipelineM4: a placeholder skipped for bad data fails the run with exit 7 and is printed") {
    nlohmann::json unknownEquipment = SmallDay::placeholderJson("1", 1);
    unknownEquipment["ZZNA_EQUIP_SIZE"] = "48F";
    nlohmann::json unreadableLoads = SmallDay::placeholderJson("1", 1);
    unreadableLoads["NO_OF_LOADS"] = "two";
    // The Validator already errors on an unreadable load count, and the lower exit code wins.
    for (const auto& badAndOutcome : {pair{unknownEquipment, RunOutcome::SelectionIncomplete},
                                      pair{unreadableLoads, RunOutcome::ValidationErrors}}) {
        const nlohmann::json& bad = badAndOutcome.first;
        const RunOutcome outcome = badAndOutcome.second;
        CAPTURE(bad.dump());
        const SmallDay day("skipped", "", "," + bad.dump());
        const PipelineResult result = Pipeline::run(day.inputs);
        REQUIRE(result.reservationBook.skipped.size() == 1);
        CHECK(runOutcome(result) == outcome);
        CHECK(shortfallNaming(result, "placeholder rows skipped"));
        CHECK(reportOf(result).find(describeSkipReason(result.reservationBook.skipped.front().reason)) != string::npos);
    }
}

TEST_CASE("pipelineM4: a lane and date reserved on two rows fails the run with exit 7, both rows printed") {
    const SmallDay day("duplicateReservation", "", "," + SmallDay::placeholderJson("1", 1).dump());
    const PipelineResult result = Pipeline::run(day.inputs);
    REQUIRE(result.reservationBook.skipped.size() == 2);
    CHECK(runOutcome(result) == RunOutcome::SelectionIncomplete);
    CHECK(shortfallNaming(result, "placeholder rows skipped"));
    CHECK(reportOf(result).find("duplicate lane and ship date") != string::npos);
}

TEST_CASE("pipelineM4: a placeholder with zero loads or after the window leaves the run complete") {
    nlohmann::json zeroLoads = SmallDay::placeholderJson("1", 0);
    nlohmann::json afterWindow = SmallDay::placeholderJson("1", 1);
    afterWindow["DATFR_TA"] = "2026-08-20";
    afterWindow["DATTO_TA"] = "2026-08-21";
    for (const nlohmann::json& normal : {zeroLoads, afterWindow}) {
        CAPTURE(normal.dump());
        const SmallDay day("normalSkip", "", "," + normal.dump());
        const PipelineResult result = Pipeline::run(day.inputs);
        CHECK(result.reservationBook.skipped.size() == 1);
        CHECK(runOutcome(result) == RunOutcome::Complete);
    }
}

TEST_CASE("pipelineM4: the report names lanes reserved with no demand") {
    const SmallDay day("noDemandLane", "", "," + SmallDay::placeholderJson("9", 3).dump());
    const PipelineResult result = Pipeline::run(day.inputs);
    REQUIRE(result.reservationCoverage.reservationsWithoutDemand.size() == 1);
    CHECK(result.reservationCoverage.penaltyExposureTrucks == 3);
    CHECK(reportOf(result).find("9 -> 2 TL") != string::npos);
}

TEST_CASE("pipelineM4: the report's Result line names every shortfall") {
    SmallDay day("result");
    PipelineResult result = Pipeline::run(day.inputs);
    CHECK(reportOf(result).find("Result: complete") != string::npos);
    result.selectionPlan.routes.front().overBudget = true;
    const string report = reportOf(result);
    CHECK(report.find("Result: INCOMPLETE (exit 7)") != string::npos);
    CHECK(report.find("time budget") != string::npos);
}

TEST_CASE("pipelineM4: two priorities competing for the trucks ship in the order the setting names") {
    nlohmann::json urgentByZero = SmallDay::lineJson("GOOD", 2000, "TL");
    nlohmann::json urgentByEleven = SmallDay::lineJson("OTHER", 2000, "TL");
    urgentByEleven["TPRIO"] = 11;
    const string lines = "," + urgentByZero.dump() + "," + urgentByEleven.dump();
    for (const bool zeroIsMostUrgent : {true, false}) {
        CAPTURE(zeroIsMostUrgent);
        SmallDay day("priority", lines);
        // This product's unit loads are small enough for a truck to carry 150 of them.
        day.inputs.paramsPath =
            paramsWith(day, {{"priorityZeroIsMostUrgent", zeroIsMostUrgent}, {"maxUnitLoadsPerTruck", 200}});
        const PipelineResult result = Pipeline::run(day.inputs);
        REQUIRE(result.ranSelection);
        REQUIRE_FALSE(result.selectionPlan.leftOver.empty());
        const double zeroShipped = unitLoadsShipped(result, 0);
        const double elevenShipped = unitLoadsShipped(result, 11);
        if (zeroIsMostUrgent) {
            CHECK(zeroShipped > elevenShipped);
        } else {
            CHECK(elevenShipped > zeroShipped);
        }
    }
}

TEST_CASE("pipelineM4: the report prints the priority direction under provisional rules") {
    for (const bool zeroIsMostUrgent : {true, false}) {
        SmallDay day("priorityReport");
        day.inputs.paramsPath = paramsWith(day, {{"priorityZeroIsMostUrgent", zeroIsMostUrgent}});
        const string report = reportOf(Pipeline::run(day.inputs));
        CHECK(report.find("PROVISIONAL BUSINESS RULES") != string::npos);
        CHECK(report.find(zeroIsMostUrgent ? "TPRIO 0 ships first" : "TPRIO 11 ships first") != string::npos);
    }
}

TEST_CASE("pipelineM4: the report names stacks mixing ship dates and how far their lines are pulled forward") {
    SmallDay day("mixedDates");
    const string report = reportOf(Pipeline::run(day.inputs));
    const size_t provisional = report.find("PROVISIONAL BUSINESS RULES");
    REQUIRE(provisional != string::npos);
    CHECK(report.find("mixed ship dates") < provisional);
    CHECK(report.find("later lines pulled forward up to") != string::npos);
    CHECK(report.find("a stack ships on its earliest line's date", provisional) != string::npos);
}

TEST_CASE("pipelineM4: the report lists the footprint rule and PAL reading as provisional rules") {
    SmallDay day("footprintReport");
    const string report = reportOf(Pipeline::run(day.inputs));
    const size_t provisional = report.find("PROVISIONAL BUSINESS RULES");
    REQUIRE(provisional != string::npos);
    CHECK(report.find("footprintRule = WrittenRule", provisional) != string::npos);
    CHECK(report.find("Rev 8 question 3", provisional) != string::npos);
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

TEST_CASE("pipelineM4: I10 holds on a rejected item only while its unit loads are counted") {
    nlohmann::json priorityTwelve = SmallDay::lineJson("GOOD", 16, "TL");
    priorityTwelve["TPRIO"] = 12;
    const SmallDay day("i10Rejected", "," + priorityTwelve.dump());
    const PipelineResult result = Pipeline::run(day.inputs);
    REQUIRE(result.selectionItems.rejected.size() == 1);
    CHECK_FALSE(hasI10(result.selectionViolations));

    SelectionItems rejectionDropped = result.selectionItems;
    rejectionDropped.rejected.clear();
    CHECK(hasI10(invariantsOf(result, rejectionDropped, result.selectionPlan)));
}

TEST_CASE("pipelineM4: I10 holds on a skipped placeholder's lane only while its goods are counted") {
    nlohmann::json otherLane = SmallDay::lineJson("GOOD", 16, "TL");
    otherLane["LOCFRNO"] = "3";
    nlohmann::json unknownEquipment = SmallDay::placeholderJson("3", 1);
    unknownEquipment["ZZNA_EQUIP_SIZE"] = "48F";
    const SmallDay day("i10Skipped", "," + otherLane.dump(), "," + unknownEquipment.dump());
    const PipelineResult result = Pipeline::run(day.inputs);
    REQUIRE(result.reservationBook.skipped.size() == 1);
    CHECK_FALSE(hasI10(result.selectionViolations));

    SelectionPlan leftOverDropped = result.selectionPlan;
    const auto noReservation = remove_if(leftOverDropped.leftOver.begin(), leftOverDropped.leftOver.end(),
                                         [](const LeftOverItem& left) { return left.reason == LeftOverReason::NoReservation; });
    REQUIRE(noReservation != leftOverDropped.leftOver.end());
    leftOverDropped.leftOver.erase(noReservation, leftOverDropped.leftOver.end());
    CHECK(hasI10(invariantsOf(result, result.selectionItems, leftOverDropped)));
}

TEST_CASE("pipelineM4: I10 holds on a clean run and the run exits 0") {
    const SmallDay day("i10Clean");
    const PipelineResult result = Pipeline::run(day.inputs);
    CHECK(result.selectionItems.unitLoadsIn > 0.0);
    CHECK_FALSE(hasI10(result.selectionViolations));
    CHECK(runOutcome(result) == RunOutcome::Complete);
}

TEST_CASE("pipelineM4: an assignment that drops an item breaks I10 and the run does not exit 0") {
    SmallDay day("i10Dropped");
    PipelineResult result = Pipeline::run(day.inputs);
    REQUIRE_FALSE(result.selectionPlan.trucks.empty());
    result.selectionPlan.trucks.front().items.pop_back();
    result.selectionViolations = invariantsOf(result, result.selectionItems, result.selectionPlan);
    CHECK(hasI10(result.selectionViolations));
    CHECK(runOutcome(result) != RunOutcome::Complete);
}
