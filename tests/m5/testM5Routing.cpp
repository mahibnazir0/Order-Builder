// Routing into M5 and the selector seam on days built through the real reservation book and
// classifier (v7 4.1, 4.2, 11), threads, and the M4 conversions M5 relies on (4.9, 18).
#include "doctest.h"
#include "../m4SmallDay.hpp"
#include "../m4SyntheticDay.hpp"
#include "converter.hpp"
#include "joiner.hpp"
#include "m5Integration.hpp"
#include "pipeline.hpp"
#include "support/m5Fixtures.hpp"

#include <algorithm>
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

TEST_CASE("U-RT-03 X-ROUTE-01 X-ROUTE-10 only reserved > T goes to M5, T from the settings" * doctest::test_suite("m5")) {
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

TEST_CASE("X-ROUTE-03 U-SP-01 a lane over the threshold on one date and under on the other" * doctest::test_suite("m5")) {
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
}

TEST_CASE("X-ROUTE-04 two lanes of 11 are never joined into a lane of 22" * doctest::test_suite("m5")) {
    const Day day = dayOf({group("2027"), group("2028")}, 10, join(itemsOf(0, 5, 0), itemsOf(1, 5, 5)), {reserved("2027", 11), reserved("2028", 11)});
    for (const LaneRoute& route : day.classification.routes) CHECK(route.method != SolveMethod::M5);
    CHECK(plan(day, withPlanner()).largeLanes.empty());
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

TEST_CASE("X-ROUTE-07 routing follows reserved trucks, never the size of the demand" * doctest::test_suite("m5")) {
    for (const size_t goods : {size_t{1}, size_t{500}}) {
        const Day day = dayOf({group("2027")}, goods, itemsOf(0, goods, 0), {reserved("2027", 21)});
        CHECK(day.classification.routes.front().method == SolveMethod::M5);
    }
}

TEST_CASE("U-RT-07 X-ROUTE-08 RD-08 a raw-material lane over the threshold is never planned" * doctest::test_suite("m5")) {
    const Day day = dayOf({groupOn("2027", "2528", "TL_N102")}, 30, itemsOf(0, 30, 0), {reservedOn("2027", "2528", "TL_N102", 21)});
    REQUIRE(day.classification.routes.front().method == SolveMethod::M5);
    const SelectionPlan selection = plan(day, withPlanner());
    CHECK(selection.trucks.empty());
    REQUIRE(selection.largeLanes.size() == 1);
    CHECK_FALSE(selection.largeLanes[0].planned);
    CHECK(selection.largeLanes[0].outcome.find("out of scope") != string::npos);
    for (const LeftOverItem& left : selection.leftOver) CHECK(left.reason == LeftOverReason::M5InvalidInput);
}

TEST_CASE("U-SP-03 U-SP-05 a lane ID with underscores is one lane; goods that may mix stay one piece" * doctest::test_suite("m5")) {
    const Day day = dayOf({groupOn("1757_1724_1725", "2528", "TL")}, 40, itemsOf(0, 40, 0), {reservedOn("1757_1724_1725", "2528", "TL", 21)});
    const SelectionPlan selection = plan(day, withPlanner());
    REQUIRE(selection.largeLanes.size() == 1);
    CHECK(selection.largeLanes[0].origin == "1757_1724_1725");
    CHECK(selection.trucks.size() == 2);
    CHECK(selection.largeLanes[0].text.find("groups sharing the reservation: group 0\n") != string::npos);
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

TEST_CASE("X-STATE-03 an M4 lane plans the same with an unrelated M5 lane beside it" * doctest::test_suite("m5")) {
    const Day day = dayOf({group("2027"), group("2028")}, 60, join(itemsOf(0, 30, 0), itemsOf(1, 30, 30)),
                          {reserved("2027", 20), reserved("2028", 21)});
    const auto withM5 = trucksByGroup(plan(day, withPlanner()));
    const auto withoutM5 = trucksByGroup(plan(day, selectorSettings()));
    CHECK(withM5.at(0) == withoutM5.at(0));
    CHECK(withM5.count(1) == 1);
    CHECK(withoutM5.count(1) == 0);
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

TEST_CASE("U-DA-06 X-RUN-08 a combo lane stops the run before any plan is published" * doctest::test_suite("m5")) {
    const m4SmallDay::SmallDay day("comboLane", "," + m4SmallDay::SmallDay::line("GOOD", 16, "TF"));
    CHECK_THROWS_AS(Pipeline::run(day.inputs), runtime_error);
}

TEST_CASE("U-DA-01 X-DATA-08 CS, DIS and PAL all divide by Cases_Unit_Load" * doctest::test_suite("m5")) {
    CHECK(Converter::to_pallets(240, "CS", product("a", 10)) == 24.0);
    CHECK(Converter::to_pallets(432, "DIS", product("b", 18)) == 24.0);
    CHECK(Converter::to_pallets(60, "PAL", product("c", 2)) == 30.0);
}

TEST_CASE("U-DA-02 X-DATA-03 X-DATA-04 eaches, zero divisors and part pallets follow M4's rule" * doctest::test_suite("m5")) {
    CHECK_FALSE(Converter::isConvertibleUom("EA"));
    CHECK(Converter::to_pallets(500, "EA", product("a", 10)) == 0.0);
    CHECK(Converter::to_pallets(9, "CS", product("b", 0)) == 0.0);
    CHECK(Converter::to_pallets(9, "CS", product("c", 2)) == 4.5);
}

TEST_CASE("U-DA-03 a product missing from the master is reported by its ID" * doctest::test_suite("m5")) {
    STRRecord line;
    line.matnr = "MISSING";
    line.trans = 10;
    line.unitofmeas = "CS";
    const ProductIndex index = Joiner::build_index({product("PRESENT", 10)});
    const JoinResult joined = Joiner::join({line}, index);
    CHECK(joined.unmatched_lines == 1);
    CHECK(find(joined.unmatched_matnrs.begin(), joined.unmatched_matnrs.end(), "MISSING") != joined.unmatched_matnrs.end());
}

TEST_CASE("U-DA-04 X-DATA-05 the pallet type is chosen by whole pallets, not by row order" * doctest::test_suite("m5")) {
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
}
