#include "doctest.h"
#include "../m4SyntheticDay.hpp"
#include "benchmarkHarness.hpp"
#include "shipDatePlanner.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;
using namespace m4SyntheticDay;

namespace {

// The synthetic window is 6-7 Aug. A truck takes 30 of these 50 x 42 items on its floor area and in its
// rows alike: 15 rows of two, 100 in across.
const string kAug6 = "2026-08-06";
const string kAug7 = "2026-08-07";
constexpr ShipDateRules kBothRules{true, true};
constexpr ShipDateRules kCarryOnly{true, false};
constexpr ShipDateRules kPullOnly{false, true};
constexpr long long kMaxDays = 10;

SelectionItem dueOn(const string& shipDate, int priority = 0, size_t groupIndex = 0) {
    SelectionItem built = item(groupIndex, groupIndex, 1000.0, priority, 50.0, 42.0);
    built.shipDate = shipDate;
    return built;
}

// Due after the window, as buildSelectionItems marks such a good.
SelectionItem dueAfterWindow(const string& shipDate, int priority = 0, size_t groupIndex = 0) {
    SelectionItem built = dueOn(shipDate, priority, groupIndex);
    built.timing = ShipTiming::Future;
    return built;
}

vector<SelectionItem> copies(size_t count, const SelectionItem& one) { return vector<SelectionItem>(count, one); }

vector<SelectionItem> joined(vector<SelectionItem> first, const vector<SelectionItem>& second) {
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

Day laneDay(const vector<SelectionItem>& items, const vector<PlaceholderRecord>& reservations) {
    return dayOf({group("2027")}, 1, items, reservations);
}

ShipDatePlan planned(const Day& day, const ShipDateRules& rules, long long maxDays = kMaxDays) {
    const ShipDatePlan result = planShipDates(day.run, day.book, day.items, kRouting, selectorSettings(), rules, maxDays);
    for (const InvariantViolation& violation : checkInvariants(day.run, day.book, day.items, result.plan, 64.0)) {
        FAIL_CHECK(violation.invariant << ": " << violation.detail);
    }
    REQUIRE(result.plan.routes.size() == result.classification.routes.size());
    return result;
}

// The goods on trucks of `shipDate`, by the ship date each is due.
vector<string> dueDatesOnTrucks(const Day& day, const SelectionPlan& plan, const string& shipDate) {
    vector<string> dates;
    for (const PlannedTruck& truck : plan.trucks) {
        if (truck.shipDate != shipDate) continue;
        for (const size_t index : truck.items) dates.push_back(day.items.items[index].shipDate);
    }
    sort(dates.begin(), dates.end());
    return dates;
}

size_t countDue(const vector<string>& dates, const string& shipDate) {
    return static_cast<size_t>(count(dates.begin(), dates.end(), shipDate));
}

size_t countLeft(const SelectionPlan& plan, LeftOverReason reason) {
    return static_cast<size_t>(count_if(plan.leftOver.begin(), plan.leftOver.end(),
                                        [&](const LeftOverItem& left) { return left.reason == reason; }));
}

} // anonymous namespace

TEST_CASE("shipDatePlanner: with both rules off the plan is classifyLanes then selectLoads") {
    const Day day = laneDay(joined(copies(40, dueOn(kAug6)), copies(10, dueOn(kAug7))),
                            {reserved("2027", 1, kAug6), reserved("2027", 1, kAug7)});
    const ShipDatePlan result = planned(day, {false, false});
    CHECK(planFingerprint(result.plan) == planFingerprint(plan(day)));
    CHECK(result.classification.routes.size() == day.classification.routes.size());
    CHECK(result.plan.moved.empty());
}

TEST_CASE("shipDatePlanner: goods over a date's reservation ride the lane's next reserved date, ahead of its own") {
    const Day day = laneDay(joined(copies(40, dueOn(kAug6, 9)), copies(25, dueOn(kAug7, 0))),
                            {reserved("2027", 1, kAug6), reserved("2027", 1, kAug7)});
    const ShipDatePlan result = planned(day, kCarryOnly);
    CHECK(countDue(dueDatesOnTrucks(day, result.plan, kAug6), kAug6) == 30);
    const vector<string> onAug7 = dueDatesOnTrucks(day, result.plan, kAug7);
    CHECK(countDue(onAug7, kAug6) == 10);
    CHECK(countDue(onAug7, kAug7) == 20);
    CHECK(countLeft(result.plan, LeftOverReason::OverReservation) == 5);
    REQUIRE(result.plan.moved.size() == 10);
    for (const MovedItem& move : result.plan.moved) {
        CHECK_FALSE(move.pulledForward);
        CHECK(move.reason == LeftOverReason::OverReservation);
        CHECK(result.classification.routes[move.routeIndex].shipDate == kAug6);
    }
}

TEST_CASE("shipDatePlanner: without carryUnshippedForward goods over the reservation stay left over") {
    const Day day = laneDay(joined(copies(40, dueOn(kAug6, 9)), copies(25, dueOn(kAug7, 0))),
                            {reserved("2027", 1, kAug6), reserved("2027", 1, kAug7)});
    const ShipDatePlan result = planned(day, {false, false});
    CHECK(countDue(dueDatesOnTrucks(day, result.plan, kAug7), kAug6) == 0);
    CHECK(countLeft(result.plan, LeftOverReason::OverReservation) == 10);
}

TEST_CASE("shipDatePlanner: a good due on a day with no truck reserved rides the lane's next reserved date") {
    const Day day = laneDay(copies(12, dueOn(kAug6)), {reserved("2027", 1, kAug7)});
    const ShipDatePlan result = planned(day, kCarryOnly);
    CHECK(countDue(dueDatesOnTrucks(day, result.plan, kAug7), kAug6) == 12);
    CHECK(result.plan.leftOver.empty());

    CHECK(countLeft(planned(day, {false, false}).plan, LeftOverReason::NoReservation) == 12);
}

TEST_CASE("shipDatePlanner: a good with no reserved date left on or after its own stays left over, no reservation") {
    const Day day = laneDay(joined(copies(5, dueOn(kAug6)), copies(4, dueOn(kAug7))), {reserved("2027", 1, kAug6)});
    const ShipDatePlan result = planned(day, kCarryOnly);
    CHECK(countDue(dueDatesOnTrucks(day, result.plan, kAug6), kAug6) == 5);
    CHECK(countLeft(result.plan, LeftOverReason::NoReservation) == 4);
}

TEST_CASE("shipDatePlanner: a good no truck can take is never carried") {
    SelectionItem tooTall = dueOn(kAug6);
    tooTall.heightIn = 120.0;
    const Day day = laneDay({tooTall}, {reserved("2027", 1, kAug6), reserved("2027", 1, kAug7)});
    const ShipDatePlan result = planned(day, kCarryOnly);
    CHECK(result.plan.moved.empty());
    CHECK(countLeft(result.plan, LeftOverReason::Unfittable) == 1);
}

TEST_CASE("shipDatePlanner: a truck with room takes goods due later, after its own date's goods") {
    // The later goods are the more urgent by priority; their later date still ranks them last.
    const Day day = laneDay(joined(copies(10, dueOn(kAug6, 11)), copies(30, dueAfterWindow("2026-08-10", 0))),
                            {reserved("2027", 1, kAug6)});
    const ShipDatePlan result = planned(day, kPullOnly);
    REQUIRE(result.plan.trucks.size() == 1);
    const vector<string> onAug6 = dueDatesOnTrucks(day, result.plan, kAug6);
    CHECK(countDue(onAug6, kAug6) == 10);
    CHECK(countDue(onAug6, "2026-08-10") == 20);
    CHECK(countLeft(result.plan, LeftOverReason::AfterWindow) == 10);

    CHECK(countLeft(planned(day, {false, false}).plan, LeftOverReason::AfterWindow) == 30);
}

TEST_CASE("shipDatePlanner: goods due exactly maxDaysPulledForward days after the truck are pulled forward") {
    const Day day = laneDay(copies(5, dueAfterWindow("2026-08-10")), {reserved("2027", 1, kAug6)});
    CHECK(countDue(dueDatesOnTrucks(day, planned(day, kPullOnly, 4).plan, kAug6), "2026-08-10") == 5);
}

TEST_CASE("shipDatePlanner: goods due a day past maxDaysPulledForward are not pulled forward") {
    const Day day = laneDay(copies(5, dueAfterWindow("2026-08-11")), {reserved("2027", 1, kAug6)});
    const ShipDatePlan result = planned(day, kPullOnly, 4);
    CHECK(result.plan.trucks.empty());
    CHECK(result.plan.moved.empty());
    CHECK(countLeft(result.plan, LeftOverReason::AfterWindow) == 5);
}

TEST_CASE("shipDatePlanner: a mixed-date stack is pulled forward only if its latest line is within the limit") {
    SelectionItem stack = dueAfterWindow("2026-08-10");
    stack.mixesShipDates = true;
    stack.daysPulledForward = 1;  // its latest line is due 11 Aug, five days after the truck
    const Day day = laneDay({stack}, {reserved("2027", 1, kAug6)});
    CHECK(planned(day, kPullOnly, 4).plan.trucks.empty());
    CHECK(planned(day, kPullOnly, 5).plan.trucks.size() == 1);
}

TEST_CASE("shipDatePlanner: no good is pulled onto a truck before it is available") {
    SelectionItem later = dueAfterWindow("2026-08-10");
    later.availDate = kAug7;
    const Day day = laneDay(copies(5, later), {reserved("2027", 1, kAug6), reserved("2027", 1, kAug7)});
    const ShipDatePlan result = planned(day, kPullOnly);
    CHECK(dueDatesOnTrucks(day, result.plan, kAug6).empty());
    CHECK(countDue(dueDatesOnTrucks(day, result.plan, kAug7), "2026-08-10") == 5);
}

TEST_CASE("shipDatePlanner: a group's part-filled truck is topped up with its own later goods first") {
    // Two segregated groups share the lane's two trucks. The lane's goods due soonest are S03's, but
    // a truck carries one group's goods: S01's truck is filled with S01's own, due later still.
    const Day day = dayOf({group("2027", SplitReason::None, "S01"), group("2027", SplitReason::None, "S03")}, 2,
                          joined(joined(copies(5, dueOn(kAug6, 0, 0)), copies(25, dueOn(kAug6, 0, 1))),
                                 joined(copies(40, dueAfterWindow("2026-08-08", 0, 1)),
                                        copies(30, dueAfterWindow("2026-08-09", 0, 0)))),
                          {reserved("2027", 2, kAug6)});
    const ShipDatePlan result = planned(day, kPullOnly);
    REQUIRE(result.plan.trucks.size() == 2);
    for (const PlannedTruck& truck : result.plan.trucks) {
        CAPTURE(truck.groupIndex);
        CHECK(truck.items.size() == 30);
    }
}

TEST_CASE("shipDatePlanner: a lane-date left to M5 is offered no goods due later") {
    const Day day = laneDay(joined(copies(5, dueOn(kAug6)), copies(5, dueAfterWindow("2026-08-10"))),
                            {reserved("2027", kRouting.m5Threshold + 1, kAug6)});
    const ShipDatePlan result = planned(day, kPullOnly);
    CHECK(result.plan.moved.empty());
    CHECK(countLeft(result.plan, LeftOverReason::EscalatedToM5) == 5);
    CHECK(countLeft(result.plan, LeftOverReason::AfterWindow) == 5);
}

TEST_CASE("shipDatePlanner: goods carried on rank ahead of goods pulled forward onto the same truck") {
    // 6 Aug takes 30 of its 35; the five over go to 7 Aug before anything due after the window.
    const Day day = laneDay(joined(copies(35, dueOn(kAug6, 11)), copies(40, dueAfterWindow("2026-08-09", 0))),
                            {reserved("2027", 1, kAug6), reserved("2027", 1, kAug7)});
    const ShipDatePlan result = planned(day, kBothRules);
    const vector<string> onAug7 = dueDatesOnTrucks(day, result.plan, kAug7);
    CHECK(countDue(onAug7, kAug6) == 5);
    CHECK(countDue(onAug7, "2026-08-09") == 25);
    CHECK(countLeft(result.plan, LeftOverReason::OverReservation) == 0);
}

TEST_CASE("shipDatePlanner: each good ends on the last route it was offered, and a moved good on no other") {
    const Day day = laneDay(joined(copies(35, dueOn(kAug6)), copies(40, dueAfterWindow("2026-08-09"))),
                            {reserved("2027", 1, kAug6), reserved("2027", 1, kAug7)});
    const ShipDatePlan result = planned(day, kBothRules);
    const vector<optional<size_t>> finalRoute =
        finalRouteOfItems(result.classification, result.plan, day.items.items.size());
    REQUIRE_FALSE(result.plan.moved.empty());
    for (const MovedItem& move : result.plan.moved) {
        CHECK(finalRoute[move.itemIndex] != move.routeIndex);
        CHECK(move.pulledForward == (day.items.items[move.itemIndex].shipDate != kAug6));
    }
    for (const PlannedTruck& truck : result.plan.trucks) {
        for (const size_t index : truck.items) CHECK(finalRoute[index] == truck.routeIndex);
    }
    // Offered to both dates and loaded on neither: after the window, and on no route.
    for (const LeftOverItem& left : result.plan.leftOver) {
        if (left.reason == LeftOverReason::AfterWindow) CHECK_FALSE(finalRoute[left.itemIndex]);
    }
}

TEST_CASE("shipDatePlanner: routes come out by group and then date") {
    const Day day = dayOf({group("2027"), group("2029")}, 2,
                          joined(copies(40, dueOn(kAug6)), copies(40, dueOn(kAug6, 0, 1))),
                          {reserved("2027", 1, kAug6), reserved("2027", 1, kAug7), reserved("2029", 1, kAug6),
                           reserved("2029", 1, kAug7)});
    const vector<LaneRoute>& routes = planned(day, kBothRules).classification.routes;
    CHECK(is_sorted(routes.begin(), routes.end(), [](const LaneRoute& left, const LaneRoute& right) {
        return tie(left.groupIndex, left.shipDate) < tie(right.groupIndex, right.shipDate);
    }));
}

TEST_CASE("shipDatePlanner: the plan is the same at one thread and at four") {
    const Day day = dayOf({group("2027"), group("2029")}, 2,
                          joined(joined(copies(45, dueOn(kAug6)), copies(40, dueOn(kAug6, 0, 1))),
                                 copies(30, dueAfterWindow("2026-08-09"))),
                          {reserved("2027", 1, kAug6), reserved("2027", 1, kAug7), reserved("2029", 1, kAug6),
                           reserved("2029", 1, kAug7)});
    SelectorSettings fourThreads = selectorSettings();
    fourThreads.threads = 4;
    const ShipDatePlan once = planned(day, kBothRules);
    const ShipDatePlan atFour = planShipDates(day.run, day.book, day.items, kRouting, fourThreads, kBothRules, kMaxDays);
    CHECK(planFingerprint(once.plan) == planFingerprint(atFour.plan));
}

TEST_CASE("shipDatePlanner: a negative pull-forward limit is rejected") {
    const Day day = laneDay(copies(5, dueOn(kAug6)), {reserved("2027", 1, kAug6)});
    CHECK_THROWS_AS(planShipDates(day.run, day.book, day.items, kRouting, selectorSettings(), kBothRules, -1),
                    invalid_argument);
}

TEST_CASE("shipDatePlanner: sixteen stacks for one truck ship all 34 unit loads, not 32 or 31") {
    const ShipDatePlan result = planned(sixteenStacksWhereAll34Fit(), kBothRules);
    REQUIRE(result.plan.trucks.size() == 1);
    CHECK(result.plan.trucks[0].unitLoads == doctest::Approx(34.0));
    CHECK(result.plan.routes[result.plan.trucks[0].routeIndex].proven);
}

TEST_CASE("shipDatePlanner: eleven stacks the sorted rows refuse all ship on one truck, proven") {
    const ShipDatePlan result = planned(elevenStacksTheSortedRowsRefuse(), kBothRules);
    REQUIRE(result.plan.trucks.size() == 1);
    CHECK(result.plan.trucks[0].unitLoads == doctest::Approx(11.0));
    CHECK(result.plan.trucks[0].floor.usedLengthIn == doctest::Approx(586.0));
    CHECK(result.plan.routes[result.plan.trucks[0].routeIndex].proven);
}

TEST_CASE("shipDatePlanner: fourteen stacks for one truck ship the 23 unit loads that fit, not 22") {
    const ShipDatePlan result = planned(fourteenStacksWhere23Fit(), kBothRules);
    REQUIRE(result.plan.trucks.size() == 1);
    CHECK(result.plan.trucks[0].unitLoads == doctest::Approx(23.0));
    CHECK(result.plan.routes[result.plan.trucks[0].routeIndex].proven);
}

TEST_CASE("shipDatePlanner: one truck for two groups that must not mix carries the eleven that fit, not ten of twelve") {
    const ShipDatePlan result = planned(twelveLargeBesideElevenSmall(), kBothRules);
    REQUIRE(result.plan.trucks.size() == 1);
    CHECK(result.plan.trucks[0].groupIndex == 1);
    CHECK(result.plan.trucks[0].unitLoads == doctest::Approx(11.0));
}
