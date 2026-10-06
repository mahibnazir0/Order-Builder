#include "doctest.h"
#include "../importer/crossDayFixtures.hpp"
#include "floorPlanner.hpp"
#include "paramsLoader.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;
using namespace crossDayTests;

namespace {

const M2Params& shippedParams() {
    static const M2Params params = loadParams(kStrictParamsPath);
    return params;
}

const TrailerSpec& shippedTrailer() {
    return selectTrailer(shippedParams().trailers, shippedParams().sourcePath, "53FT_NA");
}

M2Params paramsRoundingAt(FloorRoundingPoint roundingPoint) {
    M2Params params = shippedParams();
    params.floorRoundingPoint = roundingPoint;
    return params;
}

// One case per layer and one layer per unit load, so the case height is the unit-load height.
ProductRecord unitLoadProduct(const string& palletId, double unitLoadHeightIn) {
    ProductRecord record;
    record.id = "P";
    record.pallet_id = palletId;
    record.height_in = unitLoadHeightIn;
    record.weight_lb = 1.0;
    record.cases_layer = 1;
    record.layers_unit_load = 1;
    record.cases_unit_load = 1;
    return record;
}

STRRecord palletDemand(double unitLoads) {
    STRRecord record;
    record.matnr = "P";
    record.trans = unitLoads;
    record.unitofmeas = "PAL";
    return record;
}

// Pairs products[i] with demand[i]; the result points into both, so they must outlive it.
vector<JoinedLine> joinedLines(const vector<ProductRecord>& products,
                               const vector<STRRecord>& demand) {
    vector<JoinedLine> lines(demand.size());
    for (size_t lineIndex = 0; lineIndex < demand.size(); ++lineIndex) {
        lines[lineIndex].str = &demand[lineIndex];
        lines[lineIndex].product = &products[lineIndex];
        lines[lineIndex].matched = true;
    }
    return lines;
}

SegregatedGroup group(const string& locationTo, const string& segregant,
                      const vector<size_t>& lineIndices) {
    SegregatedGroup segregatedGroup;
    segregatedGroup.key = {"2027", locationTo, "TL", !segregant.empty(), segregant};
    segregatedGroup.lineIndices = lineIndices;
    return segregatedGroup;
}

// Half a trailer of stacked height per line.
struct HalfTrailerLines {
    vector<ProductRecord> products;
    vector<STRRecord> demand;
    vector<JoinedLine> lines;

    explicit HalfTrailerLines(size_t lineCount) {
        const TrailerSpec& trailer = shippedTrailer();
        products.assign(lineCount, unitLoadProduct("TLD", trailer.stackHeightCeilingIn));
        demand.assign(lineCount, palletDemand(trailer.stackPositions / 2.0));
        lines = joinedLines(products, demand);
    }
    // lines points into this object's own vectors; a copy would point into the original.
    HalfTrailerLines(const HalfTrailerLines&) = delete;
    HalfTrailerLines& operator=(const HalfTrailerLines&) = delete;
};

SegregationResult segregation(const vector<SegregatedGroup>& groups) {
    SegregationResult result;
    result.groups = groups;
    return result;
}

} // namespace

TEST_CASE("floorPlanner: a lane split by segregation is floored per group, never across it") {
    const HalfTrailerLines fixture(2);
    const SegregationResult split =
        segregation({group("S1", "", {0}), group("S1", "PLANNER_A", {1})});
    const FloorPlan plan = planFloor(split, fixture.lines,
                                     paramsRoundingAt(FloorRoundingPoint::Group), shippedTrailer());
    REQUIRE(plan.lanes.size() == 1);
    CHECK(plan.lanes[0].groupIndices == vector<size_t>{0, 1});
    CHECK(plan.groups[0].bound.floorTrucks == 1);
    CHECK(plan.groups[1].bound.floorTrucks == 1);
    CHECK(plan.lanes[0].floorTrucks == 2);
    CHECK(plan.floorTrucks == 2);
}

