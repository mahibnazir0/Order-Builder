#pragma once

// Joiner: combines validated product, demand, and placeholder data into
// JoinResult records keyed on the shared join key(s).
//
// Milestone 1: declarations only.

#include <vector>

#include "demand_types.hpp"
#include "types.hpp"

namespace ob {

class Joiner {
public:
    // TODO(dev2): join products + demand + placeholder records.
    static std::vector<JoinResult> join(const std::vector<ProductRecord>& products,
                                         const DemandFile& demand,
                                         const std::vector<PlaceholderRecord>& placeholder);
};

}  // namespace ob
