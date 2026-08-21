#include "validator/validator.hpp"

#include <stdexcept>

namespace ob {

// TODO(mahib): implement.
std::vector<ValidationError> Validator::validate_products(
    const std::vector<ProductRecord>& /*products*/) {
    throw std::logic_error("Validator::validate_products not implemented");
}

// TODO(mahib): implement.
std::vector<ValidationError> Validator::validate_demand(const DemandFile& /*demand*/) {
    throw std::logic_error("Validator::validate_demand not implemented");
}

// TODO(mahib): implement.
std::vector<ValidationError> Validator::validate_placeholder(
    const std::vector<PlaceholderRecord>& /*records*/) {
    throw std::logic_error("Validator::validate_placeholder not implemented");
}

}  // namespace ob
