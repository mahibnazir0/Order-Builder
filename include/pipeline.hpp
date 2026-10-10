#pragma once
// ============================================================================
// pipeline.hpp — The whole Milestone 1 run, as one callable unit.
//
// main() is only argument parsing; everything it does lives here so the
// end-to-end tests can exercise the real pipeline rather than a shell command.
//
// Order of operations:
//   read three files -> join -> convert -> validate -> summarise
//   and, when a params file is given:
//   -> segregate -> pass 1 (binding constraint) -> pass 2 (stacks) -> stack report
//   and, when a demand rule is also given:
//   -> select demand -> floor (Milestone 3)
// Conversion precedes validation because the large-line check needs pallet figures.
// The floor runs last because it reads the groups segregation formed.
// ============================================================================

#include "demand_types.hpp"
#include "bindingConstraint.hpp"
#include "demandSelector.hpp"
#include "floorPlanner.hpp"
#include "joiner.hpp"
#include "paramsTypes.hpp"
#include "placeholder_types.hpp"
#include "segregationTypes.hpp"
#include "stackBuilder.hpp"
#include "stackReporter.hpp"
#include "product_importer.hpp"
#include "placeholder_importer.hpp"
#include "reporter.hpp"
#include "validator.hpp"

#include <optional>
#include <string>
#include <vector>

namespace ob {

struct PipelineInputs {
    std::string product_path;
    std::string demand_path;
    std::string placeholder_path;
    std::string planning_day;      // optional label for the report header

    ValidationConfig validation;   // thresholds, allowed UoM codes

    // Milestone 2 runs only when this is set; leave it empty for the M1 summary alone.
    std::string paramsPath;
    // Trailer to plan against, by params trailerCode; empty picks the largest one listed.
    std::string trailerCode;
    // Milestone 3 runs only when this is set, and needs paramsPath. Never defaulted: which
    // demand counts toward the day is open with the client (M3 question 1).
    std::optional<DemandSelector> demandSelector;
    // Pallet table (Customer2-Pallet-Data.csv). Optional when every demanded product row
    // carries its own pallet figures, as the 88-column masters do; required for older masters.
    std::string palletPath;
};

// Everything the run produced. Held together so a caller (or a test) can
// inspect any stage without re-running the earlier ones.
struct PipelineResult {
    PipelineResult() = default;

    // `join` (via JoinedLine) holds raw pointers into `demand` and `products`.
    // Copying this struct would duplicate those containers while the pointers
    // kept referencing the original's memory — a use-after-free the moment
    // the original is destroyed. Move-only sidesteps that instead of writing
    // a copy that has to rebuild every pointer against the new containers.
    PipelineResult(const PipelineResult&)            = delete;
    PipelineResult& operator=(const PipelineResult&) = delete;
    PipelineResult(PipelineResult&&)                 = default;
    PipelineResult& operator=(PipelineResult&&)      = default;

    DemandFile             demand;
    ProductLoadResult      products;
    PlaceholderLoadResult  placeholders;
    ProductIndex           index;
    JoinResult             join;
    ValidationReport       validation;
    DaySummary             summary;

    std::vector<double> pallets_per_line;   // parallel to join.lines
    std::vector<double> weight_per_line;    // parallel to join.lines

    // Milestone 2. Empty unless PipelineInputs::paramsPath was set.
    bool ranMilestone2 = false;
    M2Params params;
    // Pallet types in the demand whose figures neither the master nor the pallet table gives.
    std::vector<std::string> missingPalletIds;
    // pallets_per_line with validator-excluded lines zeroed, so segregation's downstream
    // passes see the same lines the M1 totals do. weightForStacking is zeroed the same way,
    // but weighs each pallet with the supplied pallet figures, as the stacks do, rather than
    // the fixed pallet weight behind weight_per_line.
    std::vector<double> palletsForStacking;
    std::vector<double> weightForStacking;
    SegregationResult segregation;
    BindingResult binding;
    StackingResult stacking;
    StackReport stackReport;
    // The trailer M2 and the floor were planned against, and how it was chosen.
    TrailerSpec trailer;
    TrailerChoice trailerChoice = TrailerChoice::Largest;

    // Milestone 3. Empty unless PipelineInputs::demandSelector was set.
    bool ranFloor = false;
    DemandSelection demandSelection;
    FloorPlan floorPlan;
};

// True when the run covered all of its demand: no validation errors; when M2 ran, every
// demand line ended up in a stack; and when the floor ran, the demand rule selected at least
// one line of a non-empty extract, could judge every line's date, and no selected line was
// left out of the floor. main() exits 1 otherwise.
bool isRunComplete(const PipelineResult& result);

class Pipeline {
public:
    // Run every stage in order.
    // Throws std::runtime_error if any input file cannot be read, or if a demand rule is
    // given without a params file — bad data inside a file is reported through
    // result.validation, not by throwing.
    static PipelineResult run(const PipelineInputs& inputs);
};

} // namespace ob
