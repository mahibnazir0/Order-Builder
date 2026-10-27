#include "shipCalendar.hpp"
#include "isoDate.hpp"

#include <cctype>
#include <stdexcept>

using namespace std;

namespace ob {

namespace {

string upperCase(string text) {
    for (char& letter : text) letter = static_cast<char>(toupper(static_cast<unsigned char>(letter)));
    return text;
}

// -1 for anything that is not a weekday name. The feed writes "TUESDAY"; case is not trusted.
int weekdayFromName(const string& name) {
    const string upper = upperCase(name);
    for (int weekday = 0; weekday < 7; ++weekday) {
        if (upper == upperCase(weekdayName(weekday))) return weekday;
    }
    return -1;
}

string describeCtlRow(size_t rowIndex, const CTLRecord& row) {
    return "CTL row " + to_string(rowIndex) + " (ZDATE '" + row.zdate + "', ZDAY '" + row.zday
           + "')";
}

} // anonymous namespace

PlanningWindow planningWindow(const vector<CTLRecord>& ctl, const string& planDate) {
    if (!isIsoDate(planDate)) {
        throw invalid_argument("Plan date '" + planDate + "' is not a YYYY-MM-DD date");
    }
    const int planWeekday = weekdayIndex(planDate);

    // Every row's shape is checked, not only the matching ones: a malformed row could be the
    // plan date's own override, and skipping it would silently apply the weekday row instead.
    vector<size_t> datedMatches;
    vector<size_t> weekdayMatches;
    for (size_t rowIndex = 0; rowIndex < ctl.size(); ++rowIndex) {
        const CTLRecord& row = ctl[rowIndex];
        if (!row.zdate.empty()) {
            if (!isIsoDate(row.zdate)) {
                throw invalid_argument(describeCtlRow(rowIndex, row) + ": ZDATE is not a date");
            }
            if (row.zdate == planDate) datedMatches.push_back(rowIndex);
        } else {
            const int rowWeekday = weekdayFromName(row.zday);
            if (rowWeekday < 0) {
                throw invalid_argument(describeCtlRow(rowIndex, row)
                                       + ": neither a date nor a weekday");
            }
            if (rowWeekday == planWeekday) weekdayMatches.push_back(rowIndex);
        }
    }

    const vector<size_t>& matches = datedMatches.empty() ? weekdayMatches : datedMatches;
    if (matches.empty()) {
        throw invalid_argument(string("CTL table has no row for ") + weekdayName(planWeekday)
                               + " or for " + planDate + ": the planning window is unknown");
    }
    const CTLRecord& chosen = ctl[matches.front()];
    for (const size_t rowIndex : matches) {
        const CTLRecord& row = ctl[rowIndex];
        if (row.level_load_start != chosen.level_load_start
            || row.level_load_end != chosen.level_load_end) {
            throw invalid_argument(describeCtlRow(rowIndex, row) + " disagrees with "
                                   + describeCtlRow(matches.front(), chosen)
                                   + " on the planning window");
        }
    }
    if (chosen.level_load_start < 0 || chosen.level_load_end < chosen.level_load_start) {
        throw invalid_argument(describeCtlRow(matches.front(), chosen)
                               + ": LEVEL_LOAD_START " + to_string(chosen.level_load_start)
                               + " and LEVEL_LOAD_END " + to_string(chosen.level_load_end)
                               + " are not a window of days after the plan date");
    }

    PlanningWindow window;
    window.planDate = planDate;
    window.firstShipDate = addDays(planDate, chosen.level_load_start);
    window.lastShipDate = addDays(planDate, chosen.level_load_end);
    window.ctlRowIndex = matches.front();
    window.fromDatedRow = !datedMatches.empty();
    return window;
}

} // namespace ob
