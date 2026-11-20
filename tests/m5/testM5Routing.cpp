// Routing into M5 and the selector seam on days built through the real reservation book and
// classifier (v7 4.1, 4.2, 11), threads, and the M4 conversions M5 relies on (4.9, 18).
#include "doctest.h"
#include "../m4SmallDay.hpp"
#include "../m4SyntheticDay.hpp"
#include "converter.hpp"
#include "floorPattern.hpp"
#include "joiner.hpp"
#include "m5Integration.hpp"
#include "m5Policy.hpp"
#include "m5UnitLoads.hpp"
#include "paramsFields.hpp"
#include "pipeline.hpp"
#include "placeholder_importer.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Fixtures.hpp"
#include "../runTempFolder.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>

using namespace std;
using namespace ob;
using namespace m4SyntheticDay;

namespace {

shared_ptr<const LargeLanePlanner> m5Planner() {
    return make_shared<m5::M5LargeLanePlanner>(m5Fixtures::syntheticProfile(), "", m5::DateRules{false, 10}, 20);
}

SelectorSettings withPlanner(int threads = 1) {
    SelectorSettings settings = selectorSettings();
    settings.largeLanePlanner = m5Planner();
    settings.threads = threads;
    return settings;
}

PlaceholderRecord reservedOn(const string& origin, const string& destination, const string& condition, int trucks,
                             const string& date = "2026-08-06") {
    PlaceholderRecord record = reserved(origin, trucks, date);
    record.loctono = destination;
    record.ship_cond = condition;
    return record;
}

SegregatedGroup groupOn(const string& origin, const string& destination, const string& condition) {
    SegregatedGroup built = group(origin);
    built.key.locationTo = destination;
    built.key.shipCondition = condition;
    return built;
}

vector<SelectionItem> itemsOf(size_t group, size_t count, size_t firstLine, const string& date = "2026-08-06") {
    vector<SelectionItem> items;
    for (size_t index = 0; index < count; ++index) {
        SelectionItem built = item(group, firstLine + index);
        built.shipDate = date;
        items.push_back(built);
    }
    return items;
}

vector<SelectionItem> join(vector<SelectionItem> left, const vector<SelectionItem>& right) {
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

bool routedToM5(const Day& day, const string& origin, const string& date) {
    for (const LaneRoute& route : day.classification.routes) {
        if (day.run.segregation.groups[route.groupIndex].key.locationFrom == origin && route.shipDate == date) {
            return route.method == SolveMethod::M5;
        }
    }
    return false;
}

map<size_t, vector<vector<size_t>>> trucksByGroup(const SelectionPlan& plan) {
    map<size_t, vector<vector<size_t>>> trucks;
    for (const PlannedTruck& truck : plan.trucks) trucks[truck.groupIndex].push_back(truck.items);
    for (auto& [group, loads] : trucks) sort(loads.begin(), loads.end());
    return trucks;
}

ProductRecord product(const string& id, int casesUnitLoad, const string& pallet = "TLD") {
    ProductRecord record;
    record.id = id;
    record.cases_unit_load = casesUnitLoad;
    record.pallet_id = pallet;
    record.weight_lb = 10.0;
    return record;
}

}  // namespace

TEST_CASE("U-RT-03/synthetic: the threshold comes from the params file; at T = 50, 21 reserved stays in M4 and 51 goes to M5" * doctest::test_suite("m5")) {
    // The shipped params file, with only selection.m5Threshold changed, read by the real loader.
    nlohmann::json params;
    {
        ifstream shipped("config/orderBuilderParams.json");
        REQUIRE(shipped);
        shipped >> params;
    }
    REQUIRE(loadSelectionParams("config/orderBuilderParams.json").routing.m5Threshold == 20);
    params["selection"]["m5Threshold"] = 50;
    const filesystem::path path = testTemp::runTempFolder() / "uRt03Threshold50.json";
    {
        ofstream out(path);
        out << params.dump(2);
    }
    const LaneClassifierSettings fifty = loadSelectionParams(path.string()).routing;
    CHECK(fifty.m5Threshold == 50);
    const auto methodAt = [](const LaneClassifierSettings& routing, int reservedTrucks) {
        const Day day = dayOf({group("2027")}, 5, itemsOf(0, 5, 0), {reserved("2027", reservedTrucks)});
        return classifyLanes(day.run, day.book, day.items, routing).routes.front().method;
    };
    CHECK(methodAt(fifty, 21) != SolveMethod::M5);
    CHECK(methodAt(fifty, 50) != SolveMethod::M5);
    CHECK(methodAt(fifty, 51) == SolveMethod::M5);
    const LaneClassifierSettings twenty = loadSelectionParams("config/orderBuilderParams.json").routing;
    CHECK(methodAt(twenty, 21) == SolveMethod::M5);
    m5Evidence::note("expected", {"shipped T = 20: 21 -> M5", "file T = 50: 21 and 50 stay in M4, 51 -> M5"});
}

TEST_CASE("X-ROUTE-01 X-ROUTE-10 only reserved > T goes to M5, T from the settings" * doctest::test_suite("m5")) {
    for (const int threshold : {20, 50}) {
        for (const int reservedTrucks : {threshold - 1, threshold, threshold + 1}) {
            const Day day = dayOf({group("2027")}, 5, itemsOf(0, 5, 0), {reserved("2027", reservedTrucks)});
            const LaneClassification routed = classifyLanes(day.run, day.book, day.items, LaneClassifierSettings{threshold, 5.0});
            INFO("T " << threshold << ", reserved " << reservedTrucks);
            CHECK((routed.routes.front().method == SolveMethod::M5) == (reservedTrucks > threshold));
        }
    }
    const Day day = dayOf({group("2027")}, 5, itemsOf(0, 5, 0), {reserved("2027", 21)});
    CHECK_THROWS_AS(classifyLanes(day.run, day.book, day.items, LaneClassifierSettings{1, 5.0}), invalid_argument);
}

TEST_CASE("U-SP-01/synthetic: one lane with two ship dates is two pieces, each with its own reservation and date rules" * doctest::test_suite("m5")) {
    const vector<SelectionItem> goods = join(itemsOf(0, 40, 0, "2026-08-06"), itemsOf(0, 40, 40, "2026-08-07"));
    const Day day = dayOf({group("2027")}, 80, goods, {reserved("2027", 21, "2026-08-06"), reserved("2027", 25, "2026-08-07")});
    CHECK(routedToM5(day, "2027", "2026-08-06"));
    CHECK(routedToM5(day, "2027", "2026-08-07"));
    const SelectionPlan selection = plan(day, withPlanner());
    REQUIRE(selection.largeLanes.size() == 2);
    CHECK(selection.largeLanes[0].shipDate != selection.largeLanes[1].shipDate);
    map<string, string> textOf;
    for (const LargeLaneReport& lane : selection.largeLanes) textOf[lane.shipDate] = lane.text;
    CHECK(textOf["2026-08-06"].find("reserved: 21\n") != string::npos);
    CHECK(textOf["2026-08-07"].find("reserved: 25\n") != string::npos);
    map<string, size_t> trucksOn;
    for (const PlannedTruck& truck : selection.trucks) {
        ++trucksOn[truck.shipDate];
        // Pull-forward is off in this planner: no good rides a date other than its own.
        for (const size_t index : truck.items) CHECK(day.items.items[index].shipDate == truck.shipDate);
    }
    CHECK(trucksOn["2026-08-06"] >= 1);
    CHECK(trucksOn["2026-08-06"] <= 21);
    CHECK(trucksOn["2026-08-07"] >= 1);
    CHECK(trucksOn["2026-08-07"] <= 25);
    CHECK(accountedOnce(selection, day.items.items.size()));
    m5Evidence::note("expected", {"two M5 pieces: 6 Aug pool 21, 7 Aug pool 25", "each good on its own date", "every good once"});
}

TEST_CASE("X-ROUTE-03/synthetic: only the 21-reserved dates route to M5 in either input order" * doctest::test_suite("m5")) {
    const vector<SelectionItem> goods =
        join(join(itemsOf(0, 4, 0, "2026-08-06"), itemsOf(0, 4, 4, "2026-08-07")), join(itemsOf(1, 4, 8, "2026-08-06"), itemsOf(1, 4, 12, "2026-08-07")));
    const Day day = dayOf({group("2027"), group("2028")}, 16, goods,
                          {reserved("2027", 21, "2026-08-06"), reserved("2027", 20, "2026-08-07"), reserved("2028", 20, "2026-08-06"),
                           reserved("2028", 21, "2026-08-07")});
    CHECK(routedToM5(day, "2027", "2026-08-06"));
    CHECK_FALSE(routedToM5(day, "2027", "2026-08-07"));
    CHECK_FALSE(routedToM5(day, "2028", "2026-08-06"));
    CHECK(routedToM5(day, "2028", "2026-08-07"));
    const SelectionPlan selection = plan(day, withPlanner());
    REQUIRE(selection.largeLanes.size() == 2);
    for (const PlannedTruck& truck : selection.trucks) {
        for (const size_t index : truck.items) CHECK(day.items.items[index].shipDate == truck.shipDate);
    }
    vector<SelectionItem> reversedGoods = goods;
    reverse(reversedGoods.begin(), reversedGoods.end());
    const Day reversed = dayOf({group("2027"), group("2028")}, 16, reversedGoods,
                               {reserved("2028", 21, "2026-08-07"), reserved("2028", 20, "2026-08-06"),
                                reserved("2027", 20, "2026-08-07"), reserved("2027", 21, "2026-08-06")});
    for (const string& origin : {string("2027"), string("2028")}) {
        for (const string& date : {string("2026-08-06"), string("2026-08-07")}) {
            CHECK(routedToM5(reversed, origin, date) == routedToM5(day, origin, date));
        }
    }
    CHECK(accountedOnce(selection, day.items.items.size()));
    m5Evidence::note("expected", {"2027 on 6 Aug and 2028 on 7 Aug route to M5", "20-reserved dates stay in M4",
                                  "reversing goods and placeholder rows leaves routing unchanged"});
}

TEST_CASE("X-ROUTE-04/synthetic: two 11-reserved lanes with the same goods never become a 22-reserved lane" * doctest::test_suite("m5")) {
    const Day day = dayOf({group("2027"), group("2028")}, 10, join(itemsOf(0, 5, 0), itemsOf(1, 5, 5)), {reserved("2027", 11), reserved("2028", 11)});
    for (const LaneRoute& route : day.classification.routes) CHECK(route.method != SolveMethod::M5);
    CHECK(plan(day, withPlanner()).largeLanes.empty());
    const Day destinations = dayOf({groupOn("2027", "2528", "TL"), groupOn("2027", "2530", "TL")}, 10,
                                    join(itemsOf(0, 5, 0), itemsOf(1, 5, 5)),
                                    {reservedOn("2027", "2528", "TL", 11), reservedOn("2027", "2530", "TL", 11)});
    CHECK(destinations.book.reservedTrucks == 22);
    REQUIRE(destinations.classification.routes.size() == 2);
    for (const LaneRoute& route : destinations.classification.routes) {
        CHECK(route.reservedTrucks == 11);
        CHECK(route.method != SolveMethod::M5);
    }
    const SelectionPlan selected = plan(destinations, withPlanner());
    CHECK(selected.largeLanes.empty());
    CHECK(accountedOnce(selected, destinations.items.items.size()));
    m5Evidence::note("expected", {"different origins: two 11-truck lanes stay below T=20",
                                  "different destinations: two 11-truck lanes stay below T=20"});
}

TEST_CASE("X-ROUTE-10/synthetic: repetition does not change the 19 versus 21 reservation threshold" * doctest::test_suite("m5")) {
    const vector<SelectionItem> goods = itemsOf(0, 120, 0);
    const Day nineteen = dayOf({group("2027")}, goods.size(), goods, {reserved("2027", 19)});
    const Day twentyOne = dayOf({group("2027")}, goods.size(), goods, {reserved("2027", 21)});
    REQUIRE(nineteen.classification.routes.size() == 1);
    REQUIRE(twentyOne.classification.routes.size() == 1);
    CHECK(nineteen.classification.routes[0].reservedTrucks == 19);
    CHECK(nineteen.classification.routes[0].method != SolveMethod::M5);
    CHECK(twentyOne.classification.routes[0].reservedTrucks == 21);
    CHECK(twentyOne.classification.routes[0].method == SolveMethod::M5);
    m5Evidence::note("expected", {"same 120 repetitive goods", "19 stays in M4", "21 routes to M5"});
}

TEST_CASE("X-ROUTE-09/synthetic: a forced M5 probe at 20 reserved leaves the next normal route in M4" * doctest::test_suite("m5")) {
    const Day day = dayOf({group("2027")}, 60, itemsOf(0, 60, 0), {reserved("2027", 20)});
    const SelectionPlan before = plan(day, withPlanner());
    REQUIRE(day.classification.routes.size() == 1);
    CHECK(day.classification.routes[0].method != SolveMethod::M5);
    CHECK(before.largeLanes.empty());
    const m5::M5LaneDateInput forced = m5Fixtures::laneWith(m5Fixtures::fx30Units("probe", 60), 20);
    const m5::M5LaneOutcome probe = m5Fixtures::plan(forced, m5Fixtures::syntheticProfile());
    REQUIRE(probe.selected);
    CHECK(probe.reservedTrucks == 20);
    CHECK(m5Fixtures::shippedCount(probe) == 60);
    const SelectionPlan after = plan(day, withPlanner());
    CHECK(after.largeLanes.empty());
    CHECK(day.classification.routes[0].method != SolveMethod::M5);
    CHECK(after.trucks.size() == before.trucks.size());
    CHECK(trucksByGroup(after) == trucksByGroup(before));
    CHECK(after.leftOver.size() == before.leftOver.size());
    CHECK(accountedOnce(after, day.items.items.size()));
    m5Evidence::note("expected", {"forced M5 probe plans 60 on a 20-slot lane", "normal route before and after stays M4",
                                  "normal truck membership and leftovers unchanged"});
}

TEST_CASE("X-ROUTE-06/synthetic: invalid thresholds and block sizes are rejected before planning" * doctest::test_suite("m5")) {
    nlohmann::json shipped;
    {
        ifstream file("config/orderBuilderParams.json");
        REQUIRE(file.good());
        file >> shipped;
    }
    const filesystem::path path = testTemp::runTempFolder() / "xRoute06Params.json";
    const auto thresholdError = [&](nlohmann::json params) {
        ofstream file(path, ios::binary);
        file << params.dump();
        file.close();
        try {
            (void)loadSelectionParams(path.string());
        } catch (const runtime_error& error) {
            return string(error.what());
        }
        return string();
    };
    for (const nlohmann::json& bad : {nlohmann::json(-1), nlohmann::json(0), nlohmann::json(1.5),
                                      nlohmann::json(1.0e300)}) {
        nlohmann::json params = shipped;
        params["selection"]["m5Threshold"] = bad;
        CHECK(thresholdError(params).find("m5Threshold") != string::npos);
        nlohmann::json block = shipped["m5"];
        block["blockSize"] = bad;
        try {
            (void)m5::loadProfile(block);
            FAIL_CHECK("invalid block size accepted");
        } catch (const runtime_error& error) {
            CHECK(string(error.what()).find("blockSize") != string::npos);
        }
    }
    nlohmann::json missingThreshold = shipped;
    missingThreshold["selection"].erase("m5Threshold");
    CHECK(thresholdError(missingThreshold).find("m5Threshold") != string::npos);
    nlohmann::json missingBlock = shipped["m5"];
    missingBlock.erase("blockSize");
    CHECK_THROWS_AS(m5::loadProfile(missingBlock), runtime_error);
    m5Evidence::note("expected", {"negative, zero, fractional, oversized and missing threshold refused by name",
                                  "same invalid block-size inputs refused before planning"});
}

TEST_CASE("X-ROUTE-05 X-RES-04 a lane-date on two rows is not summed; separate lanes keep their own" * doctest::test_suite("m5")) {
    const ReservationBook book =
        buildReservationBook({reserved("2027", 11), reserved("2027", 11), reserved("2028", 21)}, kCtl, kPlanDate, {"53F", {"53F"}});
    CHECK(findReservation(book, "2027", "2528", "TL", "2026-08-06") == nullptr);
    REQUIRE(findReservation(book, "2028", "2528", "TL", "2026-08-06") != nullptr);
    CHECK(findReservation(book, "2028", "2528", "TL", "2026-08-06")->trucks == 21);
    CHECK(count_if(book.skipped.begin(), book.skipped.end(), [](const SkippedPlaceholder& skipped) {
              return skipped.reason == PlaceholderSkipReason::DuplicateLaneDate;
          }) == 2);
}

TEST_CASE("X-RES-05 a reservation count out of range is refused with its row; zero is no trucks" * doctest::test_suite("m5")) {
    PlaceholderRecord negative = reserved("2027", -1), zero = reserved("2028", 0);
    const ReservationBook book = buildReservationBook({negative, zero}, kCtl, kPlanDate, {"53F", {"53F"}});
    CHECK(book.reservations.empty());
    REQUIRE(book.skipped.size() == 2);
    CHECK(book.skipped[0].reason == PlaceholderSkipReason::InvalidLoadCount);
    CHECK(book.skipped[1].reason == PlaceholderSkipReason::ZeroLoads);
}

TEST_CASE("X-RES-04/synthetic: several labels for one lane-date never double its reservation; another lane's trucks are never used" * doctest::test_suite("m5")) {
    const ReservationBook book =
        buildReservationBook({reserved("2027", 11), reserved("2027", 11), reserved("2028", 21)}, kCtl, kPlanDate, {"53F", {"53F"}});
    // The repeated lane-date is refused as ambiguous (no row identity tells a repeat from a second request).
    CHECK(findReservation(book, "2027", "2528", "TL", "2026-08-06") == nullptr);
    REQUIRE(findReservation(book, "2028", "2528", "TL", "2026-08-06") != nullptr);
    CHECK(findReservation(book, "2028", "2528", "TL", "2026-08-06")->trucks == 21);
    CHECK(book.reservedTrucks == 21);
    CHECK(book.skippedTrucks == 22);
    // The same products on the two lanes: only the 21-truck lane goes to M5, with its own pool only.
    const Day day = dayOf({group("2027"), group("2028")}, 10, join(itemsOf(0, 5, 0), itemsOf(1, 5, 5)),
                          {reserved("2027", 11), reserved("2027", 11), reserved("2028", 21)});
    CHECK_FALSE(routedToM5(day, "2027", "2026-08-06"));
    CHECK(routedToM5(day, "2028", "2026-08-06"));
    const SelectionPlan selection = plan(day, withPlanner());
    REQUIRE(selection.largeLanes.size() == 1);
    CHECK(selection.largeLanes[0].text.find("reserved: 21\n") != string::npos);
    CHECK(accountedOnce(selection, day.items.items.size()));
    m5Evidence::note("expected", {"2027 labels refused, never 22", "2028 keeps 21", "M5 pool 21 for 2028 only"});
}

TEST_CASE("X-ROUTE-05/synthetic: a repeated reservation record never doubles available trucks" * doctest::test_suite("m5")) {
    const PlaceholderRecord row = reserved("2027", 21);
    const ReservationBook once = buildReservationBook({row}, kCtl, kPlanDate, {"53F", {"53F"}});
    const ReservationBook twice = buildReservationBook({row, row}, kCtl, kPlanDate, {"53F", {"53F"}});
    REQUIRE(findReservation(once, "2027", "2528", "TL", "2026-08-06"));
    CHECK(once.reservedTrucks == 21);
    CHECK(findReservation(twice, "2027", "2528", "TL", "2026-08-06") == nullptr);
    CHECK(twice.reservedTrucks == 0);
    CHECK(twice.skippedTrucks == 42);
    REQUIRE(twice.skipped.size() == 2);
    CHECK(twice.skipped[0].placeholderIndex == 0);
    CHECK(twice.skipped[1].placeholderIndex == 1);
    for (const SkippedPlaceholder& skipped : twice.skipped) {
        CHECK(skipped.reason == PlaceholderSkipReason::DuplicateLaneDate);
    }
    CHECK(string(describeSkipReason(twice.skipped[0].reason)) == "duplicate lane and ship date");
    m5Evidence::note("expected", {"one row reserves 21", "repeated row reserves 0, never 42", "both rows named duplicate lane and ship date"});
    m5Evidence::note("notCovered", "two genuine equal reservations cannot be distinguished from a repeated record: the placeholder feed has no reservation identity");
}

TEST_CASE("X-RES-05/synthetic: reserved counts -1, 1.5, NaN, infinity and huge are refused with their row; a real 0 is no trucks" * doctest::test_suite("m5")) {
    const filesystem::path folder = testTemp::runTempFolder() / "xres05";
    filesystem::create_directories(folder);
    const filesystem::path path = folder / "placeholders.json";
    {
        ofstream file(path, ios::binary);
        file << "{\"PHOLDER\": [";
        const vector<string> counts{"-1", "1.5", "1e300", "2000", "\"NaN\"", "\"Infinity\"", "0", "21"};
        for (size_t row = 0; row < counts.size(); ++row) {
            file << (row ? "," : "") << "{\"LOCFRNO\": \"" << 3000 + row << "\", \"LOCTONO\": \"2528\", \"SHIP_COND\": \"TL\","
                 << " \"DATFR_TA\": \"2026-08-06\", \"DATTO_TA\": \"2026-08-06\", \"ZZNA_EQUIP_SIZE\": \"53F\", \"NO_OF_LOADS\": " << counts[row] << "}";
        }
        file << "]}";
    }
    const PlaceholderLoadResult loaded = PlaceholderImporter::load(path.string());
    REQUIRE(loaded.placeholders.size() == 8);
    const ReservationBook book = buildReservationBook(loaded.placeholders, kCtl, kPlanDate, {"53F", {"53F"}});
    REQUIRE(book.reservations.size() == 1);
    CHECK(book.reservations.front().origin == "3007");
    CHECK(book.reservations.front().trucks == 21);
    map<size_t, PlaceholderSkipReason> reasonOf;
    for (const SkippedPlaceholder& skipped : book.skipped) reasonOf[skipped.placeholderIndex] = skipped.reason;
    REQUIRE(reasonOf.size() == 7);
    for (size_t row = 0; row < 6; ++row) {
        INFO("row " << row);
        CHECK(reasonOf.at(row) == PlaceholderSkipReason::InvalidLoadCount);
        // The skipped row names its lane and date through the placeholder it points at.
        CHECK(loaded.placeholders[row].locfrno == to_string(3000 + row));
        CHECK(loaded.placeholders[row].datfr_ta == "2026-08-06");
    }
    CHECK(reasonOf.at(6) == PlaceholderSkipReason::ZeroLoads);
    CHECK(book.reservedTrucks == 21);
    // JSON has no NaN or Infinity number: a bare NaN makes the file invalid and it is refused whole.
    const filesystem::path bare = folder / "bareNaN.json";
    {
        ofstream file(bare, ios::binary);
        file << "{\"PHOLDER\": [{\"LOCFRNO\": \"3000\", \"NO_OF_LOADS\": NaN}]}";
    }
    CHECK_THROWS_AS(PlaceholderImporter::load(bare.string()), runtime_error);
    m5Evidence::note("expected", {"-1, 1.5, 1e300, 2000, \"NaN\", \"Infinity\": InvalidLoadCount on their rows", "0: ZeroLoads", "21 kept", "bare NaN: file refused"});
}

TEST_CASE("X-ROUTE-07 routing follows reserved trucks, never the size of the demand" * doctest::test_suite("m5")) {
    for (const size_t goods : {size_t{1}, size_t{500}}) {
        const Day day = dayOf({group("2027")}, goods, itemsOf(0, goods, 0), {reserved("2027", 21)});
        CHECK(day.classification.routes.front().method == SolveMethod::M5);
    }
}

TEST_CASE("U-RT-07/synthetic X-ROUTE-08 RD-08: a raw-material lane over the threshold is never planned" * doctest::test_suite("m5")) {
    const Day day = dayOf({groupOn("2027", "2528", "TL_N102")}, 30, itemsOf(0, 30, 0), {reservedOn("2027", "2528", "TL_N102", 21)});
    REQUIRE(day.classification.routes.front().method == SolveMethod::M5);
    const SelectionPlan selection = plan(day, withPlanner());
    CHECK(selection.trucks.empty());
    REQUIRE(selection.largeLanes.size() == 1);
    CHECK_FALSE(selection.largeLanes[0].planned);
    CHECK(selection.largeLanes[0].outcome.find("out of scope") != string::npos);
    REQUIRE(selection.leftOver.size() == 30);
    for (const LeftOverItem& left : selection.leftOver) CHECK(left.reason == LeftOverReason::M5InvalidInput);
    CHECK(accountedOnce(selection, day.items.items.size()));
    // Control: the same lane on a finished-goods condition is planned.
    const Day finished = dayOf({groupOn("2027", "2528", "TL")}, 30, itemsOf(0, 30, 0), {reservedOn("2027", "2528", "TL", 21)});
    const SelectionPlan planned = plan(finished, withPlanner());
    REQUIRE(planned.largeLanes.size() == 1);
    CHECK(planned.largeLanes[0].planned);
    CHECK_FALSE(planned.trucks.empty());
    m5Evidence::note("expected", {"TL_N102 over 20: not planned, 30 goods left as out of scope", "TL control planned"});
}

TEST_CASE("U-RT-06/synthetic: zero reserved with valid demand: no M5, no truck, reported as no reservation; a forced M5 run invents no capacity" * doctest::test_suite("m5")) {
    const Day day = dayOf({group("2027")}, 10, itemsOf(0, 10, 0), {reserved("2027", 0)});
    for (const LaneRoute& route : day.classification.routes) CHECK(route.method != SolveMethod::M5);
    const SelectionPlan selection = plan(day, withPlanner());
    CHECK(selection.trucks.empty());
    CHECK(selection.largeLanes.empty());
    REQUIRE(selection.leftOver.size() == 10);
    for (const LeftOverItem& left : selection.leftOver) CHECK(left.reason == LeftOverReason::NoReservation);
    CHECK(accountedOnce(selection, day.items.items.size()));
    // Test-only forcing: M5 itself on a lane-date of 0 slots.
    const m5::M5LaneDateInput forced = m5Fixtures::laneWith(m5Fixtures::fx30Units("u", 10), 0);
    const m5::M5LaneOutcome outcome = m5Fixtures::plan(forced, m5Fixtures::syntheticProfile());
    CHECK(outcome.kind == m5::OutcomeKind::CompletedZeroDispatch);
    CHECK_FALSE(outcome.hasCandidate);
    CHECK_FALSE(outcome.selected.has_value());
    CHECK(outcome.reservedTrucks == 0);
    REQUIRE(outcome.leftOver.size() == 10);
    for (const m5::LeftOver& left : outcome.leftOver) CHECK(left.cause == m5::LeftOverCause::OverReservation);
    m5Evidence::note("expected", {"0 reserved: M4 lane, no truck, 10 goods NoReservation", "forced M5: zero dispatch, 0 slots, 10 left"});
}

TEST_CASE("U-SP-03/synthetic U-SP-05/synthetic: a lane ID with underscores is one origin and one destination; goods that may mix stay one piece" * doctest::test_suite("m5")) {
    const Day day = dayOf({groupOn("1757_1724_1725", "2528", "TL")}, 40, itemsOf(0, 40, 0), {reservedOn("1757_1724_1725", "2528", "TL", 21)});
    REQUIRE(day.classification.routes.size() == 1);
    const SelectionPlan selection = plan(day, withPlanner());
    REQUIRE(selection.largeLanes.size() == 1);
    CHECK(selection.largeLanes[0].origin == "1757_1724_1725");
    CHECK(selection.largeLanes[0].destination == "2528");
    CHECK(selection.largeLanes[0].planned);
    CHECK(selection.largeLanes[0].text.find("multi-stop") == string::npos);
    CHECK(selection.trucks.size() == 2);
    for (const PlannedTruck& truck : selection.trucks) CHECK(truck.groupIndex == 0);
    CHECK(selection.largeLanes[0].text.find("groups sharing the reservation: group 0\n") != string::npos);
    CHECK(accountedOnce(selection, day.items.items.size()));
    m5Evidence::note("expected", {"1757_1724_1725 -> 2528 is one lane, not multi-stop", "one group, one piece, 2 trucks"});
}

TEST_CASE("X-DATE-11 X-DATE-12 lanes never borrow each other's trucks; M5 makes up no date" * doctest::test_suite("m5")) {
    const vector<SelectionItem> goods = join(itemsOf(0, 1000, 0), itemsOf(1, 10, 1000));
    const Day day = dayOf({groupOn("2027", "2528", "TL"), groupOn("2027", "2600", "TL")}, 1010, goods,
                          {reservedOn("2027", "2528", "TL", 21), reservedOn("2027", "2600", "TL", 21)});
    const SelectionPlan selection = plan(day, withPlanner());
    map<size_t, size_t> trucksOf;
    for (const PlannedTruck& truck : selection.trucks) {
        ++trucksOf[truck.groupIndex];
        CHECK(truck.shipDate == "2026-08-06");
        for (const size_t index : truck.items) CHECK(day.items.items[index].groupIndex == truck.groupIndex);
    }
    CHECK(trucksOf[0] == 21);
    CHECK(trucksOf[1] == 1);
}

TEST_CASE("X-DATE-11/synthetic: same-product lanes to different destinations use only their own reservations" * doctest::test_suite("m5")) {
    Day day = dayOf({groupOn("2027", "2528", "TL"), groupOn("2027", "2600", "TL")}, 80,
                    join(itemsOf(0, 60, 0), itemsOf(1, 20, 60)),
                    {reservedOn("2027", "2528", "TL", 21), reservedOn("2027", "2600", "TL", 21)});
    for (STRRecord& line : day.run.demand.str) line.matnr = "SAME";
    day.classification = classifyLanes(day.run, day.book, day.items, kRouting);
    const SelectionPlan selected = plan(day, withPlanner());
    REQUIRE(selected.largeLanes.size() == 2);
    map<size_t, size_t> trucks;
    for (const PlannedTruck& truck : selected.trucks) {
        ++trucks[truck.groupIndex];
        for (const size_t index : truck.items) CHECK(day.items.items[index].groupIndex == truck.groupIndex);
    }
    CHECK(trucks[0] > 0);
    CHECK(trucks[1] > 0);
    CHECK(trucks[0] <= 21);
    CHECK(trucks[1] <= 21);
    CHECK(accountedOnce(selected, day.items.items.size()));
    m5Evidence::note("expected", {"same product and origin, destinations 2528 and 2600", "each uses only its 21-slot pool"});
}

TEST_CASE("X-DATE-12/synthetic: data beyond M4's period cannot create a new M5 departure" * doctest::test_suite("m5")) {
    const Day day = dayOf({group("2027")}, 40,
                          join(itemsOf(0, 30, 0, "2026-08-06"), itemsOf(0, 10, 30, "2026-08-08")),
                          {reserved("2027", 21, "2026-08-06"), reserved("2027", 21, "2026-08-08")});
    CHECK(day.book.window.lastShipDate == "2026-08-07");
    CHECK(findReservation(day.book, "2027", "2528", "TL", "2026-08-08") == nullptr);
    const SelectionPlan selected = plan(day, withPlanner());
    for (const PlannedTruck& truck : selected.trucks) CHECK(truck.shipDate != "2026-08-08");
    for (const LargeLaneReport& lane : selected.largeLanes) CHECK(lane.shipDate != "2026-08-08");
    CHECK(selected.leftOver.size() == 10);
    for (const LeftOverItem& left : selected.leftOver) CHECK(day.items.items[left.itemIndex].shipDate == "2026-08-08");
    const m5::Atom beyondPull = m5Fixtures::fx30("beyond", "A", 0, m5Fixtures::dayAfter(11));
    CHECK_FALSE(m5::atomEligibility(beyondPull, m5Fixtures::kD, m5::DateRules{true, 10}).eligible);
    CHECK(accountedOnce(selected, day.items.items.size()));
    m5Evidence::note("expected", {"M4 period ends 7 Aug", "8 Aug reservation skipped", "no M5 truck or lane-date on 8 Aug"});
}

TEST_CASE("R-6/m4Path one truck, two do-not-mix groups on one lane: M4's selector gives it to eleven 48 x 40, not to twelve 60 x 60 of which ten fit" * doctest::test_suite("m5")) {
    // Group A: twelve 60 x 60 stacks; group B: eleven 48 x 40 stacks; all P0, one reserved truck on the lane.
    vector<SelectionItem> goods;
    for (size_t unit = 0; unit < 12; ++unit) goods.push_back(item(0, unit, 1000.0, 0, 60.0, 60.0));
    for (size_t unit = 0; unit < 11; ++unit) goods.push_back(item(1, 12 + unit, 1000.0, 0, 48.0, 40.0));
    const Day day = dayOf({group("2027", SplitReason::None, "A"), group("2027", SplitReason::None, "B")}, 23, goods, {reserved("2027", 1)});
    const SelectionPlan selected = plan(day, selectorSettings());
    const auto byGroup = trucksByGroup(selected);
    REQUIRE(byGroup.count(1) == 1);
    REQUIRE(byGroup.at(1).size() == 1);
    CHECK(byGroup.at(1).front().size() == 11);
    CHECK(byGroup.count(0) == 0);
    CHECK(accountedOnce(selected, day.items.items.size()));
    m5Evidence::note("expected", {"M4 selector, no planner: group B's eleven ship on the one truck", "group A's twelve stay left over (only ten fit)"});
}

TEST_CASE("R-5b/m4Path seeded single trucks of 6-10 one-high stacks through M4's selector are never worse than the exact best" * doctest::test_suite("m5")) {
    struct Footprint {
        double longSide, shortSide;
    };
    const vector<Footprint> shapes{{72, 60}, {80, 48}, {120, 48}, {48, 48}, {96, 40}, {60, 60}, {40, 33}, {100, 50},
                                   {110, 40}, {90, 55}, {80, 60}, {100, 60}, {48, 36}, {48, 40}, {44, 40}, {100, 20}};
    constexpr unsigned seed = 20261121;
    mt19937 generator(seed);
    for (int round = 0; round < 25; ++round) {
        const size_t count = uniform_int_distribution<size_t>(6, 10)(generator);
        vector<SelectionItem> goods;
        vector<m5::Atom> atoms;
        string described;
        for (size_t stack = 0; stack < count; ++stack) {
            const Footprint shape = shapes[uniform_int_distribution<size_t>(0, shapes.size() - 1)(generator)];
            const int priority = uniform_int_distribution<int>(0, 1)(generator) == 0 ? 0 : 11;
            goods.push_back(item(0, stack, 2500.0, priority, shape.longSide, shape.shortSide));
            atoms.push_back(m5Fixtures::unitOf("s" + to_string(100 + stack), "A", shape.longSide, shape.shortSide, 50.0, 2500.0, priority));
            described += to_string(static_cast<int>(shape.longSide)) + "x" + to_string(static_cast<int>(shape.shortSide)) + "/P" + to_string(priority) + " ";
        }
        const Day day = dayOf({group("2027")}, count, goods, {reserved("2027", 1)});
        // The exact best over every subset, judged by M4's floor and the payload: P0 stacks first, then P11.
        pair<int, int> best{0, 0};
        for (unsigned mask = 1; mask < (1u << count); ++mask) {
            pair<int, int> value{0, 0};
            double weight = 0.0;
            vector<ob::FloorItem> floors;
            for (size_t stack = 0; stack < count; ++stack) {
                if (!(mask & (1u << stack))) continue;
                (goods[stack].priority == 0 ? value.first : value.second) += 1;
                weight += goods[stack].weightLb;
                floors.push_back(atoms[stack].floor);
            }
            if (value <= best || weight > m5Fixtures::kDefaultTrailer.maxWeightLb) continue;
            const ob::FloorLayout layout = ob::arrangeFloor(floors, m5::trailerOf(m5Fixtures::kDefaultTrailer), m5Fixtures::physicalSettings().floor);
            if (layout.fitsLength && layout.unplaceableItems.empty()) best = value;
        }
        const SelectionPlan selected = plan(day, selectorSettings());
        pair<int, int> shipped{0, 0};
        for (const PlannedTruck& truck : selected.trucks) {
            for (const size_t index : truck.items) (day.items.items[index].priority == 0 ? shipped.first : shipped.second) += 1;
        }
        INFO("seed " << seed << " round " << round << " stacks " << count << ": " << described);
        CHECK(shipped >= best);
        CHECK(accountedOnce(selected, day.items.items.size()));
    }
    m5Evidence::note("expected", {"25 rounds, seed 20261121, 6 to 10 one-high stacks", "the selector's truck is never worse than the exact best over every subset"});
    m5Evidence::note("notCovered", "the oracle judges each subset with M4's own floor arrangement; an independent exact row-grouping enumerator is not built");
}

TEST_CASE("X-STATE-03/synthetic: an M4 lane plans the same with an unrelated M5 lane beside it" * doctest::test_suite("m5")) {
    const Day day = dayOf({group("2027"), group("2028")}, 60, join(itemsOf(0, 30, 0), itemsOf(1, 30, 30)),
                          {reserved("2027", 20), reserved("2028", 21)});
    const SelectionPlan withPlan = plan(day, withPlanner());
    const SelectionPlan withoutPlan = plan(day, selectorSettings());
    const auto withM5 = trucksByGroup(withPlan);
    const auto withoutM5 = trucksByGroup(withoutPlan);
    CHECK(withM5.at(0) == withoutM5.at(0));
    CHECK(withM5.count(1) == 1);
    CHECK(withoutM5.count(1) == 0);
    // The frozen lane's goods are untouched and none of them rides on an M5 truck.
    set<size_t> frozenGoods;
    for (const vector<size_t>& truck : withoutM5.at(0)) frozenGoods.insert(truck.begin(), truck.end());
    for (const vector<size_t>& truck : withM5.at(1)) {
        for (const size_t item : truck) CHECK(frozenGoods.count(item) == 0);
    }
    CHECK(accountedOnce(withPlan, day.items.items.size()));
    CHECK(accountedOnce(withoutPlan, day.items.items.size()));
    m5Evidence::note("expected", {"the M4 lane's trucks are the same with and without the M5 planner", "the M5 lane gets its own trucks, sharing no goods"});
}

TEST_CASE("X-RUN-01 U-PF-03 the same input plans the same at 1, 2 and 8 threads" * doctest::test_suite("m5")) {
    vector<SegregatedGroup> groups;
    vector<SelectionItem> goods;
    vector<PlaceholderRecord> reservations;
    for (size_t lane = 0; lane < 4; ++lane) {
        const string origin = to_string(3000 + lane);
        groups.push_back(group(origin));
        goods = join(goods, itemsOf(lane, 200 + 50 * lane, goods.size()));
        reservations.push_back(reserved(origin, 21));
    }
    const Day day = dayOf(groups, goods.size(), goods, reservations);
    const SelectionPlan one = plan(day, withPlanner(1));
    for (const int threads : {2, 8}) {
        const SelectionPlan many = plan(day, withPlanner(threads));
        REQUIRE(many.trucks.size() == one.trucks.size());
        for (size_t truck = 0; truck < one.trucks.size(); ++truck) CHECK(many.trucks[truck].items == one.trucks[truck].items);
        CHECK(many.leftOver.size() == one.leftOver.size());
    }
}

TEST_CASE("U-RS-05/synthetic: a priority of -1, 12, 99, empty, text or 1.5 is refused and reported, never clamped" * doctest::test_suite("m5")) {
    // Through the importer and selection: each bad TPRIO is counted and its goods never ship.
    const vector<nlohmann::json> badPriorities{-1, 12, 99, "", "x", 1.5};
    string extraLines;
    int lineNumber = 20;
    for (const nlohmann::json& priority : badPriorities) {
        nlohmann::json line = m4SmallDay::SmallDay::lineJson("OTHER", 8, "TL");
        line["TPRIO"] = priority;
        line["BNFPO"] = lineNumber++;
        extraLines += "," + line.dump();
    }
    const m4SmallDay::SmallDay base("uRs05Base");
    const m4SmallDay::SmallDay bad("uRs05Bad", extraLines);
    const PipelineResult before = Pipeline::run(base.inputs);
    const PipelineResult after = Pipeline::run(bad.inputs);
    CHECK(after.validation.unreadablePriority == 6);
    size_t refused = 0;
    for (const RejectedItem& rejected : after.selectionItems.rejected) refused += rejected.reason == ItemRejection::UnreadablePriority ? 1 : 0;
    CHECK(refused >= 1);
    CHECK(after.selectionPlan.trucks.size() == before.selectionPlan.trucks.size());
    double shippedBefore = 0.0, shippedAfter = 0.0;
    for (const PlannedTruck& truck : before.selectionPlan.trucks) shippedBefore += truck.unitLoads;
    for (const PlannedTruck& truck : after.selectionPlan.trucks) shippedAfter += truck.unitLoads;
    CHECK(shippedAfter == shippedBefore);
    // At M5's own boundary an out-of-range priority refuses the lane-date's input.
    for (const int priority : {-1, 12, 99}) {
        m5::M5LaneDateInput input = m5Fixtures::laneWith(m5Fixtures::fx30Units("u", 3), 1);
        input.atoms[1].components[0].priority = priority;
        CHECK(m5Fixtures::plan(input, m5Fixtures::syntheticProfile()).kind == m5::OutcomeKind::InvalidInput);
    }
    m5Evidence::note("expected", {"6 bad TPRIO lines counted unreadable", "their goods rejected, plan unchanged", "M5: -1, 12, 99 refused"});
}

TEST_CASE("U-DA-06/synthetic: a combo lane stops the run with a clear message (multi-stop and temperature class not covered)" * doctest::test_suite("m5")) {
    const m4SmallDay::SmallDay combo("uDa06Combo", "," + m4SmallDay::SmallDay::line("GOOD", 16, "TF"));
    string message;
    try {
        Pipeline::run(combo.inputs);
    } catch (const runtime_error& error) {
        message = error.what();
    }
    INFO(message);
    CHECK(message.find("M4 does not plan combo lanes") != string::npos);
    CHECK(message.find("1 -> 2 under TF and TL") != string::npos);
    // Control: the same day without the TF line plans.
    const m4SmallDay::SmallDay plain("uDa06Plain");
    const PipelineResult result = Pipeline::run(plain.inputs);
    CHECK_FALSE(result.selectionPlan.trucks.empty());
    m5Evidence::note("expected", {"combo lane 1 -> 2 (TF and TL): run stops naming it", "TL only: planned"});
    // The input has no field that carries a multi-stop load or a temperature class (one LOCFRNO and LOCTONO
    // per line, no temperature), so those two clauses cannot be fed in (decision D11-1, question for Tom).
    m5Evidence::note("notCovered", "multi-stop and temperature class: no input field carries them today, so they cannot be fed in");
}

TEST_CASE("U-DA-06 X-RUN-08/synthetic a parked feature found after other goods were processed stops the run; no earlier truck is published" * doctest::test_suite("m5")) {
    // The parked TF row is the last line: every TL line before it is already read and joined.
    const m4SmallDay::SmallDay day("comboLane", "," + m4SmallDay::SmallDay::line("GOOD", 16, "TF"));
    bool published = false;
    try {
        const PipelineResult result = Pipeline::run(day.inputs);
        published = !result.selectionPlan.trucks.empty();
    } catch (const runtime_error& error) {
        CHECK(string(error.what()).find("combo") != string::npos);
    }
    CHECK_FALSE(published);
    CHECK_THROWS_AS(Pipeline::run(day.inputs), runtime_error);
    const m4SmallDay::SmallDay plain("comboLanePlain");
    CHECK_FALSE(Pipeline::run(plain.inputs).selectionPlan.trucks.empty());
    m5Evidence::note("expected", {"parked feature after ordinary goods: the run throws, no plan is returned", "the same day without it plans"});
}

TEST_CASE("U-DA-01/synthetic X-DATA-08: CS, DIS and PAL all divide by Cases_Unit_Load; a DIS is never one whole unit load" * doctest::test_suite("m5")) {
    CHECK(Converter::to_pallets(240, "CS", product("a", 10)) == 24.0);
    CHECK(Converter::to_pallets(432, "DIS", product("b", 18)) == 24.0);
    CHECK(Converter::to_pallets(60, "PAL", product("c", 2)) == 30.0);
    // One display is 1/18 of a unit load, not a unit load of its own; part loads are kept, not rounded.
    CHECK(Converter::to_pallets(1, "DIS", product("b", 18)) == 1.0 / 18.0);
    CHECK(Converter::to_pallets(9, "DIS", product("b", 18)) == 0.5);
    CHECK(Converter::to_pallets(433, "DIS", product("b", 18)) == 433.0 / 18.0);
    m5Evidence::note("expected", {"240 CS / 10 = 24", "432 DIS / 18 = 24", "60 PAL / 2 = 30", "9 DIS / 18 = 0.5"});
}

TEST_CASE("X-DATA-08/synthetic: 24 CS, 24 DIS and 30 PAL unit loads reach both M5 methods" * doctest::test_suite("m5")) {
    const size_t cs = static_cast<size_t>(Converter::to_pallets(240, "CS", product("cs", 10)));
    const size_t dis = static_cast<size_t>(Converter::to_pallets(432, "DIS", product("dis", 18)));
    const size_t pal = static_cast<size_t>(Converter::to_pallets(60, "PAL", product("pal", 2)));
    REQUIRE(cs == 24);
    REQUIRE(dis == 24);
    REQUIRE(pal == 30);
    vector<m5::Atom> atoms;
    for (size_t index = 0; index < cs + dis + pal; ++index) {
        m5::Atom atom = m5Fixtures::display("load-" + to_string(index));
        atom.components[0].product = index < cs ? "CS" : index < cs + dis ? "DIS" : "PAL";
        atoms.push_back(atom);
    }
    const m5::M5LaneDateInput input = m5Fixtures::laneWith(atoms, 3);
    for (const m5::MethodPolicy method : {m5::MethodPolicy::PatternOnly, m5::MethodPolicy::BlocksOnly}) {
        m5::M5PolicyProfile profile = m5Fixtures::syntheticProfile();
        profile.method = method;
        const m5::M5LaneOutcome outcome = m5Fixtures::plan(input, profile);
        CHECK(m5Fixtures::shippedCount(outcome) == 78);
        REQUIRE(outcome.selected);
        map<string, size_t> byProduct;
        for (const m5::TruckPlan& truck : outcome.selected->trucks) {
            for (const size_t index : truck.atoms) ++byProduct[input.atoms[index].components[0].product];
        }
        CHECK(byProduct["CS"] == 24);
        CHECK(byProduct["DIS"] == 24);
        CHECK(byProduct["PAL"] == 30);
    }
    m5Evidence::note("expected", {"240 CS / 10 = 24", "432 DIS / 18 = 24", "60 PAL / 2 = 30", "A and B each ship 78"});
}

TEST_CASE("U-DA-02/synthetic: a line in eaches is caught and reported; it adds no truck" * doctest::test_suite("m5")) {
    CHECK_FALSE(Converter::isConvertibleUom("EA"));
    CHECK(Converter::to_pallets(5000, "EA", product("a", 10)) == 0.0);
    const m4SmallDay::SmallDay base("uDa02Base");
    nlohmann::json eaches = m4SmallDay::SmallDay::lineJson("GOOD", 5000, "TL");
    eaches["UNITOFMEAS"] = "EA";
    eaches["BNFPO"] = 20;
    const m4SmallDay::SmallDay withEaches("uDa02Eaches", "," + eaches.dump());
    const PipelineResult before = Pipeline::run(base.inputs);
    const PipelineResult after = Pipeline::run(withEaches.inputs);
    CHECK(after.validation.unknown_uom == 1);
    CHECK(after.selectionPlan.trucks.size() == before.selectionPlan.trucks.size());
    const auto reported = find_if(after.stackReport.unstackedLines.begin(), after.stackReport.unstackedLines.end(),
                                  [](const ReportedLine& line) { return line.reason.find("EA") != string::npos; });
    REQUIRE(reported != after.stackReport.unstackedLines.end());
    CHECK(reported->matnr == "GOOD");
    m5Evidence::note("expected", {"5,000 EA: unknown unit of measure, 1 line reported by product", "trucks unchanged"});
}

TEST_CASE("U-DA-02 X-DATA-03 X-DATA-04 eaches, zero divisors and part pallets follow M4's rule" * doctest::test_suite("m5")) {
    CHECK_FALSE(Converter::isConvertibleUom("EA"));
    CHECK(Converter::to_pallets(500, "EA", product("a", 10)) == 0.0);
    CHECK(Converter::to_pallets(9, "CS", product("b", 0)) == 0.0);
    CHECK(Converter::to_pallets(9, "CS", product("c", 2)) == 4.5);
}

TEST_CASE("X-DATA-03/synthetic: bad Cases_Unit_Load cells report the product and demand line" * doctest::test_suite("m5")) {
    const string header = "ID,Description,Length,Width,Height,Strength,UoM,Weight,Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n";
    for (const string cell : {"0", "-2", "", "NaN", "Infinity"}) {
        const m4SmallDay::SmallDay day("xData03_" + to_string(cell.size()) + "_" + (cell.empty() ? "missing" : cell));
        ofstream(day.directory / "products.csv") << header
            << "GOOD,Good,10,10,10,5,CS,20,4,2," << cell << ",TLD\n"
            << "OTHER,Other,10,10,10,5,CS,30,4,2,8,TLD\n";
        const PipelineResult result = Pipeline::run(day.inputs);
        const auto issue = find_if(result.validation.issues.begin(), result.validation.issues.end(),
                                   [](const ValidationIssue& value) { return value.matnr == "GOOD" && value.line_index == 0; });
        INFO("Cases_Unit_Load=" << cell);
        REQUIRE(issue != result.validation.issues.end());
        const auto unstacked = find_if(result.stackReport.unstackedLines.begin(), result.stackReport.unstackedLines.end(),
                                       [](const ReportedLine& value) { return value.matnr == "GOOD" && value.lineIndex == 0; });
        REQUIRE(unstacked != result.stackReport.unstackedLines.end());
        CHECK_FALSE(unstacked->reason.empty());
    }
    m5Evidence::note("expected", {"0, negative, missing, NaN and Infinity never divided", "GOOD line 0 reported each time"});
}

TEST_CASE("X-DATA-04/synthetic: nine cases on a two-case unit load remain four and a half" * doctest::test_suite("m5")) {
    const vector<ProductRecord> onlyType{product("partial", 2)};
    const Joiner::PalletTypePick picked = Joiner::choosePalletType(onlyType, 9.0);
    REQUIRE(picked.product != nullptr);
    CHECK(picked.product->cases_unit_load == 2);
    const double loads = Converter::to_pallets(9.0, "CS", *picked.product);
    CHECK(loads == 4.5);
    CHECK(loads != 4.0);
    CHECK(loads != 5.0);
    m5Evidence::note("expected", {"9 / 2 = 4.5", "no other pallet variant", "not rounded to 4 or 5"});
}

TEST_CASE("X-DATA-05/synthetic: reversed master variants yield the same M4 pick and M5 identity" * doctest::test_suite("m5")) {
    const vector<ProductRecord> master{product("dual", 2, "PTL"), product("dual", 3, "TLD")};
    const vector<ProductRecord> reversed{master.rbegin(), master.rend()};
    const Joiner::PalletTypePick first = Joiner::choosePalletType(master, 12.0);
    const Joiner::PalletTypePick second = Joiner::choosePalletType(reversed, 12.0);
    REQUIRE(first.product != nullptr);
    REQUIRE(second.product != nullptr);
    CHECK(first.choice == PalletTypeChoice::SeveralDivide);
    CHECK(second.choice == PalletTypeChoice::SeveralDivide);
    CHECK(first.product->pallet_id == second.product->pallet_id);
    CHECK(first.product->cases_unit_load == second.product->cases_unit_load);
    for (const ProductRecord* chosen : {first.product, second.product}) {
        m5::Atom atom = m5Fixtures::fx30("picked");
        atom.components[0].product = chosen->id + "/" + chosen->pallet_id;
        const m5::M5LaneDateInput input = m5Fixtures::laneWith({atom}, 1);
        const m5::M5LaneOutcome outcome = m5Fixtures::plan(input, m5Fixtures::syntheticProfile());
        CHECK(m5Fixtures::shippedCount(outcome) == 1);
        CHECK(input.atoms[0].components[0].product == chosen->id + "/" + first.product->pallet_id);
    }
    m5Evidence::note("expected", {"12 divides by 2 and 3", "row order chooses the same pallet variant", "M5 preserves chosen identity"});
}

TEST_CASE("U-DA-03/synthetic: a product missing from the master is reported by its ID, never silently dropped" * doctest::test_suite("m5")) {
    const m4SmallDay::SmallDay day("uDa03Missing", "," + m4SmallDay::SmallDay::line("MISSING", 80, "TL"));
    const PipelineResult result = Pipeline::run(day.inputs);
    CHECK(result.join.unmatched_lines == 1);
    CHECK(find(result.join.unmatched_matnrs.begin(), result.join.unmatched_matnrs.end(), "MISSING") != result.join.unmatched_matnrs.end());
    CHECK(result.validation.unmatched_product == 1);
    const auto reported = find_if(result.stackReport.unstackedLines.begin(), result.stackReport.unstackedLines.end(),
                                  [](const ReportedLine& line) { return line.matnr == "MISSING"; });
    REQUIRE(reported != result.stackReport.unstackedLines.end());
    CHECK_FALSE(reported->reason.empty());
    m5Evidence::note("expected", {"MISSING unmatched once, reported by ID with a reason"});
}

TEST_CASE("U-DA-04/synthetic X-DATA-05: the pallet type is chosen by whole pallets as in M4; ambiguous and non-whole results are reported, never rounded" * doctest::test_suite("m5")) {
    const vector<ProductRecord> types{product("p", 5, "PTL"), product("p", 4, "TLD")};
    const Joiner::PalletTypePick divides = Joiner::choosePalletType(types, 12.0);
    CHECK(divides.choice == PalletTypeChoice::DividesEvenly);
    CHECK(divides.product->cases_unit_load == 4);
    const vector<ProductRecord> both{product("q", 2, "PTL"), product("q", 3, "TLD")};
    const vector<ProductRecord> reversed{both[1], both[0]};
    const Joiner::PalletTypePick forward = Joiner::choosePalletType(both, 12.0);
    const Joiner::PalletTypePick backward = Joiner::choosePalletType(reversed, 12.0);
    CHECK(forward.choice == PalletTypeChoice::SeveralDivide);
    CHECK(forward.product->pallet_id == backward.product->pallet_id);
    // 7 cases on types of 2 and 3: none divides; the pick is flagged and the part load stays a part.
    const Joiner::PalletTypePick none = Joiner::choosePalletType(both, 7.0);
    CHECK(none.choice == PalletTypeChoice::NoneDivides);
    REQUIRE(none.product != nullptr);
    const double pallets = Converter::to_pallets(7, "CS", *none.product);
    CHECK(pallets == 7.0 / none.product->cases_unit_load);
    CHECK(pallets != floor(pallets));
    m5Evidence::note("expected", {"12 on 5/4: TLD 4 divides", "12 on 2/3: several, same pick either order", "7 on 2/3: none divides, 3.5 or 2.33 kept"});
}

namespace {

// Tom's 40 ft high-cube container, inside sizes as sent (smaller value of each range), in millimetres.
// Payload and cap here are synthetic values for the earlier millimetre checks.
nlohmann::json containerType(double payloadLb = 50000.0, int maxUnitLoads = 40) {
    return {{"equipment", "40HC"}, {"lengthMm", 12032}, {"widthMm", 2350}, {"heightMm", 2698},
            {"payloadLb", payloadLb}, {"maxUnitLoads", maxUnitLoads}};
}

nlohmann::json m5BlockWith(const nlohmann::json& trailerTypes) {
    nlohmann::json block = readParamsFile("config/orderBuilderParams.json")["m5"];
    block["trailerTypes"] = trailerTypes;
    return block;
}

const m5::TrailerCapability kTyped53F{630.0, 100.0, 108.0, 45000.0, 64};

shared_ptr<const m5::M5LargeLanePlanner> typedPlanner(map<string, m5::TrailerCapability> types) {
    m5::M5PolicyProfile profile = m5Fixtures::syntheticProfile();
    profile.trailerTypes = std::move(types);
    return make_shared<m5::M5LargeLanePlanner>(profile, "", m5::DateRules{false, 10}, 20);
}

// One lane-date with `goods` items on 21 reservations; the request as the selector hands it over.
struct Handover {
    Day day;
    LargeLaneRequest request;
};

Handover handoverOf(size_t goods) {
    Handover handover{dayOf({group("2027")}, goods, itemsOf(0, goods, 0), {reserved("2027", 21)}), {}};
    handover.request = largeLaneRequestOf(handover.day.run, handover.day.classification, {0});
    return handover;
}

void requireRefusedWhole(const LargeLaneResult& result, size_t goods, const string& reasonText) {
    CHECK_FALSE(result.report.planned);
    CHECK(result.trucks.empty());
    CHECK(result.reservationsUsed == 0);
    REQUIRE(result.leftOver.size() == goods);
    for (const LeftOverItem& left : result.leftOver) CHECK(left.reason == LeftOverReason::M5InvalidInput);
    INFO(result.report.text);
    CHECK(result.report.text.find(reasonText) != string::npos);
}

}  // namespace

TEST_CASE("U-DA-07/mmConversion: the container converts with 25.4 exactly and keeps the smaller value of each range" * doctest::test_suite("m5")) {
    const m5::M5PolicyProfile profile = m5::loadProfile(m5BlockWith(nlohmann::json::array({containerType()})));
    REQUIRE(profile.trailerTypes.count("40HC") == 1);
    const m5::TrailerCapability& container = profile.trailerTypes.at("40HC");
    CHECK(container.lengthIn == 12032.0 / 25.4);
    CHECK(container.widthIn == 2350.0 / 25.4);
    CHECK(container.heightIn == 2698.0 / 25.4);
    CHECK(fabs(container.lengthIn - 473.700787) < 5e-7);
    CHECK(fabs(container.widthIn - 92.519685) < 5e-7);
    CHECK(fabs(container.heightIn - 106.220472) < 5e-7);
    CHECK(container.maxWeightLb == 50000.0);
    CHECK(container.maxUnitLoads == 40);
    CHECK(fabs(container.lengthIn * container.widthIn / 144.0 - 304.35) < 0.005);
    CHECK(fabs(container.lengthIn * container.widthIn * container.heightIn / 1728.0 - 2694.03) < 0.005);
    // Final checks see 473.70 x 92.51 x 106.22 in whole hundredths, rounded down, never up.
    CHECK(*m5::hundredthsRoundedDown(container.lengthIn) == 47370);
    CHECK(*m5::hundredthsRoundedDown(container.widthIn) == 9251);
    CHECK(*m5::hundredthsRoundedDown(container.heightIn) == 10622);
    const m5::M5PolicyProfile shipped = m5::loadProfile(readParamsFile("config/orderBuilderParams.json")["m5"]);
    CHECK(shipped.trailerTypes.size() == 2);
    CHECK(shipped.trailerTypes.count("40STD") == 1);
    m5Evidence::note("expected", {"473.700787 x 92.519685 x 106.220472 in", "304.35 sq ft", "2694.03 cu ft"});
}

TEST_CASE("U-DA-07/rowPatterns: 48 x 40 stacks in the 92.52 in container: 80 and 88 in rows, never 96" * doctest::test_suite("m5")) {
    const m5::TrailerCapability container = m5::loadProfile(m5BlockWith(nlohmann::json::array({containerType()}))).trailerTypes.at("40HC");
    // Independent widths: two short sides, a long and a short side, two long sides.
    CHECK(40.0 + 40.0 <= container.widthIn);
    CHECK(48.0 + 40.0 <= container.widthIn);
    CHECK_FALSE(48.0 + 48.0 <= container.widthIn);
    vector<FloorItem> stacks(18, FloorItem{48.0, 40.0, 50.0, 1, 48.0 * 40.0 * 50.0});
    const FloorLayout layout = arrangeFloor(stacks, m5::trailerOf(container), m5Fixtures::physicalSettings().floor);
    REQUIRE_FALSE(layout.rows.empty());
    size_t twoAcross = 0;
    for (const FloorRow& row : layout.rows) {
        CHECK(row.widthIn <= container.widthIn);
        CHECK(row.pattern != FloorPatternKind::WideWide);
        if (row.placements.size() == 2) ++twoAcross;
    }
    CHECK(twoAcross > 0);
    CHECK(layout.positions == 18);
    m5Evidence::note("expected", {"80 fits", "88 fits", "96 never"});
}

TEST_CASE("U-DA-07/containerRun: the high-cube container plans generic stacks with its own floor and no unit cap" * doctest::test_suite("m5")) {
    const m5::M5PolicyProfile shipped = m5::loadProfile(readParamsFile("config/orderBuilderParams.json")["m5"]);
    REQUIRE(shipped.trailerTypes.count("40STD") == 1);
    const m5::TrailerCapability& container = shipped.trailerTypes.at("40STD");
    CHECK(container.lengthIn == 12032.0 / 25.4);
    CHECK(container.widthIn == 2350.0 / 25.4);
    CHECK(container.heightIn == 106.0);
    CHECK(container.maxWeightLb == 45000.0);
    CHECK(container.maxUnitLoads == m5::kUnlimitedUnitLoads);
    Handover handover = handoverOf(30);
    handover.request.slotEquipment.assign(21, "40STD");
    SelectionItem& otherFootprint = handover.day.items.items.front();
    otherFootprint.footprintLongIn = 42.0;
    otherFootprint.footprintShortIn = 42.0;
    otherFootprint.heightIn = 100.0;
    otherFootprint.cubeCubicIn = 42.0 * 42.0 * otherFootprint.heightIn;
    const m5::M5LaneDateInput input = m5::gatherLaneDate(handover.request, handover.day.run, handover.day.items,
                                                         {false, 10}, nullopt, 64.0, shipped.trailerTypes);
    REQUIRE(input.pool.slots.size() == 21);
    CHECK(input.pool.slots.front().maxUnitLoads == m5::kUnlimitedUnitLoads);
    const LargeLaneResult result = typedPlanner(shipped.trailerTypes)->plan(handover.request, handover.day.run,
                                                                             handover.day.items, selectorSettings());
    REQUIRE(result.report.planned);
    REQUIRE_FALSE(result.trucks.empty());
    size_t shippedItems = 0;
    for (const PlannedTruck& truck : result.trucks) {
        shippedItems += truck.items.size();
        CHECK(truck.weightLb <= 45000.0);
        CHECK(truck.floor.fitsLength);
        CHECK(truck.floor.usedLengthIn <= container.lengthIn);
        CHECK(fabs(truck.floor.rearWasteIn - (container.lengthIn - truck.floor.usedLengthIn)) < 1e-6);
        for (const FloorRow& row : truck.floor.rows) CHECK(row.widthIn <= container.widthIn);
    }
    CHECK(shippedItems == 30);
    m5Evidence::note("expected", {"container 473.700787 x 92.519685 x 106 in", "45,000 lb", "no unit-load cap",
                                  "a 100 in tall stack on a 42 x 42 footprint, plus 48 x 40 footprints", "container floor and rear inches"});
}

TEST_CASE("U-DA-07/missingLimitsRejected: a type without payload or unit-load cap is a named configuration error" * doctest::test_suite("m5")) {
    const auto refusedNaming = [](const nlohmann::json& type, const string& key) {
        INFO(type.dump());
        bool refused = false;
        try {
            m5::loadProfile(m5BlockWith(nlohmann::json::array({type})));
        } catch (const runtime_error& error) {
            refused = true;
            CHECK(string(error.what()).find(key) != string::npos);
        }
        CHECK(refused);
    };
    nlohmann::json noPayload = containerType(), noCap = containerType(), zeroCap = containerType(), both = containerType(),
                   noWidth = containerType();
    noPayload.erase("payloadLb");
    noCap.erase("maxUnitLoads");
    zeroCap["maxUnitLoads"] = 0;
    both["lengthIn"] = 473.7;
    noWidth.erase("widthMm");
    refusedNaming(noPayload, "payloadLb");
    refusedNaming(noCap, "maxUnitLoads");
    refusedNaming(zeroCap, "maxUnitLoads");
    refusedNaming(both, "length");
    refusedNaming(noWidth, "width");
    refusedNaming(containerType(-1.0), "payloadLb");
    refusedNaming(nlohmann::json::object({{"equipment", "40HC"}}), "length");
    refusedNaming(nlohmann::json("40HC"), "trailerTypes[0]");
    CHECK_THROWS_AS(m5::loadProfile(m5BlockWith(nlohmann::json::array({containerType(), containerType()}))), runtime_error);
    CHECK_THROWS_AS(m5::loadProfile(m5BlockWith(nlohmann::json::object())), runtime_error);
    m5Evidence::note("expected", {"payloadLb named", "maxUnitLoads named", "never 45,000 lb or 64"});
}

TEST_CASE("U-DA-07/unknownTypeNoFallback: a reservation of equipment outside the catalogue is refused, never sized as 53F" * doctest::test_suite("m5")) {
    const size_t goods = 40;
    Day day = dayOf({group("2027")}, goods, itemsOf(0, goods, 0), {reserved("2027", 21)});
    PlaceholderRecord unknown = reserved("2027", 21);
    unknown.zzna_equip_size = "UNKNOWN_TYPE";
    day.book = buildReservationBook({unknown}, kCtl, kPlanDate, {"53F", {"53F", "UNKNOWN_TYPE"}});
    day.classification = classifyLanes(day.run, day.book, day.items, kRouting);
    REQUIRE(day.classification.routes[0].method == SolveMethod::M5);
    REQUIRE(day.classification.routes[0].reservedEquipment == vector<string>(21, "UNKNOWN_TYPE"));
    SelectorSettings settings = selectorSettings();
    settings.largeLanePlanner = typedPlanner({{"53F", kTyped53F}});
    const SelectionPlan selection = selectLoads(day.run, day.book, day.items, day.classification, settings);
    CHECK(selection.trucks.empty());
    REQUIRE(selection.largeLanes.size() == 1);
    CHECK_FALSE(selection.largeLanes[0].planned);
    CHECK(selection.largeLanes[0].text.find("unknown equipment 'UNKNOWN_TYPE'") != string::npos);
    CHECK(selection.leftOver.size() == goods);
    for (const LeftOverItem& left : selection.leftOver) CHECK(left.reason == LeftOverReason::M5InvalidInput);
    m5Evidence::note("expected", {"refused: unknown equipment", "0 trucks", "40 left M5InvalidInput"});
}

TEST_CASE("U-DA-05/mixedRunRejected: one lane-date with an A slot and a B slot of different sizes is refused as mixed equipment" * doctest::test_suite("m5")) {
    Handover handover = handoverOf(40);
    handover.request.reservedTrucks = 2;
    handover.request.slotEquipment = {"A", "B"};
    const m5::TrailerCapability typeA{630.0, 100.0, 108.0, 45000.0, 64}, typeB{600.0, 96.0, 108.0, 40000.0, 60};
    const LargeLaneResult result = typedPlanner({{"A", typeA}, {"B", typeB}})->plan(handover.request, handover.day.run, handover.day.items,
                                                                                    selectorSettings());
    requireRefusedWhole(result, 40, "mixed equipment on one lane-date (A and B)");
    // A direct adapter test with explicit, equal slots still plans.
    const m5::M5LaneDateInput direct = m5Fixtures::laneWith(m5Fixtures::fx30Units("u", 30), 2);
    CHECK(m5Fixtures::shippedCount(m5Fixtures::plan(direct, m5Fixtures::syntheticProfile())) == 30);
    m5Evidence::note("expected", {"refused: mixed equipment", "no trucks", "no commit"});
}

TEST_CASE("X-ROUTE-05/malformedHandoffReservation: a negative count or an invalid equipment identity is refused, never zero trucks" * doctest::test_suite("m5")) {
    Handover negative = handoverOf(40);
    negative.request.reservedTrucks = -1;
    requireRefusedWhole(m5Planner()->plan(negative.request, negative.day.run, negative.day.items, selectorSettings()), 40,
                        "the hand-over reserves -1 trucks");

    Handover badIdentity = handoverOf(40);
    badIdentity.request.slotEquipment.assign(21, "53F");
    badIdentity.request.slotEquipment[3] = "NOT-A-TYPE";
    requireRefusedWhole(typedPlanner({{"53F", kTyped53F}})->plan(badIdentity.request, badIdentity.day.run, badIdentity.day.items,
                                                               selectorSettings()),
                        40, "unknown equipment 'NOT-A-TYPE'");

    // The valid control on the same planner plans; exact-duplicate reservation rows stay refused the same
    // way however often the book is built.
    const Handover control = handoverOf(40);
    REQUIRE(control.request.slotEquipment == vector<string>(21, "53F"));
    const LargeLaneResult planned = typedPlanner({{"53F", kTyped53F}})->plan(control.request, control.day.run, control.day.items,
                                                                             selectorSettings());
    CHECK(planned.report.planned);
    CHECK(planned.trucks.size() == 2);
    for (int build = 0; build < 2; ++build) {
        const ReservationBook book = buildReservationBook({reserved("2027", 11), reserved("2027", 11)}, kCtl, kPlanDate, {"53F", {"53F"}});
        CHECK(book.reservations.empty());
        CHECK(book.skipped.size() == 2);
    }
    m5Evidence::note("expected", {"-1 refused by name", "unknown identity refused", "control 2 trucks"});
}
