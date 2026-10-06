#include "doctest.h"
#include "../importer/crossDayFixtures.hpp"
#include "demandSelector.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;
using namespace crossDayTests;

namespace {

STRRecord line(const string& earliestArrival, const string& latestArrival) {
    STRRecord record;
    record.datfr_ta = earliestArrival;
    record.datto_ta = latestArrival;
    return record;
}

// Arrival windows 1-3, 2-4 and 3-5 October.
const vector<STRRecord>& octoberLines() {
    static const vector<STRRecord> lines{line("2026-10-01", "2026-10-03"),
                                         line("2026-10-02", "2026-10-04"),
                                         line("2026-10-03", "2026-10-05")};
    return lines;
}

vector<bool> selectedFlags(const string& rule, const vector<STRRecord>& demand) {
    return selectDemand(demand, parseDemandSelector(rule)).selected;
}

string parseErrorMessage(const string& rule) {
    try {
        parseDemandSelector(rule);
    } catch (const invalid_argument& error) {
        return error.what();
    }
    return "";
}

} // namespace

TEST_CASE("demandSelector: wholeExtract selects every line, dated or not") {
    vector<STRRecord> demand = octoberLines();
    demand.push_back(line("", ""));
    const DemandSelection selection = selectDemand(demand, parseDemandSelector("wholeExtract"));
    CHECK(selection.selectedLines == 4);
    CHECK(selection.undatedLines.empty());
}

TEST_CASE("demandSelector: dueBy selects lines whose latest arrival is on or before the date") {
    CHECK(selectedFlags("dueBy:2026-10-04", octoberLines()) == vector<bool>{true, true, false});
    CHECK(selectedFlags("dueBy:2026-10-02", octoberLines()) == vector<bool>{false, false, false});
}

TEST_CASE("demandSelector: availableBy selects lines whose earliest arrival is on or before the date") {
    CHECK(selectedFlags("availableBy:2026-10-02", octoberLines())
          == vector<bool>{true, true, false});
}

TEST_CASE("demandSelector: window selects lines whose latest arrival falls inside it") {
    CHECK(selectedFlags("window:2026-10-04:2026-10-05", octoberLines())
          == vector<bool>{false, true, true});
    CHECK(selectedFlags("window:2026-10-04:2026-10-04", octoberLines())
          == vector<bool>{false, true, false});
}

TEST_CASE("demandSelector: a line with a blank or malformed date is listed, not selected") {
    const vector<STRRecord> demand{line("2026-10-01", "2026-10-03"), line("2026-10-01", ""),
                                   line("2026-10-01", "2026-13-01"),
                                   line("2026-10-01", "03/10/2026")};
    const DemandSelection selection = selectDemand(demand, parseDemandSelector("dueBy:2026-10-31"));
    CHECK(selection.selectedLines == 1);
    CHECK(selection.undatedLines == vector<size_t>{1, 2, 3});
}

TEST_CASE("demandSelector: availableBy reads only the earliest arrival date") {
    const vector<STRRecord> demand{line("2026-10-01", "")};
    const DemandSelection selection =
        selectDemand(demand, parseDemandSelector("availableBy:2026-10-01"));
    CHECK(selection.selectedLines == 1);
    CHECK(selection.undatedLines.empty());
}

TEST_CASE("demandSelector: the chosen rule is described for printing beside its figure") {
    CHECK(describeDemandSelector(parseDemandSelector("wholeExtract")) == "wholeExtract");
    CHECK(describeDemandSelector(parseDemandSelector("dueBy:2026-10-02")) == "dueBy(2026-10-02)");
    CHECK(describeDemandSelector(parseDemandSelector("availableBy:2026-10-02"))
          == "availableBy(2026-10-02)");
    CHECK(describeDemandSelector(parseDemandSelector("window:2026-10-01:2026-10-03"))
          == "window(2026-10-01, 2026-10-03)");
}

TEST_CASE("demandSelector: a missing rule is an error naming the argument, never a default") {
    const string message = parseErrorMessage("");
    CHECK(message.find(kDemandRuleArgument) != string::npos);
    CHECK(message.find("no default") != string::npos);
}

