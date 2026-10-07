#pragma once

#include "joiner.hpp"
#include "palletSpec.hpp"
#include "trailerSpec.hpp"

#include <vector>

namespace ob {

// Why a line cannot feed the floor. Each is a named rejection, never a zero or a NaN that
// would slip into a group total.
enum class UnitLoadMetricsError {
    None,
    MissingProduct,           // demand line has no product in the master
    MissingPalletSpec,        // blank or unknown Pallet_ID: no weight, deck or footprint
    UnconvertibleUom,         // unit of measure is not CS, PAL or DIS
    InvalidQuantity,          // negative or non-finite demand quantity
    InvalidCasesPerUnitLoad,  // Cases_Unit_Load zero or negative
    InvalidLayersPerUnitLoad, // Layers_Unit_Load zero or negative
    InvalidCaseHeight,        // Height zero, negative or non-finite: an unbounded column
    InvalidCaseWeight,        // Weight negative or non-finite (blank reads as NaN); 0 is a value
    NonFiniteResult,          // finite inputs whose product overflows
};

struct UnitLoadMetrics {
    double unitLoads = 0.0;
    double weightLb = 0.0;
    double unitLoadHeightIn = 0.0;
    double stackedInches = 0.0;
    // Cases_Unit_Load differs from Cases_Layer x Layers_Unit_Load, so the two describe
    // different pallets. Unit loads and weight follow Cases_Unit_Load; height follows
    // Layers_Unit_Load. Reported, not rejected.
    bool casesPerUnitLoadMismatch = false;
    UnitLoadMetricsError error = UnitLoadMetricsError::None;
};

// The three quantities the floor needs from one demand line. Weight is per case in the
// master, so a unit load weighs Weight x Cases_Unit_Load plus its pallet. PAL and DIS
// quantities are already unit loads. Length and Width are case dimensions and are never
// used here; footprint comes from palletSpec. Never throws.
UnitLoadMetrics unitLoadMetricsFor(const JoinedLine& line, const std::vector<PalletSpec>& pallets,
                                   DeckHeightRule deckHeight);

// Taller than the trailer ceiling by more than kQuantityEpsilon. A unit load exactly at the
// ceiling fits: 155 real products sit exactly at it, and a case height that is not exact in
// binary (36/7 in x 21 layers) computes one ULP above, so this must never become a bare > or >=.
bool exceedsCeiling(const UnitLoadMetrics& metrics, const TrailerSpec& trailer);

// Stable plain-ASCII name for error messages and reports.
const char* unitLoadMetricsErrorName(UnitLoadMetricsError error);

} // namespace ob
