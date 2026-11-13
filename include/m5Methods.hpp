#pragma once
// ============================================================================
// m5Methods.hpp - M5's two planning methods, each offering every group of the
// lane-date its best plan for each number of trucks (a GroupSupply); the
// allocation, not the method, decides which trucks dispatch.
//
// Method A, repeated patterns (the client's "build an ideal truck and repeat
// it"): for each physical class, the most copies one truck takes (M4's
// floorPattern), repeated while whole trucks of the class remain and slots
// allow, drawing goods in rank order; the remainder is packed by M4's packing
// layer through m5Physical, never by re-entering the selector (U-RM-01).
//
// Method B, rolling blocks (Tom's Q1): the ranked goods are taken a block of
// at most blockSize trucks at a time - the longest prefix whose floor/payload
// bound fits the block - and each block packed carefully (M4's binPacker,
// then the truck check). Blocks bound the search; they never take
// reservations themselves (X-BLK-04) and the last is never padded.
//
// Both rank goods by their most urgent line's class (m5Allocation's urgency
// domain), then canonical order. Can-go goods rank after every good due by
// the departure, so they fill only room required goods leave.
// ============================================================================

#include "m5Allocation.hpp"
#include "m5Physical.hpp"
#include "m5Policy.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace ob {
namespace m5 {

// What a method plans one group with. Holds references to the orchestrator's state for one call.
struct MethodContext {
    const DemandSnapshot& snapshot;
    const UrgencyDomain& domain;
    const M5PolicyProfile& profile;
    const PhysicalSettings& physical;
    const AllocationComparator& comparator;
    GeometryCache& cache;
    const std::vector<char>& unfittable;  // parallel to the atoms: fits no slot on its own
    std::size_t maxTrucks = 0;            // the pool's size
    StopCheck stop;
    // Test seam: called after each of method B's blocks with the trucks built so far.
    std::function<void(std::size_t)> afterBlock;

    MethodContext(const MethodContext&) = delete;
    MethodContext& operator=(const MethodContext&) = delete;
};

class MethodPlanner {
public:
    virtual ~MethodPlanner() = default;
    virtual MethodId id() const = 0;
    // Pure with respect to the snapshot: plans from it, claims nothing.
    virtual GroupSupply supplyForGroup(const MethodContext& context, const GroupId& group) const = 0;
};

class PatternPlanner final : public MethodPlanner {
public:
    MethodId id() const override { return MethodId::PatternRepeat; }
    GroupSupply supplyForGroup(const MethodContext& context, const GroupId& group) const override;
};

class BlockPlanner final : public MethodPlanner {
public:
    MethodId id() const override { return MethodId::RollingBlocks; }
    GroupSupply supplyForGroup(const MethodContext& context, const GroupId& group) const override;
};

// The group's atoms a plan may carry, most urgent first: eligible on the departure, fitting a slot
// alone, and - with can-go fill off - due by the departure.
std::vector<std::size_t> rankedCandidates(const MethodContext& context, const GroupId& group);

// The L1 bound on the trucks `atoms` need on floor area, payload and the unit-load cap: no packing
// of them uses fewer (Section 8.2). A bound of the capacity-only model, never a layout.
long long trucksAtLeast(const M5LaneDateInput& input, const std::vector<std::size_t>& atoms,
                        const TrailerCapability& capability, const PhysicalSettings& physical);

// Pattern copies: min(stock / patternSize, slots), checked. Zero for an empty pattern or no stock
// (X-PAT-01, X-PAT-03); counts past 2^62 are refused rather than wrapped (X-PAT-04).
long long patternCopies(long long stock, long long patternSize, long long slots);

// A pattern of several products: copies limited by each product's stock over its count in the
// pattern, and by slots (X-PAT-02). `stock` and `perCopy` are parallel; a product the pattern does
// not use (perCopy 0) limits nothing; an empty pattern gives zero.
long long mixedPatternCopies(const std::vector<long long>& stock, const std::vector<long long>& perCopy, long long slots);

// A group's supply from candidate plans, each in the order its trucks are best taken: for each k,
// the best under the comparator of every candidate's first k trucks, rebalanced and withheld under
// the optional acceptance policy when it is on.
GroupSupply supplyFromPlans(const MethodContext& context, const GroupId& group, const std::vector<GroupTrucks>& candidates);

// Pools of at most this many slots also get M4's exact truck-by-truck knapsack plan (ED-12).
constexpr std::size_t kExactSlots = 4;

} // namespace m5
} // namespace ob
