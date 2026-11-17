#include "m5Physical.hpp"
#include "m5UnitLoads.hpp"
#include "truckCheck.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

using namespace std;

namespace ob {
namespace m5 {

namespace {

struct LoadTotals {
    double weightLb = 0.0;
    std::int64_t unitLoadHundredths = 0;  // checked; see m5UnitLoads.hpp
    bool unitLoadsOverflowed = false;
    double areaSqIn = 0.0;
};

void addUnitLoads(LoadTotals& totals, const Atom& atom) {
    const optional<int64_t> atomLoads = hundredthsOfUnitLoads(atom.unitLoads);
    const optional<int64_t> grown = atomLoads ? addCounts(totals.unitLoadHundredths, *atomLoads) : nullopt;
    if (grown) {
        totals.unitLoadHundredths = *grown;
    } else {
        totals.unitLoadsOverflowed = true;
    }
}

LoadTotals totalsOf(const M5LaneDateInput& input, const vector<size_t>& atoms) {
    LoadTotals totals;
    for (const size_t index : atoms) {
        const Atom& atom = input.atoms[index];
        totals.weightLb += atom.weightLb;
        addUnitLoads(totals, atom);
        totals.areaSqIn += atom.floor.longSideIn * atom.floor.shortSideIn;
    }
    return totals;
}

// Necessary conditions only, so a refusal here is one checkTruck would make too.
bool mayFit(const Atom& atom, const LoadTotals& totals, const TrailerCapability& capability) {
    if (atom.floor.heightIn > capability.heightIn) return false;
    if (totals.weightLb + atom.weightLb > capability.maxWeightLb) return false;
    LoadTotals grown = totals;
    addUnitLoads(grown, atom);
    if (grown.unitLoadsOverflowed || grown.unitLoadHundredths > capInHundredths(capability.maxUnitLoads)) return false;
    return totals.areaSqIn + atom.floor.longSideIn * atom.floor.shortSideIn <= capability.lengthIn * capability.widthIn;
}

vector<size_t> withAtom(const vector<size_t>& truck, size_t atom) {
    vector<size_t> grown = truck;
    grown.insert(upper_bound(grown.begin(), grown.end(), atom), atom);
    return grown;
}

}  // namespace

string classSignatureOf(const Atom& atom) {
    ostringstream out;
    out << hexfloat << atom.floor.longSideIn << ',' << atom.floor.shortSideIn << ',' << atom.floor.heightIn << ','
        << atom.floor.levels << ',' << atom.weightLb << ',' << atom.unitLoads;
    return out.str();
}

TrailerSpec trailerOf(const TrailerCapability& capability) {
    TrailerSpec trailer;
    trailer.trailerCode = "M5 slot " + capability.signature();
    trailer.interiorLengthIn = capability.lengthIn;
    trailer.interiorWidthIn = capability.widthIn;
    trailer.stackHeightCeilingIn = capability.heightIn;
    trailer.weightLimitLb = capability.maxWeightLb;
    return trailer;
}

vector<size_t> slotsBestFirst(const ReservationPool& pool) {
    vector<size_t> order(pool.slots.size());
    for (size_t slot = 0; slot < order.size(); ++slot) order[slot] = slot;
    stable_sort(order.begin(), order.end(),
                [&](size_t left, size_t right) { return pool.slots[left].maxUnitLoads > pool.slots[right].maxUnitLoads; });
    return order;
}

bool slotsAreAlike(const ReservationPool& pool) {
    return all_of(pool.slots.begin(), pool.slots.end(),
                  [&](const TrailerCapability& slot) { return slot.signature() == pool.slots.front().signature(); });
}

bool slotsDifferOnlyInCap(const ReservationPool& pool) {
    for (const TrailerCapability& slot : pool.slots) {
        const TrailerCapability& first = pool.slots.front();
        if (slot.lengthIn != first.lengthIn || slot.widthIn != first.widthIn || slot.heightIn != first.heightIn
            || slot.maxWeightLb != first.maxWeightLb) {
            return false;
        }
    }
    return true;
}

PhysicalCheck checkTruck(const M5LaneDateInput& input, const vector<size_t>& atoms, const TrailerCapability& capability,
                         const PhysicalSettings& settings, FloorSearchBudget* budget) {
    PhysicalCheck check;
    // The cap is judged on the components' quantities in whole hundredths, summed with checked
    // arithmetic and compared exactly; the cached per-atom figure is never trusted for a final answer
    // (ED-20, ED-ARITH).
    if (capability.maxUnitLoads < 1) {
        check.findings.push_back("the slot has no unit-load cap: a whole number from 1 is required");
        return check;
    }
    const optional<int64_t> unitLoads = unitLoadHundredthsOfAtoms(input, atoms);
    if (!unitLoads) {
        check.findings.push_back("the load's unit loads are not multiples of 0.01, or their sum overflows a 64-bit count");
        return check;
    }
    if (*unitLoads > capInHundredths(capability.maxUnitLoads)) {
        check.findings.push_back("load holds " + unitLoadsText(*unitLoads) + " unit loads, over the cap of " + to_string(capability.maxUnitLoads));
        return check;
    }
    vector<FloorItem> floorItems;
    floorItems.reserve(atoms.size());
    for (const size_t index : atoms) floorItems.push_back(input.atoms[index].floor);
    const LoadTotals totals = totalsOf(input, atoms);
    TruckLoadCheck load = checkTruckLoad(floorItems, totals.weightLb, 0.0, trailerOf(capability), settings.floor, nullopt, budget);
    check.fits = load.verdict == TruckFit::Fits;
    check.undecided = load.verdict == TruckFit::Undecided;
    check.findings = std::move(load.findings);
    check.layout = std::move(load.layout);
    return check;
}

double rearInchesOf(const FloorLayout& layout, const TrailerCapability& capability) {
    double extent = 0.0;
    for (const FloorRow& row : layout.rows) extent += row.depthIn;
    return capability.lengthIn - extent;
}

WitnessMeasure measureSuppliedWitness(const vector<PlacedRectangle>& placed, const TrailerCapability& capability) {
    WitnessMeasure measure;
    for (size_t index = 0; index < placed.size(); ++index) {
        const PlacedRectangle& box = placed[index];
        const string name = "rectangle " + to_string(index);
        if (!isfinite(box.xIn) || !isfinite(box.yIn) || !isfinite(box.lengthIn) || !isfinite(box.widthIn) || box.lengthIn <= 0.0
            || box.widthIn <= 0.0) {
            measure.findings.push_back(name + " has a coordinate or size that is not finite and positive");
            continue;
        }
        if (box.xIn < 0.0 || box.yIn < 0.0 || box.xIn + box.lengthIn > capability.lengthIn || box.yIn + box.widthIn > capability.widthIn) {
            measure.findings.push_back(name + " lies outside the trailer floor");
        }
        for (size_t other = 0; other < index; ++other) {
            const PlacedRectangle& next = placed[other];
            const bool apart = box.xIn + box.lengthIn <= next.xIn || next.xIn + next.lengthIn <= box.xIn
                               || box.yIn + box.widthIn <= next.yIn || next.yIn + next.widthIn <= box.yIn;
            if (!apart) measure.findings.push_back(name + " overlaps rectangle " + to_string(other));
        }
        measure.occupiedExtentIn = max(measure.occupiedExtentIn, box.xIn + box.lengthIn);
    }
    measure.valid = measure.findings.empty();
    measure.rearInches = capability.lengthIn - measure.occupiedExtentIn;
    return measure;
}

vector<size_t> physicalClassOf(const M5LaneDateInput& input) {
    map<string, size_t> classes;
    vector<size_t> classOf;
    classOf.reserve(input.atoms.size());
    for (const Atom& atom : input.atoms) {
        classOf.push_back(classes.try_emplace(classSignatureOf(atom), classes.size()).first->second);
    }
    return classOf;
}

int bestSingleClassLoad(const M5LaneDateInput& input, size_t prototype, const TrailerCapability& capability,
                        const PhysicalSettings& settings, int available) {
    const Atom& atom = input.atoms[prototype];
    if (available <= 0 || atom.floor.heightIn > capability.heightIn) return 0;
    // Bounds no load can pass, so the search below starts at the most that could fit.
    double most = available;
    if (atom.weightLb > 0.0) most = min(most, floor(capability.maxWeightLb / atom.weightLb));
    const optional<int64_t> copyLoads = unitLoadHundredthsOfAtom(atom);
    if (!copyLoads || capability.maxUnitLoads < 1 || *copyLoads > capInHundredths(capability.maxUnitLoads)) return 0;
    if (*copyLoads > 0) most = min(most, static_cast<double>(capInHundredths(capability.maxUnitLoads) / *copyLoads));
    most = min(most, floor(capability.lengthIn * capability.widthIn / (atom.floor.longSideIn * atom.floor.shortSideIn)));
    if (settings.floor.maxPositions) most = min(most, static_cast<double>(*settings.floor.maxPositions));
    const TrailerSpec trailer = trailerOf(capability);
    for (int copies = static_cast<int>(most); copies > 0; --copies) {
        const vector<FloorItem> items(static_cast<size_t>(copies), atom.floor);
        if (floorFits(arrangeFloor(items, trailer, settings.floor))) return copies;
    }
    return 0;
}

optional<int> GeometryCache::find(const string& key) const {
    lock_guard<mutex> lock(mutex_);
    const auto found = copiesByKey_.find(key);
    if (found == copiesByKey_.end()) return nullopt;
    return found->second;
}

void GeometryCache::put(const string& key, int copies) {
    lock_guard<mutex> lock(mutex_);
    copiesByKey_[key] = copies;
}

void fillTruck(const M5LaneDateInput& input, const vector<size_t>& classOf, vector<size_t>& truck, const vector<size_t>& ranked,
               vector<char>& placed, const TrailerCapability& capability, const PhysicalSettings& settings, const StopCheck& stop) {
    LoadTotals totals = totalsOf(input, truck);
    set<size_t> refusedClasses;
    for (const size_t candidate : ranked) {
        if (stop && stop()) return;
        if (placed[candidate] || refusedClasses.count(classOf[candidate])) continue;
        const Atom& atom = input.atoms[candidate];
        if (!mayFit(atom, totals, capability)) {
            refusedClasses.insert(classOf[candidate]);
            continue;
        }
        vector<size_t> grown = withAtom(truck, candidate);
        FloorSearchBudget budget{settings.searchStatesPerPackingCheck};
        if (!checkTruck(input, grown, capability, settings, &budget).fits) {
            refusedClasses.insert(classOf[candidate]);
            continue;
        }
        truck = std::move(grown);
        placed[candidate] = 1;
        totals.weightLb += atom.weightLb;
        addUnitLoads(totals, atom);
        totals.areaSqIn += atom.floor.longSideIn * atom.floor.shortSideIn;
    }
}

vector<vector<size_t>> packByUrgency(const M5LaneDateInput& input, const vector<size_t>& ranked,
                                     const vector<TrailerCapability>& slots, const PhysicalSettings& settings, const StopCheck& stop) {
    const vector<size_t> classOf = physicalClassOf(input);
    vector<char> placed(input.atoms.size(), 0);
    vector<vector<size_t>> trucks;
    while (trucks.size() < slots.size() && !(stop && stop())) {
        vector<size_t> truck;
        fillTruck(input, classOf, truck, ranked, placed, slots[trucks.size()], settings, stop);
        if (truck.empty()) break;
        trucks.push_back(std::move(truck));
    }
    return trucks;
}

KnapsackItem knapsackItemOf(const M5LaneDateInput& input, size_t index) {
    const Atom& atom = input.atoms[index];
    KnapsackItem item;
    item.areaSqIn = atom.floor.longSideIn * atom.floor.shortSideIn;
    item.weightLb = atom.weightLb;
    item.heightIn = atom.floor.heightIn;
    item.unitLoads = atom.unitLoads;
    item.longSideIn = atom.floor.longSideIn;
    item.shortSideIn = atom.floor.shortSideIn;
    item.levels = atom.floor.levels;
    auto rankOf = [&](const Component& component) {
        return make_tuple(rankDateOf(component.dueDate, input.departure, input.pastDueOrder), component.priority, component.dueDate);
    };
    const auto urgent = min_element(atom.components.begin(), atom.components.end(),
                                    [&](const Component& a, const Component& b) { return rankOf(a) < rankOf(b); });
    item.shipDate = urgent->dueDate;
    item.rankDate = rankDateOf(urgent->dueDate, input.departure, input.pastDueOrder);
    item.priority = urgent->priority;
    item.productNumber = urgent->product;
    return item;
}

vector<vector<size_t>> packByKnapsack(const M5LaneDateInput& input, const vector<size_t>& ranked,
                                      const vector<TrailerCapability>& slots, const PhysicalSettings& settings) {
    if (ranked.empty() || slots.empty()) return {};
    vector<KnapsackItem> items;
    items.reserve(ranked.size());
    for (const size_t index : ranked) items.push_back(knapsackItemOf(input, index));
    auto atomsOf = [&](const vector<size_t>& chosen) {
        vector<size_t> atoms;
        atoms.reserve(chosen.size());
        for (const size_t position : chosen) atoms.push_back(ranked[position]);
        sort(atoms.begin(), atoms.end());
        return atoms;
    };
    // The search prunes on the unit-load sum as a double; the final answer is checkTruck's exact count.
    auto limitsOf = [&](const TrailerCapability& capability) {
        TruckLimits limits;
        limits.fits = [&, capability](const vector<size_t>& chosen) {
            const PhysicalCheck check = checkTruck(input, atomsOf(chosen), capability, settings);
            return check.fits ? TruckFit::Fits : check.undecided ? TruckFit::Undecided : TruckFit::DoesNotFit;
        };
        ItemLimit unitLoads{{}, static_cast<double>(capability.maxUnitLoads)};
        for (const KnapsackItem& item : items) unitLoads.perItem.push_back(item.unitLoads);
        limits.limits.push_back(std::move(unitLoads));
        return limits;
    };
    vector<vector<size_t>> trucks;
    try {
        vector<size_t> remaining(items.size());
        for (size_t position = 0; position < remaining.size(); ++position) remaining[position] = position;
        while (trucks.size() < slots.size() && !remaining.empty()) {
            const TrailerCapability& capability = slots[trucks.size()];
            const KnapsackCapacity capacity{capability.lengthIn * capability.widthIn, capability.maxWeightLb, capability.heightIn};
            const KnapsackSelection truck = selectNextTruck(items, remaining, capacity, settings.knapsack, limitsOf(capability));
            if (truck.chosen.empty()) break;
            trucks.push_back(atomsOf(truck.chosen));
            remaining = truck.leftOver;
        }
    } catch (const invalid_argument&) {
        return {};
    }
    return trucks;
}

vector<vector<size_t>> packCarefully(const M5LaneDateInput& input, const vector<size_t>& ranked,
                                     const vector<TrailerCapability>& slots, const PhysicalSettings& settings, const StopCheck& stop) {
    if (ranked.empty() || slots.empty()) return {};
    const TrailerCapability& capability = slots.front();
    const size_t maxTrucks = slots.size();
    const bool oneCap = all_of(slots.begin(), slots.end(), [&](const TrailerCapability& slot) { return slot.signature() == capability.signature(); });
    if (!oneCap) return packByUrgency(input, ranked, slots, settings, stop);
    vector<KnapsackItem> items;
    items.reserve(ranked.size());
    for (const size_t index : ranked) items.push_back(knapsackItemOf(input, index));
    BinPacking packing;
    try {
        const KnapsackCapacity capacity{capability.lengthIn * capability.widthIn, capability.maxWeightLb, capability.heightIn};
        packing = packBins(items, capacity, settings.binPacker, static_cast<int>(maxTrucks));
    } catch (const invalid_argument&) {
        // Goods M4's packer cannot count on its grains (a weightless stack, say) are packed in
        // order instead; the trucks are checked the same way.
        return packByUrgency(input, ranked, slots, settings, stop);
    }

    // Positions in `ranked` order the trucks' atoms most urgent first, so a trim drops the least.
    vector<size_t> rankOf(input.atoms.size(), 0);
    for (size_t position = 0; position < ranked.size(); ++position) rankOf[ranked[position]] = position;
    vector<char> placed(input.atoms.size(), 1);
    for (const size_t index : ranked) placed[index] = 0;
    vector<vector<size_t>> trucks;
    for (const BinTruck& bin : packing.trucks) {
        vector<size_t> truck;
        for (const size_t position : bin.items) truck.push_back(ranked[position]);
        sort(truck.begin(), truck.end(), [&](size_t left, size_t right) { return rankOf[left] < rankOf[right]; });
        while (!truck.empty()) {
            vector<size_t> sorted = truck;
            sort(sorted.begin(), sorted.end());
            FloorSearchBudget budget{settings.searchStatesPerPackingCheck};
            if (checkTruck(input, sorted, capability, settings, &budget).fits) break;
            truck.pop_back();
        }
        sort(truck.begin(), truck.end());
        for (const size_t index : truck) placed[index] = 1;
        if (!truck.empty()) trucks.push_back(std::move(truck));
    }
    const vector<size_t> classOf = physicalClassOf(input);
    for (vector<size_t>& truck : trucks) fillTruck(input, classOf, truck, ranked, placed, capability, settings, stop);
    while (trucks.size() < maxTrucks && !(stop && stop())) {
        vector<size_t> truck;
        fillTruck(input, classOf, truck, ranked, placed, capability, settings, stop);
        if (truck.empty()) break;
        trucks.push_back(std::move(truck));
    }
    return trucks;
}

} // namespace m5
} // namespace ob
