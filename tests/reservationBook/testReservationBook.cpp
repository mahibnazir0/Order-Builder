#include "doctest.h"
#include "../importer/crossDayFixtures.hpp"
#include "importer.hpp"
#include "placeholder_importer.hpp"
#include "reservationBook.hpp"
#include "validator.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace std;
using namespace ob;

namespace {

const ReservationBookSettings kSettings{"53F", {"53F"}};

// The August CTL rows as shipped: weekday rows plus stale dated rows for 30 Jun - 2 Jul.
vector<CTLRecord> augustCtl() {
    return {
        {"", "FRIDAY", 3, 5, 4},       {"", "MONDAY", 2, 3, 2},
        {"", "TUESDAY", 2, 3, 2},      {"2026-06-30", "", 2, 4, 2},
        {"2026-07-01", "", 4, 5, 4},   {"2026-07-02", "", 5, 6, 5},
        {"", "WEDNESDAY", 2, 3, 3},    {"", "THURSDAY", 2, 4, 4},
    };
}

// 2026-08-04 is a Tuesday: the window is 6-7 Aug.
const string kTuesday = "2026-08-04";

PlaceholderRecord placeholder(const string& origin, const string& shipDate, int trucks,
                              const string& equipment = "53F") {
    PlaceholderRecord record;
    record.locfrno = origin;
    record.loctono = "2528";
    record.ship_cond = "TL";
    record.datfr_ta = shipDate;
    record.datto_ta = "2026-08-12";
    record.zzna_equip_size = equipment;
    record.no_of_loads = trucks;
    return record;
}

ReservationBook bookFor(const vector<PlaceholderRecord>& placeholders) {
    return buildReservationBook(placeholders, augustCtl(), kTuesday, kSettings);
}

PlaceholderSkipReason onlySkipReason(const PlaceholderRecord& record) {
    const ReservationBook book = bookFor({record});
    REQUIRE(book.reservations.empty());
    REQUIRE(book.skipped.size() == 1);
    return book.skipped.front().reason;
}

STRRecord demandLine(const string& origin, const string& destination, const string& shipCondition) {
    STRRecord line;
    line.locfrno = origin;
    line.loctono = destination;
    line.ship_cond = shipCondition;
    return line;
}

using ReservationKey = tuple<string, string, string, string>;

map<ReservationKey, long long> trucksByKey(const ReservationBook& book) {
    map<ReservationKey, long long> trucks;
    for (const Reservation& reservation : book.reservations) {
        trucks[{reservation.origin, reservation.destination, reservation.shipCondition,
                reservation.shipDate}] = reservation.trucks;
    }
    return trucks;
}

// One lane spread over several ship dates, with repeated rows per date and dates on both
// sides of the window.
vector<PlaceholderRecord> genSplitReservation(mt19937& random) {
    const array<string, 3> origins{"2027", "2028", "2031"};
    const array<string, 5> shipDates{"2026-08-05", "2026-08-06", "2026-08-07", "2026-08-08",
                                     "2026-08-09"};
    uniform_int_distribution<int> rowCount(0, 40);
    uniform_int_distribution<size_t> originPick(0, origins.size() - 1);
    uniform_int_distribution<size_t> datePick(0, shipDates.size() - 1);
    uniform_int_distribution<int> trucks(0, 13);
    vector<PlaceholderRecord> rows(static_cast<size_t>(rowCount(random)));
    for (PlaceholderRecord& row : rows) {
        row = placeholder(origins[originPick(random)], shipDates[datePick(random)], trucks(random),
                          random() % 4 == 0 ? "" : "53F");
        if (random() % 2 == 0) row.ship_cond = "TF";
    }
    return rows;
}

} // anonymous namespace

