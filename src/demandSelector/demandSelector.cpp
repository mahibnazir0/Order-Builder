#include "demandSelector.hpp"
#include "isoDate.hpp"

#include <algorithm>
#include <stdexcept>

using namespace std;

namespace ob {

const char* const kDemandRuleArgument = "--demand-rule";

namespace {

const char* const kRuleUsage =
    "expected wholeExtract, shipDates:YYYY-MM-DD[:YYYY-MM-DD] or calendar:YYYY-MM-DD";

[[noreturn]] void throwRuleError(const string& problem) {
    throw invalid_argument(string(kDemandRuleArgument) + ": " + problem);
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

// Every date from first to last. Both must be real dates.
vector<string> datesFrom(const string& firstShipDate, const string& lastShipDate) {
    if (lastShipDate < firstShipDate) {
        throwRuleError("the last ship date (" + lastShipDate + ") is before the first ("
            + firstShipDate + ")");
    }
    vector<string> dates{firstShipDate};
    while (dates.back() != lastShipDate) {
        if (dates.size() == static_cast<size_t>(kMaxShipDatesPerRun)) {
            throwRuleError("ship dates " + firstShipDate + " to " + lastShipDate + " cover more"
                " than " + to_string(kMaxShipDatesPerRun) + " days");
        }
        dates.push_back(addDays(dates.back(), 1));
    }
    return dates;
}

void requireSelectorDates(const DemandSelector& selector) {
    switch (selector.rule) {
    case DemandRule::WholeExtract: return;
    case DemandRule::ShipDates:
        if (!isIsoDate(selector.firstShipDate) || !isIsoDate(selector.lastShipDate)) {
            throwRuleError("every ship date must be a real date written YYYY-MM-DD");
        }
        datesFrom(selector.firstShipDate, selector.lastShipDate);
        return;
    case DemandRule::Calendar:
        if (!isIsoDate(selector.planDate)) {
            throwRuleError("the plan date must be a real date written YYYY-MM-DD");
        }
        return;
    }
}

} // anonymous namespace

DemandSelector parseDemandSelector(const string& text) {
    if (text.empty()) {
        throwRuleError("is required; there is no default (" + string(kRuleUsage) + ")");
    }
    const vector<string> parts = splitOnColon(text);
    const string& ruleName = parts.front();

    DemandSelector selector;
    bool partsFit = false;
    if (ruleName == "wholeExtract") {
        selector.rule = DemandRule::WholeExtract;
        partsFit = parts.size() == 1;
    } else if (ruleName == "shipDates") {
        selector.rule = DemandRule::ShipDates;
        partsFit = parts.size() == 2 || parts.size() == 3;
        if (partsFit) {
            selector.firstShipDate = parts[1];
            selector.lastShipDate = parts.back();
        }
    } else if (ruleName == "calendar") {
        selector.rule = DemandRule::Calendar;
        partsFit = parts.size() == 2;
        if (partsFit) selector.planDate = parts[1];
    } else {
        throwRuleError("unknown rule (" + string(kRuleUsage) + ")");
    }
    if (!partsFit) {
        throwRuleError("wrong number of dates for " + ruleName + " (" + kRuleUsage + ")");
    }
    requireSelectorDates(selector);
    return selector;
}

ShipDateCoverage coverShipDates(const DemandSelector& selector, const vector<CTLRecord>& calendar) {
    requireSelectorDates(selector);
    ShipDateCoverage coverage;
    switch (selector.rule) {
    case DemandRule::WholeExtract: break;
    case DemandRule::ShipDates:
        coverage.shipDates = datesFrom(selector.firstShipDate, selector.lastShipDate);
        break;
    case DemandRule::Calendar:
        coverage.calendarWindow = planningWindow(calendar, selector.planDate);
        coverage.shipDates =
            datesFrom(coverage.calendarWindow->firstShipDate, coverage.calendarWindow->lastShipDate);
        break;
    }
    return coverage;
}

void countCoverage(ShipDateCoverage& coverage, const vector<STRRecord>& demand,
                   const vector<PlaceholderRecord>& placeholders,
                   const vector<bool>& placeholderExcluded) {
    if (placeholderExcluded.size() != placeholders.size()) {
        throw invalid_argument("demandSelector: placeholder flags are not parallel to the"
                               " placeholders");
    }
    if (coverage.shipDates.empty()) {
        coverage.linesOnShipDates = demand.size();
        return;
    }
    const string& firstShipDate = coverage.shipDates.front();
    const string& lastShipDate = coverage.shipDates.back();
    for (size_t lineIndex = 0; lineIndex < demand.size(); ++lineIndex) {
        const string& shipDate = demand[lineIndex].datfr_ta;
        if (!isIsoDate(shipDate)) {
            coverage.undatedLines.push_back(lineIndex);
        } else if (shipDate < firstShipDate) {
            ++coverage.linesBeforeFirstShipDate;
        } else if (shipDate > lastShipDate) {
            ++coverage.linesAfterLastShipDate;
        } else {
            ++coverage.linesOnShipDates;
        }
    }
    // shipDates is a contiguous run of real dates, so a real date between its ends is one of them.
    for (size_t placeholderIndex = 0; placeholderIndex < placeholders.size(); ++placeholderIndex) {
        if (placeholderExcluded[placeholderIndex]) continue;
        const string& shipDate = placeholders[placeholderIndex].datfr_ta;
        if (!isIsoDate(shipDate) || shipDate < firstShipDate || shipDate > lastShipDate) {
            coverage.reservedTrucksOutsideShipDates += placeholders[placeholderIndex].no_of_loads;
        }
    }
}

DemandSelection selectShipDate(const vector<STRRecord>& demand, const string& shipDate) {
    DemandSelection selection;
    selection.shipDate = shipDate;
    selection.selected.assign(demand.size(), false);
    for (size_t lineIndex = 0; lineIndex < demand.size(); ++lineIndex) {
        if (shipDate.empty() || demand[lineIndex].datfr_ta == shipDate) {
            selection.selected[lineIndex] = true;
            ++selection.selectedLines;
        }
    }
    return selection;
}

string describeDemandSelector(const DemandSelector& selector) {
    switch (selector.rule) {
    case DemandRule::WholeExtract: return "wholeExtract";
    case DemandRule::ShipDates:
        return selector.firstShipDate == selector.lastShipDate
            ? "shipDates(" + selector.firstShipDate + ")"
            : "shipDates(" + selector.firstShipDate + " to " + selector.lastShipDate + ")";
    case DemandRule::Calendar:
        return "calendar(plan " + selector.planDate + " "
            + weekdayName(weekdayIndex(selector.planDate)) + ")";
    }
    return "unknown";
}

} // namespace ob
