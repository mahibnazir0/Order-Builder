#pragma once
// ============================================================================
// m5Physical.hpp - M5's one gateway to M4's physical behaviour: the shared
// truck check (truckCheck), floorPattern's rows, and M4's packers.
//
// M5 never stacks atoms on atoms (stacks come from M2), never invents a floor
// pattern, and never lets cube stand in for a layout (INV-20). Every truck it
// builds is judged by checkTruck against the trailer of the slot it uses.
// Packing works in the order goods rank, so a truck holds the most urgent goods
// that fit it before any less urgent one: an early, low-priority truck cannot
// starve urgent goods, because which trucks dispatch is the allocation's call.
// ============================================================================

#include "binPacker.hpp"
#include "floorPattern.hpp"
#include "m5FloorSearch.hpp"
#include "m5Types.hpp"
#include "trailerSpec.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ob {
namespace m5 {

struct PhysicalSettings {
    FloorPatternSettings floor;
    // Row groupings one packing check may search, at most floor's per-search limit. Final validation
    // searches with floor's own limit; a packing refusal under this smaller one only costs fill, never a
    // wrong truck.
    long long searchStatesPerPackingCheck = 20000;
    BinPackerSettings binPacker;  // method B's careful packing of a block
    KnapsackSettings knapsack;    // the exact truck-by-truck plans of a small pool
    // The route context's shared floor-search states (ED-19). Copies of these settings share it, so a
    // method, block, repair or probe that copies them draws on the same states. planLaneDate installs
    // one per lane-date; without one each search gets its per-call states afresh.
    std::shared_ptr<RouteSearchBudget> routeBudget;
};

TrailerSpec trailerOf(const TrailerCapability& capability);

// The slots' indices with the largest unit-load cap first, ties in the pool's own order. Truck t of a
// plan is built for the t-th of them, so a cap travels with its slot, never with an array position.
std::vector<std::size_t> slotsBestFirst(const ReservationPool& pool);

// True when every slot has the same trailer, payload and cap.
bool slotsAreAlike(const ReservationPool& pool);

// True when the slots share one trailer's size and payload, whatever their unit-load caps (ED-20).
bool slotsDifferOnlyInCap(const ReservationPool& pool);

struct PhysicalCheck {
    bool fits = false;
    bool undecided = false;              // refused only because the row search ran out of states
    SearchStop stop = SearchStop::None;  // which limit an undecided check met (checkTruckInRoute)
    long long statesCharged = 0;         // route states the check spent (checkTruckInRoute)
    std::vector<std::string> findings;
    FloorLayout layout;                  // indices are positions in the atoms checked
};

// The atoms (indices into input.atoms) as one truck on `capability`: its size, payload and unit-load
// cap (whole counts, checked, compared exactly), then its floor rows. A null budget searches with the
// floor settings' own state limit.
PhysicalCheck checkTruck(const M5LaneDateInput& input, const std::vector<std::size_t>& atoms,
                         const TrailerCapability& capability, const PhysicalSettings& settings,
                         FloorSearchBudget* budget = nullptr);

// checkTruck as a search of the route context: its row search may hold up to `callStates` states, no more
// than the route budget's per-call cap, drawn from the route's shared states (none once they are spent or
// the wall deadline has passed), and gives back what it did not use. An undecided check names its stop.
PhysicalCheck checkTruckInRoute(const M5LaneDateInput& input, const std::vector<std::size_t>& atoms,
                                const TrailerCapability& capability, const PhysicalSettings& settings, long long callStates);

// The settings with a route budget for one context, unless they already carry one (a wrapper or retry
// inside the context never gets another). Per call: floor's per-search limit; route: floor's route limit.
PhysicalSettings withRouteBudget(const PhysicalSettings& settings, const std::string& scope, std::function<bool()> wallStop);

// Trailer length less the rows' farthest extent, recomputed from the rows (ED-FULL, U-FL-10).
double rearInchesOf(const FloorLayout& layout, const TrailerCapability& capability);

// A layout supplied as rectangles on the floor (U-FL-10): measured as given, never compacted.
struct PlacedRectangle {
    double xIn = 0.0;       // along the trailer, from the front
    double yIn = 0.0;       // across it
    double lengthIn = 0.0;  // along
    double widthIn = 0.0;   // across
};

struct WitnessMeasure {
    bool valid = false;
    std::vector<std::string> findings;  // out of bounds, overlap, non-finite
    double occupiedExtentIn = 0.0;      // the farthest x + length, not the sum of lengths
    double rearInches = 0.0;
};

WitnessMeasure measureSuppliedWitness(const std::vector<PlacedRectangle>& placed, const TrailerCapability& capability);

// Pure validation of a supplied layout witness for these atoms on one slot (R-PROOF, X-PAT-18): each atom
// placed exactly once in its own footprint (either way round), on the floor and overlapping none, under the
// ceiling; payload and unit-load cap in whole hundredths; and the claimed extent equal to the measured one.
// It never searches and never arranges a replacement: a witness that fails is rejected as supplied.
WitnessMeasure validateSuppliedWitness(const M5LaneDateInput& input, const std::vector<std::size_t>& atoms,
                                       const LayoutWitness& witness, const TrailerCapability& capability);

// Atoms physically alike - footprint, height, levels, weight and unit loads to the bit - share a
// class: one that does not fit a truck, its twin does not either. The class signature names the atom's
// content, so it is the same in every lane-date.
std::string classSignatureOf(const Atom& atom);
std::vector<std::size_t> physicalClassOf(const M5LaneDateInput& input);

// The most copies of `prototype` that fit one truck on `capability`, at most `available`. Zero when
// none fits. Its searches draw on the route budget; `decided`, when given, says whether every larger
// count was refused with proof, so only a decided answer may be cached (an Undecided refusal never is).
int bestSingleClassLoad(const M5LaneDateInput& input, std::size_t prototype, const TrailerCapability& capability,
                        const PhysicalSettings& settings, int available, bool* decided = nullptr);

// Thread-safe memo of bestSingleClassLoad by class signature and trailer signature. Holds geometry
// only - never acceptance, eligibility or stock (INV-21).
class GeometryCache {
public:
    std::optional<int> find(const std::string& key) const;
    void put(const std::string& key, int copies);

private:
    mutable std::mutex mutex_;
    std::map<std::string, int> copiesByKey_;
};

using StopCheck = std::function<bool()>;

// Adds atoms from `ranked` (most urgent first) not yet `placed` to the truck while it still passes
// checkTruck; marks what it adds. Once one atom of a physical class is refused, its twins are not
// tried on this truck.
void fillTruck(const M5LaneDateInput& input, const std::vector<std::size_t>& classOf, std::vector<std::size_t>& truck,
               const std::vector<std::size_t>& ranked, std::vector<char>& placed, const TrailerCapability& capability,
               const PhysicalSettings& settings, const StopCheck& stop);

// Truck after truck, each filled most urgent first, truck t for slots[t] and at most slots.size()
// trucks: the first k trucks of the result are this packing's best plan for k trucks. Atoms that fit
// no truck alone are skipped.
std::vector<std::vector<std::size_t>> packByUrgency(const M5LaneDateInput& input, const std::vector<std::size_t>& ranked,
                                                    const std::vector<TrailerCapability>& slots, const PhysicalSettings& settings,
                                                    const StopCheck& stop);

// The atom as M4's packers see it, ranked by its most urgent line under the lane's past-due order.
KnapsackItem knapsackItemOf(const M5LaneDateInput& input, std::size_t atom);

// M4's knapsack truck after truck (selectNextTruck), truck t judged by checkTruck on slots[t], so each
// is the best load of what is left that M4's search can prove. Empty when M4's packer cannot count the
// goods on its grains.
std::vector<std::vector<std::size_t>> packByKnapsack(const M5LaneDateInput& input, const std::vector<std::size_t>& ranked,
                                                     const std::vector<TrailerCapability>& slots, const PhysicalSettings& settings);

// M4's binPacker (branch and price, first-fit decreasing past its item limit) on floor area and
// payload, then each truck trimmed least urgent first until checkTruck passes, then refilled from
// `ranked`. The careful method for one block. Slots that differ in unit-load cap are packed in order
// instead, as M4's packer knows one capacity per bin.
std::vector<std::vector<std::size_t>> packCarefully(const M5LaneDateInput& input, const std::vector<std::size_t>& ranked,
                                                    const std::vector<TrailerCapability>& slots, const PhysicalSettings& settings,
                                                    const StopCheck& stop);

} // namespace m5
} // namespace ob
