#include "converter.hpp"
#include "palletSpec.hpp"

#include <cmath>

using namespace std;

namespace ob {

double Converter::inches_to_cm(double inches) {
    return inches * kCmPerInch;
}

double Converter::cm_to_inches(double cm) {
    return cm / kCmPerInch;
}

DimensionsCm Converter::to_cm(const ProductRecord& product) {
    DimensionsCm cm;
    cm.length = inches_to_cm(product.length_in);
    cm.width  = inches_to_cm(product.width_in);
    cm.height = inches_to_cm(product.height_in);
    return cm;
}

double Converter::round_pallets(double pallets, PalletRounding rounding) {
    switch (rounding) {
        case PalletRounding::Floor: return floor(pallets);
        case PalletRounding::Ceil:  return ceil(pallets);
        case PalletRounding::None:  break;
    }
    return pallets;
}

bool Converter::isConvertibleUom(const string& uom) {
    return uom == "CS" || uom == "PAL" || uom == "DIS";
}

double Converter::to_pallets(double trans,
                             const string& uom,
                             const ProductRecord& product,
                             PalletRounding rounding) {
    // Unrecognised code. Not this module's job to complain about it.
    if (!isConvertibleUom(uom)) return 0.0;

    // The quantity counts the product's base unit, the master's "case", whatever the code
    // names it, and Cases_Unit_Load of them make one unit load (client, 16 and 20 Oct: 18 DIS
    // per unit load). A PAL product with Cases_Unit_Load 2 stacks two PAL in one unit load,
    // so 60 PAL is 30 unit loads: read as 60, Truck Builder's August loads would be
    // overweight. The Validator rejects a line whose unit differs from the master's.
    // Cases_Unit_Load is 0 on exactly one master row. Return 0 rather than dividing by zero;
    // the Validator reports the line.
    if (product.cases_unit_load == 0) return 0.0;
    return round_pallets(trans / static_cast<double>(product.cases_unit_load), rounding);
}

double Converter::to_weight_lb(double pallets, const ProductRecord& product,
                               const vector<PalletSpec>& palletSpecs) {
    // Weight in the master is per CASE, so a unit load is the case weight
    // times the number of cases on it.
    const double unit_load_lb =
        product.weight_lb * static_cast<double>(product.cases_unit_load);

    return pallets * (unit_load_lb + palletAddedWeightLb(product, palletSpecs).value_or(0.0));
}

optional<double> Converter::palletAddedWeightLb(const ProductRecord& product,
                                                const vector<PalletSpec>& palletSpecs) {
    if (product.palletWeightLb) return product.palletWeightLb;
    const PalletSpec* pallet = palletSpecFor(palletSpecs, product.pallet_id);
    if (pallet == nullptr) return nullopt;
    return pallet->addedWeightLb;
}

}  // namespace ob
