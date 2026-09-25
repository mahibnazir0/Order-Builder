#include "doctest.h"
#include "paramsLoader.hpp"
#include "pipeline.hpp"
#include "segregation.hpp"
#include "stackBuilder.hpp"
#include "stackReporter.hpp"

#include <sstream>

using namespace ob;

namespace {

struct Inputs {
    SegregationResult segregation;
    BindingResult binding;
    StackingResult stacking;
    M2Params params;
};

// One group per entry: key, best stacks (heights as line-index lists) and floor use.
Inputs oneGroup(GroupKey key, std::vector<BuiltStack> stacks, double floorPositions,
                BindingConstraint bound = BindingConstraint::Cube) {
    Inputs inputs;
    SegregatedGroup group;
    group.key = std::move(key);
    group.lineIndices = {0, 1};
    inputs.segregation.groups.push_back(group);
    inputs.segregation.lanesIn = 1;
    GroupBinding groupBinding;
    groupBinding.binding = bound;
    groupBinding.totalPallets = 6.0;
    groupBinding.totalWeightLb = 1200.0;
    inputs.binding.groups.push_back(groupBinding);
    GroupStacking stacking;
    stacking.best.method = StackMethod::BaseAndTop;
    stacking.best.stacks = std::move(stacks);
    stacking.best.floorPositions = floorPositions;
    inputs.stacking.groups.push_back(std::move(stacking));
    return inputs;
}

GroupKey key(const std::string& from, const std::string& to, bool segregated = false,
             const std::string& segregant = "") {
    GroupKey groupKey;
    groupKey.locationFrom = from;
    groupKey.locationTo = to;
    groupKey.shipCondition = "TL";
    groupKey.isSegregated = segregated;
    groupKey.segregant = segregant;
    return groupKey;
}

std::string printed(const Inputs& inputs, std::size_t maxRows = 0) {
    std::ostringstream out;
    StackReporter::print(StackReporter::build(inputs.segregation, inputs.binding, inputs.stacking,
                                              inputs.params), out, maxRows);
    return out.str();
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("stackReporter: defaulted config keys are printed, and none is stated when there are none") {
    Inputs inputs = oneGroup(key("2027", "2500"), {{{0}, 6.0}}, 6.0);
    CHECK(contains(printed(inputs), "Config keys defaulted     none"));
    inputs.params.defaultedKeys = {"pass2AttemptCap", "doNotMixReading"};
    CHECK(contains(printed(inputs), "pass2AttemptCap, doNotMixReading"));
}

TEST_CASE("stackReporter: config warnings are printed") {
    Inputs inputs = oneGroup(key("2027", "2500"), {{{0}, 6.0}}, 6.0);
    inputs.params.warnings = {"footprint defaulted to 48x40"};
    CHECK(contains(printed(inputs), "Config warning            footprint defaulted to 48x40"));
}

TEST_CASE("stackReporter: group rows name the lane, stream, limit and method") {
    const std::string text = printed(oneGroup(key("2027", "2500", true, "S1"), {{{0}, 6.0}}, 6.0,
                                              BindingConstraint::Weight));
    CHECK(contains(text, "2027 -> 2500 TL"));
    CHECK(contains(text, "S1"));
    CHECK(contains(text, "Weight"));
    CHECK(contains(text, "Base & Top"));
}

TEST_CASE("stackReporter: normal and all-planner flagged streams are labelled") {
    CHECK(contains(printed(oneGroup(key("2027", "2500"), {{{0}, 6.0}}, 6.0)), "normal"));
    CHECK(contains(printed(oneGroup(key("2027", "2500", true), {{{0}, 6.0}}, 6.0)),
                   "flagged (all planners)"));
}

TEST_CASE("stackReporter: a group with no stacks says every group ships single-high") {
    const std::string text = printed(oneGroup(key("2027", "2500"), {{{0}, 6.0}}, 6.0));
    CHECK(contains(text, "every group ships single-high"));
    CHECK(contains(text, "1-high 6.0"));
}

TEST_CASE("stackReporter: stack heights are broken down by pallets riding at each height") {
    const std::string text = printed(oneGroup(key("2027", "2500"),
                                              {{{0}, 2.0}, {{0, 1}, 2.0}}, 4.0));
    CHECK(contains(text, "1-high 2.0, 2-high 4.0"));
    CHECK(contains(text, "0 of 1 groups ship entirely single-high"));
}

TEST_CASE("stackReporter: totals come straight from the earlier modules") {
    const StackReport report = StackReporter::build(
        oneGroup(key("2027", "2500"), {{{0, 1}, 3.0}}, 3.0).segregation,
        oneGroup(key("2027", "2500"), {{{0, 1}, 3.0}}, 3.0).binding,
        oneGroup(key("2027", "2500"), {{{0, 1}, 3.0}}, 3.0).stacking, M2Params{});
    CHECK(report.totalPallets == doctest::Approx(6.0));
    CHECK(report.totalFloorPositions == doctest::Approx(3.0));
    CHECK(report.groups == 1);
}

TEST_CASE("stackReporter: the group table can be truncated") {
    Inputs inputs = oneGroup(key("2027", "2500"), {{{0}, 6.0}}, 6.0);
    Inputs second = oneGroup(key("2027", "2600"), {{{0}, 2.0}}, 2.0);
    inputs.segregation.groups.push_back(second.segregation.groups[0]);
    inputs.binding.groups.push_back(second.binding.groups[0]);
    inputs.stacking.groups.push_back(second.stacking.groups[0]);
    const std::string text = printed(inputs, 1);
    CHECK(contains(text, "top 1 of 2"));
    CHECK(contains(text, "1 more groups"));
    CHECK(contains(text, "2027 -> 2500"));
    CHECK_FALSE(contains(text, "2027 -> 2600"));
}

TEST_CASE("stackReporter: an empty result prints without failing") {
    Inputs inputs;
    const std::string text = printed(inputs);
    CHECK(contains(text, "Groups                    0"));
}

TEST_CASE("stackReporter: results for different groups are rejected") {
    Inputs inputs = oneGroup(key("2027", "2500"), {{{0}, 6.0}}, 6.0);
    inputs.binding.groups.clear();
    CHECK_THROWS_AS(StackReporter::build(inputs.segregation, inputs.binding, inputs.stacking,
                                         inputs.params), std::invalid_argument);
}

TEST_CASE("stackReporter: real demand report states the segregation and binding counts") {
    PipelineInputs pipelineInputs;
    pipelineInputs.product_path = "tests/importer/Customer2-Product-Data.csv";
    pipelineInputs.demand_path = "tests/importer/Demand-1.json";
    pipelineInputs.placeholder_path = "tests/importer/PlaceHolder-1.json";
    const PipelineResult run = Pipeline::run(pipelineInputs);
    const M2Params params = loadParams("config/orderBuilderParams.json");
    const auto segregation = segregate(run.join.lines, run.demand.dnm, SegregationReading::Strict);
    const auto binding = assessBinding(segregation, run.pallets_per_line, run.weight_per_line,
                                       params.trailers[0]);
    const auto stacking = buildStacks(segregation, run.join.lines, run.pallets_per_line, binding,
                                      params, params.trailers[0]);

    std::ostringstream out;
    StackReporter::print(StackReporter::build(segregation, binding, stacking, params), out, 10);
    const std::string text = out.str();
    CHECK(contains(text, "Groups                    387"));
    CHECK(contains(text, "split into groups       17"));
    CHECK(contains(text, "Cube-bound groups         383"));
    CHECK(contains(text, "Weight-bound groups       4"));
    CHECK(contains(text, "top 10 of 387"));
}
