#include "m5Commit.hpp"
#include "m5Validation.hpp"

#include <algorithm>
#include <sstream>

using namespace std;

namespace ob {
namespace m5 {

namespace {

string payloadOf(const CandidatePlan& plan, const DemandSnapshot& snapshot, const M5PolicyProfile& profile) {
    ostringstream out;
    out << profile.key() << '|';
    for (const TruckPlan& truck : plan.trucks) {
        out << truck.slot << ':' << truck.group << ':';
        for (const size_t atom : truck.atoms) out << snapshot.input.atoms[atom].id << ',';
        out << '|';
    }
    for (const LeftOver& left : plan.leftOver) out << snapshot.input.atoms[left.atom].id << '=' << static_cast<int>(left.cause) << ',';
    return digestOf(out.str());
}

}  // namespace

optional<CommitDelta> buildDelta(const CandidatePlan& selected, const DemandSnapshot& snapshot, const M5PolicyProfile& profile,
                                 string* error) {
    auto refuse = [&](const string& why) -> optional<CommitDelta> {
        if (error) *error = why;
        return nullopt;
    };
    if (!selected.validation) return refuse("the selected plan was never validated");
    if (!validationCurrent(selected, snapshot, profile)) {
        return refuse("the selected plan's validation is stale: its revision, profile, snapshot or pool changed since");
    }
    if (!selected.validation->allPassed()) return refuse("the selected plan failed validation");
    CommitDelta delta;
    delta.operationKey = digestOf(snapshot.input.lane.text() + "|" + snapshot.input.departure + "|" + snapshot.id);
    delta.expectedSnapshot = snapshot.id;
    delta.expectedReservationVersion = snapshot.input.pool.version;
    delta.payloadHash = payloadOf(selected, snapshot, profile);
    delta.trucks = selected.trucks;
    delta.leftOver = selected.leftOver;
    delta.reservationsUsed = selected.trucks.size();
    return delta;
}

ApplyStatus CommitLedger::applyIfCurrent(const CommitDelta& delta, const DemandSnapshot& snapshot, OperationalState& state) {
    lock_guard<mutex> lock(mutex_);
    const auto known = payloadByOperation_.find(delta.operationKey);
    if (known != payloadByOperation_.end()) {
        return known->second == delta.payloadHash ? ApplyStatus::AlreadyApplied : ApplyStatus::PayloadConflict;
    }
    if (state.snapshot != delta.expectedSnapshot || state.reservationVersion != delta.expectedReservationVersion) {
        return ApplyStatus::StaleSnapshot;
    }
    // Checked in full before anything changes, so a refusal leaves the state as it was.
    vector<string> consumed;
    for (const TruckPlan& truck : delta.trucks) {
        for (const size_t atom : truck.atoms) {
            const string& id = snapshot.input.atoms.at(atom).id;
            if (state.consumedAtoms.count(id)) return ApplyStatus::StaleSnapshot;
            consumed.push_back(id);
        }
    }
    if (state.slotsUsed + delta.reservationsUsed > state.slotsReserved) return ApplyStatus::StaleSnapshot;
    state.consumedAtoms.insert(consumed.begin(), consumed.end());
    state.slotsUsed += delta.reservationsUsed;
    ++state.reservationVersion;
    state.snapshot = delta.expectedSnapshot + "+" + delta.operationKey;
    payloadByOperation_.emplace(delta.operationKey, delta.payloadHash);
    return ApplyStatus::Applied;
}

DemandSnapshot residualAfter(const DemandSnapshot& original, const CommitDelta& delta) {
    vector<char> committed(original.input.atoms.size(), 0);
    for (const TruckPlan& truck : delta.trucks) {
        for (const size_t atom : truck.atoms) committed[atom] = 1;
    }
    M5LaneDateInput residual = original.input;
    residual.atoms.clear();
    for (size_t atom = 0; atom < original.input.atoms.size(); ++atom) {
        if (!committed[atom]) residual.atoms.push_back(original.input.atoms[atom]);
    }
    const size_t slotsLeft = original.input.pool.slots.size() - min(original.input.pool.slots.size(), delta.reservationsUsed);
    residual.pool.slots.resize(slotsLeft);
    ++residual.pool.version;
    residual.sourceVersion = original.id + "+" + delta.operationKey;
    // The original passed every check, and removing committed atoms cannot make it fail one.
    return *buildSnapshot(residual).snapshot;
}

const char* applyStatusName(ApplyStatus status) {
    switch (status) {
    case ApplyStatus::Applied: return "applied";
    case ApplyStatus::AlreadyApplied: return "already applied";
    case ApplyStatus::StaleSnapshot: return "stale snapshot";
    case ApplyStatus::PayloadConflict: return "payload conflict";
    }
    return "unknown";
}

} // namespace m5
} // namespace ob
