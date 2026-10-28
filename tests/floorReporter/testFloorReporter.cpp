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

// A run of the one floor, as a single-date or wholeExtract run has.
FloorRun runOf(const FloorPlan& plan, const DemandSelection& selection) {
    FloorRun run;
    run.selector = parseDemandSelector(plan.shipDate.empty() ? "wholeExtract"
                                                             : "shipDates:" + plan.shipDate);
    run.coverage = coverShipDates(run.selector, {});
    run.floors.push_back({selection, plan});
    return run;
}

string reportOf(const FloorRun& run, const TrailerSpec& trailer = shippedTrailer(),
                TrailerChoice trailerChoice = TrailerChoice::Named, size_t maxLanes = 0,
                const M2Params& params = shippedParams()) {
    ostringstream out;
    printFloorReport(out, run, params, trailer, trailerChoice, "17 Aug", maxLanes);
    return out.str();
}

string reportFor(const FloorPlan& plan, const DemandSelection& selection, size_t maxLanes = 0,
                 const M2Params& params = shippedParams()) {
    return reportOf(runOf(plan, selection), shippedTrailer(), TrailerChoice::Named, maxLanes,
                    params);
}

// Lanes S1 (two groups of half a trailer: floor 2), S2 (half a trailer: floor 1) and S3 (two
// and a half trailers: floor 3), every line shipping 2026-10-02.
struct ThreeLanes {
    HalfTrailerLines fixture{8};
    DemandSelection selection;
    FloorPlan plan;

    explicit ThreeLanes(const M2Params& params = shippedParams()) {
        for (STRRecord& line : fixture.demand) line.datfr_ta = "2026-10-02";
        selection = selectShipDate(fixture.demand, "2026-10-02");
        plan = planFloor(segregation({group("S1", "", {0}), group("S1", "P", {1}),
                                      group("S2", "", {2}), group("S3", "", {3, 4, 5, 6, 7})}),
                         fixture.lines, selection, params, shippedTrailer());
    }
};

PlaceholderRecord placeholder(const string& locationTo, const string& shipDate, int trucks) {
    PlaceholderRecord record;
    record.locfrno = "2027";
    record.loctono = locationTo;
    record.ship_cond = "TL";
    record.datfr_ta = shipDate;
    record.no_of_loads = trucks;
    return record;
}

} // namespace

TEST_CASE("floorReporter: the basis block names every decision behind the figures") {
    const ThreeLanes lanes;
    const string report = reportFor(lanes.plan, lanes.selection);
    const TrailerSpec& trailer = shippedTrailer();
    CHECK(lineContaining(report, "Extract").find("17 Aug") != string::npos);
    CHECK(lineContaining(report, "Demand rule").find("shipDates(2026-10-02)") != string::npos);
    CHECK(lineContaining(report, "Demand rule").find("a floor per ship date") != string::npos);
    CHECK(lineContaining(report, "Ship dates").find("2026-10-02") != string::npos);
    CHECK(lineContaining(report, "as given on the command line") != "");
    CHECK(lineContaining(report, "Trailer").find(trailer.trailerCode) != string::npos);
    CHECK(lineContaining(report, "Weight limit").find(grouped(trailer.weightLimitLb) + " lb")
          != string::npos);
    CHECK(lineContaining(report, "Interior floor").find("630 x 100 in") != string::npos);
    CHECK(lineContaining(report, "Footprint").find("layer size") != string::npos);
    CHECK(lineContaining(report, "Interior floor").find("63,000 sq in") != string::npos);
    CHECK(lineContaining(report, "Stack positions").empty());
    CHECK(lineContaining(report, "Interior height")
              .find(grouped(trailer.stackHeightCeilingIn) + " in") != string::npos);
    CHECK(lineContaining(report, "Max stack depth").find("no limit") != string::npos);
    CHECK(lineContaining(report, "Pallet weight").find("PTL") != string::npos);
    CHECK(lineContaining(report, "source").find(shippedParams().sourcePath) != string::npos);
    CHECK(lineContaining(report, "Deck height").find("floorDeckHeight = Included")
          != string::npos);
    CHECK(lineContaining(report, "client ruling (12 Oct)") != "");
    CHECK(lineContaining(report, "Rounding point").find("floorRoundingPoint = Group")
          != string::npos);
}

