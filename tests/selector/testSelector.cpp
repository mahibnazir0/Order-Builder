#include "doctest.h"
#include "../m4AugustFixtures.hpp"
#include "../m4SyntheticDay.hpp"
#include "benchmarkHarness.hpp"
#include "selector.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;

namespace {

using namespace m4SyntheticDay;

// I10, I2, I3, I4: every item on one truck or left over with a reason, no truck over the
// floor, payload or its rows, no truck carrying two groups, and no lane-date given more trucks
// than it reserved.
void checkPlan(const Day& day, const SelectionPlan& selection) {
    vector<int> seen(day.items.items.size(), 0);
    for (const PlannedTruck& truck : selection.trucks) {
        REQUIRE_FALSE(truck.items.empty());
        REQUIRE(truck.areaSqIn <= kTrailer.interiorLengthIn * kTrailer.interiorWidthIn + kQuantityEpsilon);
        REQUIRE(truck.weightLb <= kTrailer.weightLimitLb + kQuantityEpsilon);
        REQUIRE(truck.floor.fitsLength);
        REQUIRE(truck.floor.unplaceableItems.empty());
        for (const size_t index : truck.items) {
            ++seen[index];
            REQUIRE(day.items.items[index].groupIndex == truck.groupIndex);
        }
    }
    for (const LeftOverItem& left : selection.leftOver) ++seen[left.itemIndex];
    REQUIRE(all_of(seen.begin(), seen.end(), [](int count) { return count == 1; }));
    for (const ReservationUse& use : selection.reservations) REQUIRE(use.built <= use.reserved);
}

size_t countReason(const SelectionPlan& selection, LeftOverReason reason) {
    return static_cast<size_t>(count_if(selection.leftOver.begin(), selection.leftOver.end(),
                                        [&](const LeftOverItem& left) { return left.reason == reason; }));
}

} // anonymous namespace

TEST_CASE("selector: a lane is solved by its method and every item accounted for") {
    const Day day = dayOf({group("2027")}, 1, repeated(40, 0, 0), {reserved("2027", 2)});
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    REQUIRE(selection.routes.size() == 1);
    CHECK(selection.routes[0].status == RouteStatus::Solved);
    CHECK(selection.routes[0].methodUsed == PackingMethod::PatternPacker);
    CHECK(selection.routes[0].trucksBuilt == 2);
    CHECK(selection.trucks.size() == 2);
    CHECK(selection.penaltyExposureTrucks == 0);
}

TEST_CASE("selector: m5CallsNoSolver - an escalated lane is marked, its goods and trucks left to M5") {
    const Day day = dayOf({group("2027")}, 1, repeated(40, 0, 0), {reserved("2027", 21)});
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    CHECK(selection.routes[0].status == RouteStatus::Escalated);
    CHECK(selection.routes[0].attempts.empty());
    CHECK(selection.trucks.empty());
    CHECK(countReason(selection, LeftOverReason::EscalatedToM5) == 40);
    CHECK(selection.reservations[0].escalated);
    CHECK(selection.reservations[0].unused() == 21);
    CHECK(selection.penaltyExposureTrucks == 21);
}

TEST_CASE("selector: fallbackNamed - a method that throws is named and the next one down runs") {
    const Day day = dayOf({group("2027")}, 1, repeated(40, 0, 0), {reserved("2027", 2)});
    SelectorSettings settings = selectorSettings();
    settings.patternPacker.weightGrainLb = 0.0;
    const SelectionPlan selection = plan(day, settings);
    checkPlan(day, selection);
    const RouteOutcome& route = selection.routes[0];
    REQUIRE(route.attempts.size() == 2);
    CHECK(route.attempts[0].method == PackingMethod::PatternPacker);
    CHECK(route.attempts[0].outcome == AttemptOutcome::Threw);
    CHECK_FALSE(route.attempts[0].error.empty());
    CHECK(route.methodUsed == PackingMethod::BinPacker);
    CHECK(route.trucksBuilt == 2);
}

