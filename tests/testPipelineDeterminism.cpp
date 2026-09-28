#include "doctest.h"
#include "importer/crossDayFixtures.hpp"

#include <sstream>
#include <string>

using namespace ob;
using namespace crossDayTests;

namespace {

// Exact comparison throughout, doubles included: the same input must give the same
// bits, not merely close figures. Two different stack sets can share a floor total.
std::size_t segregationDifferences(const SegregationResult& first, const SegregationResult& second) {
    std::size_t differences = 0;
    if (first.linesIn != second.linesIn || first.linesSegregated != second.linesSegregated
        || first.lanesIn != second.lanesIn || first.lanesSplit != second.lanesSplit
        || first.doNotMixPairsLoaded != second.doNotMixPairsLoaded
        || first.doNotMixPairsWithDemand != second.doNotMixPairsWithDemand
        || first.groups.size() != second.groups.size()) {
        return 1;
    }
    for (std::size_t i = 0; i < first.groups.size(); ++i) {
        const auto& a = first.groups[i];
        const auto& b = second.groups[i];
        if (a.key.locationFrom != b.key.locationFrom || a.key.locationTo != b.key.locationTo
            || a.key.shipCondition != b.key.shipCondition || a.key.isSegregated != b.key.isSegregated
            || a.key.segregant != b.key.segregant || a.lineIndices != b.lineIndices
            || a.splitReason != b.splitReason || a.sameSiteFlag != b.sameSiteFlag) {
            ++differences;
        }
    }
    return differences;
}

std::size_t bindingDifferences(const BindingResult& first, const BindingResult& second) {
    if (first.cubeBoundGroups != second.cubeBoundGroups
        || first.weightBoundGroups != second.weightBoundGroups
        || first.excludedInvalidLines != second.excludedInvalidLines
        || first.groups.size() != second.groups.size()) {
        return 1;
    }
    std::size_t differences = 0;
    for (std::size_t i = 0; i < first.groups.size(); ++i) {
        const auto& a = first.groups[i];
        const auto& b = second.groups[i];
        if (a.binding != b.binding || a.totalPallets != b.totalPallets
            || a.totalWeightLb != b.totalWeightLb || a.trucksIfWeight != b.trucksIfWeight
            || a.trucksIfCube != b.trucksIfCube) {
            ++differences;
        }
    }
    return differences;
}

bool sameStacks(const StackSet& a, const StackSet& b) {
    if (a.method != b.method || a.floorPositions != b.floorPositions
        || a.heaviestStackLb != b.heaviestStackLb || a.stacks.size() != b.stacks.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.stacks.size(); ++i) {
        if (a.stacks[i].lineIndices != b.stacks[i].lineIndices
            || a.stacks[i].quantity != b.stacks[i].quantity) {
            return false;
        }
    }
    return true;
}

bool sameOutcomes(const std::vector<MethodOutcome>& a, const std::vector<MethodOutcome>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].method != b[i].method || a[i].floorPositions != b[i].floorPositions) return false;
    }
    return true;
}

std::size_t stackingDifferences(const StackingResult& first, const StackingResult& second) {
    if (first.excludedLines.size() != second.excludedLines.size()
        || first.excludedInvalidQuantityLines != second.excludedInvalidQuantityLines
        || first.groups.size() != second.groups.size()) {
        return 1;
    }
    std::size_t differences = 0;
    for (std::size_t i = 0; i < first.groups.size(); ++i) {
        if (!sameStacks(first.groups[i].best, second.groups[i].best)
            || !sameOutcomes(first.groups[i].outcomes, second.groups[i].outcomes)) {
            ++differences;
        }
    }
    return differences;
}

std::string printedReports(const PipelineResult& run) {
    std::ostringstream out;
    Reporter::print_summary(run.summary, out, 0);
    Reporter::print_warnings(run.validation, out);
    StackReporter::print(run.stackReport, out, 0);
    return out.str();
}

} // namespace

TEST_CASE("pipeline: two runs on the same input give identical groups, stacks and reports" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    const PipelineResult& first = pipelineRuns()[0];
    const PipelineResult second = Pipeline::run(dayInputs(0, kStrictParamsPath));
    REQUIRE(second.ranMilestone2);

    CHECK(segregationDifferences(first.segregation, second.segregation) == 0);
    CHECK(bindingDifferences(first.binding, second.binding) == 0);
    CHECK(stackingDifferences(first.stacking, second.stacking) == 0);
    CHECK(first.palletsForStacking == second.palletsForStacking);
    CHECK(first.weightForStacking == second.weightForStacking);

    const std::string firstReport = printedReports(first);
    CHECK(firstReport.size() > 10000);
    CHECK(firstReport == printedReports(second));
}
