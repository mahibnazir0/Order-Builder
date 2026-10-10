#include "palletSpec.hpp"

using namespace std;

namespace ob {

const vector<PalletSpec>& confirmedPalletSpecs() {
    // PTL and PGM are wood: 60 lb, confirmed by Tom in writing on 25 August. The client's
    // September pallet table says 65 lb; that is an open question (M3 question 5), and a
    // confirmed weight does not change on silence. TLD and GMA add nothing.
    static const vector<PalletSpec> specs{
        {"PTL", 60.0, 5.5, 48.0, 40.0},
        {"PGM", 60.0, 5.5, 48.0, 40.0},
        {"TLD", 0.0, 0.0, 48.0, 40.0},
        {"GMA", 0.0, 0.0, 48.0, 40.0},
    };
    return specs;
}

const PalletSpec* palletSpecFor(const vector<PalletSpec>& pallets, const string& palletId) {
    for (const auto& pallet : pallets) {
        if (pallet.palletId == palletId) return &pallet;
    }
    return nullptr;
}

} // namespace ob
