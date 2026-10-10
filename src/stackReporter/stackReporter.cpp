#include "stackReporter.hpp"

#include "reportFormat.hpp"

#include <algorithm>
#include <ostream>
#include <stdexcept>

using namespace std;

namespace ob {
namespace {

constexpr size_t kLinesListed = 20;

const char* methodName(StackMethod method) {
    switch (method) {
    case StackMethod::Natural: return "Natural";
    case StackMethod::Target: return "Target";
    case StackMethod::TallAndHeavy: return "Tall & Heavy";
    case StackMethod::BaseAndTop: return "Base & Top";
    case StackMethod::TryHard: return "Try Hard";
    }
    return "?";
}

string streamLabel(const GroupKey& key) {
    if (!key.isSegregated) return "normal";
    return key.segregant.empty() ? "flagged (all planners)" : key.segregant;
}

string stackHeightSummary(const map<size_t, double>& palletsByStackHeight) {
    string text;
    for (const auto& [height, pallets] : palletsByStackHeight) {
        if (!text.empty()) text += ", ";
        text += to_string(height) + "-high " + fixed(pallets, 1);
    }
    return text.empty() ? "-" : text;
}

string joined(const vector<string>& values) {
    string text;
    for (const auto& value : values) text += (text.empty() ? "" : ", ") + value;
    return text;
}

void printLineList(const vector<ReportedLine>& lines, const string& whereTheRestAre, ostream& out) {
    const size_t listed = min(lines.size(), kLinesListed);
    for (size_t i = 0; i < listed; ++i) {
        out << "      line " << lines[i].lineIndex << ", material " << lines[i].matnr << ": "
            << lines[i].reason << "\n";
    }
    if (listed < lines.size()) {
        out << "      ... and " << (lines.size() - listed) << " more (" << whereTheRestAre << ")\n";
    }
}

void printProvisionalRules(const StackReport& report, ostream& out) {
    out << "----------------------------------------------------------------\n";
    out << " PROVISIONAL BUSINESS RULES (not yet confirmed by the customer)\n";
    out << "----------------------------------------------------------------\n";
    out << "  Pallet variant            " << grouped(static_cast<double>(report.ambiguousPalletLines))
        << " line(s) chose a pallet type by preference order ("
        << joined(Joiner::default_pallet_preference()) << ")\n";
    out << "  Do-not-mix reading        "
        << (report.doNotMixReading == SegregationReading::Strict
                ? "Strict: each flagged planner kept apart from other planners and normal stock\n"
                : "FlaggedVsNormal: flagged planners share groups, kept apart from normal stock\n");
    out << "  Stacking methods          Order Builder's reading of the T3 method names;"
           " equivalence to T3 not demonstrated\n";
    out << "  Stacking basis            "
        << (report.stackWholePallets
                ? "whole pallets (stackWholePallets = true)\n"
                : "fractional estimate, not a physical count (stackWholePallets = false)\n");
    out << "  Deck height               "
        << (report.deckHeight == DeckHeightRule::Included
                ? "added to unit-load height (floorDeckHeight = Included), as in the floor\n\n"
                : "not added to unit-load height (floorDeckHeight = Excluded), as in the floor\n\n");
}

} // namespace

StackReport StackReporter::build(const SegregationResult& segregation, const BindingResult& binding,
                                 const StackingResult& stacking, const M2Params& params) {
    if (binding.groups.size() != segregation.groups.size()
        || stacking.groups.size() != segregation.groups.size()) {
        throw invalid_argument("stackReporter: results do not describe the same groups");
    }

    StackReport report;
    report.groups = segregation.groups.size();
    report.lanes = segregation.lanesIn;
    report.lanesSplit = segregation.lanesSplit;
    report.linesSegregated = segregation.linesSegregated;
    report.cubeBoundGroups = binding.cubeBoundGroups;
    report.weightBoundGroups = binding.weightBoundGroups;
    report.linesExcludedByValidator = segregation.linesExcluded;
    report.excludedLines = stacking.excludedLines.size();
    report.overHeightLines = stacking.overHeightLines.size();
    report.invalidQuantityLines = stacking.invalidQuantityLines.size();
    report.zeroQuantityLines = stacking.zeroQuantityLines.size();
    report.ownCriExceededLines = stacking.ownCriExceededLines.size();
    report.linesNotStacked = stacking.linesNotStacked();
    report.doNotMixReading = params.doNotMixReading;
    report.stackWholePallets = params.stackWholePallets;
    report.deckHeight = params.floorDeckHeight;
    report.defaultedKeys = params.defaultedKeys;
    report.paramWarnings = params.warnings;
    report.rows.reserve(report.groups);

    for (size_t i = 0; i < report.groups; ++i) {
        const GroupKey& key = segregation.groups[i].key;
        const StackSet& best = stacking.groups[i].best;
        StackReportRow row;
        row.lane = key.locationFrom + " -> " + key.locationTo + " " + key.shipCondition;
        row.stream = streamLabel(key);
        row.demandLines = segregation.groups[i].lineIndices.size();
        row.pallets = binding.groups[i].totalPallets;
        row.weightLb = binding.groups[i].totalWeightLb;
        row.binding = binding.groups[i].binding;
        row.method = best.method;
        row.floorPositions = best.floorPositions;
        for (const auto& stack : best.stacks) {
            const double palletsInStack = stack.quantity * static_cast<double>(stack.lineIndices.size());
            row.palletsByStackHeight[stack.lineIndices.size()] += palletsInStack;
            report.totalPalletsStacked += palletsInStack;
        }
        report.totalPallets += row.pallets;
        report.totalFloorPositions += row.floorPositions;
        if (row.palletsByStackHeight.size() == 1 && row.palletsByStackHeight.count(1) == 1) {
            ++report.groupsAllSingleHigh;
        }
        report.rows.push_back(std::move(row));
    }
    const bool demandReachedStacking = segregation.linesIn + segregation.linesExcluded > 0;
    report.builtNoStacks = demandReachedStacking && report.totalPalletsStacked == 0.0;
    stable_sort(report.rows.begin(), report.rows.end(),
                     [](const StackReportRow& a, const StackReportRow& b) {
                         return a.floorPositions > b.floorPositions; });
    return report;
}

void StackReporter::print(const StackReport& report, ostream& out, size_t maxRows) {
    out << "\n================================================================\n";
    out << " ORDER BUILDER - MILESTONE 2 GROUPS AND STACKS\n";
    out << "================================================================\n\n";

    // Printed even when empty, so a misspelled config key shows up as a visible line.
    out << "  Config keys defaulted     ";
    if (report.defaultedKeys.empty()) out << "none";
    for (size_t i = 0; i < report.defaultedKeys.size(); ++i) {
        out << (i ? ", " : "") << report.defaultedKeys[i];
    }
    out << "\n";
    for (const auto& warning : report.paramWarnings) out << "  Config warning            " << warning << "\n";
    out << "\n";

    out << "  Lanes                     " << grouped(static_cast<double>(report.lanes)) << "\n";
    out << "    split into groups       " << grouped(static_cast<double>(report.lanesSplit)) << "\n";
    out << "  Groups                    " << grouped(static_cast<double>(report.groups)) << "\n";
    out << "  Lines segregated          " << grouped(static_cast<double>(report.linesSegregated)) << "\n";
    out << "  Cube-bound groups         " << grouped(static_cast<double>(report.cubeBoundGroups)) << "\n";
    out << "  Weight-bound groups       " << grouped(static_cast<double>(report.weightBoundGroups)) << "\n";
    out << "  Pallet-equivalents        " << grouped(report.totalPallets, 1) << "\n";
    out << "  Pallets in stacks         " << grouped(report.totalPalletsStacked, 1)
        << (report.stackWholePallets ? "  (each line rounded up to whole pallets)\n"
                                     : "  (fractional pallet-equivalents)\n");
    out << "  Floor positions after stacking  " << grouped(report.totalFloorPositions, 1) << "\n";
    out << "  Lines rejected            "
        << grouped(static_cast<double>(report.linesExcludedByValidator))
        << "  (validation errors)\n";
    out << "  Excluded lines            " << grouped(static_cast<double>(report.excludedLines))
        << "  (no unit load)\n";
    out << "  Over-height lines         " << grouped(static_cast<double>(report.overHeightLines))
        << "  (one pallet exceeds the trailer ceiling)\n";
    out << "  Excluded quantities       "
        << grouped(static_cast<double>(report.invalidQuantityLines))
        << "  (negative or non-finite)\n";
    out << "  Zero-pallet lines         " << grouped(static_cast<double>(report.zeroQuantityLines))
        << "  (quantity converts to no pallets)\n";
    out << "  Over own CRI lines        " << grouped(static_cast<double>(report.ownCriExceededLines))
        << "  (warning: product cannot support its own build; shipped single-high)\n";
    printLineList(report.ownCriExceeded, "not listed individually", out);
    out << "  Result                    ";
    if (!report.unstackedLines.empty()) {
        out << "INCOMPLETE: " << grouped(static_cast<double>(report.unstackedLines.size()))
            << " demand line(s) are in no stack\n";
        printLineList(report.unstackedLines, "every line is in the log", out);
        out << "\n";
    } else if (report.linesNotStacked > 0) {
        out << "INCOMPLETE: " << grouped(static_cast<double>(report.linesNotStacked))
            << " line(s) that passed validation are in no stack\n\n";
    } else if (report.builtNoStacks) {
        out << "INCOMPLETE: demand was supplied but no stack was built\n\n";
    } else {
        out << "complete: every demand line is in a stack\n\n";
    }

    if (report.groups > 0 && report.groupsAllSingleHigh == report.groups) {
        out << "  No group could stack anything: every group ships single-high.\n\n";
    } else {
        out << "  " << grouped(static_cast<double>(report.groupsAllSingleHigh)) << " of "
            << grouped(static_cast<double>(report.groups))
            << " groups ship entirely single-high. Most goods do: height blocks stacking first.\n\n";
    }

    printProvisionalRules(report, out);

    out << "----------------------------------------------------------------\n";
    out << " PER-GROUP SUMMARY";
    const size_t shown = (maxRows > 0 && maxRows < report.rows.size()) ? maxRows : report.rows.size();
    if (shown < report.rows.size()) out << "  (top " << shown << " of " << report.rows.size() << " by floor use)";
    out << "\n----------------------------------------------------------------\n";

    pad_right(out, "Lane", 20);
    pad_right(out, "Stream", 24);
    pad_left(out, "Lines", 7);
    pad_left(out, "Pallets", 10);
    pad_left(out, "Weight(lb)", 12);
    pad_right(out, "  Limit", 9);
    pad_right(out, "Method", 14);
    pad_left(out, "Floor", 9);
    out << "  Stacks\n";
    for (size_t i = 0; i < shown; ++i) {
        const StackReportRow& row = report.rows[i];
        pad_right(out, row.lane, 20);
        pad_right(out, row.stream, 24);
        pad_left(out, grouped(static_cast<double>(row.demandLines)), 7);
        pad_left(out, grouped(row.pallets, 1), 10);
        pad_left(out, grouped(row.weightLb), 12);
        pad_right(out, row.binding == BindingConstraint::Cube ? "  Cube" : "  Weight", 9);
        pad_right(out, methodName(row.method), 14);
        pad_left(out, grouped(row.floorPositions, 1), 9);
        out << "  " << stackHeightSummary(row.palletsByStackHeight) << "\n";
    }
    if (shown < report.rows.size()) out << "  ... " << (report.rows.size() - shown) << " more groups\n";
}

} // namespace ob
