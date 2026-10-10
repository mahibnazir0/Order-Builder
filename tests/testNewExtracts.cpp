#include "doctest.h"
#include "importer/crossDayFixtures.hpp"
#include "stackRules.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>

using namespace std;

using namespace ob;
using namespace crossDayTests::newExtracts;

// The 29 Sep - 5 Oct extracts end to end: UUID-named demand and placeholder files, the
// 88-column master with its blank shell block, and the pallet table. A change in any of
// those shapes fails here instead of waiting for the next hand verification.

namespace {

const bool kNoNewExtracts = !anyDayPresent();

void forEachPresentDay(const function<void(size_t, const PipelineResult&)>& check) {
    for (size_t day = 0; day < kDayCount; ++day) {
        if (!dayPresent(day)) continue;
        CAPTURE(labels[day]);
        check(day, *run(day));
    }
}

size_t headerColumns(const string& csvPath) {
    ifstream in(csvPath);
    string header;
    getline(in, header);
    return static_cast<size_t>(count(header.begin(), header.end(), ',')) + 1;
}

using PlannerSite = pair<string, string>;

map<PlannerSite, size_t> flaggedLinesByPair(const PipelineResult& result) {
    set<PlannerSite> pairs;
    for (const auto& pair : result.demand.dnm) pairs.emplace(pair.planner_snp, pair.locfrno);
    map<PlannerSite, size_t> counts;
    for (const auto& joined : result.join.lines) {
        const PlannerSite plannerAtSite{joined.str->planner_snp, joined.str->locfrno};
        if (pairs.count(plannerAtSite) != 0) ++counts[plannerAtSite];
    }
    return counts;
}

} // namespace

// The tests below skip a day whose confidential files are absent, so this one fails instead:
// a clone without them must never pass a suite that ran none of them.
TEST_CASE("new extracts: every day's fixtures are present, so the extract tests ran") {
    for (size_t day = 0; day < kDayCount; ++day) {
        if (!dayPresent(day)) {
            FAIL_CHECK(labels[day] << ": " << directories[day]
                       << " is incomplete (confidential and gitignored; see README, Test data)");
        }
    }
}

TEST_CASE("new extracts: each day's files load with the measured shape" * doctest::skip(kNoNewExtracts)) {
    forEachPresentDay([](size_t day, const PipelineResult& result) {
        CHECK(headerColumns(productPath(day)) == expected::masterColumns);
        CHECK(result.products.rows_read == expected::productRows[day]);
        CHECK(result.products.misalignedRows == 0);
        CHECK(result.products.duplicate_ids == expected::duplicatedProductIds);
        CHECK(result.demand.str.size() == expected::demandLines[day]);
        CHECK(result.join.unmatched_lines == 0);
        CHECK(result.placeholders.placeholders.size() == expected::placeholderEntries[day]);
        CHECK(result.summary.trucks_requested == expected::trucksRequested[day]);
        CHECK(result.validation.errors == 0);
        cout << "New extract " << labels[day] << ": lines=" << result.demand.str.size()
             << " placeholders=" << result.placeholders.placeholders.size()
             << " trucks=" << result.summary.trucks_requested
             << " masterRows=" << result.products.rows_read << '\n';
    });
}

TEST_CASE("new extracts: lanes, groups, split lanes and segregated lines" * doctest::skip(kNoNewExtracts)) {
    forEachPresentDay([](size_t day, const PipelineResult& result) {
        REQUIRE(result.ranMilestone2);
        CHECK(static_cast<size_t>(result.summary.lanes_with_demand) == expected::lanesWithDemand[day]);
        CHECK(result.segregation.lanesIn == expected::lanesWithDemand[day]);
        CHECK(result.segregation.groups.size() == expected::strictGroups[day]);
        CHECK(result.segregation.lanesSplit == expected::lanesSplit[day]);
        CHECK(result.segregation.linesSegregated == expected::linesSegregated[day]);
        cout << "New extract " << labels[day] << " M2: lanes=" << result.segregation.lanesIn
             << " groups=" << result.segregation.groups.size()
             << " split=" << result.segregation.lanesSplit
             << " segregated=" << result.segregation.linesSegregated << '\n';
    });
}

