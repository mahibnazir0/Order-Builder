#include "doctest.h"
#include "../m4AugustFixtures.hpp"
#include "knapsack.hpp"
#include "laneClassifier.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;

namespace {

// A 630 x 100 in floor and a 45,000 lb payload.
const KnapsackCapacity kTruck{63000.0, 45000.0, 108.0};
const KnapsackSettings kFine{1.0, 1.0, 2'000'000};

KnapsackItem load(double area, double weight, double unitLoads = 1.0,
                  const string& shipDate = "2026-08-06", int priority = 0, double height = 60.0) {
    return {area, weight, height, unitLoads, shipDate, priority};
}

// Chosen and left over together are the input, each index exactly once.
void checkEveryItemOnce(const KnapsackSelection& selection, size_t itemCount) {
    vector<int> seen(itemCount, 0);
    for (const size_t index : selection.chosen) ++seen[index];
    for (const size_t index : selection.leftOver) ++seen[index];
    REQUIRE(all_of(seen.begin(), seen.end(), [](int count) { return count == 1; }));
}

void checkWithinCapacity(const vector<KnapsackItem>& items, const KnapsackSelection& selection,
                         const KnapsackCapacity& capacity) {
    double area = 0.0, weight = 0.0;
    for (const size_t index : selection.chosen) {
        area += items[index].areaSqIn;
        weight += items[index].weightLb;
        REQUIRE(items[index].heightIn <= capacity.ceilingIn + kQuantityEpsilon);
    }
    REQUIRE(area <= capacity.floorAreaSqIn + kQuantityEpsilon);
    REQUIRE(weight <= capacity.weightLb + kQuantityEpsilon);
}

// Every subset of the prepared candidates, judged on their grains: the oracle the solvers are
// checked against.
KnapsackValue bruteForceValue(const KnapsackProblem& problem) {
    const size_t count = problem.candidates.size();
    KnapsackValue best(problem.classCount + 1, 0);
    for (size_t mask = 0; mask < (size_t{1} << count); ++mask) {
        long long area = 0, weight = 0;
        KnapsackValue value(problem.classCount + 1, 0);
        for (size_t position = 0; position < count; ++position) {
            if (!(mask & (size_t{1} << position))) continue;
            area += problem.areaGrains[position];
            weight += problem.weightGrains[position];
            value[problem.classOf[position]] += problem.amount[position];
            value.back() += problem.areaGrains[position];
        }
        if (area <= problem.areaCapacity && weight <= problem.weightCapacity && best < value) best = value;
    }
    return best;
}

vector<KnapsackItem> randomItems(mt19937& random, size_t count) {
    const array<string, 3> dates{"2026-08-05", "2026-08-06", "2026-08-07"};
    uniform_real_distribution<double> area(1200.0, 25000.0);
    uniform_real_distribution<double> weight(500.0, 20000.0);
    uniform_real_distribution<double> unitLoads(0.25, 4.0);
    vector<KnapsackItem> items;
    for (size_t index = 0; index < count; ++index) {
        items.push_back(load(area(random), weight(random), unitLoads(random), dates[random() % 3],
                             static_cast<int>(random() % 3) * 5));
    }
    return items;
}

} // anonymous namespace

TEST_CASE("knapsack: everything fits - all returned, and said to be a fit") {
    const vector<KnapsackItem> items{load(1920, 1000), load(1920, 1000), load(1920, 1000)};
    const KnapsackSelection selection = selectForTruck(items, kTruck, kFine);
    CHECK(selection.chosen.size() == 3);
    CHECK(selection.everythingFit);
    CHECK(selection.binding == BindingLimit::None);
}

