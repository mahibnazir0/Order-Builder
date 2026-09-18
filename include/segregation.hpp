#pragma once

#include "joiner.hpp"
#include "paramsTypes.hpp"
#include "segregationTypes.hpp"

namespace ob {

// JoinedLine::str must be valid, as guaranteed by Joiner::join.
// Pairs loaded counts input entries; pairs with demand counts distinct matches.
SegregationResult segregate(const std::vector<JoinedLine>& lines,
                            const std::vector<DoNotMixPair>& doNotMixPairs,
                            SegregationReading reading);

} // namespace ob
