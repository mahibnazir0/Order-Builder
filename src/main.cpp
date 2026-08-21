// Order Builder CLI entry point.
//
// Milestone 1: argument parsing + usage message only. No module is wired
// up yet -- Importer/Converter/Validator/Joiner/Reporter are not called.

#include <iostream>
#include <string>

#include "logger.hpp"

namespace {

void print_usage(const std::string& program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  --product <path>      Path to the product master file\n"
              << "  --demand <path>       Path to the demand file (STR/CTL/DNM)\n"
              << "  --placeholder <path>  Path to the placeholder input file\n"
              << "  --day <value>         Day to build the order for\n"
              << "  --debug               Enable debug logging\n"
              << "  --help                Show this message\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string program_name = (argc > 0) ? argv[0] : "order_builder";

    std::string product_path;
    std::string demand_path;
    std::string placeholder_path;
    std::string day;
    bool debug = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto next_value = [&](const std::string& flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << "\n";
                return "";
            }
            return argv[++i];
        };

        if (arg == "--product") {
            product_path = next_value(arg);
        } else if (arg == "--demand") {
            demand_path = next_value(arg);
        } else if (arg == "--placeholder") {
            placeholder_path = next_value(arg);
        } else if (arg == "--day") {
            day = next_value(arg);
        } else if (arg == "--debug") {
            debug = true;
        } else if (arg == "--help") {
            print_usage(program_name);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(program_name);
            return 1;
        }
    }

    ob::Logger::instance().set_debug(debug);

    print_usage(program_name);

    LOG_INFO("Order Builder Milestone 1 skeleton -- no module logic wired up yet.");
    LOG_DEBUG("product=" + product_path + " demand=" + demand_path +
              " placeholder=" + placeholder_path + " day=" + day);

    return 0;
}