TEST_CASE("selector: oneFailingLaneDoesNotAbort - every method throwing leaves that lane unsolved") {
    // One truck reserved sends 2027 to knapsack, whose settings are broken; 2028 packs.
    const vector<SelectionItem> items = [] {
        vector<SelectionItem> both = repeated(10, 0, 0);
        const vector<SelectionItem> other = repeated(40, 1, 1);
        both.insert(both.end(), other.begin(), other.end());
        return both;
    }();
    const Day day = dayOf({group("2027"), group("2028")}, 2, items, {reserved("2027", 1), reserved("2028", 2)});
    SelectorSettings settings = selectorSettings();
    settings.knapsack.areaGrainSqIn = -1.0;
    const SelectionPlan selection = plan(day, settings);
    checkPlan(day, selection);
    CHECK(selection.routes[0].status == RouteStatus::Unsolved);
    CHECK(selection.routes[0].attempts.size() == 2);
    CHECK(countReason(selection, LeftOverReason::LaneUnsolved) == 10);
    CHECK(selection.routes[1].status == RouteStatus::Solved);
    CHECK(selection.routes[1].trucksBuilt == 2);
}

TEST_CASE("selector: a slow clock does not change the plan") {
    const Day day = dayOf({group("2027")}, 1, repeated(40, 0, 0), {reserved("2027", 2)});
    const SelectionPlan unhurried = plan(day, selectorSettings());
    SelectorSettings slow = selectorSettings();
    auto now = make_shared<double>(0.0);
    slow.clock = [now] { return *now += 100.0; };
    const SelectionPlan hurried = plan(day, slow);
    checkPlan(day, hurried);
    CHECK(planFingerprint(hurried) == planFingerprint(unhurried));
    CHECK(hurried.routes[0].methodUsed == unhurried.routes[0].methodUsed);
}

TEST_CASE("selector: a method over budget is kept and its route flagged") {
    const Day day = dayOf({group("2027")}, 1, repeated(40, 0, 0), {reserved("2027", 2)});
    SelectorSettings settings = selectorSettings();
    auto now = make_shared<double>(0.0);
    settings.clock = [now] { return *now += 100.0; };
    const SelectionPlan selection = plan(day, settings);
    const RouteOutcome& route = selection.routes[0];
    REQUIRE(route.attempts.size() == 1);
    CHECK(route.attempts[0].outcome == AttemptOutcome::OverBudget);
    CHECK(route.overBudget);
    CHECK(route.methodUsed == route.attempts[0].method);
}

TEST_CASE("selector: a route whose LP stops at its simplex iteration limit is flagged") {
    vector<SelectionItem> items = repeated(30, 0, 0);
    const vector<SelectionItem> smaller(20, item(0, 1, 600.0, 0, 48.0, 30.0));
    items.insert(items.end(), smaller.begin(), smaller.end());
    const Day day = dayOf({group("2027")}, 2, items, {reserved("2027", 3)});
    const vector<PackingMethod> chain{PackingMethod::PatternPacker};
    SelectorSettings capped = selectorSettings();
    capped.patternPacker.simplexIterationLimit = 1;
    const RouteSolution capHit = solveRoute(day.run, day.items, day.classification, 0, chain, 3, capped);
    CHECK(capHit.outcome.lpIterationCapHit);
    const RouteSolution clean = solveRoute(day.run, day.items, day.classification, 0, chain, 3, selectorSettings());
    CHECK_FALSE(clean.outcome.lpIterationCapHit);
}

TEST_CASE("selector: a method within budget leaves its route unflagged") {
    const Day day = dayOf({group("2027")}, 1, repeated(40, 0, 0), {reserved("2027", 2)});
    const SelectionPlan selection = plan(day, selectorSettings());
    const RouteOutcome& route = selection.routes[0];
    CHECK_FALSE(route.overBudget);
    CHECK(route.attempts[0].outcome == AttemptOutcome::Used);
}

TEST_CASE("selector: neverTwoGroupsOnOneTruck - two groups of a split lane that fit one truck get two") {
    vector<SelectionItem> items = repeated(3, 0, 0);
    const vector<SelectionItem> other = repeated(3, 1, 1);
    items.insert(items.end(), other.begin(), other.end());
    const Day day = dayOf({group("2028"), group("2028", SplitReason::DoNotMix, "S01")}, 2, items, {reserved("2028", 2)});
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    REQUIRE(selection.trucks.size() == 2);
    CHECK(selection.trucks[0].groupIndex != selection.trucks[1].groupIndex);
}

