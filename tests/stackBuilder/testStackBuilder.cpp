#include "doctest.h"
#include "../importer/crossDayFixtures.hpp"
#include "paramsLoader.hpp"
#include "pipeline.hpp"
#include "segregation.hpp"
#include "stackBuilder.hpp"
#include "stackRules.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>

using namespace std;
using namespace ob;

namespace {

const double kNaN = numeric_limits<double>::quiet_NaN();
const double kInf = numeric_limits<double>::infinity();

M2Params testParams() {
    M2Params params;
    params.cri.safeLimitLb = {0, 5, 299, 549, 799, 1149, 1499, 1849, 2199, 3099, 3599};
    params.pallets = {{"TLD", 0.0, 0.0, 48.0, 40.0}};
    params.pass2AttemptCap = 4;
    params.maxStackHeight = 3;
    return params;
}

TrailerSpec trailer() {
    TrailerSpec spec;
    spec.stackHeightCeilingIn = 108.0;
    spec.weightLimitLb = 45000.0;
    spec.stackPositions = 30;
    return spec;
}

// One case per layer and one layer, so height and weight are the given case figures.
ProductRecord product(const string& id, double heightIn, double weightLb, int cri) {
    ProductRecord record;
    record.id = id;
    record.height_in = heightIn;
    record.weight_lb = weightLb;
    record.strength = cri;
    record.cases_layer = 1;
    record.layers_unit_load = 1;
    record.cases_unit_load = 1;
    record.pallet_id = "TLD";
    return record;
}

// Products must outlive the lines that point at them.
struct Scenario {
    vector<ProductRecord> products;
    vector<JoinedLine> lines;
    vector<double> pallets;

    Scenario(vector<ProductRecord> productList, vector<double> quantities)
        : products(std::move(productList)), pallets(std::move(quantities)) {
        for (const auto& record : products) {
            JoinedLine line;
            line.product = &record;
            line.matched = true;
            lines.push_back(line);
        }
    }

    StackingResult build(const M2Params& params = testParams()) const {
        SegregationResult segregation;
        SegregatedGroup group;
        for (size_t i = 0; i < lines.size(); ++i) group.lineIndices.push_back(i);
        segregation.groups.push_back(group);
        BindingResult binding;
        binding.groups.resize(1);
        return buildStacks(segregation, lines, pallets, binding, params, trailer());
    }
};

double outcomeFor(const GroupStacking& group, StackMethod method) {
    for (const auto& outcome : group.outcomes) {
        if (outcome.method == method) return outcome.floorPositions;
    }
    return -1.0;
}

} // namespace

TEST_CASE("stackBuilder: loads too tall to stack stay single-high") {
    Scenario scenario({product("A", 100, 10, 9), product("B", 101, 10, 9)}, {3, 2});
    const auto result = scenario.build();
    REQUIRE(result.groups.size() == 1);
    CHECK(result.groups[0].best.floorPositions == doctest::Approx(5.0));
    for (const auto& outcome : result.groups[0].outcomes) {
        CHECK(outcome.floorPositions == doctest::Approx(5.0));
    }
}

M2Params fractionalParams() {
    M2Params params = testParams();
    params.stackWholePallets = false;
    return params;
}

TEST_CASE("stackBuilder: a short load stacks on itself up to the ceiling") {
    Scenario scenario({product("A", 30, 100, 9)}, {4});
    const auto result = scenario.build();
    CHECK(result.groups[0].best.floorPositions == doctest::Approx(2.0));
    REQUIRE(result.groups[0].best.stacks.size() == 2);
    CHECK(result.groups[0].best.stacks[0].lineIndices.size() == 3);
    CHECK(result.groups[0].best.stacks[0].quantity == 1.0);
    CHECK(result.groups[0].best.stacks[1].lineIndices.size() == 1);
    CHECK(result.groups[0].best.stacks[1].quantity == 1.0);
}

TEST_CASE("stackBuilder: the fractional estimate spreads 4 pallets over 4/3 floor positions") {
    Scenario scenario({product("A", 30, 100, 9)}, {4});
    CHECK(scenario.build(fractionalParams()).groups[0].best.floorPositions == doctest::Approx(4.0 / 3.0));
}

TEST_CASE("stackBuilder: one physical pallet takes a whole floor position") {
    Scenario scenario({product("A", 30, 100, 9)}, {1});
    CHECK(scenario.build().groups[0].best.floorPositions == 1.0);
    M2Params params = fractionalParams();
    params.maxStackHeight = 2;
    CHECK(scenario.build(params).groups[0].best.floorPositions == doctest::Approx(0.5));
}

