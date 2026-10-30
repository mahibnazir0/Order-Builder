#include "doctest.h"
#include "../m4AugustFixtures.hpp"
#include "laneClassifier.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;

namespace {

const TrailerSpec kTrailer{"53FT_NA", 630.0, 100.0, 108.0, 45000.0, 32, nullopt};
const LaneClassifierSettings kSettings{20, 5.0};

// 2026-08-04 is a Tuesday: the window is 6-7 Aug.
const string kPlanDate = "2026-08-04";
const vector<CTLRecord> kCtl{{"", "TUESDAY", 2, 3, 2}};

SegregatedGroup group(const string& origin, SplitReason reason = SplitReason::None,
                      const string& segregant = "") {
    SegregatedGroup built;
    built.key.locationFrom = origin;
    built.key.locationTo = "2528";
    built.key.shipCondition = "TL";
    built.key.isSegregated = !segregant.empty();
    built.key.segregant = segregant;
    built.splitReason = reason;
    return built;
}

// A run holding only what the classifier reads: groups, the joined lines' products and the
// trailer. Line i is product "P<i>".
PipelineResult runWith(vector<SegregatedGroup> groups, size_t productCount) {
    PipelineResult run;
    run.demand.str.resize(productCount);
    for (size_t index = 0; index < productCount; ++index) {
        run.demand.str[index].matnr = "P" + to_string(index);
    }
    for (const STRRecord& line : run.demand.str) run.join.lines.push_back({&line, nullptr, nullptr, false});
    run.segregation.groups = std::move(groups);
    run.trailer = kTrailer;
    run.ranMilestone2 = true;
    return run;
}

SelectionItem item(size_t groupIndex, size_t lineIndex, const string& shipDate = "2026-08-06") {
    SelectionItem built;
    built.groupIndex = groupIndex;
    built.lineIndices = {lineIndex};
    built.unitLoads = 1.0;
    built.weightLb = 1000.0;
    built.cubeCubicIn = 48.0 * 40.0 * 50.0;
    built.shipDate = shipDate;
    built.availDate = "2026-08-01";
    return built;
}

SelectionItems itemsOf(vector<SelectionItem> items) {
    SelectionItems built;
    built.items = std::move(items);
    return built;
}

vector<SelectionItem> repeated(size_t count, size_t groupIndex, size_t lineIndex,
                               const string& shipDate = "2026-08-06") {
    return vector<SelectionItem>(count, item(groupIndex, lineIndex, shipDate));
}

PlaceholderRecord reserved(const string& origin, const string& shipDate, int trucks) {
    PlaceholderRecord record;
    record.locfrno = origin;
    record.loctono = "2528";
    record.ship_cond = "TL";
    record.datfr_ta = shipDate;
    record.datto_ta = "2026-08-12";
    record.zzna_equip_size = "53F";
    record.no_of_loads = trucks;
    return record;
}

ReservationBook bookOf(const vector<PlaceholderRecord>& placeholders) {
    return buildReservationBook(placeholders, kCtl, kPlanDate, {"53F", {"53F"}});
}

LaneClassification classify(const PipelineResult& run, const vector<PlaceholderRecord>& placeholders,
                            const vector<SelectionItem>& items,
                            const LaneClassifierSettings& settings = kSettings) {
    return classifyLanes(run, bookOf(placeholders), itemsOf(items), settings);
}

LaneRoute onlyRoute(const LaneClassification& classification) {
    REQUIRE(classification.routes.size() == 1);
    return classification.routes.front();
}

// Every routed or left-out item appears exactly once, and no two routes share a group and date.
void checkEveryItemExactlyOnce(const LaneClassification& classification, size_t itemCount) {
    vector<int> seen(itemCount, 0);
    set<pair<size_t, string>> routeKeys;
    for (const LaneRoute& route : classification.routes) {
        REQUIRE(routeKeys.insert({route.groupIndex, route.shipDate}).second);
        for (const size_t index : route.itemIndices) ++seen[index];
    }
    for (const UnroutedItem& left : classification.itemsLeftOut) ++seen[left.itemIndex];
    REQUIRE(all_of(seen.begin(), seen.end(), [](int count) { return count == 1; }));
}

} // anonymous namespace

TEST_CASE("laneClassifier: reserved 1 goes to knapsack") {
    const PipelineResult run = runWith({group("2027")}, 1);
    const LaneRoute route = onlyRoute(classify(run, {reserved("2027", "2026-08-06", 1)}, repeated(10, 0, 0)));
    CHECK(route.method == SolveMethod::Knapsack);
    CHECK(route.reservedTrucks == 1);
    CHECK(route.demandTrucks == 1);
}

