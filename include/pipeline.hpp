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
//   -> cover the ship dates -> a floor per ship date, matched to its placeholders (Milestone 3)
// Conversion precedes validation because the large-line check needs pallet figures.
// The floor runs last because it reads the groups segregation formed.
// ============================================================================

#include "demand_types.hpp"
#include "benchmarkHarness.hpp"
#include "bindingConstraint.hpp"
#include "demandSelector.hpp"
#include "floorPlanner.hpp"
#include "joiner.hpp"
#include "paramsTypes.hpp"
#include "placeholder_types.hpp"
#include "segregationTypes.hpp"
#include "selectionParams.hpp"
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
    // Milestone 3 runs only when this is set, and needs paramsPath. Never defaulted: the
    // floor is planned per ship date, and which dates a run covers comes from the upstream
    // calendar or the caller, never from a guess.
    std::optional<DemandSelector> demandSelector;
    // Pallet table (Customer2-Pallet-Data.csv). Optional when every demanded product row
    // carries its own pallet figures, as the 88-column masters do; required for older masters.
    std::string palletPath;
    // A product master to borrow layer sizes from, for a master that has no Layer_* columns
    // (17 Aug and 4-6 Aug). Fills only products with no layer size of their own, matched on
    // ID and Pallet_ID. Optional; the floor falls back to the pallet's footprint without it.
    std::string layerSizePath;
    // M4 runs only when this is set, and needs paramsPath with a "selection" block.
    bool runSelection = false;
    // When set, every input file and the params file are copied here first, with a manifest,
    // so the solve can be recreated from what was recorded.
    std::string recordDirectory;
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

    // Products given a layer size from PipelineInputs::layerSizePath.
    std::size_t layerSizesBorrowed = 0;

    // M4. Empty unless PipelineInputs::runSelection was set.
    bool ranSelection = false;
    SelectionParams selectionParams;
    ReservationBook reservationBook;
    SelectionItems selectionItems;
    LaneClassification laneClassification;
    SelectionPlan selectionPlan;
    // Recounted from the trucks after the plan is made; any entry fails the run (exit 7).
    std::vector<InvariantViolation> selectionViolations;
    // What the run found and planned around rather than stopped on, e.g. a combo lane.
    std::vector<std::string> selectionWarnings;

    // Milestone 3. Empty unless PipelineInputs::demandSelector was set.
    bool ranFloor = false;
    FloorRun floorRun;
};

// How a run that finished turned out; main() exits with the value, so each shortfall has a
// code of its own. 2 is main's "could not run" and is never returned here. When several
// shortfalls hold, the lowest code wins.
enum class RunOutcome {
    Complete = 0,
    ValidationErrors = 1,
    // Milestone 2 ran and a line that passed validation is in no stack, or no stack was built.
    LinesNotStacked = 3,
    // A floor left out a selected line, or a line has no ship date to be counted on.
    FloorIncomplete = 4,
    // No line of a non-empty extract ships on the rule's dates: usually a mistyped date.
    NothingSelected = 5,
    // No floor was planned, so the run says nothing about trucks.
    FloorNotComputed = 6,
    // M4 ran and its plan broke an invariant, or a lane had every method fail.
    SelectionIncomplete = 7,
};

RunOutcome runOutcome(const PipelineResult& result);

class Pipeline {
public:
    // Run every stage in order.
    // Throws std::runtime_error if any input file cannot be read, or if a demand rule is
    // given without a params file — bad data inside a file is reported through
    // result.validation, not by throwing.
    static PipelineResult run(const PipelineInputs& inputs);
};

} // namespace ob