TEST_CASE("reservationBook: keyedByLaneAndShipDate") {
    const ReservationBook book = bookFor({placeholder("2027", "2026-08-06", 3)});
    REQUIRE(book.reservations.size() == 1);
    const Reservation& reservation = book.reservations.front();
    CHECK(reservation.origin == "2027");
    CHECK(reservation.destination == "2528");
    CHECK(reservation.shipCondition == "TL");
    CHECK(reservation.shipDate == "2026-08-06");
    CHECK(reservation.trucks == 3);
    CHECK(findReservation(book, "2027", "2528", "TL", "2026-08-06") == &reservation);
    CHECK(findReservation(book, "2027", "2528", "TL", "2026-08-07") == nullptr);
    CHECK(findReservation(book, "2027", "2528", "TF", "2026-08-06") == nullptr);
}

TEST_CASE("reservationBook: one lane on two ship dates stays two reservations") {
    const ReservationBook book = bookFor({placeholder("2027", "2026-08-06", 3),
                                          placeholder("2027", "2026-08-07", 2)});
    REQUIRE(book.reservations.size() == 2);
    CHECK(book.reservations[0].trucks == 3);
    CHECK(book.reservations[1].trucks == 2);
    CHECK(book.reservedTrucks == 5);
}

TEST_CASE("reservationBook: rows for the same lane and date are summed in file order") {
    const ReservationBook book = bookFor({placeholder("2027", "2026-08-06", 3),
                                          placeholder("2031", "2026-08-06", 1),
                                          placeholder("2027", "2026-08-06", 2, "")});
    const Reservation* summed = findReservation(book, "2027", "2528", "TL", "2026-08-06");
    REQUIRE(summed != nullptr);
    CHECK(summed->trucks == 5);
    REQUIRE(summed->entries.size() == 2);
    CHECK(summed->entries[0].placeholderIndex == 0);
    CHECK(summed->entries[1].placeholderIndex == 2);
    CHECK(summed->entries[1].equipmentDefaulted);
}

TEST_CASE("reservationBook: splitReservationNeverSummedAcrossDates") {
    mt19937 random(20261016);
    for (int trial = 0; trial < 2000; ++trial) {
        const vector<PlaceholderRecord> rows = genSplitReservation(random);
        const ReservationBook book = bookFor(rows);

        map<ReservationKey, long long> expected;
        long long totalTrucks = 0;
        for (const PlaceholderRecord& row : rows) {
            totalTrucks += row.no_of_loads;
            if (row.no_of_loads > 0 && row.datfr_ta >= "2026-08-06" && row.datfr_ta <= "2026-08-07") {
                expected[{row.locfrno, row.loctono, row.ship_cond, row.datfr_ta}] += row.no_of_loads;
            }
        }
        REQUIRE(trucksByKey(book) == expected);
        REQUIRE(book.reservedTrucks + book.skippedTrucks == totalTrucks);
        REQUIRE(book.reservations.size() + book.skipped.size()
                <= rows.size());

        vector<PlaceholderRecord> shuffled = rows;
        shuffle(shuffled.begin(), shuffled.end(), random);
        REQUIRE(trucksByKey(bookFor(shuffled)) == expected);
    }
}

TEST_CASE("reservationBook: ship dates outside the window are reported, not reserved") {
    const ReservationBook book = bookFor({placeholder("2027", "2026-08-05", 1),
                                          placeholder("2027", "2026-08-06", 2),
                                          placeholder("2027", "2026-08-07", 3),
                                          placeholder("2027", "2026-08-08", 4)});
    CHECK(book.reservedTrucks == 5);
    CHECK(book.skippedTrucks == 5);
    REQUIRE(book.skipped.size() == 2);
    CHECK(book.skipped[0].placeholderIndex == 0);
    CHECK(book.skipped[0].reason == PlaceholderSkipReason::BeforeWindow);
    CHECK(book.skipped[1].placeholderIndex == 3);
    CHECK(book.skipped[1].reason == PlaceholderSkipReason::AfterWindow);
}

