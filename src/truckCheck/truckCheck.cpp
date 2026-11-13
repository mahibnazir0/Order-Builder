#include "truckCheck.hpp"
#include "reportFormat.hpp"

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

} // namespace ob
