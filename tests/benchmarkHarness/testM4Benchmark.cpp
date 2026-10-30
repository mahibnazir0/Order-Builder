#include "doctest.h"
#include "../m4AugustFixtures.hpp"
#include "../m4SyntheticDay.hpp"
#include "benchmarkHarness.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace std;
using namespace ob;

namespace {

// The figure the client's own front-end check stops a run at (I6).
constexpr double kMaxUnitLoadsPerTruck = 64.0;

SelectorSettings realDataSettings() {
    SelectorSettings settings = m4SyntheticDay::selectorSettings();
    settings.knapsack = {0.001, 0.001, 200'000};
    settings.patternPacker = {0.001, 0.001, 200, 200'000};
    settings.binPacker = {0.001, 0.001, 50, 100'000, 2'000, 100};
    settings.secondsPerLane = 10.0;
    return settings;
}

// One extract as M4 sees it: its run, reservation book, items, routes and plan.
struct HeldDay {
    string label;
    const PipelineResult* run = nullptr;
    ReservationBook book;
    SelectionItems items;
    LaneClassification classification;
    SelectionPlan plan;
};

HeldDay heldDay(const string& label, const PipelineResult& run) {
    HeldDay day;
    day.label = label;
    day.run = &run;
    day.book = buildReservationBook(run.placeholders.placeholders, run.demand.ctl,
                                    planDateFromRequestId(run.demand.request_id), {"53F", {"53F"}});
    day.items = buildSelectionItems(run, day.book.window);
    day.classification = classifyLanes(run, day.book, day.items, m4SyntheticDay::kRouting);
    day.plan = selectLoads(run, day.book, day.items, day.classification, realDataSettings());
    return day;
}

// Every extract on this machine: the 4-6 August runs, the 17 Aug and early September days, and
// 29 Sep - 5 Oct. Built once.
const vector<HeldDay>& heldDays() {
    static const vector<HeldDay> days = [] {
        vector<HeldDay> held;
        if (m4AugustTests::augustPresent()) {
            const array<string, 3> labels{"Aug4", "Aug5", "Aug6"};
            for (size_t run = 0; run < labels.size(); ++run) {
                held.push_back(heldDay(labels[run], m4AugustTests::augustItems(run).run));
            }
        }
        if (crossDayTests::allExtractsPresent()) {
            for (size_t day = 0; day < crossDayTests::kDayCount; ++day) {
                held.push_back(heldDay(crossDayTests::dayFiles()[day].label, crossDayTests::pipelineRuns()[day]));
            }
        }
        for (size_t day = 0; day < crossDayTests::newExtracts::kDayCount; ++day) {
            if (crossDayTests::newExtracts::dayPresent(day)) {
                held.push_back(heldDay(crossDayTests::newExtracts::labels[day], *crossDayTests::newExtracts::run(day)));
            }
        }
        return held;
    }();
    return days;
}

// The routes sent to patternPacker or binPacker, benchmarked with every method, for one day.
// Those are where the variety ratio decides; a knapsack route's demand runs far past its
// reservation, and packing all of it with every method says nothing about the crossover.
vector<RouteBenchmark> benchmarkDay(const HeldDay& day) {
    vector<RouteBenchmark> benchmarks;
    for (size_t route = 0; route < day.classification.routes.size(); ++route) {
        const LaneRoute& laneRoute = day.classification.routes[route];
        if (laneRoute.method != SolveMethod::PatternPacker && laneRoute.method != SolveMethod::BinPacker) continue;
        benchmarks.push_back(benchmarkRoute(*day.run, day.items, day.classification, route, realDataSettings()));
    }
    return benchmarks;
}

} // anonymous namespace

// Run with: ./build/ob_tests -ts=m4Benchmark --no-skip, or ctest (its m4Benchmark entry). Every
// test here is skipped in a plain ob_tests run: they take minutes over every extract, and a
// clean clone has none (E224). ctest runs them, and there a missing extract fails by name
// instead of the suite passing with nothing checked (E225).
TEST_SUITE("m4Benchmark") {

TEST_CASE("m4Benchmark: every extract the benchmark reads is present" * doctest::skip()) {
    CHECK(m4AugustTests::augustPresent());
    for (const string& missing : crossDayTests::missingExtracts()) FAIL_CHECK(missing);
    for (size_t day = 0; day < crossDayTests::newExtracts::kDayCount; ++day) {
        CHECK_MESSAGE(crossDayTests::newExtracts::dayPresent(day),
                      crossDayTests::newExtracts::directories[day] << " is not complete");
    }
}

TEST_CASE("m4Benchmark: every held extract keeps every invariant" * doctest::skip()) {
    for (const HeldDay& day : heldDays()) {
        CAPTURE(day.label);
        const vector<InvariantViolation> violations =
            checkInvariants(*day.run, day.book, day.items, day.plan, kMaxUnitLoadsPerTruck);
        for (const InvariantViolation& violation : violations) FAIL_CHECK(violation.invariant << ": " << violation.detail);
        MESSAGE(day.label << ": " << day.plan.trucks.size() << " trucks, " << day.book.reservedTrucks
                          << " reserved, " << day.plan.penaltyExposureTrucks << " unused");
    }
}

TEST_CASE("m4Benchmark: every held extract plans the same at one thread, again, and at four"
          * doctest::skip()) {
    SelectorSettings fourThreads = realDataSettings();
    fourThreads.threads = 4;
    for (const HeldDay& day : heldDays()) {
        CAPTURE(day.label);
        CHECK(samePlan(day.plan, selectLoads(*day.run, day.book, day.items, day.classification, realDataSettings())));
        CHECK(samePlan(day.plan, selectLoads(*day.run, day.book, day.items, day.classification, fourThreads)));
    }
}

TEST_CASE("m4Benchmark: the largest August lanes are run with every method" * doctest::skip()) {
    // --no-skip runs this without the files; the first three held days are August only with them.
    REQUIRE(m4AugustTests::augustPresent());
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = heldDays()[run];
        CAPTURE(day.label);
        size_t largest = 0;
        for (size_t route = 0; route < day.classification.routes.size(); ++route) {
            if (day.classification.routes[route].reservedTrucks > day.classification.routes[largest].reservedTrucks) {
                largest = route;
            }
        }
        const RouteBenchmark benchmark = benchmarkRoute(*day.run, day.items, day.classification, largest, realDataSettings());
        REQUIRE(benchmark.scores.size() == benchmarkedMethods().size());
        for (const MethodScore& score : benchmark.scores) {
            CHECK(score.ran);
            CHECK(score.trucks <= benchmark.reserved);
            MESSAGE(day.label << " reserved " << benchmark.reserved << " " << string(packingMethodName(score.method)) << ": "
                              << score.trucks << " trucks, " << score.unitLoadsShipped << " unit loads, "
                              << score.averageCube << " cube, " << score.seconds << " s");
        }
        CHECK(benchmark.best);
    }
}

TEST_CASE("m4Benchmark: every August route benchmarked derives the routing rules" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    vector<RouteBenchmark> all;
    map<string, size_t> bestByRouting;
    for (size_t run = 0; run < 3; ++run) {
        const vector<RouteBenchmark> day = benchmarkDay(heldDays()[run]);
        all.insert(all.end(), day.begin(), day.end());
    }
    for (const RouteBenchmark& benchmark : all) {
        REQUIRE(benchmark.best);
        ++bestByRouting[string(solveMethodName(benchmark.routedTo)) + " -> " + packingMethodName(*benchmark.best)];
    }
    for (const auto& [pair, routes] : bestByRouting) MESSAGE(pair << ": " << routes);
    const optional<VarietyCrossover> crossover = varietyCrossover(all);
    if (crossover) {
        MESSAGE("crossover " << crossover->ratio << " over " << crossover->lanesCompared << " lanes, "
                             << crossover->lanesMisrouted << " misrouted");
    }
    const optional<long long> overBudget = smallestReservationOverBudget(all);
    MESSAGE("smallest reservation over budget: " << (overBudget ? to_string(*overBudget) : string("none")));
}

} // TEST_SUITE