TEST_CASE("demandSelector: an unknown rule is an error naming the argument") {
    for (const string rule : {"WholeExtract", "dueby:2026-10-02", "due:2026-10-02", "all", " "}) {
        CAPTURE(rule);
        CHECK(parseErrorMessage(rule).find(kDemandRuleArgument) != string::npos);
    }
}

TEST_CASE("demandSelector: a missing or extra date is an error") {
    for (const string rule : {"dueBy", "dueBy:", "availableBy", "window:2026-10-01",
                              "wholeExtract:2026-10-01", "dueBy:2026-10-01:2026-10-02",
                              "window:2026-10-01:2026-10-02:2026-10-03"}) {
        CAPTURE(rule);
        CHECK(parseErrorMessage(rule).find(kDemandRuleArgument) != string::npos);
    }
}

TEST_CASE("demandSelector: a date that is not a real YYYY-MM-DD date is an error") {
    for (const string rule : {"dueBy:2026-02-29", "dueBy:2026-04-31", "dueBy:2026-00-10",
                              "dueBy:2026-10-00", "dueBy:2026-1-02", "dueBy:02/10/2026",
                              "dueBy:2026-10-02 ", "dueBy:2026-1O-02", "window:x:2026-10-02"}) {
        CAPTURE(rule);
        CHECK(parseErrorMessage(rule).find(kDemandRuleArgument) != string::npos);
    }
}

TEST_CASE("demandSelector: 29 February is a real date only in a leap year") {
    CHECK_NOTHROW(parseDemandSelector("dueBy:2028-02-29"));
    CHECK_NOTHROW(parseDemandSelector("dueBy:2000-02-29"));
    CHECK_THROWS_AS(parseDemandSelector("dueBy:2100-02-29"), invalid_argument);
}

TEST_CASE("demandSelector: a window that ends before it starts is an error") {
    const string message = parseErrorMessage("window:2026-10-03:2026-10-01");
    CHECK(message.find(kDemandRuleArgument) != string::npos);
    CHECK(message.find("before it starts") != string::npos);
}

TEST_CASE("demandSelector: a selector built in code with a bad date is rejected when applied") {
    DemandSelector selector;
    selector.rule = DemandRule::DueBy;
    CHECK_THROWS_AS(selectDemand(octoberLines(), selector), invalid_argument);
    selector.rule = DemandRule::Window;
    selector.fromDate = "2026-10-05";
    selector.toDate = "2026-10-01";
    CHECK_THROWS_AS(selectDemand(octoberLines(), selector), invalid_argument);
}

TEST_CASE("demandSelector: line counts on every extract match the raw files" * doctest::skip(!allExtractsPresent())) {
    struct ExpectedSelection {
        string rule;
        size_t selectedLines;
    };
    // Counted directly from each Demand JSON's STR dates, independently of this code.
    const PerDay<vector<ExpectedSelection>> expected{{
        {{"dueBy:2026-08-20", 1177}, {"availableBy:2026-08-19", 4582},
         {"window:2026-08-21:2026-08-22", 2158}},
        {{"dueBy:2026-09-05", 3163}, {"availableBy:2026-09-04", 7843},
         {"window:2026-09-06:2026-09-07", 2889}},
        {{"dueBy:2026-09-05", 2147}, {"availableBy:2026-09-04", 5832},
         {"window:2026-09-06:2026-09-07", 2038}},
        {{"dueBy:2026-09-06", 1861}, {"availableBy:2026-09-05", 4962},
         {"window:2026-09-07:2026-09-08", 1795}},
    }};
    for (size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const vector<STRRecord>& demand = pipelineRuns()[dayIndex].demand.str;
        const DemandSelection whole = selectDemand(demand, parseDemandSelector("wholeExtract"));
        CHECK(whole.selectedLines == expectedM1::demandLines[dayIndex]);
        for (const ExpectedSelection& expectation : expected[dayIndex]) {
            CAPTURE(expectation.rule);
            const DemandSelection selection =
                selectDemand(demand, parseDemandSelector(expectation.rule));
            CHECK(selection.selectedLines == expectation.selectedLines);
            CHECK(selection.undatedLines.empty());
        }
    }
}