TEST_CASE("stackBuilder: a part pallet is rounded up to a whole pallet") {
    Scenario scenario({product("A", 60, 100, 9)}, {0.25});
    const auto result = scenario.build();
    REQUIRE(result.groups[0].best.stacks.size() == 1);
    CHECK(result.groups[0].best.stacks[0].quantity == 1.0);
    CHECK(result.groups[0].best.floorPositions == 1.0);
}

TEST_CASE("stackBuilder: an exact whole quantity is not rounded up past itself") {
    Scenario scenario({product("A", 60, 100, 9)}, {3.0 + 1e-12});
    CHECK(scenario.build().groups[0].best.floorPositions == 3.0);
}

TEST_CASE("stackBuilder: stacks never exceed the configured maximum height") {
    M2Params params = testParams();
    Scenario scenario({product("A", 30, 100, 9)}, {4});
    params.maxStackHeight = 2;
    const auto twoHigh = scenario.build(params);
    CHECK(twoHigh.groups[0].best.floorPositions == doctest::Approx(2.0));
    for (const auto& stack : twoHigh.groups[0].best.stacks) CHECK(stack.lineIndices.size() <= 2);
    params.maxStackHeight = 1;
    CHECK(scenario.build(params).groups[0].best.floorPositions == doctest::Approx(4.0));
}

TEST_CASE("stackBuilder: stacks use only as many pallets as the scarcer line has") {
    Scenario scenario({product("A", 60, 10, 9), product("B", 40, 10, 9)}, {3, 1});
    const auto result = scenario.build();
    CHECK(result.groups[0].best.floorPositions == doctest::Approx(3.0));
}

TEST_CASE("stackBuilder: the best method is kept and a weaker one is still reported") {
    Scenario scenario({product("A", 40, 10, 0), product("B", 60, 10, 9)}, {1, 1});
    const auto result = scenario.build();
    CHECK(result.groups[0].best.floorPositions == doctest::Approx(1.0));
    CHECK(outcomeFor(result.groups[0], StackMethod::Natural) == doctest::Approx(2.0));
    CHECK(outcomeFor(result.groups[0], StackMethod::BaseAndTop) == doctest::Approx(1.0));
}

TEST_CASE("stackBuilder: a base with a low crush rating cannot carry a heavy load") {
    Scenario scenario({product("Weak", 30, 10, 1), product("Heavy", 30, 200, 9)}, {1, 1});
    const auto result = scenario.build();
    for (const auto& stack : result.groups[0].best.stacks) {
        CHECK_FALSE((stack.lineIndices.size() == 2 && stack.lineIndices[0] == 0));
    }
}

TEST_CASE("stackBuilder: a middle load must carry everything above it, not only the next one") {
    // Weak carries 299 lb: one 200 lb load fits, two do not.
    Scenario scenario({product("Weak", 20, 50, 2), product("Load", 20, 200, 9)}, {1, 2});
    const auto result = scenario.build();
    for (const auto& stack : result.groups[0].best.stacks) {
        size_t loadsOnWeak = 0;
        bool sawWeak = false;
        for (const size_t member : stack.lineIndices) {
            if (sawWeak && member == 1) ++loadsOnWeak;
            if (member == 0) sawWeak = true;
        }
        CHECK(loadsOnWeak <= 1);
    }
}

TEST_CASE("stackBuilder: every pallet is placed exactly once") {
    Scenario scenario({product("A", 30, 100, 9), product("B", 45, 50, 5), product("C", 70, 20, 9)},
                      {4.5, 3.25, 2});
    for (const bool wholePallets : {true, false}) {
        CAPTURE(wholePallets);
        M2Params params = testParams();
        params.stackWholePallets = wholePallets;
        const auto result = scenario.build(params);
        map<size_t, double> placed;
        for (const auto& stack : result.groups[0].best.stacks) {
            if (wholePallets) CHECK(stack.quantity == floor(stack.quantity));
            for (const size_t member : stack.lineIndices) placed[member] += stack.quantity;
        }
        for (size_t i = 0; i < scenario.pallets.size(); ++i) {
            const double expected = wholePallets ? ceil(scenario.pallets[i]) : scenario.pallets[i];
            CHECK(placed[i] == doctest::Approx(expected));
        }
    }
}

