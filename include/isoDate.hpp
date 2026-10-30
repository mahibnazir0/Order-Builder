#pragma once

#include <string>

namespace ob {

// True for a real calendar date written YYYY-MM-DD: month 1..12, and a day that exists in that
// month (30 February never, 29 February only in a leap year). Two strings that pass compare
// correctly as dates with plain string comparison, so callers need not parse further.
bool isIsoDate(const std::string& text);

// Day of the week of a date isIsoDate accepts: 0 is Monday, 6 is Sunday.
int weekdayIndex(const std::string& isoDate);

// "Monday" .. "Sunday" for weekdayIndex's 0 .. 6.
const char* weekdayName(int weekday);

// The date `days` after a date isIsoDate accepts, as YYYY-MM-DD; negative goes back.
std::string addDays(const std::string& isoDate, int days);

} // namespace ob
