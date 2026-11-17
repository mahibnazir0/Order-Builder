#pragma once
// ============================================================================
// m5Repair.hpp - the only ways a candidate changes after it is built.
//
// CandidateEditor is the one mutator of a CandidatePlan: every change raises
// its revision and drops its validation, so a stale "accepted" or "proven"
// can never authorise the changed truck (U-AC-07, R-1c). Repairs work on a
// copy; only a copy that then passes validateFinal replaces the original, and
// an interrupted repair is discarded (X-RUN-03).
//
// rebalanceForAcceptance applies only when the optional minimum-load policy is
// enabled - never for this customer, whose check runs at export. It moves goods
// between trucks of one group (one lane-date, so one departure) until a failing
// truck passes, checking donor and recipient on every move, and undoes the
// moves of a truck it cannot lift; trucks still failing are withheld whole.
// ============================================================================

#include "m5Allocation.hpp"
#include "m5Physical.hpp"
#include "m5Policy.hpp"

#include <cstddef>
#include <vector>

namespace ob {
namespace m5 {

class CandidateEditor {
public:
    explicit CandidateEditor(CandidatePlan& plan);
    CandidateEditor(const CandidateEditor&) = delete;
    CandidateEditor& operator=(const CandidateEditor&) = delete;

    void moveAtom(std::size_t fromTruck, std::size_t toTruck, std::size_t atom);
    void removeToLeftOver(std::size_t truck, std::size_t atom, LeftOverCause cause);
    void removeTruck(std::size_t truck, LeftOverCause cause);

private:
    void touch();
    CandidatePlan& plan_;
};

// The group's trucks rebalanced in place, truck t judged on slots[t]; returns the atoms of trucks
// withheld. Does nothing and returns nothing when the profile has no acceptance policy.
std::vector<std::size_t> rebalanceForAcceptance(GroupTrucks& trucks, const DemandSnapshot& snapshot,
                                                const M5PolicyProfile& profile, const PhysicalSettings& physical,
                                                const std::vector<TrailerCapability>& slots, const StopCheck& stop);

} // namespace m5
} // namespace ob
