// NOTE: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — main lives in one
// implementing translation unit only (see tests/importer/test_importer.cpp).
#include "doctest.h"
#include "placeholder_importer.hpp"
#include "validator.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>

using namespace ob;

static const char* PH_PATH = "tests/importer/PlaceHolder-1.json";

TEST_CASE("placeholder file loads with correct counts") {
    PlaceholderLoadResult r = PlaceholderImporter::load(PH_PATH);

    // Verified against the real file.
    CHECK(r.placeholders.size() == 189);
    CHECK(r.total_loads == 372);
}

TEST_CASE("first placeholder record parses field-for-field") {
    PlaceholderLoadResult r = PlaceholderImporter::load(PH_PATH);
    REQUIRE(!r.placeholders.empty());
    const PlaceholderRecord& p = r.placeholders[0];

    CHECK(p.locfrno         == "2023");
    CHECK(p.loctono         == "2528");
    CHECK(p.ship_cond       == "TL");
    CHECK(p.datfr_ta        == "2026-08-20");
    CHECK(p.datto_ta        == "2026-08-22");
    CHECK(p.zzna_equip_size == "");      // blank = any trailer
    CHECK(p.no_of_loads     == 1);
    CHECK(p.ebeln           == "");      // blank in every record of this file
}

TEST_CASE("ship condition is only TL or TF") {
    PlaceholderLoadResult r = PlaceholderImporter::load(PH_PATH);
    int tl = 0, tf = 0;
    for (const auto& p : r.placeholders) {
        REQUIRE((p.ship_cond == "TL" || p.ship_cond == "TF"));
        if (p.ship_cond == "TL") ++tl; else ++tf;
    }
    CHECK(tl == 105);
    CHECK(tf == 84);
}

TEST_CASE("equipment size is 53F or blank") {
    PlaceholderLoadResult r = PlaceholderImporter::load(PH_PATH);
    int blank = 0, f53 = 0;
    for (const auto& p : r.placeholders) {
        REQUIRE((p.zzna_equip_size == "53F" || p.zzna_equip_size.empty()));
        if (p.zzna_equip_size.empty()) ++blank; else ++f53;
    }
    CHECK(blank == 43);
    CHECK(f53 == 146);
}

TEST_CASE("every entry requests at least one truck") {
    PlaceholderLoadResult r = PlaceholderImporter::load(PH_PATH);
    for (const auto& p : r.placeholders) {
        REQUIRE(p.no_of_loads >= 1);
    }
}

TEST_CASE("lane triplets are unique — no de-dup needed") {
    PlaceholderLoadResult r = PlaceholderImporter::load(PH_PATH);
    std::vector<std::string> keys;
    keys.reserve(r.placeholders.size());
    for (const auto& p : r.placeholders) {
        keys.push_back(p.locfrno + "|" + p.loctono + "|" + p.ship_cond);
    }
    std::sort(keys.begin(), keys.end());
    CHECK(std::adjacent_find(keys.begin(), keys.end()) == keys.end());
}

TEST_CASE("missing file throws, does not crash") {
    CHECK_THROWS_AS(PlaceholderImporter::load("does/not/exist.json"), std::runtime_error);
}

TEST_CASE("PHOLDER present but the wrong JSON type throws") {
    const std::string path = "tests/importer/_tmp_pholder_wrong_type.json";
    {
        std::ofstream out(path);
        out << R"({"PHOLDER":{"LOCFRNO":"2023"}})";
    }
    CHECK_THROWS_AS(PlaceholderImporter::load(path), std::runtime_error);
    std::remove(path.c_str());
}

TEST_CASE("PHOLDER genuinely absent still loads cleanly with zero entries") {
    const std::string path = "tests/importer/_tmp_pholder_absent.json";
    {
        std::ofstream out(path);
        out << R"({})";
    }
    PlaceholderLoadResult r = PlaceholderImporter::load(path);
    CHECK(r.placeholders.empty());
    CHECK(r.total_loads == 0);
    std::remove(path.c_str());
}

namespace {

// Loads a one-entry placeholder file whose entry is the given JSON object text.
PlaceholderLoadResult loadOnePlaceholder(const std::string& entryJson) {
    const std::string path = "tests/importer/_tmp_pholder_entry.json";
    {
        std::ofstream out(path);
        out << R"({"PHOLDER":[)" << entryJson << "]}";
    }
    PlaceholderLoadResult result = PlaceholderImporter::load(path);
    std::remove(path.c_str());
    return result;
}

} // namespace

TEST_CASE("a NO_OF_LOADS of the wrong type loads as the unreadable sentinel, not 0") {
    const auto r = loadOnePlaceholder(R"({"LOCFRNO":"1","LOCTONO":"2","NO_OF_LOADS":"three"})");
    REQUIRE(r.placeholders.size() == 1);
    CHECK(r.placeholders[0].no_of_loads == -1);
}

TEST_CASE("an absent NO_OF_LOADS loads as the unreadable sentinel, not 0") {
    const auto r = loadOnePlaceholder(R"({"LOCFRNO":"1","LOCTONO":"2"})");
    REQUIRE(r.placeholders.size() == 1);
    CHECK(r.placeholders[0].no_of_loads == -1);
}

