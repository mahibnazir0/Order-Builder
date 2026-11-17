#pragma once
// ============================================================================
// m5Validation.hpp - whether a candidate may dispatch, on its final contents,
// under the current profile; and the advisory fullness labels of the plan
// selected.
//
// validateFinal runs every mandatory section - physical (each truck's stored
// rows as its witness on its own slot, checkStoredRows; no fresh search),
// conservation, eligibility per component, one group per
// truck, the shared pool, the optional acceptance policy when enabled, and the
// evidence's claims - and any failure makes the candidate non-dispatchable.
// Acceptance OFF is NotApplicable: it neither passes nor withholds anything.
// Acceptance never overrides a hard limit (U-AC-06).
//
// evaluateFullness (ED-FULL, CL-03) labels each truck FULL by weight (100 x
// weight >= 98 x the trailer's limit, exactly), by rear inches (trailer length
// less the stored rows' extent, at most the limit, and only when those rows
// pass checkStoredRows), or floor-limited: only when the
// group has eligible goods left and none of them fits the truck within M4's
// supported layouts, each refusal decided, not cut short. A search cut short
// is Unknown, and with no weight or rear reason the label is Undetermined, not
// a conclusive NOT FULL. Labels never withhold a truck.
// ============================================================================

#include "m5Ledger.hpp"
#include "m5Physical.hpp"
#include "m5Policy.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ob {
namespace m5 {

// Sections run in turn; a stop that arrives before they finish leaves the report incomplete,
// which never passes.
ValidationReport validateFinal(const CandidatePlan& plan, const DemandSnapshot& snapshot, const M5PolicyProfile& profile,
                               const PhysicalSettings& physical, const StopCheck& stop = StopCheck());

// Whether the plan's validation still describes it: same revision, profile, snapshot and pool.
bool validationCurrent(const CandidatePlan& plan, const DemandSnapshot& snapshot, const M5PolicyProfile& profile);

enum class RemainingFitStatus { NotApplicable, Fits, ProvenNoFit, Unknown };
enum class FullnessState { Full, NotFull, Undetermined };
enum class FullnessBucket { GoodsRemain, NoGoodsRemain, RuleLimited };

struct FullnessLabel {
    std::size_t truck = 0;
    FullnessState state = FullnessState::Undetermined;
    bool byWeight = false;
    bool byRearInches = false;
    bool byFloorLimit = false;
    double weightPercent = 0.0;
    double rearInches = 0.0;
    RemainingFitStatus fitStatus = RemainingFitStatus::NotApplicable;
    std::size_t probes = 0;           // distinct physical classes of the group's eligible residual
    std::string fitScope;
    std::optional<FullnessBucket> bucket;  // empty only when the declared source scope is incomplete
    std::uint64_t candidateRevision = 0;
    std::string profileKey;
    SnapshotId remainingSnapshot;
};

std::vector<FullnessLabel> evaluateFullness(const CandidatePlan& plan, const DemandSnapshot& snapshot,
                                            const RemainingDemandView& remaining, const M5PolicyProfile& profile,
                                            const PhysicalSettings& physical, const StopCheck& stop = StopCheck());

const char* fullnessStateName(FullnessState state);
const char* fullnessBucketName(FullnessBucket bucket);
const char* remainingFitName(RemainingFitStatus status);

} // namespace m5
} // namespace ob
