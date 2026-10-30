#include "reservationBook.hpp"
#include "isoDate.hpp"
#include "validator.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

using namespace std;

namespace ob {

namespace {

// '\x1f' cannot occur in a location code or ship condition, so distinct lanes never share a key.
string laneKey(const string& origin, const string& destination, const string& shipCondition) {
    return origin + '\x1f' + destination + '\x1f' + shipCondition;
}

bool isBlank(const string& text) {
    return all_of(text.begin(), text.end(), [](char c) { return isspace(static_cast<unsigned char>(c)); });
}

auto reservationOrder(const Reservation& reservation) {
    return tie(reservation.origin, reservation.destination, reservation.shipCondition,
               reservation.shipDate);
}

} // anonymous namespace

string planDateFromRequestId(const string& requestId) {
    // The timestamp is the last 14 digits before the closing '#': YYYYMMDDhhmmss.
    const size_t end = requestId.find_last_of("0123456789");
    if (end != string::npos && end >= 13) {
        const string digits = requestId.substr(end - 13, 14);
        if (all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            const string date = digits.substr(0, 4) + "-" + digits.substr(4, 2) + "-"
                                + digits.substr(6, 2);
            if (isIsoDate(date)) return date;
        }
    }
    throw invalid_argument("REQUEST_ID '" + requestId + "' carries no plan date");
}

ReservationBook buildReservationBook(const vector<PlaceholderRecord>& placeholders,
                                     const vector<CTLRecord>& ctl, const string& planDate,
                                     const ReservationBookSettings& settings) {
    const auto& known = settings.knownEquipment;
    if (isBlank(settings.defaultEquipment)
        || find(known.begin(), known.end(), settings.defaultEquipment) == known.end()) {
        throw invalid_argument("Default equipment '" + settings.defaultEquipment
                               + "' is blank or not one of the known equipment types");
    }

    ReservationBook book;
    book.window = planningWindow(ctl, planDate);

    vector<Reservation> rows;
    rows.reserve(placeholders.size());
    for (size_t index = 0; index < placeholders.size(); ++index) {
        const PlaceholderRecord& placeholder = placeholders[index];
        const int trucks = placeholder.no_of_loads;
        const bool loadCountReadable = trucks >= 0 && trucks <= kMaxLoadsPerPlaceholder;
        const bool equipmentBlank = isBlank(placeholder.zzna_equip_size);
        const string& equipment = equipmentBlank ? settings.defaultEquipment
                                                 : placeholder.zzna_equip_size;

        // Checked in this order so each row gets the most basic reason that applies.
        PlaceholderSkipReason reason = PlaceholderSkipReason::MissingLane;
        bool skip = true;
        if (isBlank(placeholder.locfrno) || isBlank(placeholder.loctono)
            || isBlank(placeholder.ship_cond)) {
            reason = PlaceholderSkipReason::MissingLane;
        } else if (!isIsoDate(placeholder.datfr_ta)) {
            reason = PlaceholderSkipReason::InvalidShipDate;
        } else if (!isIsoDate(placeholder.datto_ta) || placeholder.datto_ta < placeholder.datfr_ta) {
            reason = PlaceholderSkipReason::InvalidDeliveryDate;
        } else if (!loadCountReadable) {
            reason = PlaceholderSkipReason::InvalidLoadCount;
        } else if (trucks == 0) {
            reason = PlaceholderSkipReason::ZeroLoads;
        } else if (find(known.begin(), known.end(), equipment) == known.end()) {
            reason = PlaceholderSkipReason::UnknownEquipment;
        } else if (placeholder.datfr_ta < book.window.firstShipDate) {
            reason = PlaceholderSkipReason::BeforeWindow;
        } else if (placeholder.datfr_ta > book.window.lastShipDate) {
            reason = PlaceholderSkipReason::AfterWindow;
        } else {
            skip = false;
        }
        if (skip) {
            book.skipped.push_back({index, reason});
            if (loadCountReadable) book.skippedTrucks += trucks;
            continue;
        }

        Reservation row;
        row.origin = placeholder.locfrno;
        row.destination = placeholder.loctono;
        row.shipCondition = placeholder.ship_cond;
        row.shipDate = placeholder.datfr_ta;
        row.trucks = trucks;
        row.entries.push_back({index, trucks, equipment, equipmentBlank});
        rows.push_back(std::move(row));
        book.reservedTrucks += trucks;
        if (equipmentBlank) ++book.entriesWithDefaultEquipment;
    }

    // Stable, so rows merged into one reservation keep their file order.
    stable_sort(rows.begin(), rows.end(), [](const Reservation& left, const Reservation& right) {
        return reservationOrder(left) < reservationOrder(right);
    });
    for (Reservation& row : rows) {
        if (!book.reservations.empty()
            && reservationOrder(book.reservations.back()) == reservationOrder(row)) {
            Reservation& merged = book.reservations.back();
            merged.trucks += row.trucks;
            merged.entries.push_back(std::move(row.entries.front()));
        } else {
            book.reservations.push_back(std::move(row));
        }
    }
    return book;
}

const Reservation* findReservation(const ReservationBook& book, const string& origin,
                                   const string& destination, const string& shipCondition,
                                   const string& shipDate) {
    const auto wanted = tie(origin, destination, shipCondition, shipDate);
    const auto found = lower_bound(
        book.reservations.begin(), book.reservations.end(), wanted,
        [](const Reservation& reservation, const auto& key) { return reservationOrder(reservation) < key; });
    if (found == book.reservations.end() || reservationOrder(*found) != wanted) return nullptr;
    return &*found;
}

ReservationCoverage matchDemand(const ReservationBook& book, const vector<STRRecord>& demand) {
    unordered_set<string> demandLanes;
    demandLanes.reserve(demand.size());
    for (const STRRecord& line : demand) {
        demandLanes.insert(laneKey(line.locfrno, line.loctono, line.ship_cond));
    }

    ReservationCoverage coverage;
    unordered_set<string> reservedLanes;
    reservedLanes.reserve(book.reservations.size());
    for (size_t index = 0; index < book.reservations.size(); ++index) {
        const Reservation& reservation = book.reservations[index];
        const string key = laneKey(reservation.origin, reservation.destination,
                                   reservation.shipCondition);
        reservedLanes.insert(key);
        if (demandLanes.count(key) == 0) {
            coverage.reservationsWithoutDemand.push_back(index);
            coverage.penaltyExposureTrucks += reservation.trucks;
        }
    }

    unordered_set<string> reported;
    for (const STRRecord& line : demand) {
        const string key = laneKey(line.locfrno, line.loctono, line.ship_cond);
        if (reservedLanes.count(key) == 0 && reported.insert(key).second) {
            coverage.demandLanesWithoutReservation.push_back(
                {line.locfrno, line.loctono, line.ship_cond});
        }
    }
    sort(coverage.demandLanesWithoutReservation.begin(),
         coverage.demandLanesWithoutReservation.end(),
         [](const DemandLane& left, const DemandLane& right) {
             return tie(left.origin, left.destination, left.shipCondition)
                    < tie(right.origin, right.destination, right.shipCondition);
         });
    return coverage;
}

const char* describeSkipReason(PlaceholderSkipReason reason) {
    switch (reason) {
    case PlaceholderSkipReason::MissingLane: return "missing lane";
    case PlaceholderSkipReason::InvalidShipDate: return "invalid ship date";
    case PlaceholderSkipReason::InvalidDeliveryDate: return "invalid delivery date";
    case PlaceholderSkipReason::InvalidLoadCount: return "invalid load count";
    case PlaceholderSkipReason::ZeroLoads: return "zero loads";
    case PlaceholderSkipReason::UnknownEquipment: return "unknown equipment";
    case PlaceholderSkipReason::BeforeWindow: return "before window";
    case PlaceholderSkipReason::AfterWindow: return "after window";
    }
    return "unknown";
}

} // namespace ob
