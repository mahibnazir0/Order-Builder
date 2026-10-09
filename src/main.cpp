// ============================================================================
// main.cpp — Order Builder command-line entry point.
//
// Usage:
//   order_builder --product <csv> --demand <json> --placeholder <json>
//                 [--params <json> --demand-rule <rule>] [--trailer <code>]
//                 [--groups N] [--day <YYYY-MM-DD>] [--lanes N] [--debug] [--help]
//
// Reads one planning day, validates it, and prints a summary. With --params it
// also groups the demand, reports the stacks (Milestone 2) and prints the
// truck floor for the demand the rule selects (Milestone 3). --params needs
// --demand-rule: which demand counts toward the day has no default.
//
// Without --params only Milestone 1 runs: the report then says no truck floor
// was computed, so a forgotten --params is never read as a floor of nothing.
//
// Exit codes:
//   0  ran successfully, no validation errors
//   1  ran, but the result does not cover all of the demand: validation found
//      errors, or (with --params) a line that passed validation is in no stack
//      (no unit load, taller than the ceiling, bad quantity) or no stack was built,
//      or the floor left out a selected line or could not judge a line's date,
//      or the demand rule selected no line of a non-empty extract
//   2  could not run (missing argument, unreadable file, invalid demand rule,
//      a --groups or --lanes value that is not a whole number of 0 or more)
// ============================================================================

#include "demandSelector.hpp"
#include "floorReporter.hpp"
#include "logger.hpp"
#include "pipeline.hpp"

#include <exception>
#include <iostream>
#include <string>

using namespace std;

namespace {

void print_usage(std::ostream& out) {
    out <<
        "Order Builder - Milestones 1, 2 and 3\n"
        "\n"
        "Usage:\n"
        "  order_builder --product <csv> --demand <json> --placeholder <json>\n"
        "                [--params <json> --demand-rule <rule>] [--trailer <code>]\n"
        "                [--groups N] [--day <YYYY-MM-DD>] [--lanes N] [--debug] [--help]\n"
        "\n"
        "Required:\n"
        "  --product <path>      Product master CSV\n"
        "  --demand <path>       Demand extract JSON (STR / CTL / DNM)\n"
        "  --placeholder <path>  Placeholder JSON (trucks per lane)\n"
        "\n"
        "Optional:\n"
        "  --params <path>       Params JSON; turns on the Milestone 2 groups and stacks report\n"
        "                        and the Milestone 3 truck floor. Needs --demand-rule\n"
        "  --demand-rule <rule>  Which demand counts toward the floor; no default:\n"
        "                          wholeExtract           every line in the file\n"
        "                          dueBy:YYYY-MM-DD       DATTO_TA on or before the date\n"
        "                          availableBy:YYYY-MM-DD DATFR_TA on or before the date\n"
        "                          window:YYYY-MM-DD:YYYY-MM-DD  DATTO_TA inside the window\n"
        "  --trailer <code>      Trailer code from the params file (default: the largest listed)\n"
        "  --groups N            Print only the N largest groups (0 or absent: all)\n"
        "  --day <date>          Planning day, shown in the report header\n"
        "  --lanes N             Print only the N largest lanes, in the summary and the\n"
        "                        floor by lane (0 or absent: all)\n"
        "  --debug               Verbose logging\n"
        "  --help                Show this message\n"
        "\n"
        "Exit codes:\n"
        "  0  success, no validation errors\n"
        "  1  incomplete: validation errors, or a line is in no stack (see Result),\n"
        "     or left out of the floor or undated under the demand rule, or the rule\n"
        "     selected no line at all (see section B)\n"
        "  2  could not run\n";
}

// Returns the value following `flag`, or an empty string if it is absent.
// Reports a missing value rather than reading past the end of argv.
bool take_value(int argc, char** argv, int& i, const char* flag, std::string& out) {
    if (i + 1 >= argc) {
        LOG_ERROR(std::string(flag) + " needs a value");
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
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(std::cout);
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
        } else if (arg == "--trailer") {
            if (!take_value(argc, argv, i, "--trailer", inputs.trailerCode)) return 2;
        } else if (arg == "--groups") {
            string value;
            if (!take_value(argc, argv, i, "--groups", value)) return 2;
            if (!parse_count("--groups", value, max_groups)) return 2;
        } else if (arg == "--day") {
            if (!take_value(argc, argv, i, "--day", inputs.planning_day)) return 2;
        } else if (arg == "--lanes") {
            string value;
            if (!take_value(argc, argv, i, "--lanes", value)) return 2;
            if (!parse_count("--lanes", value, max_lanes)) return 2;
        } else {
            LOG_ERROR("unrecognised option '" + arg + "'");
            print_usage(std::cerr);
            return 2;
        }
    }

    // ── Check the required arguments are present ────────────────────────────
    if (inputs.product_path.empty() || inputs.demand_path.empty()
        || inputs.placeholder_path.empty()) {
        LOG_ERROR("--product, --demand and --placeholder are all required");
        print_usage(std::cerr);
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
                  "against the params file's trailer and pallet specs");
        return 2;
    }

    ob::Logger::instance().set_debug(debug);

    // ── Run ─────────────────────────────────────────────────────────────────
    try {
        const ob::PipelineResult result = ob::Pipeline::run(inputs);

        ob::Reporter::print_summary(result.summary, std::cout, max_lanes);
        ob::Reporter::print_warnings(result.validation, std::cout);
        if (result.ranMilestone2) {
            ob::StackReporter::print(result.stackReport, std::cout,
                                     static_cast<size_t>(max_groups));
        }
        if (result.ranFloor) {
            ob::printFloorReport(cout, result.floorPlan, result.demandSelection, result.params,
                                 result.trailer, result.trailerChoice, inputs.demand_path,
                                 static_cast<size_t>(max_lanes));
        } else {
            print_floor_not_computed(cout);
        }

        return ob::isRunComplete(result) ? 0 : 1;

    } catch (const std::exception& e) {
        LOG_ERROR(e.what());
        return 2;
    }
}
