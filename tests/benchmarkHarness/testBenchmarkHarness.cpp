#include "doctest.h"
#include "../m4AugustFixtures.hpp"
#include "../m4SyntheticDay.hpp"
#include "benchmarkHarness.hpp"

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using namespace ob;
using namespace m4SyntheticDay;

namespace {

MethodScore score(PackingMethod method, double unitLoads, long long trucks, double rearWaste = 10.0) {
    MethodScore built;
    built.method = method;
    built.ran = true;
    built.withinBudget = true;
    built.unitLoadsShipped = unitLoads;
    built.trucks = trucks;
    built.averageRearWasteIn = rearWaste;
    return built;
}

// A route benchmark with only patternPacker and binPacker scored, the winner shipping more.
RouteBenchmark separating(double unitLoadsPerProduct, bool patternWins) {
    RouteBenchmark benchmark;
    benchmark.routedTo = SolveMethod::BinPacker;
    benchmark.unitLoadsPerProduct = unitLoadsPerProduct;
    benchmark.scores = {score(PackingMethod::PatternPacker, patternWins ? 32.0 : 30.0, 1),
                        score(PackingMethod::BinPacker, patternWins ? 30.0 : 32.0, 1)};
    return benchmark;
}

bool hasViolation(const vector<InvariantViolation>& violations, const string& invariant) {
    return any_of(violations.begin(), violations.end(),
                  [&](const InvariantViolation& violation) { return violation.invariant == invariant; });
}

} // anonymous namespace

TEST_CASE("benchmarkHarness: fullness beats truck count, and only methods within budget rank") {
    CHECK(scoresBetter(score(PackingMethod::BinPacker, 64.0, 3), score(PackingMethod::PatternPacker, 60.0, 2)));
    CHECK(scoresBetter(score(PackingMethod::BinPacker, 60.0, 2), score(PackingMethod::PatternPacker, 60.0, 3)));
    CHECK(scoresBetter(score(PackingMethod::BinPacker, 60.0, 2, 5.0), score(PackingMethod::PatternPacker, 60.0, 2, 9.0)));
    MethodScore slow = score(PackingMethod::PatternPacker, 99.0, 1);
    slow.withinBudget = false;
    CHECK(scoresBetter(score(PackingMethod::FirstFitDecreasing, 10.0, 5), slow));
    MethodScore threw = score(PackingMethod::Knapsack, 99.0, 1);
    threw.ran = false;
    CHECK_FALSE(scoresBetter(threw, slow));
}

TEST_CASE("benchmarkHarness: every method is run on a route and the best is named") {
    const Day day = dayOf({group("2027")}, 1, repeated(40, 0, 0), {reserved("2027", 2)});
    const RouteBenchmark benchmark = benchmarkRoute(day.run, day.items, day.classification, 0, selectorSettings());
    REQUIRE(benchmark.scores.size() == benchmarkedMethods().size());
    for (size_t index = 0; index < benchmark.scores.size(); ++index) {
        const MethodScore& methodScore = benchmark.scores[index];
        CHECK(methodScore.method == benchmarkedMethods()[index]);
        CHECK(methodScore.ran);
        CHECK(methodScore.trucks <= 2);
        CHECK(methodScore.unitLoadsShipped == doctest::Approx(40.0));
    }
    REQUIRE(benchmark.best);
    CHECK(benchmark.routedTo == SolveMethod::PatternPacker);
    CHECK(benchmark.products == 1);
    CHECK(benchmark.unitLoadsPerProduct == doctest::Approx(40.0));
}

TEST_CASE("benchmarkHarness: a method that throws is recorded and never ranked") {
    const Day day = dayOf({group("2027")}, 1, repeated(40, 0, 0), {reserved("2027", 2)});
    SelectorSettings settings = selectorSettings();
    settings.binPacker.weightGrainLb = 0.0;
    const RouteBenchmark benchmark = benchmarkRoute(day.run, day.items, day.classification, 0, settings);
    const MethodScore& bin = benchmark.scores[2];
    CHECK_FALSE(bin.ran);
    CHECK_FALSE(bin.error.empty());
    REQUIRE(benchmark.best);
    CHECK(*benchmark.best != PackingMethod::BinPacker);
}

TEST_CASE("benchmarkHarness: a method over budget is recorded and never ranked") {
    const Day day = dayOf({group("2027")}, 1, repeated(40, 0, 0), {reserved("2027", 2)});
    SelectorSettings settings = selectorSettings();
    auto now = make_shared<double>(0.0);
    settings.clock = [now] { return *now += 100.0; };
    const RouteBenchmark benchmark = benchmarkRoute(day.run, day.items, day.classification, 0, settings);
    for (const MethodScore& methodScore : benchmark.scores) CHECK_FALSE(methodScore.withinBudget);
    CHECK_FALSE(benchmark.best);
}

TEST_CASE("benchmarkHarness: the crossover is the ratio that misroutes fewest lanes") {
    const vector<RouteBenchmark> clean{separating(2.0, false), separating(3.0, false), separating(8.0, true),
                                       separating(10.0, true)};
    const optional<VarietyCrossover> split = varietyCrossover(clean);
    REQUIRE(split);
    CHECK(split->ratio == doctest::Approx(3.0));
    CHECK(split->lanesCompared == 4);
    CHECK(split->lanesMisrouted == 0);

    vector<RouteBenchmark> noisy = clean;
    noisy.push_back(separating(9.0, false));
    const optional<VarietyCrossover> noisySplit = varietyCrossover(noisy);
    REQUIRE(noisySplit);
    CHECK(noisySplit->lanesMisrouted == 1);
    CHECK_FALSE(varietyCrossover({}));
}

