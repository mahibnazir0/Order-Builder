#include "doctest.h"
#include "columnGeneration.hpp"
#include "m5Solver.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Oracles.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
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

size_t packedCount(const ScalarPacking& packing) {
    size_t items = 0;
    for (const vector<size_t>& bin : packing.bins) items += bin.size();
    return items;
}

ScalarResult solveChecked(const ScalarModel& model) {
    const ScalarResult result = solveCuttingStock(model, ScalarLimits());
    REQUIRE(result.packing.has_value());
    CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
    return result;
}

}  // namespace

TEST_CASE("X-CG-01/synthetic: the FFD trap [6,5,3,2,2,2] on 10: FFD uses 3, M5 finds 2, every item kept, gap 1 truck" * doctest::test_suite("m5")) {
    const ScalarModel model = modelOf({6, 5, 3, 2, 2, 2});
    CHECK(scalarFirstFitDecreasing(model).bins.size() == 3);
    const ScalarResult result = solveChecked(model);
    CHECK(result.packing->bins.size() == 2);
    CHECK(packedCount(*result.packing) == 6);
    CHECK(result.impossible.empty());
    CHECK(result.ffdBins == 3);
    CHECK(result.ffdBins - static_cast<long long>(result.packing->bins.size()) == 1);
    CHECK(m5Oracles::bruteForceBins(model.sizes, 10) == 2);
    CHECK(result.evidence.incumbent == 2);
    CHECK(result.evidence.lowerBound == 2);
    CHECK(result.evidence.proof == ProofStatus::ProvenOptimal);
    m5Evidence::note("expected", {"FFD 3", "M5 2, all 6 items", "gap 1 truck", "proven by bound 2"});
}

TEST_CASE("X-CG-02/synthetic: three 6s on 10 need 3; the volume bound of 2 is never the plan or a zero gap" * doctest::test_suite("m5")) {
    const ScalarModel model = modelOf({6, 6, 6});
    CHECK((18 + 10 - 1) / 10 == 2);
    CHECK(scalarLowerBound(model) == 3);
    const ScalarResult result = solveChecked(model);
    CHECK(result.packing->bins.size() == 3);
    CHECK(packedCount(*result.packing) == 3);
    CHECK(result.evidence.lowerBound == 3);
    CHECK(result.evidence.incumbent == 3);
    CHECK(result.evidence.proof == ProofStatus::ProvenOptimal);
    CHECK(gapText(3, 2) == "1 truck, 50.0% above the bound of 2");
    m5Evidence::note("expected", {"3 bins", "volume bound 2 is not used; bound 3 (items over half)"});
}

TEST_CASE("X-CG-03/synthetic: five 3s on 10 ship in 2 bins, exactly 5 items; the relaxation alone is not the proof" * doctest::test_suite("m5")) {
    const ScalarModel model = modelOf({3, 3, 3, 3, 3});
    const ScalarResult result = solveChecked(model);
    CHECK(result.packing->bins.size() == 2);
    CHECK(packedCount(*result.packing) == 5);
    // A bin holds at most three 3s, so the LP over legal patterns is 5 / 3: below 2, not itself a proof.
    REQUIRE(result.evidence.lpValue.has_value());
    CHECK(*result.evidence.lpValue == doctest::Approx(5.0 / 3.0));
    // The proof is the integer bound of the same model meeting the checked packing.
    CHECK(result.evidence.lowerBound == 2);
    CHECK(result.evidence.incumbent == 2);
    CHECK(result.evidence.proof == ProofStatus::ProvenOptimal);
    CHECK(result.evidence.proofModel == result.evidence.boundModel);
    CHECK(m5Oracles::bruteForceBins(model.sizes, 10) == 2);
    m5Evidence::note("expected", {"2 bins, 5 items", "LP 5/3; proof from integer bound 2"});
}

TEST_CASE("X-CG-04/synthetic: sizes 6 and 4, LP started on (1,0) and (0,2) at 1.5: pricing finds (1,1) and the answer is 1, one of each shipped" * doctest::test_suite("m5")) {
    // The restricted LP exactly as written, on the covering LP M5's solver uses (src/columnGeneration).
    const ob::CoveringLpSolution restricted = ob::solveCoveringLp({{1, 0}, {0, 2}}, {1, 1}, 1000);
    REQUIRE(restricted.solved);
    CHECK(restricted.value == doctest::Approx(1.5));
    REQUIRE(restricted.patternUses.size() == 2);
    CHECK(restricted.patternUses[0] == doctest::Approx(1.0));
    CHECK(restricted.patternUses[1] == doctest::Approx(0.5));
    REQUIRE(restricted.typeValues.size() == 2);
    const PricedPattern mixed = pricePattern({6, 4}, 10, restricted.typeValues, {1, 1}, 1000);
    CHECK(mixed.exact);
    CHECK(mixed.counts == vector<int>{1, 1});
    CHECK(1.0 - mixed.value < 0.0);
    const ob::CoveringLpSolution withMixed = ob::solveCoveringLp({{1, 0}, {0, 2}, {1, 1}}, {1, 1}, 1000);
    REQUIRE(withMixed.solved);
    CHECK(withMixed.value == doctest::Approx(1.0));
    // The solver itself (its own start is demand-limited, (1,0) and (0,1)) reaches the same answer.
    const ScalarModel model = modelOf({6, 4});
    vector<vector<double>> dualsSeen;
    ScalarLimits limits;
    limits.pricing = [&](const vector<long long>& sizes, long long capacity, const vector<double>& duals, const vector<long long>& available) {
        dualsSeen.push_back(duals);
        return pricePattern(sizes, capacity, duals, available, 1000);
    };
    const ScalarResult result = solveCuttingStock(model, limits);
    REQUIRE(result.packing.has_value());
    CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
    REQUIRE_FALSE(dualsSeen.empty());
    CHECK(result.evidence.columnsGenerated == 3);
    REQUIRE(result.evidence.lpValue.has_value());
    CHECK(*result.evidence.lpValue == doctest::Approx(1.0));
    REQUIRE(result.packing->bins.size() == 1);
    CHECK(result.packing->bins.front().size() == 2);
    CHECK(result.evidence.lowerBound == 1);
    CHECK(result.evidence.proof == ProofStatus::ProvenOptimal);
    m5Evidence::note("expected", {"restricted LP 1.5 (duals 1, 0.5)", "(1,1) enters", "1 bin with one 6 and one 4", "bound 1, never 2"});
}

