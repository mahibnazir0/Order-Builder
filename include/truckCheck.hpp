#pragma once
// ============================================================================
// truckCheck.hpp - the physical check of one truck's load, shared by M4's
// selector and M5 so both judge a truck by one implementation (M5 handover P2).
//
// A load fits when every stack stands under the ceiling, the load is within the
// payload and, when the customer has one, the unit-load cap, and floorPattern
// arranges every stack in rows within the trailer length and position cap.
// Rows are judged by arrangeFloor; a refusal where only the row search ran out
// of states is Undecided, never a proof that the load does not fit.
// ============================================================================

#include "floorPattern.hpp"
#include "knapsack.hpp"
#include "trailerSpec.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ob {

// Whether the rows fit the trailer: every item placed, within the length and the position cap.
bool floorFits(const FloorLayout& layout);

// Fits, DoesNotFit, or Undecided when only the rows refused the load and their search ran out of
// states first.
TruckFit floorVerdict(const FloorLayout& layout);

struct TruckLoadCheck {
    TruckFit verdict = TruckFit::DoesNotFit;
    FloorLayout layout;                 // the rows arrangeFloor chose; empty when not arranged
    std::vector<std::string> findings;  // each hard limit the load breaks, in words
};

// The whole load judged against the trailer: ceiling per stack, payload, unit-load cap when
// given, then the rows. `weightLb` and `unitLoads` are the load's totals. Throws as arrangeFloor
// does for a trailer or settings out of range.
TruckLoadCheck checkTruckLoad(const std::vector<FloorItem>& items, double weightLb, double unitLoads,
                              const TrailerSpec& trailer, const FloorPatternSettings& settings,
                              std::optional<double> maxUnitLoadsPerTruck, FloorSearchBudget* budget = nullptr);

// Row verdicts on a route's loads, by their sorted floor types: on every grouping of the rows, and
// on the rows without searching groupings.
struct RowVerdicts {
    std::map<std::vector<std::size_t>, TruckFit> byGroupings;
    std::map<std::vector<std::size_t>, bool> bySortedRows;
};

// Whether the trucks still carry every good the method loaded, on no more trucks. `placed` is
// parallel to the route's items.
bool keepsMethodLoad(const std::vector<std::vector<std::size_t>>& methodTrucks,
                     const std::vector<std::vector<std::size_t>>& trucks, const std::vector<char>& placed);

// The rows of one route's loads. A load is a list of positions in the route's item list. Holds the
// route's floor items, so the trailer and settings given must outlive it; the limits it hands out
// refer back to it, so it must outlive them and is neither copied nor moved.
class RouteRows {
public:
    RouteRows(std::vector<FloorItem> items, const TrailerSpec& trailer, const FloorPatternSettings& settings,
              double maxUnitLoadsPerTruck);
    RouteRows(const RouteRows&) = delete;
    RouteRows& operator=(const RouteRows&) = delete;

    // The rows arrangeFloor chooses for the load; `budget` bounds its grouping search when given.
    FloorLayout arrange(const std::vector<std::size_t>& truck, FloorSearchBudget* budget = nullptr) const;

    // One truck of the route as the knapsack judges it: rows and the unit-load cap on a whole load,
    // and the limits it may cut on - the cap, each stack's least share of the trailer length, the
    // positions cap when the customer has one, and the rows relaxed so they only grow with goods.
    // `items` and `verdicts` must outlive the limits.
    TruckLimits truckLimits(const std::vector<KnapsackItem>& items, RowVerdicts& verdicts) const;

private:
    // The knapsack's row checks, both refusing a load over the unit-load cap or one mayStillFit cuts,
    // which no grouping of its rows fits. `fits` judges every grouping of the rows, drawing on
    // `budget`; quickFits judges the rows without searching groupings - exact up to kExactFloorItems
    // stacks, in sorted order above - so it accepts only what `fits` accepts. Rows depend only on
    // each item's footprint and levels, so loads alike in those share a verdict, remembered by their
    // sorted floor types.
    void setRowChecks(const std::vector<KnapsackItem>& items, RowVerdicts& verdicts,
                      std::shared_ptr<FloorSearchBudget> budget, const LoadCheck& mayStillFit, TruckLimits& truck) const;

    std::vector<FloorItem> items_;
    const TrailerSpec& trailer_;
    const FloorPatternSettings& settings_;
    double maxUnitLoadsPerTruck_;
};

} // namespace ob
