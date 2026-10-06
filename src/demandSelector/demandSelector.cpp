#include "demandSelector.hpp"

#include <stdexcept>

using namespace std;

namespace ob {

const char* const kDemandRuleArgument = "--demand-rule";

namespace {

const char* const kRuleUsage =
    "expected wholeExtract, dueBy:YYYY-MM-DD, availableBy:YYYY-MM-DD"
    " or window:YYYY-MM-DD:YYYY-MM-DD";

[[noreturn]] void throwRuleError(const string& problem) {
    throw invalid_argument(string(kDemandRuleArgument) + ": " + problem);
}

bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int digitsValue(const string& text, size_t position, size_t count) {
    int value = 0;
    for (size_t offset = 0; offset < count; ++offset) {
        value = value * 10 + (text[position + offset] - '0');
    }
    return value;
}

// Valid YYYY-MM-DD dates compare correctly as strings, so the rules never parse a date
// beyond checking it.
bool isIsoDate(const string& text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
    for (const size_t position : {0, 1, 2, 3, 5, 6, 8, 9}) {
        if (text[position] < '0' || text[position] > '9') return false;
    }
    const int year = digitsValue(text, 0, 4);
    const int month = digitsValue(text, 5, 2);
    const int day = digitsValue(text, 8, 2);
    if (month < 1 || month > 12 || day < 1) return false;
    constexpr int kDaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int lastDay = month == 2 && isLeapYear(year) ? 29 : kDaysInMonth[month - 1];
    return day <= lastDay;
}

void requireSelectorDates(const DemandSelector& selector) {
    if (selector.rule == DemandRule::WholeExtract) return;
    if (!isIsoDate(selector.toDate)) {
        throwRuleError("the planning date must be a real date written YYYY-MM-DD");
    }
    if (selector.rule != DemandRule::Window) return;
    if (!isIsoDate(selector.fromDate)) {
        throwRuleError("the window start must be a real date written YYYY-MM-DD");
    }
    if (selector.toDate < selector.fromDate) {
        throwRuleError("the window ends (" + selector.toDate + ") before it starts ("
            + selector.fromDate + ")");
    }
}

vector<string> splitOnColon(const string& text) {
    vector<string> parts(1);
    for (const char character : text) {
        if (character == ':') {
            parts.emplace_back();
        } else {
            parts.back() += character;
        }
    }
    return parts;
}

const string& ruleDate(const STRRecord& line, DemandRule rule) {
    return rule == DemandRule::AvailableBy ? line.datfr_ta : line.datto_ta;
}

bool isSelected(const string& lineDate, const DemandSelector& selector) {
    switch (selector.rule) {
    case DemandRule::WholeExtract: return true;
    case DemandRule::DueBy:
    case DemandRule::AvailableBy: return lineDate <= selector.toDate;
    case DemandRule::Window:
        return selector.fromDate <= lineDate && lineDate <= selector.toDate;
    }
    return false;
}

} // anonymous namespace

DemandSelector parseDemandSelector(const string& text) {
    if (text.empty()) {
        throwRuleError("is required; there is no default (" + string(kRuleUsage) + ")");
    }
    const vector<string> parts = splitOnColon(text);
    const string& ruleName = parts.front();

    DemandSelector selector;
    size_t expectedParts = 2;
    if (ruleName == "wholeExtract") {
        selector.rule = DemandRule::WholeExtract;
        expectedParts = 1;
    } else if (ruleName == "dueBy") {
        selector.rule = DemandRule::DueBy;
    } else if (ruleName == "availableBy") {
        selector.rule = DemandRule::AvailableBy;
    } else if (ruleName == "window") {
        selector.rule = DemandRule::Window;
        expectedParts = 3;
    } else {
        throwRuleError("unknown rule (" + string(kRuleUsage) + ")");
    }
    if (parts.size() != expectedParts) {
        throwRuleError("wrong number of dates for " + ruleName + " (" + kRuleUsage + ")");
    }
    if (selector.rule == DemandRule::Window) {
        selector.fromDate = parts[1];
        selector.toDate = parts[2];
    } else if (selector.rule != DemandRule::WholeExtract) {
        selector.toDate = parts[1];
    }
    requireSelectorDates(selector);
    return selector;
}

DemandSelection selectDemand(const vector<STRRecord>& demand, const DemandSelector& selector) {
    requireSelectorDates(selector);
    DemandSelection selection;
    selection.selector = selector;
    selection.selected.assign(demand.size(), false);
    for (size_t lineIndex = 0; lineIndex < demand.size(); ++lineIndex) {
        const string& lineDate = ruleDate(demand[lineIndex], selector.rule);
        if (selector.rule != DemandRule::WholeExtract && !isIsoDate(lineDate)) {
            selection.undatedLines.push_back(lineIndex);
            continue;
        }
        if (isSelected(lineDate, selector)) {
            selection.selected[lineIndex] = true;
            ++selection.selectedLines;
        }
    }
    return selection;
}

string describeDemandSelector(const DemandSelector& selector) {
    switch (selector.rule) {
    case DemandRule::WholeExtract: return "wholeExtract";
    case DemandRule::DueBy: return "dueBy(" + selector.toDate + ")";
    case DemandRule::AvailableBy: return "availableBy(" + selector.toDate + ")";
    case DemandRule::Window: return "window(" + selector.fromDate + ", " + selector.toDate + ")";
    }
    return "unknown";
}

} // namespace ob