TEST_CASE("X-CG-05/synthetic: U-CG-08 pricing, on M4's shared PatternPricer, at duals 3/5 and 11/20 returns (1,1) at 23/20" * doctest::test_suite("m5")) {
    const PricedPattern priced = pricePattern({6, 4}, 10, {0.6, 0.55}, {1, 1}, 1000);
    CHECK(priced.exact);
    CHECK(priced.counts == vector<int>{1, 1});
    CHECK(priced.value == doctest::Approx(1.15));
    CHECK(1.0 - priced.value == doctest::Approx(-0.15));
}

TEST_CASE("X-CG-06/synthetic: pricing stopped early on every call is reported incomplete; a proof needs a separate equal bound" * doctest::test_suite("m5")) {
    const PricedPattern priced = pricePattern({6, 4, 3, 2}, 10, {0.6, 0.55, 0.3, 0.2}, {5, 5, 5, 5}, 1);
    CHECK_FALSE(priced.exact);
    const vector<vector<long long>> models{{6, 4}, {6, 5, 3, 2, 2, 2}, {6, 6, 6}};
    for (const vector<long long>& sizes : models) {
        INFO("items " << sizes.size());
        const ScalarModel model = modelOf(sizes);
        ScalarLimits limits;
        limits.pricing = [](const vector<long long>& types, long long capacity, const vector<double>& duals, const vector<long long>& available) {
            PricedPattern partial = pricePattern(types, capacity, duals, available, 1000);
            partial.exact = false;
            return partial;
        };
        const ScalarResult result = solveCuttingStock(model, limits);
        REQUIRE(result.packing.has_value());
        CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
        CHECK(result.evidence.lpStatus == "pricingIncomplete");
        const long long bins = static_cast<long long>(result.packing->bins.size());
        CHECK(bins == m5Oracles::bruteForceBins(model.sizes, 10));
        if (result.evidence.proof == ProofStatus::ProvenOptimal) {
            CHECK(result.evidence.lowerBound == bins);
            CHECK(result.evidence.incumbent == bins);
            CHECK(result.evidence.boundModel == result.evidence.proofModel);
            CHECK(scalarLowerBound(model) == bins);
        }
    }
    m5Evidence::note("expected", {"lpStatus pricingIncomplete", "proof only where the separate bound equals the checked result"});
}

TEST_CASE("X-CG-09/synthetic: one item of 11 on 10 is reported impossible and counted; no illegal pattern, no empty rescue truck" * doctest::test_suite("m5")) {
    const ScalarModel model = modelOf({11, 4});
    const ScalarResult result = solveChecked(model);
    CHECK(result.impossible == vector<size_t>{0});
    CHECK(result.packing->bins.size() == 1);
    for (const vector<size_t>& bin : result.packing->bins) {
        CHECK_FALSE(bin.empty());
        CHECK(find(bin.begin(), bin.end(), size_t{0}) == bin.end());
    }
    const ScalarResult alone = solveChecked(modelOf({11}));
    CHECK(alone.impossible == vector<size_t>{0});
    CHECK(alone.packing->bins.empty());
    CHECK_FALSE(validColumn({1}, {11}, 10));
    m5Evidence::note("expected", {"item 11 impossible, counted once", "the 4 ships in 1 bin", "alone: 0 bins"});
}

TEST_CASE("X-CG-10 no goods: zero bins, nothing divided by zero" * doctest::test_suite("m5")) {
    const ScalarResult result = solveChecked(modelOf({}));
    CHECK(result.packing->bins.empty());
    CHECK(result.evidence.lowerBound == 0);
}

TEST_CASE("X-CG-13/synthetic: 6 in A and 4 in B, no mixing: 2 though volume says 1; A [6,6] on 12 and B [4] on 8: bound 2, per group then added" * doctest::test_suite("m5")) {
    ScalarModel model = modelOf({6, 4});
    model.groupOf = {0, 1};
    CHECK((6 + 4 + 9) / 10 == 1);
    CHECK(scalarLowerBound(model) == 2);
    const ScalarResult first = solveChecked(model);
    CHECK(first.packing->bins.size() == 2);
    for (const vector<size_t>& bin : first.packing->bins) {
        for (const size_t item : bin) CHECK(model.groupOf[item] == model.groupOf[bin.front()]);
    }
    ScalarModel trailers = modelOf({6, 6, 4});
    trailers.groupOf = {0, 0, 1};
    trailers.capacityOfGroup = {12, 8};
    // Per group: A ceil(12 / 12) = 1, B ceil(4 / 8) = 1, added 2.
    CHECK(scalarLowerBound(trailers) == 2);
    const ScalarResult second = solveChecked(trailers);
    CHECK(second.packing->bins.size() == 2);
    CHECK(second.evidence.lowerBound == 2);
    CHECK(second.evidence.proof == ProofStatus::ProvenOptimal);
    for (const vector<size_t>& bin : second.packing->bins) {
        for (const size_t item : bin) CHECK(trailers.groupOf[item] == trailers.groupOf[bin.front()]);
    }
    m5Evidence::note("expected", {"first: 2 bins, bound 2 (volume 1)", "second: bound 1 + 1 = 2, 2 bins"});
}

