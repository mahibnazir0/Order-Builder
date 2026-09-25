#include "doctest.h"
#include "bindingConstraint.hpp"
#include "paramsLoader.hpp"
#include "pipeline.hpp"
#include "segregation.hpp"

#include <limits>

using namespace ob;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

TrailerSpec trailer() {
    TrailerSpec spec;
    spec.weightLimitLb = 45000.0;
    spec.stackPositions = 30;
    return spec;
}

SegregationResult groupsOf(std::vector<std::vector<std::size_t>> lineIndicesPerGroup) {
    SegregationResult result;
    for (auto& indices : lineIndicesPerGroup) {
        SegregatedGroup group;
        group.lineIndices = std::move(indices);
        result.groups.push_back(std::move(group));
    }
    return result;
}

} // namespace

TEST_CASE("bindingConstraint: light pallets are cube-bound") {
    const auto result = assessBinding(groupsOf({{0, 1}}), {10, 20}, {5000, 10000}, trailer());
    REQUIRE(result.groups.size() == 1);
    CHECK(result.groups[0].totalPallets == doctest::Approx(30));
    CHECK(result.groups[0].totalWeightLb == doctest::Approx(15000));
    CHECK(result.groups[0].trucksIfCube == doctest::Approx(1.0));
    CHECK(result.groups[0].trucksIfWeight == doctest::Approx(1.0 / 3.0));
    CHECK(result.groups[0].binding == BindingConstraint::Cube);
    CHECK(result.cubeBoundGroups == 1);
}

TEST_CASE("bindingConstraint: heavy pallets are weight-bound") {
    const auto result = assessBinding(groupsOf({{0}}), {10}, {90000}, trailer());
    CHECK(result.groups[0].binding == BindingConstraint::Weight);
    CHECK(result.weightBoundGroups == 1);
}

TEST_CASE("bindingConstraint: an exact tie is cube-bound") {
    const auto result = assessBinding(groupsOf({{0}}), {30}, {45000}, trailer());
    CHECK(result.groups[0].binding == BindingConstraint::Cube);
}

TEST_CASE("bindingConstraint: an empty group is cube-bound with zero totals") {
    const auto result = assessBinding(groupsOf({{}}), {}, {}, trailer());
    CHECK(result.groups[0].binding == BindingConstraint::Cube);
    CHECK(result.groups[0].totalPallets == 0.0);
}

TEST_CASE("bindingConstraint: results are parallel to groups and each group sums only its lines") {
    const auto result = assessBinding(groupsOf({{0}, {1}}), {10, 10}, {1000, 90000}, trailer());
    REQUIRE(result.groups.size() == 2);
    CHECK(result.groups[0].binding == BindingConstraint::Cube);
    CHECK(result.groups[1].binding == BindingConstraint::Weight);
    CHECK(result.cubeBoundGroups + result.weightBoundGroups == 2);
}

TEST_CASE("bindingConstraint: negative and non-finite line figures are excluded and counted") {
    const auto result = assessBinding(groupsOf({{0, 1, 2, 3, 4}}),
                                      {10, -1, kNaN, 5, 5}, {1000, 100, 100, kInf, -5}, trailer());
    CHECK(result.excludedInvalidLines == 4);
    CHECK(result.groups[0].totalPallets == doctest::Approx(10));
    CHECK(result.groups[0].totalWeightLb == doctest::Approx(1000));
}

TEST_CASE("bindingConstraint: unusable trailer specs are rejected") {
    for (double badWeightLimit : {0.0, -1.0, kNaN, kInf}) {
        TrailerSpec spec = trailer();
        spec.weightLimitLb = badWeightLimit;
        CHECK_THROWS_AS(assessBinding(groupsOf({{0}}), {1}, {1}, spec), std::invalid_argument);
    }
    for (int badPositions : {0, -1}) {
        TrailerSpec spec = trailer();
        spec.stackPositions = badPositions;
        CHECK_THROWS_AS(assessBinding(groupsOf({{0}}), {1}, {1}, spec), std::invalid_argument);
    }
}

TEST_CASE("bindingConstraint: mismatched vectors and out-of-range indices are rejected") {
    CHECK_THROWS_AS(assessBinding(groupsOf({{0}}), {1, 2}, {1}, trailer()), std::invalid_argument);
    CHECK_THROWS_AS(assessBinding(groupsOf({{1}}), {1}, {1}, trailer()), std::invalid_argument);
}

TEST_CASE("bindingConstraint: real demand under strict segregation is almost all cube-bound") {
    PipelineInputs inputs;
    inputs.product_path = "tests/importer/Customer2-Product-Data.csv";
    inputs.demand_path = "tests/importer/Demand-1.json";
    inputs.placeholder_path = "tests/importer/PlaceHolder-1.json";
    const PipelineResult run = Pipeline::run(inputs);
    const M2Params params = loadParams("config/orderBuilderParams.json");
    REQUIRE(params.trailers.size() == 1);

    const auto segregation = segregate(run.join.lines, run.demand.dnm, SegregationReading::Strict);
    const auto result = assessBinding(segregation, run.pallets_per_line, run.weight_per_line,
                                      params.trailers[0]);
    CHECK(result.groups.size() == 387);
    CHECK(result.excludedInvalidLines == 0);
    CHECK(result.cubeBoundGroups == 383);
    CHECK(result.weightBoundGroups == 4);
}
