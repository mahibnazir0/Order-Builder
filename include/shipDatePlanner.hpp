#pragma once
// ============================================================================
// shipDatePlanner.hpp - M4 module C11. The ship date each good is planned on
// when it is not its own: the customer's rules for goods their own date's
// trucks do not take, and for reserved trucks their own date's goods do not
// fill. Each rule is a setting, so either can be taken out.
//
//   carryUnshippedForward (Tom Moore, 28 Oct 2026): a good its date's trucks
//     do not take - none reserved on its lane that day, or the reserved ones
//     full - spills over to the lane's next reserved date in the window. It
//     ranks there as every good does, by ship date and then priority, so it
//     goes ahead of that date's own goods.
//   pullForwardToFill (Tom Moore, 16 Oct 2026; Rev 8 section 2.10): a reserved
//     truck its date's goods do not fill takes goods due later, once they are
//     available, if no line of the good is due more than maxDaysPulledForward
//     days after the truck. They rank after that date's own goods, so they only
//     take room those leave.
//
// With either rule on, the window is planned one date at a time, earliest
// first. Each date is classified and selected (classifyLanes, selectLoads) with
// the goods that reach it, and its trucks are final before the next date is
// planned, so goods carried onto a date are those its earlier dates really
// left. What a date does not load goes on: carried to the next reserved date,
// or, pulled forward and not needed, back to its own date. Each good ends once,
// on a truck or left over; plan.moved lists every date it was offered and not
// loaded, so a moved good is in more than one route's itemIndices.
//
// A lane-date is offered pulled-forward goods, earliest due first, until the
// classifier's estimate of its goods (floor area and payload, the solvers'
// limits) reaches its reservation: offering every later good to every date
// would only slow the solvers. A lane-date above m5Threshold is left to M5 and
// offered none.
//
// With both rules off the plan is classifyLanes then selectLoads, each good on
// its own date, exactly as before the rules existed.
// ============================================================================

#include "laneClassifier.hpp"
#include "reservationBook.hpp"
#include "selectionItem.hpp"
#include "selector.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace ob {

// By reference only, so pipeline.hpp can hold M4's results without including itself.
struct PipelineResult;

struct ShipDateRules {
    bool carryUnshippedForward = false;
    bool pullForwardToFill = false;
};

struct ShipDatePlan {
    LaneClassification classification;
    SelectionPlan plan;
};

// Throws std::invalid_argument for a pull-forward limit below 0, and whatever classifyLanes and
// selectLoads throw for their settings and inputs.
ShipDatePlan planShipDates(const PipelineResult& run, const ReservationBook& book, const SelectionItems& items,
                           const LaneClassifierSettings& routing, const SelectorSettings& selector,
                           const ShipDateRules& rules, long long maxDaysPulledForward);

// The route each item ended on: the one that loaded it or left it over. None for an item in no
// route, or one only offered and moved on (pulled forward, not needed, and after the window).
std::vector<std::optional<std::size_t>> finalRouteOfItems(const LaneClassification& classification,
                                                          const SelectionPlan& plan, std::size_t itemCount);

} // namespace ob