TEST_CASE("selector: a split lane never takes more trucks than it reserved; the short group ships short") {
    vector<SelectionItem> items = repeated(3, 0, 0);
    const vector<SelectionItem> other = repeated(3, 1, 1);
    items.insert(items.end(), other.begin(), other.end());
    const Day day = dayOf({group("2028"), group("2028", SplitReason::DoNotMix, "S01")}, 2, items, {reserved("2028", 1)});
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    CHECK(selection.trucks.size() == 1);
    CHECK(selection.routes[1].trucksAllowed == 0);
    CHECK(countReason(selection, LeftOverReason::OverReservation) == 3);
    CHECK(selection.reservations[0].built == 1);
}

TEST_CASE("selector: a split lane's one truck carries the more urgent group, whichever is listed first") {
    vector<SelectionItem> items = repeated(3, 0, 0);
    for (SelectionItem& lessUrgent : items) lessUrgent.priority = 5;
    const vector<SelectionItem> urgent = repeated(3, 1, 1);
    items.insert(items.end(), urgent.begin(), urgent.end());
    const Day day = dayOf({group("2028"), group("2028", SplitReason::DoNotMix, "S01")}, 2, items, {reserved("2028", 1)});
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    REQUIRE(selection.trucks.size() == 1);
    CHECK(selection.trucks[0].groupIndex == 1);
    CHECK(countReason(selection, LeftOverReason::OverReservation) == 3);
}

TEST_CASE("selector: a truck a more urgent group leaves unbuilt passes to the next group") {
    vector<SelectionItem> items = repeated(3, 0, 0);
    vector<SelectionItem> lessUrgent = repeated(3, 1, 1);
    for (SelectionItem& one : lessUrgent) one.priority = 5;
    items.insert(items.end(), lessUrgent.begin(), lessUrgent.end());
    const Day day = dayOf({group("2028"), group("2028", SplitReason::DoNotMix, "S01")}, 2, items, {reserved("2028", 4)});
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    CHECK(selection.routes[0].trucksAllowed == 3);
    CHECK(selection.routes[0].trucksBuilt == 1);
    CHECK(selection.routes[1].trucksAllowed == 3);
    CHECK(selection.trucks.size() == 2);
    CHECK(selection.reservations[0].built == 2);
}

// 32 stacks the classifier estimates as one truck by floor area, though only 30 fit its rows,
// and four heavy stacks it estimates as three trucks, cut in order, though they pack into two.
Day underEstimatedUrgentGroup() {
    vector<SelectionItem> items = repeated(32, 0, 0);
    const vector<double> heavyLb{27000.0, 27000.0, 18000.0, 18000.0};
    for (size_t heavy = 0; heavy < heavyLb.size(); ++heavy) {
        items.push_back(item(1, 1 + heavy, heavyLb[heavy], 5 + static_cast<int>(heavy)));
    }
    return dayOf({group("2028"), group("2028", SplitReason::DoNotMix, "S01")}, 5, items, {reserved("2028", 4)});
}

size_t routeOfGroup(const Day& day, size_t groupIndex) {
    for (size_t routeIndex = 0; routeIndex < day.classification.routes.size(); ++routeIndex) {
        if (day.classification.routes[routeIndex].groupIndex == groupIndex) return routeIndex;
    }
    throw logic_error("no route for group " + to_string(groupIndex));
}

TEST_CASE("selector: a truck a less urgent group leaves unbuilt goes back to the more urgent group") {
    const Day day = underEstimatedUrgentGroup();
    const size_t urgentRoute = routeOfGroup(day, 0);
    REQUIRE(day.classification.routes[urgentRoute].trucksAllocated == 1);
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    CHECK(selection.routes[urgentRoute].trucksBuilt == 2);
    CHECK(selection.routes[routeOfGroup(day, 1)].trucksBuilt == 2);
    CHECK(selection.leftOver.empty());
    CHECK(selection.reservations[0].built == 4);
}