TEST_CASE("X-CG-14 a bound above the incumbent is never a proof" * doctest::test_suite("m5")) {
    const ScalarResult result = solveChecked(modelOf({6, 5, 3, 2, 2, 2}));
    REQUIRE(result.evidence.lowerBound.has_value());
    CHECK(*result.evidence.lowerBound <= *result.evidence.incumbent);
    CHECK(gapText(2, nullopt) == "N/A (no valid bound)");
}

TEST_CASE("X-CG-16/synthetic: a column with a negative count, an unknown item or over capacity is rejected before use" * doctest::test_suite("m5")) {
    const vector<long long> sizes{6, 4};
    CHECK(validColumn({1, 1}, sizes, 10));
    CHECK_FALSE(validColumn({-1, 2}, sizes, 10));
    CHECK_FALSE(validColumn({1, 1, 1}, sizes, 10));
    CHECK_FALSE(validColumn({2, 0}, sizes, 10));
    CHECK_FALSE(validColumn({0, 0}, sizes, 10));
    // Counts are whole numbers by type (vector<int>), so a fractional count cannot be expressed. A priced
    // column that breaks the rules never enters the master; the result stays checked.
    const vector<vector<int>> badColumns{{-1, 2}, {1, 1, 1}, {2, 0}};
    for (const vector<int>& bad : badColumns) {
        ScalarLimits limits;
        limits.pricing = [&](const vector<long long>&, long long, const vector<double>&, const vector<long long>&) {
            return PricedPattern{bad, 5.0, true};
        };
        const ScalarModel model = modelOf({6, 4});
        const ScalarResult result = solveCuttingStock(model, limits);
        REQUIRE(result.packing.has_value());
        CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
        CHECK(result.evidence.columnsGenerated == 2);
    }
    m5Evidence::note("expected", {"(1,1) valid", "(-1,2), (1,1,1), (2,0), (0,0) rejected", "bad priced columns never enter"});
}

TEST_CASE("X-CG-17/synthetic: result 12 on bound 10: gap 2 trucks, 20.0% above the bound (one contract); escalation by F > B with allowance; no bound prints N/A" * doctest::test_suite("m5")) {
    CHECK(gapText(12, 10) == "2 trucks, 20.0% above the bound of 10");
    // The recorded contract's denominator is the bound: (12 - 10) / 10, not (12 - 10) / 12 = 16.7%.
    CHECK(100.0 * (12 - 10) / 10 == doctest::Approx(20.0));
    CHECK(gapText(12, 10).find("16.") == string::npos);
    CHECK(gapText(10, 10) == "0 trucks, 0.0% above the bound of 10");
    CHECK(gapText(3, 0) == "N/A (no valid bound)");
    CHECK(gapText(3, nullopt) == "N/A (no valid bound)");
    CHECK(shouldEscalate(12, 10, 1, true));
    CHECK_FALSE(shouldEscalate(12, 10, 1, false));
    CHECK_FALSE(shouldEscalate(10, 10, 1, true));
    m5Evidence::note("expected", {"2 trucks, 20.0% above the bound of 10", "escalate only with allowance left", "N/A for bound 0 or none"});
}

TEST_CASE("X-CG-18/synthetic: three 5s and six 2s on 10: LP 2.7 converged, single-product integer 4, final 3 bins proven by bound 3" * doctest::test_suite("m5")) {
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
    CHECK(packedCount(*result.packing) == 9);
    CHECK(evidence.boundModel == evidence.proofModel);
    m5Evidence::note("expected", {"LP 2.7 converged", "restricted integer 4", "final 3 bins, all 9 items", "bound 3, proven"});
}

namespace {

AcceptedModel xcg19Model() {
    AcceptedModel model;
    model.cube = {6, 2, 2, 2};
    model.weight = {1, 1, 1, 1};
    model.cubeCapacity = 10;
    model.weightCapacity = 10;
    model.xWeight = 10;
    model.yCube = 6;
    model.bins = 4;
    return model;
}

}  // namespace

TEST_CASE("X-CG-19/allGoodsFeasibility: cubes [6,2,2,2] on 10 with y = 6 strict: the capacity bound 2 certifies nothing; all goods proven infeasible" * doctest::test_suite("m5")) {
    const AcceptedModel model = xcg19Model();
    const AcceptedResult result = solveAccepted(model);
    // By hand: weight never passes x = 10, so an accepted bin needs cube > 6. With every item placed, the bin
    // holding the 6 can take at most two 2s; any bin without the 6 holds at most 6. So no all-goods packing.
    CHECK(result.capacityBound == 2);
    CHECK(result.allGoods == ProofStatus::ProvenInfeasible);
    CHECK_FALSE(result.allGoodsPacking.has_value());
    m5Evidence::note("expected", {"capacity bound 2, reported as a relaxation only", "all goods: proven infeasible"});
}