TEST_CASE("floorReporter: lines with no layer size are counted beside the cargo volume") {
    const ThreeLanes lanes;
    FloorPlan plan = lanes.plan;
    plan.linesWithoutLayerSize = 7;
    CHECK(lineContaining(reportFor(plan, lanes.selection), "pallet footprint stood in").find("7 line(s)")
          != string::npos);
}

TEST_CASE("floorReporter: the floor is printed beside its ship date, rounding point and trailer") {
    const ThreeLanes lanes;
    const string floorLine = lineContaining(reportFor(lanes.plan, lanes.selection), "  FLOOR ");
    CHECK(floorLine.find(to_string(lanes.plan.floorTrucks) + " trucks") != string::npos);
    CHECK(floorLine.find("2026-10-02") != string::npos);
    CHECK(floorLine.find("rounded group level") != string::npos);
    CHECK(floorLine.find(shippedTrailer().trailerCode) != string::npos);
}

TEST_CASE("floorReporter: every bound term is printed, including the ones that do not bind") {
    const ThreeLanes lanes;
    const string report = reportFor(lanes.plan, lanes.selection);
    CHECK(lineContaining(report, "  weight ").find("binds on 0 group(s)") != string::npos);
    CHECK(lineContaining(report, "  volume ").find("binds on 4 group(s)") != string::npos);
    CHECK(lineContaining(report, "    stack depth").find("not applied") != string::npos);
}

TEST_CASE("floorReporter: a configured depth limit prints its term") {
    TrailerSpec trailer = shippedTrailer();
    trailer.maxStackDepth = 3;
    const ThreeLanes lanes;
    const string report = reportOf(runOf(lanes.plan, lanes.selection), trailer);
    CHECK(lineContaining(report, "    stack depth").find("binds on") != string::npos);
    CHECK(lineContaining(report, "Max stack depth").find("3") != string::npos);
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
    CHECK(rowS1.find("volume") != string::npos);
    CHECK(rowS1.find("  2         0") != string::npos);
}

