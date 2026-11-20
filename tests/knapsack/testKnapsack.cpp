#include "doctest.h"
#include "../m4AugustFixtures.hpp"
#include "knapsack.hpp"
#include "laneClassifier.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace std;
using namespace ob;

namespace {

// A 630 x 100 in floor and a 45,000 lb payload.
const KnapsackCapacity kTruck{63000.0, 45000.0, 108.0};
const KnapsackSettings kFine{1.0, 1.0, 2'000'000};
// Large enough for the coarse grains the cross-check uses.
const long long kTableCells = 4'000'000;

KnapsackItem load(double area, double weight, double unitLoads = 1.0,
                  const string& shipDate = "2026-08-06", int priority = 0, double height = 60.0) {
    return {area, weight, height, unitLoads, shipDate, priority, ""};
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

TEST_CASE("knapsack: the table runs at its cell limit and throws one cell under it") {
    const KnapsackSettings coarse{250.0, 500.0, 1000};
    const vector<KnapsackItem> items{load(1920, 1000)};
    const KnapsackProblem problem = prepareKnapsack(items, kTruck, coarse);
    const long long cells = (problem.areaCapacity + 1) * (problem.weightCapacity + 1);
    CHECK(selectByTable(items, kTruck, coarse, cells).chosen == vector<size_t>{0});
    CHECK_THROWS_AS(selectByTable(items, kTruck, coarse, cells - 1), invalid_argument);
    CHECK_THROWS_AS(selectByTable(items, kTruck, coarse, 0), invalid_argument);
    CHECK_THROWS_AS(selectByTable(items, kTruck, coarse, -5), invalid_argument);
}

TEST_CASE("knapsack: tableAndBranchBoundAgree and bothMatchBruteForceUnder20 (differential)") {
    mt19937 random(31102026);
    const KnapsackSettings coarse{250.0, 500.0, 5'000'000};
    for (int trial = 0; trial < 150; ++trial) {
        const vector<KnapsackItem> items = randomItems(random, 1 + random() % 14);
        const KnapsackProblem problem = prepareKnapsack(items, kTruck, coarse);
        const KnapsackSelection branchAndBound = selectForTruck(items, kTruck, coarse);
        const KnapsackSelection table = selectByTable(items, kTruck, coarse, kTableCells);
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
    CHECK_THROWS_AS(selectByTable({load(1920, 1000)}, kTruck, kFine, kTableCells), invalid_argument);
}

TEST_CASE("knapsack: a grain so fine the floor or payload counts past kMaxGrains is rejected") {
    for (const double grain : {1e-12, 1e-300, numeric_limits<double>::denorm_min()}) {
        CAPTURE(grain);
        CHECK_THROWS_AS(prepareKnapsack({}, kTruck, {1.0, grain, 1000}), invalid_argument);
        CHECK_THROWS_AS(prepareKnapsack({}, kTruck, {grain, 1.0, 1000}), invalid_argument);
        CHECK_THROWS_AS(selectForTruck({load(1920, 1000)}, kTruck, {grain, grain, 1000}), invalid_argument);
        CHECK_THROWS_AS(selectGreedy({load(1920, 1000)}, kTruck, {grain, grain, 1000}), invalid_argument);
    }
}

TEST_CASE("knapsack: a floor or payload counting past kMaxGrains at a normal grain is rejected") {
    for (const double huge : {1e17, 1e300, numeric_limits<double>::max()}) {
        CAPTURE(huge);
        CHECK_THROWS_AS(prepareKnapsack({}, {huge, 45000.0, 108.0}, kFine), invalid_argument);
        CHECK_THROWS_AS(prepareKnapsack({}, {63000.0, huge, 108.0}, kFine), invalid_argument);
    }
}

TEST_CASE("knapsack: an item too large to count in grains is set aside as too large") {
    for (const double huge : {1e17, 1e300, numeric_limits<double>::max()}) {
        CAPTURE(huge);
        const KnapsackProblem problem = prepareKnapsack({load(huge, 1000), load(1920, huge), load(1920, 1000)}, kTruck, kFine);
        CHECK(problem.tooLarge == vector<size_t>{0, 1});
        CHECK(problem.candidates == vector<size_t>{2});
    }
}

TEST_CASE("knapsack: unit loads too many to count in thousandths are rejected") {
    for (const double huge : {1e17, 1e300}) {
        CAPTURE(huge);
        CHECK_THROWS_AS(prepareKnapsack({load(1920, 1000, huge)}, kTruck, kFine), invalid_argument);
    }
}

TEST_CASE("knapsack: items whose grains together pass kMaxGrains are rejected, not summed past long long") {
    // At 1e-12 sq in a 9,000 sq in floor is 9.0e15 grains, just under kMaxGrains, and each item
    // 1.92e15: four of them are under it together, five over.
    const KnapsackSettings fineArea{1.0, 1e-12, 1000};
    const KnapsackCapacity wideFloor{9000.0, 45000.0, 108.0};
    CHECK_NOTHROW(prepareKnapsack(vector<KnapsackItem>(4, load(1920, 10)), wideFloor, fineArea));
    CHECK_THROWS_AS(prepareKnapsack(vector<KnapsackItem>(5, load(1920, 10)), wideFloor, fineArea), invalid_argument);
}

TEST_CASE("knapsack: a table whose floor times payload cells pass long long is rejected, not multiplied") {
    // 63,000,000,001 floor cells by 45,000,000,001 payload cells: 2.8e21, past 9.2e18.
    CHECK_THROWS_AS(selectByTable({load(1920, 1000)}, kTruck, {1e-6, 1e-6, 1}, kTableCells), invalid_argument);
}

TEST_CASE("knapsack: the shipped grain of 0.001 counts the 53 ft trailer and selects as before") {
    const KnapsackSettings shipped{0.001, 0.001, 200'000};
    const KnapsackProblem problem = prepareKnapsack({load(1920, 1000)}, kTruck, shipped);
    CHECK(problem.areaCapacity == 63'000'000);
    CHECK(problem.weightCapacity == 45'000'000);
    CHECK(problem.areaGrains == vector<long long>{1'920'000});
    const vector<KnapsackItem> items(40, load(1920, 1000));
    CHECK(selectForTruck(items, kTruck, shipped).chosen.size() == selectForTruck(items, kTruck, kFine).chosen.size());
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

TEST_CASE("knapsack: moreUrgent orders by rank date, then priority, then ship date, then product number") {
    KnapsackItem earliest = load(1920, 1000, 1.0, "2026-08-05", 11);
    earliest.productNumber = "Z";
    KnapsackItem urgentRankedLater = load(1920, 1000, 1.0, "2026-08-04", 0);
    urgentRankedLater.rankDate = "2026-08-06";
    urgentRankedLater.productNumber = "Z";
    KnapsackItem older = load(1920, 1000, 1.0, "2026-08-04", 11);
    older.rankDate = "2026-08-06";
    older.productNumber = "Z";
    KnapsackItem lowerProduct = load(1920, 1000, 1.0, "2026-08-06", 11);
    lowerProduct.productNumber = "A";
    KnapsackItem last = load(1920, 1000, 1.0, "2026-08-06", 11);
    last.productNumber = "Z";
    const vector<KnapsackItem> ordered{earliest, urgentRankedLater, older, lowerProduct, last};
    for (size_t index = 0; index + 1 < ordered.size(); ++index) {
        CAPTURE(index);
        CHECK(moreUrgent(ordered[index], ordered[index + 1]));
        CHECK_FALSE(moreUrgent(ordered[index + 1], ordered[index]));
    }
}

TEST_CASE("knapsack: goods sharing a rank date fill the truck by priority before age") {
    // Room for one: an older good at priority 11 and a newer one at 0, both ranked by 6 Aug.
    KnapsackItem older = load(40000, 1000, 1.0, "2026-08-04", 11);
    KnapsackItem newer = load(40000, 1000, 1.0, "2026-08-06", 0);
    CHECK(selectForTruck({older, newer}, kTruck, kFine).chosen == vector<size_t>{0});
    older.rankDate = "2026-08-06";
    newer.rankDate = "2026-08-06";
    CHECK(selectForTruck({older, newer}, kTruck, kFine).chosen == vector<size_t>{1});
}

TEST_CASE("knapsack: a rank date that is not a real date is rejected") {
    KnapsackItem bad = load(1920, 1000);
    bad.rankDate = "2026-02-30";
    CHECK_THROWS_AS(selectForTruck({bad}, kTruck, kFine), invalid_argument);
}

TEST_CASE("knapsack: trucksByUrgency keeps the most urgent truck first, and the fuller of two equally urgent") {
    const vector<KnapsackItem> items{load(1920, 1000, 1.0, "2026-08-06", 11), load(1920, 1000, 1.0, "2026-08-06", 0),
                                     load(1920, 1000, 1.0, "2026-08-06", 5)};
    CHECK(trucksByUrgency(items, {{0}, {2}, {1}}) == vector<size_t>{2, 1, 0});
    CHECK(trucksByUrgency(items, {{1}, {1, 2}}) == vector<size_t>{1, 0});
    CHECK(trucksByUrgency(items, {{0}, {0}}) == vector<size_t>{0, 1});
}

namespace {

TruckCheck checkOf(const function<bool(const vector<size_t>&)>& accepts) {
    return [accepts](const vector<size_t>& chosen) { return accepts(chosen) ? TruckFit::Fits : TruckFit::DoesNotFit; };
}

// Accepts a load unless it holds exactly two goods: a check that refuses {0, 1} yet accepts {0, 1, 2}.
const TruckLimits kNotTwo{checkOf([](const vector<size_t>& chosen) { return chosen.size() != 2; }), {}, {}, {}};

// Every subset within floor and payload that `accepts` takes, judged on the prepared grains: the
// oracle for a search with a check of any shape.
KnapsackValue bruteForceValue(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                              const KnapsackSettings& settings, const function<bool(const vector<size_t>&)>& accepts) {
    const KnapsackProblem problem = prepareKnapsack(items, capacity, settings);
    const size_t count = problem.candidates.size();
    KnapsackValue best(problem.classCount + 1, 0);
    for (size_t mask = 0; mask < (size_t{1} << count); ++mask) {
        long long area = 0, weight = 0;
        KnapsackValue value(problem.classCount + 1, 0);
        vector<size_t> chosen;
        for (size_t position = 0; position < count; ++position) {
            if (!(mask & (size_t{1} << position))) continue;
            chosen.push_back(problem.candidates[position]);
            area += problem.areaGrains[position];
            weight += problem.weightGrains[position];
            value[problem.classOf[position]] += problem.amount[position];
            value.back() += problem.areaGrains[position];
        }
        if (area > problem.areaCapacity || weight > problem.weightCapacity || !(best < value)) continue;
        if (accepts(chosen)) best = value;
    }
    return best;
}

} // anonymous namespace

TEST_CASE("knapsack: a load the check refuses is still grown into a larger one it accepts") {
    const vector<KnapsackItem> items(3, load(1920, 1000));
    const KnapsackSelection selection = selectForTruck(items, kTruck, kFine, kNotTwo);
    CHECK(selection.chosen == vector<size_t>{0, 1, 2});
    CHECK(selection.proven);
}

TEST_CASE("knapsack: a truck is unproven when a load better than the one chosen comes back undecided") {
    const vector<KnapsackItem> items(3, load(1920, 1000));
    const TruckLimits undecidedAtThree{[](const vector<size_t>& chosen) {
        return chosen.size() == 3 ? TruckFit::Undecided : TruckFit::Fits;
    }, {}, {}, {}};
    const KnapsackSelection selection = selectForTruck(items, kTruck, kFine, undecidedAtThree);
    CHECK(selection.chosen == vector<size_t>{0, 1});
    CHECK(selection.betterLoadUndecided);
    CHECK_FALSE(selection.stoppedAtNodeLimit);
    CHECK_FALSE(selection.proven);
}

TEST_CASE("knapsack: a load the check could not decide is still grown into a larger one it accepts, proven") {
    const vector<KnapsackItem> items(3, load(1920, 1000));
    const TruckLimits undecidedAtTwo{[](const vector<size_t>& chosen) {
        return chosen.size() == 2 ? TruckFit::Undecided : TruckFit::Fits;
    }, {}, {}, {}};
    const KnapsackSelection selection = selectForTruck(items, kTruck, kFine, undecidedAtTwo);
    CHECK(selection.chosen == vector<size_t>{0, 1, 2});
    CHECK_FALSE(selection.betterLoadUndecided);
    CHECK(selection.proven);
}

TEST_CASE("knapsack: a load the quick check refuses but `fits` accepts is still found, proven, truck by truck") {
    const vector<KnapsackItem> items(6, load(1920, 1000));
    TruckLimits atMostOneQuickly;
    atMostOneQuickly.fits = [](const vector<size_t>& chosen) { return chosen.size() <= 3 ? TruckFit::Fits : TruckFit::DoesNotFit; };
    atMostOneQuickly.quickFits = [](const vector<size_t>& chosen) { return chosen.size() <= 1; };
    const LaneSelection lane = selectForTrucks(items, kTruck, kFine, 2, atMostOneQuickly);
    REQUIRE(lane.trucks.size() == 2);
    CHECK(lane.trucks[0].chosen == vector<size_t>{0, 1, 2});
    CHECK(lane.trucks[1].chosen == vector<size_t>{3, 4, 5});
    CHECK(lane.proven);
}

TEST_CASE("knapsack: a truck stopped at its node limit says so apart from an undecided load") {
    const vector<KnapsackItem> items(3, load(1920, 1000));
    const KnapsackSelection selection = selectForTruck(items, kTruck, {1.0, 1.0, 1}, kNotTwo);
    CHECK(selection.stoppedAtNodeLimit);
    CHECK_FALSE(selection.betterLoadUndecided);
    CHECK_FALSE(selection.proven);
}

TEST_CASE("knapsack: with a check of any shape, a proven truck is the best load the check accepts (differential)") {
    mt19937 random(10112026);
    const KnapsackSettings coarse{250.0, 500.0, 5'000'000};
    for (int trial = 0; trial < 150; ++trial) {
        const vector<KnapsackItem> items = randomItems(random, 1 + random() % 12);
        // A fixed random verdict per set of goods, so refusing a load says nothing of those holding it.
        const unsigned salt = static_cast<unsigned>(random());
        auto accepts = [salt](vector<size_t> chosen) {
            sort(chosen.begin(), chosen.end());
            size_t mask = 0;
            for (const size_t index : chosen) mask |= size_t{1} << index;
            return ((mask * 2654435761u) ^ salt) % 3 != 0;
        };
        const KnapsackSelection selection = selectForTruck(items, kTruck, coarse, {checkOf(accepts), {}, {}, {}});
        CAPTURE(trial);
        REQUIRE(selection.proven);
        REQUIRE((selection.chosen.empty() || accepts(selection.chosen)));
        REQUIRE(selection.value == bruteForceValue(items, kTruck, coarse, accepts));
    }
}

namespace {

// A stack the rows see: its footprint and levels, at the floor area they give it.
KnapsackItem stackItem(double longSide, double shortSide, double weight, double unitLoads = 1.0,
                   const string& shipDate = "2026-08-06") {
    KnapsackItem item = load(longSide * shortSide, weight, unitLoads, shipDate);
    item.longSideIn = longSide;
    item.shortSideIn = shortSide;
    item.levels = static_cast<int>(unitLoads);
    return item;
}

} // anonymous namespace

TEST_CASE("knapsack: of goods alike but for weight, a truck carries the lightest") {
    const vector<KnapsackItem> items{stackItem(48, 40, 9000), stackItem(48, 40, 1000), stackItem(48, 40, 5000), stackItem(48, 40, 3000)};
    const KnapsackSelection selection = selectForTruck(items, {63000.0, 10000.0, 108.0}, kFine);
    CHECK(selection.chosen == vector<size_t>{1, 2, 3});
    CHECK(selection.proven);
}

TEST_CASE("knapsack: with many goods alike but for weight, a proven truck is the best load its rows accept (differential)") {
    mt19937 random(16112026);
    const array<pair<double, double>, 3> footprints{{{48, 40}, {48, 36}, {60, 48}}};
    const array<string, 2> dates{"2026-08-05", "2026-08-06"};
    const KnapsackSettings coarse{50.0, 50.0, 5'000'000};
    for (int trial = 0; trial < 150; ++trial) {
        vector<KnapsackItem> items;
        const size_t count = 4 + random() % 13;
        for (size_t index = 0; index < count; ++index) {
            const pair<double, double>& sides = footprints[random() % footprints.size()];
            items.push_back(stackItem(sides.first, sides.second, static_cast<double>(500 + random() % 9500),
                                  static_cast<double>(1 + random() % 2), dates[random() % dates.size()]));
        }
        // Rows that hold no more than five of the widest footprint: a check judging goods by what
        // the rows see of them, as the selector's does.
        auto accepts = [&items](const vector<size_t>& chosen) {
            return count_if(chosen.begin(), chosen.end(), [&](size_t index) { return items[index].longSideIn == 60.0; }) <= 5;
        };
        const KnapsackCapacity capacity{20000.0, 20000.0 + static_cast<double>(random() % 20000), 108.0};
        const KnapsackSelection selection = selectForTruck(items, capacity, coarse, {checkOf(accepts), {}, {}, {}});
        CAPTURE(trial);
        REQUIRE(selection.proven);
        checkEveryItemOnce(selection, items.size());
        checkWithinCapacity(items, selection, capacity);
        REQUIRE(accepts(selection.chosen));
        REQUIRE(selection.value == bruteForceValue(items, capacity, coarse, accepts));
    }
}

TEST_CASE("knapsack: no truck is chosen over an item limit") {
    const vector<KnapsackItem> items(10, load(1920, 1000, 2.0));
    const TruckLimits unitLoadCap{{}, {{vector<double>(10, 2.0), 7.0}}, {}, {}};
    const KnapsackSelection selection = selectForTruck(items, kTruck, kFine, unitLoadCap);
    CHECK(selection.chosen.size() == 3);
    CHECK(selection.proven);
}

TEST_CASE("knapsack: an item whose share of a limit is infinite is never chosen") {
    const vector<KnapsackItem> items(2, load(1920, 1000));
    const double infinity = numeric_limits<double>::infinity();
    const KnapsackSelection selection = selectForTruck(items, kTruck, kFine, {{}, {{{infinity, 1.0}, 630.0}}, {}, {}});
    CHECK(selection.chosen == vector<size_t>{1});
}

TEST_CASE("knapsack: limits that are not one share per item, or out of range, are rejected") {
    const vector<KnapsackItem> items(2, load(1920, 1000));
    const double nan = numeric_limits<double>::quiet_NaN();
    const double infinity = numeric_limits<double>::infinity();
    CHECK_THROWS_AS(selectForTruck(items, kTruck, kFine, {{}, {{{1.0}, 10.0}}, {}, {}}), invalid_argument);
    for (const double bad : {nan, -1.0}) {
        CHECK_THROWS_AS(selectForTruck(items, kTruck, kFine, {{}, {{{1.0, bad}, 10.0}}, {}, {}}), invalid_argument);
    }
    for (const double bad : {nan, infinity, -1.0}) {
        CHECK_THROWS_AS(selectForTruck(items, kTruck, kFine, {{}, {{{1.0, 1.0}, bad}}, {}, {}}), invalid_argument);
        CHECK_THROWS_AS(selectForTrucks(items, kTruck, kFine, 1, {{}, {{{1.0, 1.0}, bad}}, {}, {}}), invalid_argument);
    }
}

TEST_CASE("knapsack: the next truck is chosen from the goods remaining, named as the caller's items") {
    vector<KnapsackItem> items(4, load(40000, 1000));
    items[3].priority = 0;
    for (size_t index = 0; index < 3; ++index) items[index].priority = 5;
    const KnapsackSelection next = selectNextTruck(items, {1, 3}, kTruck, kFine, kNotTwo);
    CHECK(next.chosen == vector<size_t>{3});
    CHECK(next.leftOver == vector<size_t>{1});
    CHECK_THROWS_AS(selectNextTruck(items, {1, 4}, kTruck, kFine), invalid_argument);
}

namespace {

const KnapsackCapacity kTwoStacksOf10000Lb{63000.0, 10000.0, 108.0};

// No truck holds more than `stacks` of the goods: a stand-in for rows that take two fixed stacks.
TruckLimits stacksPerTruck(size_t itemCount, double stacks) { return {{}, {{vector<double>(itemCount, 1.0), stacks}}, {}, {}}; }

// Unit loads at priority 0, then at 1, then floor, over every truck of the lane.
KnapsackValue laneValue(const vector<KnapsackItem>& items, const vector<vector<size_t>>& trucks) {
    KnapsackValue total(3, 0);
    for (const vector<size_t>& truck : trucks) {
        for (const size_t index : truck) {
            total[static_cast<size_t>(items[index].priority)] += llround(items[index].unitLoads);
            total.back() += llround(items[index].areaSqIn);
        }
    }
    return total;
}

// The trucks filled in turn by selectForTruck alone, each taking the lightest of goods alike, as
// indices into `items`.
vector<vector<size_t>> lightestFirstTrucks(const vector<KnapsackItem>& items, int trucks, double stacks) {
    vector<vector<size_t>> filled;
    vector<size_t> remaining(items.size());
    iota(remaining.begin(), remaining.end(), size_t{0});
    for (int truck = 0; truck < trucks; ++truck) {
        vector<KnapsackItem> pool;
        for (const size_t index : remaining) pool.push_back(items[index]);
        KnapsackSelection selection = selectForTruck(pool, kTwoStacksOf10000Lb, kFine, stacksPerTruck(pool.size(), stacks));
        vector<size_t>& chosen = filled.emplace_back();
        for (const size_t index : selection.chosen) chosen.push_back(remaining[index]);
        vector<size_t> left;
        for (const size_t index : selection.leftOver) left.push_back(remaining[index]);
        remaining = std::move(left);
    }
    return filled;
}

} // anonymous namespace

TEST_CASE("knapsack: Tom's 2,000, 2,000 and three 8,000 lb stacks ship four on two 10,000 lb trucks of two stacks") {
    const vector<KnapsackItem> items{stackItem(48, 40, 2000), stackItem(48, 40, 2000), stackItem(48, 40, 8000),
                                     stackItem(48, 40, 8000), stackItem(48, 40, 8000)};
    const LaneSelection lane = selectForTrucks(items, kTwoStacksOf10000Lb, kFine, 2, stacksPerTruck(items.size(), 2.0));
    REQUIRE(lane.trucks.size() == 2);
    for (const KnapsackSelection& truck : lane.trucks) {
        CHECK(truck.chosen.size() == 2);
        CHECK(truck.weightUsedLb == doctest::Approx(10000.0));
        CHECK(truck.proven);
    }
    CHECK(lane.leftOver.size() == 1);
}

TEST_CASE("knapsack: a lane's first truck keeps the value and proof of the best single truck") {
    mt19937 random(20112026);
    for (int trial = 0; trial < 300; ++trial) {
        vector<KnapsackItem> items;
        const size_t count = 3 + random() % 10;
        for (size_t index = 0; index < count; ++index) {
            items.push_back(stackItem(48, random() % 2 == 0 ? 40.0 : 36.0, static_cast<double>(500 + random() % 9500)));
            items.back().priority = static_cast<int>(random() % 2);
        }
        const double stacks = static_cast<double>(1 + random() % 3);
        const KnapsackSelection alone = selectForTruck(items, kTwoStacksOf10000Lb, kFine, stacksPerTruck(count, stacks));
        const LaneSelection lane = selectForTrucks(items, kTwoStacksOf10000Lb, kFine, 2, stacksPerTruck(count, stacks));
        CAPTURE(trial);
        for (const KnapsackSelection& truck : lane.trucks) checkWithinCapacity(items, truck, kTwoStacksOf10000Lb);
        REQUIRE(lane.trucks[0].value == alone.value);
        REQUIRE(lane.trucks[0].proven == alone.proven);
    }
}

TEST_CASE("knapsack: a more urgent light good is never traded for a heavier less urgent one") {
    vector<KnapsackItem> items{stackItem(48, 40, 2000), stackItem(48, 40, 2000), stackItem(48, 40, 8000),
                               stackItem(48, 40, 8000), stackItem(48, 40, 8000)};
    for (size_t heavy = 2; heavy < items.size(); ++heavy) items[heavy].priority = 11;
    const LaneSelection lane = selectForTrucks(items, kTwoStacksOf10000Lb, kFine, 2, stacksPerTruck(items.size(), 2.0));
    REQUIRE(lane.trucks.size() == 2);
    CHECK(lane.trucks[0].chosen == vector<size_t>{0, 1});
}

TEST_CASE("knapsack: trucks filled in turn ship no less of any class than lightest-first on random lanes") {
    mt19937 random(19112026);
    int shippedMore = 0;
    for (int trial = 0; trial < 400; ++trial) {
        vector<KnapsackItem> items;
        const size_t count = 4 + random() % 6;
        for (size_t index = 0; index < count; ++index) {
            items.push_back(stackItem(48, 40, static_cast<double>(500 + random() % 9500)));
            items.back().priority = static_cast<int>(random() % 2);
        }
        const int trucks = 2 + static_cast<int>(random() % 2);
        const double stacks = static_cast<double>(2 + random() % 2);
        const LaneSelection lane = selectForTrucks(items, kTwoStacksOf10000Lb, kFine, trucks, stacksPerTruck(count, stacks));
        vector<vector<size_t>> chosen;
        for (const KnapsackSelection& truck : lane.trucks) chosen.push_back(truck.chosen);
        const KnapsackValue before = laneValue(items, lightestFirstTrucks(items, trucks, stacks));
        const KnapsackValue after = laneValue(items, chosen);
        CAPTURE(trial);
        REQUIRE_FALSE(after < before);
        if (before < after) ++shippedMore;
        for (const KnapsackSelection& truck : lane.trucks) checkWithinCapacity(items, truck, kTwoStacksOf10000Lb);
    }
    CHECK(shippedMore > 0);
}

TEST_CASE("knapsack: no load within the floor, payload and limits carries more unit loads than the upper bound (oracle)") {
    mt19937 random(21112026);
    for (int trial = 0; trial < 300; ++trial) {
        const size_t count = 3 + random() % 10;
        vector<KnapsackItem> items;
        vector<double> rowShares;
        for (size_t index = 0; index < count; ++index) {
            items.push_back(stackItem(48, 40, static_cast<double>(500 + random() % 9500), static_cast<double>(1 + random() % 3)));
            items.back().areaSqIn = static_cast<double>(1000 + random() % 9000);
            rowShares.push_back(static_cast<double>(20 + random() % 100));
        }
        vector<double> unitLoadShares;
        for (const KnapsackItem& item : items) unitLoadShares.push_back(item.unitLoads);
        const KnapsackCapacity capacity{static_cast<double>(10000 + random() % 30000), static_cast<double>(5000 + random() % 30000), 108.0};
        const vector<ItemLimit> limits{{unitLoadShares, static_cast<double>(2 + random() % 10)}, {rowShares, 300.0}};
        const double bound = unitLoadUpperBound(items, capacity, limits);
        double best = 0.0;
        for (size_t mask = 0; mask < (size_t{1} << count); ++mask) {
            double area = 0.0, weight = 0.0, unitLoads = 0.0, rowLength = 0.0;
            for (size_t index = 0; index < count; ++index) {
                if (!(mask >> index & 1)) continue;
                area += items[index].areaSqIn;
                weight += items[index].weightLb;
                unitLoads += items[index].unitLoads;
                rowLength += rowShares[index];
            }
            if (area <= capacity.floorAreaSqIn && weight <= capacity.weightLb && unitLoads <= limits[0].capacity
                && rowLength <= limits[1].capacity) {
                best = max(best, unitLoads);
            }
        }
        CAPTURE(trial);
        REQUIRE(best <= bound + kQuantityEpsilon);
    }
}

TEST_CASE("knapsack: the upper bound is the payload's share when only the payload binds") {
    const vector<KnapsackItem> items(10, stackItem(48, 40, 1000));
    CHECK(unitLoadUpperBound(items, {63000.0, 7000.0, 108.0}, {}) == doctest::Approx(7.0));
    CHECK(unitLoadUpperBound(items, {63000.0, 7500.0, 108.0}, {}) == doctest::Approx(7.5));
}

TEST_CASE("knapsack: the upper bound leaves out goods over the ceiling or over a limit alone") {
    vector<KnapsackItem> items(3, stackItem(48, 40, 1000));
    items[0].heightIn = 200.0;
    const double infinity = numeric_limits<double>::infinity();
    CHECK(unitLoadUpperBound(items, kTruck, {{{1.0, infinity, 1.0}, 10.0}}) == doctest::Approx(1.0));
}

TEST_CASE("knapsack: the upper bound rejects a negative or non-finite figure, item or limit") {
    const double nan = numeric_limits<double>::quiet_NaN();
    const double infinity = numeric_limits<double>::infinity();
    for (const double bad : {nan, infinity, -1.0}) {
        CHECK_THROWS_AS(unitLoadUpperBound({load(1920, 1000, bad)}, kTruck, {}), invalid_argument);
        CHECK_THROWS_AS(unitLoadUpperBound({load(bad, 1000)}, kTruck, {}), invalid_argument);
        CHECK_THROWS_AS(unitLoadUpperBound({load(1920, bad)}, kTruck, {}), invalid_argument);
        CHECK_THROWS_AS(unitLoadUpperBound({load(1920, 1000)}, {bad, 45000.0, 108.0}, {}), invalid_argument);
        CHECK_THROWS_AS(unitLoadUpperBound({load(1920, 1000)}, kTruck, {{{1.0}, bad}}), invalid_argument);
    }
    for (const double bad : {nan, -1.0}) {
        CHECK_THROWS_AS(unitLoadUpperBound({load(1920, 1000)}, kTruck, {{{bad}, 10.0}}), invalid_argument);
    }
}

TEST_CASE("knapsack: where carrying the heaviest copies costs a later truck, the lane keeps the lightest-first fill") {
    const vector<tuple<double, double, int>> stacks{{48, 3491, 1}, {60, 8823, 0}, {60, 2726, 0}, {48, 915, 0}, {60, 4553, 0},
                                                    {48, 7419, 1}, {60, 9073, 1}, {48, 1880, 0}, {48, 6728, 0}};
    vector<KnapsackItem> items;
    for (const auto& [longSide, weight, priority] : stacks) {
        items.push_back(stackItem(longSide, 40, weight, longSide == 60 ? 2.0 : 1.0));
        items.back().priority = priority;
    }
    // Rows that refuse some mixes of the two footprints, judging copies alike whatever they weigh.
    TruckLimits rows = stacksPerTruck(items.size(), 3.0);
    rows.fits = [&items](const vector<size_t>& chosen) {
        const long long wide = count_if(chosen.begin(), chosen.end(), [&](size_t index) { return items[index].levels == 2; });
        const long long narrow = static_cast<long long>(chosen.size()) - wide;
        return (wide * 7 + narrow * 3 + 2) % 5 != 0 ? TruckFit::Fits : TruckFit::DoesNotFit;
    };
    const LaneSelection lane = selectForTrucks(items, kTwoStacksOf10000Lb, kFine, 3, rows);
    vector<vector<size_t>> chosen;
    for (const KnapsackSelection& truck : lane.trucks) chosen.push_back(truck.chosen);
    CHECK(laneValue(items, chosen)[0] == 9);
}
