#include "doctest.h"
#include "../importer/crossDayFixtures.hpp"
#include "pipeline.hpp"
#include "reservationBook.hpp"
#include "selectionItem.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;

namespace {

const string kProductHeader = "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
                              "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n";

// 10 in cases, 4 to a layer, 2 layers: a 20 in unit load of 8 cases at 2 lb, on a TLD
// pallet weighing 1 lb, so one unit load weighs 17 lb.
const string kGoodProduct = "GOOD,Good,10,10,10,5,CS,2,4,2,8,TLD\n";

// 2026-08-04 is a Tuesday: the August CTL table gives a window of 6-7 Aug.
PlanningWindow augustTuesdayWindow() {
    PlanningWindow window;
    window.planDate = "2026-08-04";
    window.firstShipDate = "2026-08-06";
    window.lastShipDate = "2026-08-07";
    return window;
}

struct DemandLine {
    string matnr = "GOOD";
    double cases = 16.0;
    string shipDate = "2026-08-06";
    string deliveryDate = "2026-08-08";
    string availDate = "2026-08-04";
    string priority = "0";
};

string strJson(const DemandLine& line) {
    ostringstream json;
    json << R"({"LOCFRNO":"1","LOCTONO":"2","MATNR":")" << line.matnr
         << R"(","DATFR_TA":")" << line.shipDate << R"(","DATTO_TA":")" << line.deliveryDate
         << R"(","AVAIL_DATE":")" << line.availDate << R"(","TPRIO":)" << line.priority
         << R"(,"AVAIL_QTY":0,"SHIP_COND":"TL","TRANS":)" << line.cases
         << R"(,"UNITOFMEAS":"CS"})";
    return json.str();
}

PipelineResult runLines(const vector<DemandLine>& lines, const string& products = kGoodProduct) {
    const string productPath = "tests/importer/_tmp_m4_items_products.csv";
    const string demandPath = "tests/importer/_tmp_m4_items_demand.json";
    const string placeholderPath = "tests/importer/_tmp_m4_items_placeholder.json";
    const string palletPath = "tests/importer/_tmp_m4_items_pallets.csv";
    ofstream(palletPath) << "ID,Footprint_Length,Footprint_Width,Height,Weight\nTLD,48,40,0.1,1\n";
    ofstream(productPath) << kProductHeader << products;
    {
        ofstream demand(demandPath);
        demand << R"({"REQUEST_ID":"t","CTL":[],"DNM":[],"STR":[)";
        for (size_t index = 0; index < lines.size(); ++index) {
            demand << (index == 0 ? "" : ",") << strJson(lines[index]);
        }
        demand << "]}";
    }
    ofstream(placeholderPath) << R"({"PHOLDER":[]})";
    PipelineInputs inputs;
    inputs.product_path = productPath;
    inputs.demand_path = demandPath;
    inputs.placeholder_path = placeholderPath;
    inputs.paramsPath = "config/orderBuilderParams.json";
    inputs.palletPath = palletPath;
    struct RemoveFiles {
        vector<string> paths;
        ~RemoveFiles() { for (const auto& path : paths) remove(path.c_str()); }
    } cleanup{{productPath, demandPath, placeholderPath, palletPath}};
    return Pipeline::run(inputs);
}

SelectionItems itemsFor(const vector<DemandLine>& lines, const string& products = kGoodProduct) {
    const PipelineResult run = runLines(lines, products);
    return buildSelectionItems(run, augustTuesdayWindow());
}

double totalUnitLoads(const SelectionItems& built) {
    double total = 0.0;
    for (const SelectionItem& item : built.items) total += item.unitLoads;
    return total;
}

size_t totalLevels(const SelectionItems& built) {
    size_t levels = 0;
    for (const SelectionItem& item : built.items) levels += item.lineIndices.size();
    return levels;
}

} // anonymous namespace

TEST_CASE("selectionItem: a stack of any depth is one item on one position") {
    const SelectionItems built = itemsFor({DemandLine{}});
    REQUIRE_FALSE(built.items.empty());
    for (const SelectionItem& item : built.items) CHECK(item.positions == 1);
    CHECK(totalUnitLoads(built) == doctest::Approx(2.0));
    CHECK(totalLevels(built) == 2);
}

TEST_CASE("selectionItem: item figures come from the line's unit loads") {
    const SelectionItems built = itemsFor({DemandLine{}});
    double weight = 0.0;
    for (const SelectionItem& item : built.items) {
        weight += item.weightLb;
        CHECK(item.footprintLongIn == doctest::Approx(48.0));
        CHECK(item.footprintShortIn == doctest::Approx(40.0));
        CHECK(item.heightIn == doctest::Approx(20.0 * static_cast<double>(item.lineIndices.size())));
    }
    CHECK(weight == doctest::Approx(34.0));
}