TEST_CASE("reservationBook: zero loads is no reservation, and is reported") {
    CHECK(onlySkipReason(placeholder("2027", "2026-08-06", 0)) == PlaceholderSkipReason::ZeroLoads);
}

TEST_CASE("reservationBook: a negative, unreadable or oversized load count is rejected") {
    for (const int trucks : {-3, kUnreadableLoadCount, kMaxLoadsPerPlaceholder + 1}) {
        const ReservationBook book = bookFor({placeholder("2027", "2026-08-06", trucks)});
        REQUIRE(book.skipped.size() == 1);
        CHECK(book.skipped.front().reason == PlaceholderSkipReason::InvalidLoadCount);
        CHECK(book.skippedTrucks == 0);
        CHECK(book.reservedTrucks == 0);
    }
}

TEST_CASE("reservationBook: a non-integer NO_OF_LOADS reads as unreadable and is rejected") {
    const string path = "reservationBookFractionalLoads.json";
    ofstream(path) << R"({"PHOLDER":[{"LOCFRNO":"2027","LOCTONO":"2528","SHIP_COND":"TL",)"
                      R"("DATFR_TA":"2026-08-06","DATTO_TA":"2026-08-08","NO_OF_LOADS":2.5}]})";
    const PlaceholderLoadResult loaded = PlaceholderImporter::load(path);
    remove(path.c_str());
    CHECK(onlySkipReason(loaded.placeholders.front()) == PlaceholderSkipReason::InvalidLoadCount);
}

TEST_CASE("reservationBook: a row with a blank lane field is rejected") {
    PlaceholderRecord blankShipCondition = placeholder("2073", "2026-08-06", 4);
    blankShipCondition.ship_cond = "";
    CHECK(onlySkipReason(blankShipCondition) == PlaceholderSkipReason::MissingLane);
    PlaceholderRecord blankOrigin = placeholder(" ", "2026-08-06", 4);
    CHECK(onlySkipReason(blankOrigin) == PlaceholderSkipReason::MissingLane);
    PlaceholderRecord blankDestination = placeholder("2073", "2026-08-06", 4);
    blankDestination.loctono = "";
    CHECK(onlySkipReason(blankDestination) == PlaceholderSkipReason::MissingLane);
}

TEST_CASE("reservationBook: invalid ship and delivery dates are rejected") {
    CHECK(onlySkipReason(placeholder("2027", "", 1)) == PlaceholderSkipReason::InvalidShipDate);
    CHECK(onlySkipReason(placeholder("2027", "2026-08-32", 1)) == PlaceholderSkipReason::InvalidShipDate);
    PlaceholderRecord deliveredBeforeShipped = placeholder("2027", "2026-08-06", 1);
    deliveredBeforeShipped.datto_ta = "2026-08-05";
    CHECK(onlySkipReason(deliveredBeforeShipped) == PlaceholderSkipReason::InvalidDeliveryDate);
    PlaceholderRecord noDeliveryDate = placeholder("2027", "2026-08-06", 1);
    noDeliveryDate.datto_ta = "";
    CHECK(onlySkipReason(noDeliveryDate) == PlaceholderSkipReason::InvalidDeliveryDate);
}

TEST_CASE("reservationBook: blank equipment takes the customer default and is counted") {
    const ReservationBook book = bookFor({placeholder("2027", "2026-08-06", 2, "")});
    REQUIRE(book.reservations.size() == 1);
    const ReservationEntry& entry = book.reservations.front().entries.front();
    CHECK(entry.equipment == "53F");
    CHECK(entry.equipmentDefaulted);
    CHECK(book.entriesWithDefaultEquipment == 1);
}

TEST_CASE("reservationBook: equipment that is not configured is rejected") {
    CHECK(onlySkipReason(placeholder("2027", "2026-08-06", 2, "48F"))
          == PlaceholderSkipReason::UnknownEquipment);
}

