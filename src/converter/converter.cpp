#include "converter.hpp"

namespace ob {

double Converter::inches_to_cm(double inches) {
    return inches * kCmPerInch;
}

double Converter::cm_to_inches(double cm) {
    return cm / kCmPerInch;
}

bool Converter::pallet_has_wood(const std::string& pallet_id) {
    // PTL and PGM are wood. TLD and GMA are not, and add nothing.
    return pallet_id == "PTL" || pallet_id == "PGM";
}

double Converter::to_pallets(double trans,
                             const std::string& uom,
                             const ProductRecord& product) {
    if (uom == "CS") {
        // Cases_Unit_Load is 0 on exactly one master row. Return 0 rather than
        // dividing by zero; the Validator reports the line.
        if (product.cases_unit_load == 0) {
            return 0.0;
        }
        return trans / static_cast<double>(product.cases_unit_load);
    }

    // PAL and DIS are already expressed in unit loads.
    if (uom == "PAL" || uom == "DIS") {
        return trans;
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
