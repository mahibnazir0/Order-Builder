// ============================================================================
// main.cpp — Order Builder command-line entry point.
//
// Usage:
//   order_builder --product <csv> --demand <json> --placeholder <json>
//                 [--params <json> --demand-rule <rule>] [--pallets <csv>]
//                 [--trailer <code>] [--groups N] [--day <YYYY-MM-DD>] [--lanes N]
//                 [--select] [--record <dir>] [--debug] [--help]
//
// Reads one planning day, validates it, and prints a summary. With --params it
// also groups the demand, reports the stacks (Milestone 2) and prints a truck
// floor for each ship date the rule covers (Milestone 3). --params needs
// --demand-rule: which ship dates a run covers has no default. --select also fills each
// reserved truck (M4), with the settings in the params file's "selection" block.
//
// Without --params only Milestone 1 runs: the report then says no truck floor
// was computed and the run exits 6, so a forgotten --params is never read as a
// floor of nothing, by a person or by a script.
//
// Exit codes (when several apply, the lowest code wins):
//   0  ran successfully: every line validated, stacked and counted toward the floor
//   1  validation found errors
//   2  could not run (missing argument, unreadable file, invalid demand rule,
//      a calendar with no usable row for the plan date, a --groups or --lanes
//      value that is not a whole number of 0 or more, or --demand and
//      --placeholder naming the same file)
//   3  a line that passed validation is in no stack (no unit load, taller than
//      the ceiling, bad quantity), or no stack was built
//   4  a floor left out a line shipping on its date, or a line has no ship date
//   5  no line of a non-empty extract ships on the rule's dates
//   6  no floor was computed: --params was not given
//   7  M4 ran and its plan broke an invariant, or every method failed on a lane
// ============================================================================

#include "demandSelector.hpp"
#include "floorReporter.hpp"
#include "selectionReporter.hpp"
#include "logger.hpp"
#include "pipeline.hpp"

#include <exception>
#include <iostream>
#include <string>

using namespace std;

