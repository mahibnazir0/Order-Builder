#include "doctest.h"
#include "../m4AugustFixtures.hpp"
#include "laneClassifier.hpp"
#include "patternPacker.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;

namespace {

const KnapsackCapacity kTruck{63000.0, 45000.0, 108.0};
const PatternPackerSettings kSettings{1.0, 1.0, 200, 200'000};

KnapsackItem load(double area, double weight, const string& shipDate = "2026-08-06", int priority = 0,
                  double height = 60.0) {
    return {area, weight, height, 1.0, shipDate, priority};
}

vector<KnapsackItem> copies(const KnapsackItem& item, size_t count) { return vector<KnapsackItem>(count, item); }

vector<KnapsackItem> joined(vector<KnapsackItem> first, const vector<KnapsackItem>& second) {
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

// Every item on one truck or left over, no truck over the floor or payload, and no more trucks
// than reserved.
void checkPacking(const vector<KnapsackItem>& items, const PatternPacking& packing, int reserved,
                  const KnapsackCapacity& capacity = kTruck) {
    REQUIRE(packing.trucks.size() <= static_cast<size_t>(reserved));
    vector<int> seen(items.size(), 0);
    for (const PackedTruck& truck : packing.trucks) {
        REQUIRE_FALSE(truck.items.empty());
        double area = 0.0, weight = 0.0;
        for (const size_t index : truck.items) {
            ++seen[index];
            area += items[index].areaSqIn;
            weight += items[index].weightLb;
        }
        REQUIRE(area <= capacity.floorAreaSqIn + kQuantityEpsilon);
        REQUIRE(weight <= capacity.weightLb + kQuantityEpsilon);
        REQUIRE(truck.pattern < packing.patterns.size());
    }
    for (const size_t index : packing.leftOver) ++seen[index];
    REQUIRE(all_of(seen.begin(), seen.end(), [](int count) { return count == 1; }));
}

// The fewest trucks that carry every item, by trying each item in every truck so far or a new
// one. Small inputs only: the oracle for the bounds.
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
        items.push_back(load(static_cast<double>(5000 + random() % 30000), static_cast<double>(1000 + random() % 25000)));
    }
    return items;
}

} // anonymous namespace

TEST_CASE("patternPacker: singleProductOnePattern") {
    const vector<KnapsackItem> items = copies(load(1920, 1000), 64);
    const PatternPacking packing = packByPatterns(items, kTruck, kSettings, 3);
    checkPacking(items, packing, 3);
    CHECK(packing.trucks.size() == 2);
    CHECK(packing.patterns.size() == 1);
    CHECK(packing.converged);
    CHECK(packing.lpTrucks == 2);
    CHECK(packing.gapToLp == 0);
    CHECK(packing.provenOptimal);
}

TEST_CASE("patternPacker: two products in a fixed ratio need two or three patterns") {
    const vector<KnapsackItem> items = joined(copies(load(1920, 1000), 60), copies(load(2400, 600), 30));
    const PatternPacking packing = packByPatterns(items, kTruck, kSettings, 5);
    checkPacking(items, packing, 5);
    CHECK(packing.converged);
    CHECK(packing.patterns.size() <= 3);
    CHECK(packing.integerTrucks <= packing.lpTrucks + 1);
}

TEST_CASE("patternPacker: a fractional LP bound is rounded up and the gap reported") {
    // 32 fit a truck by floor, so 33 are 1.03 trucks: the bound is 2, and 2 are built.
    const vector<KnapsackItem> items = copies(load(1920, 1000), 33);
    const PatternPacking packing = packByPatterns(items, kTruck, kSettings, 2);
    CHECK(packing.lpValue == doctest::Approx(33.0 / 32.0));
    CHECK(packing.lpTrucks == 2);
    CHECK(packing.integerTrucks == 2);
    CHECK(packing.gapToLp == 0);
}