TEST_CASE("knapsack: nothing fits - empty, every item named") {
    const vector<KnapsackItem> items{load(64000, 1000), load(1920, 46000), load(1920, 1000, 1.0, "2026-08-06", 0, 109)};
    const KnapsackSelection selection = selectForTruck(items, kTruck, kFine);
    CHECK(selection.chosen.empty());
    CHECK(selection.leftOver == vector<size_t>{0, 1, 2});
    const KnapsackProblem problem = prepareKnapsack(items, kTruck, kFine);
    CHECK(problem.tooLarge == vector<size_t>{0, 1});
    CHECK(problem.tooTall == vector<size_t>{2});
}

TEST_CASE("knapsack: capacity zero chooses nothing") {
    const vector<KnapsackItem> items{load(1920, 1000)};
    CHECK(selectForTruck(items, {0.0, 45000.0, 108.0}, kFine).chosen.empty());
    CHECK(selectForTruck(items, {63000.0, 0.0, 108.0}, kFine).chosen.empty());
}

TEST_CASE("knapsack: an item exactly at capacity is chosen") {
    const KnapsackSelection selection = selectForTruck({load(63000, 45000)}, kTruck, {10.0, 10.0, 1000});
    CHECK(selection.chosen == vector<size_t>{0});
    CHECK(selection.weightFull);
}

TEST_CASE("knapsack: an item exactly at the ceiling fits; a hair over does not") {
    CHECK(selectForTruck({load(1920, 1000, 1.0, "2026-08-06", 0, 108.0)}, kTruck, kFine).chosen.size() == 1);
    CHECK(selectForTruck({load(1920, 1000, 1.0, "2026-08-06", 0, 108.0 + 1e-6)}, kTruck, kFine).chosen.empty());
}

TEST_CASE("knapsack: when only one item fits it is chosen, whatever its priority") {
    const vector<KnapsackItem> items{load(40000, 1000, 1.0, "2026-08-06", 0),
                                     load(30000, 1000, 1.0, "2026-08-06", 11)};
    const vector<KnapsackItem> onlyLow{load(64000, 1000, 1.0, "2026-08-06", 0),
                                       load(30000, 1000, 1.0, "2026-08-06", 11)};
    CHECK(selectForTruck(items, kTruck, kFine).chosen == vector<size_t>{0});
    CHECK(selectForTruck(onlyLow, kTruck, kFine).chosen == vector<size_t>{1});
}

TEST_CASE("knapsack: capacity equal to the total takes everything") {
    const vector<KnapsackItem> items{load(31500, 22500), load(31500, 22500)};
    const KnapsackSelection selection = selectForTruck(items, kTruck, kFine);
    CHECK(selection.everythingFit);
    CHECK(selection.areaUsedSqIn == doctest::Approx(63000.0));
    CHECK(selection.weightUsedLb == doctest::Approx(45000.0));
}

TEST_CASE("knapsack: the report names which limit bound") {
    const KnapsackSelection floor = selectForTruck({load(40000, 1000), load(30000, 1000)}, kTruck, kFine);
    CHECK(floor.binding == BindingLimit::FloorArea);
    const KnapsackSelection weight = selectForTruck({load(1920, 30000), load(1920, 20000)}, kTruck, kFine);
    CHECK(weight.binding == BindingLimit::Weight);
    const KnapsackSelection both = selectForTruck({load(40000, 30000), load(30000, 20000)}, kTruck, kFine);
    CHECK(both.binding == BindingLimit::Both);
}

TEST_CASE("knapsack: exactBeatsGreedy on an adversarial order") {
    // Greedy takes the 60% item and can fit neither half; the two halves fill the truck.
    const vector<KnapsackItem> items{load(37800, 1000, 3.0), load(31500, 1000, 2.5), load(31500, 1000, 2.5)};
    const KnapsackSelection greedy = selectGreedy(items, kTruck, kFine);
    const KnapsackSelection exact = selectForTruck(items, kTruck, kFine);
    CHECK(greedy.chosen == vector<size_t>{0});
    CHECK(exact.chosen == vector<size_t>{1, 2});
    CHECK(greedy.value < exact.value);
    CHECK(exact.proven);
}

