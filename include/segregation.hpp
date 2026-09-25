#pragma once

#include "joiner.hpp"
#include "paramsTypes.hpp"
#include "segregationTypes.hpp"

namespace ob {

// Splits joined demand into groups per lane (ship-from, ship-to, ship condition),
// and per planner where a do-not-mix pair flags the line's planner at its origin.
// JoinedLine::str must be non-null, as Joiner::join guarantees.
// Pairs loaded counts input entries; pairs with demand counts distinct matches.
SegregationResult segregate(const std::vector<JoinedLine>& lines,
                            const std::vector<DoNotMixPair>& doNotMixPairs,
                            SegregationReading reading);

} // namespace ob
