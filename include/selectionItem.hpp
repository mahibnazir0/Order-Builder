#pragma once
// ============================================================================
// selectionItem.hpp - M4 module C2. Turns Milestone 2's stacks into the items
// the selection methods choose between: one item per physical stack, of any
// depth, taking one floor position.
//
// A BuiltStack is a template repeated `quantity` times; each copy is an item.
// A line's real unit loads are dealt out across the copies that carry it, one
// whole unit load per level until the line runs out, so a fractional unit load
// counts its fraction for weight and cube but still takes a whole level and a
// whole position. The items of a line therefore add up to exactly the line.
//
// A stack can carry lines of different ship dates and priorities, since M2
// groups by lane, not date. The item ships by its earliest ship date, at its
// most urgent priority, and cannot leave before its latest available date.
//
// Which end of TPRIO is most urgent is the customer's open question, so it is a
// setting (selection.priorityZeroIsMostUrgent) applied here alone: each item's
// priority is an urgency rank, 0 most urgent, and every consumer downstream -
// the selection order, the knapsack's classes - reads only that rank.
// ============================================================================

#include "joiner.hpp"
#include "shipCalendar.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ob {

// By reference only, so pipeline.hpp can hold M4's results without including itself.
struct PipelineResult;

enum class ShipTiming {
    Overdue,  // ship date before the window: ships first, never dropped
    InWindow,
    Future,   // ship date after the window: not shipped this run, left for M6
};

struct SelectionItem {
    std::size_t groupIndex = 0;   // into PipelineResult::segregation.groups
    std::size_t stackIndex = 0;   // into stacking.groups[groupIndex].best.stacks
    std::size_t copyIndex = 0;    // which copy of that stack
    // Joined-line indices bottom to top, one per level. Indices, never pointers, so items stay
    // copy-safe.
    std::vector<std::size_t> lineIndices;

    double footprintLongIn = 0.0;  // the larger of its levels' footprints, per side
    double footprintShortIn = 0.0;
    double heightIn = 0.0;         // whole unit-load heights, partial levels included
    double weightLb = 0.0;         // partial levels count their fraction
    double unitLoads = 0.0;
    double cubeCubicIn = 0.0;      // each level's footprint x height x its fraction
    int positions = 1;

    std::string shipDate;          // earliest DATFR_TA of its lines
    std::string availDate;         // latest AVAIL_DATE of its lines
    int priority = 0;              // urgency rank of its most urgent line: 0 ships first
    int tprio = 0;                 // that line's TPRIO as read, for the report
    std::string productNumber;     // bottom line's MATNR, the tie-break after priority
    ShipTiming timing = ShipTiming::InWindow;
    bool mixesShipDates = false;
    bool availableAfterShipDate = false; // cannot ship on its own ship date
    bool unavailableInWindow = false;    // not available by the window's last ship date
};

enum class ItemRejection {
    InvalidShipDate,     // DATFR_TA is not a real date
    InvalidDeliveryDate, // DATTO_TA is not a real date, or is before DATFR_TA
    InvalidAvailDate,    // AVAIL_DATE is not a real date
    UnreadablePriority,  // TPRIO missing, not an integer, or outside 0..kMaxPriority
    InvalidUnitLoad,     // the line's unit-load metrics carry an error
};

struct RejectedItem {
    SelectionItem item;          // as built, for the report and the I10 totals
    std::size_t lineIndex = 0;   // the first line that failed
    ItemRejection reason = ItemRejection::InvalidShipDate;
};

enum class ItemInfeasibility {
    OverPayload,  // heavier than the trailer's payload: no truck can take it
    OverCeiling,  // taller than the trailer's ceiling
    OverFloor,    // a footprint side longer than the trailer's interior
};

struct InfeasibleItem {
    std::size_t itemIndex = 0;   // into SelectionItems::items; still an item, never dropped
    ItemInfeasibility reason = ItemInfeasibility::OverPayload;
};

// A stacked line's unit loads that no stack had room for, and what they weigh.
struct UnstackedLine {
    std::size_t lineIndex = 0;
    double unitLoads = 0.0;
    double weightLb = 0.0;
};

struct SelectionItems {
    std::vector<SelectionItem> items;     // in shipsBefore order
    std::vector<RejectedItem> rejected;
    std::vector<InfeasibleItem> infeasible;

    // I10: what the stacked lines hold (In) equals what the items carry (Out), plus what the
    // rejected items carry, plus what linesNotFullyStacked kept back.
    double unitLoadsIn = 0.0;
    double weightInLb = 0.0;
    double unitLoadsOut = 0.0;
    double weightOutLb = 0.0;
    // Must be empty: any entry means M2's stacks and the line's unit loads disagree.
    std::vector<UnstackedLine> linesNotFullyStacked;

    std::size_t itemsMixingShipDates = 0;
    std::size_t partialItems = 0; // items whose unit loads are not a whole number
};

// Builds the items for every group of a Milestone 2 run, against the run's planning window.
// Throws std::invalid_argument when Milestone 2 did not run or its results do not line up.
SelectionItems buildSelectionItems(const PipelineResult& run, const PlanningWindow& window,
                                   bool priorityZeroIsMostUrgent);

// A readable TPRIO (0..kMaxPriority) as an urgency rank, 0 most urgent.
int urgencyRank(int tprio, bool priorityZeroIsMostUrgent);

// The selection order: ship date first, then priority, then product number, then position in
// the stacks, so no two items tie (E83, E229).
bool shipsBefore(const SelectionItem& left, const SelectionItem& right);

// A truck someone else already loaded, as read from their solution: the client's Truck Builder
// loads the benchmark plans beside.
struct InputTruckLine {
    std::string idpr;
    std::string matnr;
    double quantity = 0.0;
    std::string unitOfMeasure;
};

struct InputTruck {
    std::string id;
    std::string laneId;
    std::string shipDate;
    std::vector<InputTruckLine> lines;
};

// A truck's unit-load equivalents: each line's quantity over Cases_Unit_Load of its product's
// master row in the same unit, as the Converter divides demand. Throws std::invalid_argument
// naming the truck, line, product and unit for a quantity that is negative or not finite, a
// product not in the master or not in that unit, or a Cases_Unit_Load that is not positive.
double unitLoadEquivalents(const InputTruck& truck, const ProductIndex& products);

// The physical-plausibility gate, run on every input truck before any planning: a truck over
// maxUnitLoadEquivalents is a conversion error, not a load, so the run stops. Throws
// std::invalid_argument naming the truck and its largest line's IDPR, product and unit.
void requirePlausibleTrucks(const std::vector<InputTruck>& trucks, const ProductIndex& products,
                            double maxUnitLoadEquivalents);

// Plain-ASCII names for reports.
const char* itemRejectionName(ItemRejection reason);
const char* itemInfeasibilityName(ItemInfeasibility reason);

} // namespace ob