TEST_CASE("laneClassifier: thresholdBothSides") {
    const PipelineResult run = runWith({group("2027")}, 1);
    const vector<SelectionItem> items = repeated(32 * 20, 0, 0);
    const LaneRoute atThreshold = onlyRoute(classify(run, {reserved("2027", "2026-08-06", 20)}, items));
    CHECK(atThreshold.method == SolveMethod::PatternPacker);
    CHECK_FALSE(atThreshold.methodBeforeEscalation);
    const LaneRoute over = onlyRoute(classify(run, {reserved("2027", "2026-08-06", 21)}, items));
    CHECK(over.method == SolveMethod::M5);
    REQUIRE(over.methodBeforeEscalation);
    CHECK(*over.methodBeforeEscalation == SolveMethod::PatternPacker);
}

TEST_CASE("laneClassifier: the largest lane goes to M5 at threshold 20 and patternPacker at 50") {
    // 41 reserved, 13 products, 1,300 unit loads: 100 per product.
    const PipelineResult run = runWith({group("2027")}, 13);
    vector<SelectionItem> items;
    for (size_t product = 0; product < 13; ++product) {
        const vector<SelectionItem> ofProduct = repeated(100, 0, product);
        items.insert(items.end(), ofProduct.begin(), ofProduct.end());
    }
    const vector<PlaceholderRecord> reservation{reserved("2027", "2026-08-06", 41)};
    CHECK(onlyRoute(classify(run, reservation, items)).method == SolveMethod::M5);
    const LaneRoute at50 = onlyRoute(classify(run, reservation, items, {50, 5.0}));
    CHECK(at50.method == SolveMethod::PatternPacker);
    CHECK(at50.distinctProducts == 13);
}

TEST_CASE("laneClassifier: reserved 0 with demand is named and not routed") {
    const PipelineResult run = runWith({group("2019")}, 1);
    const LaneRoute route = onlyRoute(classify(run, {}, repeated(4, 0, 0)));
    CHECK(route.method == SolveMethod::Unrouted);
    CHECK(route.reservedTrucks == 0);
    CHECK(route.itemIndices.size() == 4);
}

TEST_CASE("laneClassifier: a reservation with no demand is a route with nothing to load") {
    const PipelineResult run = runWith({group("2027")}, 1);
    const LaneRoute route = onlyRoute(classify(run, {reserved("2027", "2026-08-07", 3)}, {}));
    CHECK(route.method == SolveMethod::NoDemand);
    CHECK(route.reservedTrucks == 3);
    CHECK(route.shortLane);
}

TEST_CASE("laneClassifier: demand below the reservation is a short lane, classified normally") {
    const PipelineResult run = runWith({group("2027")}, 1);
    const LaneRoute route = onlyRoute(classify(run, {reserved("2027", "2026-08-06", 3)}, repeated(40, 0, 0)));
    CHECK(route.shortLane);
    CHECK(route.demandTrucks == 2);
    CHECK(route.method == SolveMethod::PatternPacker);
}

TEST_CASE("laneClassifier: demand above the reservation goes to knapsack") {
    const PipelineResult run = runWith({group("2027")}, 1);
    const LaneRoute route = onlyRoute(classify(run, {reserved("2027", "2026-08-06", 2)}, repeated(70, 0, 0)));
    CHECK(route.demandAboveReservation);
    CHECK(route.demandTrucks == 3);
    CHECK(route.method == SolveMethod::Knapsack);
}

TEST_CASE("laneClassifier: demand trucks count weight and cube as well as positions") {
    const PipelineResult run = runWith({group("2027")}, 1);
    vector<SelectionItem> heavy = repeated(10, 0, 0);
    for (SelectionItem& one : heavy) one.weightLb = 9000.0;
    CHECK(onlyRoute(classify(run, {reserved("2027", "2026-08-06", 3)}, heavy)).demandTrucks == 2);
}

TEST_CASE("laneClassifier: routingRatioFromBenchmark - the variety ratio decides") {
    // 4 products: 40 unit loads is 10 per product, 12 is 3.
    const PipelineResult run = runWith({group("2027")}, 4);
    vector<SelectionItem> few, many;
    for (size_t product = 0; product < 4; ++product) {
        const vector<SelectionItem> lots = repeated(10, 0, product);
        few.insert(few.end(), lots.begin(), lots.end());
        const vector<SelectionItem> little = repeated(3, 0, product);
        many.insert(many.end(), little.begin(), little.end());
    }
    const vector<PlaceholderRecord> reservation{reserved("2027", "2026-08-06", 2)};
    CHECK(onlyRoute(classify(run, reservation, few)).method == SolveMethod::PatternPacker);
    CHECK(onlyRoute(classify(run, reservation, many)).method == SolveMethod::BinPacker);
    CHECK(onlyRoute(classify(run, reservation, few, {20, 10.0})).method == SolveMethod::BinPacker);
}

