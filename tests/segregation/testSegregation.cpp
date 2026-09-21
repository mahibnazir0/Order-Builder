#include "doctest.h"
#include "importer.hpp"
#include "product_importer.hpp"
#include "segregation.hpp"

#include <algorithm>
#include <set>

using namespace ob;

namespace {

STRRecord demandLine(const std::string& from, const std::string& to,
                     const std::string& planner, const std::string& shipCondition = "TL") {
    STRRecord record;
    record.locfrno = from;
    record.loctono = to;
    record.planner_snp = planner;
    record.ship_cond = shipCondition;
    return record;
}

std::vector<JoinedLine> joinedLines(const std::vector<STRRecord>& demand) {
    std::vector<JoinedLine> lines(demand.size());
    for (std::size_t i = 0; i < demand.size(); ++i) lines[i].str = &demand[i];
    return lines;
}

std::size_t totalGroupedLines(const SegregationResult& result) {
    std::size_t total = 0;
    for (const auto& group : result.groups) total += group.lineIndices.size();
    return total;
}

const DoNotMixPair pairS1At2027{"S1", "2027"};

} // namespace

TEST_CASE("segregation: empty demand gives an empty result") {
    const auto result = segregate({}, {pairS1At2027}, SegregationReading::Strict);
    CHECK(result.groups.empty());
    CHECK(result.linesIn == 0);
    CHECK(result.doNotMixPairsLoaded == 1);
    CHECK(result.doNotMixPairsWithDemand == 0);
}

TEST_CASE("segregation: without pairs each lane is one group") {
    const std::vector<STRRecord> demand{demandLine("2027", "2500", "S1"),
                                        demandLine("2027", "2500", "S2"),
                                        demandLine("2027", "2600", "S1")};
    const auto result = segregate(joinedLines(demand), {}, SegregationReading::Strict);
    CHECK(result.groups.size() == 2);
    CHECK(result.lanesIn == 2);
    CHECK(result.lanesSplit == 0);
    CHECK(result.linesSegregated == 0);
    CHECK(result.groups[0].splitReason == SplitReason::None);
}

TEST_CASE("segregation: ship condition is part of the lane") {
    const std::vector<STRRecord> demand{demandLine("2027", "2500", "S1", "TL"),
                                        demandLine("2027", "2500", "S1", "TF")};
    const auto result = segregate(joinedLines(demand), {}, SegregationReading::Strict);
    CHECK(result.lanesIn == 2);
    CHECK(result.groups.size() == 2);
}

TEST_CASE("segregation: strict keeps each flagged planner apart from normal stock") {
    const std::vector<DoNotMixPair> pairs{{"S1", "2027"}, {"S2", "2027"}};
    const std::vector<STRRecord> demand{demandLine("2027", "2500", "S1"),
                                        demandLine("2027", "2500", "S2"),
                                        demandLine("2027", "2500", "S9")};
    const auto result = segregate(joinedLines(demand), pairs, SegregationReading::Strict);
    REQUIRE(result.groups.size() == 3);
    CHECK(result.lanesIn == 1);
    CHECK(result.lanesSplit == 1);
    CHECK(result.linesSegregated == 2);
    CHECK_FALSE(result.groups[0].key.isSegregated);
    CHECK(result.groups[0].splitReason == SplitReason::None);
    CHECK(result.groups[1].key.segregant == "S1");
    CHECK(result.groups[2].key.segregant == "S2");
    CHECK(result.groups[2].splitReason == SplitReason::DoNotMix);
}

TEST_CASE("segregation: flagged-vs-normal puts every flagged planner in one group") {
    const std::vector<DoNotMixPair> pairs{{"S1", "2027"}, {"S2", "2027"}};
    const std::vector<STRRecord> demand{demandLine("2027", "2500", "S1"),
                                        demandLine("2027", "2500", "S2"),
                                        demandLine("2027", "2500", "S9")};
    const auto result = segregate(joinedLines(demand), pairs, SegregationReading::FlaggedVsNormal);
    REQUIRE(result.groups.size() == 2);
    CHECK_FALSE(result.groups[0].key.isSegregated);
    CHECK(result.groups[1].key.isSegregated);
    CHECK(result.groups[1].key.segregant.empty());
    CHECK(result.groups[1].lineIndices.size() == 2);
}

