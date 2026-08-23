#pragma once
// importer.hpp — declares the demand reader (product + placeholder readers to follow)
#include "demand_types.hpp"
#include <string>

namespace ob {

class Importer {
public:
    // Read Demand-1.json into a DemandFile.
    // Throws std::runtime_error if the file cannot be opened or is not valid JSON.
    // Missing fields inside a record default to empty/zero — the Validator, not
    // the Importer, decides whether that is acceptable.
    static DemandFile load_demand(const std::string& json_path);
};

} // namespace ob
