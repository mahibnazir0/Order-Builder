#include "stackRules.hpp"

#include <cmath>
#include <set>

using namespace std;

namespace ob {
namespace {

bool isPositiveFinite(double value) noexcept { return isfinite(value) && value > 0.0; }

bool isNonNegativeFinite(double value) noexcept { return isfinite(value) && value >= 0.0; }

bool isCriInRange(int cri, const M2Params& params) noexcept {
    return cri >= 0 && static_cast<size_t>(cri) < params.cri.safeLimitLb.size();
}

bool isStackableData(const UnitLoad& load, const M2Params& params) noexcept {
    return load.error == UnitLoadError::None && isPositiveFinite(load.heightIn)
        && isPositiveFinite(load.weightLb) && isNonNegativeFinite(load.ownWeightAboveLb)
        && isCriInRange(load.cri, params);
}

// The client's pallet data gives TLD and GMA a 0.1 in deck. That is a placeholder, not a
// measured height (client, 3 Oct): it adds nothing to the stack height, or the products built
// to exactly 108.00 in would read as over the ceiling. The figure arrives as a float, so
// 0.10000000149011612.
constexpr double kPlaceholderPalletHeightIn = 0.1;
constexpr double kPlaceholderHeightTolerance = 1e-6;

double effectivePalletHeight(double heightIn) noexcept {
    return heightIn <= kPlaceholderPalletHeightIn + kPlaceholderHeightTolerance && heightIn >= 0.0
        ? 0.0 : heightIn;
}

bool isBuildableProduct(const ProductRecord& product, const PalletSpec& pallet,
                        const M2Params& params) noexcept {
    return isPositiveFinite(product.height_in) && isPositiveFinite(product.weight_lb)
        && product.layers_unit_load > 0 && product.cases_layer > 0 && product.cases_unit_load > 0
        && isCriInRange(product.strength, params)
        && isPositiveFinite(pallet.footprintLengthIn) && isPositiveFinite(pallet.footprintWidthIn)
        && isNonNegativeFinite(pallet.addedHeightIn) && isNonNegativeFinite(pallet.addedWeightLb);
}

} // namespace

optional<PalletSpec> resolvePalletSpec(const ProductRecord& product, const M2Params& params) {
    const PalletSpec* tableRow = palletSpecFor(params, product.pallet_id);
    auto pick = [tableRow](const optional<double>& fromProduct,
                           double PalletSpec::*field) -> optional<double> {
        if (fromProduct) return fromProduct;
        if (tableRow != nullptr) return tableRow->*field;
        return nullopt;
    };
    const optional<double> weightLb = pick(product.palletWeightLb, &PalletSpec::addedWeightLb);
    const optional<double> heightIn = pick(product.palletHeightIn, &PalletSpec::addedHeightIn);
    const optional<double> lengthIn = pick(product.palletFootprintLengthIn, &PalletSpec::footprintLengthIn);
    const optional<double> widthIn = pick(product.palletFootprintWidthIn, &PalletSpec::footprintWidthIn);
    if (!weightLb || !heightIn || !lengthIn || !widthIn) return nullopt;
    return PalletSpec{product.pallet_id, *weightLb, effectivePalletHeight(*heightIn), *lengthIn, *widthIn};
}

double unitLoadHeightIn(const ProductRecord& product, const PalletSpec& pallet,
                        const M2Params& params) noexcept {
    return product.height_in * product.layers_unit_load
        + (params.floorDeckHeight == DeckHeightRule::Included ? pallet.addedHeightIn : 0.0);
}

UnitLoad buildUnitLoad(const JoinedLine& line, const M2Params& params,
                       optional<double> suppliedWeightAboveLb) {
    UnitLoad load;
    if (!line.matched || line.product == nullptr) {
        load.error = UnitLoadError::MissingProduct;
        return load;
    }
    const ProductRecord& product = *line.product;
    load.id = product.id;
    load.cri = product.strength;

    const optional<PalletSpec> pallet = resolvePalletSpec(product, params);
    if (!pallet) {
        load.error = UnitLoadError::MissingPalletSpec;
        return load;
    }
    if (!isBuildableProduct(product, *pallet, params)
        || (suppliedWeightAboveLb && !isNonNegativeFinite(*suppliedWeightAboveLb))) {
        load.error = UnitLoadError::InvalidData;
        return load;
    }

    load.footprintLengthIn = pallet->footprintLengthIn;
    load.footprintWidthIn = pallet->footprintWidthIn;
    load.heightIn = unitLoadHeightIn(product, *pallet, params);
    load.weightLb = product.weight_lb * product.cases_unit_load + pallet->addedWeightLb;
    load.ownWeightAboveLb = suppliedWeightAboveLb.value_or(
        static_cast<double>(product.layers_unit_load - 1) * product.cases_layer * product.weight_lb);
    if (!isfinite(load.heightIn) || !isfinite(load.weightLb)
        || !isfinite(load.ownWeightAboveLb)) {
        load.error = UnitLoadError::InvalidData;
    }
    return load;
}

vector<string> missingPalletIds(const vector<JoinedLine>& lines,
                                          const M2Params& params) {
    set<string> missing;
    for (const auto& line : lines) {
        if (line.matched && line.product != nullptr && !resolvePalletSpec(*line.product, params)) {
            missing.insert(line.product->pallet_id);
        }
    }
    return {missing.begin(), missing.end()};
}

bool exceedsOwnCri(const UnitLoad& load, const M2Params& params) noexcept {
    if (load.cri <= 0 || !isCriInRange(load.cri, params)) return false;
    return load.ownWeightAboveLb > params.cri.safeLimitLb[static_cast<size_t>(load.cri)];
}

StackFeasibility canStack(const UnitLoad& base, const UnitLoad& top,
                          const M2Params& params, double ceilingIn) noexcept {
    using Reason = StackFeasibility::Reason;
    if (!isPositiveFinite(ceilingIn) || !isStackableData(base, params)
        || !isStackableData(top, params)) {
        return {false, Reason::InvalidData, 0.0};
    }

    // Height first: it is the cheapest test and rejects about 97% of pairs on real demand.
    const double combinedHeightIn = base.heightIn + top.heightIn;
    if (combinedHeightIn > ceilingIn) return {false, Reason::HeightCeiling, combinedHeightIn - ceilingIn};

    const double weightAboveLb = base.ownWeightAboveLb + top.weightLb;
    if (base.cri == 0) {
        if (!params.blankCriIsStackable) return {false, Reason::BlankCri, 0.0};
        return {true, Reason::Ok, ceilingIn - combinedHeightIn};
    }
    const double safeLimitLb = params.cri.safeLimitLb[static_cast<size_t>(base.cri)];
    if (weightAboveLb > safeLimitLb) return {false, Reason::CriExceeded, weightAboveLb - safeLimitLb};
    return {true, Reason::Ok, ceilingIn - combinedHeightIn};
}

} // namespace ob
