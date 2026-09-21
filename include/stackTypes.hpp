#pragma once

#include <string>

namespace ob {

enum class UnitLoadError { None, MissingProduct, MissingPalletSpec, InvalidData };

// One pallet as it would ride in the trailer. Built from a joined demand line by
// buildUnitLoad; a load whose error is not None must not be used for stacking.
struct UnitLoad {
    std::string id;
    double footprintLengthIn = 0.0;   // from the pallet spec, never the case Length/Width
    double footprintWidthIn = 0.0;
    double heightIn = 0.0;            // case Height * layers + pallet deck
    double weightLb = 0.0;            // case Weight * cases per unit load + pallet weight
    double ownWeightAboveLb = 0.0;    // weight this load carries on its own upper layers
    int cri = 0;                      // 0 = blank in the product master
    UnitLoadError error = UnitLoadError::None;
};

struct StackFeasibility {
    // FootprintMismatch is reserved until an overhang rule is specified; canStack never returns it.
    enum class Reason { Ok, HeightCeiling, CriExceeded, BlankCri, FootprintMismatch, InvalidData };
    bool isFeasible = false;
    Reason reason = Reason::Ok;
    // Ok: headroom under the ceiling (in). HeightCeiling / CriExceeded: the overage (in / lb).
    // Zero for the categorical reasons, which have no numeric margin.
    double marginInOrLb = 0.0;
};

} // namespace ob
