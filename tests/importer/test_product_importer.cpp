#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "product_importer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>

using namespace std;

using namespace ob;

static const char* PRODUCT_PATH = "tests/importer/Customer2-Product-Data.csv";

TEST_CASE("product master loads with correct row and ID counts") {
    ProductLoadResult r = ProductImporter::load(PRODUCT_PATH);

    // Verified against the real file.
    CHECK(r.rows_read == 20201);
    // ALL rows are kept, including pallet-type variants — the Joiner chooses
    // between them. 18 IDs appear twice (TLD/PTL/PGM/GMA variants of the same
    // product, with different Cases_Unit_Load).
    CHECK(r.products.size() == 20201);
    CHECK(r.duplicate_ids == 18);
}

TEST_CASE("first record parses field-for-field") {
    ProductLoadResult r = ProductImporter::load(PRODUCT_PATH);
    REQUIRE(!r.products.empty());

    // Find a known product by ID rather than assuming order after de-dup.
    auto it = find_if(r.products.begin(), r.products.end(),
                           [](const ProductRecord& p){ return p.id == "100802205"; });
    REQUIRE(it != r.products.end());
    CHECK(it->length_in == doctest::Approx(48.0));
    CHECK(it->width_in  == doctest::Approx(40.0));
    CHECK(it->uom       == "PAL");
    CHECK(it->pallet_id == "PTL");
}

TEST_CASE("ID is kept as a string, not parsed to int") {
    ProductLoadResult r = ProductImporter::load(PRODUCT_PATH);
    // Every ID should be a non-empty string; join relies on string equality.
    for (const auto& p : r.products) {
        REQUIRE_FALSE(p.id.empty());
    }
}

TEST_CASE("pallet-type variants are preserved for the Joiner to choose") {
    ProductLoadResult r = ProductImporter::load(PRODUCT_PATH);
    // ID 105553001 exists as GMA (84 cases/unit load) and TLD (168).
    int found = 0;
    for (const auto& p : r.products) if (p.id == "105553001") ++found;
    CHECK(found == 2);
}

TEST_CASE("blank UoM rows are loaded, not dropped (UoM comes from demand anyway)") {
    ProductLoadResult r = ProductImporter::load(PRODUCT_PATH);
    int blank_uom = 0;
    for (const auto& p : r.products) if (p.uom.empty()) ++blank_uom;
    // 7,287 blanks in the current file; some may collapse under de-dup, so
    // assert the reader preserved a large blank count rather than silently
    // cleaning it — the Validator/Tom handle the blanks, not the reader.
    CHECK(blank_uom > 7000);
}

TEST_CASE("the one bad record (zero dims + zero cases_unit_load) is present, not skipped") {
    // The reader loads everything; skipping is the Validator's job per Tom's ruling.
    ProductLoadResult r = ProductImporter::load(PRODUCT_PATH);
    int zero_dim = 0;
    for (const auto& p : r.products) {
        if (p.length_in == 0.0 && p.width_in == 0.0 && p.height_in == 0.0) ++zero_dim;
    }
    CHECK(zero_dim >= 1);   // reader keeps it; Validator will skip+warn later
}

TEST_CASE("a numeric cell with trailing garbage falls back instead of partially parsing") {
    // stod/stoi stop at the first character they can't parse rather
    // than rejecting the whole string, so "9500x" would silently parse as
    // 9500.0 instead of being rejected. A literal comma (a thousands
    // separator, e.g. "9,500") would demonstrate the same defect, but
    // split_csv is not quote-aware, so an unquoted comma would just misalign
    // the columns instead of exercising this code path — trailing letters
    // isolate the same bug without that complication.
    const string path = "tests/importer/_tmp_trailing_garbage.csv";
    {
        ofstream out(path);
        out << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
               "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n";
        out << "T1,Test,10,10,10,5,CS,9500x,4,2,8,TLD\n";
    }

    ProductLoadResult r = ProductImporter::load(path);
    remove(path.c_str());

    REQUIRE(r.products.size() == 1);
    CHECK(isnan(r.products[0].weight_lb));
}

TEST_CASE("a blank Weight cell loads as NaN, not 0") {
    const string path = "tests/importer/_tmp_blank_weight.csv";
    {
        ofstream out(path);
        out << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
               "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n";
        out << "T1,Test,10,10,10,5,CS,,4,2,8,TLD\n";
    }

    ProductLoadResult r = ProductImporter::load(path);
    remove(path.c_str());

    REQUIRE(r.products.size() == 1);
    CHECK(isnan(r.products[0].weight_lb));
}

