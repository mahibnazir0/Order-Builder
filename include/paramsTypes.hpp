#pragma once

#include <array>
#include <string>
#include <vector>

namespace ob {

enum class SegregationReading { Strict, FlaggedVsNormal };

struct CriTable {
    // Index 0 is unused so callers can index by CRI 1..10 directly.
    std::array<double, 11> safeLimitLb{};
};

struct PalletSpec {
    std::string palletId;
    double addedWeightLb = 0.0;
    double addedHeightIn = 0.0;
    double footprintLengthIn = 0.0;
    double footprintWidthIn = 0.0;
};

struct TrailerSpec {
    std::string trailerCode;
    double interiorLengthIn = 0.0;
    double interiorWidthIn = 0.0;
    double stackHeightCeilingIn = 0.0;
    double weightLimitLb = 0.0;
    int stackPositions = 0;
};

struct M2Params {
    CriTable cri;
    std::vector<PalletSpec> pallets;
    std::vector<TrailerSpec> trailers;
    SegregationReading doNotMixReading = SegregationReading::Strict;
    int pass2AttemptCap = 4;
    bool blankCriIsStackable = false;
    std::vector<std::string> defaultedKeys;
    std::vector<std::string> warnings;
};

// Exact match ("PTL" and "PTL " differ), nullptr if absent; Module 3 checks every joined pallet type has a spec.
const PalletSpec* palletSpecFor(const M2Params& params, const std::string& palletId);
// Prevent returning a pointer into a params object destroyed at the end of the call.
const PalletSpec* palletSpecFor(M2Params&& params, const std::string& palletId) = delete;

} // namespace ob