TEST_CASE("laneClassifier: underscoredLaneIsSegregationNotMultiStop") {
    const PipelineResult run = runWith(
        {group("2028"), group("2028", SplitReason::DoNotMix, "S01"), group("2028", SplitReason::DoNotMix, "S03")}, 3);
    vector<SelectionItem> items = repeated(5, 0, 0);
    const vector<SelectionItem> second = repeated(5, 1, 1);
    const vector<SelectionItem> third = repeated(5, 2, 2);
    items.insert(items.end(), second.begin(), second.end());
    items.insert(items.end(), third.begin(), third.end());
    const LaneClassification classification = classify(run, {reserved("2028", "2026-08-06", 3)}, items);
    REQUIRE(classification.routes.size() == 3);
    CHECK(classification.routes[0].conflict == ConflictReason::SegregationSplit);
    CHECK(classification.routes[1].conflict == ConflictReason::DoNotMix);
    CHECK(classification.routes[2].conflict == ConflictReason::DoNotMix);
    for (const LaneRoute& route : classification.routes) {
        CHECK(route.groupsSharingReservation == 3);
        CHECK(route.reservedTrucks == 3);
        CHECK(route.itemIndices.size() == 5);
    }
}

TEST_CASE("laneClassifier: classifyPerGroup - groups of one lane can take different methods") {
    const PipelineResult run = runWith({group("2028"), group("2028", SplitReason::DoNotMix, "S01")}, 9);
    vector<SelectionItem> items = repeated(40, 0, 0);
    for (size_t product = 1; product < 9; ++product) items.push_back(item(1, product));
    const LaneClassification classification = classify(run, {reserved("2028", "2026-08-06", 4)}, items);
    REQUIRE(classification.routes.size() == 2);
    CHECK(classification.routes[0].method == SolveMethod::PatternPacker);
    CHECK(classification.routes[1].method == SolveMethod::BinPacker);
}

TEST_CASE("laneClassifier: a lane reserved on two dates gets one route per date") {
    const PipelineResult run = runWith({group("2027")}, 1);
    vector<SelectionItem> items = repeated(10, 0, 0, "2026-08-06");
    const vector<SelectionItem> later = repeated(10, 0, 0, "2026-08-07");
    items.insert(items.end(), later.begin(), later.end());
    const LaneClassification classification = classify(
        run, {reserved("2027", "2026-08-06", 1), reserved("2027", "2026-08-07", 2)}, items);
    REQUIRE(classification.routes.size() == 2);
    CHECK(classification.routes[0].shipDate == "2026-08-06");
    CHECK(classification.routes[0].reservedTrucks == 1);
    CHECK(classification.routes[0].itemIndices.size() == 10);
    CHECK(classification.routes[1].shipDate == "2026-08-07");
    CHECK(classification.routes[1].reservedTrucks == 2);
}

TEST_CASE("laneClassifier: demand on a date with no reservation is never put on another date's truck") {
    const PipelineResult run = runWith({group("2027")}, 1);
    const LaneClassification classification =
        classify(run, {reserved("2027", "2026-08-06", 2)}, repeated(5, 0, 0, "2026-08-07"));
    REQUIRE(classification.routes.size() == 2);
    CHECK(classification.routes[0].method == SolveMethod::NoDemand);
    CHECK(classification.routes[1].shipDate == "2026-08-07");
    CHECK(classification.routes[1].method == SolveMethod::Unrouted);
}

TEST_CASE("laneClassifier: overdue items ride the lane's first reserved truck") {
    const PipelineResult run = runWith({group("2027")}, 1);
    vector<SelectionItem> items = repeated(3, 0, 0, "2026-08-01");
    for (SelectionItem& overdue : items) overdue.timing = ShipTiming::Overdue;
    const LaneRoute route = onlyRoute(classify(run, {reserved("2027", "2026-08-07", 1)}, items));
    CHECK(route.shipDate == "2026-08-07");
    CHECK(route.method == SolveMethod::Knapsack);
    CHECK(route.itemIndices.size() == 3);
}

TEST_CASE("laneClassifier: an item available later in the window is routed to that day") {
    const PipelineResult run = runWith({group("2027")}, 1);
    SelectionItem late = item(0, 0, "2026-08-06");
    late.availDate = "2026-08-07";
    late.availableAfterShipDate = true;
    const LaneRoute route = onlyRoute(classify(run, {reserved("2027", "2026-08-07", 1)}, {late}));
    CHECK(route.shipDate == "2026-08-07");
}

