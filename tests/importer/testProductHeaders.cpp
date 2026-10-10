#include "doctest.h"
#include "crossDayFixtures.hpp"
#include "placeholder_importer.hpp"
#include "product_importer.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

using namespace std;

using namespace ob;
using namespace crossDayTests;

namespace {

const string kDuplicateWarning = "[WARN] Duplicate product column";

ProductLoadResult loadWithLog(const string& path, string& logText) {
    ostringstream captured;
    auto* previousBuffer = cout.rdbuf(captured.rdbuf());
    try {
        auto result = ProductImporter::load(path);
        cout.rdbuf(previousBuffer);
        logText = captured.str();
        return result;
    } catch (...) {
        cout.rdbuf(previousBuffer);
        throw;
    }
}

size_t occurrences(const string& text, const string& needle) {
    size_t count = 0;
    for (auto position = text.find(needle); position != string::npos;
         position = text.find(needle, position + 1)) {
        ++count;
    }
    return count;
}

// filesystem::exists is case-insensitive on Windows, so compare the listed names.
bool directoryListsExactly(const filesystem::path& directory, const string& name) {
    for (const auto& entry : filesystem::directory_iterator(directory)) {
        if (entry.path().filename().string() == name) return true;
    }
    return false;
}

struct SyntheticProductFile {
    string path = "tests/importer/duplicateHeaderSynthetic.csv";
    SyntheticProductFile() = default;
    SyntheticProductFile(const SyntheticProductFile&) = delete;
    SyntheticProductFile& operator=(const SyntheticProductFile&) = delete;
    ~SyntheticProductFile() { remove(path.c_str()); }
};

} // namespace

TEST_CASE("product headers: only the 03 Sep master warns of a duplicate column" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        const DayFiles& day = dayFiles()[dayIndex];
        CAPTURE(day.label);
        string logText;
        loadWithLog(day.productPath, logText);
        const bool isThirdSeptember = dayIndex == 3;
        CHECK(occurrences(logText, kDuplicateWarning) == (isThirdSeptember ? 1u : 0u));
        if (isThirdSeptember) {
            CHECK(logText.find("'strength' at column 9; using first occurrence at column 6")
                  != string::npos);
        }
    }
}

TEST_CASE("product headers: every master yields its row and unique ID counts, duplicate or not" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        const DayFiles& day = dayFiles()[dayIndex];
        CAPTURE(day.label);
        string logText;
        const auto loaded = loadWithLog(day.productPath, logText);
        set<string> uniqueIds;
        for (const auto& product : loaded.products) uniqueIds.insert(product.id);
        CHECK(loaded.rows_read == expectedM1::productRows[dayIndex]);
        CHECK(loaded.products.size() == static_cast<size_t>(expectedM1::productRows[dayIndex]));
        CHECK(uniqueIds.size() == expectedM1::uniqueProductIds[dayIndex]);
        cout << "Cross-day " << day.label << " productRows=" << loaded.rows_read
                  << " uniqueIds=" << uniqueIds.size()
                  << " duplicateHeaderWarnings=" << occurrences(logText, kDuplicateWarning) << '\n';
    }
}

TEST_CASE("product headers: synthetic duplicate columns keep the first occurrence") {
    string duplicateHeader;
    SUBCASE("identical spelling") { duplicateHeader = "Strength"; }
    SUBCASE("case-insensitive collision") { duplicateHeader = "strength"; }
    SUBCASE("trimmed case-insensitive collision") { duplicateHeader = " STRENGTH "; }
    const SyntheticProductFile fixture;
    {
        ofstream output(fixture.path);
        REQUIRE(output.is_open());
        output << "ID,Length,Width,Height,Strength,UoM,Weight," << duplicateHeader
               << ",Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
               << "synthetic,48,40,50,3,PAL,100,9,4,15,60,TLD\n";
    }
    string logText;
    const auto loaded = loadWithLog(fixture.path, logText);
    REQUIRE(loaded.products.size() == 1);
    CHECK(loaded.products.front().strength == 3);
    CHECK(occurrences(logText, kDuplicateWarning) == 1);
    CHECK(logText.find("using first occurrence at column 5") != string::npos);
}

TEST_CASE("product rows without a unit load are reported as info, not as a warning") {
    const SyntheticProductFile fixture;
    {
        ofstream output(fixture.path);
        REQUIRE(output.is_open());
        output << "ID,Length,Width,Height,Strength,UoM,Weight,Cases_Layer,Layers_Unit_Load,"
                  "Cases_Unit_Load,Pallet_ID\n"
               << "SHELL,,,,,,,,,,\n"
               << "GOOD,48,40,50,3,PAL,100,9,4,15,TLD\n";
    }
    string logText;
    const auto loaded = loadWithLog(fixture.path, logText);
    CHECK(loaded.rowsWithoutUnitLoad == 1);
    CHECK(occurrences(logText, "[INFO] 1 product rows have Cases_Unit_Load at or below zero") == 1);
    CHECK(occurrences(logText, "[WARN]") == 0);
}

// The client's placeholder filename changes between extracts ("PlaceHolder-1.json",
// "Placeholder-N.json", UUID-named from 29 Sep), so no naming pattern is asserted. What
// matters is that a case-sensitive filesystem opens the exact name shipped.
TEST_CASE("placeholders: each day loads by the exact filename the client shipped" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        const DayFiles& day = dayFiles()[dayIndex];
        CAPTURE(day.label);
        const bool isAugust = dayIndex == 0;
        if (!isAugust) {
            const filesystem::path directory(day.placeholderDirectory);
            CHECK(directoryListsExactly(directory.parent_path(), "PlaceHolder"));
        }
        REQUIRE(directoryListsExactly(day.placeholderDirectory, day.placeholderFileName));
        const auto loaded = PlaceholderImporter::load(day.placeholderPath());
        CHECK(loaded.placeholders.size() == expectedM1::placeholderEntries[dayIndex]);
        CHECK(loaded.total_loads == expectedM1::trucksRequested[dayIndex]);
        cout << "Cross-day " << day.label << " placeholders=" << loaded.placeholders.size()
                  << " trucks=" << loaded.total_loads << " path=" << day.placeholderPath() << '\n';
    }
}
