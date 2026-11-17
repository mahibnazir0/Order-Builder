#include "m5Validation.hpp"

#include <algorithm>
#include <map>
#include <set>

using namespace std;

namespace ob {
namespace m5 {

namespace {

constexpr const char* kLayoutScope = "M4 supported floor patterns (floorPattern), rigid M2 stacks";

double weightOf(const DemandSnapshot& snapshot, const vector<size_t>& atoms) {
    double weight = 0.0;
    for (const size_t index : atoms) weight += snapshot.input.atoms[index].weightLb;
    return weight;
}

double cubeOf(const DemandSnapshot& snapshot, const vector<size_t>& atoms) {
    double cube = 0.0;
    for (const size_t index : atoms) cube += snapshot.input.atoms[index].floor.cubeCubicIn;
    return cube;
}

SectionStatus statusOf(bool failed) { return failed ? SectionStatus::Fail : SectionStatus::Pass; }

}  // namespace

ValidationReport validateFinal(const CandidatePlan& plan, const DemandSnapshot& snapshot, const M5PolicyProfile& profile,
                               const PhysicalSettings& physical, const StopCheck& stop) {
    ValidationReport report;
    report.revision = plan.revision;
    report.profileKey = profile.key();
    report.sourceSnapshot = snapshot.id;
    report.reservationVersion = snapshot.input.pool.version;
    auto fail = [&](optional<size_t> truck, const string& section, const string& reason) {
        report.findings.push_back({truck, section, reason});
    };
    const vector<TrailerCapability>& slots = snapshot.input.pool.slots;
    const size_t atomCount = snapshot.input.atoms.size();

    // Reservations first: the physical check needs each truck's own slot.
    bool reservationsFailed = plan.trucks.size() > slots.size();
    if (reservationsFailed) {
        fail(nullopt, "reservations", to_string(plan.trucks.size()) + " trucks on " + to_string(slots.size()) + " reserved");
    }
    set<size_t> slotsUsed;
    for (size_t truck = 0; truck < plan.trucks.size(); ++truck) {
        const size_t slot = plan.trucks[truck].slot;
        if (slot >= slots.size()) {
            reservationsFailed = true;
            fail(truck, "reservations", "uses slot " + to_string(slot) + " of " + to_string(slots.size()));
        } else if (!slotsUsed.insert(slot).second) {
            reservationsFailed = true;
            fail(truck, "reservations", "uses slot " + to_string(slot) + ", which another truck uses too");
        }
    }
    report.reservations = statusOf(reservationsFailed);

    bool groupsFailed = false, eligibilityFailed = false, physicalFailed = false, acceptanceFailed = false;
    for (size_t truck = 0; truck < plan.trucks.size(); ++truck) {
        const TruckPlan& planned = plan.trucks[truck];
        if (planned.atoms.empty()) {
            physicalFailed = true;
            fail(truck, "physical", "carries nothing: an empty truck is never dispatched");
            continue;
        }
        bool atomsKnown = true;
        for (const size_t atom : planned.atoms) {
            if (atom >= atomCount) {
                atomsKnown = false;
                continue;
            }
            const Atom& carried = snapshot.input.atoms[atom];
            if (carried.group != planned.group) {
                groupsFailed = true;
                fail(truck, "groups", "carries " + carried.id + " of group " + carried.group + " on a truck of group " + planned.group);
            }
            const Eligibility& eligibility = snapshot.eligibility[atom];
            for (const size_t component : eligibility.failingComponents) {
                eligibilityFailed = true;
                fail(truck, "eligibility", "line " + carried.components[component].source + " of " + carried.id + " is "
                                               + eligibilityReasonName(componentEligibility(carried.components[component],
                                                                                            snapshot.input.departure,
                                                                                            snapshot.input.dates).reason));
            }
        }
        if (!atomsKnown) {
            physicalFailed = true;
            fail(truck, "physical", "names an atom the lane-date was not offered");
            continue;
        }
        if (stop && stop()) return report;
        if (planned.slot < slots.size()) {
            const PhysicalCheck check = checkTruck(snapshot.input, planned.atoms, slots[planned.slot], physical);
            if (!check.fits) {
                physicalFailed = true;
                for (const string& finding : check.findings) fail(truck, "physical", finding);
            }
        }
        if (profile.acceptance) {
            const AcceptanceDecision decision =
                evaluateAcceptance(weightOf(snapshot, planned.atoms), cubeOf(snapshot, planned.atoms), *profile.acceptance);
            if (!decision.accepted) {
                acceptanceFailed = true;
                fail(truck, "acceptance", "neither weight > x nor cube > y on its final contents");
            }
        }
    }
    report.physical = statusOf(physicalFailed);
    report.groups = statusOf(groupsFailed);
    report.eligibility = statusOf(eligibilityFailed);
    report.acceptance = profile.acceptance ? statusOf(acceptanceFailed) : SectionStatus::NotApplicable;

    const Reconciliation reconciliation = reconcile(snapshot, plan);
    for (const string& finding : reconciliation.findings) fail(nullopt, "conservation", finding);
    report.conservation = statusOf(!reconciliation.balanced);

    // A proof must be backed by a bound of its own model equal to its incumbent (INV-10, INV-19).
    bool metadataFailed = false;
    const SolverEvidence& evidence = plan.evidence;
    if (evidence.lowerBound && evidence.incumbent && *evidence.lowerBound > *evidence.incumbent) {
        metadataFailed = true;
        fail(nullopt, "metadata", "claims a lower bound above its own incumbent");
    }
    if (evidence.proof == ProofStatus::ProvenOptimal
        && (!evidence.lowerBound || !evidence.incumbent || *evidence.lowerBound != *evidence.incumbent
            || evidence.boundModel != evidence.proofModel)) {
        metadataFailed = true;
        fail(nullopt, "metadata", "claims proven optimal without an equal bound of the same model");
    }
    report.metadata = statusOf(metadataFailed);
    report.mandatoryChecksComplete = true;
    return report;
}

bool validationCurrent(const CandidatePlan& plan, const DemandSnapshot& snapshot, const M5PolicyProfile& profile) {
    return plan.validation && plan.validation->revision == plan.revision && plan.validation->profileKey == profile.key()
           && plan.validation->sourceSnapshot == snapshot.id && plan.validation->reservationVersion == snapshot.input.pool.version;
}

vector<FullnessLabel> evaluateFullness(const CandidatePlan& plan, const DemandSnapshot& snapshot,
                                       const RemainingDemandView& remaining, const M5PolicyProfile& profile,
                                       const PhysicalSettings& physical, const StopCheck& stop) {
    const vector<size_t> classOf = physicalClassOf(snapshot.input);
    // Per group: one eligible residual atom per physical class (its twins fit exactly when it
    // does), and whether any residual, eligible or not, is left.
    map<GroupId, map<size_t, size_t>> probesOf;
    map<GroupId, bool> anyEligible, anyResidual;
    for (const ResidualSource& source : remaining.sources) {
        anyResidual[source.group] = true;
        if (!source.eligible) continue;
        anyEligible[source.group] = true;
        if (source.atom) probesOf[source.group].emplace(classOf[*source.atom], *source.atom);
    }

    vector<FullnessLabel> labels;
    for (size_t truck = 0; truck < plan.trucks.size(); ++truck) {
        const TruckPlan& planned = plan.trucks[truck];
        const TrailerCapability& capability = snapshot.input.pool.slots[planned.slot];
        FullnessLabel label;
        label.truck = truck;
        label.candidateRevision = plan.revision;
        label.profileKey = profile.key();
        label.remainingSnapshot = remaining.sourceVersion + "#" + to_string(remaining.selectedRevision);
        label.fitScope = kLayoutScope;

        const long double weight = weightOf(snapshot, planned.atoms);
        label.weightPercent = static_cast<double>(100.0L * weight / capability.maxWeightLb);
        label.byWeight = 100.0L * weight >= static_cast<long double>(profile.fullness.weightFullPercent) * capability.maxWeightLb;
        const PhysicalCheck finalCheck = checkTruck(snapshot.input, planned.atoms, capability, physical);
        label.rearInches = rearInchesOf(finalCheck.layout, capability);
        label.byRearInches = finalCheck.fits && label.rearInches >= 0.0 && label.rearInches <= profile.fullness.rearInchLimit;

        const auto probes = probesOf.find(planned.group);
        if (probes != probesOf.end() && !probes->second.empty()) {
            label.probes = probes->second.size();
            label.fitStatus = RemainingFitStatus::ProvenNoFit;
            for (const auto& [physicalClass, atom] : probes->second) {
                if (stop && stop()) {
                    label.fitStatus = RemainingFitStatus::Unknown;
                    break;
                }
                vector<size_t> grown = planned.atoms;
                grown.insert(upper_bound(grown.begin(), grown.end(), atom), atom);
                const PhysicalCheck probe = checkTruck(snapshot.input, grown, capability, physical);
                if (probe.fits) {
                    label.fitStatus = RemainingFitStatus::Fits;
                    break;
                }
                if (probe.undecided) label.fitStatus = RemainingFitStatus::Unknown;
            }
        }
        label.byFloorLimit = label.fitStatus == RemainingFitStatus::ProvenNoFit;
        if (label.byWeight || label.byRearInches || label.byFloorLimit) {
            label.state = FullnessState::Full;
        } else {
            label.state = label.fitStatus == RemainingFitStatus::Unknown ? FullnessState::Undetermined : FullnessState::NotFull;
        }

        if (anyEligible[planned.group]) {
            label.bucket = FullnessBucket::GoodsRemain;
        } else if (anyResidual[planned.group]) {
            label.bucket = FullnessBucket::RuleLimited;
        } else if (remaining.completeForDeclaredScope) {
            label.bucket = FullnessBucket::NoGoodsRemain;
        }
        labels.push_back(std::move(label));
    }
    return labels;
}

const char* fullnessStateName(FullnessState state) {
    switch (state) {
    case FullnessState::Full: return "FULL";
    case FullnessState::NotFull: return "NOT FULL";
    case FullnessState::Undetermined: return "not established (fit check incomplete)";
    }
    return "unknown";
}

const char* fullnessBucketName(FullnessBucket bucket) {
    switch (bucket) {
    case FullnessBucket::GoodsRemain: return "goods remain";
    case FullnessBucket::NoGoodsRemain: return "no goods remain";
    case FullnessBucket::RuleLimited: return "rule-limited";
    }
    return "unknown";
}

const char* remainingFitName(RemainingFitStatus status) {
    switch (status) {
    case RemainingFitStatus::NotApplicable: return "no eligible goods left to probe";
    case RemainingFitStatus::Fits: return "a remaining good fits";
    case RemainingFitStatus::ProvenNoFit: return "no remaining good fits";
    case RemainingFitStatus::Unknown: return "fit check incomplete";
    }
    return "unknown";
}

} // namespace m5
} // namespace ob