TEST_CASE("floorReporter: section C names the ship date and rounding its floors were made under") {
    const ThreeLanes lanes;
    const string header =
        lineContaining(reportFor(lanes.plan, lanes.selection), "C. FLOOR BY LANE");
    CHECK(header.find("2026-10-02") != string::npos);
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

TEST_CASE("floorReporter: section A places every line before, on or after the run's dates") {
    HalfTrailerLines fixture(4);
    fixture.demand[0].datfr_ta = "2026-10-01";
    fixture.demand[1].datfr_ta = "2026-10-02";
    fixture.demand[2].datfr_ta = "2026-10-09";
    DemandFile demand;
    demand.str = fixture.demand;
    const FloorRun run = planFloorRun(parseDemandSelector("shipDates:2026-10-02"), demand,
                                      segregation({group("S1", "", {0, 1, 2, 3})}), fixture.lines,
                                      {placeholder("S1", "2026-10-05", 3)}, {false},
                                      shippedParams(), shippedTrailer());
    const string report = reportOf(run);
    CHECK(lineContaining(report, "Lines on the run's dates").find(" 1  (") != string::npos);
    CHECK(lineContaining(report, "before the first date").find(" 1  (") != string::npos);
    CHECK(lineContaining(report, "after the last date").find(" 1  (future demand") != string::npos);
    CHECK(lineContaining(report, "  undated").find(" 1  (") != string::npos);
    CHECK(lineContaining(report, "Reserved, other dates").find("3 truck(s)") != string::npos);
    CHECK(lineContaining(report, "Reserved, other dates").find("set against no floor")
          != string::npos);
    CHECK(lineContaining(report, "Pulling demand forward").find("not applied") != string::npos);
    CHECK(lineContaining(report, "Demand lines selected").find("1 of 4") != string::npos);
    CHECK(lineContaining(report, "grouped, not selected").find("3") != string::npos);
}

TEST_CASE("floorReporter: each ship date gets its own floor section and the total adds them") {
    HalfTrailerLines fixture(2);
    fixture.demand[0].datfr_ta = "2026-10-02";
    fixture.demand[1].datfr_ta = "2026-10-03";
    DemandFile demand;
    demand.str = fixture.demand;
    const FloorRun run = planFloorRun(parseDemandSelector("shipDates:2026-10-02:2026-10-03"),
                                      demand, segregation({group("S1", "", {0, 1})}),
                                      fixture.lines, {}, {}, shippedParams(), shippedTrailer());
    const string report = reportOf(run);
    const size_t firstDay = report.find("B. THE FLOOR - SHIP DATE 2026-10-02");
    const size_t secondDay = report.find("B. THE FLOOR - SHIP DATE 2026-10-03");
    REQUIRE(firstDay != string::npos);
    REQUIRE(secondDay != string::npos);
    CHECK(firstDay < secondDay);
    CHECK(lineContaining(report, "Floor, all ship dates").find("2 trucks") != string::npos);
}

TEST_CASE("floorReporter: the basis names the calendar row a run's ship dates came from") {
    HalfTrailerLines fixture(1);
    fixture.demand[0].datfr_ta = "2026-10-07";
    DemandFile demand;
    demand.str = fixture.demand;
    demand.ctl = {{"", "MONDAY", 2, 3, 0}, {"2026-10-05", "", 2, 4, 0}};
    const FloorRun run = planFloorRun(parseDemandSelector("calendar:2026-10-05"), demand,
                                      segregation({group("S1", "", {0})}), fixture.lines, {}, {},
                                      shippedParams(), shippedTrailer());
    const string report = reportOf(run);
    CHECK(lineContaining(report, "Demand rule").find("calendar(plan 2026-10-05 Monday)")
          != string::npos);
    CHECK(lineContaining(report, "Ship dates").find("2026-10-07, 2026-10-08, 2026-10-09")
          != string::npos);
    CHECK(lineContaining(report, "CTL row 1, dated the plan date: overrides the weekday row")
          != "");
}

TEST_CASE("floorReporter: section B and C set each day's reserved trucks beside its floor") {
    ThreeLanes lanes;
    matchReservations(lanes.plan, {placeholder("S1", "2026-10-02", 5),
                                   placeholder("S9", "2026-10-02", 2)},
                      {false, false});
    const string report = reportFor(lanes.plan, lanes.selection);
    CHECK(lineContaining(report, "  Reserved ").find("7 trucks") != string::npos);
    CHECK(lineContaining(report, "on lanes without demand").find("2 trucks") != string::npos);
    CHECK(lineContaining(report, "lanes floored above it").find(" 2  (") != string::npos);
    const string rowS1 = lineContaining(report, "2027 -> S1 TL");
    CHECK(rowS1.substr(rowS1.size() - 2) == " 5");
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
    CHECK(lineContaining(report, "  FLOOR ").find("rounded lane level") != string::npos);
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
    printFloorReport(out, runOf(plan, selection), shippedParams(), trailer, TrailerChoice::Named,
                     "Extract \xE2\x80\x94 Aug");
    CHECK(isPlainAscii(out.str()));
    CHECK(out.str().find("2027 -> S?? TL") != string::npos);
}

TEST_CASE("floorReporter: an empty plan still prints its basis and a zero floor") {
    const DemandSelection selection = wholeExtract({});
    const FloorPlan plan =
        planFloor(SegregationResult{}, {}, selection, shippedParams(), shippedTrailer());
    const string report = reportFor(plan, selection);
    CHECK(lineContaining(report, "  FLOOR ").find("0 trucks") != string::npos);
    CHECK(lineContaining(report, "Demand rule").find("wholeExtract") != string::npos);
}

TEST_CASE("floorReporter: the floor is printed with the evidence that it is a lower bound") {
    const ThreeLanes lanes;
    const string report = reportFor(lanes.plan, lanes.selection);
    REQUIRE(report.find("a lower bound: checked against Truck Builder") != string::npos);
    CHECK(report.find("a lower bound: checked against Truck Builder") > report.find("  FLOOR "));
    CHECK(report.find("NOT YET VALIDATED") == string::npos);
}

TEST_CASE("floorReporter: the basis says how the trailer was chosen") {
    const ThreeLanes lanes;
    const string largest =
        reportOf(runOf(lanes.plan, lanes.selection), shippedTrailer(), TrailerChoice::Largest);
    CHECK(lineContaining(largest, "Trailer").find("largest listed") != string::npos);
    CHECK(lineContaining(reportFor(lanes.plan, lanes.selection), "Trailer")
              .find("named with --trailer") != string::npos);
}

TEST_CASE("floorReporter: unit loads are labelled as fractional equivalents") {
    const ThreeLanes lanes;
    const string report = reportFor(lanes.plan, lanes.selection);
    CHECK(lineContaining(report, "Unit-load equivalents").find("not physical pallets")
          != string::npos);
    CHECK(report.find("Unit loads") == string::npos);
}

TEST_CASE("floorReporter: a lane with no line counted prints not measurable, never a zero floor") {
    const vector<ProductRecord> products{unitLoadProduct("", 50.0)};
    const vector<STRRecord> demand{palletDemand(5.0)};
    const vector<JoinedLine> lines = joinedLines(products, demand);
    const DemandSelection selection = wholeExtract(lines);
    const FloorPlan plan = planFloor(segregation({group("S1", "", {0})}), lines, selection,
                                     shippedParams(), shippedTrailer());
    const string report = reportFor(plan, selection);
    CHECK(lineContaining(report, "2027 -> S1").find("not measurable") != string::npos);
    CHECK(lineContaining(report, "  not measurable").find(" 1  (every") != string::npos);
}

TEST_CASE("floorReporter: a lane with some lines left out is starred as understated") {
    const vector<ProductRecord> products{unitLoadProduct("TLD", 50.0), unitLoadProduct("", 50.0)};
    const vector<STRRecord> demand{palletDemand(1.0), palletDemand(5.0)};
    const vector<JoinedLine> lines = joinedLines(products, demand);
    const DemandSelection selection = wholeExtract(lines);
    const FloorPlan plan = planFloor(segregation({group("S1", "", {0, 1})}), lines, selection,
                                     shippedParams(), shippedTrailer());
    const string report = reportFor(plan, selection);
    CHECK(lineContaining(report, "2027 -> S1").find("1*") != string::npos);
    CHECK(report.find("* lane has selected lines left out") != string::npos);
    CHECK(lineContaining(report, "  understated").find(" 1  (some") != string::npos);
}

TEST_CASE("floorReporter: a fully counted lane prints its floor with no star or footnote") {
    const ThreeLanes lanes;
    const string report = reportFor(lanes.plan, lanes.selection);
    CHECK(lineContaining(report, "2027 -> S1").find("*") == string::npos);
    CHECK(report.find("* lane has selected lines left out") == string::npos);
}

TEST_CASE("floorReporter: a floor whose selection is for another ship date is a caller error") {
    const ThreeLanes lanes;
    FloorRun run = runOf(lanes.plan, lanes.selection);
    run.floors[0].selection = selectShipDate(lanes.fixture.demand, "2026-10-03");
    ostringstream out;
    CHECK_THROWS_AS(printFloorReport(out, run, shippedParams(), shippedTrailer(),
                                     TrailerChoice::Named, "17 Aug"),
                    invalid_argument);
}

TEST_CASE("floorReporter: the report on every extract is plain ASCII and states each day's floor" * doctest::skip(!allExtractsPresent())) {
    const PerDay<string> planDates{"2026-08-17", "2026-08-31", "2026-09-01", "2026-09-02"};
    for (size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const PipelineResult& run = pipelineRuns()[dayIndex];
        const FloorRun floorRun = planFloorRun(
            parseDemandSelector("calendar:" + planDates[dayIndex]), run.demand, run.segregation,
            run.join.lines, run.placeholders.placeholders,
            Validator::excludedPlaceholderFlags(run.validation, run.placeholders.placeholders.size()),
            run.params, shippedTrailer());
        ostringstream out;
        printFloorReport(out, floorRun, run.params, shippedTrailer(), TrailerChoice::Named,
                         dayFiles()[dayIndex].label);
        const string report = out.str();
        CHECK(isPlainAscii(report));
        size_t searchFrom = 0;
        for (const ShipDateFloor& floor : floorRun.floors) {
            CAPTURE(floor.plan.shipDate);
            const size_t section = report.find("SHIP DATE " + floor.plan.shipDate, searchFrom);
            REQUIRE(section != string::npos);
            const size_t floorLine = report.find("  FLOOR ", section);
            CHECK(report.substr(floorLine, 80)
                      .find(grouped(static_cast<double>(floor.plan.floorTrucks)) + " trucks")
                  != string::npos);
            searchFrom = floorLine;
        }
        CHECK(lineContaining(report, "Floor, all ship dates")
                  .find(grouped(static_cast<double>(floorRun.floorTrucks())) + " trucks")
              != string::npos);
    }
}