namespace {

void print_usage(ostream& out) {
    out <<
        "Order Builder - Milestones 1, 2 and 3\n"
        "\n"
        "Usage:\n"
        "  order_builder --product <csv> --demand <json> --placeholder <json>\n"
        "                [--params <json> --demand-rule <rule>] [--pallets <csv>]\n"
        "                [--trailer <code>] [--groups N] [--day <YYYY-MM-DD>] [--lanes N]\n"
        "                [--select] [--record <dir>] [--debug] [--help]\n"
        "\n"
        "Required:\n"
        "  --product <path>      Product master CSV\n"
        "  --demand <path>       Demand extract JSON (STR / CTL / DNM)\n"
        "  --placeholder <path>  Placeholder JSON (trucks per lane)\n"
        "\n"
        "Optional:\n"
        "  --params <path>       Params JSON; turns on the Milestone 2 groups and stacks report\n"
        "                        and the Milestone 3 truck floor. Needs --demand-rule\n"
        "  --demand-rule <rule>  Which ship dates (DATFR_TA) get a floor, each on its own; no default:\n"
        "                          calendar:YYYY-MM-DD    the dates the demand file's CTL calendar\n"
        "                                                 gives a plan made on that date\n"
        "                          shipDates:YYYY-MM-DD[:YYYY-MM-DD]  that date, or every date\n"
        "                                                 from the first to the last\n"
        "                          wholeExtract           every line as one floor (a check on the\n"
        "                                                 extract, not a daily floor)\n"
        "  --pallets <path>      Pallet table CSV (weight, height, footprint per pallet type);\n"
        "                        needed with --params when the master has no Pallet_* columns\n"
        "  --layer-sizes <path>  Product master to borrow Layer_Length/Layer_Width from, for a\n"
        "                        master without them (matched on ID and Pallet_ID)\n"
        "  --trailer <code>      Trailer code from the params file (default: the largest listed)\n"
        "  --groups N            Print only the N largest groups (0 or absent: all)\n"
        "  --day <date>          Planning day, shown in the report header\n"
        "  --lanes N             Print only the N largest lanes, in the summary and the\n"
        "                        floor by lane (0 or absent: all)\n"
        "  --select              Also fill each reserved truck (M4), with the params file's\n"
        "                        \"selection\" settings; needs --params\n"
        "  --record <dir>        Copy every input and the params file into <dir> with a\n"
        "                        manifest, so the run can be recreated from them\n"
        "  --debug               Verbose logging\n"
        "  --help                Show this message\n"
        "\n"
        "Exit codes (when several apply, the lowest code wins):\n"
        "  0  success: every line validated, stacked and counted toward the floor\n"
        "  1  validation errors\n"
        "  2  could not run\n"
        "  3  a line is in no stack (see Result)\n"
        "  4  a line was left out of a floor (section B) or has no ship date (section A)\n"
        "  5  no line ships on the rule's dates\n"
        "  6  no floor computed: --params was not given\n"
        "  7  M4 broke an invariant, or every method failed on a lane\n";
}

// Returns the value following `flag`, or an empty string if it is absent.
// Reports a missing value rather than reading past the end of argv.
bool take_value(int argc, char** argv, int& i, const char* flag, string& out) {
    if (i + 1 >= argc) {
        LOG_ERROR(string(flag) + " needs a value");
        return false;
    }
    out = argv[++i];
    return true;
}

// Reads the row count of --groups or --lanes. The whole value must be a whole number of 0 or
// more: "-3" or "5x" is an error, never quietly read as "all" or as 5.
bool parse_count(const char* flag, const string& value, int& out) {
    size_t consumed = 0;
    int parsed = 0;
    try {
        parsed = stoi(value, &consumed);
    } catch (const exception&) {
        consumed = 0;
    }
    if (consumed == 0 || consumed != value.size()) {
        LOG_ERROR(string(flag) + " needs a number, got '" + value + "'");
        return false;
    }
    if (parsed < 0) {
        LOG_ERROR(string(flag) + " must be 0 (all) or more, got '" + value + "'");
        return false;
    }
    out = parsed;
    return true;
}

void print_floor_not_computed(ostream& out) {
    out << "\n"
           "Truck floor: NOT COMPUTED. No --params was given, so only Milestone 1 ran: no\n"
           "groups, stacks or floor. Pass --params <json> --demand-rule <rule> for them.\n";
}

} // anonymous namespace


