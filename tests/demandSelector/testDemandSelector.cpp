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

STRRecord line(const string& shipDate, const string& deliveryDate = "2026-10-12") {
    STRRecord record;
    record.datfr_ta = shipDate;
    record.datto_ta = deliveryDate;
    return record;
}

PlaceholderRecord placeholder(const string& shipDate, int trucks) {
    PlaceholderRecord record;
    record.locfrno = "1";
    record.loctono = "2";
    record.ship_cond = "TL";
    record.datfr_ta = shipDate;
    record.no_of_loads = trucks;
    return record;
}

// Ship dates 1 to 5 October.
const vector<STRRecord>& octoberLines() {
    static const vector<STRRecord> lines{line("2026-10-01"), line("2026-10-02"),
                                         line("2026-10-03"), line("2026-10-04"),
                                         line("2026-10-05")};
    return lines;
}

// A calendar of weekday rows for every working day, each shipping two to three days later,
// Thursday to four, as the client's extracts carry.
const vector<CTLRecord>& weekdayCalendar() {
    static const vector<CTLRecord> calendar{
        {"", "MONDAY", 2, 3, 0}, {"", "TUESDAY", 2, 3, 0}, {"", "WEDNESDAY", 2, 3, 0},
        {"", "THURSDAY", 2, 4, 0}, {"", "FRIDAY", 3, 5, 0}};
    return calendar;
}

