#include "doctest.h"
#include "m5Solver.hpp"
#include "support/m5Oracles.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

using namespace std;
using namespace ob::m5;

namespace {

ScalarModel modelOf(vector<long long> sizes, long long capacity = 10) {
    ScalarModel model;
    model.sizes = std::move(sizes);
    model.capacity = capacity;
    return model;
}

ScalarResult solveChecked(const ScalarModel& model) {
    const ScalarResult result = solveCuttingStock(model, ScalarLimits());
    REQUIRE(result.packing.has_value());
    CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
    return result;
}

}  // namespace

TEST_CASE("X-CG-01 the FFD trap: FFD needs 3, M5 finds 2" * doctest::test_suite("m5")) {
    const ScalarModel model = modelOf({6, 5, 3, 2, 2, 2});
    CHECK(scalarFirstFitDecreasing(model).bins.size() == 3);
    const ScalarResult result = solveChecked(model);
    CHECK(result.packing->bins.size() == 2);
    CHECK(result.ffdBins == 3);
    CHECK(result.evidence.proof == ProofStatus::ProvenOptimal);
}

TEST_CASE("X-CG-02 three 6s need 3 bins; the volume bound of 2 is not the answer" * doctest::test_suite("m5")) {
    const ScalarModel model = modelOf({6, 6, 6});
    CHECK(scalarLowerBound(model) == 3);
    CHECK(solveChecked(model).packing->bins.size() == 3);
}

TEST_CASE("X-CG-03 five 3s ship in 2 bins, exactly 5 items" * doctest::test_suite("m5")) {
    const ScalarResult result = solveChecked(modelOf({3, 3, 3, 3, 3}));
    CHECK(result.packing->bins.size() == 2);
}

TEST_CASE("X-CG-04 the mixed pattern (1,1) is found: one bin for 6 and 4" * doctest::test_suite("m5")) {
    const ScalarResult result = solveChecked(modelOf({6, 4}));
    CHECK(result.packing->bins.size() == 1);
}

TEST_CASE("X-CG-05 U-CG-08 pricing, on M4's shared PatternPricer, at duals 3/5 and 11/20 returns (1,1) at 23/20" * doctest::test_suite("m5")) {
    const PricedPattern priced = pricePattern({6, 4}, 10, {0.6, 0.55}, {1, 1}, 1000);
    CHECK(priced.exact);
    CHECK(priced.counts == vector<int>{1, 1});
    CHECK(priced.value == doctest::Approx(1.15));
    CHECK(1.0 - priced.value == doctest::Approx(-0.15));
}

TEST_CASE("X-CG-06 pricing stopped early is reported incomplete" * doctest::test_suite("m5")) {
    const PricedPattern priced = pricePattern({6, 4, 3, 2}, 10, {0.6, 0.55, 0.3, 0.2}, {5, 5, 5, 5}, 1);
    CHECK_FALSE(priced.exact);
}

TEST_CASE("X-CG-09 an item bigger than the bin is reported impossible, never packed" * doctest::test_suite("m5")) {
    const ScalarModel model = modelOf({11, 4});
    const ScalarResult result = solveChecked(model);
    CHECK(result.impossible == vector<size_t>{0});
    CHECK(result.packing->bins.size() == 1);
}

TEST_CASE("X-CG-10 no goods: zero bins, nothing divided by zero" * doctest::test_suite("m5")) {
    const ScalarResult result = solveChecked(modelOf({}));
    CHECK(result.packing->bins.empty());
    CHECK(result.evidence.lowerBound == 0);
}

TEST_CASE("X-CG-13 bounds are worked out per group, then added" * doctest::test_suite("m5")) {
    ScalarModel model = modelOf({6, 4});
    model.groupOf = {0, 1};
    CHECK(scalarLowerBound(model) == 2);
    CHECK(solveChecked(model).packing->bins.size() == 2);

    ScalarModel trailers = modelOf({6, 6, 4});
    trailers.groupOf = {0, 0, 1};
    trailers.capacityOfGroup = {12, 8};
    CHECK(scalarLowerBound(trailers) == 2);
    CHECK(solveChecked(trailers).packing->bins.size() == 2);
}

