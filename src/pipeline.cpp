#include "pipeline.hpp"
#include "converter.hpp"
#include "importer.hpp"
#include "logger.hpp"
#include "paramsLoader.hpp"
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
    result.weightForStacking = zeroExcluded(result.weight_per_line, excluded);

    result.segregation = segregate(result.join.lines, result.demand.dnm, result.params.doNotMixReading);
    result.binding = assessBinding(result.segregation, result.palletsForStacking,
                                   result.weightForStacking, trailer);
    result.stacking = buildStacks(result.segregation, result.join.lines, result.palletsForStacking,
                                  result.binding, result.params, trailer);
    result.stackReport = StackReporter::build(result.segregation, result.binding,
                                              result.stacking, result.params);
    result.ranMilestone2 = true;
}

} // namespace

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
