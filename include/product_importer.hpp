#pragma once
// product_importer.hpp — declares the product master CSV reader
#include "product_types.hpp"
#include <string>
#include <vector>

namespace ob {

// Result of reading the product master. Carries the rows plus a note of any
// duplicate IDs found, so the caller/Validator can decide what to do about them.
struct ProductLoadResult {
    std::vector<ProductRecord> products;
    int duplicate_ids = 0;      // count of IDs seen more than once (last one wins)
    int rows_read = 0;          // total data rows parsed
};

class ProductImporter {
public:
    // Read Customer2-Product-Data.csv.
    // Throws std::runtime_error if the file cannot be opened or the header
    // does not contain the expected columns.
    //
    // Design choices, matching the demand reader:
    //   - Missing/malformed numeric cells default to 0 (Validator judges them).
    //   - ID and Description are kept as strings.
    //   - On duplicate ID, the LAST occurrence wins and duplicate_ids is
    //     incremented, so nothing is silently dropped without a count.
    static ProductLoadResult load(const std::string& csv_path);
};

} // namespace ob
