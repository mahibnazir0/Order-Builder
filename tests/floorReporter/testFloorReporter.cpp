#include "doctest.h"
#include "../floorPlanner/floorFixtures.hpp"
#include "floorReporter.hpp"
#include "reportFormat.hpp"

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;
using namespace crossDayTests;
using namespace floorTests;

namespace {

bool isPlainAscii(const string& text) {
    for (const char character : text) {
        const auto code = static_cast<unsigned char>(character);
        if (character != '\n' && (code < 0x20 || code > 0x7e)) return false;
    }
    return true;
}

string lineContaining(const string& report, const string& marker) {
    istringstream lines(report);
    string line;
    while (getline(lines, line)) {
        if (line.find(marker) != string::npos) return line;
    }
    return "";
}

string reportFor(const FloorPlan& plan, const DemandSelection& selection, size_t maxLanes = 0,
                 const M2Params& params = shippedParams()) {
    ostringstream out;
    printFloorReport(out, plan, selection, params, shippedTrailer(), "17 Aug", maxLanes);
    return out.str();
}

// Lanes S1 (two groups of half a trailer: floor 2), S2 (half a trailer: floor 1) and S3 (two
// and a half trailers: floor 3), every line due 2026-10-02.
struct ThreeLanes {
    HalfTrailerLines fixture{8};
    DemandSelection selection;
    FloorPlan plan;

    explicit ThreeLanes(const M2Params& params = shippedParams()) {
        for (STRRecord& line : fixture.demand) line.datto_ta = "2026-10-02";
        selection = selectDemand(fixture.demand, parseDemandSelector("dueBy:2026-10-02"));
        plan = planFloor(segregation({group("S1", "", {0}), group("S1", "P", {1}),
                                      group("S2", "", {2}), group("S3", "", {3, 4, 5, 6, 7})}),
                         fixture.lines, selection, params, shippedTrailer());
    }
};

} // namespace

TEST_CASE("floorReporter: the basis block names every decision behind the figures") {
    const ThreeLanes lanes;
    const string report = reportFor(lanes.plan, lanes.selection);
    const TrailerSpec& trailer = shippedTrailer();
    CHECK(lineContaining(report, "Extract").find("17 Aug") != string::npos);
    CHECK(lineContaining(report, "Demand rule").find("dueBy(2026-10-02)") != string::npos);
    CHECK(lineContaining(report, "Trailer").find(trailer.trailerCode) != string::npos);
    CHECK(lineContaining(report, "Weight limit").find(grouped(trailer.weightLimitLb) + " lb")
          != string::npos);
    CHECK(lineContaining(report, "Stack positions").find(to_string(trailer.stackPositions))
          != string::npos);
    CHECK(lineContaining(report, "Interior height")
              .find(grouped(trailer.stackHeightCeilingIn) + " in") != string::npos);
    CHECK(lineContaining(report, "Max stack depth").find("no limit") != string::npos);
    CHECK(lineContaining(report, "Pallet weight").find("PTL") != string::npos);
    CHECK(lineContaining(report, "source").find(shippedParams().sourcePath) != string::npos);
    CHECK(lineContaining(report, "Deck height").find("floorDeckHeight = Excluded")
          != string::npos);
    CHECK(lineContaining(report, "Rounding point").find("floorRoundingPoint = Group")
          != string::npos);
}

TEST_CASE("floorReporter: the floor is printed beside its rule, rounding point and trailer") {
    const ThreeLanes lanes;
    const string floorLine = lineContaining(reportFor(lanes.plan, lanes.selection), "FLOOR ");
    CHECK(floorLine.find(to_string(lanes.plan.floorTrucks) + " trucks") != string::npos);
    CHECK(floorLine.find("dueBy(2026-10-02)") != string::npos);
    CHECK(floorLine.find("rounded group level") != string::npos);
    CHECK(floorLine.find(shippedTrailer().trailerCode) != string::npos);
}