TEST_CASE("laneClassifier: future and unavailable items are left out and listed") {
    const PipelineResult run = runWith({group("2027")}, 1);
    SelectionItem future = item(0, 0, "2026-08-09");
    future.timing = ShipTiming::Future;
    SelectionItem unavailable = item(0, 0);
    unavailable.availDate = "2026-08-09";
    unavailable.unavailableInWindow = true;
    const LaneClassification classification =
        classify(run, {reserved("2027", "2026-08-06", 1)}, {future, unavailable, item(0, 0)});
    REQUIRE(classification.itemsLeftOut.size() == 2);
    CHECK(classification.itemsLeftOut[0].reason == UnroutedItemReason::AfterWindow);
    CHECK(classification.itemsLeftOut[1].reason == UnroutedItemReason::UnavailableInWindow);
    CHECK(onlyRoute(classification).itemIndices == vector<size_t>{2});
}

TEST_CASE("laneClassifier: everyGroupExactlyOneCase (property)") {
    mt19937 random(25102026);
    const array<string, 3> dates{"2026-08-05", "2026-08-06", "2026-08-07"};
    for (int trial = 0; trial < 300; ++trial) {
        const PipelineResult run = runWith(
            {group("2027"), group("2028"), group("2028", SplitReason::DoNotMix, "S01")}, 5);
        vector<SelectionItem> items(random() % 60);
        for (SelectionItem& one : items) {
            one = item(random() % 3, random() % 5, dates[random() % 3]);
            if (one.shipDate < "2026-08-06") one.timing = ShipTiming::Overdue;
            if (random() % 10 == 0) one.timing = ShipTiming::Future;
        }
        vector<PlaceholderRecord> placeholders;
        for (int count = static_cast<int>(random() % 4); count > 0; --count) {
            placeholders.push_back(reserved(random() % 2 == 0 ? "2027" : "2028", dates[1 + random() % 2],
                                            1 + static_cast<int>(random() % 30)));
        }
        const LaneClassification classification = classify(run, placeholders, items);
        checkEveryItemExactlyOnce(classification, items.size());
        for (const LaneRoute& route : classification.routes) {
            REQUIRE((route.method != SolveMethod::M5 || route.reservedTrucks > kSettings.m5Threshold));
            REQUIRE((route.method != SolveMethod::Unrouted || route.reservedTrucks == 0));
            REQUIRE((route.method != SolveMethod::NoDemand || route.itemIndices.empty()));
        }
    }
}

TEST_CASE("laneClassifier: settings out of range are rejected") {
    const PipelineResult run = runWith({group("2027")}, 1);
    const double nan = numeric_limits<double>::quiet_NaN();
    const double infinity = numeric_limits<double>::infinity();
    for (const LaneClassifierSettings bad : {LaneClassifierSettings{1, 5.0}, LaneClassifierSettings{-3, 5.0},
                                             LaneClassifierSettings{20, 0.0}, LaneClassifierSettings{20, -5.0},
                                             LaneClassifierSettings{20, nan}, LaneClassifierSettings{20, infinity}}) {
        CHECK_THROWS_AS(classify(run, {}, {}, bad), invalid_argument);
    }
    PipelineResult noPositions = runWith({group("2027")}, 1);
    noPositions.trailer.stackPositions = 0;
    CHECK_THROWS_AS(classify(noPositions, {}, {}), invalid_argument);
    noPositions.trailer = kTrailer;
    noPositions.trailer.weightLimitLb = nan;
    CHECK_THROWS_AS(classify(noPositions, {}, {}), invalid_argument);
}

TEST_CASE("laneClassifier: an item naming a group outside the run is rejected") {
    const PipelineResult run = runWith({group("2027")}, 1);
    CHECK_THROWS_AS(classify(run, {}, {item(1, 0)}), invalid_argument);
}

TEST_CASE("laneClassifier: every August item is routed once or left out with a reason"
          * doctest::skip(!m4AugustTests::augustPresent())) {
    for (size_t runIndex = 0; runIndex < m4AugustTests::kAugustRunFiles.size(); ++runIndex) {
        CAPTURE(runIndex);
        const m4AugustTests::AugustItems& august = m4AugustTests::augustItems(runIndex);
        const LaneClassification classification =
            classifyLanes(august.run, august.book, august.built, kSettings);
        checkEveryItemExactlyOnce(classification, august.built.items.size());
        long long reservedOnRoutes = 0;
        set<pair<size_t, string>> reservationRoutes;
        for (const LaneRoute& route : classification.routes) {
            const SegregatedGroup& routed = august.run.segregation.groups[route.groupIndex];
            const Reservation* found = findReservation(august.book, routed.key.locationFrom,
                                                       routed.key.locationTo,
                                                       routed.key.shipCondition, route.shipDate);
            CHECK(route.reservedTrucks == (found ? found->trucks : 0));
            if (route.groupsSharingReservation == 1) reservedOnRoutes += route.reservedTrucks;
        }
        CHECK(reservedOnRoutes <= august.book.reservedTrucks);
    }
}
