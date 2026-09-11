#include "placeholder_importer.hpp"
#include "logger.hpp"
#include "json_util.hpp"

#include <fstream>
#include <stdexcept>

namespace ob {

using json = nlohmann::json;

namespace {

PlaceholderRecord parse_placeholder(const json& j) {
    PlaceholderRecord p;
    p.locfrno         = get_or<std::string>(j, "LOCFRNO", "");
    p.loctono         = get_or<std::string>(j, "LOCTONO", "");
    p.ship_cond       = get_or<std::string>(j, "SHIP_COND", "");
    p.datfr_ta        = get_or<std::string>(j, "DATFR_TA", "");
    p.datto_ta        = get_or<std::string>(j, "DATTO_TA", "");
    p.zzna_equip_size = get_or<std::string>(j, "ZZNA_EQUIP_SIZE", "");
    p.no_of_loads     = get_or<int>(j, "NO_OF_LOADS", 0);
    p.ebeln           = get_or<std::string>(j, "EBELN", "");
    return p;
}

} // anonymous namespace

PlaceholderLoadResult PlaceholderImporter::load(const std::string& json_path) {
    std::ifstream in(json_path);
    if (!in) {
        throw std::runtime_error("Cannot open placeholder file: " + json_path);
    }

    json root;
    try {
        in >> root;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("Placeholder file is not valid JSON: " + std::string(e.what()));
    }

    PlaceholderLoadResult result;

    // Present but the wrong shape (e.g. PHOLDER as an object) is malformed
    // input and throws instead of being treated as an absent block — see
    // get_optional_array.
    if (const auto* pholder = get_optional_array(root, "PHOLDER", "Placeholder file")) {
        result.placeholders.reserve(pholder->size());
        for (const auto& item : *pholder) {
            PlaceholderRecord p = parse_placeholder(item);
            result.total_loads += p.no_of_loads;
            result.placeholders.push_back(p);
        }
    }

    LOG_INFO("Loaded placeholders: " + std::to_string(result.placeholders.size())
             + " entries, " + std::to_string(result.total_loads) + " trucks requested");

    return result;
}

} // namespace ob
