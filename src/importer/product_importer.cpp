#include "product_importer.hpp"
#include "inputFile.hpp"
#include "logger.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cctype>

using namespace std;

namespace ob {

namespace {

// Trim leading/trailing whitespace (and stray \r from CRLF files on Windows).
string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Split one CSV line into `cells`, reusing its storage across rows. Quote-aware: the
// 88-column master (29 Sep onward) carries free-text columns, and one quoted comma split
// naively would shift every later column of that row onto the wrong name.
void split_csv(const string& line, vector<string>& cells) {
    cells.clear();
    string cell;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inQuotes) {
            if (c != '"') {
                cell += c;
            } else if (i + 1 < line.size() && line[i + 1] == '"') {
                cell += '"';
                ++i;
            } else {
                inQuotes = false;
            }
        } else if (c == '"') {
            inQuotes = true;
        } else if (c == ',') {
            cells.push_back(trim(cell));
            cell.clear();
        } else {
            cell += c;
        }
    }
    cells.push_back(trim(cell));
}

// Read one CSV record, which spans several lines when a quoted cell holds a newline. A
// record is still open while it has an odd number of quotes; a doubled quote adds two, so
// it never changes that parity. A quote left open at end of file would otherwise swallow
// every row after it into one record, so it is an error rather than a short master.
bool readCsvRecord(istream& in, string& record) {
    if (!getline(in, record)) return false;
    size_t quoteCount = count(record.begin(), record.end(), '"');
    string continuation;
    while (quoteCount % 2 == 1) {
        if (!getline(in, continuation)) {
            throw runtime_error("Product file has an unterminated quote in the record starting: "
                                + record.substr(0, record.find('\n')).substr(0, 80));
        }
        quoteCount += count(continuation.begin(), continuation.end(), '"');
        record += '\n';
        record += continuation;
    }
    return true;
}

// Safe numeric parse: return fallback if the cell is blank or not a number,
// rather than throwing. Weight and the dimensions pass a NaN fallback so the
// Validator can't mistake an unreadable cell for a real 0 (a 0 weight passes
// invalid_weight; a 0 dimension reads as a raw-material zero_dimension).
//
// stod/stoi stop at the first character they can't parse rather
// than rejecting the whole string, so "9,500" would silently come back as 9
// (a thousands separator, orders of magnitude off) instead of falling back.
// Checking that the parse consumed the entire cell catches that.
double to_double(const string& s, double fallback = 0.0) {
    if (s.empty()) return fallback;
    try {
        size_t consumed = 0;
        const double v = stod(s, &consumed);
        return (consumed == s.size()) ? v : fallback;
    } catch (...) { return fallback; }
}
int to_int(const string& s, int fallback = 0) {
    if (s.empty()) return fallback;
    try {
        size_t consumed = 0;
        const int v = stoi(s, &consumed);
        return (consumed == s.size()) ? v : fallback;
    } catch (...) { return fallback; }
}

// Case-insensitive header match, so "UoM" / "uom" / "UOM" all resolve.
string lower(string s) {
    transform(s.begin(), s.end(), s.begin(),
              [](unsigned char c){ return static_cast<char>(tolower(c)); });
    return s;
}

} // anonymous namespace

