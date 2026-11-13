#pragma once
// Synthetic M5 fixtures (v7 Sections 0.5-0.6). Every value here is a test input, never a production
// default: production profiles come only from the params file's "m5" block.

#include "m5Budget.hpp"
#include "m5Orchestrator.hpp"
#include "m5Policy.hpp"
#include "m5Types.hpp"

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace m5Fixtures {

inline const std::string kD = "2026-08-07";
inline const std::string kDayBefore = "2026-08-06";

inline std::string dayAfter(int days) {
    // Within August only, which every fixture here stays in.
    const int day = 7 + days;
    return "2026-08-" + std::string(day < 10 ? "0" : "") + std::to_string(day);
}

inline const ob::m5::TrailerCapability kDefaultTrailer{630.0, 100.0, 108.0, 45000.0};

// A unit with one source line of its own.
inline ob::m5::Atom unitOf(const std::string& id, const std::string& group, double longSide, double shortSide, double height,
                           double weight, int priority = 0, const std::string& due = kD, const std::string& available = kD) {
    ob::m5::Atom atom;
    atom.id = id;
    atom.group = group;
    atom.floor.longSideIn = longSide;
    atom.floor.shortSideIn = shortSide;
    atom.floor.heightIn = height;
    atom.floor.levels = 1;
    atom.floor.cubeCubicIn = longSide * shortSide * height;
    atom.weightLb = weight;
    atom.unitLoads = 1.0;
    ob::m5::Component component;
    component.source = "line-" + id;
    component.product = "P-" + group;
    component.unitLoads = 1.0;
    component.dueDate = due;
    component.availableDate = available;
    component.priority = priority;
    component.offerKind = due > kD ? ob::m5::OfferKind::CanGo : ob::m5::OfferKind::Required;
    atom.components.push_back(component);
    return atom;
}

// FX-30: 20 x 100 in, 108 in, 1,500 lb, not stackable; 30 fill the default trailer at 45,000 lb.
inline ob::m5::Atom fx30(const std::string& id, const std::string& group = "A", int priority = 0, const std::string& due = kD,
                         const std::string& available = kD) {
    return unitOf(id, group, 100.0, 20.0, 108.0, 1500.0, priority, due, available);
}

inline std::vector<ob::m5::Atom> fx30Units(const std::string& prefix, std::size_t count, const std::string& group = "A",
                                           int priority = 0, const std::string& due = kD, const std::string& available = kD) {
    std::vector<ob::m5::Atom> units;
    for (std::size_t index = 0; index < count; ++index) {
        units.push_back(fx30(prefix + std::to_string(100 + index), group, priority, due, available));
    }
    return units;
}

// FX-BLOCK: one whole-truck item, 630 x 100 x 108 in, 45,000 lb.
inline ob::m5::Atom fxBlock(const std::string& id, const std::string& group = "A", int priority = 0) {
    return unitOf(id, group, 630.0, 100.0, 108.0, 45000.0, priority);
}

// X-E2E-09 display: 48 x 40 in, 50 in, 800 lb; 30 stand wide-wide in 15 rows, 600 in.
inline ob::m5::Atom display(const std::string& id, const std::string& group = "A") {
    return unitOf(id, group, 48.0, 40.0, 50.0, 800.0);
}

inline ob::m5::M5LaneDateInput laneWith(std::vector<ob::m5::Atom> atoms, std::size_t slots,
                                        const ob::m5::TrailerCapability& trailer = kDefaultTrailer, bool pullForward = false) {
    ob::m5::M5LaneDateInput input;
    input.lane = {"2028", "2574", "TL"};
    input.departure = kD;
    input.atoms = std::move(atoms);
    input.pool.slots.assign(slots, trailer);
    input.dates.pullForward = pullForward;
    input.dates.maxDaysPulledForward = 10;
    input.sourceScopeComplete = true;
    input.sourceVersion = "fixture";
    return input;
}

// The synthetic profile: acceptance OFF (as for this customer) unless a test turns it on.
inline ob::m5::M5PolicyProfile syntheticProfile() {
    ob::m5::M5PolicyProfile profile;
    profile.profileId = "fx-synthetic";
    profile.scope = ob::m5::EvidenceScope::Synthetic;
    profile.secondsPerLoad = 10.0;
    profile.configHash = "fixture";
    return profile;
}

inline ob::m5::M5PolicyProfile withAcceptance(ob::m5::M5PolicyProfile profile, double x, double y) {
    profile.acceptance = ob::m5::NumericAcceptance{x, y};
    profile.configHash = "fixture-x" + std::to_string(x) + "-y" + std::to_string(y);
    return profile;
}

inline ob::m5::PhysicalSettings physicalSettings() {
    ob::m5::PhysicalSettings settings;
    settings.floor.fullCubeFraction = 0.84;
    settings.floor.searchStateLimit = 200'000;
    settings.floor.routeSearchStateLimit = 1'000'000;
    settings.binPacker = {1.0, 1.0, 50, 100'000, 2'000, 100, 100'000};
    settings.knapsack = {0.001, 0.001, 200'000};
    return settings;
}

class FakeClock final : public ob::m5::Clock {
public:
    double nowSeconds() const override { return now_; }
    void advance(double seconds) { now_ += seconds; }

private:
    double now_ = 1000.0;
};

// A run with a fake clock that never moves on its own, so a completed run is never a timeout.
inline ob::m5::M5LaneOutcome plan(const ob::m5::M5LaneDateInput& input, const ob::m5::M5PolicyProfile& profile) {
    FakeClock clock;
    const ob::m5::M5RunServices services(clock);
    return ob::m5::planLaneDate(input, profile, physicalSettings(), services);
}

inline std::size_t shippedCount(const ob::m5::M5LaneOutcome& outcome) {
    std::size_t shipped = 0;
    if (!outcome.selected) return 0;
    for (const ob::m5::TruckPlan& truck : outcome.selected->trucks) shipped += truck.atoms.size();
    return shipped;
}

inline std::set<std::string> shippedIds(const ob::m5::M5LaneOutcome& outcome, const ob::m5::M5LaneDateInput& input) {
    std::set<std::string> ids;
    if (!outcome.selected) return ids;
    for (const ob::m5::TruckPlan& truck : outcome.selected->trucks) {
        for (const std::size_t atom : truck.atoms) ids.insert(input.atoms[atom].id);
    }
    return ids;
}

inline std::size_t shippedOfGroup(const ob::m5::M5LaneOutcome& outcome, const ob::m5::M5LaneDateInput& input, const std::string& group) {
    std::size_t shipped = 0;
    if (!outcome.selected) return 0;
    for (const ob::m5::TruckPlan& truck : outcome.selected->trucks) {
        for (const std::size_t atom : truck.atoms) shipped += input.atoms[atom].group == group ? 1 : 0;
    }
    return shipped;
}

} // namespace m5Fixtures
