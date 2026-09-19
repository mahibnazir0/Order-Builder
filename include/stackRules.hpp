#pragma once

#include "joiner.hpp"
#include "paramsTypes.hpp"
#include "stackTypes.hpp"

#include <optional>
#include <vector>

namespace ob {

// Construction owns an ID string and can allocate; invalid input is returned as
// UnitLoad::error. The current M1 importer has no supplied weight-above field.
UnitLoad buildUnitLoad(const JoinedLine& line, const M2Params& params,
                       std::optional<double> suppliedWeightAboveLb = std::nullopt);

// Call once over joined demand before construction; exact IDs, sorted and unique.
std::vector<std::string> missingPalletIds(const std::vector<JoinedLine>& lines,
                                        const M2Params& params);

// No allocation, logging, pallet lookup or group/segregation dependencies.
StackFeasibility canStack(const UnitLoad& base, const UnitLoad& top,
                          const M2Params& params, double ceilingIn) noexcept;

} // namespace ob
