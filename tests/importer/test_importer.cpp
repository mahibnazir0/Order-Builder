// doctest's main() is implemented once for the whole ob_tests binary, in
// tests/importer/test_product_importer.cpp. Every other test TU just includes
// the header.
#include "doctest.h"
#include "importer.hpp"
#include <cstdio>
#include <fstream>
#include <stdexcept>

using namespace ob;

// Path to the real sample file, relative to where the test binary runs.
static const char* DEMAND_PATH = "tests/importer/Demand-1.json";

TEST_CASE("demand file loads with correct record counts") {
    DemandFile d = Importer::load_demand(DEMAND_PATH);

    // These numbers are the M1 acceptance check — verified against Tom's file.
    CHECK(d.str.size() == 24357);
    CHECK(d.ctl.size() == 8);
    CHECK(d.dnm.size() == 22);
    CHECK(d.request_id == "#STR_PA4400_20260817164454#");
}

TEST_CASE("first STR record parses field-for-field") {
    DemandFile d = Importer::load_demand(DEMAND_PATH);
    REQUIRE(d.str.size() > 0);
    const STRRecord& r = d.str[0];

    CHECK(r.idpr        == "1332890062");
    CHECK(r.bnfpo       == 10);
    CHECK(r.locfrno     == "2190");
    CHECK(r.loctono     == "2508");
    CHECK(r.matnr       == "105521103");
    CHECK(r.ship_cond   == "TF");
    CHECK(r.unitofmeas  == "CS");
    CHECK(r.trans       == doctest::Approx(1439.0));
    CHECK(r.planner_snp == "S23");   // the field that links to DNM
}

TEST_CASE("CTL and DNM blocks parse") {
    DemandFile d = Importer::load_demand(DEMAND_PATH);

    // Find the FRIDAY level-load entry
    bool found_friday = false;
    for (const auto& c : d.ctl) {
        if (c.zday == "FRIDAY") {
            found_friday = true;
            CHECK(c.level_load_start == 3);
            CHECK(c.level_load_end == 5);
        }
    }
    CHECK(found_friday);

    // DNM records have both fields populated
    REQUIRE(d.dnm.size() > 0);
    CHECK_FALSE(d.dnm[0].planner_snp.empty());
    CHECK_FALSE(d.dnm[0].locfrno.empty());
}

TEST_CASE("missing file throws, does not crash") {
    CHECK_THROWS_AS(Importer::load_demand("does/not/exist.json"), std::runtime_error);
}

TEST_CASE("STR present but the wrong JSON type throws, rather than loading zero lines") {
    // A block given as an object instead of an array must not be treated the
    // same as an absent block — that would silently produce zero demand
    // lines with a successful exit.
    const std::string path = "tests/importer/_tmp_str_wrong_type.json";
    {
        std::ofstream out(path);
        out << R"({"REQUEST_ID":"X","STR":{"MATNR":"1"}})";
    }
    CHECK_THROWS_AS(Importer::load_demand(path), std::runtime_error);
    std::remove(path.c_str());
}

TEST_CASE("CTL and DNM present but the wrong JSON type also throw") {
    const std::string ctlPath = "tests/importer/_tmp_ctl_wrong_type.json";
    {
        std::ofstream out(ctlPath);
        out << R"({"REQUEST_ID":"X","CTL":"not-an-array"})";
    }
    CHECK_THROWS_AS(Importer::load_demand(ctlPath), std::runtime_error);
    std::remove(ctlPath.c_str());

    const std::string dnmPath = "tests/importer/_tmp_dnm_wrong_type.json";
    {
        std::ofstream out(dnmPath);
        out << R"({"REQUEST_ID":"X","DNM":42})";
    }
    CHECK_THROWS_AS(Importer::load_demand(dnmPath), std::runtime_error);
    std::remove(dnmPath.c_str());
}

TEST_CASE("STR genuinely absent still loads cleanly with zero demand lines") {
    // An absent block is a different case from a malformed one: it is a
    // legitimate empty result, not an error.
    const std::string path = "tests/importer/_tmp_str_absent.json";
    {
        std::ofstream out(path);
        out << R"({"REQUEST_ID":"X"})";
    }
    DemandFile d = Importer::load_demand(path);
    CHECK(d.str.empty());
    CHECK(d.request_id == "X");
    std::remove(path.c_str());
}

TEST_CASE("UNITOFMEAS only ever CS, DIS or PAL in this file") {
    DemandFile d = Importer::load_demand(DEMAND_PATH);
    for (const auto& r : d.str) {
        bool ok = (r.unitofmeas == "CS" || r.unitofmeas == "DIS" || r.unitofmeas == "PAL");
        REQUIRE(ok);
    }
}
