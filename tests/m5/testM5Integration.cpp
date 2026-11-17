// M5 behind the selector's seam: routing, the shared pool, per-lane order (P9) and can-go offers
// across dates (P10), on days built by hand through the real reservation book and classifier.
#include "doctest.h"
#include "../m4SyntheticDay.hpp"
#include "m5Integration.hpp"
#include "paramsFields.hpp"
#include "selectionParams.hpp"
#include "shipDatePlanner.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Fixtures.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>

using namespace std;
using namespace ob;
using namespace m4SyntheticDay;

namespace {

shared_ptr<const LargeLanePlanner> plannerOf(optional<m5::M5PolicyProfile> profile = m5Fixtures::syntheticProfile(),
                                             const string& error = "", bool pullForward = false) {
    return make_shared<m5::M5LargeLanePlanner>(std::move(profile), error, m5::DateRules{pullForward, 10}, 20);
}

Day dayWithReserved(int reservedTrucks, size_t items, size_t groups = 1) {
    vector<SegregatedGroup> built{group("2027")};
    if (groups > 1) built.push_back(group("2027", SplitReason::DoNotMix, "S01"));
    vector<SelectionItem> goods;
    for (size_t index = 0; index < items; ++index) goods.push_back(item(index % groups, index, 1000.0, 0));
    return dayOf(built, items, goods, {reserved("2027", reservedTrucks)});
}

// Every item on exactly one truck or left over.
bool accountedOnce(const SelectionPlan& selection, size_t items) {
    vector<int> uses(items, 0);
    for (const PlannedTruck& truck : selection.trucks) {
        for (const size_t index : truck.items) ++uses[index];
    }
    for (const LeftOverItem& left : selection.leftOver) ++uses[left.itemIndex];
    return all_of(uses.begin(), uses.end(), [](int count) { return count == 1; });
}

// What a LargeLanePlanner is handed, kept as it arrives.
class RecordingPlanner : public LargeLanePlanner {
public:
    LargeLaneResult plan(const LargeLaneRequest& request, const PipelineResult&, const SelectionItems&,
                         const SelectorSettings&) const override {
        requests.push_back(request);
        LargeLaneResult result;
        result.report.planned = false;
        for (size_t route = 0; route < request.routeIndices.size(); ++route) {
            RouteOutcome outcome;
            outcome.routeIndex = request.routeIndices[route];
            outcome.status = RouteStatus::Escalated;
            result.routes.push_back(outcome);
            for (const size_t index : request.itemIndicesByRoute[route]) {
                result.leftOver.push_back({index, LeftOverReason::M5ConfigurationIncomplete});
            }
        }
        return result;
    }
    mutable vector<LargeLaneRequest> requests;
};

}  // namespace

TEST_CASE("U-RT-01/plannerAbsentFallback: 21 reserved with no planner installed leave every good escalated, no truck, no plan" * doctest::test_suite("m5")) {
    const Day day = dayWithReserved(21, 100);
    REQUIRE(day.classification.routes[0].method == SolveMethod::M5);
    const SelectionPlan selection = plan(day, selectorSettings());
    REQUIRE(selection.routes.size() == 1);
    CHECK(selection.routes[0].status == RouteStatus::Escalated);
    CHECK_FALSE(selection.routes[0].methodUsed.has_value());
    CHECK(selection.routes[0].trucksBuilt == 0);
    CHECK(selection.trucks.empty());
    CHECK(selection.largeLanes.empty());
    REQUIRE(selection.leftOver.size() == 100);
    for (const LeftOverItem& left : selection.leftOver) CHECK(left.reason == LeftOverReason::EscalatedToM5);
    CHECK(accountedOnce(selection, day.items.items.size()));
    REQUIRE(selection.reservations.size() == 1);
    CHECK(selection.reservations[0].escalated);
    CHECK(selection.reservations[0].built == 0);
    m5Evidence::note("expected", "escalated, no truck, 100 goods left over as EscalatedToM5, no large-lane report");
}

