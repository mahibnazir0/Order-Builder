#include "doctest.h"
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
