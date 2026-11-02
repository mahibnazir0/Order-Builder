#include "doctest.h"
#include "../m4AugustFixtures.hpp"
#include "binPacker.hpp"
#include "laneClassifier.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace ob;

namespace {

const KnapsackCapacity kTruck{63000.0, 45000.0, 108.0};
const BinPackerSettings kSettings{1.0, 1.0, 50, 100'000, 2'000, 400, 100'000};

KnapsackItem load(double area, double weight = 1000.0, double height = 60.0) {
    return {area, weight, height, 1.0, "2026-08-06", 0, false, ""};
}

// Eight trucks of one item each: a 63,000 sq in item fills a truck, a 40,000 sq in one leaves a
// third of it empty and no two share. Six are reserved, so two trucks must be dropped.
vector<KnapsackItem> eightTrucks(int fullPriority, int partialPriority) {
    vector<KnapsackItem> items(2, load(63000));
    for (KnapsackItem& item : items) item.priority = fullPriority;
    for (int index = 0; index < 6; ++index) {
        items.push_back(load(40000));
        items.back().priority = partialPriority;
    }
    return items;
}

// Every item on one truck or left over, no truck empty or over the floor or payload, no more
// trucks than reserved, and never fewer trucks than proven necessary.
void checkPacking(const vector<KnapsackItem>& items, const BinPacking& packing, int reserved,
                  const KnapsackCapacity& capacity = kTruck) {
    REQUIRE(packing.trucks.size() <= static_cast<size_t>(reserved));
    REQUIRE(packing.bestTrucks >= packing.lowerBound);
    REQUIRE(packing.bestTrucks <= packing.ffdTrucks);
    vector<int> seen(items.size(), 0);
    for (const BinTruck& truck : packing.trucks) {
        REQUIRE_FALSE(truck.items.empty());
        double area = 0.0, weight = 0.0;
        for (const size_t index : truck.items) {
            ++seen[index];
            area += items[index].areaSqIn;
            weight += items[index].weightLb;
        }
        REQUIRE(area <= capacity.floorAreaSqIn + kQuantityEpsilon);
        REQUIRE(weight <= capacity.weightLb + kQuantityEpsilon);
    }
    for (const size_t index : packing.leftOver) ++seen[index];
    REQUIRE(all_of(seen.begin(), seen.end(), [](int count) { return count == 1; }));
}

// The fewest trucks for every item, by trying each item in every truck so far or a new one.
size_t optimalTrucks(const vector<KnapsackItem>& items) {
    size_t best = items.size();
    vector<pair<double, double>> trucks;
    auto place = [&](auto&& self, size_t index) -> void {
        if (trucks.size() >= best) return;
        if (index == items.size()) {
            best = trucks.size();
            return;
        }
        // By position: deeper calls add trucks, which would invalidate a reference.
        for (size_t truck = 0; truck < trucks.size(); ++truck) {
            if (trucks[truck].first + items[index].areaSqIn <= kTruck.floorAreaSqIn
                && trucks[truck].second + items[index].weightLb <= kTruck.weightLb) {
                trucks[truck].first += items[index].areaSqIn;
                trucks[truck].second += items[index].weightLb;
                self(self, index + 1);
                trucks[truck].first -= items[index].areaSqIn;
                trucks[truck].second -= items[index].weightLb;
            }
        }
        trucks.emplace_back(items[index].areaSqIn, items[index].weightLb);
        self(self, index + 1);
        trucks.pop_back();
    };
    place(place, 0);
    return best;
}

vector<KnapsackItem> randomItems(mt19937& random, size_t count) {
    vector<KnapsackItem> items;
    for (size_t index = 0; index < count; ++index) {
        // Whole numbers, so 1 lb and 1 sq in grains round nothing and the oracle agrees.
        items.push_back(load(static_cast<double>(4000 + random() % 32000), static_cast<double>(500 + random() % 24000)));
    }
    return items;
}

// Floor sizes 9, 9, 7, 6, 5, 4 against a truck of 20: first-fit decreasing needs 3 trucks,
// 9 + 7 + 4 and 9 + 6 + 5 fill 2.
vector<KnapsackItem> beatsFirstFitDecreasing() {
    vector<KnapsackItem> items;
    for (const double share : {9.0, 9.0, 7.0, 6.0, 5.0, 4.0}) items.push_back(load(share * 3150.0, 100.0));
    return items;
}

} // anonymous namespace

TEST_CASE("binPacker: 65 stacks a truck holds 32 of are proven to need three trucks, with no search") {
    // 1.98 trucks of floor by volume; the LP agrees, so only counting whole stacks proves three.
    const vector<KnapsackItem> items(65, load(1920.0, 800.0));
    const BinPacking packing = packBins(items, kTruck, kSettings, 3);
    checkPacking(items, packing, 3);
    CHECK(packing.bestTrucks == 3);
    CHECK(packing.lowerBound == 3);
    CHECK(packing.proven);
    CHECK(packing.branchNodes == 0);
}

TEST_CASE("binPacker: three loads no two of which share the payload need three trucks, proven") {
    const vector<KnapsackItem> items(3, load(1920.0, 23000.0));
    const BinPacking packing = packBins(items, kTruck, kSettings, 3);
    checkPacking(items, packing, 3);
    CHECK(packing.lowerBound == 3);
    CHECK(packing.proven);
    CHECK(packFirstFitDecreasing(items, kTruck, kSettings, 3).lowerBound == 3);
}

TEST_CASE("binPacker: empty builds zero trucks") {
    const BinPacking packing = packBins({}, kTruck, kSettings, 3);
    CHECK(packing.trucks.empty());
    CHECK(packing.bestTrucks == 0);
    CHECK(packing.proven);
}

TEST_CASE("binPacker: one item is one truck") {
    const BinPacking packing = packBins({load(1920)}, kTruck, kSettings, 3);
    REQUIRE(packing.trucks.size() == 1);
    CHECK(packing.trucks[0].items == vector<size_t>{0});
    CHECK(packing.proven);
}

TEST_CASE("binPacker: an exact fill wastes no truck") {
    const vector<KnapsackItem> items(4, load(31500, 22500));
    const BinPacking packing = packBins(items, kTruck, kSettings, 5);
    checkPacking(items, packing, 5);
    CHECK(packing.bestTrucks == 2);
    CHECK(packing.proven);
}

TEST_CASE("binPacker: neverWorseThanFfd - it beats first-fit decreasing where FFD loses a truck") {
    const vector<KnapsackItem> items = beatsFirstFitDecreasing();
    const BinPacking ffd = packFirstFitDecreasing(items, kTruck, kSettings, 5);
    const BinPacking packing = packBins(items, kTruck, kSettings, 5);
    checkPacking(items, packing, 5);
    CHECK(ffd.bestTrucks == 3);
    CHECK(packing.ffdTrucks == 3);
    CHECK(packing.bestTrucks == 2);
    CHECK(packing.improvedOnWarmStart);
    CHECK(packing.proven);
    CHECK_FALSE(packing.fellBack);
}

TEST_CASE("binPacker: an LP stopped at its simplex iteration limit is reported") {
    const vector<KnapsackItem> items = beatsFirstFitDecreasing();
    BinPackerSettings capped = kSettings;
    capped.simplexIterationLimit = 1;
    const BinPacking packing = packBins(items, kTruck, capped, 5);
    checkPacking(items, packing, 5);
    CHECK(packing.lpIterationCapHit);
    CHECK_FALSE(packBins(items, kTruck, kSettings, 5).lpIterationCapHit);
}

TEST_CASE("binPacker: first-fit decreasing stays within 11/9 of the optimum plus 6/9") {
    // Each optimum is set by construction: the areas add up to exactly that many floors, and a
    // packing that fills them is written out beside each instance.
    const auto repeat = [](double area, size_t count) { return vector<KnapsackItem>(count, load(area)); };
    const auto concat = [](vector<vector<KnapsackItem>> parts) {
        vector<KnapsackItem> all;
        for (const auto& part : parts) all.insert(all.end(), part.begin(), part.end());
        return all;
    };
    struct Instance {
        vector<KnapsackItem> items;
        long long optimum;
    };
    const vector<Instance> instances{
        // Johnson's worst case at a 63,000 sq in floor, e = 100: six trucks of 31,600 + 15,850
        // + 15,550, and three of 15,950 x 2 + 15,550 x 2. First-fit decreasing needs 11.
        {concat({repeat(31600, 6), repeat(15950, 6), repeat(15850, 6), repeat(15550, 12)}), 9},
        // Five trucks of 35,000 + 28,000.
        {concat({repeat(35000, 5), repeat(28000, 5)}), 5},
        // Four trucks of 21,000 x 3.
        {repeat(21000, 12), 4},
    };
    for (const Instance& instance : instances) {
        CAPTURE(instance.optimum);
        const BinPacking ffd = packFirstFitDecreasing(instance.items, kTruck, kSettings, 100);
        CHECK(ffd.bestTrucks * 9 <= 11 * instance.optimum + 6);
        CHECK(ffd.bestTrucks >= instance.optimum);
    }
}

TEST_CASE("binPacker: a warm start that is already optimal is recorded as such, not as an improvement") {
    const vector<KnapsackItem> items(6, load(20000));
    const BinPacking packing = packBins(items, kTruck, kSettings, 5);
    CHECK(packing.bestTrucks == 2);
    CHECK(packing.ffdTrucks == 2);
    CHECK_FALSE(packing.improvedOnWarmStart);
    CHECK(packing.proven);
}

TEST_CASE("binPacker: terminatesOnUnfittable - set aside, named, no loop, no empty trucks") {
    const vector<KnapsackItem> items{load(1920), load(70000), load(1920, 46000), load(1920, 1000, 109.0)};
    // On its own thread with a deadline, so a hang fails the test instead of hanging the suite.
    auto result = make_shared<promise<BinPacking>>();
    future<BinPacking> done = result->get_future();
    thread([result, items] { result->set_value(packBins(items, kTruck, kSettings, 4)); }).detach();
    REQUIRE(done.wait_for(chrono::seconds(10)) == future_status::ready);
    const BinPacking packing = done.get();
    checkPacking(items, packing, 4);
    CHECK(packing.unfittable == vector<size_t>{1, 2, 3});
    CHECK(packing.leftOver == vector<size_t>{1, 2, 3});
    CHECK(packing.trucks.size() == 1);
}

TEST_CASE("binPacker: every item unfittable builds zero trucks, all named") {
    const vector<KnapsackItem> items{load(70000), load(1920, 46000)};
    const BinPacking packing = packBins(items, kTruck, kSettings, 4);
    CHECK(packing.trucks.empty());
    CHECK(packing.unfittable == vector<size_t>{0, 1});
}

TEST_CASE("binPacker: neverExceedsReservation - overflow is reported, never built") {
    const vector<KnapsackItem> items(6, load(31500));
    const BinPacking packing = packBins(items, kTruck, kSettings, 2);
    checkPacking(items, packing, 2);
    CHECK(packing.bestTrucks == 3);
    CHECK(packing.trucks.size() == 2);
    CHECK(packing.overReservation);
    CHECK(packing.leftOver.size() == 2);
}

TEST_CASE("binPacker: over the reservation the least urgent trucks are dropped, not the least full") {
    const vector<KnapsackItem> items = eightTrucks(11, 0);
    for (const bool search : {true, false}) {
        CAPTURE(search);
        const BinPacking packing = search ? packBins(items, kTruck, kSettings, 6)
                                          : packFirstFitDecreasing(items, kTruck, kSettings, 6);
        checkPacking(items, packing, 6);
        CHECK(packing.bestTrucks == 8);
        REQUIRE(packing.leftOver.size() == 2);
        for (const size_t index : packing.leftOver) CHECK(items[index].areaSqIn == 63000.0);
    }
}

TEST_CASE("binPacker: where fullness and urgency agree the least urgent are still the ones dropped") {
    const vector<KnapsackItem> items = eightTrucks(0, 11);
    for (const bool search : {true, false}) {
        CAPTURE(search);
        const BinPacking packing = search ? packBins(items, kTruck, kSettings, 6)
                                          : packFirstFitDecreasing(items, kTruck, kSettings, 6);
        checkPacking(items, packing, 6);
        REQUIRE(packing.leftOver.size() == 2);
        for (const size_t index : packing.leftOver) CHECK(items[index].priority == 11);
    }
}

TEST_CASE("binPacker: two segregated groups in one call are rejected") {
    const vector<KnapsackItem> items{load(1920), load(1920)};
    CHECK_THROWS_AS(packBins(items, kTruck, kSettings, 2, {0, 1}), invalid_argument);
    CHECK_THROWS_AS(packBins(items, kTruck, kSettings, 2, {0}), invalid_argument);
    CHECK(packBins(items, kTruck, kSettings, 2, {3, 3}).bestTrucks == 1);
}

TEST_CASE("binPacker: at the branch node limit the best found is kept, marked unproven") {
    mt19937 random(8112026);
    bool sawUnproven = false;
    for (int trial = 0; trial < 60 && !sawUnproven; ++trial) {
        const vector<KnapsackItem> items = randomItems(random, 14);
        const BinPacking packing = packBins(items, kTruck, {1.0, 1.0, 1, 100'000, 1, 400, 100'000}, 20);
        checkPacking(items, packing, 20);
        if (!packing.proven) {
            sawUnproven = true;
            CHECK(packing.branchNodes == 1);
        }
    }
    CHECK(sawUnproven);
}

TEST_CASE("binPacker: noTruckOverCapacity and everyItemExactlyOnce (property)") {
    mt19937 random(9112026);
    for (int trial = 0; trial < 150; ++trial) {
        const vector<KnapsackItem> items = randomItems(random, random() % 25);
        const int reserved = static_cast<int>(random() % 15);
        checkPacking(items, packBins(items, kTruck, kSettings, reserved), reserved);
    }
}

TEST_CASE("binPacker: proven packings match brute force, and none beats it (differential)") {
    mt19937 random(10112026);
    for (int trial = 0; trial < 120; ++trial) {
        const vector<KnapsackItem> items = randomItems(random, 1 + random() % 8);
        const BinPacking packing = packBins(items, kTruck, kSettings, 100);
        const long long optimal = static_cast<long long>(optimalTrucks(items));
        REQUIRE(packing.lowerBound <= optimal);
        REQUIRE(packing.bestTrucks >= optimal);
        if (packing.proven) REQUIRE(packing.bestTrucks == optimal);
    }
}

TEST_CASE("binPacker: shuffleSamePacking - shuffled input needs the same trucks when proven") {
    mt19937 random(11112026);
    for (int trial = 0; trial < 60; ++trial) {
        vector<KnapsackItem> items = randomItems(random, 10);
        const BinPacking original = packBins(items, kTruck, kSettings, 20);
        shuffle(items.begin(), items.end(), random);
        const BinPacking shuffled = packBins(items, kTruck, kSettings, 20);
        if (original.proven && shuffled.proven) REQUIRE(original.bestTrucks == shuffled.bestTrucks);
    }
}

TEST_CASE("binPacker: 10,000 items fall back to first-fit decreasing, predictably and named") {
    mt19937 random(12112026);
    const vector<KnapsackItem> items = randomItems(random, 10000);
    const auto start = chrono::steady_clock::now();
    const BinPacking packing = packBins(items, kTruck, kSettings, 10000);
    CHECK(chrono::duration<double>(chrono::steady_clock::now() - start).count() < 10.0);
    CHECK(packing.fellBack);
    CHECK(packing.bestTrucks == packing.ffdTrucks);
    checkPacking(items, packing, 10000);
}

TEST_CASE("binPacker: bad settings and reservations are rejected") {
    const double nan = numeric_limits<double>::quiet_NaN();
    const vector<KnapsackItem> items{load(1920)};
    CHECK_THROWS_AS(packBins(items, kTruck, {0.0, 1.0, 50, 1000, 100, 400, 100'000}, 1), invalid_argument);
    CHECK_THROWS_AS(packBins(items, kTruck, {1.0, nan, 50, 1000, 100, 400, 100'000}, 1), invalid_argument);
    CHECK_THROWS_AS(packBins(items, kTruck, {1.0, 1.0, 0, 1000, 100, 400, 100'000}, 1), invalid_argument);
    CHECK_THROWS_AS(packBins(items, kTruck, {1.0, 1.0, 50, 0, 100, 400, 100'000}, 1), invalid_argument);
    CHECK_THROWS_AS(packBins(items, kTruck, {1.0, 1.0, 50, 1000, 0, 400, 100'000}, 1), invalid_argument);
    CHECK_THROWS_AS(packBins(items, kTruck, {1.0, 1.0, 50, 1000, 100, 0, 100'000}, 1), invalid_argument);
    CHECK_THROWS_AS(packBins(items, kTruck, {1.0, 1.0, 50, 1000, 100, 400, 0}, 1), invalid_argument);
    CHECK_THROWS_AS(packBins(items, kTruck, {1.0, 1.0, 50, 1000, 100, 400, -1}, 1), invalid_argument);
    CHECK_THROWS_AS(packBins(items, kTruck, kSettings, -1), invalid_argument);
    CHECK_THROWS_AS(packBins({load(nan)}, kTruck, kSettings, 1), invalid_argument);
    CHECK_THROWS_AS(packFirstFitDecreasing(items, kTruck, kSettings, -1), invalid_argument);
}

TEST_CASE("binPacker: a grain or capacity counting past kMaxGrains is rejected, as knapsack rejects it") {
    const vector<KnapsackItem> items{load(1920), load(2400)};
    for (const double grain : {1e-12, 1e-300, numeric_limits<double>::denorm_min()}) {
        CAPTURE(grain);
        const BinPackerSettings fine{grain, grain, 50, 100'000, 2'000, 400, 100'000};
        CHECK_THROWS_AS(packBins(items, kTruck, fine, 2), invalid_argument);
        CHECK_THROWS_AS(packFirstFitDecreasing(items, kTruck, fine, 2), invalid_argument);
    }
    CHECK_THROWS_AS(packBins(items, {1e300, 45000.0, 108.0}, kSettings, 2), invalid_argument);
    CHECK_THROWS_AS(packFirstFitDecreasing(items, {63000.0, 1e300, 108.0}, kSettings, 2), invalid_argument);
}

TEST_CASE("binPacker: an item too large to count in grains is unfittable") {
    const BinPacking packing = packBins({load(1e300), load(1920)}, kTruck, kSettings, 2);
    CHECK(packing.unfittable == vector<size_t>{0});
    CHECK(packing.trucks.size() == 1);
}

TEST_CASE("binPacker: the shipped grain of 0.001 packs as 1 lb and 1 sq in grains do") {
    mt19937 random(2112026);
    const vector<KnapsackItem> items = randomItems(random, 14);
    const BinPacking shipped = packBins(items, kTruck, {0.001, 0.001, 50, 100'000, 2'000, 400, 100'000}, 20);
    checkPacking(items, shipped, 20);
    CHECK(shipped.bestTrucks == packBins(items, kTruck, kSettings, 20).bestTrucks);
}

TEST_CASE("binPacker: August binPacker lanes pack within capacity and reservation"
          * doctest::skip(!m4AugustTests::augustPresent())) {
    const BinPackerSettings settings{0.001, 0.001, 50, 100'000, 2'000, 400, 100'000};
    for (size_t runIndex = 0; runIndex < m4AugustTests::kAugustRunFiles.size(); ++runIndex) {
        CAPTURE(runIndex);
        const m4AugustTests::AugustItems& august = m4AugustTests::augustItems(runIndex);
        const LaneClassification classification =
            classifyLanes(august.run, august.book, august.built, {20, 5.0});
        const KnapsackCapacity capacity = capacityOf(august.run.trailer);
        for (const LaneRoute& route : classification.routes) {
            if (route.method != SolveMethod::BinPacker) continue;
            vector<KnapsackItem> items;
            for (const size_t index : route.itemIndices) items.push_back(knapsackItemFrom(august.built.items[index]));
            const int reserved = static_cast<int>(route.reservedTrucks);
            checkPacking(items, packBins(items, capacity, settings, reserved), reserved, capacity);
        }
    }
}
