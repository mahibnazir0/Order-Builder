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
// PARTIAL-PALLET ROUNDING
// The M1 plan (Phase 3.2) asks for floor/ceil rounding to be configurable.
// Tom's later ruling on the "0.9 of a pallet?" query was that the figure is a
// PALLET-EQUIVALENT and fractions are summed across the lane.
//
// Both are honoured: rounding is a parameter, and its DEFAULT IS None, which
// is Tom's ruling. Nothing rounds unless a caller explicitly asks it to, so
// no lane total changes by adding this. Should Tom's ruling ever be revised,
// the switch is already here and no caller needs redesigning.
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

// A product's dimensions in centimetres. The master stores inches.
struct DimensionsCm {
    double length = 0.0;
    double width  = 0.0;
    double height = 0.0;
};

// How a partial unit load is treated. None is the default everywhere and is
// Tom's ruling; Floor and Ceil exist so the behaviour can be changed without
// a redesign if that ruling is revised.
enum class PalletRounding {
    None,   // keep the fraction (default) - fractions are summed across the lane
    Floor,  // round down to whole unit loads
    Ceil    // round up to whole unit loads
};

class Converter {
public:
    // 1 inch = 2.54 cm, exactly, by definition.
    static constexpr double kCmPerInch = 2.54;

    // Assumed weight of a wood pallet. Documented assumption, not Tom's figure.
    static constexpr double kWoodPalletWeightLb = 60.0;

    static double inches_to_cm(double inches);
    static double cm_to_inches(double cm);

    // The product master is in inches. This is the single point at which a
    // master row's dimensions become centimetres, so downstream code works in
    // one unit and never re-derives the conversion itself.
    //
    // It deliberately returns a NEW value rather than converting ProductRecord
    // in place: those fields are named length_in / width_in / height_in, and
    // leaving centimetres sitting in a field called "_in" would be a trap for
    // the next reader. Renaming them belongs with product_types.hpp, not here.
    static DimensionsCm to_cm(const ProductRecord& product);

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
    //
    // `rounding` defaults to None, so by default the fraction is kept.
    static double to_pallets(double trans,
                             const std::string& uom,
                             const ProductRecord& product,
                             PalletRounding rounding = PalletRounding::None);

    // Apply a rounding mode to an already-computed pallet figure. Exposed so
    // a caller that has summed a lane can round the total rather than each
    // line, which are not the same number.
    static double round_pallets(double pallets, PalletRounding rounding);

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