TEST_CASE("stackBuilder: a partial pallet stacks as a whole pallet, or as its fraction when whole pallets are off") {
    M2Params params = testParams();
    CHECK(stackedPalletsForLine(2.25, params) == 3.0);
    CHECK(stackedPalletsForLine(0.5, params) == 1.0);
    CHECK(stackedPalletsForLine(3.0, params) == 3.0);
    params.stackWholePallets = false;
    CHECK(stackedPalletsForLine(2.25, params) == 2.25);
    CHECK(stackedPalletsForLine(0.5, params) == 0.5);
}

TEST_CASE("stackBuilder: stacked line flags mark exactly the lines that sit in a stack") {
    Scenario scenario({product("A", 30, 10, 9), product("B", 30, 10, 9), product("C", 200, 10, 9)},
                      {2, 0, 1});
    const auto result = scenario.build();
    CHECK(stackedLineFlags(result, 3) == vector<bool>{true, false, false});
}

// Worked example 4 in docs/m2BusinessRules.md: the five methods on one 4-pallet group,
// two high as for this customer. Only Base & Top finds both pairs.
TEST_CASE("stackBuilder: documented worked example gives each method's floor positions") {
    M2Params params = testParams();
    params.maxStackHeight = 2;
    Scenario scenario({product("A", 70, 500, 9), product("B", 40, 900, 2),
                       product("C", 35, 200, 9), product("D", 60, 300, 5)}, {1, 1, 1, 1});
    const auto result = scenario.build(params);
    const GroupStacking& group = result.groups[0];
    CHECK(outcomeFor(group, StackMethod::Natural) == 3.0);
    CHECK(outcomeFor(group, StackMethod::Target) == 3.0);
    CHECK(outcomeFor(group, StackMethod::TallAndHeavy) == 3.0);
    CHECK(outcomeFor(group, StackMethod::BaseAndTop) == 2.0);
    CHECK(outcomeFor(group, StackMethod::TryHard) == 3.0);
    CHECK(group.best.method == StackMethod::BaseAndTop);
    REQUIRE(group.best.stacks.size() == 2);
    CHECK(group.best.stacks[0].lineIndices == vector<size_t>{0, 2});
    CHECK(group.best.stacks[1].lineIndices == vector<size_t>{3, 1});
}

TEST_CASE("stackBuilder: results are repeatable") {
    Scenario scenario({product("A", 30, 100, 9), product("B", 45, 50, 5), product("C", 60, 20, 9)},
                      {4, 3, 2});
    const auto first = scenario.build();
    const auto second = scenario.build();
    CHECK(first.groups[0].best.floorPositions == second.groups[0].best.floorPositions);
    CHECK(first.groups[0].best.method == second.groups[0].best.method);
}

TEST_CASE("stackBuilder: an attempt cap of zero skips Try Hard") {
    M2Params params = testParams();
    params.pass2AttemptCap = 0;
    Scenario scenario({product("A", 30, 100, 9)}, {4});
    CHECK(scenario.build(params).groups[0].outcomes.size() == 4);
    CHECK(scenario.build().groups[0].outcomes.size() == 5);
}

TEST_CASE("stackBuilder: an empty group gives an empty stack set") {
    Scenario scenario({}, {});
    const auto result = scenario.build();
    CHECK(result.groups[0].best.stacks.empty());
    CHECK(result.groups[0].best.floorPositions == 0.0);
}

TEST_CASE("stackBuilder: zero, negative and non-finite quantities are reported by line, not stacked") {
    Scenario scenario({product("A", 30, 10, 9), product("B", 30, 10, 9), product("C", 30, 10, 9),
                       product("D", 30, 10, 9), product("E", 30, 10, 9)}, {0, -1, kNaN, kInf, 2});
    const auto result = scenario.build();
    CHECK(result.zeroQuantityLines == vector<size_t>{0});
    CHECK(result.invalidQuantityLines == vector<size_t>{1, 2, 3});
    CHECK(result.linesNotStacked() == 4);
    CHECK(result.excludedLines.empty());
    CHECK(result.groups[0].best.floorPositions > 0.0);
}