TEST_CASE("selectionItem: quarterPalletTakesOnePosition") {
    DemandLine quarter;
    quarter.cases = 2.0;
    const SelectionItems built = itemsFor({quarter});
    REQUIRE(built.items.size() == 1);
    const SelectionItem& item = built.items.front();
    CHECK(item.positions == 1);
    CHECK(item.unitLoads == doctest::Approx(0.25));
    CHECK(item.weightLb == doctest::Approx(0.25 * 17.0));
    CHECK(item.heightIn == doctest::Approx(20.0));
    CHECK(item.cubeCubicIn == doctest::Approx(0.25 * 48.0 * 40.0 * 20.0));
    CHECK(built.partialItems == 1);
}

TEST_CASE("selectionItem: positionsRoundedPerItem - each fractional line takes a whole level") {
    vector<DemandLine> lines(12);
    for (DemandLine& line : lines) line.cases = 1.0;
    const SelectionItems built = itemsFor(lines);
    CHECK(totalLevels(built) == 12);
    CHECK(totalUnitLoads(built) == doctest::Approx(12.0 / 8.0));
}

TEST_CASE("selectionItem: unit loads and weight in equal what the items carry (I10)") {
    mt19937 random(20261025);
    uniform_int_distribution<int> lineCount(1, 12);
    uniform_int_distribution<int> cases(1, 200);
    uniform_int_distribution<int> priority(0, kMaxPriority);
    for (int trial = 0; trial < 40; ++trial) {
        vector<DemandLine> lines(static_cast<size_t>(lineCount(random)));
        for (DemandLine& line : lines) {
            line.cases = cases(random);
            line.priority = to_string(priority(random));
            line.shipDate = random() % 2 == 0 ? "2026-08-06" : "2026-08-07";
        }
        const PipelineResult run = runLines(lines);
        const SelectionItems built = buildSelectionItems(run, augustTuesdayWindow());
        REQUIRE(built.linesNotFullyStacked.empty());
        REQUIRE(built.unitLoadsOut == doctest::Approx(built.unitLoadsIn));
        REQUIRE(built.weightOutLb == doctest::Approx(built.weightInLb));
        double expectedUnitLoads = 0.0;
        for (const DemandLine& line : lines) expectedUnitLoads += line.cases / 8.0;
        REQUIRE(built.unitLoadsIn == doctest::Approx(expectedUnitLoads));
        REQUIRE(is_sorted(built.items.begin(), built.items.end(), shipsBefore));
    }
}

TEST_CASE("selectionItem: ship timing against the window") {
    DemandLine overdue, inWindow, future;
    overdue.shipDate = "2026-08-05";
    inWindow.shipDate = "2026-08-07";
    future.shipDate = "2026-08-08";
    future.deliveryDate = "2026-08-09";
    map<string, ShipTiming> timingByDate;
    for (const SelectionItem& item : itemsFor({overdue}).items) timingByDate[item.shipDate] = item.timing;
    for (const SelectionItem& item : itemsFor({inWindow}).items) timingByDate[item.shipDate] = item.timing;
    for (const SelectionItem& item : itemsFor({future}).items) timingByDate[item.shipDate] = item.timing;
    CHECK(timingByDate.at("2026-08-05") == ShipTiming::Overdue);
    CHECK(timingByDate.at("2026-08-07") == ShipTiming::InWindow);
    CHECK(timingByDate.at("2026-08-08") == ShipTiming::Future);
}

TEST_CASE("selectionItem: overdueNeverDropped and ships first") {
    DemandLine overdue, onTime;
    overdue.shipDate = "2026-07-30";
    overdue.priority = "11";
    const SelectionItems built = itemsFor({onTime, overdue});
    REQUIRE_FALSE(built.items.empty());
    CHECK(built.items.front().shipDate == "2026-07-30");
    CHECK(built.items.front().timing == ShipTiming::Overdue);
    CHECK(built.rejected.empty());
}

TEST_CASE("selectionItem: available after the ship date, or after the window, is listed") {
    DemandLine late, veryLate;
    late.availDate = "2026-08-07";
    veryLate.availDate = "2026-08-09";
    const SelectionItem lateItem = itemsFor({late}).items.front();
    CHECK(lateItem.availableAfterShipDate);
    CHECK_FALSE(lateItem.unavailableInWindow);
    const SelectionItem veryLateItem = itemsFor({veryLate}).items.front();
    CHECK(veryLateItem.availableAfterShipDate);
    CHECK(veryLateItem.unavailableInWindow);
}

