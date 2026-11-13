#pragma once
// ============================================================================
// m5Policy.hpp - M5's settings: the immutable profile a lane-date is planned
// under, read from the params file's "m5" block and checked before any search.
//
// Customer decisions (v7 0.3, Tom's answers):
//   - minimum-load acceptance (weight > x OR cube > y) runs at export, after
//     M5, so the production profile has it OFF. An absent "acceptance" block is
//     that OFF state; a present but malformed one is an error (U-AC-04).
//   - fullness labels: FULL at 98% of the trailer's weight limit, or when rear
//     inches are at most the rear-inch limit (default 14, a user setting), or
//     floor-limited on evidence (ED-FULL). Labels never withhold a truck.
//   - time: seconds per LOAD, user set; a lane-date's allowance is
//     secondsPerLoad x its reserved trucks, declared before the search (CL-05).
//   - goods past due rank date first by default, per lane overridable (Q7, P9);
//     can-go goods fill after required goods (FX-CANGO).
//   - routing counts reserved trucks per lane and ship date; other count modes
//     and scopes are rejected (CL-08).
// Synthetic fixture values live under tests/m5/support only: the loader never
// returns them.
// ============================================================================

#include "json.hpp"
#include "m5Types.hpp"
#include "selectionItem.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace ob {
namespace m5 {

enum class EvidenceScope { Synthetic, RealCustomer };

// CL-01, strict on both branches. Thresholds compared at full precision.
struct NumericAcceptance {
    double weightLb = 0.0;  // x
    double cubeCuFt = 0.0;  // y
};

struct FullnessRule {
    double weightFullPercent = 98.0;
    double rearInchLimit = 14.0;
    bool rearInchLimitDefaulted = true;  // printed as provenance: default stated to Tom
};

enum class MethodPolicy { SequentialAThenB, PatternOnly, BlocksOnly };

// ED-02 production comparator, or the synthetic FX-PRIORITY fixture comparator (tests only).
enum class ComparatorKind { M4Urgency, FxPriority };

struct RepairLimits {
    int maxRounds = 8;
    int maxMovesPerTruck = 64;
};

struct M5PolicyProfile {
    std::string profileId = "production";
    std::uint32_t profileRevision = 1;
    EvidenceScope scope = EvidenceScope::RealCustomer;
    std::optional<NumericAcceptance> acceptance;  // empty: OFF, the customer's export check
    FullnessRule fullness;
    double secondsPerLoad = 0.0;                  // user setting; no default
    PastDueOrder order = PastDueOrder::AgeFirst;
    bool canGoFill = true;
    MethodPolicy method = MethodPolicy::SequentialAThenB;
    double methodAShare = 0.6;                    // of the allowance; B runs in what is left
    int blockSize = 19;
    int scarcityWindow = 2;
    long long gapTrucks = 1;                      // FX-GAP G: escalate when F - B >= G (Q6: F > B)
    RepairLimits repair;
    ComparatorKind comparator = ComparatorKind::M4Urgency;
    std::optional<double> penaltyPerUnusedTruck;  // CL-06: empty is unknown
    std::string configHash;                       // identity of the block it was read from

    // profileId, revision and config hash together: anything a cached decision depends on.
    std::string key() const;
};

struct AcceptanceDecision {
    bool accepted = false;
    bool byWeight = false;
    bool byCube = false;
};

// Strict OR on exact totals: weight > x, or cube (cubic inches / 1728) > y.
AcceptanceDecision evaluateAcceptance(double weightLb, double cubeCubicIn, const NumericAcceptance& rule);

// Reads root["m5"]. Throws std::runtime_error naming the key for a malformed block: a missing or
// non-finite, zero or negative secondsPerLoad; a rear-inch limit that is not finite and positive
// or above kMaxRearInchLimit; a block size, scarcity window or gap that is not a whole number at
// least 1; an unknown method policy; a routing count mode or scope other than reservedTrucks per
// lane and ship date; a present acceptance block with a missing, non-finite or negative x or y.
M5PolicyProfile loadProfile(const nlohmann::json& m5Block);

// Whether the params root asks for M5: an "m5" object whose "enabled" is true.
bool m5Enabled(const nlohmann::json& root);

constexpr double kMaxRearInchLimit = 120.0;
// The largest allowance a lane-date may declare: about 31 years. Beyond it the product of seconds
// per load and reservations is a configuration error, not a silently saturated deadline.
constexpr double kMaxAllowanceSeconds = 1.0e9;

// secondsPerLoad x reservedTrucks, checked: empty with `error` set for a non-finite or non-positive
// rate, negative reservations or a product past kMaxAllowanceSeconds. Zero reservations give zero.
std::optional<double> laneAllowanceSeconds(double secondsPerLoad, long long reservedTrucks, std::string* error);

const char* methodPolicyName(MethodPolicy policy);

} // namespace m5
} // namespace ob