TEST_CASE("segregation: a pair only flags its planner at its own origin") {
    const std::vector<STRRecord> demand{demandLine("2028", "2500", "S1")};
    const auto result = segregate(joinedLines(demand), {pairS1At2027}, SegregationReading::Strict);
    CHECK(result.linesSegregated == 0);
    CHECK(result.doNotMixPairsWithDemand == 0);
    CHECK_FALSE(result.groups[0].key.isSegregated);
}

TEST_CASE("segregation: a blank-planner pair never flags a blank-planner line") {
    const std::vector<STRRecord> demand{demandLine("2027", "2500", "")};
    const auto result = segregate(joinedLines(demand), {{"", "2027"}}, SegregationReading::Strict);
    CHECK(result.linesSegregated == 0);
    CHECK_FALSE(result.groups[0].key.isSegregated);
}

TEST_CASE("segregation: a planner named like the normal group cannot merge into it") {
    const std::vector<STRRecord> demand{demandLine("2027", "2500", "S1"),
                                        demandLine("2027", "2500", "S9")};
    const auto result = segregate(joinedLines(demand), {pairS1At2027}, SegregationReading::Strict);
    CHECK(result.groups.size() == 2);
}

TEST_CASE("segregation: pairs with demand counts distinct matches, not lines") {
    const std::vector<DoNotMixPair> pairs{{"S1", "2027"}, {"S2", "2027"}, {"S3", "2027"}};
    const std::vector<STRRecord> demand{demandLine("2027", "2500", "S1"),
                                        demandLine("2027", "2600", "S1"),
                                        demandLine("2027", "2500", "S2")};
    const auto result = segregate(joinedLines(demand), pairs, SegregationReading::Strict);
    CHECK(result.doNotMixPairsLoaded == 3);
    CHECK(result.doNotMixPairsWithDemand == 2);
    CHECK(result.linesSegregated == 3);
}

TEST_CASE("segregation: every line lands in exactly one group in input order") {
    const std::vector<STRRecord> demand{demandLine("2027", "2500", "S1"),
                                        demandLine("2027", "2500", "S9"),
                                        demandLine("2027", "2500", "S1")};
    const auto result = segregate(joinedLines(demand), {pairS1At2027}, SegregationReading::Strict);
    CHECK(totalGroupedLines(result) == demand.size());
    CHECK(result.groups[1].lineIndices == std::vector<std::size_t>{0, 2});
}

TEST_CASE("segregation: a copied result is independent of the original") {
    const std::vector<STRRecord> demand{demandLine("2027", "2500", "S1")};
    auto original = segregate(joinedLines(demand), {pairS1At2027}, SegregationReading::Strict);
    const auto copy = original;
    original.groups.clear();
    REQUIRE(copy.groups.size() == 1);
    CHECK(copy.groups[0].lineIndices == std::vector<std::size_t>{0});
}

TEST_CASE("segregation: real demand partitions every line into a group") {
    const DemandFile file = Importer::load_demand("tests/importer/Demand-1.json");
    const auto products = ProductImporter::load("tests/importer/Customer2-Product-Data.csv");
    const auto join = Joiner::join(file.str, Joiner::build_index(products.products));
    const auto strict = segregate(join.lines, file.dnm, SegregationReading::Strict);
    const auto flagged = segregate(join.lines, file.dnm, SegregationReading::FlaggedVsNormal);

    CHECK(strict.linesIn == file.str.size());
    CHECK(totalGroupedLines(strict) == file.str.size());
    CHECK(totalGroupedLines(flagged) == file.str.size());
    CHECK(strict.linesSegregated == flagged.linesSegregated);
    CHECK(strict.groups.size() >= flagged.groups.size());
    // Figures measured on the 17 August extract (M2 structure document, Finding D).
    CHECK(strict.lanesIn == 360);
    CHECK(strict.doNotMixPairsWithDemand == 4);
    CHECK(strict.linesSegregated == 2849);
    CHECK(strict.groups.size() == 387);
    CHECK(strict.lanesSplit == 17);
}