TEST_CASE("selectionItem: priorityZeroBeatsEleven on the same ship date") {
    const string products = kGoodProduct + "OTHER,Other,10,10,10,5,CS,2,4,2,8,TLD\n";
    DemandLine urgent, relaxed;
    urgent.matnr = "OTHER";
    urgent.cases = 8.0;
    relaxed.cases = 8.0;
    relaxed.priority = "11";
    vector<SelectionItem> items = itemsFor({relaxed, urgent}, products).items;
    REQUIRE_FALSE(items.empty());
    CHECK(items.front().priority == 0);
}

TEST_CASE("selectionItem: same date and priority tie-break on product number") {
    SelectionItem first, second;
    first.shipDate = second.shipDate = "2026-08-06";
    first.productNumber = "100";
    second.productNumber = "200";
    CHECK(shipsBefore(first, second));
    CHECK_FALSE(shipsBefore(second, first));
    first.priority = 2;
    CHECK(shipsBefore(second, first));
}

TEST_CASE("selectionItem: a stack mixing ship dates ships by its earliest") {
    // One unit load each of two products, stacked on one floor position.
    const string products = kGoodProduct + "OTHER,Other,10,10,10,5,CS,2,4,2,8,TLD\n";
    DemandLine early, late;
    early.cases = 8.0;
    early.priority = "6";
    early.availDate = "2026-08-03";
    late.matnr = "OTHER";
    late.cases = 8.0;
    late.shipDate = "2026-08-07";
    late.availDate = "2026-08-05";
    late.priority = "2";
    const SelectionItems built = itemsFor({early, late}, products);
    REQUIRE(built.items.size() == 1);
    const SelectionItem& item = built.items.front();
    REQUIRE(item.lineIndices.size() == 2);
    CHECK(item.mixesShipDates);
    CHECK(item.shipDate == "2026-08-06");
    CHECK(item.availDate == "2026-08-05");
    CHECK(item.priority == 2);
    CHECK(built.itemsMixingShipDates == 1);
}

TEST_CASE("selectionItem: an unreadable priority rejects the item but keeps its goods counted") {
    DemandLine blankPriority;
    blankPriority.priority = "null";
    const SelectionItems built = itemsFor({blankPriority});
    CHECK(built.items.empty());
    REQUIRE_FALSE(built.rejected.empty());
    CHECK(built.rejected.front().reason == ItemRejection::UnreadablePriority);
    CHECK(built.unitLoadsOut == doctest::Approx(built.unitLoadsIn));
    CHECK(built.unitLoadsIn == doctest::Approx(2.0));
}

TEST_CASE("selectionItem: a priority outside 0..11 is rejected") {
    for (const string priority : {"-3", "12", "99"}) {
        DemandLine line;
        line.priority = priority;
        const SelectionItems built = itemsFor({line});
        CHECK(built.items.empty());
        REQUIRE_FALSE(built.rejected.empty());
        CHECK(built.rejected.front().reason == ItemRejection::UnreadablePriority);
    }
}

TEST_CASE("selectionItem: invalid dates reject the item") {
    DemandLine badShip, badAvail, noDelivery;
    badShip.shipDate = "2026-13-01";
    badShip.deliveryDate = "2026-14-01";
    badAvail.availDate = "soon";
    noDelivery.deliveryDate = "";
    CHECK(itemsFor({badShip}).rejected.front().reason == ItemRejection::InvalidShipDate);
    CHECK(itemsFor({badAvail}).rejected.front().reason == ItemRejection::InvalidAvailDate);
    CHECK(itemsFor({noDelivery}).rejected.front().reason == ItemRejection::InvalidDeliveryDate);
}

TEST_CASE("selectionItem: an item heavier than the payload is named, never dropped") {
    // 6,000 lb cases, 8 to a unit load: 48,001 lb against a 45,000 lb payload.
    const string heavy = "HEAVY,Heavy,10,10,10,5,CS,6000,4,2,8,TLD\n";
    DemandLine line;
    line.matnr = "HEAVY";
    line.cases = 8.0;
    const SelectionItems built = itemsFor({line}, heavy);
    REQUIRE(built.items.size() == 1);
    REQUIRE(built.infeasible.size() == 1);
    CHECK(built.infeasible.front().itemIndex == 0);
    CHECK(built.infeasible.front().reason == ItemInfeasibility::OverPayload);
}

TEST_CASE("selectionItem: needs Milestone 2") {
    PipelineResult notRun;
    CHECK_THROWS_AS(buildSelectionItems(notRun, augustTuesdayWindow()), invalid_argument);
}

