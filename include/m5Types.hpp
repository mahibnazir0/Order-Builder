#pragma once
// ============================================================================
// m5Types.hpp - the data every M5 module shares: one lane-date's goods and
// reservation pool, candidate plans, validation reports and solver evidence.
//
// Numbers (ED-ARITH). M5 never re-normalises a quantity: every dimension,
// weight and unit-load count is the value M4 handed over, held as the double
// M4 computed it. Those values are the authoritative normalised figures; final
// hard limits, the optional minimum-load predicate and the fullness thresholds
// compare them exactly, never after rounding to M4's 0.001 search grain or to
// report precision. The search grains belong to M4's packers alone.
//
// Atoms (ED-07). An atom is one M4 stack (SelectionItem): one rigid floor item
// M5 never splits, re-stacks or tops up. Its components are the source lines it
// carries, each with its own dates and priority, so eligibility is decided per
// component, never hidden by the stack.
//
// Indices, never pointers: every type here is copy-safe.
// ============================================================================

#include "floorPattern.hpp"
#include "selectionItem.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ob {
namespace m5 {

using SourceLineId = std::string;
using GroupId = std::string;
using SnapshotId = std::string;
using ReservationVersion = std::uint64_t;

struct LaneKey {
    std::string origin;
    std::string destination;
    std::string shipCondition;
    bool operator<(const LaneKey& other) const;
    bool operator==(const LaneKey& other) const;
    std::string text() const;  // "2028 -> 2574 TL"
};

// Required: due by the departure (carried goods included). CanGo: pulled forward, due after it.
enum class OfferKind { Required, CanGo };

// One source line's share inside an atom.
struct Component {
    SourceLineId source;
    OfferKind offerKind = OfferKind::Required;
    std::string product;
    double unitLoads = 0.0;      // already normalised by M4; M5 never divides again
    std::string dueDate;         // DATFR_TA, kept through carry
    std::string availableDate;   // AVAIL_DATE
    int priority = 0;            // urgency rank 0..kMaxPriority, 0 most urgent
};

struct Atom {
    std::string id;              // stable identity: the M4 item index, or a fixture's name
    GroupId group;
    FloorItem floor;             // footprint, height (deck included), levels and cube, as M4 built it
    double weightLb = 0.0;       // final gross, tare counted once
    double unitLoads = 0.0;
    std::vector<Component> components;  // at least one; bottom to top
};

// One reserved truck's trailer. Real inputs give every slot the run's trailer (C9: no per-truck
// figures in the reservation feed); the type keeps the slot's own so the seam can be tested.
struct TrailerCapability {
    double lengthIn = 0.0;
    double widthIn = 0.0;
    double heightIn = 0.0;
    double maxWeightLb = 0.0;
    std::string signature() const;  // equal capabilities share patterns and supplies
};

struct ReservationPool {
    ReservationVersion version = 1;
    std::vector<TrailerCapability> slots;      // one per reserved truck, shared by every group
    std::optional<double> penaltyPerUnusedTruck;  // CL-06: empty is unknown, never zero
};

struct DateRules {
    bool pullForward = false;
    long long maxDaysPulledForward = 0;  // used only while pullForward is on
    long long daysAllowed() const { return pullForward ? maxDaysPulledForward : 0; }
};

// A source of the declared lane scope that was not offered on this departure, kept so the final
// residual view can tell "no goods remain" from "only rule-excluded goods remain" (ED-FULL).
struct ExcludedSource {
    SourceLineId source;
    GroupId group;
    double unitLoads = 0.0;
    std::string reason;  // "due after the pull-forward window", "not available", ...
};

// One lane-date as M5 receives it, from M4's hand-over or from a test fixture.
struct M5LaneDateInput {
    LaneKey lane;
    std::string departure;  // the lane-date's ship date, YYYY-MM-DD
    std::vector<Atom> atoms;
    std::vector<ExcludedSource> excluded;
    // The declared universe for fullness buckets is complete: atoms plus excluded hold every
    // source of the lane's groups this departure could see.
    bool sourceScopeComplete = false;
    ReservationPool pool;
    DateRules dates;
    // The lane's own past-due order (P9): the run's unless the lane is set apart.
    PastDueOrder pastDueOrder = PastDueOrder::AgeFirst;
    std::string sourceVersion;  // identity of the hand-over this input was gathered from
};

enum class LeftOverCause {
    NotEligible,          // a component's dates rule it out on this departure
    Unfittable,           // fits no slot's trailer on its own
    OverReservation,      // the shared pool had no truck left for it
    AcceptanceWithheld,   // optional minimum-load policy only
    NoResult,             // the lane-date ended without a checked plan
    ConfigurationIncomplete,
    InvalidInput,
};

struct LeftOver {
    std::size_t atom = 0;  // into M5LaneDateInput::atoms
    LeftOverCause cause = LeftOverCause::OverReservation;
};

enum class MethodId { PatternRepeat, RollingBlocks, Stub };

// One truck of a candidate. Totals are recomputed from the atoms by validation; a layout is the
// witness arrangeFloor gave for these exact atoms.
struct TruckPlan {
    std::size_t slot = 0;
    GroupId group;
    std::vector<std::size_t> atoms;  // into M5LaneDateInput::atoms, ascending
    FloorLayout layout;
};

enum class ProofStatus { NotAttempted, Unproven, ProvenOptimal, ProvenInfeasible };

enum class Termination { Completed, IterationCap, Budget, Cancelled, Error };

// What a solve may claim, and about which model (INV-19): an LP value is not a bound, a bound is
// not an incumbent, and a proof names its model, objective and layout scope.
struct SolverEvidence {
    std::optional<double> lpValue;
    std::string lpStatus;                       // "converged", "pricingIncomplete", "notRun"
    std::optional<long long> restrictedMasterInteger;
    std::optional<long long> incumbent;
    std::optional<long long> lowerBound;
    std::string boundModel;                     // the model the bound is valid for
    ProofStatus proof = ProofStatus::NotAttempted;
    std::string proofModel;
    std::string layoutScope;                    // "M4 supported floor patterns" for physical trucks
    Termination termination = Termination::Completed;
    std::size_t columnsGenerated = 0;
};

enum class SectionStatus { Pass, Fail, NotApplicable, Incomplete };

struct ValidationFinding {
    std::optional<std::size_t> truck;  // into CandidatePlan::trucks
    std::string section;
    std::string reason;
};

// Mandatory dispatch checks on a candidate's final contents. Valid only for the candidate revision,
// profile, source snapshot and reservation version it names.
struct ValidationReport {
    std::uint64_t revision = 0;
    std::string profileKey;
    SnapshotId sourceSnapshot;
    ReservationVersion reservationVersion = 0;
    SectionStatus physical = SectionStatus::Incomplete;
    SectionStatus conservation = SectionStatus::Incomplete;
    SectionStatus eligibility = SectionStatus::Incomplete;
    SectionStatus groups = SectionStatus::Incomplete;
    SectionStatus reservations = SectionStatus::Incomplete;
    SectionStatus acceptance = SectionStatus::Incomplete;
    SectionStatus metadata = SectionStatus::Incomplete;
    bool mandatoryChecksComplete = false;
    std::vector<ValidationFinding> findings;
    bool allPassed() const;
};

struct CandidatePlan {
    MethodId method = MethodId::PatternRepeat;
    std::uint64_t revision = 1;  // CandidateEditor increments it on every change
    std::vector<TruckPlan> trucks;
    std::vector<LeftOver> leftOver;
    SolverEvidence evidence;
    std::optional<ValidationReport> validation;  // stands only while its revision is this one's
};

// FNV-1a 64-bit, as 16 hex digits: the identity of snapshots, payloads and profiles. Not a
// security hash; it names content so a stale or different input is detected.
std::string digestOf(const std::string& text);

const char* leftOverCauseName(LeftOverCause cause);
const char* methodName(MethodId method);
const char* proofStatusName(ProofStatus status);
const char* terminationName(Termination termination);
const char* sectionStatusName(SectionStatus status);

} // namespace m5
} // namespace ob
