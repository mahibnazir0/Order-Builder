#include "unitLoadMetrics.hpp"
#include "converter.hpp"
#include "stackRules.hpp"
#include "tolerance.hpp"

#include <cmath>

using namespace std;

namespace ob {

namespace {

UnitLoadMetrics rejected(UnitLoadMetricsError error) {
    UnitLoadMetrics metrics;
    metrics.error = error;
    return metrics;
}

UnitLoadMetricsError productDataError(const ProductRecord& product) {
    if (product.cases_unit_load <= 0) return UnitLoadMetricsError::InvalidCasesPerUnitLoad;
    if (product.layers_unit_load <= 0) return UnitLoadMetricsError::InvalidLayersPerUnitLoad;
    if (!isfinite(product.height_in) || product.height_in <= 0.0) {
        return UnitLoadMetricsError::InvalidCaseHeight;
    }
    if (!isfinite(product.weight_lb) || product.weight_lb < 0.0) {
        return UnitLoadMetricsError::InvalidCaseWeight;
    }
    return UnitLoadMetricsError::None;
}

} // anonymous namespace

UnitLoadMetrics unitLoadMetricsFor(const JoinedLine& line, const M2Params& params) {
    if (!line.matched || line.product == nullptr || line.str == nullptr) {
        return rejected(UnitLoadMetricsError::MissingProduct);
    }
    const ProductRecord& product = *line.product;
    const STRRecord& demand = *line.str;

    const optional<PalletSpec> pallet = resolvePalletSpec(product, params);
    if (!pallet) return rejected(UnitLoadMetricsError::MissingPalletSpec);
    // Pallet figures come from CSV cells, where an unreadable one is kept as NaN.
    if (!isfinite(pallet->addedWeightLb) || pallet->addedWeightLb < 0.0
        || !isfinite(pallet->addedHeightIn) || pallet->addedHeightIn < 0.0) {
        return rejected(UnitLoadMetricsError::InvalidPalletSpec);
    }
    if (!Converter::isConvertibleUom(demand.unitofmeas)) {
        return rejected(UnitLoadMetricsError::UnconvertibleUom);
    }
    if (!isfinite(demand.trans) || demand.trans < 0.0) {
        return rejected(UnitLoadMetricsError::InvalidQuantity);
    }
    const UnitLoadMetricsError dataError = productDataError(product);
    if (dataError != UnitLoadMetricsError::None) return rejected(dataError);

    UnitLoadMetrics metrics;
    metrics.unitLoads = Converter::to_pallets(demand.trans, demand.unitofmeas, product);
    metrics.weightLb = metrics.unitLoads
        * (product.weight_lb * product.cases_unit_load + pallet->addedWeightLb);
    metrics.unitLoadHeightIn = unitLoadHeightIn(product, *pallet, params);
    metrics.stackedInches = metrics.unitLoads * metrics.unitLoadHeightIn;
    metrics.casesPerUnitLoadMismatch = static_cast<long long>(product.cases_layer)
        * product.layers_unit_load != product.cases_unit_load;

    if (!isfinite(metrics.weightLb) || !isfinite(metrics.unitLoadHeightIn)
        || !isfinite(metrics.stackedInches)) {
        return rejected(UnitLoadMetricsError::NonFiniteResult);
    }
    return metrics;
}

bool exceedsCeiling(const UnitLoadMetrics& metrics, const TrailerSpec& trailer) {
    return metrics.unitLoadHeightIn > trailer.stackHeightCeilingIn + kQuantityEpsilon;
}

const char* unitLoadMetricsErrorName(UnitLoadMetricsError error) {
    switch (error) {
    case UnitLoadMetricsError::None: return "none";
    case UnitLoadMetricsError::MissingProduct: return "missing_product";
    case UnitLoadMetricsError::MissingPalletSpec: return "missing_pallet_spec";
    case UnitLoadMetricsError::InvalidPalletSpec: return "invalid_pallet_spec";
    case UnitLoadMetricsError::UnconvertibleUom: return "unconvertible_uom";
    case UnitLoadMetricsError::InvalidQuantity: return "invalid_quantity";
    case UnitLoadMetricsError::InvalidCasesPerUnitLoad: return "invalid_cases_per_unit_load";
    case UnitLoadMetricsError::InvalidLayersPerUnitLoad: return "invalid_layers_per_unit_load";
    case UnitLoadMetricsError::InvalidCaseHeight: return "invalid_case_height";
    case UnitLoadMetricsError::InvalidCaseWeight: return "invalid_case_weight";
    case UnitLoadMetricsError::NonFiniteResult: return "non_finite_result";
    }
    return "unknown";
}

} // namespace ob
