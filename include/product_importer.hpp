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
    // Rows with Cases_Unit_Load at or below zero, including the blank "shell" rows the
    // 29 Sep - 5 Oct masters carry (~900 each). Loaded, but no unit load can be built.
    int rowsWithoutUnitLoad = 0;
    // Rows whose cell count differs from the header's, so their columns may be misread.
    int misalignedRows = 0;
};

class ProductImporter {
public:
    // Read Customer2-Product-Data.csv.
    // Throws std::runtime_error if the file cannot be opened or the header
    // does not contain the expected columns. Columns are found by name, so
    // extra columns (the 88-column master) and column order do not matter.
    //
    // Design choices, matching the demand reader:
    //   - Missing/malformed numeric cells default to 0 (Validator judges them).
    //   - ID and Description are kept as strings; Description is optional.
    //   - Rows sharing an ID are pallet-type variants (TLD/PTL/PGM/GMA) with
    //     different Cases_Unit_Load, NOT data errors. All rows are kept; the
    //     Joiner groups them and chooses. Nothing is dropped here.
    static ProductLoadResult load(const std::string& csv_path);

    // Read Customer2-Pallet-Data.csv: one row per pallet type, giving its footprint,
    // height and weight by name (ID, Footprint_Length, Footprint_Width, Height, Weight).
    // Throws std::runtime_error if the file cannot be read, a required column is missing,
    // or a pallet ID is blank or listed twice. A blank or unreadable figure is kept as NaN.
    static std::vector<PalletSpec> loadPalletTable(const std::string& csvPath);
};

} // namespace ob
