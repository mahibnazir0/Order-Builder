#include "doctest.h"
#include "../m4SmallDay.hpp"
#include "../m4SyntheticDay.hpp"
#include "benchmarkHarness.hpp"
#include "pipeline.hpp"
#include "selectionItem.hpp"
#include "selectionReporter.hpp"

#include <algorithm>
#include <cstddef>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using namespace ob;
using m4SmallDay::SmallDay;

namespace {

// The two lines every SmallDay starts with; a generated line comes after them.
constexpr size_t kGeneratedLineIndex = 2;

struct MalformedLine {
    nlohmann::json line;
    string field;
};

// One demand line with one field made bad, chosen at random from the fields selection judges.
MalformedLine genMalformed(mt19937& random) {
    MalformedLine malformed{SmallDay::lineJson(random() % 2 == 0 ? "GOOD" : "OTHER",
                                               8 * static_cast<int>(1 + random() % 6), "TL"),
                            ""};
    nlohmann::json& line = malformed.line;
    switch (random() % 8) {
    case 0: line["DATFR_TA"] = "2026-02-30"; malformed.field = "DATFR_TA"; break;
    case 1: line["DATTO_TA"] = "2026-08-01"; malformed.field = "DATTO_TA"; break;
    case 2: line["AVAIL_DATE"] = ""; malformed.field = "AVAIL_DATE"; break;
    case 3: line["AVAIL_DATE"] = "06/08/2026"; malformed.field = "AVAIL_DATE"; break;
    case 4: line["TPRIO"] = 12; malformed.field = "TPRIO"; break;
    case 5: line["TPRIO"] = -3; malformed.field = "TPRIO"; break;
    case 6: line.erase("TPRIO"); malformed.field = "TPRIO"; break;
    default: line["TRANS"] = -8; malformed.field = "TRANS"; break;
    }
    return malformed;
}

// `count` items of a random fraction of a unit load each, on one lane with room for all of them.
vector<SelectionItem> genFractional(mt19937& random, size_t count) {
    uniform_real_distribution<double> fraction(0.01, 0.99);
    vector<SelectionItem> items;
    items.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        SelectionItem built = m4SyntheticDay::item(0, index, 50.0);
        built.unitLoads = fraction(random);
        items.push_back(built);
    }
    return items;
}

// Demand lines on one lane, each randomly of the do-not-mix planner S02 or the normal planner S01.
string genSegregated(mt19937& random, size_t count) {
    string lines;
    for (size_t index = 0; index < count; ++index) {
        nlohmann::json line = SmallDay::lineJson(random() % 2 == 0 ? "GOOD" : "OTHER",
                                                 8 * static_cast<int>(1 + random() % 4), "TL");
        line["IDPR"] = "G" + to_string(index);
        line["PLANNER_SNP"] = random() % 2 == 0 ? "S02" : "S01";
        lines += "," + line.dump();
    }
    return lines;
}

const string kDoNotMixS02 = R"({"PLANNER_SNP":"S02","LOCFRNO":"1"})";

bool namesLine(const PipelineResult& result, size_t lineIndex) {
    const vector<ValidationIssue>& issues = result.validation.issues;
    const vector<RejectedItem>& rejected = result.selectionItems.rejected;
    return any_of(issues.begin(), issues.end(),
                  [&](const ValidationIssue& issue) { return issue.line_index == static_cast<int>(lineIndex); })
        || any_of(rejected.begin(), rejected.end(),
                  [&](const RejectedItem& item) { return item.lineIndex == lineIndex; });
}

string reportOf(const PipelineResult& result) {
    SelectionReportContext context;
    context.routing = result.selectionParams.routing;
    context.settings = result.selectionParams.selector;
    context.shortfalls = selectionShortfalls(result);
    context.priorityZeroIsMostUrgent = result.selectionParams.priorityZeroIsMostUrgent;
    context.maxDaysPulledForward = result.selectionParams.maxDaysPulledForward;
    context.shipDates = result.selectionParams.shipDates;
    context.defaultEquipment = result.selectionParams.reservations.defaultEquipment;
    ostringstream out;
    printSelectionReport(out, result, result.reservationBook, result.selectionItems, result.laneClassification,
                         result.selectionPlan, context);
    return out.str();
}

} // anonymous namespace