ShipDateCoverage coverageOf(const string& rule, const vector<CTLRecord>& calendar = {}) {
    return coverShipDates(parseDemandSelector(rule), calendar);
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

TEST_CASE("demandSelector: shipDates with one date covers that day alone") {
    CHECK(coverageOf("shipDates:2026-10-02").shipDates == vector<string>{"2026-10-02"});
}

TEST_CASE("demandSelector: shipDates with two dates covers every day from the first to the last") {
    CHECK(coverageOf("shipDates:2026-10-30:2026-11-02").shipDates
          == vector<string>{"2026-10-30", "2026-10-31", "2026-11-01", "2026-11-02"});
}

TEST_CASE("demandSelector: wholeExtract covers no particular date") {
    const ShipDateCoverage coverage = coverageOf("wholeExtract");
    CHECK(coverage.shipDates.empty());
    CHECK_FALSE(coverage.calendarWindow);
}

TEST_CASE("demandSelector: calendar takes the ship dates from the plan date's CTL row") {
    const ShipDateCoverage coverage = coverageOf("calendar:2026-10-05", weekdayCalendar());
    CHECK(coverage.shipDates == vector<string>{"2026-10-07", "2026-10-08"});
    REQUIRE(coverage.calendarWindow);
    CHECK(coverage.calendarWindow->planDate == "2026-10-05");
}

TEST_CASE("demandSelector: Wednesday and Thursday plans read their own rows like any other day") {
    CHECK(coverageOf("calendar:2026-10-07", weekdayCalendar()).shipDates
          == vector<string>{"2026-10-09", "2026-10-10"});
    CHECK(coverageOf("calendar:2026-10-08", weekdayCalendar()).shipDates
          == vector<string>{"2026-10-10", "2026-10-11", "2026-10-12"});
}

TEST_CASE("demandSelector: a dated calendar row overrides the plan's weekday row") {
    vector<CTLRecord> calendar = weekdayCalendar();
    calendar.push_back({"2026-10-07", "", 2, 5, 0});
    const ShipDateCoverage coverage = coverageOf("calendar:2026-10-07", calendar);
    CHECK(coverage.shipDates
          == vector<string>{"2026-10-09", "2026-10-10", "2026-10-11", "2026-10-12"});
    REQUIRE(coverage.calendarWindow);
    CHECK(coverage.calendarWindow->fromDatedRow);
}

TEST_CASE("demandSelector: a plan date the calendar has no row for is an error naming the weekday") {
    try {
        coverageOf("calendar:2026-10-10", weekdayCalendar());
        FAIL("a Saturday plan with no Saturday row was given ship dates");
    } catch (const invalid_argument& error) {
        CHECK(string(error.what()).find("Saturday") != string::npos);
    }
}

TEST_CASE("demandSelector: a calendar window longer than a run may cover is an error") {
    CHECK_THROWS_AS(coverageOf("calendar:2026-10-05", {{"", "MONDAY", 2, 400, 0}}),
                    invalid_argument);
}

TEST_CASE("demandSelector: a line counts toward its own ship date only") {
    const DemandSelection selection = selectShipDate(octoberLines(), "2026-10-03");
    CHECK(selection.shipDate == "2026-10-03");
    CHECK(selection.selected == vector<bool>{false, false, true, false, false});
    CHECK(selection.selectedLines == 1);
}

TEST_CASE("demandSelector: a ship date reads DATFR_TA, never the delivery date") {
    const vector<STRRecord> demand{line("2026-10-03", "2026-10-03"), line("2026-10-01", "2026-10-03")};
    CHECK(selectShipDate(demand, "2026-10-03").selected == vector<bool>{true, false});
}

TEST_CASE("demandSelector: an empty ship date selects every line, dated or not") {
    vector<STRRecord> demand = octoberLines();
    demand.push_back(line(""));
    CHECK(selectShipDate(demand, "").selectedLines == 6);
}

TEST_CASE("demandSelector: coverage counts lines before, on and after the run's dates") {
    vector<STRRecord> demand = octoberLines();
    demand.push_back(line(""));
    demand.push_back(line("2026-02-30"));
    demand.push_back(line("03/10/2026"));
    ShipDateCoverage coverage = coverageOf("shipDates:2026-10-02:2026-10-03");
    countCoverage(coverage, demand, {}, {});
    CHECK(coverage.linesBeforeFirstShipDate == 1);
    CHECK(coverage.linesOnShipDates == 2);
    CHECK(coverage.linesAfterLastShipDate == 2);
    CHECK(coverage.undatedLines == vector<size_t>{5, 6, 7});
}

TEST_CASE("demandSelector: placeholders for dates the run does not cover are counted") {
    const vector<PlaceholderRecord> placeholders{placeholder("2026-10-01", 3),
                                                 placeholder("2026-10-02", 2),
                                                 placeholder("2026-10-04", 4),
                                                 placeholder("", 7)};
    ShipDateCoverage coverage = coverageOf("shipDates:2026-10-02:2026-10-03");
    countCoverage(coverage, {}, placeholders, {false, false, false, false});
    CHECK(coverage.reservedTrucksOutsideShipDates == 3 + 4 + 7);
}

TEST_CASE("demandSelector: a placeholder the Validator rejected reserves nothing anywhere") {
    ShipDateCoverage coverage = coverageOf("shipDates:2026-10-02");
    countCoverage(coverage, {}, {placeholder("2026-10-01", 3)}, {true});
    CHECK(coverage.reservedTrucksOutsideShipDates == 0);
}

TEST_CASE("demandSelector: under wholeExtract every line is on the run's dates and none is undated") {
    vector<STRRecord> demand = octoberLines();
    demand.push_back(line(""));
    ShipDateCoverage coverage = coverageOf("wholeExtract");
    countCoverage(coverage, demand, {placeholder("2026-10-01", 3)}, {false});
    CHECK(coverage.linesOnShipDates == 6);
    CHECK(coverage.undatedLines.empty());
    CHECK(coverage.reservedTrucksOutsideShipDates == 0);
}

TEST_CASE("demandSelector: placeholder flags not parallel to the placeholders are rejected") {
    ShipDateCoverage coverage = coverageOf("shipDates:2026-10-02");
    CHECK_THROWS_AS(countCoverage(coverage, {}, {placeholder("2026-10-01", 3)}, {}),
                    invalid_argument);
}

TEST_CASE("demandSelector: the chosen rule is described for printing beside its figure") {
    CHECK(describeDemandSelector(parseDemandSelector("wholeExtract")) == "wholeExtract");
    CHECK(describeDemandSelector(parseDemandSelector("shipDates:2026-10-02"))
          == "shipDates(2026-10-02)");
    CHECK(describeDemandSelector(parseDemandSelector("shipDates:2026-10-02:2026-10-03"))
          == "shipDates(2026-10-02 to 2026-10-03)");
    CHECK(describeDemandSelector(parseDemandSelector("calendar:2026-10-08"))
          == "calendar(plan 2026-10-08 Thursday)");
}

TEST_CASE("demandSelector: a missing rule is an error naming the argument, never a default") {
    const string message = parseErrorMessage("");
    CHECK(message.find(kDemandRuleArgument) != string::npos);
    CHECK(message.find("no default") != string::npos);
}

TEST_CASE("demandSelector: an unknown rule is an error naming the argument") {
    for (const string rule : {"WholeExtract", "shipdates:2026-10-02", "dueBy:2026-10-02",
                              "schedule:2026-10-05", "availableBy:2026-10-02", "all", " "}) {
        CAPTURE(rule);
        CHECK(parseErrorMessage(rule).find(kDemandRuleArgument) != string::npos);
    }
}

TEST_CASE("demandSelector: a missing or extra date is an error") {
    for (const string rule : {"shipDates", "shipDates:", "calendar", "calendar:",
                              "wholeExtract:2026-10-01", "calendar:2026-10-01:2026-10-02",
                              "shipDates:2026-10-01:2026-10-02:2026-10-03"}) {
        CAPTURE(rule);
        CHECK(parseErrorMessage(rule).find(kDemandRuleArgument) != string::npos);
    }
}

TEST_CASE("demandSelector: a date that is not a real YYYY-MM-DD date is an error") {
    for (const string rule : {"shipDates:2026-02-29", "shipDates:2026-04-31",
                              "shipDates:2026-00-10", "shipDates:2026-10-00",
                              "shipDates:2026-1-02", "calendar:02/10/2026",
                              "calendar:2026-10-02 ", "calendar:2026-1O-02",
                              "shipDates:x:2026-10-02"}) {
        CAPTURE(rule);
        CHECK(parseErrorMessage(rule).find(kDemandRuleArgument) != string::npos);
    }
}

TEST_CASE("demandSelector: 29 February is a real date only in a leap year") {
    CHECK_NOTHROW(parseDemandSelector("shipDates:2028-02-29"));
    CHECK_NOTHROW(parseDemandSelector("calendar:2000-02-29"));
    CHECK_THROWS_AS(parseDemandSelector("shipDates:2100-02-29"), invalid_argument);
}

TEST_CASE("demandSelector: ship dates that run backwards are an error") {
    const string message = parseErrorMessage("shipDates:2026-10-03:2026-10-01");
    CHECK(message.find(kDemandRuleArgument) != string::npos);
    CHECK(message.find("before the first") != string::npos);
}

TEST_CASE("demandSelector: more ship dates than one run may cover are an error") {
    CHECK_NOTHROW(parseDemandSelector("shipDates:2026-10-01:2026-10-31"));
    CHECK(parseErrorMessage("shipDates:2026-10-01:2026-11-01").find("more than 31") != string::npos);
    CHECK(parseErrorMessage("shipDates:2026-10-01:2062-10-01").find(kDemandRuleArgument)
          != string::npos);
}

TEST_CASE("demandSelector: a selector built in code with a bad date is rejected when applied") {
    DemandSelector selector;
    selector.rule = DemandRule::ShipDates;
    CHECK_THROWS_AS(coverShipDates(selector, {}), invalid_argument);
    selector.firstShipDate = "2026-10-05";
    selector.lastShipDate = "2026-10-01";
    CHECK_THROWS_AS(coverShipDates(selector, {}), invalid_argument);
    selector.rule = DemandRule::Calendar;
    CHECK_THROWS_AS(coverShipDates(selector, weekdayCalendar()), invalid_argument);
}

TEST_CASE("demandSelector: each extract's calendar dates and line counts match the raw files" * doctest::skip(!allExtractsPresent())) {
    struct ExpectedRun {
        string planDate;
        vector<string> shipDates;
        vector<size_t> linesPerShipDate;
        size_t linesBefore;
        size_t linesAfter;
    };
    // Counted directly from each Demand JSON's CTL rows and STR ship dates, independently of
    // this code. 3 Sep plans on a Wednesday whose dated row (2 to 5 days) overrides the
    // WEDNESDAY row (2 to 3).
    const PerDay<ExpectedRun> expected{{
        {"2026-08-17", {"2026-08-19", "2026-08-20"}, {4574, 1821}, 8, 17954},
        {"2026-08-31", {"2026-09-02", "2026-09-03"}, {4094, 1766}, 12, 17590},
        {"2026-09-01", {"2026-09-03", "2026-09-04"}, {3860, 1966}, 6, 17171},
        {"2026-09-02", {"2026-09-04", "2026-09-05", "2026-09-06", "2026-09-07"},
         {3534, 1427, 1473, 1476}, 1, 13998},
    }};
    for (size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const DemandFile& demand = pipelineRuns()[dayIndex].demand;
        ShipDateCoverage coverage =
            coverShipDates(parseDemandSelector("calendar:" + expected[dayIndex].planDate), demand.ctl);
        countCoverage(coverage, demand.str, {}, {});
        CHECK(coverage.shipDates == expected[dayIndex].shipDates);
        CHECK(coverage.linesBeforeFirstShipDate == expected[dayIndex].linesBefore);
        CHECK(coverage.linesAfterLastShipDate == expected[dayIndex].linesAfter);
        CHECK(coverage.undatedLines.empty());
        for (size_t dateIndex = 0; dateIndex < coverage.shipDates.size(); ++dateIndex) {
            CAPTURE(coverage.shipDates[dateIndex]);
            CHECK(selectShipDate(demand.str, coverage.shipDates[dateIndex]).selectedLines
                  == expected[dayIndex].linesPerShipDate[dateIndex]);
        }
        CHECK(selectShipDate(demand.str, "").selectedLines == expectedM1::demandLines[dayIndex]);
    }
}
