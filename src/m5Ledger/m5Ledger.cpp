#include "m5Ledger.hpp"
#include "demand_types.hpp"
#include "isoDate.hpp"
#include "m5UnitLoads.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

using namespace std;

namespace ob {
namespace m5 {

namespace {

// A line's unit loads add up across the stacks that carry it within this tolerance: M4 deals them
// out as doubles, one level at a time.
constexpr double kUnitLoadSumTolerance = 1e-9;

bool positiveFinite(double value) { return isfinite(value) && value > 0.0; }

void checkAtom(const Atom& atom, size_t index, vector<string>& errors) {
    const string name = "atom " + to_string(index) + " (" + atom.id + ")";
    if (atom.id.empty()) errors.push_back(name + " has no identity");
    if (atom.group.empty()) errors.push_back(name + " names no group");
    const FloorItem& floor = atom.floor;
    if (!positiveFinite(floor.longSideIn) || !positiveFinite(floor.shortSideIn) || floor.shortSideIn > floor.longSideIn) {
        errors.push_back(name + " has a footprint that is not two finite sides above zero, long side first");
    }
    if (!positiveFinite(floor.heightIn)) errors.push_back(name + " has a height that is not finite and above zero");
    if (floor.levels < 1) errors.push_back(name + " has fewer than one level");
    if (!isfinite(floor.cubeCubicIn) || floor.cubeCubicIn < 0.0) errors.push_back(name + " has a cube that is negative or not finite");
    if (!isfinite(atom.weightLb) || atom.weightLb < 0.0) errors.push_back(name + " has a weight that is negative or not finite");
    if (!positiveFinite(atom.unitLoads)) errors.push_back(name + " carries unit loads that are not finite and above zero");
    if (atom.components.empty()) {
        errors.push_back(name + " carries no source line");
        return;
    }
    double componentUnitLoads = 0.0;
    for (size_t position = 0; position < atom.components.size(); ++position) {
        const Component& component = atom.components[position];
        const string where = name + " line " + component.source;
        if (component.source.empty()) errors.push_back(name + " has a component with no source line");
        if (!positiveFinite(component.unitLoads)) errors.push_back(where + " carries unit loads that are not finite and above zero");
        if (component.priority < 0 || component.priority > kMaxPriority) {
            errors.push_back(where + " has priority " + to_string(component.priority) + ", outside 0.." + to_string(kMaxPriority));
        }
        if (!isIsoDate(component.dueDate)) errors.push_back(where + " has a due date that is not a real date: '" + component.dueDate + "'");
        if (!isIsoDate(component.availableDate)) {
            errors.push_back(where + " has an available date that is not a real date: '" + component.availableDate + "'");
        }
        componentUnitLoads += component.unitLoads;
    }
    if (isfinite(componentUnitLoads) && !hundredthsOfUnitLoads(componentUnitLoads)) {
        errors.push_back(name + " carries " + to_string(componentUnitLoads) + " unit loads, past what a 64-bit count of hundredths holds");
    }
    if (isfinite(componentUnitLoads) && fabs(componentUnitLoads - atom.unitLoads) > kUnitLoadSumTolerance * max(1.0, atom.unitLoads)) {
        errors.push_back(name + " carries " + to_string(atom.unitLoads) + " unit loads but its lines add up to "
                         + to_string(componentUnitLoads));
    }
}

bool sameFacts(const TrailerCapability& left, const TrailerCapability& right) {
    return left.lengthIn == right.lengthIn && left.widthIn == right.widthIn && left.heightIn == right.heightIn
           && left.maxWeightLb == right.maxWeightLb && left.maxUnitLoads == right.maxUnitLoads;
}

void checkPool(const ReservationPool& pool, vector<string>& errors) {
    for (size_t slot = 0; slot < pool.slots.size(); ++slot) {
        const TrailerCapability& capability = pool.slots[slot];
        if (!positiveFinite(capability.lengthIn) || !positiveFinite(capability.widthIn) || !positiveFinite(capability.heightIn)
            || !positiveFinite(capability.maxWeightLb)) {
            errors.push_back("reserved slot " + to_string(slot) + " has a trailer size or weight limit that is not finite and above zero");
        }
        if (capability.maxUnitLoads < 1) {
            errors.push_back("reserved slot " + to_string(slot) + " has no unit-load cap: a whole number from 1 to 9223372036854775807 is required");
        }
    }
    if (pool.penaltyPerUnusedTruck && (!isfinite(*pool.penaltyPerUnusedTruck) || *pool.penaltyPerUnusedTruck < 0.0)) {
        errors.push_back("the unused-reservation penalty is negative or not finite");
    }
}

// The same source line in two places must agree on everything but its share (X-DATA-11).
void checkSourcesAgree(const M5LaneDateInput& input, vector<string>& errors) {
    using Identity = tuple<string, string, string, int, string, OfferKind>;
    map<SourceLineId, Identity> seen;
    for (const Atom& atom : input.atoms) {
        for (const Component& component : atom.components) {
            const Identity identity{component.product, component.dueDate, component.availableDate, component.priority, atom.group,
                                    component.offerKind};
            const auto [found, inserted] = seen.emplace(component.source, identity);
            if (!inserted && found->second != identity) {
                errors.push_back("source line " + component.source + " appears with conflicting product, dates, priority or group");
            }
        }
    }
}

string snapshotDigest(const M5LaneDateInput& input) {
    ostringstream out;
    out << hexfloat << input.lane.text() << '|' << input.departure << '|' << input.sourceVersion << '|'
        << input.dates.pullForward << input.dates.maxDaysPulledForward << '|' << static_cast<int>(input.pastDueOrder) << '|';
    for (const Atom& atom : input.atoms) {
        out << atom.id << ',' << atom.group << ',' << atom.floor.longSideIn << ',' << atom.floor.shortSideIn << ','
            << atom.floor.heightIn << ',' << atom.floor.levels << ',' << atom.weightLb << ',' << atom.unitLoads;
        for (const Component& component : atom.components) {
            out << ';' << component.source << ',' << component.product << ',' << component.unitLoads << ',' << component.dueDate
                << ',' << component.availableDate << ',' << component.priority << ',' << static_cast<int>(component.offerKind);
        }
        out << '|';
    }
    for (const ExcludedSource& source : input.excluded) out << source.source << ',' << source.group << ',' << source.unitLoads << '|';
    for (const TrailerCapability& slot : input.pool.slots) out << slot.signature() << '|';
    out << input.pool.version;
    return digestOf(out.str());
}

}  // namespace

Eligibility componentEligibility(const Component& component, const string& departure, const DateRules& rules) {
    Eligibility result;
    if (component.availableDate > departure) {
        result.eligible = false;
        result.reason = EligibilityReason::NotAvailable;
    } else if (component.dueDate > departure) {
        if (!rules.pullForward) {
            result.eligible = false;
            result.reason = EligibilityReason::PullForwardOff;
        } else if (daysBetween(departure, component.dueDate) > rules.maxDaysPulledForward) {
            result.eligible = false;
            result.reason = EligibilityReason::DueBeyondPullWindow;
        }
    }
    return result;
}

Eligibility atomEligibility(const Atom& atom, const string& departure, const DateRules& rules) {
    Eligibility result;
    for (size_t position = 0; position < atom.components.size(); ++position) {
        const Eligibility component = componentEligibility(atom.components[position], departure, rules);
        if (component.eligible) continue;
        if (result.eligible) result.reason = component.reason;
        result.eligible = false;
        result.failingComponents.push_back(position);
    }
    return result;
}

vector<string> slotIdentityErrors(const M5LaneDateInput& input) {
    vector<string> errors;
    const ReservationPool& pool = input.pool;
    if (pool.declaredTrucks && (*pool.declaredTrucks < 0 || static_cast<unsigned long long>(*pool.declaredTrucks) != pool.slots.size())) {
        errors.push_back("the hand-over reserves " + to_string(*pool.declaredTrucks) + " trucks for " + to_string(pool.slots.size())
                         + " slots: a reserved count is a whole number from 0, and the lane-date is not planned on a guess");
    }
    // The codes run parallel to the slots: a list of another length pairs codes with the wrong trucks.
    if (!pool.slotEquipment.empty() && pool.slotEquipment.size() != pool.slots.size()) {
        errors.push_back("the hand-over names equipment for " + to_string(pool.slotEquipment.size()) + " trucks but reserves "
                         + to_string(pool.slots.size()) + " slots: each slot needs exactly one code");
    }
    set<string> equipment;
    for (size_t slot = 0; slot < pool.slots.size(); ++slot) {
        const TrailerCapability& capability = pool.slots[slot];
        const string name = "reserved slot " + to_string(slot);
        const string code = slot < pool.slotEquipment.size() ? pool.slotEquipment[slot] : string();
        if (code.empty()) {
            if (!input.trailerTypes.empty()) errors.push_back(name + " names no equipment, and the run has a trailer-type catalogue");
            continue;
        }
        equipment.insert(code);
        const auto entry = input.trailerTypes.find(code);
        if (entry == input.trailerTypes.end()) {
            errors.push_back(name + " names unknown equipment '" + code
                             + "': it is not in the trailer-type catalogue, and no 53F size, payload or cap is assumed");
        } else if (!sameFacts(capability, entry->second)) {
            errors.push_back(name + "'s size, payload or cap differs from the catalogue entry for '" + code + "'");
        }
    }
    if (equipment.size() > 1) {
        errors.push_back("mixed equipment on one lane-date (" + *equipment.begin() + " and " + *equipment.rbegin()
                         + "): one trailer type per lane-date is supported (C9, U-DA-05)");
    }
    return errors;
}

SnapshotBuild buildSnapshot(const M5LaneDateInput& input) {
    SnapshotBuild build;
    vector<string>& errors = build.errors;
    if (!isIsoDate(input.departure)) errors.push_back("departure '" + input.departure + "' is not a real date");
    if (input.dates.pullForward && input.dates.maxDaysPulledForward < 0) errors.push_back("the pull-forward window is negative");
    set<string> ids;
    for (size_t index = 0; index < input.atoms.size(); ++index) {
        checkAtom(input.atoms[index], index, errors);
        if (!ids.insert(input.atoms[index].id).second) errors.push_back("atom " + input.atoms[index].id + " is offered twice");
    }
    for (const ExcludedSource& source : input.excluded) {
        if (!isfinite(source.unitLoads) || source.unitLoads < 0.0) {
            errors.push_back("excluded source " + source.source + " carries unit loads that are negative or not finite");
        }
    }
    checkPool(input.pool, errors);
    const vector<string> identity = slotIdentityErrors(input);
    errors.insert(errors.end(), identity.begin(), identity.end());
    checkSourcesAgree(input, errors);
    if (!errors.empty()) return build;

    DemandSnapshot snapshot;
    snapshot.input = input;
    snapshot.id = snapshotDigest(input);
    snapshot.eligibility.reserve(input.atoms.size());
    set<GroupId> groups;
    for (const Atom& atom : input.atoms) {
        snapshot.eligibility.push_back(atomEligibility(atom, input.departure, input.dates));
        groups.insert(atom.group);
    }
    snapshot.groups.assign(groups.begin(), groups.end());
    build.snapshot = std::move(snapshot);
    return build;
}

RemainingDemandView remainingAfterSelected(const DemandSnapshot& snapshot, const CandidatePlan& plan) {
    RemainingDemandView view;
    view.sourceVersion = snapshot.id;
    view.selectedRevision = plan.revision;
    view.completeForDeclaredScope = snapshot.input.sourceScopeComplete;
    vector<char> shipped(snapshot.input.atoms.size(), 0);
    for (const TruckPlan& truck : plan.trucks) {
        for (const size_t atom : truck.atoms) shipped[atom] = 1;
    }
    for (size_t index = 0; index < snapshot.input.atoms.size(); ++index) {
        if (shipped[index]) continue;
        const Atom& atom = snapshot.input.atoms[index];
        const Eligibility& eligibility = snapshot.eligibility[index];
        for (const Component& component : atom.components) {
            view.sources.push_back({component.source, atom.group, component.unitLoads, eligibility.eligible,
                                    eligibility.eligible ? "" : eligibilityReasonName(eligibility.reason), index});
        }
    }
    for (const ExcludedSource& source : snapshot.input.excluded) {
        view.sources.push_back({source.source, source.group, source.unitLoads, false, source.reason, nullopt});
    }
    return view;
}

Reconciliation reconcile(const DemandSnapshot& snapshot, const CandidatePlan& plan) {
    Reconciliation result;
    const vector<Atom>& atoms = snapshot.input.atoms;
    vector<int> uses(atoms.size(), 0);
    auto count = [&](size_t atom, const string& where) {
        if (atom >= atoms.size()) {
            result.findings.push_back(where + " names atom " + to_string(atom) + " of " + to_string(atoms.size()));
            return;
        }
        ++uses[atom];
    };
    for (size_t truck = 0; truck < plan.trucks.size(); ++truck) {
        for (const size_t atom : plan.trucks[truck].atoms) count(atom, "truck " + to_string(truck));
    }
    for (const LeftOver& left : plan.leftOver) count(left.atom, "the left-over list");
    for (size_t atom = 0; atom < atoms.size(); ++atom) {
        if (uses[atom] == 0) result.findings.push_back("atom " + atoms[atom].id + " is on no truck and not left over");
        if (uses[atom] > 1) result.findings.push_back("atom " + atoms[atom].id + " is used " + to_string(uses[atom]) + " times");
    }
    // Per source line: what went in equals what is on trucks plus what is left, line by line.
    map<SourceLineId, double> in, out;
    for (const Atom& atom : atoms) {
        for (const Component& component : atom.components) in[component.source] += component.unitLoads;
    }
    auto addOut = [&](size_t atom) {
        if (atom >= atoms.size()) return;
        for (const Component& component : atoms[atom].components) out[component.source] += component.unitLoads;
    };
    for (const TruckPlan& truck : plan.trucks) {
        for (const size_t atom : truck.atoms) addOut(atom);
    }
    for (const LeftOver& left : plan.leftOver) addOut(left.atom);
    for (const auto& [source, unitLoads] : in) {
        const double accounted = out[source];
        if (fabs(accounted - unitLoads) > kUnitLoadSumTolerance * max(1.0, unitLoads)) {
            result.findings.push_back("source line " + source + ": " + to_string(unitLoads) + " unit loads in, "
                                      + to_string(accounted) + " accounted for");
        }
    }
    result.balanced = result.findings.empty();
    return result;
}

const char* eligibilityReasonName(EligibilityReason reason) {
    switch (reason) {
    case EligibilityReason::Ok: return "eligible";
    case EligibilityReason::NotAvailable: return "not available by the departure";
    case EligibilityReason::DueBeyondPullWindow: return "due after the pull-forward window";
    case EligibilityReason::PullForwardOff: return "due later, and pull-forward is off";
    }
    return "unknown";
}

} // namespace m5
} // namespace ob
