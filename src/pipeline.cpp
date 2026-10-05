#include "pipeline.hpp"
#include "converter.hpp"
#include "importer.hpp"
#include "logger.hpp"
#include "paramsLoader.hpp"
#include "reportFormat.hpp"
#include "segregation.hpp"
#include "stackRules.hpp"

#include <stdexcept>

namespace ob {

namespace {

const TrailerSpec& selectTrailer(const M2Params& params, const std::string& trailerCode) {
    if (params.trailers.empty()) throw std::runtime_error("params file lists no trailers");
    if (trailerCode.empty()) return params.trailers.front();
    for (const auto& trailer : params.trailers) {
        if (trailer.trailerCode == trailerCode) return trailer;
    }
    throw std::runtime_error("params file has no trailer '" + trailerCode + "'");
}

std::vector<double> zeroExcluded(const std::vector<double>& figures, const std::vector<bool>& excluded) {
    std::vector<double> kept = figures;
    for (std::size_t i = 0; i < kept.size(); ++i) {
        if (excluded[i]) kept[i] = 0.0;
    }
    return kept;
}

// M1 weights use the Converter's fixed wood-pallet weight so the published M1 totals stay
// reproducible. M2 must weigh a pallet exactly as buildUnitLoad does, from the configured
// pallet spec, or binding and reporting disagree with the stacks. A line with no buildable
// unit load keeps its M1 weight: it still occupies the trailer, and that is the only
// estimate there is for it.
std::vector<double> configuredWeightPerLine(const std::vector<JoinedLine>& lines,
                                            const std::vector<double>& palletsForStacking,
                                            const std::vector<double>& m1WeightForStacking,
                                            const M2Params& params) {
    std::vector<double> weights = m1WeightForStacking;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (palletsForStacking[i] == 0.0) continue;
        const UnitLoad load = buildUnitLoad(lines[i], params);
        if (load.error == UnitLoadError::None) weights[i] = palletsForStacking[i] * load.weightLb;
    }
    return weights;
}

std::string unitLoadErrorText(UnitLoadError error, const JoinedLine& line) {
    switch (error) {
    case UnitLoadError::MissingProduct: return "product is not in the master";
    case UnitLoadError::MissingPalletSpec:
        return "params file has no spec for pallet type '" + line.product->pallet_id + "'";
    case UnitLoadError::InvalidData:
        return "product height, weight, layer, case or strength data cannot form a unit load";
    case UnitLoadError::None: break;
    }
    return "unknown";
}

std::string zeroPalletReason(const JoinedLine& line) {
    if (!Converter::isConvertibleUom(line.str->unitofmeas)) {
        return "unit of measure '" + line.str->unitofmeas
             + "' cannot be converted to pallets (supported: CS, PAL, DIS)";
    }
    return "quantity converts to 0 pallets";
}

// Every joined line must end up in a stack or on this list with a reason; the list is built
// from which lines the stacks actually hold, so a line dropped anywhere upstream (validator,
// segregation, conversion, stacking) cannot pass unreported.
std::vector<UnstackedLine> unstackedLines(const PipelineResult& result, const TrailerSpec& trailer) {
    const auto& lines = result.join.lines;
    std::vector<std::string> reasons(lines.size());
    for (const auto& issue : result.validation.issues) {
        if (issue.line_index < 0 || static_cast<std::size_t>(issue.line_index) >= lines.size()) continue;
        std::string& reason = reasons[static_cast<std::size_t>(issue.line_index)];
        if (reason.empty() && Validator::excludesLine(issue)) {
            reason = "rejected by validation (" + issue.rule + "): " + issue.message;
        }
    }
    const StackingResult& stacking = result.stacking;
    for (const auto& excludedLine : stacking.excludedLines) {
        reasons[excludedLine.lineIndex] = unitLoadErrorText(excludedLine.error, lines[excludedLine.lineIndex]);
    }
    for (const std::size_t lineIndex : stacking.overHeightLines) {
        reasons[lineIndex] = "one pallet is taller than the trailer's "
                           + fixed(trailer.stackHeightCeilingIn, 0) + " in ceiling";
    }
    for (const std::size_t lineIndex : stacking.invalidQuantityLines) {
        reasons[lineIndex] = "pallet quantity is negative or not a finite number";
    }
    for (const std::size_t lineIndex : stacking.zeroQuantityLines) {
        reasons[lineIndex] = zeroPalletReason(lines[lineIndex]);
    }

    const std::vector<bool> stacked = stackedLineFlags(stacking, lines.size());
    std::vector<UnstackedLine> unstacked;
    for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        if (stacked[lineIndex]) continue;
        std::string reason = reasons[lineIndex].empty() ? "is in no stack" : std::move(reasons[lineIndex]);
        unstacked.push_back({lineIndex, lines[lineIndex].str->matnr, std::move(reason)});
    }
    return unstacked;
}