TEST_CASE("floorPlanner: lane rounding sums fractional group bounds and rounds once") {
    const HalfTrailerLines fixture(2);
    const SegregationResult split =
        segregation({group("S1", "", {0}), group("S1", "PLANNER_A", {1})});
    const FloorPlan plan = planFloor(split, fixture.lines,
                                     paramsRoundingAt(FloorRoundingPoint::Lane), shippedTrailer());
    CHECK(plan.roundingPoint == FloorRoundingPoint::Lane);
    CHECK(plan.lanes[0].boundTrucks == doctest::Approx(1.0));
    CHECK(plan.lanes[0].floorTrucks == 1);
    CHECK(plan.floorTrucks == 1);
    CHECK(plan.groups[0].bound.floorTrucks == 1);
}

TEST_CASE("floorPlanner: group, lane and plan totals agree") {
    const HalfTrailerLines fixture(5);
    const SegregationResult groups = segregation({group("S1", "", {0, 1}), group("S1", "P", {2}),
                                                  group("S2", "", {3}), group("S3", "", {4})});
    const FloorPlan plan = planFloor(groups, fixture.lines, shippedParams(), shippedTrailer());
    REQUIRE(plan.groups.size() == 4);
    REQUIRE(plan.lanes.size() == 3);
    long long groupFloorSum = 0;
    double groupUnitLoads = 0.0;
    for (const GroupFloor& floor : plan.groups) {
        groupFloorSum += floor.bound.floorTrucks;
        groupUnitLoads += floor.totals.unitLoads;
    }
    long long laneFloorSum = 0;
    for (const LaneFloor& lane : plan.lanes) laneFloorSum += lane.floorTrucks;
    CHECK(groupFloorSum == 4);
    CHECK(laneFloorSum == groupFloorSum);
    CHECK(plan.floorTrucks == groupFloorSum);
    CHECK(plan.totals.unitLoads == doctest::Approx(groupUnitLoads));
    CHECK(plan.boundTrucks == doctest::Approx(2.5));
}

TEST_CASE("floorPlanner: lanes are listed in the order of their first group") {
    const HalfTrailerLines fixture(3);
    const SegregationResult groups =
        segregation({group("S2", "", {0}), group("S1", "", {1}), group("S2", "P", {2})});
    const FloorPlan plan = planFloor(groups, fixture.lines, shippedParams(), shippedTrailer());
    REQUIRE(plan.lanes.size() == 2);
    CHECK(plan.lanes[0].locationTo == "S2");
    CHECK(plan.lanes[0].groupIndices == vector<size_t>{0, 2});
    CHECK(plan.lanes[1].locationTo == "S1");
}

TEST_CASE("floorPlanner: an empty segregation is a zero plan with no lanes") {
    const FloorPlan plan = planFloor(SegregationResult{}, {}, shippedParams(), shippedTrailer());
    CHECK(plan.lanes.empty());
    CHECK(plan.groups.empty());
    CHECK(plan.floorTrucks == 0);
    CHECK(plan.noStackingBaselineTrucks == 0);
}

TEST_CASE("floorPlanner: the no-stacking baseline is summed beside the floor, not as it") {
    const TrailerSpec& trailer = shippedTrailer();
    const vector<ProductRecord> products{unitLoadProduct("TLD", trailer.stackHeightCeilingIn / 2)};
    const vector<STRRecord> demand{palletDemand(2.0 * trailer.stackPositions)};
    const vector<JoinedLine> lines = joinedLines(products, demand);
    const FloorPlan plan =
        planFloor(segregation({group("S1", "", {0})}), lines, shippedParams(), trailer);
    CHECK(plan.floorTrucks == 1);
    CHECK(plan.noStackingBaselineTrucks == 2);
}

