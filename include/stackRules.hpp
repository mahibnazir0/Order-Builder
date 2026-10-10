#pragma once

#include "joiner.hpp"
#include "paramsTypes.hpp"
#include "stackTypes.hpp"

#include <optional>
#include <string>
#include <vector>

namespace ob {

// The pallet a product rides on. Each figure comes from the product row when the master
// supplies it, otherwise from the pallet table row for its Pallet_ID; nullopt when some figure
// has neither source. A placeholder deck height (0.1 in) comes back as 0. Figures are not
// range-checked here: buildUnitLoad rejects a negative or non-finite one as InvalidData.
std::optional<PalletSpec> resolvePalletSpec(const ProductRecord& product, const M2Params& params);

// Height of one built unit load. The deck counts only when params.floorDeckHeight says so.
// Milestone 2 (buildUnitLoad) and the Milestone 3 floor (unitLoadMetricsFor) both call this,
// so the two can never read the deck differently.
double unitLoadHeightIn(const ProductRecord& product, const PalletSpec& pallet,
                        const M2Params& params) noexcept;

// Never throws; a bad line comes back with UnitLoad::error set. The product master
// has no weight-above column yet, so callers normally omit suppliedWeightAboveLb and
// the value is derived; a supplied value (including 0) always wins over derivation.
UnitLoad buildUnitLoad(const JoinedLine& line, const M2Params& params,
                       std::optional<double> suppliedWeightAboveLb = std::nullopt);

// Pallet ids of matched lines whose pallet figures neither the product row nor the pallet
// table supplies, sorted and unique.
// Call once over the joined demand and report every id, before building unit loads.
std::vector<std::string> missingPalletIds(const std::vector<JoinedLine>& lines,
                                          const M2Params& params);

// Whether the load's own upper layers already weigh more than its CRI's safe limit. Such
// a product cannot support its own build: it still ships single-high, and canStack never
// lets it carry anything. A blank CRI (0) has no limit to exceed.
bool exceedsOwnCri(const UnitLoad& load, const M2Params& params) noexcept;

// Whether `top` may sit on `base` under a trailer stack-height ceiling. Pure: no
// allocation, logging, pallet lookup, or dependence on groups.
StackFeasibility canStack(const UnitLoad& base, const UnitLoad& top,
                          const M2Params& params, double ceilingIn) noexcept;

} // namespace ob
