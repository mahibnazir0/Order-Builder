#pragma once

// Validator: checks imported/converted data for correctness and
// collects ValidationError entries. Does not mutate input data.
//
// Milestone 1: declarations only.

#include <vector>

#include "types.hpp"

namespace ob {

class Validator {
public:
    // TODO(mahib): validate product records, return any errors found.
    static std::vector<ValidationError> validate_products(
        const std::vector<ProductRecord>& products);

    // TODO(mahib): validate a converted demand file, return any errors found.
    static std::vector<ValidationError> validate_demand(const DemandFile& demand);

    // TODO(mahib): validate placeholder records, return any errors found.
    static std::vector<ValidationError> validate_placeholder(
        const std::vector<PlaceholderRecord>& records);
};

}  // namespace ob