TEST_CASE("collectEscalatedLaneDates lists the lane-dates M4 hands to M5 without a planner, as the selector hands them" * doctest::test_suite("m5")) {
    const Day day = dayWithReserved(21, 100, 2);
    const vector<LargeLaneRequest> collected = collectEscalatedLaneDates(day.run, day.classification);
    REQUIRE(collected.size() == 1);

    SelectorSettings settings = selectorSettings();
    const auto recorder = make_shared<RecordingPlanner>();
    settings.largeLanePlanner = recorder;
    plan(day, settings);
    REQUIRE(recorder->requests.size() == 1);
    const LargeLaneRequest& handed = recorder->requests[0];
    const LargeLaneRequest& listed = collected[0];
    CHECK(listed.origin == handed.origin);
    CHECK(listed.destination == handed.destination);
    CHECK(listed.shipCondition == handed.shipCondition);
    CHECK(listed.shipDate == handed.shipDate);
    CHECK(listed.reservedTrucks == 21);
    CHECK(listed.reservedTrucks == handed.reservedTrucks);
    CHECK(listed.routeIndices == handed.routeIndices);
    CHECK(listed.groupIndices == handed.groupIndices);
    CHECK(listed.itemIndicesByRoute == handed.itemIndicesByRoute);
    CHECK(listed.methodBeforeEscalation == handed.methodBeforeEscalation);
    CHECK(listed.routeIndices.size() == 2);

    const Day twenty = dayWithReserved(20, 40);
    CHECK(collectEscalatedLaneDates(twenty.run, twenty.classification).empty());
}

TEST_CASE("U-RT-01 twenty reserved stays in M4, planner or not" * doctest::test_suite("m5")) {
    const Day day = dayWithReserved(20, 40);
    SelectorSettings settings = selectorSettings();
    settings.largeLanePlanner = plannerOf();
    const SelectionPlan selection = plan(day, settings);
    CHECK(selection.largeLanes.empty());
    CHECK(day.classification.routes[0].method != SolveMethod::M5);
}

TEST_CASE("U-RT-02 twenty-one reserved goes to M5, which plans the shared pool" * doctest::test_suite("m5")) {
    const Day day = dayWithReserved(21, 100);
    REQUIRE(day.classification.routes[0].method == SolveMethod::M5);
    SelectorSettings settings = selectorSettings();
    settings.largeLanePlanner = plannerOf();
    const SelectionPlan selection = plan(day, settings);
    REQUIRE(selection.largeLanes.size() == 1);
    CHECK(selection.largeLanes[0].planned);
    CHECK(selection.routes[0].status == RouteStatus::Solved);
    REQUIRE(selection.routes[0].methodUsed.has_value());
    CHECK(selection.trucks.size() == 4);
    CHECK(selection.leftOver.empty());
    CHECK(accountedOnce(selection, day.items.items.size()));
    CHECK_FALSE(selection.reservations[0].escalated);
    CHECK(selection.reservations[0].built == 4);
    CHECK(selection.largeLanes[0].text.find("3B Lane") != string::npos);
}

TEST_CASE("U-RT-02 without a planner the lane-date is escalated, as before M5" * doctest::test_suite("m5")) {
    const Day day = dayWithReserved(21, 100);
    const SelectionPlan selection = plan(day, selectorSettings());
    CHECK(selection.routes[0].status == RouteStatus::Escalated);
    CHECK(selection.trucks.empty());
    CHECK(selection.largeLanes.empty());
    CHECK(selection.reservations[0].escalated);
}

TEST_CASE("X-ROUTE-02 U-SP-02 two groups share one 21-truck pool, never 21 each" * doctest::test_suite("m5")) {
    const Day day = dayWithReserved(21, 2000, 2);
    SelectorSettings settings = selectorSettings();
    settings.largeLanePlanner = plannerOf();
    const SelectionPlan selection = plan(day, settings);
    CHECK(selection.trucks.size() == 21);
    CHECK(accountedOnce(selection, day.items.items.size()));
    for (const PlannedTruck& truck : selection.trucks) {
        for (const size_t index : truck.items) CHECK(day.items.items[index].groupIndex == truck.groupIndex);
    }
}

