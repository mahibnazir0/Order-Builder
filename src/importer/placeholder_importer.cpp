#include "placeholder_importer.hpp"
#include "logger.hpp"
#include "json.hpp"

#include <fstream>
#include <stdexcept>

namespace ob {

using json = nlohmann::json;

namespace {

// Same safe-access helper used by the demand reader: return the field, or a
// default if it is missing, null, or the wrong type. Keeps one bad record from
// killing the whole load; the Validator judges the data afterwards.
template <typename T>
T get_or(const json& j, const char* key, T fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    try {
        return it->get<T>();
    } catch (const json::exception&) {
        return fallback;
    }
}

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

    if (root.contains("PHOLDER") && root["PHOLDER"].is_array()) {
        result.placeholders.reserve(root["PHOLDER"].size());
        for (const auto& item : root["PHOLDER"]) {
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