TEST_CASE("knapsack: overdue beats on-time for the same space") {
    const vector<KnapsackItem> items{load(40000, 1000, 4.0, "2026-08-06", 0),
                                     load(40000, 1000, 1.0, "2026-08-01", 11)};
    CHECK(selectForTruck(items, kTruck, kFine).chosen == vector<size_t>{1});
}

TEST_CASE("knapsack: priority 0 beats 11 on the same date, whatever the size") {
    const vector<KnapsackItem> items{load(40000, 1000, 4.0, "2026-08-06", 11),
                                     load(40000, 1000, 0.5, "2026-08-06", 0)};
    CHECK(selectForTruck(items, kTruck, kFine).chosen == vector<size_t>{1});
}

TEST_CASE("knapsack: dateThenPriority - an earlier date beats a better priority") {
    const vector<KnapsackItem> items{load(40000, 1000, 1.0, "2026-08-07", 0),
                                     load(40000, 1000, 1.0, "2026-08-06", 11)};
    CHECK(selectForTruck(items, kTruck, kFine).chosen == vector<size_t>{1});
}

TEST_CASE("knapsack: identical items tie-break to the earliest in the given order") {
    const vector<KnapsackItem> items(5, load(20000, 1000));
    const KnapsackSelection first = selectForTruck(items, kTruck, kFine);
    const KnapsackSelection second = selectForTruck(items, kTruck, kFine);
    CHECK(first.chosen == vector<size_t>{0, 1, 2});
    CHECK(second.chosen == first.chosen);
}

TEST_CASE("knapsack: fractional pounds round up to the grain, capacity down") {
    const KnapsackProblem problem = prepareKnapsack({load(1920, 10.4)}, {63000.0, 45000.9, 108.0}, kFine);
    CHECK(problem.weightGrains.front() == 11);
    CHECK(problem.weightCapacity == 45000);
}

TEST_CASE("knapsack: rounding never passes a load over the payload") {
    // Three items of 15,000.2 lb weigh 45,000.6: over. Rounded down they would pass.
    const vector<KnapsackItem> items(3, load(1920, 15000.2));
    const KnapsackSelection selection = selectForTruck(items, kTruck, kFine);
    CHECK(selection.chosen.size() == 2);
    // Two of 22,499.6 lb round up to exactly the payload, and fit.
    CHECK(selectForTruck(vector<KnapsackItem>(2, load(1920, 22499.6)), kTruck, kFine).chosen.size() == 2);
}

TEST_CASE("knapsack: fullAtHundredPercentWeight") {
    CHECK(selectForTruck({load(1920, 25000), load(1920, 20000)}, kTruck, kFine).weightFull);
    CHECK_FALSE(selectForTruck({load(1920, 25000), load(1920, 19999)}, kTruck, kFine).weightFull);
}

TEST_CASE("knapsack: tableAndBranchBoundAgree and bothMatchBruteForceUnder20 (differential)") {
    mt19937 random(31102026);
    const KnapsackSettings coarse{250.0, 500.0, 5'000'000};
    for (int trial = 0; trial < 150; ++trial) {
        const vector<KnapsackItem> items = randomItems(random, 1 + random() % 14);
        const KnapsackProblem problem = prepareKnapsack(items, kTruck, coarse);
        const KnapsackSelection branchAndBound = selectForTruck(items, kTruck, coarse);
        const KnapsackSelection table = selectByTable(items, kTruck, coarse);
        REQUIRE(branchAndBound.proven);
        REQUIRE(branchAndBound.value == table.value);
        REQUIRE(branchAndBound.value == bruteForceValue(problem));
        checkWithinCapacity(items, branchAndBound, kTruck);
        checkWithinCapacity(items, table, kTruck);
    }
}

