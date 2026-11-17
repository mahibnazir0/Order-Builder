#pragma once
// Independent checks for M5 results. They recount from the input and the plan without calling M5's
// own validation, ledger or solver, so a defect there cannot pass its own test.

#include "floorPattern.hpp"
#include "m5LayoutChecker.hpp"
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

// Days since 1970-01-01 of an ISO date (YYYY-MM-DD), by the civil-calendar formula, so day counts
// cross month and year ends correctly.
inline long long daysSinceEpoch(const std::string& isoDate) {
    long long year = std::stoll(isoDate.substr(0, 4));
    const long long month = std::stoll(isoDate.substr(5, 2));
    const long long day = std::stoll(isoDate.substr(8, 2));
    year -= month <= 2 ? 1 : 0;
    const long long era = (year >= 0 ? year : year - 399) / 400;
    const long long yearOfEra = year - era * 400;
    const long long dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const long long dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

// INV-01..08 on a planned lane-date, recounted: every atom once on a truck or left over, trucks
// within the pool, one group per truck, within payload and ceiling, a floor witness that the
// independent layout checker accepts, and every line available and due within the pull-forward
// window on the departure.
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
                    const long long ahead = daysSinceEpoch(component.dueDate) - daysSinceEpoch(input.departure);
                    if (ahead > allowed) violations.push_back("INV-08 " + component.source + " outside the window");
                }
            }
        }
        if (weight > slot.maxWeightLb) violations.push_back("INV-02 over the weight limit");
        // ED-20: the slot's own cap, on the components' quantities (never stacks or records).
        double unitLoads = 0.0;
        for (const std::size_t atom : truck.atoms) {
            for (const ob::m5::Component& component : input.atoms[atom].components) unitLoads += component.unitLoads;
        }
        if (slot.maxUnitLoads < 1 || unitLoads > static_cast<double>(slot.maxUnitLoads)) violations.push_back("ED-20 over the slot's unit-load cap");
        for (const std::string& problem :
             m5LayoutChecker::witnessProblems(truck.layout, items, slot.lengthIn, slot.widthIn, slot.heightIn, floor)) {
            violations.push_back("INV-03 " + problem);
        }
    }
    for (const ob::m5::LeftOver& left : outcome.leftOver) ++uses.at(left.atom);
    for (std::size_t atom = 0; atom < uses.size(); ++atom) {
        if (uses[atom] != 1) violations.push_back("INV-01 atom " + input.atoms[atom].id + " used " + std::to_string(uses[atom]) + " times");
    }
    return violations;
}

} // namespace m5Oracles