TEST_CASE("X-CG-19/partialService: the most cube in accepted bins is one bin [6,2,2] = 10, labelled partial" * doctest::test_suite("m5")) {
    const AcceptedModel model = xcg19Model();
    const AcceptedResult result = solveAccepted(model);
    CHECK(result.allGoods == ProofStatus::ProvenInfeasible);
    REQUIRE(result.partial.bins.size() == 1);
    const vector<size_t>& bin = result.partial.bins.front();
    CHECK(bin.size() == 3);
    double cube = 0.0, weight = 0.0;
    for (const size_t item : bin) {
        cube += model.cube[item];
        weight += model.weight[item];
    }
    CHECK(cube == doctest::Approx(10.0));
    CHECK(cube > model.yCube);
    CHECK(weight <= model.weightCapacity);
    CHECK(find(bin.begin(), bin.end(), size_t{0}) != bin.end());
    CHECK(result.partialCube == doctest::Approx(10.0));
    m5Evidence::note("expected", {"partial: one bin [6,2,2], cube 10 > 6", "one 2 left"});
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

TEST_CASE("X-REB-04/synthetic: three cube-8 items, V = W = 10, x = 10, y = 8.4: no truck passes, zero dispatched, all kept" * doctest::test_suite("m5")) {
    AcceptedModel model;
    model.cube = {8, 8, 8};
    model.weight = {1, 1, 1};
    model.cubeCapacity = 10;
    model.weightCapacity = 10;
    model.xWeight = 10;
    model.yCube = 8.4;
    model.bins = 3;
    // By hand: two items need cube 16 > 10, so a truck holds one: cube 8 <= 8.4 and weight 1 <= 10, never accepted.
    CHECK(8 + 8 > 10);
    CHECK_FALSE(8.0 > 8.4);
    CHECK_FALSE(1.0 > 10.0);
    const AcceptedResult result = solveAccepted(model);
    CHECK(result.allGoods == ProofStatus::ProvenInfeasible);
    CHECK_FALSE(result.allGoodsPacking.has_value());
    CHECK(result.partial.bins.empty());
    CHECK(result.partialCube == 0.0);
    m5Evidence::note("expected", {"no truck passes (8 <= 8.4, 1 <= 10)", "zero dispatched, 3 items kept", "all-goods proven infeasible"});
}

TEST_CASE("X-REB-04/productionProfile: acceptance OFF: all three scalar bins ship, every item once; no physical labels invented" * doctest::test_suite("m5")) {
    const ScalarModel model = modelOf({8, 8, 8});
    const ScalarResult result = solveCuttingStock(model, ScalarLimits());
    REQUIRE(result.packing);
    CHECK(result.packing->bins.size() == 3);
    CHECK(m5Oracles::packingValid(model, *result.packing, {}));
    CHECK(m5Oracles::bruteForceBins({8, 8, 8}, 10) == 3);
    // The scalar model has no trailer layout: its result carries bins only, no fullness, rear inches or bucket.
    m5Evidence::note("expected", {"3 bins of one item each", "items conserved", "physical labels NOT_APPLICABLE (scalar model)"});
}

namespace {

// FX-GAP on a scalar model: F from first-fit decreasing, B from the scalar bound, which the brute-force
// optimum must not undercut (a valid bound of the same model and goods).
struct GapCase {
    long long ffd = 0, bound = 0, optimum = 0;
};

GapCase gapCaseOf(const vector<long long>& sizes) {
    const ScalarModel model = modelOf(sizes);
    const ScalarPacking ffd = scalarFirstFitDecreasing(model);
    CHECK(m5Oracles::packingValid(model, ffd, {}));
    GapCase gap{static_cast<long long>(ffd.bins.size()), scalarLowerBound(model), m5Oracles::bruteForceBins(sizes, 10)};
    CHECK(gap.bound <= gap.optimum);
    CHECK(gap.optimum <= gap.ffd);
    return gap;
}

}  // namespace

TEST_CASE("U-FF-01/fxGap: F - B exactly G = 1 with allowance left escalates; the bound is valid and recorded" * doctest::test_suite("m5")) {
    const GapCase gap = gapCaseOf({6, 5, 3, 2, 2, 2});
    CHECK(gap.ffd == 3);
    CHECK(gap.bound == 2);
    CHECK(gap.optimum == 2);
    CHECK(shouldEscalate(gap.ffd, gap.bound, 1, true));
    CHECK(gapText(gap.ffd, gap.bound) == "1 truck, 50.0% above the bound of 2");
    m5Evidence::note("expected", {{"F", 3}, {"B", 2}, {"G", 1}, {"escalate", true}});
}

TEST_CASE("U-FF-02/fxGap: F - B = G + 1 escalates with allowance left and not without" * doctest::test_suite("m5")) {
    const GapCase gap = gapCaseOf({7, 5, 7, 4, 2, 1, 7, 7, 4, 7, 8});
    CHECK(gap.ffd == 8);
    CHECK(gap.bound == 6);
    CHECK(gap.optimum == 8);
    CHECK(shouldEscalate(gap.ffd, gap.bound, 1, true));
    CHECK_FALSE(shouldEscalate(gap.ffd, gap.bound, 1, false));
    CHECK(shouldEscalate(12, 10, 1, true));
    CHECK_FALSE(shouldEscalate(12, 10, 1, false));
    CHECK(gapText(gap.ffd, gap.bound) == "2 trucks, 33.3% above the bound of 6");
    m5Evidence::note("expected", {{"F", 8}, {"B", 6}, {"escalate with allowance", true}, {"escalate without", false}});
}

TEST_CASE("U-FF-03/fxGap: F equal to a valid bound never escalates; a rounded figure is not a bound" * doctest::test_suite("m5")) {
    const GapCase gap = gapCaseOf({5, 5, 5, 5});
    CHECK(gap.ffd == 2);
    CHECK(gap.bound == 2);
    CHECK_FALSE(shouldEscalate(gap.ffd, gap.bound, 1, true));
    CHECK(gapText(gap.ffd, gap.bound) == "0 trucks, 0.0% above the bound of 2");
    // {6,6,6}: the area figure 1.8 rounds to 2, yet 3 bins are needed. F = 3 against the valid bound is
    // a positive gap; equality would need a bound of 3 the model does not give.
    const GapCase rounded = gapCaseOf({6, 6, 6});
    CHECK(rounded.ffd == 3);
    CHECK(rounded.optimum == 3);
    CHECK(rounded.bound <= 3);
    CHECK(shouldEscalate(rounded.ffd, rounded.bound, 1, true) == (rounded.bound < rounded.ffd));
    m5Evidence::note("expected", {"F 2 = B 2: no escalation", "bound never above the brute-force optimum"});
}

TEST_CASE("U-FF-05/fxGap: the gap is reported in trucks and percent of the bound, N/A without a valid bound" * doctest::test_suite("m5")) {
    CHECK(gapText(3, 2) == "1 truck, 50.0% above the bound of 2");
    CHECK(gapText(12, 10) == "2 trucks, 20.0% above the bound of 10");
    CHECK(gapText(2, 2) == "0 trucks, 0.0% above the bound of 2");
    CHECK(gapText(3, nullopt) == "N/A (no valid bound)");
    CHECK(gapText(3, 0) == "N/A (no valid bound)");
    m5Evidence::note("expected", {"1 truck 50.0%", "2 trucks 20.0%", "0 trucks", "N/A without a bound or with bound 0"});
}

TEST_CASE("ACC-3/fxGap U-FF-01 U-FF-02 U-FF-03 FX-GAP escalates on F > B with allowance left" * doctest::test_suite("m5")) {
    CHECK(shouldEscalate(11, 10, 1, true));
    CHECK(shouldEscalate(12, 10, 1, true));
    CHECK_FALSE(shouldEscalate(12, 10, 1, false));
    CHECK_FALSE(shouldEscalate(10, 10, 1, true));
    // The G = 1 boundary on both sides, and a larger recorded gap: the trigger is F - B >= G, with time left.
    for (const long long bound : {1LL, 2LL, 10LL, 38LL}) {
        INFO("bound " << bound);
        CHECK_FALSE(shouldEscalate(bound, bound, 1, true));
        CHECK(shouldEscalate(bound + 1, bound, 1, true));
        CHECK(shouldEscalate(bound + 2, bound, 1, true));
        CHECK_FALSE(shouldEscalate(bound + 1, bound, 1, false));
        CHECK_FALSE(shouldEscalate(bound + 1, bound, 2, true));
        CHECK(shouldEscalate(bound + 2, bound, 2, true));
    }
    m5Evidence::note("expected", {"F = B: no escalation", "F = B + 1 with G = 1: escalates only while allowance is left", "G = 2 moves the boundary to F = B + 2"});
}

namespace {

// Every multiset of at most `maxItems` item sizes from 1 to 6 at capacity 6, each solved and compared with brute force.
size_t everyMultisetUpTo(size_t maxItems) {
    size_t multisets = 0;
    vector<long long> sizes;
    function<void(long long)> extend = [&](long long smallest) {
        ++multisets;
        const ScalarModel model = modelOf(sizes, 6);
        const ScalarResult result = solveCuttingStock(model, ScalarLimits());
        INFO("sizes " << sizes.size() << " items, multiset " << multisets);
        REQUIRE(result.packing.has_value());
        REQUIRE(m5Oracles::packingValid(model, *result.packing, result.impossible));
        REQUIRE(static_cast<long long>(result.packing->bins.size()) == m5Oracles::bruteForceBins(sizes, 6));
        if (sizes.size() == maxItems) return;
        for (long long size = smallest; size <= 6; ++size) {
            sizes.push_back(size);
            extend(size);
            sizes.pop_back();
        }
    };
    extend(1);
    return multisets;
}

}  // namespace

TEST_CASE("X-META-01/ciSubset every multiset of up to 4 sizes 1-6 at capacity 6 matches brute force (the deterministic CI subset of the 1,716)" * doctest::test_suite("m5")) {
    // 1 + 6 + 21 + 56 + 126 multisets of 0 to 4 items.
    CHECK(everyMultisetUpTo(4) == 210);
    m5Evidence::note("expected", {"210 multisets of 0 to 4 items: best bin count equals brute force, packing valid"});
}

TEST_CASE("X-META-01/full every multiset of up to 7 sizes 1-6 at capacity 6 matches brute force: all 1,716" * doctest::test_suite("m5")) {
    CHECK(everyMultisetUpTo(7) == 1716);
    m5Evidence::note("expected", {"1,716 multisets of 0 to 7 items: best bin count equals brute force, packing valid"});
}

TEST_CASE("X-CG-15/seeded random small cases match brute force" * doctest::test_suite("m5")) {
    constexpr unsigned seed = 20261113;
    mt19937 generator(seed);
    for (int round = 0; round < 300; ++round) {
        const long long capacity = uniform_int_distribution<long long>(5, 20)(generator);
        const size_t count = uniform_int_distribution<size_t>(0, 10)(generator);
        vector<long long> sizes;
        for (size_t item = 0; item < count; ++item) sizes.push_back(uniform_int_distribution<long long>(1, capacity)(generator));
        const ScalarModel model = modelOf(sizes, capacity);
        const ScalarResult result = solveCuttingStock(model, ScalarLimits());
        INFO("seed " << seed << " round " << round << " capacity " << capacity << " items " << count);
        REQUIRE(result.packing.has_value());
        REQUIRE(m5Oracles::packingValid(model, *result.packing, result.impossible));
        REQUIRE(static_cast<long long>(result.packing->bins.size()) == m5Oracles::bruteForceBins(sizes, capacity));
    }
    m5Evidence::note("expected", {"300 seeded small cases of at most 10 items equal brute force; a failing seed and round are printed"});
}

TEST_CASE("X-META-02/synthetic scaling sizes and capacity by 10 and 100 keeps the best bin count" * doctest::test_suite("m5")) {
    const vector<long long> sizes{6, 5, 3, 2, 2, 2};
    for (const long long scale : {1LL, 10LL, 100LL}) {
        vector<long long> scaled;
        for (const long long size : sizes) scaled.push_back(size * scale);
        CHECK(solveChecked(modelOf(scaled, 10 * scale)).packing->bins.size() == 2);
    }
}

TEST_CASE("X-META-08/synthetic a do-not-mix rule never lowers the exact best; removing it restores it" * doctest::test_suite("m5")) {
    ScalarModel mixed = modelOf({6, 4, 6, 4});
    ScalarModel apart = mixed;
    apart.groupOf = {0, 1, 0, 1};
    CHECK(solveChecked(apart).packing->bins.size() >= solveChecked(mixed).packing->bins.size());
    // Seeded small cases against an independent exact answer: with the rule the best is the sum over groups.
    constexpr unsigned seed = 20261118;
    mt19937 generator(seed);
    for (int round = 0; round < 80; ++round) {
        const size_t count = uniform_int_distribution<size_t>(1, 8)(generator);
        vector<long long> sizes;
        vector<size_t> groups;
        for (size_t item = 0; item < count; ++item) {
            sizes.push_back(uniform_int_distribution<long long>(1, 10)(generator));
            groups.push_back(uniform_int_distribution<size_t>(0, 2)(generator));
        }
        long long separate = 0;
        for (size_t group = 0; group < 3; ++group) {
            vector<long long> members;
            for (size_t item = 0; item < count; ++item) {
                if (groups[item] == group) members.push_back(sizes[item]);
            }
            separate += m5Oracles::bruteForceBins(members, 10);
        }
        const long long together = m5Oracles::bruteForceBins(sizes, 10);
        ScalarModel withRule = modelOf(sizes, 10);
        withRule.groupOf = groups;
        ScalarModel withoutRule = modelOf(sizes, 10);
        INFO("seed " << seed << " round " << round << " sizes " << count);
        const ScalarResult ruled = solveChecked(withRule), free = solveChecked(withoutRule);
        REQUIRE(ruled.packing.has_value());
        REQUIRE(free.packing.has_value());
        CHECK(static_cast<long long>(ruled.packing->bins.size()) == separate);
        CHECK(static_cast<long long>(free.packing->bins.size()) == together);
        CHECK(separate >= together);
    }
    m5Evidence::note("expected", {"with a do-not-mix rule the exact best is never fewer bins than without", "removing the rule restores the unmixed best"});
}

// PUB-01..03 need OR-Library's binpack1.txt in tests/m5/data/orlib/. It is skipped in the full run so a
// clone without it still passes; the m5Published ctest entry runs it with --no-skip, where a missing
// file fails by name. Neither a skip nor a missing file is a pass.
TEST_CASE("U-CG-02/published PUB-01/published PUB-02/published PUB-03/published: ACC-1 Falkenauer u120_00..02 reach their proven optima with an independently checked packing" * doctest::test_suite("m5Published")
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
        REQUIRE(result.packing.has_value());
        CHECK(result.impossible.empty());
        CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
        CHECK(static_cast<long long>(result.packing->bins.size()) == optimum);
    }
    m5Evidence::input("binpack1", path);
    m5Evidence::note("expected", {"u120_00 48", "u120_01 49", "u120_02 46", "packings checked independently"});
}