TEST_CASE("knapsack: neverOverCapacity and chosenPlusLeftEqualsInput (property)") {
    mt19937 random(1112026);
    for (int trial = 0; trial < 300; ++trial) {
        const vector<KnapsackItem> items = randomItems(random, random() % 40);
        const KnapsackSelection selection = selectForTruck(items, kTruck, kFine);
        checkEveryItemOnce(selection, items.size());
        checkWithinCapacity(items, selection, kTruck);
        REQUIRE_FALSE(selection.value < selectGreedy(items, kTruck, kFine).value);
    }
}

TEST_CASE("knapsack: shuffleSameSelection - shuffled input gives the same value") {
    mt19937 random(2112026);
    for (int trial = 0; trial < 100; ++trial) {
        vector<KnapsackItem> items = randomItems(random, 12);
        const KnapsackValue original = selectForTruck(items, kTruck, kFine).value;
        shuffle(items.begin(), items.end(), random);
        REQUIRE(selectForTruck(items, kTruck, kFine).value == original);
    }
}

TEST_CASE("knapsack: the largest lane's 1,337 unit loads select within budget") {
    mt19937 random(3112026);
    vector<KnapsackItem> items = randomItems(random, 1337);
    for (KnapsackItem& item : items) item.areaSqIn = 1920.0;
    const auto start = chrono::steady_clock::now();
    const LaneSelection lane = selectForTrucks(items, kTruck, {1.0, 1.0, 200'000}, 41);
    const double seconds = chrono::duration<double>(chrono::steady_clock::now() - start).count();
    CHECK(seconds < 10.0);
    size_t placed = lane.leftOver.size();
    for (const KnapsackSelection& truck : lane.trucks) {
        placed += truck.chosen.size();
        checkWithinCapacity(items, truck, kTruck);
    }
    CHECK(placed == items.size());
}

TEST_CASE("knapsack: 10,000 items stop at the node limit with the best found, marked unproven") {
    mt19937 random(4112026);
    const vector<KnapsackItem> items = randomItems(random, 10000);
    const KnapsackSelection selection = selectForTruck(items, kTruck, {1.0, 1.0, 50'000});
    checkEveryItemOnce(selection, items.size());
    checkWithinCapacity(items, selection, kTruck);
    if (!selection.proven) CHECK(selection.nodes > 50'000);
    CHECK_FALSE(selection.value < selectGreedy(items, kTruck, kFine).value);
}

TEST_CASE("knapsack: trucks are filled in turn, most urgent first, nothing lost") {
    vector<KnapsackItem> items;
    for (int index = 0; index < 10; ++index) items.push_back(load(20000, 5000, 1.0, "2026-08-06", index % 2 == 0 ? 11 : 0));
    const LaneSelection lane = selectForTrucks(items, kTruck, kFine, 2);
    REQUIRE(lane.trucks.size() == 2);
    for (const size_t index : lane.trucks[0].chosen) CHECK(items[index].priority == 0);
    size_t placed = lane.leftOver.size();
    for (const KnapsackSelection& truck : lane.trucks) placed += truck.chosen.size();
    CHECK(placed == 10);
    CHECK(lane.leftOver.size() == 4);
    CHECK(selectForTrucks(items, kTruck, kFine, 0).leftOver.size() == 10);
    CHECK_THROWS_AS(selectForTrucks(items, kTruck, kFine, -1), invalid_argument);
}

TEST_CASE("knapsack: bad items, capacity and settings are rejected") {
    const double nan = numeric_limits<double>::quiet_NaN();
    const double infinity = numeric_limits<double>::infinity();
    for (const double bad : {nan, infinity, -1.0}) {
        CHECK_THROWS_AS(selectForTruck({load(bad, 1000)}, kTruck, kFine), invalid_argument);
        CHECK_THROWS_AS(selectForTruck({load(1920, bad)}, kTruck, kFine), invalid_argument);
        CHECK_THROWS_AS(selectForTruck({load(1920, 1000, bad)}, kTruck, kFine), invalid_argument);
        CHECK_THROWS_AS(selectForTruck({load(1920, 1000, 1.0, "2026-08-06", 0, bad)}, kTruck, kFine), invalid_argument);
        CHECK_THROWS_AS(selectForTruck({}, {bad, 45000.0, 108.0}, kFine), invalid_argument);
        CHECK_THROWS_AS(selectForTruck({}, {63000.0, bad, 108.0}, kFine), invalid_argument);
        CHECK_THROWS_AS(selectForTruck({}, {63000.0, 45000.0, bad}, kFine), invalid_argument);
        CHECK_THROWS_AS(selectForTruck({}, kTruck, {bad, 1.0, 1000}), invalid_argument);
        CHECK_THROWS_AS(selectForTruck({}, kTruck, {1.0, bad, 1000}), invalid_argument);
    }
    CHECK_THROWS_AS(selectForTruck({load(0.0, 1000)}, kTruck, kFine), invalid_argument);
    CHECK_THROWS_AS(selectForTruck({load(1920, 1000, 0.0)}, kTruck, kFine), invalid_argument);
    CHECK_THROWS_AS(selectForTruck({load(1920, 1000, 1.0, "2026-08-32")}, kTruck, kFine), invalid_argument);
    CHECK_THROWS_AS(selectForTruck({load(1920, 1000, 1.0, "2026-08-06", -1)}, kTruck, kFine), invalid_argument);
    CHECK_THROWS_AS(selectForTruck({load(1920, 1000, 1.0, "2026-08-06", 12)}, kTruck, kFine), invalid_argument);
    CHECK_THROWS_AS(selectForTruck({}, {63000.0, 45000.0, 0.0}, kFine), invalid_argument);
    CHECK_THROWS_AS(selectForTruck({}, kTruck, {0.0, 1.0, 1000}), invalid_argument);
    CHECK_THROWS_AS(selectForTruck({}, kTruck, {1.0, 1.0, 0}), invalid_argument);
    // A grain coarser than the lightest item would pack it for free.
    CHECK_THROWS_AS(selectForTruck({load(1920, 5.0)}, kTruck, {10.0, 1.0, 1000}), invalid_argument);
    CHECK_THROWS_AS(selectForTruck({load(1920, 1000)}, kTruck, {1.0, 2000.0, 1000}), invalid_argument);
    // At 1 lb and 1 sq in grains the table would be 63,001 x 45,001 cells.
    CHECK_THROWS_AS(selectByTable({load(1920, 1000)}, kTruck, kFine), invalid_argument);
}

TEST_CASE("knapsack: August knapsack lanes fill their reserved trucks within capacity"
          * doctest::skip(!m4AugustTests::augustPresent())) {
    const KnapsackSettings settings{0.001, 0.001, 200'000};
    for (size_t runIndex = 0; runIndex < m4AugustTests::kAugustRunFiles.size(); ++runIndex) {
        CAPTURE(runIndex);
        const m4AugustTests::AugustItems& august = m4AugustTests::augustItems(runIndex);
        const LaneClassification classification =
            classifyLanes(august.run, august.book, august.built, {20, 5.0});
        const KnapsackCapacity capacity = capacityOf(august.run.trailer);
        for (const LaneRoute& route : classification.routes) {
            if (route.method != SolveMethod::Knapsack) continue;
            vector<KnapsackItem> items;
            for (const size_t index : route.itemIndices) items.push_back(knapsackItemFrom(august.built.items[index]));
            const LaneSelection lane = selectForTrucks(items, capacity, settings, static_cast<int>(route.reservedTrucks));
            REQUIRE(lane.trucks.size() == static_cast<size_t>(route.reservedTrucks));
            size_t placed = lane.leftOver.size();
            for (const KnapsackSelection& truck : lane.trucks) {
                placed += truck.chosen.size();
                checkWithinCapacity(items, truck, capacity);
            }
            REQUIRE(placed == items.size());
        }
    }
}