TEST_CASE("selector: goods left over while their lane-date has a reserved truck unbuilt are a shortfall") {
    Day day = underEstimatedUrgentGroup();
    PipelineResult& result = day.run;
    result.ranSelection = true;
    result.reservationBook = day.book;
    result.selectionItems = day.items;
    result.laneClassification = day.classification;
    result.selectionPlan = plan(day);
    const string named = "stacks left over while the lane-date had reserved trucks unbuilt";
    auto namesUnbuilt = [&] {
        const vector<string> shortfalls = selectionShortfalls(result);
        return any_of(shortfalls.begin(), shortfalls.end(),
                      [&](const string& shortfall) { return shortfall.find(named) != string::npos; });
    };
    CHECK_FALSE(namesUnbuilt());

    // The plan as it was before unbuilt trucks went back: the urgent group's second truck unbuilt.
    SelectionPlan& selection = result.selectionPlan;
    const size_t urgentRoute = routeOfGroup(day, 0);
    const auto lastUrgent = find_if(selection.trucks.rbegin(), selection.trucks.rend(),
                                    [&](const PlannedTruck& truck) { return truck.routeIndex == urgentRoute; });
    REQUIRE(lastUrgent != selection.trucks.rend());
    for (const size_t index : lastUrgent->items) selection.leftOver.push_back({index, LeftOverReason::OverReservation});
    selection.trucks.erase(next(lastUrgent).base());
    --selection.routes[urgentRoute].trucksBuilt;
    selection.routes[urgentRoute].trucksAllowed = selection.routes[urgentRoute].trucksBuilt;
    CHECK(namesUnbuilt());
}

TEST_CASE("selector: fourFullBeatsFiveWithPenalty - four trucks are built and the fifth reported") {
    const Day day = dayOf({group("2027")}, 1, repeated(120, 0, 0), {reserved("2027", 5)});
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    CHECK(selection.trucks.size() == 4);
    CHECK(selection.reservations[0].unused() == 1);
    CHECK(selection.penaltyExposureTrucks == 1);
}

TEST_CASE("selector: a reservation with nothing to load is penalty exposure; demand with none is left") {
    const Day day = dayOf({group("2027"), group("2019")}, 2,
                          [] {
                              vector<SelectionItem> items = repeated(4, 1, 1);
                              return items;
                          }(),
                          {reserved("2027", 3)});
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    CHECK(selection.penaltyExposureTrucks == 3);
    CHECK(countReason(selection, LeftOverReason::NoReservation) == 4);
}

TEST_CASE("selector: goods whose truck's rows do not fit are evicted, least urgent first") {
    // 32 unit loads of 48 x 40 fit the floor area of one truck, but only 30 stand in its rows.
    vector<SelectionItem> items;
    for (size_t index = 0; index < 32; ++index) items.push_back(item(0, index, 1000.0, index < 30 ? 0 : 11));
    const Day day = dayOf({group("2027")}, 32, items, {reserved("2027", 1)});
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    REQUIRE(selection.trucks.size() == 1);
    CHECK(selection.trucks[0].items.size() == 30);
    CHECK(countReason(selection, LeftOverReason::FloorEviction) == 2);
    for (const LeftOverItem& left : selection.leftOver) CHECK(day.items.items[left.itemIndex].priority == 11);
}

TEST_CASE("selector: heavySpreadAcrossTrucks - no truck weighs out while another has room") {
    // First-fit decreasing puts the four 11,000 lb loads on one truck and fills it to 45,000.
    vector<SelectionItem> items = distinctProducts(4, 0, 0, 11000.0);
    const vector<SelectionItem> light = distinctProducts(28, 0, 4, 100.0);
    items.insert(items.end(), light.begin(), light.end());
    const Day day = dayOf({group("2027")}, 32, items, {reserved("2027", 2)});
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    REQUIRE(selection.trucks.size() == 2);
    for (const PlannedTruck& truck : selection.trucks) {
        CHECK_FALSE(truck.weightFull);
        CHECK(truck.weightLb <= 24000.0);
    }
}

