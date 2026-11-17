#include "m5Repair.hpp"
#include "m5Validation.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

using namespace std;

namespace ob {
namespace m5 {

CandidateEditor::CandidateEditor(CandidatePlan& plan) : plan_(plan) {}

void CandidateEditor::touch() {
    ++plan_.revision;
    plan_.validation.reset();
    // A changed load is no longer the load any proof or bound was about.
    if (plan_.evidence.proof == ProofStatus::ProvenOptimal) plan_.evidence.proof = ProofStatus::Unproven;
}

void CandidateEditor::moveAtom(size_t fromTruck, size_t toTruck, size_t atom) {
    Truck& from = plan_.trucks.at(fromTruck).atoms;
    Truck& to = plan_.trucks.at(toTruck).atoms;
    const auto found = find(from.begin(), from.end(), atom);
    if (found == from.end()) throw invalid_argument("CandidateEditor: truck " + to_string(fromTruck) + " does not carry atom " + to_string(atom));
    from.erase(found);
    to.insert(upper_bound(to.begin(), to.end(), atom), atom);
    touch();
}

void CandidateEditor::removeToLeftOver(size_t truck, size_t atom, LeftOverCause cause) {
    Truck& atoms = plan_.trucks.at(truck).atoms;
    const auto found = find(atoms.begin(), atoms.end(), atom);
    if (found == atoms.end()) throw invalid_argument("CandidateEditor: truck " + to_string(truck) + " does not carry atom " + to_string(atom));
    atoms.erase(found);
    plan_.leftOver.push_back({atom, cause});
    touch();
}

void CandidateEditor::removeTruck(size_t truck, LeftOverCause cause) {
    const TruckPlan removed = plan_.trucks.at(truck);
    plan_.trucks.erase(plan_.trucks.begin() + static_cast<ptrdiff_t>(truck));
    for (const size_t atom : removed.atoms) plan_.leftOver.push_back({atom, cause});
    touch();
}

namespace {

// Trucks as (group, atoms), sorted: equal arrangements whatever their truck order or revision.
vector<pair<GroupId, Truck>> arrangementOf(const CandidatePlan& plan) {
    vector<pair<GroupId, Truck>> trucks;
    trucks.reserve(plan.trucks.size());
    for (const TruckPlan& truck : plan.trucks) trucks.emplace_back(truck.group, truck.atoms);
    sort(trucks.begin(), trucks.end());
    return trucks;
}

}  // namespace

RepairRun runRepair(const CandidatePlan& start, const DemandSnapshot& snapshot, const M5PolicyProfile& profile,
                    const PhysicalSettings& physical, const AllocationComparator& comparator, const UrgencyDomain& domain,
                    const RepairProposal& propose, size_t maxIterations, const StopCheck& stop) {
    RepairRun run;
    const bool startChecked = start.validation && start.validation->revision == start.revision && start.validation->allPassed();
    if (startChecked) run.incumbent = start;
    set<vector<pair<GroupId, Truck>>> seen{arrangementOf(start)};
    run.stopReason = "iteration cap";
    for (size_t iteration = 0; iteration < maxIterations; ++iteration) {
        if (stop && stop()) {
            run.stopReason = "stopped";
            return run;
        }
        CandidatePlan work = run.incumbent ? *run.incumbent : start;
        if (!propose(work, iteration)) {
            run.stopReason = "no proposal";
            return run;
        }
        ++run.iterations;
        if (!seen.insert(arrangementOf(work)).second) {
            run.stopReason = "repeated arrangement";
            return run;
        }
        // Stopped between the edit and its check: the unchecked copy never stands.
        if (stop && stop()) {
            run.stopReason = "stopped";
            return run;
        }
        // A moved good makes the stored floor no witness of its truck any more.
        for (TruckPlan& truck : work.trucks) {
            if (truck.slot < snapshot.input.pool.slots.size()) {
                truck.layout = checkTruck(snapshot.input, truck.atoms, snapshot.input.pool.slots[truck.slot], physical).layout;
            }
        }
        work.validation = validateFinal(work, snapshot, profile, physical, stop);
        run.checkedRevisions.push_back(work.revision);
        if (!work.validation->allPassed()) continue;
        if (!run.incumbent || comparator.better(keyOfPlan(snapshot, domain, work), keyOfPlan(snapshot, domain, *run.incumbent))) {
            run.incumbent = std::move(work);
        }
    }
    return run;
}

namespace {

bool accepted(const DemandSnapshot& snapshot, const Truck& truck, const NumericAcceptance& rule) {
    double weight = 0.0, cube = 0.0;
    for (const size_t atom : truck) {
        weight += snapshot.input.atoms[atom].weightLb;
        cube += snapshot.input.atoms[atom].floor.cubeCubicIn;
    }
    return evaluateAcceptance(weight, cube, rule).accepted;
}

}  // namespace

vector<size_t> rebalanceForAcceptance(GroupTrucks& trucks, const DemandSnapshot& snapshot, const M5PolicyProfile& profile,
                                      const PhysicalSettings& physical, const vector<TrailerCapability>& slots, const StopCheck& stop) {
    vector<size_t> withheld;
    if (!profile.acceptance) return withheld;
    const NumericAcceptance& rule = *profile.acceptance;
    for (int round = 0; round < profile.repair.maxRounds; ++round) {
        bool anyFailing = false, anyLifted = false;
        for (size_t recipient = 0; recipient < trucks.size(); ++recipient) {
            if (accepted(snapshot, trucks[recipient], rule)) continue;
            anyFailing = true;
            // On a copy: kept only if the recipient ends accepted, so a truck that cannot be lifted
            // costs its donors nothing.
            GroupTrucks work = trucks;
            int moves = 0;
            while (!accepted(snapshot, work[recipient], rule) && moves < profile.repair.maxMovesPerTruck) {
                if (stop && stop()) return withheld;
                bool moved = false;
                for (size_t donor = 0; donor < work.size() && !moved; ++donor) {
                    if (donor == recipient || !accepted(snapshot, work[donor], rule)) continue;
                    for (auto atom = work[donor].rbegin(); atom != work[donor].rend() && !moved; ++atom) {
                        Truck donorAfter = work[donor];
                        donorAfter.erase(find(donorAfter.begin(), donorAfter.end(), *atom));
                        if (donorAfter.empty() || !accepted(snapshot, donorAfter, rule)) continue;
                        Truck recipientAfter = work[recipient];
                        recipientAfter.insert(upper_bound(recipientAfter.begin(), recipientAfter.end(), *atom), *atom);
                        if (!checkTruckInRoute(snapshot.input, recipientAfter, slots.at(recipient), physical,
                                               physical.floor.searchStateLimit)
                                 .fits) {
                            continue;
                        }
                        work[donor] = std::move(donorAfter);
                        work[recipient] = std::move(recipientAfter);
                        moved = true;
                    }
                }
                if (!moved) break;
                ++moves;
            }
            if (accepted(snapshot, work[recipient], rule)) {
                trucks = std::move(work);
                anyLifted = true;
            }
        }
        if (!anyFailing || !anyLifted) break;
    }
    GroupTrucks kept;
    for (Truck& truck : trucks) {
        if (accepted(snapshot, truck, rule)) {
            kept.push_back(std::move(truck));
        } else {
            withheld.insert(withheld.end(), truck.begin(), truck.end());
        }
    }
    trucks = std::move(kept);
    sort(withheld.begin(), withheld.end());
    return withheld;
}

} // namespace m5
} // namespace ob
