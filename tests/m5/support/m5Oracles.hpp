#pragma once
// Independent checks for M5 results. They recount from the input and the plan without calling M5's
// own validation, ledger or solver, so a defect there cannot pass its own test.

#include "floorPattern.hpp"
#include "m5Orchestrator.hpp"
#include "m5Solver.hpp"
#include "m5Types.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace m5Oracles {

// Fewest bins for whole-number sizes, by enumerating every partition (restricted growth strings).
// For ten items or fewer.
inline long long bruteForceBins(const std::vector<long long>& sizes, long long capacity) {
    const std::size_t count = sizes.size();
    if (count == 0) return 0;
    long long best = static_cast<long long>(count);
    std::vector<long long> load;
    std::function<void(std::size_t)> place = [&](std::size_t item) {
        if (static_cast<long long>(load.size()) >= best) return;
        if (item == count) {
            best = static_cast<long long>(load.size());
            return;
        }
        for (std::size_t bin = 0; bin < load.size(); ++bin) {
            if (load[bin] + sizes[item] > capacity) continue;
            load[bin] += sizes[item];
            place(item + 1);
            load[bin] -= sizes[item];
        }
        load.push_back(sizes[item]);
        place(item + 1);
        load.pop_back();
    };
    place(0);
    return best;
}

// Every packable item exactly once, each bin within its group's capacity and of one group.
inline bool packingValid(const ob::m5::ScalarModel& model, const ob::m5::ScalarPacking& packing,
                         const std::vector<std::size_t>& impossible) {
    std::vector<int> seen(model.sizes.size(), 0);
    for (const std::vector<std::size_t>& bin : packing.bins) {
        if (bin.empty()) return false;
        const std::size_t group = model.groupOf.empty() ? 0 : model.groupOf[bin.front()];
        const long long capacity = model.capacityOfGroup.empty() ? model.capacity : model.capacityOfGroup[group];
        long long used = 0;
        for (const std::size_t item : bin) {
            if (item >= model.sizes.size()) return false;
            if (!model.groupOf.empty() && model.groupOf[item] != group) return false;
            used += model.sizes[item];
            ++seen[item];
        }
        if (used > capacity) return false;
    }
    for (const std::size_t item : impossible) ++seen[item];
    return std::all_of(seen.begin(), seen.end(), [](int count) { return count == 1; });
}

// INV-01..07 on a planned lane-date, recounted: every atom once on a truck or left over, trucks
// within the pool, one group per truck, within payload and ceiling, rows that fit, and every
// line available and due within the pull-forward window on the departure.
inline std::vector<std::string> invariantViolations(const ob::m5::M5LaneOutcome& outcome, const ob::m5::M5LaneDateInput& input,
                                                    const ob::FloorPatternSettings& floor) {
    std::vector<std::string> violations;
    std::vector<int> uses(input.atoms.size(), 0);
    const std::vector<ob::m5::TruckPlan> trucks = outcome.selected ? outcome.selected->trucks : std::vector<ob::m5::TruckPlan>();
    if (trucks.size() > input.pool.slots.size()) violations.push_back("INV-05 more trucks than reserved");
    for (const ob::m5::TruckPlan& truck : trucks) {
        if (truck.atoms.empty()) violations.push_back("an empty truck");
        const ob::m5::TrailerCapability& slot = input.pool.slots.at(truck.slot);
        double weight = 0.0;
        std::vector<ob::FloorItem> items;
        for (const std::size_t atom : truck.atoms) {
            ++uses.at(atom);
            const ob::m5::Atom& carried = input.atoms[atom];
            if (carried.group != truck.group) violations.push_back("INV-06 group mixed on a truck");
            if (carried.floor.heightIn > slot.heightIn) violations.push_back("INV-04 over the ceiling");
            weight += carried.weightLb;
            items.push_back(carried.floor);
            for (const ob::m5::Component& component : carried.components) {
                if (component.availableDate > input.departure) violations.push_back("INV-07 " + component.source + " not available");
                if (component.dueDate > input.departure) {
                    const long long allowed = input.dates.pullForward ? input.dates.maxDaysPulledForward : 0;
                    // Within August fixtures the day of month is the date arithmetic.
                    const long long ahead = std::stoll(component.dueDate.substr(8)) - std::stoll(input.departure.substr(8));
                    if (ahead > allowed) violations.push_back("INV-08 " + component.source + " outside the window");
                }
            }
        }
        if (weight > slot.maxWeightLb) violations.push_back("INV-02 over the weight limit");
        ob::TrailerSpec trailer;
        trailer.interiorLengthIn = slot.lengthIn;
        trailer.interiorWidthIn = slot.widthIn;
        trailer.stackHeightCeilingIn = slot.heightIn;
        trailer.weightLimitLb = slot.maxWeightLb;
        const ob::FloorLayout layout = ob::arrangeFloor(items, trailer, floor);
        if (!layout.fitsLength || !layout.unplaceableItems.empty()) violations.push_back("INV-03 rows do not fit");
    }
    for (const ob::m5::LeftOver& left : outcome.leftOver) ++uses.at(left.atom);
    for (std::size_t atom = 0; atom < uses.size(); ++atom) {
        if (uses[atom] != 1) violations.push_back("INV-01 atom " + input.atoms[atom].id + " used " + std::to_string(uses[atom]) + " times");
    }
    return violations;
}

} // namespace m5Oracles
