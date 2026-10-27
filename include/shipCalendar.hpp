#pragma once
// ============================================================================
// shipCalendar.hpp - The ship dates a planning run covers, from the upstream
// calendar the demand file carries in its CTL block.
//
// Each CTL row is a weekday (ZDAY) or a specific date (ZDATE) and gives the
// first and last ship day as days after the plan date (LEVEL_LOAD_START,
// LEVEL_LOAD_END). A row dated the plan date overrides the plan's weekday row
// (client, 27 Oct: date-specific settings override weekday settings). No
// weekday is special: a plan made on any day reads its own row.
// ============================================================================

#include "demand_types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ob {

struct PlanningWindow {
    std::string planDate;
    std::string firstShipDate;
    std::string lastShipDate;
    std::size_t ctlRowIndex = 0;
    bool fromDatedRow = false; // a ZDATE row matching the plan date overrode the weekday row
};

// The window for planDate from the CTL table. Throws std::invalid_argument naming the problem
// when planDate is not a real date, there is no row for the plan's weekday or date, two
// matching rows disagree, or a row is malformed (blank, unknown weekday, invalid ZDATE,
// negative or reversed LEVEL_LOAD_START/END). Dated rows for other dates are ignored.
PlanningWindow planningWindow(const std::vector<CTLRecord>& ctl, const std::string& planDate);

} // namespace ob
