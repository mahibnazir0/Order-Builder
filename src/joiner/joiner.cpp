#include "joiner/joiner.hpp"

#include <stdexcept>

namespace ob {

// TODO(dev2): implement.
std::vector<JoinResult> Joiner::join(const std::vector<ProductRecord>& /*products*/,
                                      const DemandFile& /*demand*/,
                                      const std::vector<PlaceholderRecord>& /*placeholder*/) {
    throw std::logic_error("Joiner::join not implemented");
}

}  // namespace ob