TEST_CASE("M5 settings that do not read leave every good with the configuration reason" * doctest::test_suite("m5")) {
    const Day day = dayWithReserved(21, 30);
    SelectorSettings settings = selectorSettings();
    settings.largeLanePlanner = plannerOf(nullopt, "params: m5.secondsPerLoad must be a finite number of seconds above zero");
    const SelectionPlan selection = plan(day, settings);
    REQUIRE(selection.largeLanes.size() == 1);
    CHECK_FALSE(selection.largeLanes[0].planned);
    CHECK(selection.routes[0].status == RouteStatus::Escalated);
    CHECK(selection.leftOver.size() == 30);
    for (const LeftOverItem& left : selection.leftOver) CHECK(left.reason == LeftOverReason::M5ConfigurationIncomplete);
    CHECK(selection.reservations[0].escalated);
}

TEST_CASE("R-7 a lane's own past-due order changes only that lane" * doctest::test_suite("m5")) {
    vector<SelectionItem> goods;
    for (size_t lane = 0; lane < 2; ++lane) {
        SelectionItem overdue = item(lane, lane * 2, 1000.0, 11);
        overdue.shipDate = "2026-08-05";
        goods.push_back(overdue);
        goods.push_back(item(lane, lane * 2 + 1, 1000.0, 0));
    }
    Day day = dayOf({group("2027"), group("2028")}, 4, goods, {reserved("2027", 1), reserved("2028", 1)});
    LaneClassifierSettings routing = kRouting;
    const auto firstOf = [&](const LaneClassification& classification, const string& origin) {
        for (const LaneRoute& route : classification.routes) {
            if (day.run.segregation.groups[route.groupIndex].key.locationFrom == origin && !route.itemIndices.empty()) {
                return day.items.items[route.itemIndices.front()].priority;
            }
        }
        return -1;
    };
    const LaneClassification ageFirst = classifyLanes(day.run, day.book, day.items, routing);
    CHECK(firstOf(ageFirst, "2027") == 11);
    CHECK(firstOf(ageFirst, "2028") == 11);
    routing.pastDueOrderByLane[{"2027", "2528", "TL"}] = PastDueOrder::PriorityFirst;
    const LaneClassification overridden = classifyLanes(day.run, day.book, day.items, routing);
    CHECK(firstOf(overridden, "2027") == 0);
    CHECK(firstOf(overridden, "2028") == 11);
    routing.pastDueOrderByLane.clear();
    const LaneClassification removed = classifyLanes(day.run, day.book, day.items, routing);
    CHECK(firstOf(removed, "2027") == 11);
}

TEST_CASE("R-7 P9 the per-lane past-due order is optional, and refused when malformed" * doctest::test_suite("m5")) {
    nlohmann::json root = readParamsFile("config/orderBuilderParams.json");
    CHECK(parseSelectionParams(root).routing.pastDueOrderByLane.empty());
    root["selection"]["pastDueOrderByLane"] = nlohmann::json::array(
        {{{"origin", "2028"}, {"destination", "2574"}, {"shipCondition", "TL"}, {"pastDueOrder", "PriorityFirst"}}});
    const SelectionParams params = parseSelectionParams(root);
    REQUIRE(params.routing.pastDueOrderByLane.size() == 1);
    CHECK(pastDueOrderFor(params.routing, "2028", "2574", "TL") == PastDueOrder::PriorityFirst);
    CHECK(pastDueOrderFor(params.routing, "2028", "2575", "TL") == params.routing.pastDueOrder);
    for (const nlohmann::json& bad :
         {nlohmann::json::object(), nlohmann::json("PriorityFirst"),
          nlohmann::json::array({{{"origin", "2028"}, {"destination", "2574"}, {"shipCondition", "TL"}, {"pastDueOrder", "Newest"}}}),
          nlohmann::json::array({{{"origin", ""}, {"destination", "2574"}, {"shipCondition", "TL"}, {"pastDueOrder", "AgeFirst"}}}),
          nlohmann::json::array({{{"destination", "2574"}, {"shipCondition", "TL"}, {"pastDueOrder", "AgeFirst"}}}),
          nlohmann::json::array({{{"origin", "2028"}, {"destination", "2574"}, {"shipCondition", "TL"}, {"pastDueOrder", "AgeFirst"}},
                                 {{"origin", "2028"}, {"destination", "2574"}, {"shipCondition", "TL"}, {"pastDueOrder", "AgeFirst"}}})}) {
        root["selection"]["pastDueOrderByLane"] = bad;
        INFO(bad.dump());
        CHECK_THROWS_AS(parseSelectionParams(root), runtime_error);
    }
}