TEST_CASE("reservationBook: an unusable default equipment fails at build") {
    const vector<PlaceholderRecord> rows{placeholder("2027", "2026-08-06", 2)};
    CHECK_THROWS_AS(buildReservationBook(rows, augustCtl(), kTuesday, {"", {"53F"}}),
                    invalid_argument);
    CHECK_THROWS_AS(buildReservationBook(rows, augustCtl(), kTuesday, {"48F", {"53F"}}),
                    invalid_argument);
}

TEST_CASE("reservationBook: noReservationNoTruck") {
    const ReservationBook book = bookFor({placeholder("2027", "2026-08-06", 2)});
    const ReservationCoverage coverage = matchDemand(
        book, {demandLine("2027", "2528", "TL"), demandLine("2019", "2299", "TF"),
               demandLine("2019", "2299", "TF"), demandLine("2027", "2528", "TF")});
    REQUIRE(coverage.demandLanesWithoutReservation.size() == 2);
    CHECK(coverage.demandLanesWithoutReservation[0].origin == "2019");
    CHECK(coverage.demandLanesWithoutReservation[1].shipCondition == "TF");
    CHECK(coverage.reservationsWithoutDemand.empty());
    CHECK(coverage.penaltyExposureTrucks == 0);
}

TEST_CASE("reservationBook: unusedReservationIsPenaltyExposure") {
    const ReservationBook book = bookFor({placeholder("2027", "2026-08-06", 2),
                                          placeholder("2031", "2026-08-06", 3),
                                          placeholder("2031", "2026-08-07", 1)});
    const ReservationCoverage coverage = matchDemand(book, {demandLine("2027", "2528", "TL")});
    CHECK(coverage.reservationsWithoutDemand == vector<size_t>{1, 2});
    CHECK(coverage.penaltyExposureTrucks == 4);
}

TEST_CASE("reservationBook: plan date comes from the REQUEST_ID") {
    CHECK(planDateFromRequestId("#STR_PA4400_20260804154458#") == "2026-08-04");
    CHECK(planDateFromRequestId("#STR_PA4400_20261005110639#") == "2026-10-05");
    CHECK_THROWS_AS(planDateFromRequestId(""), invalid_argument);
    CHECK_THROWS_AS(planDateFromRequestId("t"), invalid_argument);
    CHECK_THROWS_AS(planDateFromRequestId("#STR_PA4400_20261305110639#"), invalid_argument);
}

TEST_CASE("reservationBook: a missing or empty placeholder file fails fast") {
    CHECK_THROWS_AS(PlaceholderImporter::load("noSuchPlaceholderFile.json"), runtime_error);
    const string path = "reservationBookEmptyPlaceholder.json";
    ofstream(path) << "";
    CHECK_THROWS_AS(PlaceholderImporter::load(path), runtime_error);
    remove(path.c_str());
}

namespace {

struct AugustInputs {
    string label;
    string demandPath;
    string placeholderPath;
};

const array<AugustInputs, 3> kAugustInputs{{
    {"Aug4", "tests/importer/august/inputs/Aug4/Demand-Aug5-2.json",
     "tests/importer/august/inputs/Aug4/PH-Aug4.json"},
    {"Aug5", "tests/importer/august/inputs/Aug5/Demand-Aug5.json",
     "tests/importer/august/inputs/Aug5/PH-Aug5.json"},
    {"Aug6", "tests/importer/august/inputs/Aug6/Demand-Aug6.json",
     "tests/importer/august/inputs/Aug6/PH-Aug6.json"},
}};

bool augustInputsPresent() {
    static const bool present = crossDayTests::filesPresent(
        {kAugustInputs[0].demandPath, kAugustInputs[0].placeholderPath,
         kAugustInputs[1].demandPath, kAugustInputs[1].placeholderPath,
         kAugustInputs[2].demandPath, kAugustInputs[2].placeholderPath},
        "4-6 Aug reservation book tests are");
    return present;
}

struct AugustBook {
    DemandFile demand;
    PlaceholderLoadResult placeholders;
    ReservationBook book;
};

AugustBook augustBook(size_t runIndex) {
    AugustBook run;
    run.demand = Importer::load_demand(kAugustInputs[runIndex].demandPath);
    run.placeholders = PlaceholderImporter::load(kAugustInputs[runIndex].placeholderPath);
    run.book = buildReservationBook(run.placeholders.placeholders, run.demand.ctl,
                                    planDateFromRequestId(run.demand.request_id), kSettings);
    return run;
}

} // anonymous namespace

