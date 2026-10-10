#pragma once

#include "product_types.hpp"

#include <array>
#include <string>
#include <vector>

namespace ob {

enum class SegregationReading { Strict, FlaggedVsNormal };

// Upper bound for pass2AttemptCap. Try Hard re-runs the whole greedy build once per
// attempt per group. The shipped config uses 4; 64 is generous while bounding runtime.
constexpr int kMaxPass2Attempts = 64;

// Upper bound for maxStackHeight. This customer never exceeds two high; 16 leaves
// ample room for future rules while preventing an unbounded value from the params file.
constexpr int kMaxStackHeight = 16;

struct CriTable {
    // Index 0 is unused so callers can index by CRI 1..10 directly.
    std::array<double, 11> safeLimitLb{};
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
    // The pallet table (Customer2-Pallet-Data.csv), filled by the pipeline from --pallets.
    // Never read from the params file: pallet figures come from the supplied data, so a client
    // with different pallets needs no config change. Product rows that carry their own pallet
    // figures take precedence over this table.
    std::vector<PalletSpec> pallets;
    std::vector<TrailerSpec> trailers;
    SegregationReading doNotMixReading = SegregationReading::Strict;
    int pass2AttemptCap = 4;
    // Most pallets in one stack. Tom: this customer never goes above two high.
    int maxStackHeight = 2;
    bool blankCriIsStackable = false;
    // True stacks whole physical pallets: each line's pallet-equivalents are rounded up, so a
    // part pallet takes a real position. False keeps the fractional estimate, which can report
    // 0.5 floor positions for one pallet and so understates floor use in small groups.
    bool stackWholePallets = true;
    std::vector<std::string> defaultedKeys;
    std::vector<std::string> warnings;
};

// Pallet table row by exact id ("PTL" and "PTL " differ), nullptr if absent.
const PalletSpec* palletSpecFor(const M2Params& params, const std::string& palletId);
// Prevent returning a pointer into a params object destroyed at the end of the call.
const PalletSpec* palletSpecFor(M2Params&& params, const std::string& palletId) = delete;

} // namespace ob
