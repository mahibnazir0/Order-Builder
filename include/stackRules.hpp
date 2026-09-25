#pragma once

#include "joiner.hpp"
#include "paramsTypes.hpp"
#include "stackTypes.hpp"

#include <optional>
#include <string>
#include <vector>

namespace ob {

// Never throws; a bad line comes back with UnitLoad::error set. The product master
// has no weight-above column yet, so callers normally omit suppliedWeightAboveLb and
// the value is derived; a supplied value (including 0) always wins over derivation.
UnitLoad buildUnitLoad(const JoinedLine& line, const M2Params& params,
                       std::optional<double> suppliedWeightAboveLb = std::nullopt);

// Exact pallet ids on matched lines that have no spec in params, sorted and unique.
// Call once over the joined demand and report every id, before building unit loads.
std::vector<std::string> missingPalletIds(const std::vector<JoinedLine>& lines,
                                          const M2Params& params);

// Whether `top` may sit on `base` under a trailer stack-height ceiling. Pure: no
// allocation, logging, pallet lookup, or dependence on groups.
StackFeasibility canStack(const UnitLoad& base, const UnitLoad& top,
                          const M2Params& params, double ceilingIn) noexcept;

} // namespace ob
