#include "palletSpec.hpp"

using namespace std;

namespace ob {

const vector<PalletSpec>& confirmedPalletSpecs() {
    // The client's own figures (every master and pallet table since September), so a run
    // without them weighs exactly as a run with them. Tom confirmed 60 lb wood on 25 August,
    // then on 13 October ruled that the item master's figure wins. TLD and GMA carry a 0.1 in
    // placeholder deck, read as 0 (stackRules).
    static const vector<PalletSpec> specs{
        {"PTL", 65.0, 6.0, 48.0, 40.0},
        {"PGM", 65.0, 6.0, 48.0, 40.0},
        {"TLD", 1.0, 0.0, 48.0, 40.0},
        {"GMA", 0.1, 0.0, 45.0, 34.0},
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