TEST_CASE("a Height cell with trailing garbage loads as NaN, not 0") {
    const string path = "tests/importer/_tmp_garbage_height.csv";
    {
        ofstream out(path);
        out << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
               "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n";
        out << "T1,Test,10,10,10x,5,CS,9500,4,2,8,TLD\n";
    }

    ProductLoadResult r = ProductImporter::load(path);
    remove(path.c_str());

    REQUIRE(r.products.size() == 1);
    CHECK(isnan(r.products[0].height_in));
}

TEST_CASE("a nan Height cell loads as NaN, leaving the Validator to reject it") {
    const string path = "tests/importer/_tmp_nan_height.csv";
    {
        ofstream out(path);
        out << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
               "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n";
        out << "T1,Test,10,10,nan,5,CS,9500,4,2,8,TLD\n";
    }

    ProductLoadResult r = ProductImporter::load(path);
    remove(path.c_str());

    REQUIRE(r.products.size() == 1);
    CHECK(isnan(r.products[0].height_in));
}

TEST_CASE("a non-numeric Strength loads as the unreadable sentinel, not blank CRI 0") {
    const string path = "tests/importer/_tmp_strength_text.csv";
    {
        ofstream out(path);
        out << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
               "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n";
        out << "T1,Test,10,10,10,7a,CS,9500,4,2,8,TLD\n";
    }

    ProductLoadResult r = ProductImporter::load(path);
    remove(path.c_str());

    REQUIRE(r.products.size() == 1);
    CHECK(r.products[0].strength == kUnreadableStrength);
}

TEST_CASE("a non-numeric Layers_Unit_Load loads as 0, which invalid_layer_data rejects") {
    const string path = "tests/importer/_tmp_layers_text.csv";
    {
        ofstream out(path);
        out << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
               "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n";
        out << "T1,Test,10,10,10,5,CS,9500,4,two,8,TLD\n";
    }

    ProductLoadResult r = ProductImporter::load(path);
    remove(path.c_str());

    REQUIRE(r.products.size() == 1);
    CHECK(r.products[0].layers_unit_load == 0);
}

TEST_CASE("a fractional Strength loads as the unreadable sentinel") {
    const string path = "tests/importer/_tmp_strength_fraction.csv";
    {
        ofstream out(path);
        out << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
               "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n";
        out << "T1,Test,10,10,10,7.5,CS,9500,4,2,8,TLD\n";
    }

    ProductLoadResult r = ProductImporter::load(path);
    remove(path.c_str());

    REQUIRE(r.products.size() == 1);
    CHECK(r.products[0].strength == kUnreadableStrength);
}

TEST_CASE("a blank Strength still loads as 0") {
    const string path = "tests/importer/_tmp_strength_blank.csv";
    {
        ofstream out(path);
        out << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
               "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n";
        out << "T1,Test,10,10,10,,CS,9500,4,2,8,TLD\n";
    }

    ProductLoadResult r = ProductImporter::load(path);
    remove(path.c_str());

    REQUIRE(r.products.size() == 1);
    CHECK(r.products[0].strength == 0);
}

TEST_CASE("missing file throws, does not crash") {
    CHECK_THROWS_AS(ProductImporter::load("does/not/exist.csv"), runtime_error);
}

TEST_CASE("Cases_Unit_Load available for CS->pallets conversion") {
    ProductLoadResult r = ProductImporter::load(PRODUCT_PATH);
    // At least the vast majority must have a positive divisor.
    int positive = 0;
    for (const auto& p : r.products) if (p.cases_unit_load > 0) ++positive;
    CHECK(positive > 20000);
}

namespace {

// Writes `text` to a scratch CSV, loads it with the real importer, then removes it.
ProductLoadResult loadCsvText(const string& text) {
    const string path = "tests/importer/_tmp_product_text.csv";
    {
        ofstream out(path);
        out << text;
    }
    try {
        ProductLoadResult loaded = ProductImporter::load(path);
        remove(path.c_str());
        return loaded;
    } catch (...) {
        remove(path.c_str());
        throw;
    }
}

const string kEightyEightColumnStyleHeader =
    "ID,Product_Master_Available,Length,Width,Height,Strength,UoM,Weight,"
    "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID,Notes\n";

} // namespace

TEST_CASE("a master without Cases_Layer or Layers_Unit_Load is rejected, not read as all zeros") {
    string missingColumn;
    SUBCASE("Cases_Layer") { missingColumn = "Cases_Layer,"; }
    SUBCASE("Layers_Unit_Load") { missingColumn = "Layers_Unit_Load,"; }
    string header = kEightyEightColumnStyleHeader;
    header.erase(header.find(missingColumn), missingColumn.size());
    CHECK_THROWS_AS(loadCsvText(header + "T1,t,10,10,10,5,CS,9,4,8,TLD,\n"), runtime_error);
}

