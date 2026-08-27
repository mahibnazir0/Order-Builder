#pragma once
// product_importer.hpp — declares the product master CSV reader
#include "product_types.hpp"
#include <string>
#include <vector>

namespace ob {

// Result of reading the product master. Carries the rows plus a note of any
// duplicate IDs found, so the caller/Validator can decide what to do about them.
struct ProductLoadResult {
    std::vector<ProductRecord> products;   // ALL rows, including pallet-type variants
    int duplicate_ids = 0;      // how many IDs appear more than once (18 in the sample)
    int rows_read = 0;          // total data rows parsed (20,201 in the sample)
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
    //   - Rows sharing an ID are pallet-type variants (TLD/PTL/PGM/GMA) with
    //     different Cases_Unit_Load, NOT data errors. All rows are kept; the
    //     Joiner groups them and chooses. Nothing is dropped here.
    static ProductLoadResult load(const std::string& csv_path);
};

} // namespace ob
