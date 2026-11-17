#pragma once
// ============================================================================
// m5Ledger.hpp - identity, eligibility and conservation for one lane-date.
//
// buildSnapshot checks the hand-over before any search and freezes it: methods
// A and B plan from the same immutable snapshot and never touch M4's book.
// Eligibility is decided per component on M4's own date rules (isoDate's day
// arithmetic, the pull-forward window): a stack is eligible only when every
// line in it is (X-DATE-06), so a later line is never hidden by an earlier one.
//
// The residual view after a selected plan is the declared source universe -
// offered atoms plus the rule-excluded sources M4 did not offer - minus the
// selected draws only. It is what fullness buckets are judged on (ED-FULL).
// ============================================================================

#include "m5Types.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ob {
namespace m5 {

enum class EligibilityReason { Ok, NotAvailable, DueBeyondPullWindow, PullForwardOff };

struct Eligibility {
    bool eligible = true;
    EligibilityReason reason = EligibilityReason::Ok;
    std::vector<std::size_t> failingComponents;  // into Atom::components
};

// Pure, on already validated dates.
Eligibility componentEligibility(const Component& component, const std::string& departure, const DateRules& rules);
Eligibility atomEligibility(const Atom& atom, const std::string& departure, const DateRules& rules);

struct DemandSnapshot {
    SnapshotId id;                        // digest of the input
    M5LaneDateInput input;
    std::vector<Eligibility> eligibility; // parallel to input.atoms
    std::vector<GroupId> groups;          // sorted, every group with an atom
};

struct SnapshotBuild {
    std::optional<DemandSnapshot> snapshot;
    std::vector<std::string> errors;      // each names the atom, component or slot at fault
};

// Checks every field that becomes a divisor, a total or a limit - negative, zero where it means
// nothing, and non-finite alike - and every date and priority, before freezing the input.
SnapshotBuild buildSnapshot(const M5LaneDateInput& input);

struct ResidualSource {
    SourceLineId source;
    GroupId group;
    double unitLoads = 0.0;
    bool eligible = false;               // date-eligible on the departure; physical fit is separate
    std::string reason;                  // why not, when not
    std::optional<std::size_t> atom;     // empty for a source M4 did not offer
};

struct RemainingDemandView {
    SnapshotId sourceVersion;
    std::uint64_t selectedRevision = 0;
    bool completeForDeclaredScope = false;
    std::vector<ResidualSource> sources;
};

RemainingDemandView remainingAfterSelected(const DemandSnapshot& snapshot, const CandidatePlan& plan);

struct Reconciliation {
    bool balanced = false;
    std::vector<std::string> findings;
};

// Every offered atom exactly once, on a truck or left over, and per source line the unit loads in
// equal those shipped plus those left (INV-01, X-DATA-02: per line, not only in total).
Reconciliation reconcile(const DemandSnapshot& snapshot, const CandidatePlan& plan);

const char* eligibilityReasonName(EligibilityReason reason);

} // namespace m5
} // namespace ob
