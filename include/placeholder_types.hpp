#pragma once
// ============================================================================
// placeholder_types.hpp — Structure for PlaceHolder-1.json
//
// Verified against the real file (189 entries, 8 fields, single PHOLDER array):
//   LOCFRNO, LOCTONO, SHIP_COND, DATFR_TA, DATTO_TA,
//   ZZNA_EQUIP_SIZE, NO_OF_LOADS, EBELN
//
// Notes from the real data:
//   - EBELN is blank in all 189 records. Kept because it is in the schema.
//   - ZZNA_EQUIP_SIZE is blank in 43 of 189; the rest are "53F".
//     Blank appears to mean "any trailer" — to confirm with Tom.
//   - No duplicate LOCFRNO/LOCTONO/SHIP_COND triplets, so no de-dup needed.
// ============================================================================

#include <string>
#include <vector>

namespace ob {

// NO_OF_LOADS as read when the field is missing, null or the wrong type. Never a valid
// count, so the Validator's negative_load_count rule catches it instead of a 0 passing.
constexpr int kUnreadableLoadCount = -1;

struct PlaceholderRecord {
    std::string locfrno;          // origin location        "2023"
    std::string loctono;          // destination            "2528"
    std::string ship_cond;        // TL (105) | TF (84)
    std::string datfr_ta;         // window start           "2026-08-20"
    std::string datto_ta;         // window end             "2026-08-22"
    std::string zzna_equip_size;  // "53F" or "" (blank = any trailer)
    int         no_of_loads = 0;  // trucks requested on this lane (1-13)
    std::string ebeln;            // purchase order ref (blank in current file)
};

// Result of reading the placeholder file.
struct PlaceholderLoadResult {
    std::vector<PlaceholderRecord> placeholders;
    // Sum of in-range no_of_loads only (0..kMaxLoadsPerPlaceholder), matching the
    // placeholders the Reporter counts — 372 in the supplied file.
    long long total_loads = 0;
};

} // namespace ob
