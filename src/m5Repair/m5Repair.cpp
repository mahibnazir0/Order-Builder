#include "m5Repair.hpp"

#include <algorithm>
#include <stdexcept>

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
                                      const PhysicalSettings& physical, const TrailerCapability& capability, const StopCheck& stop) {
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
                        if (!checkTruck(snapshot.input, recipientAfter, capability, physical).fits) continue;
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
