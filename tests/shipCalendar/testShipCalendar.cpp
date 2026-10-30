#include "doctest.h"
#include "../importer/crossDayFixtures.hpp"
#include "shipCalendar.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;
using namespace crossDayTests;

namespace {

// The August CTL rows as shipped: weekday rows plus stale dated rows for 30 Jun - 2 Jul.
vector<CTLRecord> augustCalendar() {
    return {
        {"", "FRIDAY", 3, 5, 4},       {"", "MONDAY", 2, 3, 2},
        {"", "TUESDAY", 2, 3, 2},      {"2026-06-30", "", 2, 4, 2},
        {"2026-07-01", "", 4, 5, 4},   {"2026-07-02", "", 5, 6, 5},
        {"", "WEDNESDAY", 2, 3, 3},    {"", "THURSDAY", 2, 4, 4},
    };
}

// 2026-08-04 is a Tuesday: the window is 6-7 Aug.
const string kTuesday = "2026-08-04";

} // namespace

TEST_CASE("shipCalendar: each weekday's plan reads its own row") {
    const vector<CTLRecord> calendar = augustCalendar();
    const PlanningWindow tuesday = planningWindow(calendar, "2026-08-04");
    CHECK(tuesday.firstShipDate == "2026-08-06");
    CHECK(tuesday.lastShipDate == "2026-08-07");
    CHECK_FALSE(tuesday.fromDatedRow);
    CHECK(calendar[tuesday.ctlRowIndex].zday == "TUESDAY");

    const PlanningWindow wednesday = planningWindow(calendar, "2026-08-05");
    CHECK(wednesday.firstShipDate == "2026-08-07");
    CHECK(wednesday.lastShipDate == "2026-08-08");

    const PlanningWindow thursday = planningWindow(calendar, "2026-08-06");
    CHECK(thursday.firstShipDate == "2026-08-08");
    CHECK(thursday.lastShipDate == "2026-08-10");

    vector<CTLRecord> changed = calendar;
    changed[2].level_load_end = 5;
    CHECK(planningWindow(changed, "2026-08-04").lastShipDate == "2026-08-09");
}

TEST_CASE("shipCalendar: a dated row matching the plan date overrides the weekday row") {
    vector<CTLRecord> calendar = augustCalendar();
    calendar.push_back({"2026-08-04", "", 1, 1, 1});
    const PlanningWindow window = planningWindow(calendar, "2026-08-04");
    CHECK(window.fromDatedRow);
    CHECK(window.firstShipDate == "2026-08-05");
    CHECK(window.lastShipDate == "2026-08-05");
}

TEST_CASE("shipCalendar: dated rows for other dates are ignored") {
    // 30 Jun 2026 is a Tuesday too, and its stale row says +2..+4.
    const PlanningWindow window = planningWindow(augustCalendar(), "2026-08-04");
    CHECK_FALSE(window.fromDatedRow);
    CHECK(window.lastShipDate == "2026-08-07");
}

TEST_CASE("shipCalendar: no row for the plan's weekday fails fast") {
    // 2026-08-08 is a Saturday, which the August table does not carry.
    CHECK_THROWS_AS(planningWindow(augustCalendar(), "2026-08-08"), invalid_argument);
    CHECK_THROWS_AS(planningWindow({}, kTuesday), invalid_argument);
}

TEST_CASE("shipCalendar: a malformed CTL table is rejected, never guessed around") {
    const vector<CTLRecord> base = augustCalendar();
    auto rejected = [&](void (*corrupt)(vector<CTLRecord>&)) {
        vector<CTLRecord> calendar = base;
        corrupt(calendar);
        return planningWindow(calendar, kTuesday);
    };
    CHECK_THROWS_AS(rejected([](vector<CTLRecord>& calendar) { calendar[2].level_load_start = kUnreadableLevelLoadDay; }),
                    invalid_argument);
    CHECK_THROWS_AS(rejected([](vector<CTLRecord>& calendar) { calendar[2].level_load_end = kUnreadableLevelLoadDay; }),
                    invalid_argument);
    CHECK_THROWS_AS(rejected([](vector<CTLRecord>& calendar) { calendar[2].level_load_end = 1; }),
                    invalid_argument);
    CHECK_THROWS_AS(rejected([](vector<CTLRecord>& calendar) { calendar.push_back({"", "TUESDAY", 2, 4, 2}); }),
                    invalid_argument);
    CHECK_THROWS_AS(rejected([](vector<CTLRecord>& calendar) { calendar.push_back({"2026-02-30", "", 2, 3, 2}); }),
                    invalid_argument);
    CHECK_THROWS_AS(rejected([](vector<CTLRecord>& calendar) { calendar.push_back({"", "TUESDAYS", 2, 3, 2}); }),
                    invalid_argument);
    CHECK_THROWS_AS(rejected([](vector<CTLRecord>& calendar) { calendar.push_back({"", "", 2, 3, 2}); }),
                    invalid_argument);
}

TEST_CASE("shipCalendar: an identical repeated weekday row is accepted") {
    vector<CTLRecord> calendar = augustCalendar();
    calendar.push_back({"", "tuesday", 2, 3, 9});
    CHECK(planningWindow(calendar, kTuesday).lastShipDate == "2026-08-07");
}

TEST_CASE("shipCalendar: an invalid plan date is rejected") {
    CHECK_THROWS_AS(planningWindow(augustCalendar(), ""), invalid_argument);
    CHECK_THROWS_AS(planningWindow(augustCalendar(), "2026-02-30"), invalid_argument);
}

TEST_CASE("shipCalendar: the 3 Sep extract's Wednesday plan takes its dated row" * doctest::skip(!allExtractsPresent())) {
    const PipelineResult& run = pipelineRuns()[3];
    REQUIRE(run.demand.request_id == "#STR_PA4400_20260902110529#");
    const PlanningWindow window = planningWindow(run.demand.ctl, "2026-09-02");
    CHECK(window.fromDatedRow);
    CHECK(run.demand.ctl[window.ctlRowIndex].zdate == "2026-09-02");
    CHECK(window.firstShipDate == "2026-09-04");
    CHECK(window.lastShipDate == "2026-09-07");
}