TEST_CASE("U-CF-03 can-go goods pulled into an M5 date leave the later date once" * doctest::test_suite("m5")) {
    vector<SelectionItem> goods;
    for (size_t index = 0; index < 10; ++index) goods.push_back(item(0, index));
    for (size_t index = 10; index < 30; ++index) {
        SelectionItem later = item(0, index);
        later.shipDate = "2026-08-07";
        goods.push_back(later);
    }
    const Day day = dayOf({group("2027")}, 30, goods, {reserved("2027", 21, "2026-08-06"), reserved("2027", 20, "2026-08-07")});
    SelectorSettings settings = selectorSettings();

    SUBCASE("pull-forward on: all 30 ship on the M5 date, none on the later one") {
        settings.largeLanePlanner = plannerOf(m5Fixtures::syntheticProfile(), "", true);
        const ShipDatePlan planned = planShipDates(day.run, day.book, day.items, kRouting, settings, {true, true}, 10);
        CHECK(accountedOnce(planned.plan, 30));
        size_t onSixth = 0;
        for (const PlannedTruck& truck : planned.plan.trucks) {
            CHECK(truck.shipDate == "2026-08-06");
            onSixth += truck.items.size();
        }
        CHECK(onSixth == 30);
        REQUIRE(planned.plan.largeLanes.size() == 1);
        CHECK(planned.plan.largeLanes[0].text.find("can-go") != string::npos);
    }
    SUBCASE("pull-forward off: the later goods wait for their own date") {
        settings.largeLanePlanner = plannerOf(m5Fixtures::syntheticProfile(), "", false);
        const ShipDatePlan planned = planShipDates(day.run, day.book, day.items, kRouting, settings, {true, false}, 10);
        CHECK(accountedOnce(planned.plan, 30));
        map<string, size_t> byDate;
        for (const PlannedTruck& truck : planned.plan.trucks) byDate[truck.shipDate] += truck.items.size();
        CHECK(byDate["2026-08-06"] == 10);
        CHECK(byDate["2026-08-07"] == 20);
    }
}

TEST_CASE("X-ROUTE-09 a forced M5 plan leaves no setting behind for a normal run" * doctest::test_suite("m5")) {
    const Day day = dayWithReserved(21, 60);
    SelectorSettings withPlanner = selectorSettings();
    withPlanner.largeLanePlanner = plannerOf();
    const SelectionPlan first = plan(day, withPlanner);
    const SelectionPlan second = plan(day, withPlanner);
    CHECK(first.trucks.size() == second.trucks.size());
    const SelectionPlan without = plan(day, selectorSettings());
    CHECK(without.routes[0].status == RouteStatus::Escalated);
}

