#include "stackRules.hpp"

#include <cmath>
#include <set>

namespace ob {
namespace {

bool isPositiveFinite(double value) noexcept { return std::isfinite(value) && value > 0.0; }

bool isNonNegativeFinite(double value) noexcept { return std::isfinite(value) && value >= 0.0; }

bool isCriInRange(int cri, const M2Params& params) noexcept {
    return cri >= 0 && static_cast<std::size_t>(cri) < params.cri.safeLimitLb.size();
}

bool isStackableData(const UnitLoad& load, const M2Params& params) noexcept {
    return load.error == UnitLoadError::None && isPositiveFinite(load.heightIn)
        && isPositiveFinite(load.weightLb) && isNonNegativeFinite(load.ownWeightAboveLb)
        && isCriInRange(load.cri, params);
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

UnitLoad buildUnitLoad(const JoinedLine& line, const M2Params& params,
                       std::optional<double> suppliedWeightAboveLb) {
    UnitLoad load;
    if (!line.matched || line.product == nullptr) {
        load.error = UnitLoadError::MissingProduct;
        return load;
    }
    const ProductRecord& product = *line.product;
    load.id = product.id;
    load.cri = product.strength;

    const PalletSpec* pallet = palletSpecFor(params, product.pallet_id);
    if (pallet == nullptr) {
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
    load.heightIn = product.height_in * product.layers_unit_load + pallet->addedHeightIn;
    load.weightLb = product.weight_lb * product.cases_unit_load + pallet->addedWeightLb;
    load.ownWeightAboveLb = suppliedWeightAboveLb.value_or(
        static_cast<double>(product.layers_unit_load - 1) * product.cases_layer * product.weight_lb);
    if (!std::isfinite(load.heightIn) || !std::isfinite(load.weightLb)
        || !std::isfinite(load.ownWeightAboveLb)) {
        load.error = UnitLoadError::InvalidData;
    }
    return load;
}

std::vector<std::string> missingPalletIds(const std::vector<JoinedLine>& lines,
                                          const M2Params& params) {
    std::set<std::string> palletIds;
    for (const auto& line : lines) {
        if (line.matched && line.product != nullptr) palletIds.insert(line.product->pallet_id);
    }
    std::vector<std::string> missing;
    for (const auto& palletId : palletIds) {
        if (palletSpecFor(params, palletId) == nullptr) missing.push_back(palletId);
    }
    return missing;
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
    const double safeLimitLb = params.cri.safeLimitLb[static_cast<std::size_t>(base.cri)];
    if (weightAboveLb > safeLimitLb) return {false, Reason::CriExceeded, weightAboveLb - safeLimitLb};
    return {true, Reason::Ok, ceilingIn - combinedHeightIn};
}

} // namespace ob
