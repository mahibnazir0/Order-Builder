#include "m5Allocation.hpp"
#include "demand_types.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

using namespace std;

namespace ob {
namespace m5 {

bool UrgencyClass::operator<(const UrgencyClass& other) const {
    return tie(rankDate, priority, shipDate) < tie(other.rankDate, other.priority, other.shipDate);
}

bool UrgencyClass::operator==(const UrgencyClass& other) const {
    return rankDate == other.rankDate && priority == other.priority && shipDate == other.shipDate;
}

UrgencyDomain::UrgencyDomain(const DemandSnapshot& snapshot)
    : departure_(snapshot.input.departure), order_(snapshot.input.pastDueOrder) {
    for (const Atom& atom : snapshot.input.atoms) {
        for (const Component& component : atom.components) classes_.push_back(classOf(component));
    }
    sort(classes_.begin(), classes_.end());
    classes_.erase(unique(classes_.begin(), classes_.end()), classes_.end());
}

UrgencyClass UrgencyDomain::classOf(const Component& component) const {
    return {rankDateOf(component.dueDate, departure_, order_), component.priority, component.dueDate};
}

size_t UrgencyDomain::indexOf(const Component& component) const {
    return static_cast<size_t>(lower_bound(classes_.begin(), classes_.end(), classOf(component)) - classes_.begin());
}

size_t UrgencyDomain::atomRank(const Atom& atom) const {
    size_t best = classes_.size();
    for (const Component& component : atom.components) best = min(best, indexOf(component));
    return best;
}

AllocationKey keyOf(const DemandSnapshot& snapshot, const UrgencyDomain& domain, const GroupTrucks& trucks) {
    AllocationKey key;
    key.shippedByClass.assign(domain.size(), 0);
    key.shippedByPriority.assign(static_cast<size_t>(kMaxPriority) + 1, 0);
    vector<vector<string>> members;
    for (const Truck& truck : trucks) {
        if (truck.empty()) continue;
        ++key.loadedTrucks;
        vector<string> sources;
        for (const size_t index : truck) {
            const Atom& atom = snapshot.input.atoms[index];
            for (const Component& component : atom.components) {
                const long long thousandths = llround(component.unitLoads * 1000.0);
                key.shippedByClass[domain.indexOf(component)] += thousandths;
                key.shippedByPriority[static_cast<size_t>(component.priority)] += thousandths;
            }
            sources.push_back(atom.id);
        }
        sort(sources.begin(), sources.end());
        members.push_back(std::move(sources));
    }
    sort(members.begin(), members.end());
    key.membership.push_back(std::move(members));
    return key;
}

void addTo(AllocationKey& total, const AllocationKey& part) {
    if (total.shippedByClass.empty()) total.shippedByClass.assign(part.shippedByClass.size(), 0);
    if (total.shippedByPriority.empty()) total.shippedByPriority.assign(part.shippedByPriority.size(), 0);
    for (size_t index = 0; index < part.shippedByClass.size(); ++index) total.shippedByClass[index] += part.shippedByClass[index];
    for (size_t index = 0; index < part.shippedByPriority.size(); ++index) {
        total.shippedByPriority[index] += part.shippedByPriority[index];
    }
    total.loadedTrucks += part.loadedTrucks;
    total.membership.insert(total.membership.end(), part.membership.begin(), part.membership.end());
}

namespace {

class M4UrgencyComparator final : public AllocationComparator {
public:
    bool better(const AllocationKey& left, const AllocationKey& right) const override {
        if (left.shippedByClass != right.shippedByClass) return left.shippedByClass > right.shippedByClass;
        return left.membership < right.membership;
    }
    string id() const override { return "ED-02 urgency classes (rank date, priority, ship date), then canonical membership"; }
};

class FxPriorityComparator final : public AllocationComparator {
public:
    bool better(const AllocationKey& left, const AllocationKey& right) const override {
        if (left.shippedByPriority != right.shippedByPriority) return left.shippedByPriority > right.shippedByPriority;
        if (left.loadedTrucks != right.loadedTrucks) return left.loadedTrucks < right.loadedTrucks;
        return left.membership < right.membership;
    }
    string id() const override { return "FX-PRIORITY (synthetic): priority vector, fewer trucks, membership"; }
};

}  // namespace

unique_ptr<AllocationComparator> makeComparator(ComparatorKind kind) {
    if (kind == ComparatorKind::FxPriority) return make_unique<FxPriorityComparator>();
    return make_unique<M4UrgencyComparator>();
}

AllocationResult allocate(const DemandSnapshot& snapshot, const UrgencyDomain& domain, const vector<GroupSupply>& supplies,
                          size_t slots, const AllocationComparator& comparator) {
    struct State {
        AllocationKey key;
        vector<size_t> planOfGroup;
    };
    // best[s]: the best partial allocation using exactly s slots.
    vector<optional<State>> best(slots + 1);
    best[0] = State{AllocationKey{}, {}};
    best[0]->key.shippedByClass.assign(domain.size(), 0);
    best[0]->key.shippedByPriority.assign(static_cast<size_t>(kMaxPriority) + 1, 0);
    for (const GroupSupply& supply : supplies) {
        vector<AllocationKey> planKeys;
        planKeys.reserve(supply.plans.size());
        for (const GroupTrucks& plan : supply.plans) planKeys.push_back(keyOf(snapshot, domain, plan));
        vector<optional<State>> next(slots + 1);
        for (size_t used = 0; used <= slots; ++used) {
            if (!best[used]) continue;
            for (size_t plan = 0; plan < supply.plans.size(); ++plan) {
                const size_t after = used + static_cast<size_t>(planKeys[plan].loadedTrucks);
                if (after > slots) continue;
                State candidate = *best[used];
                addTo(candidate.key, planKeys[plan]);
                candidate.planOfGroup.push_back(plan);
                if (!next[after] || comparator.better(candidate.key, next[after]->key)) next[after] = std::move(candidate);
            }
        }
        best = std::move(next);
    }
    const State* winner = nullptr;
    for (const optional<State>& state : best) {
        if (state && (!winner || comparator.better(state->key, winner->key))) winner = &*state;
    }
    AllocationResult result;
    result.provenBest = all_of(supplies.begin(), supplies.end(), [](const GroupSupply& supply) { return supply.exact; });
    if (!winner) return result;
    result.key = winner->key;
    for (size_t group = 0; group < supplies.size(); ++group) {
        result.chosen.push_back(supplies[group].plans[winner->planOfGroup[group]]);
    }
    return result;
}

} // namespace m5
} // namespace ob
