#include "demandSelector.hpp"
#include "isoDate.hpp"

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

// The latest arrival has already passed on the planning date. A window cannot select one:
// its lines all arrive inside it.
bool isOverdue(const STRRecord& line, const DemandSelector& selector) {
    if (selector.rule != DemandRule::DueBy && selector.rule != DemandRule::AvailableBy) {
        return false;
    }
    return isIsoDate(line.datto_ta) && line.datto_ta < selector.toDate;
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
            if (isOverdue(demand[lineIndex], selector)) ++selection.overdueLines;
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
