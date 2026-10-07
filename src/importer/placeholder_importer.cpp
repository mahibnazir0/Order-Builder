#include "placeholder_importer.hpp"
#include "inputFile.hpp"
#include "logger.hpp"
#include "json_util.hpp"
#include "validator.hpp"

#include <fstream>
#include <stdexcept>
#include <unordered_set>

using namespace std;

namespace ob {

using json = nlohmann::json;

namespace {

PlaceholderRecord parse_placeholder(const json& j) {
    PlaceholderRecord p;
    p.locfrno         = get_or<string>(j, "LOCFRNO", "");
    p.loctono         = get_or<string>(j, "LOCTONO", "");
    p.ship_cond       = get_or<string>(j, "SHIP_COND", "");
    p.datfr_ta        = get_or<string>(j, "DATFR_TA", "");
    p.datto_ta        = get_or<string>(j, "DATTO_TA", "");
    p.zzna_equip_size = get_or<string>(j, "ZZNA_EQUIP_SIZE", "");
    p.no_of_loads     = getIntegerOr(j, "NO_OF_LOADS", kUnreadableLoadCount);
    p.ebeln           = get_or<string>(j, "EBELN", "");
    return p;
}

} // anonymous namespace

PlaceholderLoadResult PlaceholderImporter::load(const string& json_path) {
    ifstream in(json_path);
    if (!isRegularFile(json_path) || !in) {
        throw runtime_error("Cannot open placeholder file: " + json_path);
    }

    json root;
    try {
        in >> root;
    } catch (const json::exception& e) {
        // parse_error for bad syntax, out_of_range for a number too large for a double.
        throw runtime_error("Placeholder file is not valid JSON: " + string(e.what()));
    }

    // find() on an array or scalar root returns end(), so get_optional_array would
    // read PHOLDER as absent and the file would load as zero trucks.
    if (!root.is_object()) {
        throw runtime_error("Placeholder file: root must be a JSON object");
    }

    PlaceholderLoadResult result;

    // Present but the wrong shape (e.g. PHOLDER as an object) is malformed
    // input and throws instead of being treated as an absent block — see
    // get_optional_array.
    if (const auto* pholder = get_optional_array(root, "PHOLDER", "Placeholder file")) {
        result.placeholders.reserve(pholder->size());
        unordered_set<string> lanesSeen;
        lanesSeen.reserve(pholder->size());
        for (const auto& item : *pholder) {
            PlaceholderRecord p = parse_placeholder(item);
            if (p.no_of_loads >= 0 && p.no_of_loads <= kMaxLoadsPerPlaceholder) {
                result.total_loads += p.no_of_loads;
            }
            // '\x1f' cannot occur in a location code, so distinct lanes never share a key.
            if (!lanesSeen.insert(p.locfrno + '\x1f' + p.loctono + '\x1f' + p.ship_cond).second) {
                ++result.duplicateLaneEntries;
            }
            result.placeholders.push_back(std::move(p));
        }
    }

    LOG_INFO("Loaded placeholders: " + to_string(result.placeholders.size())
             + " entries, " + to_string(result.total_loads) + " trucks requested");
    if (result.duplicateLaneEntries > 0) {
        LOG_WARN(to_string(result.duplicateLaneEntries)
                 + " placeholder entries repeat a lane listed earlier; their NO_OF_LOADS are"
                   " added to that lane's trucks, not overwritten");
    }

    return result;
}

} // namespace ob