ProductLoadResult ProductImporter::load(const string& csv_path) {
    ifstream in(csv_path);
    if (!isRegularFile(csv_path) || !in) {
        throw runtime_error("Cannot open product file: " + csv_path);
    }

    string header_line;
    if (!readCsvRecord(in, header_line)) {
        throw runtime_error("Product file is empty: " + csv_path);
    }

    // Map expected column name -> its index in this file, so column order
    // changes in the source do not break the reader.
    vector<string> headers;
    split_csv(header_line, headers);
    unordered_map<string, int> col;
    col.reserve(headers.size());
    for (int i = 0; i < static_cast<int>(headers.size()); ++i) {
        // The 3 Sep extract carries both "Strength" and "strength"; the first wins.
        const auto insertion = col.emplace(lower(headers[i]), i);
        if (!insertion.second) {
            LOG_WARN("Duplicate product column '" + headers[i] + "' at column "
                     + to_string(i + 1) + "; using first occurrence at column "
                     + to_string(insertion.first->second + 1));
        }
    }

    // Description is deliberately not required: the 88-column master (29 Sep onward)
    // dropped it. Cases_Layer and Layers_Unit_Load are, since a missing column would
    // read as 0 on every row and reject the whole day as invalid_layer_data.
    const char* required[] = {"id", "length", "width", "height", "strength", "uom", "weight",
                              "cases_layer", "layers_unit_load", "cases_unit_load", "pallet_id"};
    for (const char* r : required) {
        if (col.find(r) == col.end()) {
            throw runtime_error("Product file missing required column: " + string(r));
        }
    }

    auto at = [&](const vector<string>& cells, const char* name) -> string {
        auto it = col.find(name);
        if (it == col.end()) return "";
        int idx = it->second;
        return (idx < static_cast<int>(cells.size())) ? cells[idx] : "";
    };

    ProductLoadResult result;
    unordered_map<string, size_t> id_to_index; // for duplicate detection

    constexpr double unreadableMeasurement = numeric_limits<double>::quiet_NaN();
    string line;
    vector<string> c;
    c.reserve(headers.size());
    while (readCsvRecord(in, line)) {
        if (trim(line).empty()) continue;
        split_csv(line, c);
        ++result.rows_read;
        if (c.size() != headers.size()) ++result.misalignedRows;

        ProductRecord p;
        p.id               = at(c, "id");
        p.description      = at(c, "description");
        p.length_in        = to_double(at(c, "length"), unreadableMeasurement);
        p.width_in         = to_double(at(c, "width"), unreadableMeasurement);
        p.height_in        = to_double(at(c, "height"), unreadableMeasurement);
        const string strengthCell = at(c, "strength");
        p.strength         = strengthCell.empty() ? 0 : to_int(strengthCell, kUnreadableStrength);
        p.uom              = at(c, "uom");
        p.weight_lb        = to_double(at(c, "weight"), unreadableMeasurement);
        p.cases_layer      = to_int(at(c, "cases_layer"));
        p.layers_unit_load = to_int(at(c, "layers_unit_load"));
        p.cases_unit_load  = to_int(at(c, "cases_unit_load"));
        p.pallet_id        = at(c, "pallet_id");
        if (p.cases_unit_load <= 0) ++result.rowsWithoutUnitLoad;

        // Rows sharing an ID are pallet-type variants (TLD / PTL / PGM / GMA),
        // not data errors: same product, different Cases_Unit_Load. They are
        // ALL kept so the Joiner can choose the right one. duplicate_ids counts
        // how many IDs appear more than once, purely for reporting.
        if (id_to_index.find(p.id) != id_to_index.end()) {
            ++result.duplicate_ids;
        } else {
            id_to_index[p.id] = result.products.size();
        }
        result.products.push_back(std::move(p));
    }

    LOG_INFO("Loaded product master: " + to_string(result.rows_read)
             + " rows, " + to_string(id_to_index.size()) + " unique IDs, "
             + to_string(result.duplicate_ids) + " pallet-type variants");

    // Product_Master_Available is 't' on these rows too, so the master cannot filter them
    // out itself. They load so the master stays whole; validation rejects any demand for one.
    // Info, not a warning: every 29 Sep - 5 Oct master carries ~900 of them, so a warning
    // would fire on every run. A demanded one is still an error from the Validator.
    if (result.rowsWithoutUnitLoad > 0) {
        LOG_INFO(to_string(result.rowsWithoutUnitLoad)
                 + " product rows have Cases_Unit_Load at or below zero and cannot form a"
                   " unit load; demand for any of them is rejected by validation");
    }
    if (result.misalignedRows > 0) {
        LOG_WARN(to_string(result.misalignedRows) + " product rows do not have the header's "
                 + to_string(headers.size()) + " cells; their columns may be misread");
    }

    return result;
}

} // namespace ob