TEST_CASE("floorPlanner: a line with a metrics error is listed and left out of the totals") {
    const TrailerSpec& trailer = shippedTrailer();
    const vector<ProductRecord> products{unitLoadProduct("TLD", 50.0), unitLoadProduct("", 50.0)};
    const vector<STRRecord> demand{palletDemand(1.0), palletDemand(5.0)};
    const vector<JoinedLine> lines = joinedLines(products, demand);
    const FloorPlan plan =
        planFloor(segregation({group("S1", "", {0, 1})}), lines, shippedParams(), trailer);
    REQUIRE(plan.excludedLines.size() == 1);
    CHECK(plan.excludedLines[0].lineIndex == 1);
    CHECK(plan.excludedLines[0].error == UnitLoadMetricsError::MissingPalletSpec);
    CHECK_FALSE(plan.excludedLines[0].overCeiling);
    CHECK(plan.groups[0].linesCounted == 1);
    CHECK(plan.totals.unitLoads == doctest::Approx(1.0));
}

TEST_CASE("floorPlanner: an unmatched line is listed rather than counted as zero weight") {
    const vector<STRRecord> demand{palletDemand(3.0)};
    vector<JoinedLine> lines(1);
    lines[0].str = &demand[0];
    const FloorPlan plan = planFloor(segregation({group("S1", "", {0})}), lines, shippedParams(),
                                     shippedTrailer());
    REQUIRE(plan.excludedLines.size() == 1);
    CHECK(plan.excludedLines[0].error == UnitLoadMetricsError::MissingProduct);
    CHECK(plan.floorTrucks == 0);
}

TEST_CASE("floorPlanner: a unit load over the ceiling is excluded; one exactly at it counts") {
    const TrailerSpec& trailer = shippedTrailer();
    const vector<ProductRecord> products{
        unitLoadProduct("TLD", trailer.stackHeightCeilingIn),
        unitLoadProduct("TLD", trailer.stackHeightCeilingIn + 0.01)};
    const vector<STRRecord> demand{palletDemand(1.0), palletDemand(1.0)};
    const vector<JoinedLine> lines = joinedLines(products, demand);
    const FloorPlan plan =
        planFloor(segregation({group("S1", "", {0, 1})}), lines, shippedParams(), trailer);
    REQUIRE(plan.excludedLines.size() == 1);
    CHECK(plan.excludedLines[0].lineIndex == 1);
    CHECK(plan.excludedLines[0].overCeiling);
    CHECK(plan.excludedLines[0].error == UnitLoadMetricsError::None);
    CHECK(plan.groups[0].linesCounted == 1);
    CHECK(plan.floorTrucks == 1);
}

TEST_CASE("floorPlanner: a deck-dependent over-height product follows floorDeckHeight") {
    const TrailerSpec& trailer = shippedTrailer();
    const PalletSpec* woodPallet = palletSpecFor(shippedParams().pallets, "PTL");
    REQUIRE(woodPallet != nullptr);
    REQUIRE(woodPallet->addedHeightIn > 0.0);
    const double heightWithoutDeckIn =
        trailer.stackHeightCeilingIn - woodPallet->addedHeightIn / 2.0;
    const vector<ProductRecord> products{unitLoadProduct("PTL", heightWithoutDeckIn)};
    const vector<STRRecord> demand{palletDemand(1.0)};
    const vector<JoinedLine> lines = joinedLines(products, demand);
    const SegregationResult oneGroup = segregation({group("S1", "", {0})});

    M2Params params = shippedParams();
    params.floorDeckHeight = DeckHeightRule::Excluded;
    CHECK(planFloor(oneGroup, lines, params, trailer).excludedLines.empty());
    params.floorDeckHeight = DeckHeightRule::Included;
    const FloorPlan withDeck = planFloor(oneGroup, lines, params, trailer);
    REQUIRE(withDeck.excludedLines.size() == 1);
    CHECK(withDeck.excludedLines[0].overCeiling);
}