namespace {

// R-7: lanes 2027 and 2028 to 2528 TL, each with `slots` reservations on 6 Aug and, per lane, 30 per slot
// FX-30 units (100 x 20 in, 108 in, 1,500 lb: 30 fill a trailer's payload) of P11 due 5 Aug and as many P0
// due 6 Aug, all available 6 Aug. Line numbers are the units' identities.
Day pastDueLanes(int slots) {
    vector<SelectionItem> goods;
    size_t line = 0;
    const size_t perClass = 30 * static_cast<size_t>(slots);
    for (size_t lane = 0; lane < 2; ++lane) {
        for (const int priority : {11, 0}) {
            for (size_t unit = 0; unit < perClass; ++unit) {
                SelectionItem fx30 = item(lane, line++, 1500.0, priority, 100.0, 20.0);
                fx30.heightIn = 108.0;
                fx30.cubeCubicIn = 100.0 * 20.0 * 108.0;
                fx30.availDate = "2026-08-06";
                if (priority == 11) fx30.shipDate = "2026-08-05";
                goods.push_back(fx30);
            }
        }
    }
    return dayOf({group("2027"), group("2028")}, line, goods, {reserved("2027", slots), reserved("2028", slots)});
}

struct LaneShipment {
    size_t overdue = 0;
    size_t current = 0;
    set<size_t> lines;
};

map<string, LaneShipment> shippedByLane(const Day& day, const SelectionPlan& selection) {
    map<string, LaneShipment> lanes;
    for (const PlannedTruck& truck : selection.trucks) {
        for (const size_t index : truck.items) {
            const SelectionItem& shipped = day.items.items[index];
            LaneShipment& lane = lanes[day.run.segregation.groups[shipped.groupIndex].key.locationFrom];
            (shipped.priority == 11 ? lane.overdue : lane.current) += 1;
            lane.lines.insert(shipped.lineIndices.front());
        }
    }
    return lanes;
}

// The A/B/A sequence in one process, each step checked against a fresh day planned once with the same
// settings; carry on, pull-forward off. Lane B (2028) never has an override.
void checkPastDueSequence(int slots, int m5Threshold, bool withPlanner) {
    const size_t perLane = 30 * static_cast<size_t>(slots);
    LaneClassifierSettings routing = kRouting;
    routing.m5Threshold = m5Threshold;
    SelectorSettings settings = selectorSettings();
    if (withPlanner) settings.largeLanePlanner = plannerOf();
    const auto run = [&](const Day& day, const LaneClassifierSettings& lanes) {
        const ShipDatePlan planned = planShipDates(day.run, day.book, day.items, lanes, settings, {true, false}, 10);
        REQUIRE(accountedOnce(planned.plan, day.items.items.size()));
        for (const LaneRoute& route : planned.classification.routes) {
            if (!route.itemIndices.empty()) CHECK((route.method == SolveMethod::M5) == withPlanner);
        }
        return planned.plan;
    };
    LaneClassifierSettings overridden = routing;
    overridden.pastDueOrderByLane[{"2027", "2528", "TL"}] = PastDueOrder::PriorityFirst;

    const Day day = pastDueLanes(slots);
    const map<string, LaneShipment> ageFirst = shippedByLane(day, run(day, routing));
    const map<string, LaneShipment> override = shippedByLane(day, run(day, overridden));
    const map<string, LaneShipment> removed = shippedByLane(day, run(day, routing));
    for (const char* const lane : {"2027", "2028"}) {
        INFO("lane " << lane);
        CHECK(ageFirst.at(lane).overdue == perLane);
        CHECK(ageFirst.at(lane).current == 0);
        CHECK(removed.at(lane).overdue == perLane);
        CHECK(removed.at(lane).current == 0);
        CHECK(removed.at(lane).lines == ageFirst.at(lane).lines);
    }
    CHECK(override.at("2027").current == perLane);
    CHECK(override.at("2027").overdue == 0);
    CHECK(override.at("2028").overdue == perLane);
    CHECK(override.at("2028").lines == ageFirst.at("2028").lines);

    const Day fresh = pastDueLanes(slots);
    for (const char* const lane : {"2027", "2028"}) {
        CHECK(shippedByLane(fresh, run(fresh, overridden)).at(lane).lines == override.at(lane).lines);
        CHECK(shippedByLane(fresh, run(fresh, routing)).at(lane).lines == ageFirst.at(lane).lines);
    }
    m5Evidence::note("expected", {"AgeFirst both overdue", "override A current, B overdue", "removed both overdue"});
}

}  // namespace

TEST_CASE("R-7/m4Path: the per-lane past-due order through M4's selector, A/B/A, lane B never inherits" * doctest::test_suite("m5")) {
    checkPastDueSequence(1, 20, false);
}