TEST_CASE("floorReporter: every bound term is printed, including the ones that do not bind") {
    const ThreeLanes lanes;
    const string report = reportFor(lanes.plan, lanes.selection);
    CHECK(lineContaining(report, "  weight ").find("binds on 0 group(s)") != string::npos);
    CHECK(lineContaining(report, "stacked height  ").find("binds on 4 group(s)") != string::npos);
    CHECK(lineContaining(report, "    stack depth").find("not applied") != string::npos);
}

TEST_CASE("floorReporter: a configured depth limit prints its term") {
    TrailerSpec trailer = shippedTrailer();
    trailer.maxStackDepth = 3;
    const ThreeLanes lanes;
    ostringstream out;
    printFloorReport(out, lanes.plan, lanes.selection, shippedParams(), trailer, "17 Aug");
    CHECK(lineContaining(out.str(), "    stack depth").find("binds on") != string::npos);
    CHECK(lineContaining(out.str(), "Max stack depth").find("3") != string::npos);
}

TEST_CASE("floorReporter: the no-stacking baseline is labelled as not being a floor") {
    const ThreeLanes lanes;
    const string baseline =
        lineContaining(reportFor(lanes.plan, lanes.selection), "No-stacking baseline");
    CHECK(baseline.find(to_string(lanes.plan.noStackingBaselineTrucks) + " trucks")
          != string::npos);
    CHECK(baseline.find("NOT a floor") != string::npos);
}

TEST_CASE("floorReporter: lanes are listed largest floor first with their groups and binding") {
    const ThreeLanes lanes;
    const string report = reportFor(lanes.plan, lanes.selection);
    const size_t laneS3 = report.find("2027 -> S3 TL");
    const size_t laneS1 = report.find("2027 -> S1 TL");
    const size_t laneS2 = report.find("2027 -> S2 TL");
    REQUIRE(laneS3 != string::npos);
    CHECK(laneS3 < laneS1);
    CHECK(laneS1 < laneS2);
    const string rowS1 = lineContaining(report, "2027 -> S1 TL");
    CHECK(rowS1.find("stacked_height") != string::npos);
    CHECK(rowS1.substr(rowS1.size() - 2) == " 2");
}

TEST_CASE("floorReporter: section C names the rule and rounding its floors were made under") {
    const ThreeLanes lanes;
    const string header =
        lineContaining(reportFor(lanes.plan, lanes.selection), "C. FLOOR BY LANE");
    CHECK(header.find("dueBy(2026-10-02)") != string::npos);
    CHECK(header.find("rounded group level") != string::npos);
}

TEST_CASE("floorReporter: maxLanes limits the lane table and says how many are hidden") {
    const ThreeLanes lanes;
    const string report = reportFor(lanes.plan, lanes.selection, 1);
    CHECK(report.find("2027 -> S3 TL") != string::npos);
    CHECK(report.find("2027 -> S2 TL") == string::npos);
    CHECK(report.find("top 1 of 3 by floor") != string::npos);
    CHECK(report.find("... 2 more lane(s)") != string::npos);
}

TEST_CASE("floorReporter: lines left out of the floor are listed with their reason") {
    const TrailerSpec& trailer = shippedTrailer();
    const vector<ProductRecord> products{unitLoadProduct("TLD", 50.0), unitLoadProduct("", 50.0),
                                         unitLoadProduct("TLD", trailer.stackHeightCeilingIn + 1)};
    const vector<STRRecord> demand{palletDemand(1.0), palletDemand(1.0), palletDemand(1.0)};
    const vector<JoinedLine> lines = joinedLines(products, demand);
    const DemandSelection selection = wholeExtract(lines);
    const FloorPlan plan = planFloor(segregation({group("S1", "", {0, 1, 2})}), lines, selection,
                                     shippedParams(), trailer);
    const string report = reportFor(plan, selection);
    CHECK(lineContaining(report, "Lines left out of floor").find("2") != string::npos);
    CHECK(report.find("line 1: missing_pallet_spec") != string::npos);
    CHECK(report.find("line 2: unit load taller than the trailer ceiling") != string::npos);
}

