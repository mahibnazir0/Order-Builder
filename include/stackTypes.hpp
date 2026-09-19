#pragma once

#include <string>

namespace ob {

enum class UnitLoadError { None, MissingProduct, MissingPalletSpec, InvalidData };
enum class WeightAboveSource { Derived, Supplied };

struct UnitLoad {
    std::string id;
    double footprintLengthIn = 0.0;
    double footprintWidthIn = 0.0;
    double heightIn = 0.0;
    double weightLb = 0.0;
    double ownWeightAboveLb = 0.0;
    int cri = 0;
    WeightAboveSource weightAboveSource = WeightAboveSource::Derived;
    UnitLoadError error = UnitLoadError::None;
};

struct StackFeasibility {
    enum class Reason { Ok, HeightCeiling, CriExceeded, BlankCri, FootprintMismatch, InvalidData };
    bool isFeasible = false;
    Reason reason = Reason::Ok;
    // Height headroom on success; positive overage on height/CRI failure.
    // Zero for categorical failures, which have no numeric margin.
    double marginInOrLb = 0.0;
};

} // namespace ob
