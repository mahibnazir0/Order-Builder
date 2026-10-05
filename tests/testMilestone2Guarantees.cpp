#include "doctest.h"
#include "importer/crossDayFixtures.hpp"
#include "stackRules.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace ob;
using namespace crossDayTests;

// What Milestone 2 promises, checked on what the pipeline actually produced rather than
// inferred from counts that happen to match.

namespace {

constexpr double kCeilingIn = 108.0;
// Summing ~24,000 pallet figures in a different order moves the last few bits only.
constexpr double kPerLineTolerancePallets = 1e-6;

std::set<std::pair<std::string, std::string>> doNotMixPairs(const DemandFile& demand) {
    std::set<std::pair<std::string, std::string>> pairs;
    for (const auto& pair : demand.dnm) pairs.emplace(pair.planner_snp, pair.locfrno);
    return pairs;
}

struct StackAudit {
    std::size_t stacks = 0;
    std::size_t multiHighStacks = 0;
    std::size_t tooManyLevels = 0;
    std::size_t overCeiling = 0;
    std::size_t overCriLimit = 0;
    std::size_t onBlankCri = 0;
    std::size_t memberOutsideGroup = 0;
    std::size_t unusableLoads = 0;
    double tallestStackIn = 0.0;
};

// Recomputes each stack's height and the weight every level carries straight from the
// unit loads, independently of canStack and of the builder's chain logic.
StackAudit auditStacks(const PipelineResult& run) {
    StackAudit audit;
    const M2Params& params = run.params;
    for (std::size_t groupIndex = 0; groupIndex < run.stacking.groups.size(); ++groupIndex) {
        const auto& groupLines = run.segregation.groups[groupIndex].lineIndices;
        const std::set<std::size_t> members(groupLines.begin(), groupLines.end());
        for (const auto& stack : run.stacking.groups[groupIndex].best.stacks) {
            ++audit.stacks;
            if (stack.lineIndices.size() > 1) ++audit.multiHighStacks;
            if (stack.lineIndices.size() > static_cast<std::size_t>(params.maxStackHeight)) {
                ++audit.tooManyLevels;
            }
            std::vector<UnitLoad> levels;
            for (const std::size_t lineIndex : stack.lineIndices) {
                if (members.count(lineIndex) == 0) ++audit.memberOutsideGroup;
                levels.push_back(buildUnitLoad(run.join.lines[lineIndex], params));
                if (levels.back().error != UnitLoadError::None) ++audit.unusableLoads;
            }
            double stackHeightIn = 0.0;
            for (const auto& level : levels) stackHeightIn += level.heightIn;
            audit.tallestStackIn = std::max(audit.tallestStackIn, stackHeightIn);
            if (stackHeightIn > kCeilingIn) ++audit.overCeiling;

            double weightAboveLb = 0.0;
            for (std::size_t level = levels.size(); level-- > 1;) {
                weightAboveLb += levels[level].weightLb;
                const UnitLoad& carrier = levels[level - 1];
                if (carrier.cri == 0) {
                    if (!params.blankCriIsStackable) ++audit.onBlankCri;
                    continue;
                }
                const double carriedLb = carrier.ownWeightAboveLb + weightAboveLb;
                if (carriedLb > params.cri.safeLimitLb[static_cast<std::size_t>(carrier.cri)]) {
                    ++audit.overCriLimit;
                }
            }
        }
    }
    return audit;
}

void checkStacksObeyRules(const PipelineResult& run) {
    REQUIRE_FALSE(run.params.trailers.empty());
    REQUIRE(run.params.trailers.front().stackHeightCeilingIn == kCeilingIn);
    const StackAudit audit = auditStacks(run);
    CHECK(audit.stacks > 0);
    CHECK(audit.multiHighStacks > 0);
    CHECK(audit.tooManyLevels == 0);
    CHECK(audit.overCeiling == 0);
    CHECK(audit.overCriLimit == 0);
    CHECK(audit.onBlankCri == 0);
    CHECK(audit.memberOutsideGroup == 0);
    CHECK(audit.unusableLoads == 0);
    CHECK(audit.tallestStackIn <= kCeilingIn);
    std::cout << "  stacks=" << audit.stacks << " multiHigh=" << audit.multiHighStacks
              << " tallestIn=" << audit.tallestStackIn << '\n';
}

std::vector<double> stackedPalletsPerLine(const StackingResult& stacking, std::size_t lineCount) {
    std::vector<double> stackedPerLine(lineCount, 0.0);
    for (const auto& group : stacking.groups) {
        for (const auto& stack : group.best.stacks) {
            for (const std::size_t lineIndex : stack.lineIndices) stackedPerLine[lineIndex] += stack.quantity;
        }
    }
    return stackedPerLine;
}

struct LineConservation {
    std::size_t linesNotConserved = 0;
    std::size_t partialPalletLinesConserved = 0;
    double expectedTotal = 0.0;
    double stackedTotal = 0.0;
};

// Each line must put exactly stackedPalletsForLine of its pallet-equivalents into stacks:
// rounded up to whole pallets in the M2 physical mode, the fraction itself otherwise.
LineConservation conservePerLine(const PipelineResult& run, const StackingResult& stacking,
                                 const M2Params& params) {
    const std::vector<double> stackedPerLine = stackedPalletsPerLine(stacking, run.join.lines.size());
    LineConservation conservation;
    for (std::size_t lineIndex = 0; lineIndex < stackedPerLine.size(); ++lineIndex) {
        const double palletEquivalents = run.palletsForStacking[lineIndex];
        const double expected = stackedPalletsForLine(palletEquivalents, params);
        conservation.expectedTotal += expected;
        conservation.stackedTotal += stackedPerLine[lineIndex];
        if (std::fabs(stackedPerLine[lineIndex] - expected) > kPerLineTolerancePallets) {
            ++conservation.linesNotConserved;
        } else if (palletEquivalents != std::floor(palletEquivalents)) {
            ++conservation.partialPalletLinesConserved;
        }
    }
    return conservation;
}

// M1 pallet-equivalents are fractional and must survive segregation and pass 1 unchanged.
void checkPalletEquivalentsConserved(const PipelineResult& run, std::size_t dayIndex) {
    const double inputPallets = run.summary.total_pallet_equiv;
    CHECK(std::fabs(inputPallets - expectedM1::palletEquivalents[dayIndex]) <= 0.05);

    double stackingInput = 0.0;
    for (const double pallets : run.palletsForStacking) stackingInput += pallets;

    double inGroups = 0.0;
    for (const auto& group : run.segregation.groups) {
        for (const std::size_t lineIndex : group.lineIndices) inGroups += run.palletsForStacking[lineIndex];
    }

    double inBinding = 0.0;
    for (const auto& group : run.binding.groups) inBinding += group.totalPallets;

    CHECK(std::fabs(stackingInput - inputPallets) <= 1e-3);
    CHECK(std::fabs(inGroups - inputPallets) <= 1e-3);
    CHECK(std::fabs(inBinding - inputPallets) <= 1e-3);
    CHECK(std::fabs(run.stackReport.totalPallets - inputPallets) <= 1e-3);
    std::cout << "  pallet-equivalents input=" << inputPallets << " groups=" << inGroups << '\n';
}

// Stacks hold whole physical pallets under the shipped config; the same groups restacked
// with stackWholePallets off must hold each line's fractional pallet-equivalents instead.
void checkStackedPalletsConservedPerLine(const PipelineResult& run) {
    REQUIRE(run.params.stackWholePallets);
    CHECK(run.stackReport.unstackedLines.empty());
    CHECK(run.stacking.linesNotStacked() == 0);

    const LineConservation whole = conservePerLine(run, run.stacking, run.params);
    CHECK(whole.linesNotConserved == 0);
    CHECK(whole.partialPalletLinesConserved > 0);
    CHECK(std::fabs(run.stackReport.totalPalletsStacked - whole.expectedTotal) <= 1e-3);
    CHECK(whole.stackedTotal >= run.summary.total_pallet_equiv);

    M2Params fractionalParams = run.params;
    fractionalParams.stackWholePallets = false;
    const StackingResult fractionalStacking =
        buildStacks(run.segregation, run.join.lines, run.palletsForStacking, run.binding,
                    fractionalParams, run.params.trailers.front());
    const LineConservation fractional = conservePerLine(run, fractionalStacking, fractionalParams);
    CHECK(fractional.linesNotConserved == 0);
    CHECK(fractional.partialPalletLinesConserved > 0);
    CHECK(std::fabs(fractional.stackedTotal - run.summary.total_pallet_equiv) <= 1e-3);

    std::cout << "  whole pallets stacked=" << whole.stackedTotal
              << " partialLines=" << whole.partialPalletLinesConserved
              << "; fractional stacked=" << fractional.stackedTotal << '\n';
}

void checkPalletsConserved(const PipelineResult& run, std::size_t dayIndex) {
    checkPalletEquivalentsConserved(run, dayIndex);
    checkStackedPalletsConservedPerLine(run);
}

} // namespace