TEST_CASE("a null NO_OF_LOADS loads as the unreadable sentinel, not 0") {
    const auto r = loadOnePlaceholder(R"({"LOCFRNO":"1","LOCTONO":"2","NO_OF_LOADS":null})");
    REQUIRE(r.placeholders.size() == 1);
    CHECK(r.placeholders[0].no_of_loads == -1);
}

TEST_CASE("an explicit NO_OF_LOADS of 0 stays 0") {
    const auto r = loadOnePlaceholder(R"({"LOCFRNO":"1","LOCTONO":"2","NO_OF_LOADS":0})");
    REQUIRE(r.placeholders.size() == 1);
    CHECK(r.placeholders[0].no_of_loads == 0);
}

TEST_CASE("the unreadable sentinel is left out of total_loads") {
    const auto r = loadOnePlaceholder(R"({"LOCFRNO":"1","LOCTONO":"2","NO_OF_LOADS":"three"})");
    CHECK(r.total_loads == 0);
}

TEST_CASE("a NO_OF_LOADS above the maximum is left out of total_loads") {
    const auto r = loadOnePlaceholder(R"({"LOCFRNO":"1","LOCTONO":"2","NO_OF_LOADS":)"
                                      + std::to_string(kMaxLoadsPerPlaceholder + 1) + "}");
    REQUIRE(r.placeholders.size() == 1);
    CHECK(r.total_loads == 0);
}

TEST_CASE("a NO_OF_LOADS of exactly the maximum is still counted in total_loads") {
    const auto r = loadOnePlaceholder(R"({"LOCFRNO":"1","LOCTONO":"2","NO_OF_LOADS":)"
                                      + std::to_string(kMaxLoadsPerPlaceholder) + "}");
    CHECK(r.total_loads == kMaxLoadsPerPlaceholder);
}

TEST_CASE("a fractional NO_OF_LOADS loads as the unreadable sentinel, not a truncated count") {
    const auto r = loadOnePlaceholder(R"({"LOCFRNO":"1","LOCTONO":"2","NO_OF_LOADS":2.9})");
    REQUIRE(r.placeholders.size() == 1);
    CHECK(r.placeholders[0].no_of_loads == -1);
    CHECK(r.total_loads == 0);
}

TEST_CASE("a boolean NO_OF_LOADS loads as the unreadable sentinel, not a truncated count") {
    const auto r = loadOnePlaceholder(R"({"LOCFRNO":"1","LOCTONO":"2","NO_OF_LOADS":true})");
    REQUIRE(r.placeholders.size() == 1);
    CHECK(r.placeholders[0].no_of_loads == -1);
    CHECK(r.total_loads == 0);
}

TEST_CASE("a NO_OF_LOADS above int range loads as the unreadable sentinel, not a truncated count") {
    const auto r = loadOnePlaceholder(R"({"LOCFRNO":"1","LOCTONO":"2","NO_OF_LOADS":4294967297})");
    REQUIRE(r.placeholders.size() == 1);
    CHECK(r.placeholders[0].no_of_loads == -1);
    CHECK(r.total_loads == 0);
}

TEST_CASE("a NO_OF_LOADS float above int range loads as the unreadable sentinel, not a truncated count") {
    const auto r = loadOnePlaceholder(R"({"LOCFRNO":"1","LOCTONO":"2","NO_OF_LOADS":1e10})");
    REQUIRE(r.placeholders.size() == 1);
    CHECK(r.placeholders[0].no_of_loads == -1);
    CHECK(r.total_loads == 0);
}

TEST_CASE("a whole-number float NO_OF_LOADS loads as that count") {
    const auto r = loadOnePlaceholder(R"({"LOCFRNO":"1","LOCTONO":"2","NO_OF_LOADS":3.0})");
    REQUIRE(r.placeholders.size() == 1);
    CHECK(r.placeholders[0].no_of_loads == 3);
}

TEST_CASE("a NO_OF_LOADS too large for a double is rejected at load as a runtime_error") {
    const std::string path = "tests/importer/_tmp_pholder_overflow.json";
    {
        std::ofstream out(path);
        out << R"({"PHOLDER":[{"LOCFRNO":"1","LOCTONO":"2","NO_OF_LOADS":1e999}]})";
    }
    CHECK_THROWS_WITH_AS(PlaceholderImporter::load(path), doctest::Contains("number overflow"),
                         std::runtime_error);
    std::remove(path.c_str());
}

TEST_CASE("a placeholder file whose root is an array throws instead of loading as empty") {
    const std::string path = "tests/importer/_tmp_ph_root_array.json";
    {
        std::ofstream out(path);
        out << R"([{"PHOLDER":[]}])";
    }
    CHECK_THROWS_WITH_AS(PlaceholderImporter::load(path),
                         doctest::Contains("root must be a JSON object"), std::runtime_error);
    std::remove(path.c_str());
}

TEST_CASE("a placeholder file whose root is a number throws instead of loading as empty") {
    const std::string path = "tests/importer/_tmp_ph_root_number.json";
    {
        std::ofstream out(path);
        out << "42";
    }
    CHECK_THROWS_WITH_AS(PlaceholderImporter::load(path),
                         doctest::Contains("root must be a JSON object"), std::runtime_error);
    std::remove(path.c_str());
}
