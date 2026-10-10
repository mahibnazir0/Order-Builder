#pragma once

#include <string>
#include <vector>

namespace ob {

// One pallet type's physical figures: a row of the pallet table (Customer2-Pallet-Data.csv),
// or the figures a product row carries for its own pallet. Nothing outside this module may
// carry these figures as literals.
struct PalletSpec {
    std::string palletId;
    double addedWeightLb = 0.0;
    double addedHeightIn = 0.0;
    double footprintLengthIn = 0.0;
    double footprintWidthIn = 0.0;
};

// Whether the pallet's deck height counts toward a unit load's height, in the Milestone 2
// stacks and the Milestone 3 floor alike. Open with the client (M3 questions 6 and 21); the
// params file must state which reading runs (floorDeckHeight).
enum class DeckHeightRule { Excluded, Included };

// The confirmed pallet weights behind the Milestone 1 totals, which stay on these figures so
// the published M1 report is reproducible. Milestone 2 and the floor weigh each pallet from
// the product row and the pallet table instead (resolvePalletSpec).
const std::vector<PalletSpec>& confirmedPalletSpecs();

// Exact match ("PTL" and "PTL " differ), nullptr if absent.
const PalletSpec* palletSpecFor(const std::vector<PalletSpec>& pallets,
                                const std::string& palletId);
// Prevent returning a pointer into a pallet list destroyed at the end of the call.
const PalletSpec* palletSpecFor(std::vector<PalletSpec>&& pallets,
                                const std::string& palletId) = delete;

} // namespace ob