TEST_CASE("X-CG-14 a bound above the incumbent is never a proof" * doctest::test_suite("m5")) {
    const ScalarResult result = solveChecked(modelOf({6, 5, 3, 2, 2, 2}));
    REQUIRE(result.evidence.lowerBound.has_value());
    CHECK(*result.evidence.lowerBound <= *result.evidence.incumbent);
    CHECK(gapText(2, nullopt) == "N/A (no valid bound)");
}

TEST_CASE("X-CG-16 invalid columns are rejected before use" * doctest::test_suite("m5")) {
    const vector<long long> sizes{6, 4};
    CHECK(validColumn({1, 1}, sizes, 10));
    CHECK_FALSE(validColumn({-1, 2}, sizes, 10));
    CHECK_FALSE(validColumn({1, 1, 1}, sizes, 10));
    CHECK_FALSE(validColumn({2, 0}, sizes, 10));
    CHECK_FALSE(validColumn({0, 0}, sizes, 10));
}

TEST_CASE("X-CG-17 the gap names trucks and its denominator" * doctest::test_suite("m5")) {
    CHECK(gapText(12, 10) == "2 trucks, 20.0% above the bound of 10");
    CHECK(gapText(10, 10) == "0 trucks, 0.0% above the bound of 10");
    CHECK(gapText(3, 0) == "N/A (no valid bound)");
}

TEST_CASE("X-CG-18 converged LP pricing does not prove the restricted integer best" * doctest::test_suite("m5")) {
    const ScalarModel model = modelOf({5, 5, 5, 2, 2, 2, 2, 2, 2});
    const ScalarResult result = solveChecked(model);
    const SolverEvidence& evidence = result.evidence;
    CHECK(result.packing->bins.size() == 3);
    REQUIRE(evidence.lpValue.has_value());
    CHECK(*evidence.lpValue == doctest::Approx(2.7));
    CHECK(evidence.lpStatus == "converged");
    REQUIRE(evidence.restrictedMasterInteger.has_value());
    CHECK(*evidence.restrictedMasterInteger == 4);
    CHECK(evidence.lowerBound == 3);
    CHECK(evidence.incumbent == 3);
    CHECK(evidence.proof == ProofStatus::ProvenOptimal);
    CHECK(m5Oracles::bruteForceBins(model.sizes, model.capacity) == 3);
}

TEST_CASE("X-CG-19 a capacity bound does not certify an accepted all-goods packing" * doctest::test_suite("m5")) {
    AcceptedModel model;
    model.cube = {6, 2, 2, 2};
    model.weight = {1, 1, 1, 1};
    model.cubeCapacity = 10;
    model.weightCapacity = 10;
    model.xWeight = 10;
    model.yCube = 6;
    model.bins = 4;
    const AcceptedResult result = solveAccepted(model);
    CHECK(result.allGoods == ProofStatus::ProvenInfeasible);
    CHECK_FALSE(result.allGoodsPacking.has_value());
    CHECK(result.capacityBound == 2);
    CHECK(result.partialCube == doctest::Approx(10.0));
    REQUIRE(result.partial.bins.size() == 1);
    CHECK(result.partial.bins.front().size() == 3);
}

TEST_CASE("X-REB-04 no scalar truck passes the policy: zero dispatched, all goods kept" * doctest::test_suite("m5")) {
    AcceptedModel model;
    model.cube = {8, 8, 8};
    model.weight = {1, 1, 1};
    model.cubeCapacity = 10;
    model.weightCapacity = 10;
    model.xWeight = 10;
    model.yCube = 8.4;
    model.bins = 3;
    const AcceptedResult result = solveAccepted(model);
    CHECK(result.allGoods == ProofStatus::ProvenInfeasible);
    CHECK(result.partial.bins.empty());
    CHECK(result.partialCube == 0.0);
}

TEST_CASE("ACC-3 U-FF-01 U-FF-02 U-FF-03 FX-GAP escalates on F > B with allowance left" * doctest::test_suite("m5")) {
    CHECK(shouldEscalate(11, 10, 1, true));
    CHECK(shouldEscalate(12, 10, 1, true));
    CHECK_FALSE(shouldEscalate(12, 10, 1, false));
    CHECK_FALSE(shouldEscalate(10, 10, 1, true));
}

