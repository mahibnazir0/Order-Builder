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

} // namespace ob
