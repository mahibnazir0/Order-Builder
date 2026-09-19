#include "stackRules.hpp"

#include <cmath>
#include <set>

namespace ob {
namespace {

bool positiveFinite(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

bool nonnegativeFinite(double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

bool validLoad(const UnitLoad& load) noexcept {
    return load.error == UnitLoadError::None
        && positiveFinite(load.footprintLengthIn) && positiveFinite(load.footprintWidthIn)
        && positiveFinite(load.weightLb) && nonnegativeFinite(load.ownWeightAboveLb)
        && load.cri >= 0 && static_cast<std::size_t>(load.cri) < CriTable{}.safeLimitLb.size();
}

} // namespace

UnitLoad buildUnitLoad(const JoinedLine& line, const M2Params& params,
                       std::optional<double> suppliedWeightAboveLb) {
    UnitLoad load;
    if (!line.matched || line.product == nullptr) {
        load.error = UnitLoadError::MissingProduct;
        return load;
    }
    const auto& product = *line.product;
    load.id = product.id;
    load.cri = product.strength;
    load.weightAboveSource = suppliedWeightAboveLb.has_value()
        ? WeightAboveSource::Supplied : WeightAboveSource::Derived;
    const auto* pallet = palletSpecFor(params, product.pallet_id);
    if (pallet == nullptr) {
        load.error = UnitLoadError::MissingPalletSpec;
        return load;
    }
    if (!positiveFinite(product.height_in) || !positiveFinite(product.weight_lb)
        || product.layers_unit_load <= 0 || product.cases_layer <= 0
        || product.cases_unit_load <= 0 || product.strength < 0
        || static_cast<std::size_t>(product.strength) >= params.cri.safeLimitLb.size()
        || !positiveFinite(pallet->footprintLengthIn) || !positiveFinite(pallet->footprintWidthIn)
        || !nonnegativeFinite(pallet->addedHeightIn) || !nonnegativeFinite(pallet->addedWeightLb)
        || (suppliedWeightAboveLb && !nonnegativeFinite(*suppliedWeightAboveLb))) {
        load.error = UnitLoadError::InvalidData;
        return load;
    }
    load.footprintLengthIn = pallet->footprintLengthIn;
    load.footprintWidthIn = pallet->footprintWidthIn;
    load.heightIn = product.height_in * product.layers_unit_load + pallet->addedHeightIn;
    load.weightLb = product.weight_lb * product.cases_unit_load + pallet->addedWeightLb;
    load.ownWeightAboveLb = suppliedWeightAboveLb ? *suppliedWeightAboveLb
        : static_cast<double>(product.layers_unit_load - 1) * product.cases_layer * product.weight_lb;
    if (!positiveFinite(load.heightIn) || !validLoad(load)) load.error = UnitLoadError::InvalidData;
    return load;
}

std::vector<std::string> missingPalletIds(const std::vector<JoinedLine>& lines,
                                        const M2Params& params) {
    std::set<std::string> palletIds;
    for (const auto& line : lines) {
        if (line.product != nullptr) palletIds.insert(line.product->pallet_id);
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
    const double combinedHeightIn = base.heightIn + top.heightIn;
    if (!positiveFinite(ceilingIn) || !positiveFinite(base.heightIn)
        || !positiveFinite(top.heightIn) || !std::isfinite(combinedHeightIn)) {
        return {false, Reason::InvalidData, 0.0};
    }
    if (combinedHeightIn > ceilingIn) {
        return {false, Reason::HeightCeiling, combinedHeightIn - ceilingIn};
    }
    if (base.cri == 0) return {false, Reason::BlankCri, 0.0};
    if (!validLoad(base) || !validLoad(top)) return {false, Reason::InvalidData, 0.0};
    const double safeLimitLb = params.cri.safeLimitLb[static_cast<std::size_t>(base.cri)];
    const double weightAboveLb = base.ownWeightAboveLb + top.weightLb;
    if (!nonnegativeFinite(safeLimitLb) || !std::isfinite(weightAboveLb)) {
        return {false, Reason::InvalidData, 0.0};
    }
    if (weightAboveLb > safeLimitLb) {
        return {false, Reason::CriExceeded, weightAboveLb - safeLimitLb};
    }
    // FootprintMismatch is reserved until an orientation/overhang rule is specified.
    return {true, Reason::Ok, ceilingIn - combinedHeightIn};
}

} // namespace ob