// ACC-1: the acceptance gate for the scalar solver. It is the published cases above and the small
// capacity-only exact cases of v12 Section 12, each reaching its known best with a complete packing that an
// independent checker accepts. No x/y, no trailer, no dates: capacity 10 unless a case says otherwise.
TEST_CASE("ACC-1/scalarSuite: PUB-01..03 and the Section 12 capacity-only exact cases (X-CG-18 included) reach their known best with independently checked packings" * doctest::test_suite("m5Published")
          * doctest::skip()) {
    struct Exact {
        string name;
        vector<long long> sizes;
        long long best;
    };
    vector<long long> fifteenTwos(15, 2), seventeenTwos(17, 2), fiveThrees(5, 3);
    const vector<Exact> small{
        {"FFD trap", {6, 5, 3, 2, 2, 2}, 2},
        {"weak volume bound", {6, 6, 6}, 3},
        {"fractional relaxation", fiveThrees, 2},
        {"mixed patterns", {6, 6, 4, 4}, 2},
        {"missing mixed pattern", {6, 4}, 1},
        {"exact multiple", fifteenTwos, 3},
        {"with a remainder", seventeenTwos, 4},
        {"converged but not best (X-CG-18)", {5, 5, 5, 2, 2, 2, 2, 2, 2}, 3},
    };
    for (const Exact& instance : small) {
        INFO(instance.name);
        const ScalarModel model = modelOf(instance.sizes);
        const ScalarResult result = solveChecked(model);
        REQUIRE(result.packing.has_value());
        CHECK(result.impossible.empty());
        CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
        CHECK(static_cast<long long>(result.packing->bins.size()) == instance.best);
        size_t itemsPacked = 0;
        for (const auto& bin : result.packing->bins) itemsPacked += bin.size();
        CHECK(itemsPacked == instance.sizes.size());
    }

    const string path = "tests/m5/data/orlib/binpack1.txt";
    ifstream file(path);
    REQUIRE_MESSAGE(file.good(), "missing " << path << ": PUB-01..03 are BLOCKED until it is added");
    int problems = 0;
    file >> problems;
    for (const pair<string, long long>& published : vector<pair<string, long long>>{{"u120_00", 48}, {"u120_01", 49}, {"u120_02", 46}}) {
        string id;
        long long capacity = 0, count = 0, best = 0;
        file >> id >> capacity >> count >> best;
        REQUIRE(id == published.first);
        REQUIRE(best == published.second);
        ScalarModel model = modelOf({}, capacity);
        for (long long item = 0; item < count; ++item) {
            long long size = 0;
            file >> size;
            model.sizes.push_back(size);
        }
        const ScalarResult result = solveChecked(model);
        INFO(published.first);
        REQUIRE(result.packing.has_value());
        CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
        CHECK(static_cast<long long>(result.packing->bins.size()) == published.second);
    }
    m5Evidence::input("binpack1", path);
    m5Evidence::note("expected", {"eight Section 12 cases at their known best", "u120_00 48, u120_01 49, u120_02 46", "every packing checked independently"});
}