TEST_CASE("selector: a dense lane-date binds on weight and a light one on floor, before any truck") {
    const Day dense = dayOf({group("2027")}, 1, repeated(32, 0, 0, 1500.0), {reserved("2027", 2)});
    CHECK(plan(dense).routes[0].binding == BindingLimit::Weight);
    const Day light = dayOf({group("2027")}, 1, repeated(32, 0, 0, 500.0), {reserved("2027", 2)});
    CHECK(plan(light).routes[0].binding == BindingLimit::FloorArea);
}

TEST_CASE("selector: every group of a lane-date is planned against the same limit") {
    vector<SelectionItem> items = repeated(20, 0, 0, 2000.0);
    const vector<SelectionItem> light = repeated(20, 1, 1, 100.0);
    items.insert(items.end(), light.begin(), light.end());
    const Day day = dayOf({group("2028"), group("2028", SplitReason::DoNotMix, "S01")}, 2, items, {reserved("2028", 2)});
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    REQUIRE(selection.routes.size() == 2);
    CHECK(selection.routes[0].binding == BindingLimit::FloorArea);
    CHECK(selection.routes[1].binding == BindingLimit::FloorArea);
}

TEST_CASE("selector: a group whose trucks bind on the other limit is flagged, not swapped") {
    vector<SelectionItem> items = repeated(20, 0, 0, 2000.0);
    const vector<SelectionItem> light = repeated(20, 1, 1, 100.0);
    items.insert(items.end(), light.begin(), light.end());
    const Day day = dayOf({group("2028"), group("2028", SplitReason::DoNotMix, "S01")}, 2, items, {reserved("2028", 2)});
    const SelectionPlan selection = plan(day);
    REQUIRE(selection.routes.size() == 2);
    CHECK(selection.routes[0].bindingChanged);
    CHECK_FALSE(selection.routes[1].bindingChanged);
}

TEST_CASE("selector: multiThreadMatchesSingle at 1, 2, 8 and more threads than lanes") {
    mt19937 random(14112026);
    vector<SegregatedGroup> groups;
    vector<PlaceholderRecord> placeholders;
    vector<SelectionItem> items;
    for (size_t lane = 0; lane < 12; ++lane) {
        groups.push_back(group(to_string(3000 + lane)));
        placeholders.push_back(reserved(to_string(3000 + lane), 1 + static_cast<int>(random() % 4)));
        const size_t count = 5 + random() % 60;
        for (size_t index = 0; index < count; ++index) {
            items.push_back(item(lane, random() % 40, 200.0 + static_cast<double>(random() % 3000), static_cast<int>(random() % 12)));
        }
    }
    const Day day = dayOf(groups, 40, items, placeholders);
    SelectorSettings settings = selectorSettings();
    const SelectionPlan single = plan(day, settings);
    checkPlan(day, single);
    for (const int threads : {2, 8, 64}) {
        settings.threads = threads;
        const SelectionPlan parallel = plan(day, settings);
        REQUIRE(parallel.trucks.size() == single.trucks.size());
        for (size_t truck = 0; truck < single.trucks.size(); ++truck) {
            REQUIRE(parallel.trucks[truck].items == single.trucks[truck].items);
        }
        REQUIRE(parallel.leftOver.size() == single.leftOver.size());
        REQUIRE(parallel.penaltyExposureTrucks == single.penaltyExposureTrucks);
    }
}

TEST_CASE("selector: bad settings are rejected") {
    const Day day = dayOf({group("2027")}, 1, repeated(4, 0, 0), {reserved("2027", 1)});
    for (const double budget : {0.0, -1.0, numeric_limits<double>::quiet_NaN(), numeric_limits<double>::infinity()}) {
        SelectorSettings settings = selectorSettings();
        settings.secondsPerLane = budget;
        CHECK_THROWS_AS(plan(day, settings), invalid_argument);
    }
    SelectorSettings noThreads = selectorSettings();
    noThreads.threads = 0;
    CHECK_THROWS_AS(plan(day, noThreads), invalid_argument);
    for (const int limit : {0, -1}) {
        SelectorSettings noFill = selectorSettings();
        noFill.fillMissesPerTruck = limit;
        CHECK_THROWS_AS(plan(day, noFill), invalid_argument);
        SelectorSettings noSpread = selectorSettings();
        noSpread.weightSpreadRoundsPerTruck = limit;
        CHECK_THROWS_AS(plan(day, noSpread), invalid_argument);
    }
}