TEST_CASE("patternPacker: a weight-bound lane never builds a pattern over the payload") {
    const vector<KnapsackItem> items = copies(load(1920, 5000), 30);
    const PatternPacking packing = packByPatterns(items, kTruck, kSettings, 4);
    checkPacking(items, packing, 4);
    CHECK(packing.trucks.size() == 4);
    CHECK(packing.trucks[0].items.size() == 9);
    CHECK(packing.volumeBoundTrucks == 4);
}

TEST_CASE("patternPacker: noDuplicateColumns and no empty pattern") {
    mt19937 random(5112026);
    for (int trial = 0; trial < 50; ++trial) {
        const vector<KnapsackItem> items = randomItems(random, 3 + random() % 20);
        const PatternPacking packing = packByPatterns(items, kTruck, kSettings, 30);
        set<vector<int>> distinct(packing.patterns.begin(), packing.patterns.end());
        REQUIRE(distinct.size() == packing.patterns.size());
        for (const vector<int>& pattern : packing.patterns) {
            REQUIRE(any_of(pattern.begin(), pattern.end(), [](int count) { return count > 0; }));
        }
    }
}

TEST_CASE("patternPacker: everyPatternFeasible and every item once (property)") {
    mt19937 random(6112026);
    for (int trial = 0; trial < 200; ++trial) {
        const vector<KnapsackItem> items = randomItems(random, random() % 30);
        const int reserved = static_cast<int>(random() % 20);
        checkPacking(items, packByPatterns(items, kTruck, kSettings, reserved), reserved);
    }
}

TEST_CASE("patternPacker: lpBoundNeverAboveInteger and withinOneTruckOfOptimal (brute force)") {
    mt19937 random(7112026);
    for (int trial = 0; trial < 150; ++trial) {
        const vector<KnapsackItem> items = randomItems(random, 1 + random() % 8);
        const PatternPacking packing = packByPatterns(items, kTruck, kSettings, 100);
        const long long optimal = static_cast<long long>(optimalTrucks(items));
        REQUIRE(packing.converged);
        REQUIRE(packing.volumeBoundTrucks <= optimal);
        REQUIRE(packing.lpTrucks <= optimal);
        REQUIRE(packing.lpTrucks <= packing.integerTrucks);
        REQUIRE(packing.integerTrucks <= optimal + 1);
        if (packing.provenOptimal) REQUIRE(packing.integerTrucks == optimal);
    }
}

TEST_CASE("patternPacker: never more trucks than reserved - the least urgent goods are left") {
    vector<KnapsackItem> items;
    for (int index = 0; index < 64; ++index) items.push_back(load(1920, 1000, "2026-08-06", index < 32 ? 0 : 11));
    const PatternPacking packing = packByPatterns(items, kTruck, kSettings, 1);
    checkPacking(items, packing, 1);
    REQUIRE(packing.trucks.size() == 1);
    CHECK(packing.integerTrucks == 2);
    for (const size_t index : packing.trucks[0].items) CHECK(items[index].priority == 0);
    CHECK(packByPatterns(items, kTruck, kSettings, 0).leftOver.size() == 64);
}

TEST_CASE("patternPacker: items too tall or too large are left over, named") {
    const vector<KnapsackItem> items{load(1920, 1000), load(1920, 1000, "2026-08-06", 0, 110), load(70000, 1000)};
    const PatternPacking packing = packByPatterns(items, kTruck, kSettings, 2);
    CHECK(packing.leftOver == vector<size_t>{1, 2});
    CHECK(packing.trucks.size() == 1);
}

TEST_CASE("patternPacker: an empty lane builds nothing") {
    const PatternPacking packing = packByPatterns({}, kTruck, kSettings, 3);
    CHECK(packing.trucks.empty());
    CHECK(packing.leftOver.empty());
    CHECK(packing.integerTrucks == 0);
}

