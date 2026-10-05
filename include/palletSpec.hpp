#pragma once

#include <string>
#include <vector>

namespace ob {

// The only home for what a pallet weighs, how tall its deck is and what footprint it
// occupies. Nothing outside this module may carry these figures as literals.
struct PalletSpec {
    std::string palletId;
    double addedWeightLb = 0.0;
    double addedHeightIn = 0.0;
    double footprintLengthIn = 0.0;
    double footprintWidthIn = 0.0;
};

// The confirmed pallet table, used where no params file is supplied (the Milestone 1
// weights). The shipped params file must list exactly these specs; a test holds the two
// together so a corrected figure cannot land in one and not the other.
const std::vector<PalletSpec>& confirmedPalletSpecs();

// Exact match ("PTL" and "PTL " differ), nullptr if absent.
const PalletSpec* palletSpecFor(const std::vector<PalletSpec>& pallets,
                                const std::string& palletId);
// Prevent returning a pointer into a pallet list destroyed at the end of the call.
const PalletSpec* palletSpecFor(std::vector<PalletSpec>&& pallets,
                                const std::string& palletId) = delete;

} // namespace ob