TEST_CASE("selectionProperties: genMalformed - a bad field is rejected, named on its line, and fails the run") {
    mt19937 random(20261101);
    for (int trial = 0; trial < 16; ++trial) {
        const MalformedLine malformed = genMalformed(random);
        CAPTURE(malformed.line.dump());
        const SmallDay day("malformed", "," + malformed.line.dump());
        const PipelineResult result = Pipeline::run(day.inputs);
        CHECK(namesLine(result, kGeneratedLineIndex));
        CHECK(runOutcome(result) != RunOutcome::Complete);
        for (const RejectedItem& rejected : result.selectionItems.rejected) {
            CHECK(reportOf(result).find(itemRejectionName(rejected.reason)) != string::npos);
        }
    }
}

TEST_CASE("selectionProperties: genFractional - twelve items of 0.083 unit loads take twelve positions") {
    vector<SelectionItem> twelfths(12, m4SyntheticDay::item(0, 0, 50.0));
    for (SelectionItem& item : twelfths) item.unitLoads = 1.0 / 12.0;
    const m4SyntheticDay::Day day =
        m4SyntheticDay::dayOf({m4SyntheticDay::group("2027")}, 1, twelfths, {m4SyntheticDay::reserved("2027", 1)});
    const SelectionPlan plan = m4SyntheticDay::plan(day);
    REQUIRE(plan.trucks.size() == 1);
    CHECK(plan.trucks.front().items.size() == 12);
    CHECK(plan.trucks.front().floor.positions == 12);
    CHECK(plan.trucks.front().unitLoads == doctest::Approx(1.0));
}

TEST_CASE("selectionProperties: genFractional - every fractional item takes a whole position of its own") {
    mt19937 random(20261102);
    uniform_int_distribution<size_t> itemCount(1, 30);
    for (int trial = 0; trial < 20; ++trial) {
        const size_t count = itemCount(random);
        CAPTURE(count);
        const m4SyntheticDay::Day day = m4SyntheticDay::dayOf({m4SyntheticDay::group("2027")}, count,
                                                              genFractional(random, count),
                                                              {m4SyntheticDay::reserved("2027", 1)});
        const SelectionPlan plan = m4SyntheticDay::plan(day);
        size_t onTrucks = 0;
        for (const PlannedTruck& truck : plan.trucks) {
            CHECK(truck.floor.positions == static_cast<int>(truck.items.size()));
            onTrucks += truck.items.size();
        }
        CHECK(onTrucks + plan.leftOver.size() == count);
        CHECK(checkInvariants(day.run, day.book, day.items, plan, 64.0).empty());
    }
}

TEST_CASE("selectionProperties: genSegregated - I3 holds and no truck mixes a do-not-mix planner") {
    mt19937 random(20261103);
    size_t segregatedTrucks = 0;
    for (int trial = 0; trial < 10; ++trial) {
        const string lines = genSegregated(random, 2 + random() % 6);
        CAPTURE(lines);
        const SmallDay day("segregated", lines, "", kDoNotMixS02);
        const PipelineResult result = Pipeline::run(day.inputs);
        REQUIRE(result.ranSelection);
        for (const InvariantViolation& violation : result.selectionViolations) {
            CHECK_MESSAGE(violation.invariant != "I3", violation.detail);
        }
        for (const PlannedTruck& truck : result.selectionPlan.trucks) {
            set<bool> doNotMix;
            for (const size_t index : truck.items) {
                for (const size_t lineIndex : result.selectionItems.items[index].lineIndices) {
                    doNotMix.insert(result.join.lines[lineIndex].str->planner_snp == "S02");
                }
            }
            CHECK(doNotMix.size() == 1);
            if (doNotMix.count(true)) ++segregatedTrucks;
        }
    }
    CHECK(segregatedTrucks > 0);
}
