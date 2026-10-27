#pragma once

#include "demand_types.hpp"
#include "placeholder_types.hpp"
#include "shipCalendar.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ob {

// Which demand lines count toward the floor. The floor is calculated separately for each ship
// date (DATFR_TA) from the volume that ships on it: a run that covers Saturday and Sunday gets
// a floor for each, never one accumulated over both (client, 24 and 27 Oct). Which ship dates
// a run covers comes from the upstream calendar, never from the planning weekday.
enum class DemandRule {
    WholeExtract, // every line as one floor, whatever its ship date: a check on the extract
    ShipDates,    // a floor for each ship date from firstShipDate to lastShipDate
    Calendar,     // a floor for each ship date the demand file's CTL calendar gives planDate
};

struct DemandSelector {
    DemandRule rule = DemandRule::WholeExtract;
    std::string firstShipDate; // YYYY-MM-DD; ShipDates
    std::string lastShipDate;  // YYYY-MM-DD; ShipDates
    std::string planDate;      // YYYY-MM-DD; Calendar
};

// The lines one floor counts.
struct DemandSelection {
    std::string shipDate;       // empty under wholeExtract: every ship date
    std::vector<bool> selected; // parallel to the demand lines
    std::size_t selectedLines = 0;
};

// Where an extract's lines and placeholders fall against the ship dates a run covers. Ship
// dates built, demand the planner can see and how far demand may be pulled forward are
// separate (client, 27 Oct): a line shipping after the last date is visible but counts toward
// no floor here, and pulling it onto an earlier truck is a later stage's decision.
struct ShipDateCoverage {
    std::vector<std::string> shipDates; // in date order; empty under wholeExtract
    // Set under the calendar rule: the CTL row the dates came from.
    std::optional<PlanningWindow> calendarWindow;
    std::size_t linesOnShipDates = 0;
    std::size_t linesBeforeFirstShipDate = 0; // due to ship before the run's first date
    std::size_t linesAfterLastShipDate = 0;   // future demand, visible to the planner
    // DATFR_TA blank or not a real date: no day to count them on, so in no floor.
    std::vector<std::size_t> undatedLines;
    // Placeholder trucks reserved for a date the run does not cover; the Validator's rejected
    // placeholders are left out.
    long long reservedTrucksOutsideShipDates = 0;
};

// The longest run of ship dates one run may cover. Calendar windows in the client's extracts
// span 2 to 4 days; a date typed with the wrong year or a corrupt LEVEL_LOAD_END would
// otherwise plan a floor for every day of decades.
constexpr int kMaxShipDatesPerRun = 31;

// The command-line argument that names the rule; every parse error names it.
extern const char* const kDemandRuleArgument;

// Parses the rule as given on the command line:
//   wholeExtract | shipDates:YYYY-MM-DD[:YYYY-MM-DD] | calendar:YYYY-MM-DD
// shipDates with one date covers that day alone. Throws std::invalid_argument naming
// kDemandRuleArgument for an empty or unknown rule, a missing, extra or invalid date, dates
// that run backwards, or more than kMaxShipDatesPerRun ship dates.
DemandSelector parseDemandSelector(const std::string& text);

// The ship dates the rule covers, in order: none for wholeExtract, otherwise every date from
// the first to the last, the calendar rule taking them from planningWindow(calendar, planDate).
// Throws std::invalid_argument for the reasons parseDemandSelector and planningWindow do.
ShipDateCoverage coverShipDates(const DemandSelector& selector,
                                const std::vector<CTLRecord>& calendar);

// Counts where each line and each usable placeholder falls against coverage.shipDates.
// placeholderExcluded is parallel to placeholders (Validator::excludedPlaceholderFlags).
// Under wholeExtract every line is on the run's dates and no line is judged undated.
// Throws std::invalid_argument when placeholderExcluded is not parallel to placeholders.
void countCoverage(ShipDateCoverage& coverage, const std::vector<STRRecord>& demand,
                   const std::vector<PlaceholderRecord>& placeholders,
                   const std::vector<bool>& placeholderExcluded);

// The lines that ship on shipDate (DATFR_TA equal to it), or every line when shipDate is empty.
DemandSelection selectShipDate(const std::vector<STRRecord>& demand, const std::string& shipDate);

// The rule as it should be printed beside any figure it produced, e.g.
// "calendar(2026-08-06)". Plain ASCII.
std::string describeDemandSelector(const DemandSelector& selector);

} // namespace ob
