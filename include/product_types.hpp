#pragma once
// ============================================================================
// product_types.hpp — Structure for Customer2-Product-Data.csv
//
// Columns verified against the real file (20,201 rows, 12 columns):
//   ID, Description, Length, Width, Height, Strength, UoM, Weight,
//   Cases_Layer, Layers_Unit_Load, Cases_Unit_Load, Pallet_ID
//
// ID is kept as std::string — it is the join key against STR.MATNR, and
// parsing it as an int would risk losing formatting. Same discipline as
// the demand reader.
// ============================================================================

#include <string>

namespace ob {

struct ProductRecord {
    std::string id;                 // join key (matches STR.MATNR) — string
    std::string description;
    double      length_in = 0.0;    // inches
    double      width_in  = 0.0;    // inches
    double      height_in = 0.0;    // inches
    int         strength  = 0;      // 1-10; 1 row is blank in the current file
    std::string uom;                // CS|PAL|DIS|ROL|EA|"" (7,287 blank — fixed Mon)
    double      weight_lb = 0.0;    // pounds
    int         cases_layer = 0;
    int         layers_unit_load = 0;
    int         cases_unit_load = 0; // divisor for CS->pallets; 1 row is 0 (bad record)
    std::string pallet_id;          // PTL|TLD|PGM|GMA etc.
};

} // namespace ob