TEST_CASE("selectionItem: the same stacks always give identical items") {
    const string products = kGoodProduct + "OTHER,Other,10,10,10,5,CS,2,4,2,8,TLD\n";
    vector<DemandLine> lines(6);
    for (size_t index = 0; index < lines.size(); ++index) {
        lines[index].cases = 3.0 + 5.0 * static_cast<double>(index);
        lines[index].matnr = index % 2 == 0 ? "GOOD" : "OTHER";
        lines[index].priority = to_string(index % 3);
    }
    const PipelineResult run = runLines(lines, products);
    const SelectionItems first = buildSelectionItems(run, augustTuesdayWindow());
    const SelectionItems second = buildSelectionItems(run, augustTuesdayWindow());
    REQUIRE(first.items.size() == second.items.size());
    for (size_t index = 0; index < first.items.size(); ++index) {
        CHECK(first.items[index].lineIndices == second.items[index].lineIndices);
        CHECK(first.items[index].unitLoads == second.items[index].unitLoads);
        CHECK(first.items[index].weightLb == second.items[index].weightLb);
    }
}

namespace {

struct AugustRunFiles {
    string demandPath;
    string placeholderPath;
};

const array<AugustRunFiles, 3> kAugustRunFiles{{
    {"tests/importer/august/inputs/Aug4/Demand-Aug5-2.json", "tests/importer/august/inputs/Aug4/PH-Aug4.json"},
    {"tests/importer/august/inputs/Aug5/Demand-Aug5.json", "tests/importer/august/inputs/Aug5/PH-Aug5.json"},
    {"tests/importer/august/inputs/Aug6/Demand-Aug6.json", "tests/importer/august/inputs/Aug6/PH-Aug6.json"},
}};
const string kAugustProduct = "tests/importer/august/inputs/shared/Product-Data.csv";

bool augustPresent() {
    static const bool present = crossDayTests::filesPresent(
        {kAugustProduct, crossDayTests::kPalletTableForOlderMasters,
         crossDayTests::kLayerSizesForOlderMasters, kAugustRunFiles[0].demandPath,
         kAugustRunFiles[0].placeholderPath, kAugustRunFiles[1].demandPath,
         kAugustRunFiles[1].placeholderPath, kAugustRunFiles[2].demandPath,
         kAugustRunFiles[2].placeholderPath},
        "4-6 Aug selection item tests are");
    return present;
}

struct AugustItems {
    PipelineResult run;
    ReservationBook book;
    SelectionItems built;
};

const AugustItems& augustItems(size_t runIndex) {
    static const array<AugustItems, 3> runs = [] {
        array<AugustItems, 3> loaded;
        for (size_t index = 0; index < loaded.size(); ++index) {
            PipelineInputs inputs;
            inputs.product_path = kAugustProduct;
            inputs.demand_path = kAugustRunFiles[index].demandPath;
            inputs.placeholder_path = kAugustRunFiles[index].placeholderPath;
            inputs.paramsPath = crossDayTests::kStrictParamsPath;
            inputs.palletPath = crossDayTests::kPalletTableForOlderMasters;
            inputs.layerSizePath = crossDayTests::kLayerSizesForOlderMasters;
            loaded[index].run = Pipeline::run(inputs);
            loaded[index].book = buildReservationBook(
                loaded[index].run.placeholders.placeholders, loaded[index].run.demand.ctl,
                planDateFromRequestId(loaded[index].run.demand.request_id), {"53F", {"53F"}});
            loaded[index].built = buildSelectionItems(loaded[index].run, loaded[index].book.window);
        }
        return loaded;
    }();
    return runs[runIndex];
}

} // anonymous namespace

TEST_CASE("selectionItem: every August stacked unit load lands in exactly one item"
          * doctest::skip(!augustPresent())) {
    for (size_t run = 0; run < kAugustRunFiles.size(); ++run) {
        CAPTURE(run);
        const SelectionItems& built = augustItems(run).built;
        CHECK(built.linesNotFullyStacked.empty());
        CHECK(built.unitLoadsOut == doctest::Approx(built.unitLoadsIn));
        CHECK(built.weightOutLb == doctest::Approx(built.weightInLb));
        CHECK(built.unitLoadsIn > 0.0);
    }
}

TEST_CASE("selectionItem: August items fit the trailer and are in selection order"
          * doctest::skip(!augustPresent())) {
    for (size_t run = 0; run < kAugustRunFiles.size(); ++run) {
        CAPTURE(run);
        const AugustItems& august = augustItems(run);
        CHECK(august.built.infeasible.empty());
        CHECK(august.built.rejected.empty());
        CHECK(is_sorted(august.built.items.begin(), august.built.items.end(), shipsBefore));
        for (const SelectionItem& item : august.built.items) {
            REQUIRE(item.positions == 1);
            REQUIRE(item.heightIn <= august.run.trailer.stackHeightCeilingIn + kQuantityEpsilon);
            REQUIRE(item.footprintShortIn * 2.0 <= august.run.trailer.interiorWidthIn + kQuantityEpsilon);
        }
    }
}
