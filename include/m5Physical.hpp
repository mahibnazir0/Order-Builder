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
#include "m5Types.hpp"
#include "trailerSpec.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ob {
namespace m5 {

struct PhysicalSettings {
    FloorPatternSettings floor;
    std::optional<double> maxUnitLoadsPerTruck;
    // Row groupings one packing check may search. Final validation searches with floor's own
    // limit; a packing refusal under this smaller one only costs fill, never a wrong truck.
    long long searchStatesPerPackingCheck = 20000;
    BinPackerSettings binPacker;  // method B's careful packing of a block
};

TrailerSpec trailerOf(const TrailerCapability& capability);

struct PhysicalCheck {
    bool fits = false;
    bool undecided = false;              // refused only because the row search ran out of states
    std::vector<std::string> findings;
    FloorLayout layout;                  // indices are positions in the atoms checked
};

// The atoms (indices into input.atoms) as one truck on `capability`. A null budget searches with
// the floor settings' own state limit.
PhysicalCheck checkTruck(const M5LaneDateInput& input, const std::vector<std::size_t>& atoms,
                         const TrailerCapability& capability, const PhysicalSettings& settings,
                         FloorSearchBudget* budget = nullptr);

// Trailer length less the rows' farthest extent, recomputed from the rows (ED-FULL, U-FL-10).
double rearInchesOf(const FloorLayout& layout, const TrailerCapability& capability);

// Atoms physically alike - footprint, height, levels, weight and unit loads to the bit - share a
// class: one that does not fit a truck, its twin does not either.
std::vector<std::size_t> physicalClassOf(const M5LaneDateInput& input);

// The most copies of `prototype` that fit one truck on `capability`, at most `available`. Zero when
// none fits.
int bestSingleClassLoad(const M5LaneDateInput& input, std::size_t prototype, const TrailerCapability& capability,
                        const PhysicalSettings& settings, int available);

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

// Truck after truck, each filled most urgent first, up to maxTrucks: the first k trucks of the
// result are this packing's best plan for k trucks. Atoms that fit no truck alone are skipped.
std::vector<std::vector<std::size_t>> packByUrgency(const M5LaneDateInput& input, const std::vector<std::size_t>& ranked,
                                                    const TrailerCapability& capability, const PhysicalSettings& settings,
                                                    std::size_t maxTrucks, const StopCheck& stop);

// M4's binPacker (branch and price, first-fit decreasing past its item limit) on floor area and
// payload, then each truck trimmed least urgent first until checkTruck passes, then refilled from
// `ranked`. The careful method for one block.
std::vector<std::vector<std::size_t>> packCarefully(const M5LaneDateInput& input, const std::vector<std::size_t>& ranked,
                                                    const TrailerCapability& capability, const PhysicalSettings& settings,
                                                    std::size_t maxTrucks, const StopCheck& stop);

} // namespace m5
} // namespace ob
