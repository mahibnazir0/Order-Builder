#pragma once
// ============================================================================
// m5Allocation.hpp - choosing one complete plan across the groups of a
// lane-date, which share one reservation pool (U-SP-02, X-ROUTE-02).
//
// Each method offers, per group, its best plan for every number of trucks k
// (a GroupSupply). allocate() picks one plan per group so their trucks fit the
// pool and the whole is best under the comparator. Nothing is committed before
// that choice, and groups run in canonical order, so neither input order, a
// method's block order nor pattern repetition can take a reservation from more
// urgent goods elsewhere (X-RUN-05, X-E2E-07).
//
// Production comparator (ED-02, from corrected M4): shipped unit loads per
// urgency class, compared lexicographically, most urgent class first. A class
// is M4's (rank date, priority, ship date), the rank date by the lane's
// past-due order (rankDateOf), so goods due by the departure precede can-go
// goods, which rank at their own later dates. More goods shipped of a class
// ranks higher, so using spare trucks raises the key; there is no preference
// for fewer trucks. Equal service is broken by ED-21 (Tom, 14 and 16 Nov 2026):
// T1 shipped unit loads per group in the lane-date's rotated group order
// (TieOrder), then T2 the canonical records (stable source-line ID, then atom
// ID), then truck membership, so the result never depends on input order and
// a tie does not always go to the same group. Fill, rear space, weight and
// truck count never break a tie.
//
// The key is additive over groups and compared lexicographically, which adding
// the same group plans to two partial allocations never reverses, so the
// dynamic programme over (group, slots used) finds the best allocation of the
// supplied plans exactly.
// ============================================================================

#include "m5Ledger.hpp"
#include "m5Policy.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ob {
namespace m5 {

struct UrgencyClass {
    std::string rankDate;
    int priority = 0;
    std::string shipDate;
    bool operator<(const UrgencyClass& other) const;
    bool operator==(const UrgencyClass& other) const;
};

// The classes every candidate of the lane-date is compared on, most urgent first.
class UrgencyDomain {
public:
    explicit UrgencyDomain(const DemandSnapshot& snapshot);
    std::size_t size() const { return classes_.size(); }
    std::size_t indexOf(const Component& component) const;
    UrgencyClass classOf(const Component& component) const;
    // An atom's most urgent component's class index: the order packing takes goods in.
    std::size_t atomRank(const Atom& atom) const;

private:
    std::string departure_;
    PastDueOrder order_;
    std::vector<UrgencyClass> classes_;
};

using Truck = std::vector<std::size_t>;  // atom indices, ascending
using GroupTrucks = std::vector<Truck>;

struct AllocationKey {
    std::vector<long long> shippedByClass;     // thousandths of a unit load per class
    std::vector<long long> shippedByPriority;  // per priority 0..kMaxPriority (FX-PRIORITY)
    long long loadedTrucks = 0;
    std::map<GroupId, long long> shippedByGroup;  // thousandths of a unit load (ED-21 T1)
    // ED-21 T2: every shipped component as (stable source-line ID, atom ID), sorted.
    std::vector<std::pair<SourceLineId, std::string>> records;
    // Per group in canonical order: each truck's sorted source identities, the trucks sorted.
    std::vector<std::vector<std::vector<std::string>>> membership;
};

AllocationKey keyOf(const DemandSnapshot& snapshot, const UrgencyDomain& domain, const GroupTrucks& trucks);
// A whole candidate's key, its groups in canonical order.
AllocationKey keyOfPlan(const DemandSnapshot& snapshot, const UrgencyDomain& domain, const CandidatePlan& plan);
void addTo(AllocationKey& total, const AllocationKey& part);

class AllocationComparator {
public:
    virtual ~AllocationComparator() = default;
    // A strict weak order: true when `left` is strictly better.
    virtual bool better(const AllocationKey& left, const AllocationKey& right) const = 0;
    virtual std::string id() const = 0;
};

// ED-21 T1's group order for one lane-date: the groups with eligible demand by stable ID (bytewise),
// started at r = (laneKey mod g + dayNumber mod g) mod g and wrapped around.
struct TieOrder {
    std::uint32_t laneKey = 0;
    long long dayNumber = 0;       // whole days from 1970-01-01 to the ship date
    std::size_t rotation = 0;      // r
    std::vector<GroupId> groups;   // rotated; empty when no group has eligible demand
};

// 32-bit FNV-1a (offset basis 2166136261, prime 16777619, arithmetic mod 2^32).
std::uint32_t fnv1a32(const std::string& text);
// "origin|destination|mode" from the lane codes as M4 normalises them.
std::string tieLaneText(const LaneKey& lane);
TieOrder tieOrderFor(const DemandSnapshot& snapshot);

// ComparatorKind::FxPriority is the synthetic fixture comparator (v7 0.6): priority vector, then
// fewer trucks, then membership. Tests select it; the loader never does. The production comparator
// applies `ties` (ED-21 T1); with no groups in it, T1 is skipped and T2 decides.
std::unique_ptr<AllocationComparator> makeComparator(ComparatorKind kind, TieOrder ties = {});

struct GroupSupply {
    GroupId group;
    std::vector<GroupTrucks> plans;  // plans[k]: the best plan found with at most k trucks
    bool exact = false;              // every plan proven best for its k (exact small solver only)
};

struct AllocationResult {
    std::vector<GroupTrucks> chosen;  // parallel to the supplies
    AllocationKey key;
    bool provenBest = false;          // best of all allocations, not only of the plans supplied
};

AllocationResult allocate(const DemandSnapshot& snapshot, const UrgencyDomain& domain,
                          const std::vector<GroupSupply>& supplies, std::size_t slots,
                          const AllocationComparator& comparator);

} // namespace m5
} // namespace ob