TEST_CASE("stackBuilder: lines that cannot become a unit load are reported, not stacked") {
    Scenario scenario({product("A", 0, 10, 9), product("B", 30, 10, 9)}, {2, 2});
    scenario.products[1].pallet_id = "XXX";
    const auto result = scenario.build();
    REQUIRE(result.excludedLines.size() == 2);
    CHECK(result.excludedLines[0].error == UnitLoadError::InvalidData);
    CHECK(result.excludedLines[1].error == UnitLoadError::MissingPalletSpec);
}

TEST_CASE("stackBuilder: a single load taller than the ceiling is reported, not stacked") {
    Scenario scenario({product("TALL", 120, 10, 9), product("A", 60, 10, 9)}, {2, 1});
    for (const int maxStackHeight : {3, 1}) {
        CAPTURE(maxStackHeight);
        M2Params params = testParams();
        params.maxStackHeight = maxStackHeight;
        const auto result = scenario.build(params);
        CHECK(result.overHeightLines == vector<size_t>{0});
        CHECK(result.groups[0].best.floorPositions == doctest::Approx(1.0));
        for (const auto& stack : result.groups[0].best.stacks) {
            CHECK(find(stack.lineIndices.begin(), stack.lineIndices.end(), 0u)
                  == stack.lineIndices.end());
        }
    }
}

// 104810501 on real data: 106.25 in of product, 111.75 in with the 5.5 in deck. The floor
// counts it under Excluded and leaves it out under Included; the stacks must agree.
TEST_CASE("stackBuilder: the pallet deck counts toward a single load's height only when floorDeckHeight says so") {
    M2Params params = testParams();
    params.pallets.push_back({"PTL", 60.0, 5.5, 48.0, 40.0});
    Scenario scenario({product("DECKED", 105, 10, 9)}, {1});
    scenario.products[0].pallet_id = "PTL";

    params.floorDeckHeight = DeckHeightRule::Included;
    const auto withDeck = scenario.build(params);
    CHECK(withDeck.overHeightLines == vector<size_t>{0});
    CHECK(withDeck.groups[0].best.stacks.empty());

    params.floorDeckHeight = DeckHeightRule::Excluded;
    const auto withoutDeck = scenario.build(params);
    CHECK(withoutDeck.overHeightLines.empty());
    REQUIRE(withoutDeck.groups[0].best.stacks.size() == 1);
}

TEST_CASE("stackBuilder: a single load on the ceiling, or a ULP above it, is not over-height") {
    M2Params params = testParams();
    // 36/7 in x 21 layers is 108 in on paper and one ULP above it in binary, as on real data.
    Scenario scenario({product("SEVENTHS", 36.0 / 7.0, 10, 9)}, {1});
    scenario.products[0].layers_unit_load = 21;
    scenario.products[0].cases_unit_load = 21;
    REQUIRE(36.0 / 7.0 * 21 > 108.0);
    const auto result = scenario.build(params);
    CHECK(result.overHeightLines.empty());
    CHECK(result.groups[0].best.stacks.size() == 1);
}

TEST_CASE("stackBuilder: a single load exactly at the ceiling ships single-high") {
    Scenario scenario({product("EXACT", 108, 10, 9)}, {1});
    const auto result = scenario.build();
    CHECK(result.overHeightLines.empty());
    CHECK(result.groups[0].best.floorPositions == doctest::Approx(1.0));
}

TEST_CASE("stackBuilder: caller bugs are rejected") {
    Scenario scenario({product("A", 30, 10, 9)}, {1});
    SegregationResult segregation;
    segregation.groups.resize(1);
    BindingResult binding;
    binding.groups.resize(1);
    TrailerSpec badTrailer = trailer();
    for (double badCeiling : {0.0, -1.0, kNaN, kInf}) {
        badTrailer.stackHeightCeilingIn = badCeiling;
        CHECK_THROWS_AS(buildStacks(segregation, scenario.lines, scenario.pallets, binding,
                                    testParams(), badTrailer), invalid_argument);
    }
    CHECK_THROWS_AS(buildStacks(segregation, scenario.lines, {}, binding, testParams(), trailer()),
                    invalid_argument);
    CHECK_THROWS_AS(buildStacks(segregation, scenario.lines, scenario.pallets, BindingResult{},
                                testParams(), trailer()), invalid_argument);
    segregation.groups[0].lineIndices = {5};
    CHECK_THROWS_AS(buildStacks(segregation, scenario.lines, scenario.pallets, binding,
                                testParams(), trailer()), invalid_argument);
}

