#pragma once
// importer.hpp — declares the demand reader (product + placeholder readers to follow)
#include "demand_types.hpp"
#include <string>

namespace ob {

class Importer {
public:
    // Read a demand file into a DemandFile. The name varies by extract (Demand-1.json in
    // August and September, 100-STR-<id>.json from 29 Sep); only the contents matter.
    // Throws std::runtime_error if the file cannot be opened or is not valid JSON.
    // Missing fields inside a record default to empty/zero — the Validator, not
    // the Importer, decides whether that is acceptable.
    static DemandFile load_demand(const std::string& json_path);
};

} // namespace ob
