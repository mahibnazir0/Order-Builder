// NOTE: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — main lives in one
// implementing translation unit only (see tests/importer/test_importer.cpp).
#include "doctest.h"
#include "placeholder_importer.hpp"
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