// ---- X-CG-22 and U-FF-01/ffdConcreteGap (tests v12 Section 42) -------------------------------------

namespace {

// Two size-5 and five size-2 items at capacity 10: total 20, two bins [5,5] and [2,2,2,2,2].
ScalarModel duplicateZeroCostModel() { return modelOf({5, 5, 2, 2, 2, 2, 2}); }

double dotOf(const vector<double>& duals, const vector<int>& counts) {
    double value = 0.0;
    for (size_t type = 0; type < counts.size(); ++type) value += duals[type] * counts[type];
    return value;
}

// The sizes of each bin, sorted, the bins sorted: comparable whatever the item indices.
vector<vector<long long>> binSizes(const ScalarModel& model, const ScalarPacking& packing) {
    vector<vector<long long>> bins;
    for (const vector<size_t>& bin : packing.bins) {
        vector<long long> sizes;
        for (const size_t item : bin) sizes.push_back(model.sizes[item]);
        sort(sizes.begin(), sizes.end());
        bins.push_back(sizes);
    }
    sort(bins.begin(), bins.end());
    return bins;
}

// First-fit decreasing as the specification defines it: nonincreasing sizes, each into the first bin
// with room, else a new bin.
vector<vector<long long>> referenceFfd(vector<long long> sizes, long long capacity) {
    sort(sizes.begin(), sizes.end(), greater<long long>());
    vector<vector<long long>> bins;
    vector<long long> used;
    for (const long long size : sizes) {
        size_t bin = 0;
        while (bin < bins.size() && used[bin] + size > capacity) ++bin;
        if (bin == bins.size()) {
            bins.emplace_back();
            used.push_back(0);
        }
        bins[bin].push_back(size);
        used[bin] += size;
    }
    for (vector<long long>& bin : bins) sort(bin.begin(), bin.end());
    sort(bins.begin(), bins.end());
    return bins;
}

}  // namespace