TEST_CASE("guarantee: under Strict no group holds two flagged planners or mixes flagged with normal" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const auto& run = pipelineRuns()[dayIndex];
        REQUIRE(run.params.doNotMixReading == SegregationReading::Strict);
        const auto pairs = doNotMixPairs(run.demand);
        std::size_t groupsWithTwoFlaggedPlanners = 0;
        std::size_t groupsMixingFlaggedAndNormal = 0;
        for (const auto& group : run.segregation.groups) {
            std::set<std::string> flaggedPlanners;
            std::size_t normalLines = 0;
            for (const std::size_t lineIndex : group.lineIndices) {
                const auto& line = *run.join.lines[lineIndex].str;
                if (pairs.count({line.planner_snp, line.locfrno}) != 0) {
                    flaggedPlanners.insert(line.planner_snp);
                } else {
                    ++normalLines;
                }
            }
            if (flaggedPlanners.size() > 1) ++groupsWithTwoFlaggedPlanners;
            if (!flaggedPlanners.empty() && normalLines > 0) ++groupsMixingFlaggedAndNormal;
        }
        CHECK(groupsWithTwoFlaggedPlanners == 0);
        CHECK(groupsMixingFlaggedAndNormal == 0);
    }
}

TEST_CASE("guarantee: under FlaggedVsNormal flagged lines never share a group with normal stock" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const auto& run = flaggedVsNormalRuns()[dayIndex];
        const auto pairs = doNotMixPairs(run.demand);
        std::size_t groupsMixingFlaggedAndNormal = 0;
        for (const auto& group : run.segregation.groups) {
            std::size_t flaggedLines = 0;
            for (const std::size_t lineIndex : group.lineIndices) {
                const auto& line = *run.join.lines[lineIndex].str;
                if (pairs.count({line.planner_snp, line.locfrno}) != 0) ++flaggedLines;
            }
            if (flaggedLines != 0 && flaggedLines != group.lineIndices.size()) {
                ++groupsMixingFlaggedAndNormal;
            }
        }
        CHECK(groupsMixingFlaggedAndNormal == 0);
    }
}

TEST_CASE("guarantee: every built stack fits the 108 in ceiling and its carriers' CRI limits" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        std::cout << "Cross-day " << dayFiles()[dayIndex].label << " Strict stacks:\n";
        checkStacksObeyRules(pipelineRuns()[dayIndex]);
        std::cout << "Cross-day " << dayFiles()[dayIndex].label << " FlaggedVsNormal stacks:\n";
        checkStacksObeyRules(flaggedVsNormalRuns()[dayIndex]);
    }
}

TEST_CASE("guarantee: pallet-equivalents reach the groups and every line's pallets reach the stacks, whole or fractional" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (std::size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        std::cout << "Cross-day " << dayFiles()[dayIndex].label << " Strict pallets:\n";
        checkPalletsConserved(pipelineRuns()[dayIndex], dayIndex);
        std::cout << "Cross-day " << dayFiles()[dayIndex].label << " FlaggedVsNormal pallets:\n";
        checkPalletsConserved(flaggedVsNormalRuns()[dayIndex], dayIndex);
    }
}
