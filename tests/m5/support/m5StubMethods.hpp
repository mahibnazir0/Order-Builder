#pragma once
// A method that offers every group one fixed plan, so the selection, validation and repair seams
// can be driven with controlled candidates (X-RUN-13, X-BLK-05/06, R-1c).

#include "m5Methods.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace m5Stubs {

class FixedMethod final : public ob::m5::MethodPlanner {
public:
    FixedMethod(ob::m5::MethodId id, ob::m5::GroupTrucks plan) : id_(id), plan_(std::move(plan)) {}
    // `trucks` trucks of `perTruck` consecutive atoms from atom 0.
    FixedMethod(ob::m5::MethodId id, std::size_t trucks, std::size_t perTruck) : id_(id) {
        for (std::size_t truck = 0; truck < trucks; ++truck) {
            ob::m5::Truck atoms;
            for (std::size_t unit = 0; unit < perTruck; ++unit) atoms.push_back(truck * perTruck + unit);
            plan_.push_back(atoms);
        }
    }
    // Supplies its plans as exact: proven best for each truck count within this stub's own model.
    void markExact() { exact_ = true; }
    ob::m5::MethodId id() const override { return id_; }
    ob::m5::GroupSupply supplyForGroup(const ob::m5::MethodContext& context, const ob::m5::GroupId& group) const override {
        ob::m5::GroupSupply supply;
        supply.group = group;
        supply.exact = exact_;
        supply.plans.push_back({});
        ob::m5::GroupTrucks ofGroup;
        for (const ob::m5::Truck& truck : plan_) {
            if (context.snapshot.input.atoms[truck.front()].group == group) ofGroup.push_back(truck);
        }
        for (std::size_t trucks = 1; trucks <= std::min(ofGroup.size(), context.maxTrucks); ++trucks) {
            supply.plans.emplace_back(ofGroup.begin(), ofGroup.begin() + static_cast<std::ptrdiff_t>(trucks));
        }
        return supply;
    }

private:
    ob::m5::MethodId id_;
    ob::m5::GroupTrucks plan_;
    bool exact_ = false;
};

} // namespace m5Stubs
