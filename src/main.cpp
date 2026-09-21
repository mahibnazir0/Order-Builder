// ============================================================================
// main.cpp — Order Builder command-line entry point.
//
// Usage:
//   order_builder --product <csv> --demand <json> --placeholder <json>
//                 [--params <json>] [--trailer <code>] [--groups N]
//                 [--day <YYYY-MM-DD>] [--lanes N] [--debug] [--help]
//
// Reads one planning day, validates it, and prints a summary. With --params it
// also groups the demand and reports the stacks (Milestone 2). It does not
// decide truck counts.
//
// Exit codes:
//   0  ran successfully, no validation errors
//   1  ran successfully but validation found errors in the data
//   2  could not run (missing argument, unreadable file)
// ============================================================================

#include "logger.hpp"
#include "pipeline.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

namespace {

void print_usage(std::ostream& out) {
    out <<
        "Order Builder - Milestones 1 and 2\n"
        "\n"
        "Usage:\n"
        "  order_builder --product <csv> --demand <json> --placeholder <json>\n"
        "                [--params <json>] [--trailer <code>] [--groups N]\n"
        "                [--day <YYYY-MM-DD>] [--lanes N] [--debug] [--help]\n"
        "\n"
        "Required:\n"
        "  --product <path>      Product master CSV\n"
        "  --demand <path>       Demand extract JSON (STR / CTL / DNM)\n"
        "  --placeholder <path>  Placeholder JSON (trucks per lane)\n"
        "\n"
        "Optional:\n"
        "  --params <path>       Params JSON; turns on the Milestone 2 groups and stacks report\n"
        "  --trailer <code>      Trailer code from the params file (default: the first listed)\n"
        "  --groups N            Print only the N largest groups (default: all)\n"
        "  --day <date>          Planning day, shown in the report header\n"
        "  --lanes N             Print only the N largest lanes (default: all)\n"
        "  --debug               Verbose logging\n"
        "  --help                Show this message\n"
        "\n"
        "Exit codes:\n"
        "  0  success, no validation errors\n"
        "  1  success, but validation found errors\n"
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

} // anonymous namespace


int main(int argc, char** argv) {
    ob::PipelineInputs inputs;
    int  max_lanes = 0;        // 0 = print every lane
    int  max_groups = 0;       // 0 = print every group
    bool debug     = false;

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
        } else if (arg == "--trailer") {
            if (!take_value(argc, argv, i, "--trailer", inputs.trailerCode)) return 2;
        } else if (arg == "--groups") {
            std::string value;
            if (!take_value(argc, argv, i, "--groups", value)) return 2;
            try {
                max_groups = std::stoi(value);
            } catch (const std::exception&) {
                LOG_ERROR("--groups needs a number, got '" + value + "'");
                return 2;
            }
        } else if (arg == "--day") {
            if (!take_value(argc, argv, i, "--day", inputs.planning_day)) return 2;
        } else if (arg == "--lanes") {
            std::string value;
            if (!take_value(argc, argv, i, "--lanes", value)) return 2;
            try {
                max_lanes = std::stoi(value);
            } catch (const std::exception&) {
                LOG_ERROR("--lanes needs a number, got '" + value + "'");
                return 2;
            }
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

    ob::Logger::instance().set_debug(debug);

    // ── Run ─────────────────────────────────────────────────────────────────
    try {
        const ob::PipelineResult result = ob::Pipeline::run(inputs);

        ob::Reporter::print_summary(result.summary, std::cout, max_lanes);
        ob::Reporter::print_warnings(result.validation, std::cout);
        if (result.ranMilestone2) {
            ob::StackReporter::print(result.stackReport, std::cout,
                                     static_cast<std::size_t>(std::max(max_groups, 0)));
        }

        // A run that produced validation errors is reported, not hidden.
        return (result.validation.errors > 0) ? 1 : 0;

    } catch (const std::exception& e) {
        LOG_ERROR(e.what());
        return 2;
    }
}
