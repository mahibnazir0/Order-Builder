#pragma once
// ============================================================================
// selectionReporter.hpp - M4 module C9. Prints the day's load selection. The
// client reads the printed report, not the code, so every figure carries what
// produced it.
//
// Presentation only: it adds up and formats what the selector decided and
// decides nothing. Every byte is plain ASCII; text from the input files has any
// other byte replaced by '?'. Columns widen to their longest entry and nothing
// is cut short.
//
// A Result line opens it: complete, or incomplete (exit 7) with every cause.
// The sections are the specification's report list, with Rev 8's sections A to E:
//   1. Inputs                  files, plan date, window, items, mixed-date stacks, routes;
//                              stacks rejected and placeholder rows skipped, with reasons
//   2. Parameters              trailer, full lines, unit rules, every method's settings,
//                              and the provisional business rules the plan rests on
//   3. How each lane was routed  (A) lane-dates against their reservation, by size and
//                              products, and the group routes per method
//   4. Fullness                (B) trucks, reservations, unused, rear waste, cube, weight,
//                              lane-dates filled or short
//   5. Reservations not used   per lane-date with the reason, per lane, and the day's
//                              penalty exposure; lanes reserved with no demand, and
//                              demand lanes with no reservation
//   6. Log of activities       per route: the methods tried and how each ended, the one
//                              kept, proven or not, the lane-date's binding limit
//   7. Trucks                  per truck: weight and cube against their full lines, rear
//                              waste, floor patterns, pallets turned, lone top pallets
//   8. One truck in full       (D) one truck stack by stack
//   9. Goods left over         by reason; every item when detailed
//   10. Nothing lost           (E) unit loads in against out, and why reservations went
//                              unfilled
//   11. Item master            the products on the trucks; their master rows when detailed
// ============================================================================

#include "laneClassifier.hpp"
#include "reservationBook.hpp"
#include "selectionItem.hpp"
#include "selector.hpp"
#include "shipDatePlanner.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace ob {

// By reference only, so pipeline.hpp can hold M4's results without including itself.
struct PipelineResult;

// Detailed adds item-level lists; it prints from the same plan and never changes it.
enum class ReportDetail { Standard, Detailed };

struct SelectionReportContext {
    std::string extractLabel;
    std::vector<std::string> inputFiles;  // as given to the run
    LaneClassifierSettings routing;
    SelectorSettings settings;            // the ones the plan was made with
    // Why the run is incomplete (selectionShortfalls), printed as its Result; empty is complete.
    std::vector<std::string> shortfalls;
    // The routes whose plan is not proven (selectionProvisional), printed under the Result.
    std::vector<std::string> provisional;
    // Printed under the provisional business rules, with the routing settings.
    bool priorityZeroIsMostUrgent = true;
    long long maxDaysPulledForward = 0;
    ShipDateRules shipDates;
    std::string defaultEquipment;
    ReportDetail detail = ReportDetail::Standard;
};

// Throws std::invalid_argument before printing anything when a lane-date was given more
// trucks than it reserved: that cannot happen, so the run must stop rather than print it. Also
// when the plan does not belong to this classification.
void printSelectionReport(std::ostream& out, const PipelineResult& run, const ReservationBook& book,
                          const SelectionItems& items, const LaneClassification& classification,
                          const SelectionPlan& plan, const SelectionReportContext& context);

// The section titles, in order, as printed.
const std::vector<std::string>& selectionReportSections();

} // namespace ob