TEST_CASE("reservationBook: augustReservedTotals1503" * doctest::skip(!augustInputsPresent())) {
    const array<string, 3> planDates{"2026-08-04", "2026-08-05", "2026-08-06"};
    const array<long long, 3> reserved{324, 532, 584};
    const array<long long, 3> skipped{3, 55, 5};
    long long total = 0;
    for (size_t run = 0; run < kAugustInputs.size(); ++run) {
        CAPTURE(kAugustInputs[run].label);
        const AugustBook august = augustBook(run);
        CHECK(august.book.window.planDate == planDates[run]);
        CHECK(august.book.reservedTrucks == reserved[run]);
        CHECK(august.book.skippedTrucks == skipped[run]);
        CHECK(august.book.reservedTrucks + august.book.skippedTrucks
              == august.placeholders.total_loads);
        total += august.book.reservedTrucks + august.book.skippedTrucks;
    }
    CHECK(total == 1503);
}

TEST_CASE("reservationBook: every August row is reserved or skipped with a reason"
          * doctest::skip(!augustInputsPresent())) {
    for (size_t run = 0; run < kAugustInputs.size(); ++run) {
        CAPTURE(kAugustInputs[run].label);
        const AugustBook august = augustBook(run);
        vector<int> seen(august.placeholders.placeholders.size(), 0);
        for (const Reservation& reservation : august.book.reservations) {
            for (const ReservationEntry& entry : reservation.entries) ++seen[entry.placeholderIndex];
            CHECK(reservation.shipDate >= august.book.window.firstShipDate);
            CHECK(reservation.shipDate <= august.book.window.lastShipDate);
        }
        for (const SkippedPlaceholder& skipped : august.book.skipped) ++seen[skipped.placeholderIndex];
        CHECK(all_of(seen.begin(), seen.end(), [](int count) { return count == 1; }));
    }
}

TEST_CASE("reservationBook: the Aug 6 lane 2073 -> 2299 with no ship condition reserves nothing"
          * doctest::skip(!augustInputsPresent())) {
    const AugustBook august = augustBook(2);
    const auto missingLane = count_if(
        august.book.skipped.begin(), august.book.skipped.end(), [](const SkippedPlaceholder& skipped) {
            return skipped.reason == PlaceholderSkipReason::MissingLane;
        });
    CHECK(missingLane == 1);
    CHECK(findReservation(august.book, "2073", "2299", "TF", "2026-08-10") == nullptr);
    const ReservationCoverage coverage = matchDemand(august.book, august.demand.str);
    const auto& unreserved = coverage.demandLanesWithoutReservation;
    CHECK(any_of(unreserved.begin(), unreserved.end(), [](const DemandLane& lane) {
        return lane.origin == "2073" && lane.destination == "2299" && lane.shipCondition == "TF";
    }));
}

TEST_CASE("reservationBook: same files read twice give an identical book"
          * doctest::skip(!augustInputsPresent())) {
    const AugustBook first = augustBook(1);
    const AugustBook second = augustBook(1);
    CHECK(trucksByKey(first.book) == trucksByKey(second.book));
    REQUIRE(first.book.skipped.size() == second.book.skipped.size());
    for (size_t index = 0; index < first.book.skipped.size(); ++index) {
        CHECK(first.book.skipped[index].placeholderIndex == second.book.skipped[index].placeholderIndex);
        CHECK(first.book.skipped[index].reason == second.book.skipped[index].reason);
    }
}