TEST_CASE("benchmarkHarness: the smallest reservation over budget sets the M5 threshold") {
    vector<RouteBenchmark> benchmarks{separating(5.0, true), separating(5.0, true), separating(5.0, true)};
    benchmarks[0].reserved = 12;
    benchmarks[1].reserved = 30;
    benchmarks[1].scores[1].withinBudget = false;
    benchmarks[2].reserved = 25;
    benchmarks[2].scores[1].withinBudget = false;
    CHECK(smallestReservationOverBudget(benchmarks) == 25);
    benchmarks[1].scores[1].withinBudget = true;
    benchmarks[2].scores[1].withinBudget = true;
    CHECK_FALSE(smallestReservationOverBudget(benchmarks));
}

TEST_CASE("benchmarkHarness: a clean plan breaks no invariant") {
    vector<SelectionItem> items = repeated(40, 0, 0);
    const vector<SelectionItem> other = repeated(6, 1, 1);
    items.insert(items.end(), other.begin(), other.end());
    const Day day = dayOf({group("2028"), group("2028", SplitReason::DoNotMix, "S01")}, 2, items, {reserved("2028", 3)});
    const SelectionPlan selection = plan(day);
    CHECK(checkInvariants(day.run, day.book, day.items, selection, 64.0).empty());
}

TEST_CASE("benchmarkHarness: each invariant is caught when broken") {
    const Day day = dayOf({group("2028"), group("2028", SplitReason::DoNotMix, "S01")}, 2,
                          [] {
                              vector<SelectionItem> items = repeated(40, 0, 0);
                              const vector<SelectionItem> other = repeated(6, 1, 1);
                              items.insert(items.end(), other.begin(), other.end());
                              return items;
                          }(),
                          {reserved("2028", 3)});
    const SelectionPlan clean = plan(day);
    REQUIRE(clean.trucks.size() >= 2);

    SelectionPlan twice = clean;
    twice.trucks[0].items.push_back(twice.trucks[1].items.front());
    CHECK(hasViolation(checkInvariants(day.run, day.book, day.items, twice, 64.0), "I1"));

    SelectionItems heavy = day.items;
    for (SelectionItem& item : heavy.items) item.weightLb = 5000.0;
    CHECK(hasViolation(checkInvariants(day.run, day.book, heavy, clean, 64.0), "I2"));

    SelectionPlan mixed = clean;
    const auto otherGroup = find_if(mixed.trucks.begin(), mixed.trucks.end(),
                                    [&](const PlannedTruck& truck) { return truck.groupIndex != mixed.trucks[0].groupIndex; });
    REQUIRE(otherGroup != mixed.trucks.end());
    mixed.trucks[0].items.push_back(otherGroup->items.back());
    otherGroup->items.pop_back();
    CHECK(hasViolation(checkInvariants(day.run, day.book, day.items, mixed, 64.0), "I3"));

    SelectionPlan extra = clean;
    extra.trucks.push_back(extra.trucks[0]);
    extra.trucks.push_back(extra.trucks[0]);
    CHECK(hasViolation(checkInvariants(day.run, day.book, day.items, extra, 64.0), "I4"));

    CHECK(hasViolation(checkInvariants(day.run, day.book, day.items, clean, 10.0), "I6"));

    SelectionPlan misreported = clean;
    misreported.reservations[0].built += 1;
    CHECK(hasViolation(checkInvariants(day.run, day.book, day.items, misreported, 64.0), "I12"));
}

TEST_CASE("benchmarkHarness: samePlan tells a re-run from a different plan") {
    const Day day = dayOf({group("2027")}, 1, repeated(40, 0, 0), {reserved("2027", 2)});
    const SelectionPlan first = plan(day);
    CHECK(samePlan(first, plan(day)));
    SelectionPlan changed = first;
    swap(changed.trucks[0].items.back(), changed.trucks[1].items.back());
    CHECK_FALSE(samePlan(first, changed));
}

TEST_CASE("benchmarkHarness: the benchmark prints one plain-ASCII row per route") {
    const Day day = dayOf({group("2027"), group("2031")}, 1, [] {
        vector<SelectionItem> items = repeated(40, 0, 0);
        const vector<SelectionItem> other = repeated(10, 1, 0);
        items.insert(items.end(), other.begin(), other.end());
        return items;
    }(), {reserved("2027", 2), reserved("2031", 1)});
    vector<RouteBenchmark> benchmarks;
    for (size_t route = 0; route < day.classification.routes.size(); ++route) {
        benchmarks.push_back(benchmarkRoute(day.run, day.items, day.classification, route, selectorSettings()));
    }
    ostringstream out;
    printBenchmark(out, day.run, day.classification, benchmarks);
    const string text = out.str();
    CHECK(text.find("patternPacker") != string::npos);
    CHECK(text.find("firstFitDecreasing") != string::npos);
    CHECK(count(text.begin(), text.end(), '\n') == 5 + static_cast<long>(benchmarks.size()));
    CHECK(all_of(text.begin(), text.end(), [](char character) {
        return character == '\n' || (static_cast<unsigned char>(character) >= 0x20 && static_cast<unsigned char>(character) <= 0x7e);
    }));
}