// Sanitizers slow the build several-fold (6.2 s native on MinGW, about 17 s under ASan), so
// an instrumented run scales the budget instead of failing on overhead. GCC defines no macro
// for UBSan, hence the CMake option OB_INSTRUMENTED_TESTS as well.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || defined(OB_INSTRUMENTED_TESTS)
#define OB_INSTRUMENTED_BUILD 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) \
    || __has_feature(memory_sanitizer) || __has_feature(undefined_behavior_sanitizer)
#define OB_INSTRUMENTED_BUILD 1
#endif
#endif

namespace {
#ifdef OB_INSTRUMENTED_BUILD
constexpr double kInstrumentationSlowdown = 4.0;
#else
constexpr double kInstrumentationSlowdown = 1.0;
#endif
// About three times the slowest native time measured, so a slower machine or a larger extract
// does not fail it; a complexity regression still does, since that costs minutes, not seconds.
constexpr double kStackingBudgetSeconds = 20.0 * kInstrumentationSlowdown;
} // namespace

TEST_CASE("stackBuilder: real demand builds valid stacks within the time budget" * doctest::skip(!crossDayTests::august17StackingPresent())) {
    PipelineInputs inputs;
    inputs.product_path = "tests/importer/Customer2-Product-Data.csv";
    inputs.demand_path = "tests/importer/Demand-1.json";
    inputs.placeholder_path = "tests/importer/PlaceHolder-1.json";
    const PipelineResult run = Pipeline::run(inputs);
    M2Params params = loadParams("config/orderBuilderParams.json");
    params.pallets = ProductImporter::loadPalletTable(crossDayTests::kPalletTableForOlderMasters);
    const TrailerSpec& trailerSpec = params.trailers[0];
    const auto segregation = segregate(run.join.lines, run.demand.dnm, SegregationReading::Strict,
                                       vector<bool>(run.join.lines.size(), false));
    const auto binding = assessBinding(segregation, run.pallets_per_line, run.weight_per_line, trailerSpec);

    const auto started = chrono::steady_clock::now();
    const auto result = buildStacks(segregation, run.join.lines, run.pallets_per_line, binding,
                                    params, trailerSpec);
    const double seconds = chrono::duration<double>(chrono::steady_clock::now() - started).count();
    MESSAGE("buildStacks on 17 Aug took " << seconds << " s of a " << kStackingBudgetSeconds << " s budget");
    CHECK(seconds < kStackingBudgetSeconds);

    REQUIRE(result.groups.size() == segregation.groups.size());
    for (size_t g = 0; g < result.groups.size(); ++g) {
        double lineTotal = 0.0;
        for (const size_t i : segregation.groups[g].lineIndices) lineTotal += ceil(run.pallets_per_line[i]);
        const auto& best = result.groups[g].best;
        double stacked = 0.0;
        for (const auto& stack : best.stacks) {
            stacked += stack.quantity * static_cast<double>(stack.lineIndices.size());
            CHECK(stack.quantity == floor(stack.quantity));
            CHECK(stack.lineIndices.size() <= static_cast<size_t>(params.maxStackHeight));
            double heightIn = 0.0;
            for (const size_t member : stack.lineIndices) {
                const auto load = buildUnitLoad(run.join.lines[member], params);
                heightIn += load.heightIn;
            }
            CHECK(heightIn <= trailerSpec.stackHeightCeilingIn + 1e-9);
        }
        CHECK(best.floorPositions <= lineTotal + 1e-6);
        CHECK(stacked == doctest::Approx(lineTotal).epsilon(1e-6));
        for (const auto& outcome : result.groups[g].outcomes) {
            CHECK(best.floorPositions <= outcome.floorPositions + 1e-9);
        }
    }
}

TEST_CASE("stackBuilder: a product over its own CRI limit is warned about and still ships single-high") {
    ProductRecord overBuilt = product("OVERBUILT", 30, 400, 2);
    overBuilt.layers_unit_load = 2;
    Scenario scenario({overBuilt}, {2});
    const auto result = scenario.build();
    CHECK(result.ownCriExceededLines == vector<size_t>{0});
    CHECK(result.linesNotStacked() == 0);
    CHECK(stackedLineFlags(result, 1)[0]);
    CHECK(result.groups[0].best.floorPositions == doctest::Approx(2.0));
}

TEST_CASE("stackBuilder: a product within its own CRI limit raises no warning") {
    Scenario scenario({product("A", 30, 10, 9)}, {2});
    CHECK(scenario.build().ownCriExceededLines.empty());
}
