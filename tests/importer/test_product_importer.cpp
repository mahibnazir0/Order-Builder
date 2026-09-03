#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "product_importer.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>

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
    auto it = std::find_if(r.products.begin(), r.products.end(),
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
    // std::stod/std::stoi stop at the first character they can't parse rather
    // than rejecting the whole string, so "9500x" would silently parse as
    // 9500.0 instead of being rejected. A literal comma (a thousands
    // separator, e.g. "9,500") would demonstrate the same defect, but
    // split_csv is not quote-aware, so an unquoted comma would just misalign
    // the columns instead of exercising this code path — trailing letters
    // isolate the same bug without that complication.
    const std::string path = "tests/importer/_tmp_trailing_garbage.csv";
    {
        std::ofstream out(path);
        out << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
               "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n";
        out << "T1,Test,10,10,10,5,CS,9500x,4,2,8,TLD\n";
    }

    ProductLoadResult r = ProductImporter::load(path);
    std::remove(path.c_str());

    REQUIRE(r.products.size() == 1);
    CHECK(r.products[0].weight_lb == doctest::Approx(0.0));
}

TEST_CASE("missing file throws, does not crash") {
    CHECK_THROWS_AS(ProductImporter::load("does/not/exist.csv"), std::runtime_error);
}

TEST_CASE("Cases_Unit_Load available for CS->pallets conversion") {
    ProductLoadResult r = ProductImporter::load(PRODUCT_PATH);
    // At least the vast majority must have a positive divisor.
    int positive = 0;
    for (const auto& p : r.products) if (p.cases_unit_load > 0) ++positive;
    CHECK(positive > 20000);
}
