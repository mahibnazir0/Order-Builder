#pragma once

#include <string>

namespace ob {

// True for a real calendar date written YYYY-MM-DD: month 1..12, and a day that exists in that
// month (30 February never, 29 February only in a leap year). Two strings that pass compare
// correctly as dates with plain string comparison, so callers need not parse further.
bool isIsoDate(const std::string& text);

} // namespace ob
