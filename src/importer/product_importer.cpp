#include "product_importer.hpp"
#include "logger.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cctype>

namespace ob {

namespace {

// Trim leading/trailing whitespace (and stray \r from CRLF files on Windows).
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Split one CSV line on commas. The product file has no quoted/embedded commas,
// so a simple split is correct here; if that ever changes this is the one place
// to upgrade to a quote-aware parse.
std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::string cell;
    std::stringstream ss(line);
    while (std::getline(ss, cell, ',')) out.push_back(trim(cell));
    return out;
}

// Safe numeric parse: return fallback if the cell is blank or not a number,
// rather than throwing. The Validator decides whether a 0 here is acceptable.
//
// std::stod/std::stoi stop at the first character they can't parse rather
// than rejecting the whole string, so "9,500" would silently come back as 9
// (a thousands separator, orders of magnitude off) instead of falling back.
// Checking that the parse consumed the entire cell catches that.
double to_double(const std::string& s, double fallback = 0.0) {
    if (s.empty()) return fallback;
    try {
        size_t consumed = 0;
        const double v = std::stod(s, &consumed);
        return (consumed == s.size()) ? v : fallback;
    } catch (...) { return fallback; }
}
int to_int(const std::string& s, int fallback = 0) {
    if (s.empty()) return fallback;
    try {
        size_t consumed = 0;
        const int v = std::stoi(s, &consumed);
        return (consumed == s.size()) ? v : fallback;
    } catch (...) { return fallback; }
}

// Case-insensitive header match, so "UoM" / "uom" / "UOM" all resolve.
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

} // anonymous namespace

ProductLoadResult ProductImporter::load(const std::string& csv_path) {
    std::ifstream in(csv_path);
    if (!in) {
        throw std::runtime_error("Cannot open product file: " + csv_path);
    }

    std::string header_line;
    if (!std::getline(in, header_line)) {
        throw std::runtime_error("Product file is empty: " + csv_path);
    }

    // Map expected column name -> its index in this file, so column order
    // changes in the source do not break the reader.
    std::vector<std::string> headers = split_csv(header_line);
    std::unordered_map<std::string, int> col;
    col.reserve(headers.size());
    for (int i = 0; i < static_cast<int>(headers.size()); ++i) {
        // Match duplicate detection to lookup's case folding; the first column wins.
        const auto insertion = col.emplace(lower(headers[i]), i);
        if (!insertion.second) {
            LOG_WARN("Duplicate product column '" + headers[i]
                     + "' at column " + std::to_string(i + 1)
                     + "; using first occurrence at column "
                     + std::to_string(insertion.first->second + 1));
        }
    }

    const char* required[] = {"id", "length", "width", "height", "strength",
                              "uom", "weight", "cases_unit_load", "pallet_id"};
    for (const char* r : required) {
        if (col.find(r) == col.end()) {
            throw std::runtime_error("Product file missing required column: " + std::string(r));
        }
    }

    auto at = [&](const std::vector<std::string>& cells, const char* name) -> std::string {
        auto it = col.find(name);
        if (it == col.end()) return "";
        int idx = it->second;
        return (idx < static_cast<int>(cells.size())) ? cells[idx] : "";
    };

    ProductLoadResult result;
    std::unordered_map<std::string, size_t> id_to_index; // for duplicate detection

    std::string line;
    while (std::getline(in, line)) {
        if (trim(line).empty()) continue;
        std::vector<std::string> c = split_csv(line);
        ++result.rows_read;

        ProductRecord p;
        p.id               = at(c, "id");
        p.description      = at(c, "description");
        p.length_in        = to_double(at(c, "length"));
        p.width_in         = to_double(at(c, "width"));
        p.height_in        = to_double(at(c, "height"));
        p.strength         = to_int(at(c, "strength"));
        p.uom              = at(c, "uom");
        p.weight_lb        = to_double(at(c, "weight"));
        p.cases_layer      = to_int(at(c, "cases_layer"));
        p.layers_unit_load = to_int(at(c, "layers_unit_load"));
        p.cases_unit_load  = to_int(at(c, "cases_unit_load"));
        p.pallet_id        = at(c, "pallet_id");

        // Rows sharing an ID are pallet-type variants (TLD / PTL / PGM / GMA),
        // not data errors: same product, different Cases_Unit_Load. They are
        // ALL kept so the Joiner can choose the right one. duplicate_ids counts
        // how many IDs appear more than once, purely for reporting.
        if (id_to_index.find(p.id) != id_to_index.end()) {
            ++result.duplicate_ids;
        } else {
            id_to_index[p.id] = result.products.size();
        }
        result.products.push_back(p);
    }

    LOG_INFO("Loaded product master: " + std::to_string(result.rows_read)
             + " rows, " + std::to_string(id_to_index.size()) + " unique IDs, "
             + std::to_string(result.duplicate_ids) + " pallet-type variants");

    return result;
}

} // namespace ob
