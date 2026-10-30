#include "doctest.h"
#include "../m4AugustFixtures.hpp"
#include "../m4SyntheticDay.hpp"
#include "selectionReporter.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;
using namespace m4SyntheticDay;

namespace {

SelectionReportContext contextFor(ReportDetail detail = ReportDetail::Standard) {
    SelectionReportContext context;
    context.extractLabel = "test day";
    context.inputFiles = {"demand.json", "placeholder.json"};
    context.routing = kRouting;
    context.settings = selectorSettings();
    context.detail = detail;
    return context;
}

string reportOf(const Day& day, const SelectionPlan& selection, const SelectionReportContext& context = contextFor()) {
    ostringstream out;
    printSelectionReport(out, day.run, day.book, day.items, day.classification, selection, context);
    return out.str();
}

bool plainAscii(const string& text) {
    return all_of(text.begin(), text.end(), [](char character) {
        const auto code = static_cast<unsigned char>(character);
        return character == '\n' || (code >= 0x20 && code <= 0x7e);
    });
}

vector<string> linesOf(const string& text) {
    vector<string> lines;
    istringstream in(text);
    for (string line; getline(in, line);) lines.push_back(line);
    return lines;
}

// A light lane that fills by floor, a heavy one that fills by weight, a split lane, and a
// lane with trucks reserved and nothing to load.
Day mixedDay() {
    vector<SelectionItem> items = repeated(40, 0, 0, 500.0);
    const vector<SelectionItem> heavy = repeated(12, 1, 1, 7000.0);
    const vector<SelectionItem> split = repeated(4, 3, 3);
    items.insert(items.end(), heavy.begin(), heavy.end());
    items.insert(items.end(), split.begin(), split.end());
    const vector<SelectionItem> splitOther = repeated(4, 4, 4);
    items.insert(items.end(), splitOther.begin(), splitOther.end());
    return dayOf({group("2027"), group("2031"), group("2040"), group("2028"),
                  group("2028", SplitReason::DoNotMix, "S01")},
                 5, items,
                 {reserved("2027", 2), reserved("2031", 2), reserved("2040", 3), reserved("2028", 2)});
}

} // anonymous namespace

TEST_CASE("selectionReporter: specReportListComplete - every section, in order") {
    const Day day = mixedDay();
    const string report = reportOf(day, plan(day));
    size_t position = 0;
    for (const string& section : selectionReportSections()) {
        const size_t found = report.find(section, position);
        REQUIRE(found != string::npos);
        position = found;
    }
}

TEST_CASE("selectionReporter: penaltyExposurePrinted - per lane-date with its reason, per lane, per day") {
    const Day day = mixedDay();
    const SelectionPlan selection = plan(day);
    REQUIRE(selection.penaltyExposureTrucks == 3);
    const string report = reportOf(day, selection);
    CHECK(report.find("2040 -> 2528 TL") != string::npos);
    CHECK(report.find("nothing to load") != string::npos);
    CHECK(report.find("Penalty exposure by lane:") != string::npos);
    CHECK(report.find("Penalty exposure for the day: 3 reserved trucks unused") != string::npos);
}

TEST_CASE("selectionReporter: a reservation blocked by a rule says which") {
    // Stacks 102 x 101 in fit the floor area but stand in no row of a 100 in trailer, so the
    // floor check takes them all off and both reserved trucks go unused.
    vector<SelectionItem> items;
    for (int index = 0; index < 3; ++index) items.push_back(item(0, 0, 1000.0, 0, 102.0, 101.0));
    const Day day = dayOf({group("2027")}, 1, items, {reserved("2027", 2)});
    const SelectionPlan selection = plan(day);
    REQUIRE(selection.penaltyExposureTrucks == 2);
    CHECK(reportOf(day, selection).find("blocked: goods left over (floor eviction 3)") != string::npos);
}

TEST_CASE("selectionReporter: a day with every reservation used says so") {
    const Day day = dayOf({group("2027")}, 1, repeated(31, 0, 0), {reserved("2027", 1)});
    CHECK(reportOf(day, plan(day)).find("Every reservation was used.") != string::npos);
}

TEST_CASE("selectionReporter: boundLabelMatchesMeasurement") {
    const Day day = mixedDay();
    const SelectionPlan selection = plan(day);
    const string report = reportOf(day, selection);
    for (const string& line : linesOf(report)) {
        if (line.find("2031 -> 2528 TL") != string::npos && line.find("solved") != string::npos) {
            CHECK(line.find("weight (") != string::npos);
        }
        if (line.find("2027 -> 2528 TL") != string::npos && line.find("solved") != string::npos) {
            CHECK(line.find("floor (") != string::npos);
        }
    }
    CHECK(selection.routes[0].binding == BindingLimit::FloorArea);
    CHECK(selection.routes[1].binding == BindingLimit::Weight);
}

TEST_CASE("selectionReporter: patternNamedPerTruck, with rear waste, cube and weight against their lines") {
    const Day day = mixedDay();
    const SelectionPlan selection = plan(day);
    const string report = reportOf(day, selection);
    const size_t trucks = report.find(selectionReportSections()[5]);
    const size_t after = report.find(selectionReportSections()[6]);
    REQUIRE(trucks != string::npos);
    size_t truckLines = 0;
    for (const string& line : linesOf(report.substr(trucks, after - trucks))) {
        if (line.find(" -> ") == string::npos) continue;
        ++truckLines;
        CHECK((line.find("wide-wide") != string::npos || line.find("single") != string::npos
               || line.find("pinwheel") != string::npos || line.find("narrow") != string::npos
               || line.find("three-across") != string::npos));
        CHECK(line.find('%') != string::npos);
    }
    CHECK(truckLines == selection.trucks.size());
    CHECK(report.find("rear waste in") != string::npos);
    CHECK(report.find("full at 84.0%") != string::npos);
}