int main(int argc, char** argv) {
    ob::PipelineInputs inputs;
    int  max_lanes = 0;        // 0 = print every lane
    int  max_groups = 0;       // 0 = print every group
    bool debug     = false;
    string demandRuleText;
    bool demandRuleGiven = false;

    // ── Parse arguments ─────────────────────────────────────────────────────
    for (int i = 1; i < argc; ++i) {
        const string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(cout);
            return 0;
        } else if (arg == "--debug") {
            debug = true;
        } else if (arg == "--product") {
            if (!take_value(argc, argv, i, "--product", inputs.product_path)) return 2;
        } else if (arg == "--demand") {
            if (!take_value(argc, argv, i, "--demand", inputs.demand_path)) return 2;
        } else if (arg == "--placeholder") {
            if (!take_value(argc, argv, i, "--placeholder", inputs.placeholder_path)) return 2;
        } else if (arg == "--params") {
            if (!take_value(argc, argv, i, "--params", inputs.paramsPath)) return 2;
        } else if (arg == ob::kDemandRuleArgument) {
            if (!take_value(argc, argv, i, ob::kDemandRuleArgument, demandRuleText)) return 2;
            demandRuleGiven = true;
        } else if (arg == "--pallets") {
            if (!take_value(argc, argv, i, "--pallets", inputs.palletPath)) return 2;
        } else if (arg == "--layer-sizes") {
            if (!take_value(argc, argv, i, "--layer-sizes", inputs.layerSizePath)) return 2;
        } else if (arg == "--trailer") {
            if (!take_value(argc, argv, i, "--trailer", inputs.trailerCode)) return 2;
        } else if (arg == "--groups") {
            string value;
            if (!take_value(argc, argv, i, "--groups", value)) return 2;
            if (!parse_count("--groups", value, max_groups)) return 2;
        } else if (arg == "--select") {
            inputs.runSelection = true;
        } else if (arg == "--record") {
            if (!take_value(argc, argv, i, "--record", inputs.recordDirectory)) return 2;
        } else if (arg == "--day") {
            if (!take_value(argc, argv, i, "--day", inputs.planning_day)) return 2;
        } else if (arg == "--lanes") {
            string value;
            if (!take_value(argc, argv, i, "--lanes", value)) return 2;
            if (!parse_count("--lanes", value, max_lanes)) return 2;
        } else {
            LOG_ERROR("unrecognised option '" + arg + "'");
            print_usage(cerr);
            return 2;
        }
    }

    // ── Check the required arguments are present ────────────────────────────
    if (inputs.product_path.empty() || inputs.demand_path.empty()
        || inputs.placeholder_path.empty()) {
        LOG_ERROR("--product, --demand and --placeholder are all required");
        print_usage(cerr);
        return 2;
    }

    // The floor runs whenever --params does, and never on a defaulted rule: an absent rule
    // is parsed as empty so the error is the selector's own, naming the argument.
    if (!inputs.paramsPath.empty()) {
        try {
            inputs.demandSelector = ob::parseDemandSelector(demandRuleText);
        } catch (const exception& e) {
            LOG_ERROR(e.what());
            return 2;
        }
    } else if (demandRuleGiven) {
        LOG_ERROR(string(ob::kDemandRuleArgument) + " needs --params: the floor is planned "
                  "against the params file's trailer");
        return 2;
    }

    if (inputs.runSelection && inputs.paramsPath.empty()) {
        LOG_ERROR("--select needs --params: M4's settings are the params file's \"selection\" block");
        return 2;
    }

    ob::Logger::instance().set_debug(debug);
    if (!inputs.palletPath.empty() && inputs.paramsPath.empty()) {
        LOG_WARN("--pallets is used only by Milestones 2 and 3; without --params it is ignored");
    }

    // ── Run ─────────────────────────────────────────────────────────────────
    try {
        const ob::PipelineResult result = ob::Pipeline::run(inputs);

        ob::Reporter::print_summary(result.summary, cout, max_lanes);
        ob::Reporter::print_warnings(result.validation, cout);
        if (result.ranMilestone2) {
            ob::StackReporter::print(result.stackReport, cout,
                                     static_cast<size_t>(max_groups));
        }
        if (result.ranFloor) {
            ob::printFloorReport(cout, result.floorRun, result.params, result.trailer,
                                 result.trailerChoice, inputs.demand_path,
                                 static_cast<size_t>(max_lanes));
        } else {
            print_floor_not_computed(cout);
        }
        if (result.ranSelection) {
            ob::SelectionReportContext context;
            context.extractLabel = inputs.planning_day.empty() ? result.demand.request_id : inputs.planning_day;
            for (const string& path : {inputs.product_path, inputs.demand_path, inputs.placeholder_path,
                                       inputs.paramsPath, inputs.palletPath, inputs.layerSizePath}) {
                if (!path.empty()) context.inputFiles.push_back(path);
            }
            context.routing = result.selectionParams.routing;
            context.settings = result.selectionParams.selector;
            context.detail = result.selectionParams.reportDetail;
            context.warnings = result.selectionWarnings;
            ob::printSelectionReport(cout, result, result.reservationBook, result.selectionItems,
                                     result.laneClassification, result.selectionPlan, context);
        }

        return static_cast<int>(ob::runOutcome(result));

    } catch (const exception& e) {
        LOG_ERROR(e.what());
        return 2;
    }
}
