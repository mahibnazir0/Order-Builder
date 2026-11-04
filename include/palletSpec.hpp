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
// stacks and the Milestone 3 floor alike. The client ruled Included (12 Oct); the params file
// must still state which reading runs (floorDeckHeight).
enum class DeckHeightRule { Excluded, Included };

// What a PTL or PGM unit load covers (Rev 8 question 4, open): the customer's written rule, the
// larger of the pallet and the layer on each side, or the layer alone, which is what the product
// file's Effective_Layer columns hold. TLD and GMA cover their layer under either.
enum class FootprintRule { WrittenRule, LayerOnly };

// The fallback pallet figures for a master that carries no pallet columns and a run given no
// pallet table (the 17 August master has neither). The product row and the supplied pallet
// table always win over these, in Milestone 1 as in Milestone 2 and the floor.
const std::vector<PalletSpec>& confirmedPalletSpecs();

// Exact match ("PTL" and "PTL " differ), nullptr if absent.
const PalletSpec* palletSpecFor(const std::vector<PalletSpec>& pallets,
                                const std::string& palletId);
// Prevent returning a pointer into a pallet list destroyed at the end of the call.
const PalletSpec* palletSpecFor(std::vector<PalletSpec>&& pallets,
                                const std::string& palletId) = delete;

} // namespace ob
