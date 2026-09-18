#include "doctest.h"
#include "product_importer.hpp"
#include "crossDayFixtures.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

using namespace ob;

namespace {

ProductLoadResult loadWithLog(const std::string& path, std::string& logText) {
    std::ostringstream captured;
    auto* previousBuffer = std::cout.rdbuf(captured.rdbuf());
    try {
        auto result = ProductImporter::load(path);
        std::cout.rdbuf(previousBuffer);
        logText = captured.str();
        return result;
    } catch (...) {
        std::cout.rdbuf(previousBuffer);
        throw;
    }
}

struct SyntheticProductFile {
    std::string path = "tests/importer/duplicateHeaderSynthetic.csv";
    SyntheticProductFile() = default;
    SyntheticProductFile(const SyntheticProductFile&) = delete;
    SyntheticProductFile& operator=(const SyntheticProductFile&) = delete;
    ~SyntheticProductFile() { std::remove(path.c_str()); }
};

} // namespace

TEST_CASE("product headers: cross-day rows IDs and duplicate warnings") {
    for (std::size_t dayIndex = 0; dayIndex < crossDayTests::days().size(); ++dayIndex) {
        const auto& day = crossDayTests::days()[dayIndex];
        CAPTURE(day.label);
        std::string logText;
        const auto loaded = loadWithLog(day.productPath, logText);
        std::set<std::string> uniqueIds;
        for (const auto& product : loaded.products) uniqueIds.insert(product.id);
        CHECK(loaded.rows_read == day.productRows);
        CHECK(loaded.products.size() == static_cast<std::size_t>(day.productRows));
        CHECK(uniqueIds.size() == day.productIds);
        const auto warningPosition = logText.find("[WARN] Duplicate product column");
        if (dayIndex == 3) {
            REQUIRE(warningPosition != std::string::npos);
            CHECK(logText.find("'strength' at column 9; using first occurrence at column 6")
                  != std::string::npos);
            CHECK(logText.find("[WARN] Duplicate product column", warningPosition + 1) == std::string::npos);
        } else {
            CHECK(warningPosition == std::string::npos);
        }
        std::cout << "Cross-day " << day.label << " productRows=" << loaded.rows_read
                  << " uniqueIds=" << uniqueIds.size() << " duplicateHeaderWarnings="
                  << (warningPosition == std::string::npos ? 0 : 1) << '\n';
    }
}

TEST_CASE("product headers: synthetic differing duplicates keep first occurrence") {
    std::string duplicateHeader;
    SUBCASE("identical spelling") { duplicateHeader = "Strength"; }
    SUBCASE("case-insensitive collision") { duplicateHeader = "strength"; }
    SUBCASE("trimmed case-insensitive collision") { duplicateHeader = " STRENGTH "; }
    const SyntheticProductFile fixture;
    {
        std::ofstream output(fixture.path);
        REQUIRE(output.is_open());
        output << "ID,Length,Width,Height,Strength,UoM,Weight," << duplicateHeader
               << ",Cases_Unit_Load,Pallet_ID\n"
               << "synthetic,48,40,50,3,PAL,100,9,60,TLD\n";
    }
    std::string logText;
    const auto loaded = loadWithLog(fixture.path, logText);
    REQUIRE(loaded.products.size() == 1);
    CHECK(loaded.products.front().strength == 3);
    CHECK(logText.find("[WARN] Duplicate product column '") != std::string::npos);
    CHECK(logText.find("using first occurrence at column 5") != std::string::npos);
}
