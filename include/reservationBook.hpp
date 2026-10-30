#pragma once
// ============================================================================
// reservationBook.hpp - M4 module C1. Owns the target: the trucks reserved per
// lane and ship date for one planning run.
//
// A reservation is keyed on origin + destination + ship condition + ship date
// (DATFR_TA). A lane reserved on several dates keeps one reservation per date,
// never one lane total: 183 August lane groups are split this way. Rows for the
// same lane and date are summed.
//
// The run's window of ship dates comes from the demand file's CTL calendar
// (shipCalendar's planningWindow), the same window Milestone 3 floors by.
// ============================================================================

#include "demand_types.hpp"
#include "placeholder_types.hpp"
#include "shipCalendar.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ob {

struct ReservationBookSettings {
    // Used for a placeholder whose ZZNA_EQUIP_SIZE is blank. Must be one of knownEquipment.
    std::string defaultEquipment;
    std::vector<std::string> knownEquipment;
};

// One placeholder row that went into a reservation, in file order.
struct ReservationEntry {
    std::size_t placeholderIndex = 0;
    int trucks = 0;
    std::string equipment;
    bool equipmentDefaulted = false;
};

struct Reservation {
    std::string origin;
    std::string destination;
    std::string shipCondition;
    std::string shipDate;
    long long trucks = 0;
    std::vector<ReservationEntry> entries;
};

enum class PlaceholderSkipReason {
    MissingLane,         // blank LOCFRNO, LOCTONO or SHIP_COND
    InvalidShipDate,     // DATFR_TA is not a real YYYY-MM-DD date
    InvalidDeliveryDate, // DATTO_TA is not a real date, or is before DATFR_TA
    InvalidLoadCount,    // NO_OF_LOADS missing, negative, non-integer or above the cap
    ZeroLoads,           // NO_OF_LOADS is 0: no reservation
    UnknownEquipment,    // ZZNA_EQUIP_SIZE is not one of the known equipment types
    BeforeWindow,        // ship date before the run's window
    AfterWindow,         // ship date after the run's window
};

struct SkippedPlaceholder {
    std::size_t placeholderIndex = 0;
    PlaceholderSkipReason reason = PlaceholderSkipReason::MissingLane;
};

struct ReservationBook {
    PlanningWindow window;
    // Sorted by origin, destination, ship condition, ship date.
    std::vector<Reservation> reservations;
    // Every placeholder row not in a reservation, in file order, with the reason.
    std::vector<SkippedPlaceholder> skipped;
    long long reservedTrucks = 0;
    // Trucks on skipped rows whose NO_OF_LOADS was readable, so the total read from the file
    // reconciles: reservedTrucks + skippedTrucks.
    long long skippedTrucks = 0;
    std::size_t entriesWithDefaultEquipment = 0;
};

// The plan date in a REQUEST_ID such as "#STR_PA4400_20260804154458#" (2026-08-04). The run
// is identified by the file's content, never its name: the run labelled Aug4 is in a file
// named Demand-Aug5-2.json. Throws std::invalid_argument when no valid date is found.
std::string planDateFromRequestId(const std::string& requestId);

// Builds the book. Throws std::invalid_argument for the reasons planningWindow does, and for
// settings whose default equipment is blank or not one of the known types.
ReservationBook buildReservationBook(const std::vector<PlaceholderRecord>& placeholders,
                                     const std::vector<CTLRecord>& ctl,
                                     const std::string& planDate,
                                     const ReservationBookSettings& settings);

// The reservation for this lane and ship date, nullptr if there is none.
const Reservation* findReservation(const ReservationBook& book, const std::string& origin,
                                   const std::string& destination,
                                   const std::string& shipCondition,
                                   const std::string& shipDate);
const Reservation* findReservation(ReservationBook&& book, const std::string& origin,
                                   const std::string& destination,
                                   const std::string& shipCondition,
                                   const std::string& shipDate) = delete;

struct DemandLane {
    std::string origin;
    std::string destination;
    std::string shipCondition;
};

// Reservations against demand, matched on origin + destination + ship condition.
struct ReservationCoverage {
    // Indices into book.reservations whose lane carries no demand line. Every truck on them
    // is penalty exposure: nothing can fill them.
    std::vector<std::size_t> reservationsWithoutDemand;
    long long penaltyExposureTrucks = 0;
    // Lanes with demand and no reservation in the window. No truck is built for them, as the
    // client's own solver reports "No Placeholder(s) Received for this Lane!". Sorted.
    std::vector<DemandLane> demandLanesWithoutReservation;
};

ReservationCoverage matchDemand(const ReservationBook& book,
                                const std::vector<STRRecord>& demand);

// "missing lane", "before window", ... Plain ASCII.
const char* describeSkipReason(PlaceholderSkipReason reason);

} // namespace ob
