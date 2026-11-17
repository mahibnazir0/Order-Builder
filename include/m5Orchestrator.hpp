#pragma once
// ============================================================================
// m5Orchestrator.hpp - one lane-date planned end to end (architecture 7.1).
//
//   1. snapshot: the hand-over checked and frozen; bad input plans nothing
//   2. budget: the allowance declared before any search, never enlarged
//   3. methods: A in its share of the allowance; then B (ED-Q3, Q6) when A's
//      truck count is above a valid same-goods bound, or A left goods a
//      better allocation might serve - each skip has its reason
//   4. per method: supplies per group -> allocate across the shared pool ->
//      one candidate -> validateFinal (repair once, on a copy, if it fails)
//   5. select the better finally validated candidate by the comparator
//   6. fullness labels on the selected plan's final residual (advisory)
//   7. outcome kind and stop reason, method records, timing, commit delta
// Candidates never touch M4's book or plan: only the delta, applied by the
// integration, does (INV-17). Method A and B records are kept apart and
// never summed (INV-22).
// ============================================================================

#include "m5Allocation.hpp"
#include "m5Budget.hpp"
#include "m5Commit.hpp"
#include "m5Methods.hpp"
#include "m5Validation.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ob {
namespace m5 {

enum class OutcomeKind {
    CompletedNoEligibleInput,
    CompletedZeroDispatch,
    CompletedWithDispatch,
    TimeoutNoIncumbent,
    TimeoutWithIncumbent,
    InvalidInput,
    ConfigurationIncomplete,
};

enum class StopReason { Completed, BudgetExhausted, InputInvalid, ConfigurationIncomplete };
enum class ServiceLevel { Full, Partial, Zero };

struct MethodRecord {
    MethodId method = MethodId::PatternRepeat;
    bool ran = false;
    std::string skippedBecause;      // empty when it ran
    bool validated = false;          // its candidate passed validateFinal
    bool selected = false;
    std::size_t trucks = 0;
    double unitLoads = 0.0;
    double seconds = 0.0;
    std::vector<std::string> findings;
};

// The quick count against a valid bound of the same goods and model (Q6, CL-04).
struct EscalationRecord {
    std::optional<long long> quickTrucks;   // A's trucks, when A shipped every eligible good
    std::optional<long long> bound;         // L1 on floor, payload and unit-load cap, groups apart
    bool escalated = false;
    std::string reason;
    std::string gap;
};

struct M5LaneOutcome {
    OutcomeKind kind = OutcomeKind::CompletedNoEligibleInput;
    StopReason stop = StopReason::Completed;
    bool hasCandidate = false;              // a checked plan with at least one truck
    bool finalValidationPassed = false;
    ServiceLevel requiredService = ServiceLevel::Zero;
    ServiceLevel offeredService = ServiceLevel::Zero;
    ProofStatus proof = ProofStatus::NotAttempted;
    std::string proofScope;
    std::optional<CandidatePlan> selected;  // only a finally validated plan
    std::vector<MethodRecord> methods;
    std::vector<FullnessLabel> fullness;
    BudgetLedger timing;
    std::optional<CommitDelta> delta;
    std::vector<std::string> findings;      // why input or configuration was refused
    long long reservedTrucks = 0;
    double allowanceSeconds = 0.0;
    std::string comparator;
    EscalationRecord escalation;
    std::vector<LeftOver> leftOver;         // every atom not on a selected truck, with its cause
    FloorSearchRecord floorSearch;          // the route context's floor-search states and stops (ED-19)
};

// What a run supplies beyond the profile. Tests inject a clock, a fixed allowance (FX-CLOCK),
// a work count, their own methods or a hook before each method; production passes the clock only.
struct M5RunServices {
    const Clock& clock;
    std::optional<double> allowanceSeconds;       // FX-CLOCK: replaces secondsPerLoad x reserved
    std::optional<long long> workAllowance;       // deterministic stop
    std::vector<std::shared_ptr<const MethodPlanner>> methods;  // empty: the profile's method policy
    std::function<void(MethodId)> beforeMethod;
    std::function<void(std::size_t)> afterBlock;  // test seam, see MethodContext
    // Test seam only: replaces the check of one eligible atom alone on the roomiest slot, so a test can
    // force an Undecided verdict that a one-unit check never gives today (U-CF-08, ED-19).
    std::function<PhysicalCheck(std::size_t atom)> singleUnitCheck;
    // FX-COMMIT test seam only: a run the allowance stopped returns no plan at all instead of its
    // best checked one. Production keeps the checked plan (ED-10).
    bool wholeLaneRollback = false;

    explicit M5RunServices(const Clock& runClock) : clock(runClock) {}
    M5RunServices(const M5RunServices&) = delete;
    M5RunServices& operator=(const M5RunServices&) = delete;
};

// One lane-date is one route context for floor searches (ED-19): unless `physical` already carries a
// route budget, the run owns one with floor's per-search and route limits and the lane-date's wall
// deadline, and every method, block, repair and probe draws on it. Final validation re-checks committed
// trucks outside it.
M5LaneOutcome planLaneDate(const M5LaneDateInput& input, const M5PolicyProfile& profile, const PhysicalSettings& physical,
                           const M5RunServices& services);

// The outcome for a lane-date whose M5 settings could not be read: nothing planned, every atom
// left over with the configuration reason.
M5LaneOutcome configurationIncomplete(const M5LaneDateInput& input, const std::string& error);

const char* outcomeKindName(OutcomeKind kind);
const char* stopReasonName(StopReason reason);
const char* serviceLevelName(ServiceLevel level);

} // namespace m5
} // namespace ob
