#include "truckCheck.hpp"
#include "reportFormat.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <tuple>

using namespace std;

namespace ob {

bool floorFits(const FloorLayout& layout) {
    return layout.fitsLength && layout.withinMaxPositions && layout.unplaceableItems.empty();
}

TruckFit floorVerdict(const FloorLayout& layout) {
    if (floorFits(layout)) return TruckFit::Fits;
    const bool onlyRowsUnproven = layout.fitUnproven && layout.withinMaxPositions && layout.unplaceableItems.empty();
    return onlyRowsUnproven ? TruckFit::Undecided : TruckFit::DoesNotFit;
}

TruckLoadCheck checkTruckLoad(const vector<FloorItem>& items, double weightLb, double unitLoads,
                              const TrailerSpec& trailer, const FloorPatternSettings& settings,
                              optional<double> maxUnitLoadsPerTruck, FloorSearchBudget* budget) {
    TruckLoadCheck check;
    for (size_t index = 0; index < items.size(); ++index) {
        if (items[index].heightIn > trailer.stackHeightCeilingIn) {
            check.findings.push_back("stack " + to_string(index) + " is " + fixed(items[index].heightIn, 1)
                                     + " in tall, over the " + fixed(trailer.stackHeightCeilingIn, 1) + " in ceiling");
        }
    }
    if (weightLb > trailer.weightLimitLb) {
        check.findings.push_back("load weighs " + fixed(weightLb, 1) + " lb, over the "
                                 + fixed(trailer.weightLimitLb, 1) + " lb payload");
    }
    if (maxUnitLoadsPerTruck && unitLoads > *maxUnitLoadsPerTruck) {
        check.findings.push_back("load holds " + fixed(unitLoads, 1) + " unit loads, over the cap of "
                                 + fixed(*maxUnitLoadsPerTruck, 1));
    }
    if (!check.findings.empty()) return check;
    check.layout = arrangeFloor(items, trailer, settings, budget);
    check.verdict = floorVerdict(check.layout);
    if (check.verdict == TruckFit::DoesNotFit) {
        check.findings.push_back(check.layout.unplaceableItems.empty()
                                     ? "rows need " + fixed(check.layout.usedLengthIn, 1) + " in of the "
                                           + fixed(trailer.interiorLengthIn, 1) + " in floor"
                                     : "a stack fits the floor in no orientation");
    } else if (check.verdict == TruckFit::Undecided) {
        check.findings.push_back("the row search ran out of states before finding rows that fit");
    }
    return check;
}

bool keepsMethodLoad(const vector<vector<size_t>>& methodTrucks, const vector<vector<size_t>>& trucks,
                     const vector<char>& placed) {
    if (trucks.size() > methodTrucks.size()) return false;
    for (const vector<size_t>& truck : methodTrucks) {
        for (const size_t position : truck) {
            if (!placed[position]) return false;
        }
    }
    return true;
}

RouteRows::RouteRows(vector<FloorItem> items, const TrailerSpec& trailer, const FloorPatternSettings& settings,
                     double maxUnitLoadsPerTruck)
    : items_(std::move(items)), trailer_(trailer), settings_(settings), maxUnitLoadsPerTruck_(maxUnitLoadsPerTruck) {}

FloorLayout RouteRows::arrange(const vector<size_t>& truck, FloorSearchBudget* budget) const {
    vector<FloorItem> floorItems;
    floorItems.reserve(truck.size());
    for (const size_t position : truck) floorItems.push_back(items_[position]);
    return arrangeFloor(floorItems, trailer_, settings_, budget);
}

TruckLimits RouteRows::truckLimits(const vector<KnapsackItem>& items, RowVerdicts& verdicts) const {
    TruckLimits truck;
    ItemLimit unitLoads{{}, maxUnitLoadsPerTruck_};
    unitLoads.perItem.reserve(items.size());
    for (const KnapsackItem& item : items) unitLoads.perItem.push_back(item.unitLoads);
    truck.limits.push_back(std::move(unitLoads));
    truck.limits.push_back({rowLengthShares(items_, trailer_, settings_), trailer_.interiorLengthIn});
    // The route's floor searches are bounded however many loads its knapsack judges: the
    // relaxed rows and the rows themselves each by a budget of their own, so the cuts the
    // search makes on the way never use up the states a proof's last refusal needs.
    truck.mayStillFit = loadGrowthCheck(items_, trailer_, settings_,
                                        make_shared<FloorSearchBudget>(FloorSearchBudget{settings_.routeSearchStateLimit}));
    setRowChecks(items, verdicts, make_shared<FloorSearchBudget>(FloorSearchBudget{settings_.routeSearchStateLimit}),
                 truck.mayStillFit, truck);
    if (settings_.maxPositions) {
        truck.limits.push_back({vector<double>(items.size(), 1.0), static_cast<double>(*settings_.maxPositions)});
    }
    return truck;
}

void RouteRows::setRowChecks(const vector<KnapsackItem>& items, RowVerdicts& verdicts, shared_ptr<FloorSearchBudget> budget,
                             const LoadCheck& mayStillFit, TruckLimits& truck) const {
    map<tuple<double, double, int>, size_t> floorTypes;
    vector<size_t> floorTypeOf;
    floorTypeOf.reserve(items.size());
    for (const KnapsackItem& item : items) {
        floorTypeOf.push_back(
            floorTypes.try_emplace({item.longSideIn, item.shortSideIn, item.levels}, floorTypes.size()).first->second);
    }
    // The load's sorted floor types in `key`, and whether it is within the unit-load cap.
    auto keyOf = [this, &items, floorTypeOf](const vector<size_t>& chosen, vector<size_t>& key) {
        double unitLoads = 0.0;
        key.clear();
        for (const size_t position : chosen) {
            unitLoads += items[position].unitLoads;
            key.push_back(floorTypeOf[position]);
        }
        sort(key.begin(), key.end());
        return unitLoads <= maxUnitLoadsPerTruck_ + kQuantityEpsilon;
    };
    auto sortedTruck = [](const vector<size_t>& chosen) {
        vector<size_t> sorted = chosen;
        sort(sorted.begin(), sorted.end());
        return sorted;
    };
    truck.fits = [this, &verdicts, routeBudget = std::move(budget), keyOf, sortedTruck, mayStillFit,
                  key = vector<size_t>()](const vector<size_t>& chosen) mutable {
        if (!keyOf(chosen, key) || !mayStillFit(chosen)) return TruckFit::DoesNotFit;
        const auto known = verdicts.byGroupings.find(key);
        if (known != verdicts.byGroupings.end()) return known->second;
        const auto quick = verdicts.bySortedRows.find(key);
        const TruckFit verdict = quick != verdicts.bySortedRows.end() && quick->second
                                     ? TruckFit::Fits
                                     : floorVerdict(arrange(sortedTruck(chosen), routeBudget.get()));
        verdicts.byGroupings.emplace(key, verdict);
        return verdict;
    };
    truck.quickFits = [this, &verdicts, keyOf, sortedTruck, mayStillFit,
                       key = vector<size_t>()](const vector<size_t>& chosen) mutable {
        if (!keyOf(chosen, key) || !mayStillFit(chosen)) return false;
        const auto known = verdicts.bySortedRows.find(key);
        if (known != verdicts.bySortedRows.end()) return known->second;
        // An empty budget arranges the rows without searching groupings.
        FloorSearchBudget noSearch;
        const bool fits = floorFits(arrange(sortedTruck(chosen), &noSearch));
        verdicts.bySortedRows.emplace(key, fits);
        return fits;
    };
}

} // namespace ob
