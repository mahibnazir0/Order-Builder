#include "doctest.h"
#include "paramsLoader.hpp"
#include "pipeline.hpp"
#include "segregation.hpp"
#include "stackBuilder.hpp"
#include "stackRules.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <map>

using namespace ob;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

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
ProductRecord product(const std::string& id, double heightIn, double weightLb, int cri) {
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
    std::vector<ProductRecord> products;
    std::vector<JoinedLine> lines;
    std::vector<double> pallets;

    Scenario(std::vector<ProductRecord> productList, std::vector<double> quantities)
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
        for (std::size_t i = 0; i < lines.size(); ++i) group.lineIndices.push_back(i);
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

TEST_CASE("stackBuilder: a short load stacks on itself up to the ceiling") {
    Scenario scenario({product("A", 30, 100, 9)}, {4});
    const auto result = scenario.build();
    CHECK(result.groups[0].best.floorPositions == doctest::Approx(4.0 / 3.0));
    REQUIRE(result.groups[0].best.stacks.size() >= 1);
    CHECK(result.groups[0].best.stacks[0].lineIndices.size() == 3);
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
        std::size_t loadsOnWeak = 0;
        bool sawWeak = false;
        for (const std::size_t member : stack.lineIndices) {
            if (sawWeak && member == 1) ++loadsOnWeak;
            if (member == 0) sawWeak = true;
        }
        CHECK(loadsOnWeak <= 1);
    }
}

TEST_CASE("stackBuilder: every pallet is placed exactly once") {
    Scenario scenario({product("A", 30, 100, 9), product("B", 45, 50, 5), product("C", 70, 20, 9)},
                      {4.5, 3.25, 2});
    const auto result = scenario.build();
    std::map<std::size_t, double> placed;
    for (const auto& stack : result.groups[0].best.stacks) {
        for (const std::size_t member : stack.lineIndices) placed[member] += stack.quantity;
    }
    for (std::size_t i = 0; i < scenario.pallets.size(); ++i) {
        CHECK(placed[i] == doctest::Approx(scenario.pallets[i]));
    }
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

TEST_CASE("stackBuilder: zero quantity is skipped and negative or non-finite is counted") {
    Scenario scenario({product("A", 30, 10, 9), product("B", 30, 10, 9), product("C", 30, 10, 9),
                       product("D", 30, 10, 9), product("E", 30, 10, 9)}, {0, -1, kNaN, kInf, 2});
    const auto result = scenario.build();
    CHECK(result.excludedInvalidQuantityLines == 3);
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
                                    testParams(), badTrailer), std::invalid_argument);
    }
    CHECK_THROWS_AS(buildStacks(segregation, scenario.lines, {}, binding, testParams(), trailer()),
                    std::invalid_argument);
    CHECK_THROWS_AS(buildStacks(segregation, scenario.lines, scenario.pallets, BindingResult{},
                                testParams(), trailer()), std::invalid_argument);
    segregation.groups[0].lineIndices = {5};
    CHECK_THROWS_AS(buildStacks(segregation, scenario.lines, scenario.pallets, binding,
                                testParams(), trailer()), std::invalid_argument);
}

TEST_CASE("stackBuilder: real demand builds valid stacks within the time budget") {
    PipelineInputs inputs;
    inputs.product_path = "tests/importer/Customer2-Product-Data.csv";
    inputs.demand_path = "tests/importer/Demand-1.json";
    inputs.placeholder_path = "tests/importer/PlaceHolder-1.json";
    const PipelineResult run = Pipeline::run(inputs);
    const M2Params params = loadParams("config/orderBuilderParams.json");
    const TrailerSpec& trailerSpec = params.trailers[0];
    const auto segregation = segregate(run.join.lines, run.demand.dnm, SegregationReading::Strict);
    const auto binding = assessBinding(segregation, run.pallets_per_line, run.weight_per_line, trailerSpec);

    const auto started = std::chrono::steady_clock::now();
    const auto result = buildStacks(segregation, run.join.lines, run.pallets_per_line, binding,
                                    params, trailerSpec);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    CHECK(seconds < 10.0);

    REQUIRE(result.groups.size() == segregation.groups.size());
    for (std::size_t g = 0; g < result.groups.size(); ++g) {
        double lineTotal = 0.0;
        for (const std::size_t i : segregation.groups[g].lineIndices) lineTotal += run.pallets_per_line[i];
        const auto& best = result.groups[g].best;
        double stacked = 0.0;
        for (const auto& stack : best.stacks) {
            stacked += stack.quantity * static_cast<double>(stack.lineIndices.size());
            CHECK(stack.lineIndices.size() <= static_cast<std::size_t>(params.maxStackHeight));
            double heightIn = 0.0;
            for (const std::size_t member : stack.lineIndices) {
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