TEST_CASE("a master with no Description column and extra columns loads every field by name") {
    const auto loaded = loadCsvText(kEightyEightColumnStyleHeader
                                    + "T1,t,36,6.29,7.2,1,CS,21.7,4,3,12,TLD,note\n");
    REQUIRE(loaded.products.size() == 1);
    const ProductRecord& record = loaded.products[0];
    CHECK(record.description.empty());
    CHECK(record.length_in == doctest::Approx(36.0));
    CHECK(record.height_in == doctest::Approx(7.2));
    CHECK(record.strength == 1);
    CHECK(record.weight_lb == doctest::Approx(21.7));
    CHECK(record.cases_layer == 4);
    CHECK(record.layers_unit_load == 3);
    CHECK(record.cases_unit_load == 12);
    CHECK(record.pallet_id == "TLD");
    CHECK(loaded.misalignedRows == 0);
}

TEST_CASE("a quoted comma inside a cell does not shift the columns after it") {
    const auto loaded = loadCsvText(
        "ID,Notes,Length,Width,Height,Strength,UoM,Weight,"
        "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
        "T1,\"box, large\",10,20,30,5,CS,9,4,2,8,PTL\n");
    REQUIRE(loaded.products.size() == 1);
    CHECK(loaded.products[0].length_in == doctest::Approx(10.0));
    CHECK(loaded.products[0].cases_unit_load == 8);
    CHECK(loaded.products[0].pallet_id == "PTL");
    CHECK(loaded.misalignedRows == 0);
}

TEST_CASE("a doubled quote inside a quoted cell reads as one quote") {
    const auto loaded = loadCsvText(
        "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
        "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
        "T1,\"6\"\" pipe\",10,20,30,5,CS,9,4,2,8,PTL\n");
    REQUIRE(loaded.products.size() == 1);
    CHECK(loaded.products[0].description == "6\" pipe");
    CHECK(loaded.products[0].pallet_id == "PTL");
}

TEST_CASE("a row with fewer cells than the header is counted as misaligned") {
    const auto loaded = loadCsvText(kEightyEightColumnStyleHeader
                                    + "T1,t,10,10,10,5,CS,9,4,2,8,TLD,note\n"
                                    + "T2,t,10,10,10,5,CS,9,4,2\n");
    CHECK(loaded.products.size() == 2);
    CHECK(loaded.misalignedRows == 1);
}

TEST_CASE("shell rows and zero-count rows load but are counted as having no unit load") {
    const auto loaded = loadCsvText(kEightyEightColumnStyleHeader
                                    + "SHELL,t,,,,,,,,,,,\n"
                                    + "ZEROCOUNTS,t,36,6.29,7.2,1,CS,21.7,0,0,0,TLD,\n"
                                    + "NEGATIVE,t,10,10,10,5,CS,9,4,2,-3,TLD,\n"
                                    + "GOOD,t,10,10,10,5,CS,9,4,2,8,TLD,\n");
    CHECK(loaded.products.size() == 4);
    CHECK(loaded.rowsWithoutUnitLoad == 3);
}

TEST_CASE("the 17 Aug master has exactly one row without a unit load and no misaligned rows") {
    const ProductLoadResult loaded = ProductImporter::load(PRODUCT_PATH);
    CHECK(loaded.rowsWithoutUnitLoad == 1);
    CHECK(loaded.misalignedRows == 0);
}

TEST_CASE("a quoted cell containing a newline stays one record") {
    const auto loaded = loadCsvText(
        "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
        "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
        "T1,\"two\nlines\",10,20,30,5,CS,9,4,2,8,PTL\n"
        "T2,plain,10,20,30,5,CS,9,4,2,8,TLD\n");
    REQUIRE(loaded.products.size() == 2);
    CHECK(loaded.rows_read == 2);
    CHECK(loaded.misalignedRows == 0);
    CHECK(loaded.rowsWithoutUnitLoad == 0);
    CHECK(loaded.products[0].description == "two\nlines");
    CHECK(loaded.products[0].pallet_id == "PTL");
    CHECK(loaded.products[1].id == "T2");
}

TEST_CASE("a quote left open at end of file is rejected, not read as one long record") {
    CHECK_THROWS_AS(loadCsvText(
        "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
        "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
        "T1,\"never closed,10,20,30,5,CS,9,4,2,8,PTL\n"
        "T2,plain,10,20,30,5,CS,9,4,2,8,TLD\n"), runtime_error);
}
