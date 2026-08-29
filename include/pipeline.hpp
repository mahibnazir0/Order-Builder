#pragma once
// ============================================================================
// pipeline.hpp — The whole Milestone 1 run, as one callable unit.
//
// main() is only argument parsing; everything it does lives here so the
// end-to-end tests can exercise the real pipeline rather than a shell command.
//
// Order of operations:
//   read three files -> join -> convert -> validate -> summarise
// ============================================================================

#include "demand_types.hpp"
#include "joiner.hpp"
#include "placeholder_types.hpp"
#include "product_importer.hpp"
#include "placeholder_importer.hpp"
#include "reporter.hpp"
#include "validator.hpp"

#include <string>
#include <vector>

namespace ob {

struct PipelineInputs {
    std::string product_path;
    std::string demand_path;
    std::string placeholder_path;
    std::string planning_day;      // optional label for the report header

    ValidationConfig validation;   // thresholds, allowed UoM codes
};

// Everything the run produced. Held together so a caller (or a test) can
// inspect any stage without re-running the earlier ones.
struct PipelineResult {
    DemandFile             demand;
    ProductLoadResult      products;
    PlaceholderLoadResult  placeholders;
    ProductIndex           index;
    JoinResult             join;
    ValidationReport       validation;
    DaySummary             summary;

    std::vector<double> pallets_per_line;   // parallel to join.lines
    std::vector<double> weight_per_line;    // parallel to join.lines
};

class Pipeline {
public:
    // Run every stage in order.
    // Throws std::runtime_error if any input file cannot be read — bad data
    // inside a file is reported through result.validation, not by throwing.
    static PipelineResult run(const PipelineInputs& inputs);
};

} // namespace ob
