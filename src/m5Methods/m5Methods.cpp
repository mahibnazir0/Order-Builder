#include "m5Methods.hpp"
#include "m5Repair.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <stdexcept>

using namespace std;

namespace ob {
namespace m5 {

namespace {

constexpr long long kMaxCount = 1LL << 62;
// Above any truck's floor-position count, so the trailer, not this cap, bounds a pattern.
constexpr int kMostCopiesPerTruck = 1 << 20;

const TrailerCapability& capabilityOf(const MethodContext& context) { return context.snapshot.input.pool.slots.front(); }

// The trucks ordered best first under the comparator, each judged alone.
GroupTrucks bestFirst(const MethodContext& context, const GroupTrucks& trucks) {
    vector<size_t> order(trucks.size());
    iota(order.begin(), order.end(), size_t{0});
    vector<AllocationKey> keys;
    keys.reserve(trucks.size());
    for (const Truck& truck : trucks) keys.push_back(keyOf(context.snapshot, context.domain, {truck}));
    stable_sort(order.begin(), order.end(), [&](size_t left, size_t right) { return context.comparator.better(keys[left], keys[right]); });
    GroupTrucks ordered;
    ordered.reserve(trucks.size());
    for (const size_t position : order) ordered.push_back(trucks[position]);
    return ordered;
}

GroupTrucks firstTrucks(const GroupTrucks& trucks, size_t count) {
    return GroupTrucks(trucks.begin(), trucks.begin() + static_cast<ptrdiff_t>(min(count, trucks.size())));
}

// Floor area, payload and unit loads of goods, for the L1 bound on the trucks they need: no
// packing of them uses fewer (Section 8.2).
struct VolumeSum {
    double areaSqIn = 0.0;
    double weightLb = 0.0;
    double unitLoads = 0.0;