TEST_CASE("X-CG-22/duplicateZeroCostColumns: pricing that keeps returning the present columns stops at once; [5,5] and [2x5] proven"
          * doctest::test_suite("m5")) {
    const ScalarModel model = duplicateZeroCostModel();
    for (const double claimedExtra : {0.0, 0.5}) {
        INFO("claimed value above the duals' own: " << claimedExtra);
        size_t pricingCalls = 0;
        vector<vector<double>> dualsSeen;
        ScalarLimits limits;
        // Sizes come largest first, so the starting columns are (2,0) and (0,5); return them in turn.
        limits.pricing = [&](const vector<long long>& sizes, long long, const vector<double>& duals, const vector<long long>&) {
            REQUIRE(sizes == vector<long long>{5, 2});
            dualsSeen.push_back(duals);
            const vector<int> counts = pricingCalls++ % 2 == 0 ? vector<int>{2, 0} : vector<int>{0, 5};
            return PricedPattern{counts, dotOf(duals, counts) + claimedExtra, true};
        };
        const ScalarResult result = solveCuttingStock(model, limits);
        REQUIRE(result.packing.has_value());
        CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
        CHECK(binSizes(model, *result.packing) == vector<vector<long long>>{{2, 2, 2, 2, 2}, {5, 5}});
        CHECK(result.evidence.columnsGenerated == 2);
        CHECK(pricingCalls >= 1);
        CHECK(pricingCalls <= 2);
        REQUIRE_FALSE(dualsSeen.empty());
        CHECK(dualsSeen.front()[0] == doctest::Approx(0.5));
        CHECK(dualsSeen.front()[1] == doctest::Approx(0.2));
        // Reduced cost of each present column at those duals: 1 - 2 x 0.5 = 0 and 1 - 5 x 0.2 = 0.
        CHECK(1.0 - 2 * dualsSeen.front()[0] == doctest::Approx(0.0));
        CHECK(1.0 - 5 * dualsSeen.front()[1] == doctest::Approx(0.0));
        CHECK(m5Oracles::bruteForceBins(model.sizes, 10) == 2);
        CHECK(result.evidence.lowerBound == 2);
        CHECK(result.evidence.incumbent == 2);
        CHECK(result.evidence.proof == ProofStatus::ProvenOptimal);
        CHECK(result.evidence.termination == Termination::Completed);
        m5Evidence::note("actual", {{"pricingCalls", pricingCalls}, {"columns", result.evidence.columnsGenerated}, {"bins", 2}});
    }
    m5Evidence::note("expected", {{"bins", "[5,5] [2,2,2,2,2]"}, {"columns", 2}, {"bound", 2}, {"proof", "ProvenOptimal by bound 2"}});
}

