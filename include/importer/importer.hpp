#pragma once

// Importer: reads raw input files from disk and parses them into the
// structs defined in types.hpp. No transformation or validation logic
// belongs here -- just file IO + parsing.
//
// Milestone 1: declarations only.

#include <string>

#include "types.hpp"

namespace ob {

class Importer {
public:
    // TODO(mahib): load the product master file into ProductRecord list.
    static std::vector<ProductRecord> import_products(const std::string& path);

    // TODO(dev2): load the demand file (STR/CTL/DNM) into a DemandFile.
    static DemandFile import_demand(const std::string& path);

    // TODO(mahib): load the placeholder input file.
    static std::vector<PlaceholderRecord> import_placeholder(const std::string& path);
};

}  // namespace ob