    void add(const Atom& atom) {
        areaSqIn += atom.floor.longSideIn * atom.floor.shortSideIn;
        weightLb += atom.weightLb;
        unitLoads += atom.unitLoads;
    }
    long long trucksAtLeast(const TrailerCapability& capability, const PhysicalSettings& physical) const {
        double trucks = max(areaSqIn / (capability.lengthIn * capability.widthIn), weightLb / capability.maxWeightLb);
        if (physical.maxUnitLoadsPerTruck) trucks = max(trucks, unitLoads / *physical.maxUnitLoadsPerTruck);
        return static_cast<long long>(ceil(trucks - 1e-9));
    }
};

}  // namespace

long long trucksAtLeast(const M5LaneDateInput& input, const vector<size_t>& atoms, const TrailerCapability& capability,
                        const PhysicalSettings& physical) {
    VolumeSum sum;
    for (const size_t atom : atoms) sum.add(input.atoms[atom]);
    return sum.trucksAtLeast(capability, physical);
}

vector<size_t> rankedCandidates(const MethodContext& context, const GroupId& group) {
    const vector<Atom>& atoms = context.snapshot.input.atoms;
    vector<size_t> ranked;
    vector<size_t> rankOf(atoms.size(), 0);
    for (size_t index = 0; index < atoms.size(); ++index) {
        const Atom& atom = atoms[index];
        if (atom.group != group || !context.snapshot.eligibility[index].eligible || context.unfittable[index]) continue;
        if (!context.profile.canGoFill
            && any_of(atom.components.begin(), atom.components.end(),
                      [](const Component& component) { return component.offerKind == OfferKind::CanGo; })) {
            continue;
        }
        rankOf[index] = context.domain.atomRank(atom);
        ranked.push_back(index);
    }
    stable_sort(ranked.begin(), ranked.end(), [&](size_t left, size_t right) { return rankOf[left] < rankOf[right]; });
    return ranked;
}

long long patternCopies(long long stock, long long patternSize, long long slots) {
    if (stock < 0 || patternSize < 0 || slots < 0) throw invalid_argument("pattern copies: a negative count");
    if (stock > kMaxCount || patternSize > kMaxCount || slots > kMaxCount) {
        throw out_of_range("pattern copies: a count past 2^62");
    }
    if (patternSize == 0 || stock == 0) return 0;
    return min(stock / patternSize, slots);
}

GroupSupply supplyFromPlans(const MethodContext& context, const GroupId& group, const GroupTrucks& full,
                            const GroupTrucks& alternative) {
    GroupSupply supply;
    supply.group = group;
    supply.plans.push_back({});
    const GroupTrucks fullBestFirst = bestFirst(context, full);
    const size_t most = min(context.maxTrucks, max(full.size(), alternative.size()));
    for (size_t trucks = 1; trucks <= most; ++trucks) {
        GroupTrucks fromFull = firstTrucks(fullBestFirst, trucks);
        GroupTrucks fromAlternative = firstTrucks(alternative, trucks);
        rebalanceForAcceptance(fromFull, context.snapshot, context.profile, context.physical, capabilityOf(context), context.stop);
        rebalanceForAcceptance(fromAlternative, context.snapshot, context.profile, context.physical, capabilityOf(context),
                               context.stop);
        const bool alternativeBetter = context.comparator.better(keyOf(context.snapshot, context.domain, fromAlternative),
                                                                 keyOf(context.snapshot, context.domain, fromFull));
        supply.plans.push_back(alternativeBetter ? std::move(fromAlternative) : std::move(fromFull));
    }
    return supply;
}

GroupSupply PatternPlanner::supplyForGroup(const MethodContext& context, const GroupId& group) const {
    const M5LaneDateInput& input = context.snapshot.input;
    const TrailerCapability& capability = capabilityOf(context);
    const vector<size_t> ranked = rankedCandidates(context, group);
    const vector<size_t> classOf = physicalClassOf(input);

    // Classes in the order their most urgent good ranks; each class's goods in rank order.
    vector<size_t> classOrder;
    map<size_t, vector<size_t>> goodsOf;
    for (const size_t index : ranked) {
        vector<size_t>& goods = goodsOf[classOf[index]];
        if (goods.empty()) classOrder.push_back(classOf[index]);
        goods.push_back(index);
    }
    GroupTrucks full;
    vector<char> placed(input.atoms.size(), 0);
    size_t slotsFree = context.maxTrucks;
    for (const size_t physicalClass : classOrder) {
        if (slotsFree == 0 || (context.stop && context.stop())) break;
        const vector<size_t>& goods = goodsOf[physicalClass];
        const string key = to_string(physicalClass) + "@" + capability.signature();
        int copiesPerTruck = 0;
        if (const optional<int> known = context.cache.find(key)) {
            copiesPerTruck = *known;
        } else {
            // Sized on the trailer alone, never on this group's stock, so the memo holds for any group.
            copiesPerTruck = bestSingleClassLoad(input, goods.front(), capability, context.physical, kMostCopiesPerTruck);
            context.cache.put(key, copiesPerTruck);
        }
        // Only whole trucks of the class form a pattern; the rest is remainder (U-PB-02, U-PB-03).
        if (copiesPerTruck == 0 || static_cast<size_t>(copiesPerTruck) > goods.size()) continue;
        const long long copies = patternCopies(static_cast<long long>(goods.size()), copiesPerTruck,
                                               static_cast<long long>(slotsFree));
        for (long long copy = 0; copy < copies; ++copy) {
            Truck truck(goods.begin() + copy * copiesPerTruck, goods.begin() + (copy + 1) * copiesPerTruck);
            sort(truck.begin(), truck.end());
            for (const size_t index : truck) placed[index] = 1;
            full.push_back(std::move(truck));
        }
        slotsFree -= static_cast<size_t>(copies);
    }
    // Room a pattern truck leaves takes the remainder first, then the remainder opens trucks.
    for (Truck& truck : full) fillTruck(input, classOf, truck, ranked, placed, capability, context.physical, context.stop);
    vector<size_t> remainder;
    for (const size_t index : ranked) {
        if (!placed[index]) remainder.push_back(index);
    }
    for (Truck& truck : packByUrgency(input, remainder, capability, context.physical, slotsFree, context.stop)) {
        full.push_back(std::move(truck));
    }
    const GroupTrucks byUrgency = packByUrgency(input, ranked, capability, context.physical, context.maxTrucks, context.stop);
    return supplyFromPlans(context, group, full, byUrgency);
}

GroupSupply BlockPlanner::supplyForGroup(const MethodContext& context, const GroupId& group) const {
    const M5LaneDateInput& input = context.snapshot.input;
    const TrailerCapability& capability = capabilityOf(context);
    vector<size_t> queue = rankedCandidates(context, group);
    GroupTrucks trucks;
    while (!queue.empty() && trucks.size() < context.maxTrucks && !(context.stop && context.stop())) {
        const size_t blockTrucks = min(static_cast<size_t>(context.profile.blockSize), context.maxTrucks - trucks.size());
        // The longest prefix of the queue whose bound fits the block: at least one good, so a
        // block always has something to try.
        VolumeSum prefix;
        prefix.add(input.atoms[queue.front()]);
        size_t take = 1;
        while (take < queue.size()) {
            VolumeSum grown = prefix;
            grown.add(input.atoms[queue[take]]);
            if (grown.trucksAtLeast(capability, context.physical) > static_cast<long long>(blockTrucks)) break;
            prefix = grown;
            ++take;
        }
        const vector<size_t> goods(queue.begin(), queue.begin() + static_cast<ptrdiff_t>(take));
        const GroupTrucks block = packCarefully(input, goods, capability, context.physical, blockTrucks, context.stop);
        // No truck from a block is the end (X-RUN-06): the goods it was given fit no truck, and
        // retrying them would only repeat the same state.
        if (block.empty()) break;
        vector<char> shipped(input.atoms.size(), 0);
        for (const Truck& truck : block) {
            for (const size_t index : truck) shipped[index] = 1;
        }
        const size_t before = queue.size();
        queue.erase(remove_if(queue.begin(), queue.end(), [&](size_t index) { return shipped[index] != 0; }), queue.end());
        trucks.insert(trucks.end(), block.begin(), block.end());
        if (queue.size() == before) break;
    }
    return supplyFromPlans(context, group, trucks, trucks);
}

} // namespace m5
} // namespace ob
