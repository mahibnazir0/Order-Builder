#include "importer/importer.hpp"

#include <stdexcept>

namespace ob {

// TODO(mahib): implement.
std::vector<ProductRecord> Importer::import_products(const std::string& /*path*/) {
    throw std::logic_error("Importer::import_products not implemented");
}

// TODO(dev2): implement.
DemandFile Importer::import_demand(const std::string& /*path*/) {
    throw std::logic_error("Importer::import_demand not implemented");
}

// TODO(mahib): implement.
std::vector<PlaceholderRecord> Importer::import_placeholder(const std::string& /*path*/) {
    throw std::logic_error("Importer::import_placeholder not implemented");
}

}  // namespace ob