TEST_CASE("new extracts: every demand line is stacked, none over-height or over its own CRI" * doctest::skip(kNoNewExtracts)) {
    forEachPresentDay([](size_t, const PipelineResult& result) {
        CHECK(isRunComplete(result));
        CHECK(result.missingPalletIds.empty());
        CHECK(result.stackReport.unstackedLines.empty());
        CHECK(result.stacking.overHeightLines.empty());
        CHECK(result.stacking.ownCriExceededLines.empty());
    });
}

TEST_CASE("new extracts: the master's own pallet figures cover every demanded line" * doctest::skip(kNoNewExtracts)) {
    forEachPresentDay([](size_t, const PipelineResult& result) {
        M2Params withoutTable = result.params;
        withoutTable.pallets.clear();
        CHECK(missingPalletIds(result.join.lines, withoutTable).empty());

        // A wood pallet weighs what the master says (65 lb), not the 60 lb the config once held.
        const auto woodLine = find_if(result.join.lines.begin(), result.join.lines.end(),
            [](const JoinedLine& line) { return line.product != nullptr && line.product->pallet_id == "PTL"; });
        REQUIRE(woodLine != result.join.lines.end());
        const ProductRecord& product = *woodLine->product;
        REQUIRE(product.palletWeightLb);
        CHECK(*product.palletWeightLb == 65.0);
        const UnitLoad load = buildUnitLoad(*woodLine, withoutTable);
        REQUIRE(load.error == UnitLoadError::None);
        CHECK(load.weightLb == doctest::Approx(product.weight_lb * product.cases_unit_load + 65.0));
    });
}

TEST_CASE("new extracts: 4 of the 22 do-not-mix pairs have demand and account for every segregated line" * doctest::skip(kNoNewExtracts)) {
    const set<PlannerSite> livePairs{{"S45", "2028"}, {"S01", "2028"}, {"S03", "2028"}, {"S20", "2027"}};
    forEachPresentDay([&livePairs](size_t day, const PipelineResult& result) {
        CHECK(result.demand.dnm.size() == expected::doNotMixPairs);
        CHECK(result.segregation.doNotMixPairsWithDemand == expected::doNotMixPairsWithDemand);
        const auto counts = flaggedLinesByPair(result);
        set<PlannerSite> pairsWithDemand;
        size_t flaggedTotal = 0;
        for (const auto& entry : counts) {
            pairsWithDemand.insert(entry.first);
            flaggedTotal += entry.second;
        }
        CHECK(pairsWithDemand == livePairs);
        CHECK(flaggedTotal == expected::linesSegregated[day]);
    });
}

TEST_CASE("new extracts: the shell block and 106052500 load but can never form a unit load" * doctest::skip(kNoNewExtracts)) {
    forEachPresentDay([](size_t, const PipelineResult& result) {
        const auto& products = result.products.products;
        size_t shellRows = 0;
        for (const auto& product : products) {
            if (!product.pallet_id.empty()) continue;
            ++shellRows;
            CHECK(product.cases_unit_load <= 0);
        }
        CHECK(shellRows > 0);
        // The shell block plus 106052500, the one real product with zero counts.
        CHECK(static_cast<size_t>(result.products.rowsWithoutUnitLoad) == shellRows + 1);
        const size_t shellLinesDemanded = static_cast<size_t>(count_if(
            result.join.lines.begin(), result.join.lines.end(), [](const JoinedLine& line) {
                return line.product != nullptr && line.product->pallet_id.empty(); }));
        CHECK(shellLinesDemanded == 0);

        const auto zeroCount = find_if(products.begin(), products.end(),
            [](const ProductRecord& product) { return product.id == "106052500"; });
        REQUIRE(zeroCount != products.end());
        CHECK(zeroCount->cases_unit_load == 0);
        JoinedLine line;
        line.product = &*zeroCount;
        line.matched = true;
        CHECK(buildUnitLoad(line, result.params).error == UnitLoadError::InvalidData);
    });
}