TEST_CASE("X-META-01 every multiset of up to 7 sizes 1-6 at capacity 6 matches brute force" * doctest::test_suite("m5")) {
    size_t multisets = 0;
    vector<long long> sizes;
    function<void(long long)> extend = [&](long long smallest) {
        ++multisets;
        const ScalarModel model = modelOf(sizes, 6);
        const ScalarResult result = solveCuttingStock(model, ScalarLimits());
        REQUIRE(result.packing.has_value());
        REQUIRE(m5Oracles::packingValid(model, *result.packing, result.impossible));
        REQUIRE(static_cast<long long>(result.packing->bins.size()) == m5Oracles::bruteForceBins(sizes, 6));
        if (sizes.size() == 7) return;
        for (long long size = smallest; size <= 6; ++size) {
            sizes.push_back(size);
            extend(size);
            sizes.pop_back();
        }
    };
    extend(1);
    CHECK(multisets == 1716);
}

TEST_CASE("X-CG-15 seeded random small cases match brute force" * doctest::test_suite("m5")) {
    mt19937 generator(20261113);
    for (int round = 0; round < 300; ++round) {
        const long long capacity = uniform_int_distribution<long long>(5, 20)(generator);
        const size_t count = uniform_int_distribution<size_t>(0, 10)(generator);
        vector<long long> sizes;
        for (size_t item = 0; item < count; ++item) sizes.push_back(uniform_int_distribution<long long>(1, capacity)(generator));
        const ScalarModel model = modelOf(sizes, capacity);
        const ScalarResult result = solveCuttingStock(model, ScalarLimits());
        INFO("round " << round);
        REQUIRE(result.packing.has_value());
        REQUIRE(m5Oracles::packingValid(model, *result.packing, result.impossible));
        REQUIRE(static_cast<long long>(result.packing->bins.size()) == m5Oracles::bruteForceBins(sizes, capacity));
    }
}

TEST_CASE("X-META-02 scaling sizes and capacity keeps the best bin count" * doctest::test_suite("m5")) {
    const vector<long long> sizes{6, 5, 3, 2, 2, 2};
    for (const long long scale : {1LL, 10LL, 100LL}) {
        vector<long long> scaled;
        for (const long long size : sizes) scaled.push_back(size * scale);
        CHECK(solveChecked(modelOf(scaled, 10 * scale)).packing->bins.size() == 2);
    }
}

TEST_CASE("X-META-08 a do-not-mix rule never lowers the exact best" * doctest::test_suite("m5")) {
    ScalarModel mixed = modelOf({6, 4, 6, 4});
    ScalarModel apart = mixed;
    apart.groupOf = {0, 1, 0, 1};
    CHECK(solveChecked(apart).packing->bins.size() >= solveChecked(mixed).packing->bins.size());
}

// PUB-01..03 need OR-Library's binpack1.txt in tests/m5/data/orlib/. It is skipped in the full run so a
// clone without it still passes; the m5Published ctest entry runs it with --no-skip, where a missing
// file fails by name. Neither a skip nor a missing file is a pass.
TEST_CASE("ACC-1 U-CG-02 PUB-01 PUB-02 PUB-03 Falkenauer u120_00..02 reach their proven optima" * doctest::test_suite("m5Published")
          * doctest::skip()) {
    const string path = "tests/m5/data/orlib/binpack1.txt";
    ifstream file(path);
    REQUIRE_MESSAGE(file.good(), "missing " << path << ": PUB-01..03 are BLOCKED until it is added");
    int problems = 0;
    file >> problems;
    const vector<pair<string, long long>> expected{{"u120_00", 48}, {"u120_01", 49}, {"u120_02", 46}};
    for (const pair<string, long long>& instance : expected) {
        const string& name = instance.first;
        const long long optimum = instance.second;
        string id;
        long long capacity = 0, count = 0, best = 0;
        file >> id >> capacity >> count >> best;
        REQUIRE(id == name);
        REQUIRE(best == optimum);
        ScalarModel model = modelOf({}, capacity);
        for (long long item = 0; item < count; ++item) {
            long long size = 0;
            file >> size;
            model.sizes.push_back(size);
        }
        REQUIRE(model.sizes.size() == 120);
        const ScalarResult result = solveChecked(model);
        INFO(name);
        CHECK(static_cast<long long>(result.packing->bins.size()) == optimum);
    }
}
