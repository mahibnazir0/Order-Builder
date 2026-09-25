#include "placeholder_importer.hpp"
#include "logger.hpp"
#include "json_util.hpp"
#include "validator.hpp"

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
    p.no_of_loads     = getIntegerOr(j, "NO_OF_LOADS", kUnreadableLoadCount);
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
    } catch (const json::exception& e) {
        // parse_error for bad syntax, out_of_range for a number too large for a double.
        throw std::runtime_error("Placeholder file is not valid JSON: " + std::string(e.what()));
    }

    // find() on an array or scalar root returns end(), so get_optional_array would
    // read PHOLDER as absent and the file would load as zero trucks.
    if (!root.is_object()) {
        throw std::runtime_error("Placeholder file: root must be a JSON object");
    }

    PlaceholderLoadResult result;

    // Present but the wrong shape (e.g. PHOLDER as an object) is malformed
    // input and throws instead of being treated as an absent block — see
    // get_optional_array.
    if (const auto* pholder = get_optional_array(root, "PHOLDER", "Placeholder file")) {
        result.placeholders.reserve(pholder->size());
        for (const auto& item : *pholder) {
            PlaceholderRecord p = parse_placeholder(item);
            if (p.no_of_loads >= 0 && p.no_of_loads <= kMaxLoadsPerPlaceholder) {
                result.total_loads += p.no_of_loads;
            }
            result.placeholders.push_back(p);
        }
    }

    LOG_INFO("Loaded placeholders: " + std::to_string(result.placeholders.size())
             + " entries, " + std::to_string(result.total_loads) + " trucks requested");

    return result;
}

} // namespace ob