TEST_CASE("floorPlanner: lines whose cases per unit load disagree with the layers are counted") {
    ProductRecord mismatched = unitLoadProduct("TLD", 50.0);
    mismatched.cases_unit_load = 2;
    const vector<ProductRecord> products{mismatched};
    const vector<STRRecord> demand{palletDemand(1.0)};
    const vector<JoinedLine> lines = joinedLines(products, demand);
    const FloorPlan plan = planFloor(segregation({group("S1", "", {0})}), lines, shippedParams(),
                                     shippedTrailer());
    CHECK(plan.casesPerUnitLoadMismatchLines == 1);
    CHECK(plan.excludedLines.empty());
}

TEST_CASE("floorPlanner: a group line index outside the lines is a caller error") {
    const HalfTrailerLines fixture(1);
    CHECK_THROWS_AS(planFloor(segregation({group("S1", "", {1})}), fixture.lines, shippedParams(),
                              shippedTrailer()),
                    invalid_argument);
}

TEST_CASE("floorPlanner: an unusable trailer is rejected through floorBound") {
    const HalfTrailerLines fixture(1);
    TrailerSpec trailer = shippedTrailer();
    trailer.stackPositions = 0;
    CHECK_THROWS_AS(planFloor(segregation({group("S1", "", {0})}), fixture.lines, shippedParams(),
                              trailer),
                    invalid_argument);
}

TEST_CASE("floorPlanner: rounding points have stable plain-ASCII names") {
    CHECK(string(floorRoundingPointName(FloorRoundingPoint::Group)) == "group");
    CHECK(string(floorRoundingPointName(FloorRoundingPoint::Lane)) == "lane");
}

TEST_CASE("floorPlanner: the plan on every extract reconciles with Milestone 1 and 2" * doctest::skip(!allExtractsPresent())) {
    for (size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const PipelineResult& run = pipelineRuns()[dayIndex];
        const FloorPlan plan =
            planFloor(run.segregation, run.join.lines, run.params, shippedTrailer());

        CHECK(plan.excludedLines.empty());
        CHECK(plan.groups.size() == expectedM2::strictGroups[dayIndex]);
        CHECK(plan.lanes.size() == expectedM2::lanesWithDemand[dayIndex]);
        size_t lanesSplit = 0;
        long long laneFloorSum = 0;
        for (const LaneFloor& lane : plan.lanes) {
            if (lane.groupIndices.size() > 1) ++lanesSplit;
            laneFloorSum += lane.floorTrucks;
        }
        CHECK(lanesSplit == expectedM2::strictLanesSplit);
        long long groupFloorSum = 0;
        for (const GroupFloor& floor : plan.groups) {
            CHECK(floor.linesCounted > 0);
            CHECK(floor.bound.floorTrucks > 0);
            groupFloorSum += floor.bound.floorTrucks;
        }
        CHECK(plan.floorTrucks == groupFloorSum);
        CHECK(plan.floorTrucks == laneFloorSum);
        CHECK(fabs(plan.totals.unitLoads - expectedM1::palletEquivalents[dayIndex]) <= 0.05);
        CHECK(fabs(plan.totals.totalWeightLb - expectedM1::totalWeightLb[dayIndex]) <= 0.5);
    }
}

TEST_CASE("floorPlanner: rounding per lane never exceeds rounding per group on any extract" * doctest::skip(!allExtractsPresent())) {
    for (size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const PipelineResult& run = pipelineRuns()[dayIndex];
        const FloorPlan perGroup = planFloor(run.segregation, run.join.lines,
                                             paramsRoundingAt(FloorRoundingPoint::Group),
                                             shippedTrailer());
        const FloorPlan perLane = planFloor(run.segregation, run.join.lines,
                                            paramsRoundingAt(FloorRoundingPoint::Lane),
                                            shippedTrailer());
        CHECK(perLane.floorTrucks <= perGroup.floorTrucks);
        CHECK(perLane.boundTrucks == doctest::Approx(perGroup.boundTrucks));
    }
}
