#include "m5Orchestrator.hpp"
#include "m5Repair.hpp"
#include "m5Solver.hpp"

#include <algorithm>
#include <set>

using namespace std;

namespace ob {
namespace m5 {

namespace {

// Method B's slice ends at this share of what A leaves, so final validation, selection and the
// fullness labels run inside the same allowance (Section 10.1).
constexpr double kSearchShareAfterA = 0.9;
constexpr const char* kCountModel = "capacityOnly/allRequired/groupsSeparate (floor area, payload, unit-load cap)";

M5LaneOutcome leftOverAll(const M5LaneDateInput& input, LeftOverCause cause, OutcomeKind kind, StopReason stop,
                          vector<string> findings) {
    M5LaneOutcome outcome;
    outcome.kind = kind;
    outcome.stop = stop;
    outcome.findings = std::move(findings);
    outcome.reservedTrucks = static_cast<long long>(input.pool.slots.size());
    for (size_t atom = 0; atom < input.atoms.size(); ++atom) outcome.leftOver.push_back({atom, cause});
    return outcome;
}

bool oneTrailer(const ReservationPool& pool) {
    for (const TrailerCapability& slot : pool.slots) {
        if (slot.signature() != pool.slots.front().signature()) return false;
    }
    return true;
}

bool isRequired(const Atom& atom) {
    return any_of(atom.components.begin(), atom.components.end(),
                  [](const Component& component) { return component.offerKind == OfferKind::Required; });
}

AllocationKey planKey(const DemandSnapshot& snapshot, const UrgencyDomain& domain, const CandidatePlan& plan) {
    AllocationKey total;
    total.shippedByClass.assign(domain.size(), 0);
    for (const GroupId& group : snapshot.groups) {
        GroupTrucks trucks;
        for (const TruckPlan& truck : plan.trucks) {
            if (truck.group == group) trucks.push_back(truck.atoms);
        }
        addTo(total, keyOf(snapshot, domain, trucks));
    }
    return total;
}

CandidatePlan assemble(const DemandSnapshot& snapshot, MethodId method, const AllocationResult& allocation,
                       const vector<char>& unfittable, const M5PolicyProfile& profile, const PhysicalSettings& physical) {
    CandidatePlan plan;
    plan.method = method;
    const vector<TrailerCapability>& slots = snapshot.input.pool.slots;
    vector<char> shipped(snapshot.input.atoms.size(), 0);
    for (size_t group = 0; group < allocation.chosen.size(); ++group) {
        for (const Truck& atoms : allocation.chosen[group]) {
            if (atoms.empty()) continue;
            TruckPlan truck;
            truck.slot = plan.trucks.size();
            truck.group = snapshot.groups[group];
            truck.atoms = atoms;
            truck.layout = checkTruck(snapshot.input, atoms, slots[truck.slot], physical).layout;
            for (const size_t atom : atoms) shipped[atom] = 1;
            plan.trucks.push_back(std::move(truck));
        }
    }
    // With the optional policy on, a good left while slots stay empty was withheld for it.
    const bool slotsLeft = plan.trucks.size() < slots.size();
    for (size_t atom = 0; atom < shipped.size(); ++atom) {
        if (shipped[atom]) continue;
        LeftOverCause cause = LeftOverCause::OverReservation;
        if (!snapshot.eligibility[atom].eligible) {
            cause = LeftOverCause::NotEligible;
        } else if (unfittable[atom]) {
            cause = LeftOverCause::Unfittable;
        } else if (profile.acceptance && slotsLeft) {
            cause = LeftOverCause::AcceptanceWithheld;
        }
        plan.leftOver.push_back({atom, cause});
    }
    return plan;
}

// The trucks a failed validation names, removed on a copy; the copy stands only if it then passes.
optional<CandidatePlan> repairOnce(const CandidatePlan& failed, const DemandSnapshot& snapshot, const M5PolicyProfile& profile,
                                   const PhysicalSettings& physical, const StopCheck& stop) {
    set<size_t> broken;
    for (const ValidationFinding& finding : failed.validation->findings) {
        if (!finding.truck) return nullopt;  // a plan-wide failure no truck removal repairs
        broken.insert(*finding.truck);
    }
    CandidatePlan work = failed;
    {
        CandidateEditor editor(work);
        for (auto truck = broken.rbegin(); truck != broken.rend(); ++truck) editor.removeTruck(*truck, LeftOverCause::OverReservation);
    }
    for (size_t truck = 0; truck < work.trucks.size(); ++truck) work.trucks[truck].slot = truck;
    work.validation = validateFinal(work, snapshot, profile, physical, stop);
    if (!work.validation->allPassed()) return nullopt;
    return work;
}

ServiceLevel serviceOf(size_t shipped, size_t wanted) {
    if (wanted == 0 || shipped == wanted) return ServiceLevel::Full;
    return shipped == 0 ? ServiceLevel::Zero : ServiceLevel::Partial;
}

vector<shared_ptr<const MethodPlanner>> methodsFor(const M5PolicyProfile& profile) {
    switch (profile.method) {
    case MethodPolicy::PatternOnly: return {make_shared<PatternPlanner>()};
    case MethodPolicy::BlocksOnly: return {make_shared<BlockPlanner>()};
    case MethodPolicy::SequentialAThenB: break;
    }
    return {make_shared<PatternPlanner>(), make_shared<BlockPlanner>()};
}

}  // namespace

M5LaneOutcome configurationIncomplete(const M5LaneDateInput& input, const string& error) {
    return leftOverAll(input, LeftOverCause::ConfigurationIncomplete, OutcomeKind::ConfigurationIncomplete,
                       StopReason::ConfigurationIncomplete, {error});
}

M5LaneOutcome planLaneDate(const M5LaneDateInput& input, const M5PolicyProfile& profile, const PhysicalSettings& physical,
                           const M5RunServices& services) {
    SnapshotBuild build = buildSnapshot(input);
    if (!build.snapshot) {
        return leftOverAll(input, LeftOverCause::InvalidInput, OutcomeKind::InvalidInput, StopReason::InputInvalid, build.errors);
    }
    if (!oneTrailer(input.pool)) {
        return leftOverAll(input, LeftOverCause::InvalidInput, OutcomeKind::InvalidInput, StopReason::InputInvalid,
                           {"the reserved slots carry different trailers: the reservation feed has no per-truck trailer, so "
                            "M5 plans one trailer per lane-date (open item C9)"});
    }
    const DemandSnapshot& snapshot = *build.snapshot;
    const size_t slotCount = input.pool.slots.size();

    string allowanceError;
    const optional<double> allowance = services.allowanceSeconds
                                           ? services.allowanceSeconds
                                           : laneAllowanceSeconds(profile.secondsPerLoad, static_cast<long long>(slotCount), &allowanceError);
    if (!allowance || !isfinite(*allowance) || *allowance < 0.0) {
        return configurationIncomplete(input, "the lane-date allowance cannot be declared: seconds per load " + allowanceError);
    }
    BudgetDeclaration declaration;
    declaration.allowanceSeconds = *allowance;
    declaration.workAllowance = services.workAllowance;
    declaration.contract = services.allowanceSeconds ? "FX-CLOCK fixed allowance" : "secondsPerLoad x reserved trucks (CL-05)";
    Budget budget(declaration, services.clock);

    M5LaneOutcome outcome;
    outcome.reservedTrucks = static_cast<long long>(slotCount);
    outcome.allowanceSeconds = *allowance;
    const unique_ptr<AllocationComparator> comparator = makeComparator(profile.comparator);
    outcome.comparator = comparator->id();
    const UrgencyDomain domain(snapshot);

    vector<char> unfittable(input.atoms.size(), 0);
    size_t eligibleFitting = 0, requiredFitting = 0;
    for (size_t atom = 0; atom < input.atoms.size(); ++atom) {
        if (!snapshot.eligibility[atom].eligible) continue;
        if (slotCount > 0 && !checkTruck(input, {atom}, input.pool.slots.front(), physical).fits) {
            unfittable[atom] = 1;
            continue;
        }
        ++eligibleFitting;
        if (isRequired(input.atoms[atom])) ++requiredFitting;
    }
    const bool anyEligible = any_of(snapshot.eligibility.begin(), snapshot.eligibility.end(), [](const Eligibility& e) { return e.eligible; });
    if (!anyEligible) {
        M5LaneOutcome empty = leftOverAll(input, LeftOverCause::NotEligible, OutcomeKind::CompletedNoEligibleInput,
                                          StopReason::Completed, {});
        empty.allowanceSeconds = *allowance;
        empty.timing = budget.ledger();
        return empty;
    }
    // No truck reserved: a completed plan of no trucks, not a timeout of a zero allowance (U-RT-06).
    if (slotCount == 0) {
        M5LaneOutcome empty = leftOverAll(input, LeftOverCause::OverReservation, OutcomeKind::CompletedZeroDispatch,
                                          StopReason::Completed, {});
        for (LeftOver& left : empty.leftOver) {
            if (!snapshot.eligibility[left.atom].eligible) left.cause = LeftOverCause::NotEligible;
        }
        MethodRecord skipped;
        skipped.skippedBecause = "no truck is reserved on this lane-date";
        empty.methods.push_back(skipped);
        empty.timing = budget.ledger();
        return empty;
    }

    GeometryCache cache;
    vector<shared_ptr<const MethodPlanner>> methods = services.methods.empty() ? methodsFor(profile) : services.methods;
    optional<CandidatePlan> incumbent;
    AllocationKey incumbentKey;
    bool stoppedByBudget = false;
    for (size_t position = 0; position < methods.size(); ++position) {
        const MethodPlanner& method = *methods[position];
        MethodRecord record;
        record.method = method.id();
        if (budget.exhausted()) {
            record.skippedBecause = "the lane-date allowance was spent before it could start";
            stoppedByBudget = true;
            outcome.methods.push_back(record);
            continue;
        }
        // Q6: after the quick method, the careful one runs when the quick count is above a valid
        // bound of the same goods, or the quick plan left goods a better allocation might serve.
        if (position > 0 && profile.method == MethodPolicy::SequentialAThenB && services.methods.empty() && incumbent) {
            const EscalationRecord& escalation = outcome.escalation;
            const bool shippedAll = escalation.quickTrucks.has_value();
            if (shippedAll && !shouldEscalate(*escalation.quickTrucks, *escalation.bound, profile.gapTrucks, !budget.exhausted())) {
                record.skippedBecause = "A's " + to_string(*escalation.quickTrucks) + " trucks equal the valid bound of "
                                        + to_string(*escalation.bound) + " for the same goods (Q6: equality stops the count phase)";
                outcome.methods.push_back(record);
                continue;
            }
            outcome.escalation.escalated = true;
        }
        if (services.beforeMethod) services.beforeMethod(method.id());
        const double sliceEnd = budget.sliceEnd(position == 0 && methods.size() > 1 ? profile.methodAShare : kSearchShareAfterA);
        budget.startPhase(method.id() == MethodId::PatternRepeat ? "methodA" : method.id() == MethodId::RollingBlocks ? "methodB" : "method");
        const double started = services.clock.nowSeconds();
        const StopCheck stop = [&budget, sliceEnd] { return budget.pastSlice(sliceEnd); };
        const MethodContext context{snapshot, domain, profile, physical, *comparator, cache, unfittable, slotCount, stop,
                                    services.afterBlock};
        vector<GroupSupply> supplies;
        for (const GroupId& group : snapshot.groups) supplies.push_back(method.supplyForGroup(context, group));
        const AllocationResult allocation = allocate(snapshot, domain, supplies, slotCount, *comparator);
        // A method cut short with nothing built has no result: its empty plan is not a checked
        // zero-dispatch answer (X-RUN-02).
        const bool cutShort = stop();
        if (budget.exhausted()) stoppedByBudget = true;
        CandidatePlan candidate = assemble(snapshot, method.id(), allocation, unfittable, profile, physical);

        budget.startPhase("validation");
        candidate.validation = validateFinal(candidate, snapshot, profile, physical);
        if (!candidate.validation->allPassed()) {
            for (const ValidationFinding& finding : candidate.validation->findings) record.findings.push_back(finding.section + ": " + finding.reason);
            if (optional<CandidatePlan> repaired = repairOnce(candidate, snapshot, profile, physical, StopCheck())) {
                candidate = std::move(*repaired);
            }
        }
        record.ran = true;
        record.seconds = services.clock.nowSeconds() - started;
        record.validated = candidate.validation && candidate.validation->allPassed();
        record.trucks = candidate.trucks.size();
        for (const TruckPlan& truck : candidate.trucks) {
            for (const size_t atom : truck.atoms) record.unitLoads += input.atoms[atom].unitLoads;
        }

        if (position == 0) {
            // A's count against the bound, on the same goods: only when A shipped all of them.
            long long bound = 0;
            for (const GroupId& group : snapshot.groups) {
                bound += trucksAtLeast(input, rankedCandidates(context, group), input.pool.slots.front(), physical);
            }
            outcome.escalation.bound = bound;
            size_t shippedAtoms = 0;
            for (const TruckPlan& truck : candidate.trucks) shippedAtoms += truck.atoms.size();
            const bool shippedAll = record.validated && shippedAtoms == eligibleFitting;
            if (shippedAll) outcome.escalation.quickTrucks = static_cast<long long>(candidate.trucks.size());
            // A gap compares a count with a bound of the same goods; when A left goods there is none.
            outcome.escalation.gap = shippedAll ? gapText(static_cast<long long>(candidate.trucks.size()), bound)
                                                : "N/A: A left eligible goods; the bound of " + to_string(bound)
                                                      + " trucks is for all of them";
            outcome.escalation.reason = shippedAll ? "A shipped every eligible good; careful method runs only if A is above the bound"
                                                   : "A left eligible goods: the careful method may serve them better";
            candidate.evidence.incumbent = static_cast<long long>(candidate.trucks.size());
            candidate.evidence.layoutScope = "M4 supported floor patterns";
            candidate.evidence.proof = ProofStatus::Unproven;
            // The bound is of all the eligible goods, so it bounds this plan only when it ships them
            // all: a bound of different goods is never attached (INV-19).
            if (shippedAll) {
                candidate.evidence.lowerBound = bound;
                candidate.evidence.boundModel = kCountModel;
                candidate.evidence.proofModel = kCountModel;
                if (static_cast<long long>(candidate.trucks.size()) == bound) candidate.evidence.proof = ProofStatus::ProvenOptimal;
            }
            // The evidence changed after validation; validate again so the claim is checked too.
            candidate.validation = validateFinal(candidate, snapshot, profile, physical);
            record.validated = candidate.validation->allPassed();
            if (!record.validated) {
                for (const ValidationFinding& finding : candidate.validation->findings) {
                    record.findings.push_back(finding.section + ": " + finding.reason);
                }
            }
        }

        budget.startPhase("selection");
        if (cutShort && candidate.trucks.empty()) {
            record.validated = false;
            record.findings.push_back("stopped by the allowance before it built a truck");
        }
        if (record.validated) {
            const AllocationKey key = planKey(snapshot, domain, candidate);
            if (!incumbent || comparator->better(key, incumbentKey)) {
                incumbent = std::move(candidate);
                incumbentKey = key;
            }
        }
        outcome.methods.push_back(std::move(record));
    }
    if (services.wholeLaneRollback && stoppedByBudget && incumbent) {
        incumbent.reset();
        outcome.findings.push_back("whole-lane rollback: the allowance stopped the run, so no plan is kept");
    }
    for (MethodRecord& record : outcome.methods) record.selected = incumbent && record.ran && record.method == incumbent->method;

    if (incumbent) {
        budget.startPhase("fullness");
        const RemainingDemandView remaining = remainingAfterSelected(snapshot, *incumbent);
        const StopCheck stop = [&budget] { return budget.exhausted(); };
        outcome.fullness = evaluateFullness(*incumbent, snapshot, remaining, profile, physical, stop);
        size_t requiredShipped = 0, shipped = 0;
        for (const TruckPlan& truck : incumbent->trucks) {
            for (const size_t atom : truck.atoms) {
                ++shipped;
                if (isRequired(input.atoms[atom])) ++requiredShipped;
            }
        }
        outcome.requiredService = serviceOf(requiredShipped, requiredFitting);
        outcome.offeredService = serviceOf(shipped, eligibleFitting);
        outcome.hasCandidate = !incumbent->trucks.empty();
        outcome.finalValidationPassed = true;
        if (outcome.offeredService == ServiceLevel::Full) {
            outcome.proof = ProofStatus::ProvenOptimal;
            outcome.proofScope = "service: every eligible good that fits a reserved trailer ships, so no allocation ships "
                                 "more of any urgency class; fill and equal-service ties are not covered (OPEN-COMP)";
        } else {
            outcome.proof = ProofStatus::Unproven;
            outcome.proofScope = "service: best of the plans methods A and B supplied, not proven best of every allocation";
        }
        string error;
        outcome.delta = buildDelta(*incumbent, snapshot, profile, &error);
        if (!outcome.delta) outcome.findings.push_back("no commit delta: " + error);
        outcome.leftOver = incumbent->leftOver;
        outcome.selected = std::move(incumbent);
    } else {
        for (size_t atom = 0; atom < input.atoms.size(); ++atom) {
            const LeftOverCause cause = !snapshot.eligibility[atom].eligible ? LeftOverCause::NotEligible
                                        : unfittable[atom]                   ? LeftOverCause::Unfittable
                                        : stoppedByBudget                    ? LeftOverCause::NoResult
                                                                             : LeftOverCause::OverReservation;
            outcome.leftOver.push_back({atom, cause});
        }
    }
    budget.finish();
    outcome.timing = budget.ledger();
    outcome.stop = stoppedByBudget || outcome.timing.exhausted ? StopReason::BudgetExhausted : StopReason::Completed;
    if (outcome.selected) {
        if (outcome.stop == StopReason::BudgetExhausted) {
            outcome.kind = outcome.hasCandidate ? OutcomeKind::TimeoutWithIncumbent : OutcomeKind::TimeoutNoIncumbent;
        } else {
            outcome.kind = outcome.hasCandidate ? OutcomeKind::CompletedWithDispatch : OutcomeKind::CompletedZeroDispatch;
        }
    } else {
        outcome.kind = outcome.stop == StopReason::BudgetExhausted ? OutcomeKind::TimeoutNoIncumbent : OutcomeKind::CompletedZeroDispatch;
    }
    return outcome;
}

const char* outcomeKindName(OutcomeKind kind) {
    switch (kind) {
    case OutcomeKind::CompletedNoEligibleInput: return "completed: no eligible goods";
    case OutcomeKind::CompletedZeroDispatch: return "completed: no truck dispatched";
    case OutcomeKind::CompletedWithDispatch: return "completed with dispatch";
    case OutcomeKind::TimeoutNoIncumbent: return "timed out: no checked result";
    case OutcomeKind::TimeoutWithIncumbent: return "timed out: best checked result kept";
    case OutcomeKind::InvalidInput: return "input invalid";
    case OutcomeKind::ConfigurationIncomplete: return "configuration incomplete";
    }
    return "unknown";
}

const char* stopReasonName(StopReason reason) {
    switch (reason) {
    case StopReason::Completed: return "completed";
    case StopReason::BudgetExhausted: return "allowance spent";
    case StopReason::InputInvalid: return "input invalid";
    case StopReason::ConfigurationIncomplete: return "configuration incomplete";
    }
    return "unknown";
}

const char* serviceLevelName(ServiceLevel level) {
    switch (level) {
    case ServiceLevel::Full: return "full";
    case ServiceLevel::Partial: return "partial";
    case ServiceLevel::Zero: return "zero";
    }
    return "unknown";
}

} // namespace m5
} // namespace ob