TEST_CASE("selector: an August day keeps every invariant, at one thread and four"
          * doctest::skip(!m4AugustTests::augustPresent())) {
    for (size_t runIndex = 0; runIndex < m4AugustTests::kAugustRunFiles.size(); ++runIndex) {
        CAPTURE(runIndex);
        const m4AugustTests::AugustItems& august = m4AugustTests::augustItems(runIndex);
        const LaneClassification classification = classifyLanes(august.run, august.book, august.built, kRouting);
        SelectorSettings settings = selectorSettings();
        settings.knapsack = {0.001, 0.001, 200'000};
        settings.patternPacker = {0.001, 0.001, 200, 200'000, 100'000};
        settings.binPacker = {0.001, 0.001, 50, 100'000, 2'000, 100, 100'000};
        settings.secondsPerLane = 60.0;
        const SelectionPlan single = selectLoads(august.run, august.book, august.built, classification, settings);
        vector<int> seen(august.built.items.size(), 0);
        for (const PlannedTruck& truck : single.trucks) {
            REQUIRE(truck.floor.fitsLength);
            REQUIRE(truck.weightLb <= august.run.trailer.weightLimitLb + kQuantityEpsilon);
            for (const size_t index : truck.items) ++seen[index];
        }
        for (const LeftOverItem& left : single.leftOver) ++seen[left.itemIndex];
        REQUIRE(all_of(seen.begin(), seen.end(), [](int count) { return count == 1; }));
        for (const ReservationUse& use : single.reservations) REQUIRE(use.built <= use.reserved);

        settings.threads = 4;
        const SelectionPlan parallel = selectLoads(august.run, august.book, august.built, classification, settings);
        REQUIRE(parallel.trucks.size() == single.trucks.size());
        for (size_t truck = 0; truck < single.trucks.size(); ++truck) {
            REQUIRE(parallel.trucks[truck].items == single.trucks[truck].items);
        }
    }
}

// Three short unit loads to a stack: 30 positions of them is 90 unit loads, over the cap of 64.
vector<SelectionItem> tripleStacks(size_t count) {
    vector<SelectionItem> items = repeated(count, 0, 0, 900.0);
    for (SelectionItem& item : items) {
        item.unitLoads = 3.0;
        item.lineIndices = {0, 0, 0};
    }
    return items;
}

TEST_CASE("selector: no truck is planned over maxUnitLoadsPerTruck, however the stacks fit the floor") {
    const Day day = dayOf({group("2027")}, 1, tripleStacks(30), {reserved("2027", 2)});
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    for (const PlannedTruck& truck : selection.trucks) CHECK(truck.unitLoads <= 64.0 + kQuantityEpsilon);
    CHECK(selection.leftOver.empty());
    CHECK(selection.trucks.size() == 2);
    CHECK(checkInvariants(day.run, day.book, day.items, selection, 64.0).empty());
}

TEST_CASE("selector: goods over the unit-load cap with no truck left are left over, named") {
    const Day day = dayOf({group("2027")}, 1, tripleStacks(30), {reserved("2027", 1)});
    const SelectionPlan selection = plan(day);
    checkPlan(day, selection);
    REQUIRE(selection.trucks.size() == 1);
    CHECK(selection.trucks.front().unitLoads == doctest::Approx(63.0));
    CHECK(countReason(selection, LeftOverReason::UnitLoadCap) == 9);
}

TEST_CASE("selector: a unit-load cap that is not positive and finite is rejected") {
    const Day day = dayOf({group("2027")}, 1, tripleStacks(2), {reserved("2027", 1)});
    for (const double cap : {0.0, -64.0, numeric_limits<double>::quiet_NaN(), numeric_limits<double>::infinity()}) {
        CAPTURE(cap);
        SelectorSettings settings = selectorSettings();
        settings.maxUnitLoadsPerTruck = cap;
        CHECK_THROWS_AS(plan(day, settings), invalid_argument);
    }
}

