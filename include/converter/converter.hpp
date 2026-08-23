#pragma once

// Converter: normalizes/transforms raw imported demand records into the
// shape needed for joining (e.g. unit conversions, field normalization).
//
// Milestone 1: declarations only.

#include "demand_types.hpp"
#include "types.hpp"

namespace ob {

class Converter {
public:
    // TODO(dev2): normalize a parsed DemandFile ready for joining.
    static DemandFile convert(const DemandFile& raw);
};

}  // namespace ob
