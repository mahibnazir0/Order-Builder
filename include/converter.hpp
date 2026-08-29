#pragma once
// ============================================================================
// converter.hpp — Unit conversions: inches <-> cm, demand quantity -> pallets,
// pallets -> pounds.
//
// The Converter is deliberately pure arithmetic. It never throws, never logs
// and never rejects a line: a quantity it cannot convert comes back as 0.0 and
// the Validator decides whether that matters. This keeps the "readers load,
// the Validator judges" discipline the rest of the pipeline already follows.
//
// PALLETS ARE NOT ROUNDED
// Tom's ruling on the "0.9 of a pallet?" query: the figure is a PALLET-
// EQUIVALENT and fractions are summed across the lane. Rounding here would
// change the lane totals, so nothing in this file rounds.
//
// UNIT-LOAD WEIGHT BASIS
// Product_Data.Weight is the weight of a SINGLE CASE (median ~9 lb), not of a
// unit load. One pallet therefore weighs (case weight x Cases_Unit_Load),
// plus the pallet itself where the pallet is physical wood.
//
// WOOD PALLETS
// PTL and PGM are wood and add their own weight; TLD and GMA add nothing.
// 10,659 of the 20,201 master rows are wood (verified against the real file),
// and the pallet is ~9.5% of a median unit load, so it is not negligible.
// The 60 lb is a PARAMETER, not a literal, because it is an assumption Tom has
// not yet confirmed.
// ============================================================================

#include "product_types.hpp"

#include <string>

namespace ob {

class Converter {
public:
    // 1 inch = 2.54 cm, exactly, by definition.
    static constexpr double kCmPerInch = 2.54;

    // Assumed weight of a wood pallet. Documented assumption, not Tom's figure.
    static constexpr double kWoodPalletWeightLb = 60.0;

    static double inches_to_cm(double inches);
    static double cm_to_inches(double cm);

    // True for pallet types that are physical wood and carry their own weight.
    static bool pallet_has_wood(const std::string& pallet_id);

    // Convert a demand quantity to pallet-equivalents.
    //
    //   CS         -> trans / Cases_Unit_Load   (cases to unit loads)
    //   PAL, DIS   -> trans                      (already unit loads)
    //   anything else -> 0.0
    //
    // Returns 0.0 when Cases_Unit_Load is 0 (one master row is) rather than
    // dividing by zero. The line survives; the Validator flags it.
    static double to_pallets(double trans,
                             const std::string& uom,
                             const ProductRecord& product);

    // Weight in pounds of the given number of pallet-equivalents.
    //
    //   one pallet = (case weight x Cases_Unit_Load) + wood pallet, if wood
    //
    // Fractional pallets scale the wood pallet linearly, consistent with
    // fractions being summed across the lane rather than rounded up to a
    // physical pallet. Load building (Milestone 2) is where a part pallet
    // becomes a whole one.
    static double to_weight_lb(double pallets,
                               const ProductRecord& product,
                               double wood_pallet_weight_lb = kWoodPalletWeightLb);
};

}  // namespace ob