TEST_CASE("X-CG-22/nearZeroReducedCost: reduced costs -1e-12, 0 and +1e-12 follow the recorded tolerance; no copied demand, no proof from the tolerance"
          * doctest::test_suite("m5")) {
    const ScalarModel model = duplicateZeroCostModel();
    // A column absent from the master: one 5 and two 2s (size 9).
    const vector<int> fresh{1, 2};
    struct Case {
        double reducedCost;
        double tolerance;
        bool enters;
    };
    const vector<Case> cases{{-1e-12, 1e-9, false}, {0.0, 1e-9, false}, {1e-12, 1e-9, false},
                             {-1e-12, 0.0, true},   {0.0, 0.0, false},  {1e-12, 0.0, false}};
    for (const Case& c : cases) {
        INFO("reduced cost " << c.reducedCost << ", tolerance " << c.tolerance);
        // Independent check of the decision in long double: a column enters when 1 - value < -tolerance.
        const long double value = 1.0L - static_cast<long double>(c.reducedCost);
        CHECK(((1.0L - value) < -static_cast<long double>(c.tolerance)) == c.enters);
        size_t pricingCalls = 0;
        ScalarLimits limits;
        limits.pricingTolerance = c.tolerance;
        limits.pricing = [&](const vector<long long>&, long long, const vector<double>&, const vector<long long>&) {
            ++pricingCalls;
            return PricedPattern{fresh, 1.0 - c.reducedCost, true};
        };
        const ScalarResult result = solveCuttingStock(model, limits);
        REQUIRE(result.packing.has_value());
        CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
        size_t packedItems = 0;
        for (const vector<size_t>& bin : result.packing->bins) packedItems += bin.size();
        CHECK(packedItems == model.sizes.size());
        CHECK(result.evidence.columnsGenerated == (c.enters ? 3u : 2u));
        // The column enters once; offered again it is a duplicate, so pricing stops within a few calls.
        CHECK(pricingCalls <= static_cast<size_t>(limits.maxColumnRounds));
        CHECK(pricingCalls <= 4);
        // The proof is the incumbent meeting the L1 bound of the same model, not the tolerance rule.
        CHECK(result.evidence.incumbent == 2);
        CHECK(result.evidence.lowerBound == 2);
        CHECK(result.evidence.proof == ProofStatus::ProvenOptimal);
        CHECK(result.evidence.proofModel == result.evidence.boundModel);
    }
    m5Evidence::note("expected", {{"enters", "only -1e-12 at tolerance 0"}, {"tolerance", 1e-9}, {"proofScope", "incumbent equals L1 bound 2"}});
    m5Evidence::note("actual", "as expected");
}

TEST_CASE("U-FF-01/ffdConcreteGap: [6,5,3,2,2,2] at 10: FFD 3 bins, 2 exist and bound 2, F - B = 1 escalates under G = 1"
          * doctest::test_suite("m5")) {
    const ScalarModel model = modelOf({6, 5, 3, 2, 2, 2});
    const vector<vector<long long>> ffd = referenceFfd(model.sizes, 10);
    CHECK(ffd == vector<vector<long long>>{{2}, {2, 2, 5}, {3, 6}});
    const ScalarPacking production = scalarFirstFitDecreasing(model);
    CHECK(m5Oracles::packingValid(model, production, {}));
    CHECK(binSizes(model, production) == ffd);
    const long long bound = scalarLowerBound(model);
    CHECK(bound == 2);
    CHECK(m5Oracles::bruteForceBins(model.sizes, 10) == 2);
    const ScalarResult solved = solveChecked(model);
    CHECK(binSizes(model, *solved.packing) == vector<vector<long long>>{{2, 2, 6}, {2, 3, 5}});
    CHECK(solved.ffdBins == 3);
    // FX-GAP: G = 1 with allowance left; F - B = 1 escalates, without allowance it does not.
    CHECK(shouldEscalate(static_cast<long long>(production.bins.size()), bound, 1, true));
    CHECK_FALSE(shouldEscalate(static_cast<long long>(production.bins.size()), bound, 1, false));
    // The proof is of the scalar capacity model only, never of a physical allocation.
    CHECK(solved.evidence.proof == ProofStatus::ProvenOptimal);
    CHECK(solved.evidence.proofModel.find("capacityOnly") == 0);
    CHECK(solved.evidence.layoutScope.empty());
    m5Evidence::note("expected", {{"ffd", 3}, {"optimum", 2}, {"bound", 2}, {"escalate", true}});
    m5Evidence::note("actual", {{"ffd", production.bins.size()}, {"optimum", solved.packing->bins.size()}, {"bound", bound}});
}
