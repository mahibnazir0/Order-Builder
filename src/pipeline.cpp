#include "pipeline.hpp"
#include "importer.hpp"
#include "logger.hpp"

namespace ob {

// ===========================================================================
// TEMPORARY — replace with Converter when Saif's module lands.
//
// These two functions do exactly what Converter::to_pallets and
// Converter::to_weight_lb are specified to do. They exist so the pipeline and
// its end-to-end tests could be finished before the Converter was written.
//
// TO SWAP:
//   1. add #include "converter/converter.hpp"
//   2. delete this anonymous namespace
//   3. in run(), replace  local_to_pallets(jl)   with
//        Converter::to_pallets(jl.str->trans, jl.str->unitofmeas, *jl.product)
//      and                 local_to_weight(p, jl) with
//        Converter::to_weight_lb(p, *jl.product)
//
// The arithmetic below is the agreed spec:
//   CS  -> trans / cases_unit_load        (guard divide-by-zero)
//   PAL -> trans directly
//   DIS -> trans directly (one display = one unit load)
//   weight = pallets x (case weight x cases per unit load + pallet weight)
//   pallet weight = 60 lb for PTL and PGM (wood), 0 for TLD and GMA
// ===========================================================================
namespace {

// Weight of the wooden pallet itself, for pallet types that have one.
// Tom, 25 Aug: PTL and PGM are physical wood; TLD and GMA are not.
// Kept as a named constant rather than a literal — it is customer-specific.
constexpr double kWoodPalletWeightLb = 60.0;

bool has_wood_pallet(const std::string& pallet_id) {
    return pallet_id == "PTL" || pallet_id == "PGM";
}

double local_to_pallets(const JoinedLine& jl) {
    if (!jl.matched || jl.product == nullptr || jl.str == nullptr) return 0.0;

    const std::string& uom = jl.str->unitofmeas;
    if (uom == "CS") {
        const int cul = jl.product->cases_unit_load;
        if (cul <= 0) return 0.0;          // Validator flags this line
        return jl.str->trans / cul;
    }
    if (uom == "PAL" || uom == "DIS") {
        return jl.str->trans;
    }
    return 0.0;                            // unrecognised UoM — Validator warns
}

double local_to_weight(double pallets, const JoinedLine& jl) {
    if (jl.product == nullptr) return 0.0;
    const ProductRecord& p = *jl.product;

    // Weight is per CASE in the master, so a unit load is case weight times
    // cases per unit load. Using case weight alone understates by ~60x.
    const double per_unit_load = p.weight_lb * p.cases_unit_load;
    const double pallet_wt = has_wood_pallet(p.pallet_id) ? kWoodPalletWeightLb : 0.0;

    return pallets * (per_unit_load + pallet_wt);
}

} // anonymous namespace
// =========================== end temporary block ===========================


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
        const double pallets = local_to_pallets(jl);      // <- Converter swap point
        result.pallets_per_line.push_back(pallets);
        result.weight_per_line.push_back(local_to_weight(pallets, jl));
    }

    // ── 4. Validate ─────────────────────────────────────────────────────────
    result.validation = Validator::validate(result.join,
                                            result.pallets_per_line,
                                            inputs.validation);

    // ── 5. Summarise ────────────────────────────────────────────────────────
    result.summary = Reporter::build(result.join,
                                     result.placeholders.placeholders,
                                     result.pallets_per_line,
                                     result.weight_per_line,
                                     inputs.planning_day);

    return result;
}

} // namespace ob
