#pragma once

#include "demand_types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ob {

// Which demand lines count toward the planning day. Open with the client (M3 question 1):
// the choice moves the floor by a factor of sixty, so there is deliberately no default.
enum class DemandRule {
    WholeExtract, // every line in the file
    DueBy,        // DATTO_TA <= toDate: latest arrival on or before the planning date
    AvailableBy,  // DATFR_TA <= toDate: earliest arrival on or before the planning date
    Window,       // fromDate <= DATTO_TA <= toDate: latest arrival inside the window
};

struct DemandSelector {
    DemandRule rule = DemandRule::WholeExtract;
    std::string fromDate; // YYYY-MM-DD; Window only
    std::string toDate;   // YYYY-MM-DD; DueBy, AvailableBy and Window
};

struct DemandSelection {
    DemandSelector selector;
    std::vector<bool> selected; // parallel to the demand lines
    std::size_t selectedLines = 0;
    // Lines whose date the rule reads is blank or not a real YYYY-MM-DD date. They cannot be
    // judged, so they are not selected, and are listed rather than silently dropped.
    std::vector<std::size_t> undatedLines;
    // Selected lines whose latest arrival (DATTO_TA) is before the planning date, under dueBy
    // and availableBy. Counted, not excluded: overdue demand still ships.
    std::size_t overdueLines = 0;
};

// The command-line argument that names the rule; every parse error names it.
extern const char* const kDemandRuleArgument;

// Parses the rule as given on the command line:
//   wholeExtract | dueBy:YYYY-MM-DD | availableBy:YYYY-MM-DD | window:YYYY-MM-DD:YYYY-MM-DD
// Throws std::invalid_argument naming kDemandRuleArgument for an empty or unknown rule, a
// missing, extra or invalid date, or a window that ends before it starts.
DemandSelector parseDemandSelector(const std::string& text);

// Applies the rule to every line. Throws std::invalid_argument for a selector whose own
// dates are invalid (one built in code rather than parsed).
DemandSelection selectDemand(const std::vector<STRRecord>& demand,
                             const DemandSelector& selector);

// The rule as it should be printed beside any figure it produced, e.g. "dueBy(2026-10-02)".
// Plain ASCII.
std::string describeDemandSelector(const DemandSelector& selector);

} // namespace ob