void runMilestone2(const PipelineInputs& inputs, PipelineResult& result) {
    result.params = loadParams(inputs.paramsPath);
    const TrailerSpec trailer = selectTrailer(result.params, inputs.trailerCode);

    result.missingPalletIds = missingPalletIds(result.join.lines, result.params);
    for (const auto& palletId : result.missingPalletIds) {
        LOG_WARN("Demand uses pallet type '" + palletId + "' but the params file has no spec for it");
    }

    const std::vector<bool> excluded =
        Validator::excludedLineFlags(result.validation, result.join.lines.size());
    result.palletsForStacking = zeroExcluded(result.pallets_per_line, excluded);
    result.weightForStacking = configuredWeightPerLine(
        result.join.lines, result.palletsForStacking,
        zeroExcluded(result.weight_per_line, excluded), result.params);

    result.segregation = segregate(result.join.lines, result.demand.dnm,
                                   result.params.doNotMixReading, excluded);
    result.binding = assessBinding(result.segregation, result.palletsForStacking,
                                   result.weightForStacking, trailer);
    result.stacking = buildStacks(result.segregation, result.join.lines, result.palletsForStacking,
                                  result.binding, result.params, trailer);
    result.stackReport = StackReporter::build(result.segregation, result.binding,
                                              result.stacking, result.params);
    result.stackReport.unstackedLines = unstackedLines(result, trailer);
    for (const auto& line : result.stackReport.unstackedLines) {
        LOG_WARN("Demand line " + std::to_string(line.lineIndex) + " (MATNR " + line.matnr
                 + ") was not stacked: " + line.reason);
    }
    result.stackReport.ambiguousPalletLines = static_cast<std::size_t>(result.validation.ambiguous_pallet);
    result.ranMilestone2 = true;
}

} // namespace

bool isRunComplete(const PipelineResult& result) {
    if (result.validation.errors > 0) return false;
    return !result.ranMilestone2 || result.stackReport.isComplete();
}

PipelineResult Pipeline::run(const PipelineInputs& inputs) {
    PipelineResult result;

    // ── 1. Read the three input files ───────────────────────────────────────
    LOG_DEBUG("Reading product master: " + inputs.product_path);
    result.products = ProductImporter::load(inputs.product_path);

    LOG_DEBUG("Reading demand: " + inputs.demand_path);
    result.demand = Importer::load_demand(inputs.demand_path);

    LOG_DEBUG("Reading placeholders: " + inputs.placeholder_path);
    result.placeholders = PlaceholderImporter::load(inputs.placeholder_path);

    // ── 2. Join demand to the product master ────────────────────────────────
    result.index = Joiner::build_index(result.products.products);
    result.join  = Joiner::join(result.demand.str, result.index);

    // ── 3. Convert quantities to pallets and weight ─────────────────────────
    result.pallets_per_line.reserve(result.join.lines.size());
    result.weight_per_line.reserve(result.join.lines.size());
    for (const auto& jl : result.join.lines) {
        // Unmatched lines still need an entry: the arrays must stay exactly
        // parallel to join.lines, which the Validator and Reporter rely on.
        if (!jl.matched || jl.product == nullptr || jl.str == nullptr) {
            result.pallets_per_line.push_back(0.0);
            result.weight_per_line.push_back(0.0);
            continue;
        }

        const double pallets = Converter::to_pallets(jl.str->trans,
                                                     jl.str->unitofmeas,
                                                     *jl.product);
        result.pallets_per_line.push_back(pallets);
        result.weight_per_line.push_back(Converter::to_weight_lb(pallets, *jl.product));
    }

    // ── 4. Validate ─────────────────────────────────────────────────────────
    result.validation = Validator::validate(result.join,
                                            result.pallets_per_line,
                                            inputs.validation);
    Validator::validate_placeholders(result.placeholders.placeholders, result.validation,
                                     inputs.validation);
    Validator::validate_do_not_mix(result.demand.dnm, result.demand.str, result.validation);

    // ── 5. Summarise ────────────────────────────────────────────────────────
    result.summary = Reporter::build(result.join,
                                     result.placeholders.placeholders,
                                     result.pallets_per_line,
                                     result.weight_per_line,
                                     inputs.planning_day,
                                     result.validation);

    // ── 6. Milestone 2: segregate, pass 1, pass 2, stack report ─────────────
    if (!inputs.paramsPath.empty()) runMilestone2(inputs, result);

    return result;
}

} // namespace ob