// A lane reaches M5 through routing only above the threshold, which is at least 2, so a one-slot lane cannot;
// this path keeps the scarcity with 3 slots per lane and 90 + 90 units. The one-slot form runs at the direct
// seam (R-7 a lane's own past-due order).
TEST_CASE("R-7/m5Path: the per-lane past-due order through routing into M5, A/B/A, lane B never inherits" * doctest::test_suite("m5")) {
    checkPastDueSequence(3, 2, true);
}

namespace {

// Runs `whilePaused` once, inside the first lane-date it is handed, then plans it with the wrapped planner.
class PausingPlanner : public LargeLanePlanner {
public:
    PausingPlanner(shared_ptr<const LargeLanePlanner> inner, function<void()> whilePaused)
        : inner_(std::move(inner)), whilePaused_(std::move(whilePaused)) {}
    LargeLaneResult plan(const LargeLaneRequest& request, const PipelineResult& run, const SelectionItems& items,
                         const SelectorSettings& settings) const override {
        if (whilePaused_) {
            whilePaused_();
            whilePaused_ = nullptr;
        }
        return inner_->plan(request, run, items, settings);
    }

private:
    shared_ptr<const LargeLanePlanner> inner_;
    mutable function<void()> whilePaused_;
};

}  // namespace

TEST_CASE("X-STATE-04/thresholdSnapshotInFlight: a threshold changed while M5 plans neither reroutes it nor leaks into a fresh run" * doctest::test_suite("m5")) {
    const Day inFlightDay = dayWithReserved(21, 40);
    const Day freshDay = dayWithReserved(21, 40);
    LaneClassifierSettings live = kRouting;
    REQUIRE(live.m5Threshold == 20);
    optional<ShipDatePlan> fresh;
    SelectorSettings settings = selectorSettings();
    settings.largeLanePlanner = make_shared<PausingPlanner>(plannerOf(), [&] {
        live.m5Threshold = 50;
        SelectorSettings freshSettings = selectorSettings();
        freshSettings.largeLanePlanner = plannerOf();
        fresh = planShipDates(freshDay.run, freshDay.book, freshDay.items, live, freshSettings, {true, false}, 10);
    });
    const ShipDatePlan inFlight = planShipDates(inFlightDay.run, inFlightDay.book, inFlightDay.items, live, settings, {true, false}, 10);
    REQUIRE(fresh);
    CHECK(live.m5Threshold == 50);
    REQUIRE(inFlight.classification.routes.size() == 1);
    CHECK(inFlight.classification.routes[0].method == SolveMethod::M5);
    REQUIRE(inFlight.plan.largeLanes.size() == 1);
    CHECK(inFlight.plan.largeLanes[0].planned);
    CHECK(inFlight.plan.trucks.size() == 2);
    CHECK(accountedOnce(inFlight.plan, inFlightDay.items.items.size()));
    REQUIRE(fresh->classification.routes.size() == 1);
    CHECK(fresh->classification.routes[0].method != SolveMethod::M5);
    CHECK(fresh->plan.largeLanes.empty());
    CHECK(fresh->plan.trucks.size() == 2);
    CHECK(accountedOnce(fresh->plan, freshDay.items.items.size()));

    // Each run equals a fresh process with its own threshold.
    LaneClassifierSettings twenty = kRouting, fifty = kRouting;
    fifty.m5Threshold = 50;
    SelectorSettings plain = selectorSettings();
    plain.largeLanePlanner = plannerOf();
    const Day again = dayWithReserved(21, 40);
    CHECK(planShipDates(again.run, again.book, again.items, twenty, plain, {true, false}, 10).plan.trucks.size()
          == inFlight.plan.trucks.size());
    CHECK(planShipDates(again.run, again.book, again.items, fifty, plain, {true, false}, 10).classification.routes[0].method
          == fresh->classification.routes[0].method);
    m5Evidence::note("expected", {"in-flight M5 at threshold 20", "fresh M4 at threshold 50"});
}