TEST_CASE("patternPacker: out of pattern budget it stops unconverged, still feasible") {
    // Mixing the two sizes beats either alone, so the first priced pattern is needed.
    const vector<KnapsackItem> items = joined(copies(load(40000, 1000), 4), copies(load(23000, 1000), 4));
    const PatternPacking stopped = packByPatterns(items, kTruck, {1.0, 1.0, 1, 200'000}, 10);
    const PatternPacking finished = packByPatterns(items, kTruck, kSettings, 10);
    CHECK(finished.converged);
    CHECK(finished.integerTrucks == 4);
    CHECK_FALSE(stopped.converged);
    checkPacking(items, stopped, 10);
    CHECK(stopped.patternsGenerated <= 3);
}

TEST_CASE("patternPacker: a 38-truck lane of 13 products packs within budget") {
    vector<KnapsackItem> items;
    for (int product = 0; product < 13; ++product) {
        const vector<KnapsackItem> ofProduct =
            copies(load(1800.0 + product * 40.0, 700.0 + product * 35.0, "2026-08-06", product % 3), 95);
        items.insert(items.end(), ofProduct.begin(), ofProduct.end());
    }
    const auto start = chrono::steady_clock::now();
    const PatternPacking packing = packByPatterns(items, kTruck, kSettings, 41);
    const double seconds = chrono::duration<double>(chrono::steady_clock::now() - start).count();
    CHECK(seconds < 10.0);
    checkPacking(items, packing, 41);
    CHECK(packing.leftOver.empty());
    CHECK(packing.integerTrucks >= packing.volumeBoundTrucks);
}

TEST_CASE("patternPacker: bad settings and reservations are rejected") {
    const double nan = numeric_limits<double>::quiet_NaN();
    const vector<KnapsackItem> items{load(1920, 1000)};
    CHECK_THROWS_AS(packByPatterns(items, kTruck, {0.0, 1.0, 10, 1000}, 1), invalid_argument);
    CHECK_THROWS_AS(packByPatterns(items, kTruck, {1.0, nan, 10, 1000}, 1), invalid_argument);
    CHECK_THROWS_AS(packByPatterns(items, kTruck, {1.0, 1.0, 0, 1000}, 1), invalid_argument);
    CHECK_THROWS_AS(packByPatterns(items, kTruck, {1.0, 1.0, 10, 0}, 1), invalid_argument);
    CHECK_THROWS_AS(packByPatterns(items, kTruck, kSettings, -1), invalid_argument);
    CHECK_THROWS_AS(packByPatterns({load(nan, 1000)}, kTruck, kSettings, 1), invalid_argument);
    CHECK_THROWS_AS(packByPatterns({load(1920, -5)}, kTruck, kSettings, 1), invalid_argument);
}

TEST_CASE("patternPacker: August patternPacker lanes pack within capacity and reservation"
          * doctest::skip(!m4AugustTests::augustPresent())) {
    const PatternPackerSettings settings{0.001, 0.001, 200, 200'000};
    for (size_t runIndex = 0; runIndex < m4AugustTests::kAugustRunFiles.size(); ++runIndex) {
        CAPTURE(runIndex);
        const m4AugustTests::AugustItems& august = m4AugustTests::augustItems(runIndex);
        const LaneClassification classification =
            classifyLanes(august.run, august.book, august.built, {20, 5.0});
        const KnapsackCapacity capacity = capacityOf(august.run.trailer);
        for (const LaneRoute& route : classification.routes) {
            if (route.method != SolveMethod::PatternPacker) continue;
            vector<KnapsackItem> items;
            for (const size_t index : route.itemIndices) items.push_back(knapsackItemFrom(august.built.items[index]));
            const int reserved = static_cast<int>(route.reservedTrucks);
            const PatternPacking packing = packByPatterns(items, capacity, settings, reserved);
            checkPacking(items, packing, reserved, capacity);
            if (packing.converged) REQUIRE(packing.lpTrucks <= packing.integerTrucks);
            CHECK(packing.provenOptimal);
        }
    }
}
