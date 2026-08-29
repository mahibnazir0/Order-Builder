#include "converter.hpp"

#include <cmath>

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
        case PalletRounding::Floor: return std::floor(pallets);
        case PalletRounding::Ceil:  return std::ceil(pallets);
        case PalletRounding::None:  break;
    }
    return pallets;
}

bool Converter::pallet_has_wood(const std::string& pallet_id) {
    // PTL and PGM are wood. TLD and GMA are not, and add nothing.
    return pallet_id == "PTL" || pallet_id == "PGM";
}

double Converter::to_pallets(double trans,
                             const std::string& uom,
                             const ProductRecord& product,
                             PalletRounding rounding) {
    if (uom == "CS") {
        // Cases_Unit_Load is 0 on exactly one master row. Return 0 rather than
        // dividing by zero; the Validator reports the line.
        if (product.cases_unit_load == 0) {
            return 0.0;
        }
        return round_pallets(trans / static_cast<double>(product.cases_unit_load),
                             rounding);
    }

    // PAL and DIS are already expressed in unit loads. They still go through
    // the rounding switch so one mode means one thing across every UoM.
    if (uom == "PAL" || uom == "DIS") {
        return round_pallets(trans, rounding);
    }

    // Unrecognised code. Not this module's job to complain about it.
    return 0.0;
}

double Converter::to_weight_lb(double pallets,
                               const ProductRecord& product,
                               double wood_pallet_weight_lb) {
    // Weight in the master is per CASE, so a unit load is the case weight
    // times the number of cases on it.
    const double unit_load_lb =
        product.weight_lb * static_cast<double>(product.cases_unit_load);

    const double pallet_lb =
        pallet_has_wood(product.pallet_id) ? wood_pallet_weight_lb : 0.0;

    return pallets * (unit_load_lb + pallet_lb);
}

}  // namespace ob