TEST_CASE("floorReporter: undated and unselected lines are counted beside the selection") {
    HalfTrailerLines fixture(3);
    fixture.demand[0].datto_ta = "2026-10-02";
    fixture.demand[1].datto_ta = "2026-10-09";
    const DemandSelection selection =
        selectDemand(fixture.demand, parseDemandSelector("dueBy:2026-10-02"));
    const FloorPlan plan = planFloor(segregation({group("S1", "", {0, 1, 2})}), fixture.lines,
                                     selection, shippedParams(), shippedTrailer());
    const string report = reportFor(plan, selection);
    CHECK(lineContaining(report, "Demand lines selected").find("1 of 3") != string::npos);
    CHECK(lineContaining(report, "undated").find("1") != string::npos);
    CHECK(lineContaining(report, "grouped, not selected").find("2") != string::npos);
}

TEST_CASE("floorReporter: the deck-height and lane-rounding readings are shown when chosen") {
    M2Params params = paramsRoundingAt(FloorRoundingPoint::Lane);
    params.floorDeckHeight = DeckHeightRule::Included;
    const ThreeLanes lanes(params);
    const string report = reportFor(lanes.plan, lanes.selection, 0, params);
    CHECK(lineContaining(report, "Deck height").find("floorDeckHeight = Included")
          != string::npos);
    CHECK(lineContaining(report, "Rounding point").find("floorRoundingPoint = Lane")
          != string::npos);
    CHECK(lineContaining(report, "FLOOR ").find("rounded lane level") != string::npos);
}

TEST_CASE("floorReporter: text from the input files is printed as plain ASCII") {
    const TrailerSpec& trailer = shippedTrailer();
    const vector<ProductRecord> products{unitLoadProduct("TLD", 50.0)};
    const vector<STRRecord> demand{palletDemand(1.0)};
    const vector<JoinedLine> lines = joinedLines(products, demand);
    const DemandSelection selection = wholeExtract(lines);
    const FloorPlan plan = planFloor(segregation({group("S\xC3\xA9", "", {0})}), lines, selection,
                                     shippedParams(), trailer);
    ostringstream out;
    printFloorReport(out, plan, selection, shippedParams(), trailer, "Extract \xE2\x80\x94 Aug");
    CHECK(isPlainAscii(out.str()));
    CHECK(out.str().find("2027 -> S?? TL") != string::npos);
}

TEST_CASE("floorReporter: an empty plan still prints its basis and a zero floor") {
    const DemandSelection selection = wholeExtract({});
    const FloorPlan plan =
        planFloor(SegregationResult{}, {}, selection, shippedParams(), shippedTrailer());
    const string report = reportFor(plan, selection);
    CHECK(lineContaining(report, "FLOOR ").find("0 trucks") != string::npos);
    CHECK(lineContaining(report, "Demand rule").find("wholeExtract") != string::npos);
}

TEST_CASE("floorReporter: a selection that is not the plan's is a caller error") {
    const ThreeLanes lanes;
    const DemandSelection otherRule =
        selectDemand(lanes.fixture.demand, parseDemandSelector("dueBy:2026-10-03"));
    ostringstream out;
    CHECK_THROWS_AS(printFloorReport(out, lanes.plan, otherRule, shippedParams(), shippedTrailer(),
                                     "17 Aug"),
                    invalid_argument);
}

TEST_CASE("floorReporter: the report on every extract is plain ASCII and states its floor" * doctest::skip(!allExtractsPresent())) {
    for (size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const PipelineResult& run = pipelineRuns()[dayIndex];
        const DemandSelection selection =
            selectDemand(run.demand.str, parseDemandSelector("wholeExtract"));
        const FloorPlan plan =
            planFloor(run.segregation, run.join.lines, selection, run.params, shippedTrailer());
        ostringstream out;
        printFloorReport(out, plan, selection, run.params, shippedTrailer(),
                         dayFiles()[dayIndex].label);
        const string report = out.str();
        CHECK(isPlainAscii(report));
        CHECK(lineContaining(report, "FLOOR ")
                  .find(grouped(static_cast<double>(plan.floorTrucks)) + " trucks")
              != string::npos);
        CHECK(lineContaining(report, "Lanes ")
                  .find(grouped(static_cast<double>(plan.lanes.size()))) != string::npos);
    }
}
