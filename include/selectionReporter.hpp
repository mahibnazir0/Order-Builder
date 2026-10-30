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
// The sections are the specification's report list:
//   1. Inputs                  files, plan date, window, items, routes
//   2. Parameters              trailer, full lines, unit rules, every method's settings
//   3. Fullness                trucks, reservations, unused, rear waste, cube, weight
//   4. Reservations not used   per lane-date with the reason, per lane, and the day's
//                              penalty exposure
//   5. Log of activities       per route: the methods tried and how each ended, the one
//                              kept, proven or not, the binding limit as measured
//   6. Trucks                  per truck: weight and cube against their full lines, rear
//                              waste, floor patterns, pallets turned, lone top pallets
//   7. Goods left over         by reason; every item when detailed
//   8. Item master             the products on the trucks; their master rows when detailed
// ============================================================================

#include "laneClassifier.hpp"
#include "pipeline.hpp"
#include "reservationBook.hpp"
#include "selectionItem.hpp"
#include "selector.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace ob {

// Detailed adds item-level lists; it prints from the same plan and never changes it.
enum class ReportDetail { Standard, Detailed };

struct SelectionReportContext {
    std::string extractLabel;
    std::vector<std::string> inputFiles;  // as given to the run
    LaneClassifierSettings routing;
    SelectorSettings settings;            // the ones the plan was made with
    ReportDetail detail = ReportDetail::Standard;
};

// Throws std::invalid_argument before printing anything when a lane-date was given more
// trucks than it reserved: that cannot happen, so the run must stop rather than print it. Also
// when the plan does not belong to this classification.
void printSelectionReport(std::ostream& out, const PipelineResult& run, const ReservationBook& book,
                          const SelectionItems& items, const LaneClassification& classification,
                          const SelectionPlan& plan, const SelectionReportContext& context);

// The eight section titles, in order, as printed.
const std::vector<std::string>& selectionReportSections();

} // namespace ob