TEST_CASE("selectionReporter: each group of a split lane is listed on its own") {
    const Day day = mixedDay();
    const string report = reportOf(day, plan(day));
    size_t normal = 0, segregated = 0;
    for (const string& line : linesOf(report)) {
        if (line.find("2028 -> 2528 TL") == string::npos || line.find("solved") == string::npos) continue;
        if (line.find("group S01") != string::npos) {
            ++segregated;
        } else {
            ++normal;
        }
    }
    CHECK(normal == 1);
    CHECK(segregated == 1);
}

TEST_CASE("selectionReporter: fallbackNamed and unproven marked") {
    const Day day = dayOf({group("2027")}, 1, repeated(40, 0, 0), {reserved("2027", 2)});
    SelectorSettings settings = selectorSettings();
    settings.patternPacker.weightGrainLb = 0.0;
    SelectionReportContext context = contextFor();
    context.settings = settings;
    const string report = reportOf(day, plan(day, settings), context);
    CHECK(report.find("patternPacker threw:") != string::npos);
    CHECK(report.find("binPacker used") != string::npos);


    // One knapsack node cannot prove a 40-stack truck, so the result is kept and marked.
    settings = selectorSettings();
    settings.knapsack.nodeLimit = 1;
    const Day oneTruck = dayOf({group("2027")}, 1, repeated(40, 0, 0), {reserved("2027", 1)});
    const SelectionPlan unproven = plan(oneTruck, settings);
    REQUIRE(unproven.routes[0].methodUsed == PackingMethod::Knapsack);
    REQUIRE_FALSE(unproven.routes[0].proven);
    context.settings = settings;
    CHECK(reportOf(oneTruck, unproven, context).find("unproven") != string::npos);
}

TEST_CASE("selectionReporter: plainAsciiOnly - text from the files is cleaned, nothing cut short") {
    const string longOrigin = "2027-" + string(60, 'X') + "\xe2\x80\x94";
    vector<SelectionItem> items = repeated(6, 0, 0);
    const Day day = dayOf({group(longOrigin)}, 1, items, {reserved(longOrigin, 1)});
    SelectionReportContext context = contextFor(ReportDetail::Detailed);
    context.extractLabel = "Aug \xe2\x80\x94 4";
    context.inputFiles = {"Demand-\xc3\xa9.json"};
    const string report = reportOf(day, plan(day), context);
    CHECK(plainAscii(report));
    CHECK(report.find("2027-" + string(60, 'X') + "???") != string::npos);
}

TEST_CASE("selectionReporter: built over reserved cannot happen, so nothing is printed") {
    const Day day = dayOf({group("2027")}, 1, repeated(10, 0, 0), {reserved("2027", 1)});
    SelectionPlan selection = plan(day);
    selection.reservations[0].built = 2;
    ostringstream out;
    CHECK_THROWS_AS(printSelectionReport(out, day.run, day.book, day.items, day.classification, selection, contextFor()),
                    invalid_argument);
    CHECK(out.str().empty());
}

TEST_CASE("selectionReporter: detailedLoggingNeverChangesResult - detailed adds lines, the plan is the same") {
    const Day day = mixedDay();
    const SelectionPlan selection = plan(day);
    const string standard = reportOf(day, selection);
    const string detailed = reportOf(day, selection, contextFor(ReportDetail::Detailed));
    const SelectionPlan again = plan(day);
    REQUIRE(again.trucks.size() == selection.trucks.size());
    for (size_t truck = 0; truck < again.trucks.size(); ++truck) CHECK(again.trucks[truck].items == selection.trucks[truck].items);
    const vector<string> detailedLines = linesOf(detailed);
    for (const string& line : linesOf(standard)) {
        if (line.find("detail") != string::npos) continue;
        CHECK(find(detailedLines.begin(), detailedLines.end(), line) != detailedLines.end());
    }
    CHECK(detailedLines.size() > linesOf(standard).size());
}

TEST_CASE("selectionReporter: an August day prints in plain ASCII with its totals"
          * doctest::skip(!m4AugustTests::augustPresent())) {
    const m4AugustTests::AugustItems& august = m4AugustTests::augustItems(0);
    const LaneClassification classification = classifyLanes(august.run, august.book, august.built, kRouting);
    SelectorSettings settings = selectorSettings();
    settings.knapsack = {0.001, 0.001, 200'000};
    settings.patternPacker = {0.001, 0.001, 200, 200'000};
    settings.binPacker = {0.001, 0.001, 50, 100'000, 2'000, 100};
    settings.secondsPerLane = 60.0;
    const SelectionPlan selection = selectLoads(august.run, august.book, august.built, classification, settings);
    SelectionReportContext context = contextFor(ReportDetail::Detailed);
    context.settings = settings;
    ostringstream out;
    printSelectionReport(out, august.run, august.book, august.built, classification, selection, context);
    const string report = out.str();
    CHECK(plainAscii(report));
    CHECK(report.find("trucks built                  " + to_string(selection.trucks.size())) != string::npos);
    CHECK(report.find("Penalty exposure for the day: " + to_string(selection.penaltyExposureTrucks)) != string::npos);
    CHECK(report.find("grains 0.001 lb, 0.001 sq in") != string::npos);
}
