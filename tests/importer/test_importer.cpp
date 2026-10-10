// doctest's main() is implemented once for the whole ob_tests binary, in
// tests/importer/test_product_importer.cpp. Every other test TU just includes
// the header.
#include "doctest.h"
#include "crossDayFixtures.hpp"
#include "importer.hpp"
#include <cstdio>
#include <fstream>
#include <stdexcept>

using namespace ob;

// Path to the real sample file, relative to where the test binary runs.
static const char* DEMAND_PATH = "tests/importer/Demand-1.json";

TEST_CASE("demand file loads with correct record counts" * doctest::skip(!crossDayTests::august17Present())) {
    DemandFile d = Importer::load_demand(DEMAND_PATH);

    // These numbers are the M1 acceptance check — verified against Tom's file.
    CHECK(d.str.size() == 24357);
    CHECK(d.ctl.size() == 8);
    CHECK(d.dnm.size() == 22);
    CHECK(d.request_id == "#STR_PA4400_20260817164454#");
}

TEST_CASE("first STR record parses field-for-field" * doctest::skip(!crossDayTests::august17Present())) {
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

TEST_CASE("CTL and DNM blocks parse" * doctest::skip(!crossDayTests::august17Present())) {
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

TEST_CASE("a DNM PLANNER_SNP of the wrong JSON type loads as blank") {
    const std::string path = "tests/importer/_tmp_dnm_planner_wrong_type.json";
    {
        std::ofstream out(path);
        out << R"({"REQUEST_ID":"X","DNM":[{"PLANNER_SNP":20,"LOCFRNO":"2027"}]})";
    }
    const DemandFile demand = Importer::load_demand(path);
    std::remove(path.c_str());

    REQUIRE(demand.dnm.size() == 1);
    CHECK(demand.dnm[0].planner_snp.empty());
    CHECK(demand.dnm[0].locfrno == "2027");
}

TEST_CASE("an STR PLANNER_SNP of the wrong JSON type loads as blank") {
    const std::string path = "tests/importer/_tmp_str_planner_wrong_type.json";
    {
        std::ofstream out(path);
        out << R"({"REQUEST_ID":"X","STR":[{"PLANNER_SNP":20,"LOCFRNO":"2027"}]})";
    }
    const DemandFile demand = Importer::load_demand(path);
    std::remove(path.c_str());

    REQUIRE(demand.str.size() == 1);
    CHECK(demand.str[0].planner_snp.empty());
    CHECK(demand.str[0].locfrno == "2027");
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

TEST_CASE("UNITOFMEAS only ever CS, DIS or PAL in this file" * doctest::skip(!crossDayTests::august17Present())) {
    DemandFile d = Importer::load_demand(DEMAND_PATH);
    for (const auto& r : d.str) {
        bool ok = (r.unitofmeas == "CS" || r.unitofmeas == "DIS" || r.unitofmeas == "PAL");
        REQUIRE(ok);
    }
}

TEST_CASE("a TRANS too large for a double is rejected at load, not passed on as infinity") {
    // nlohmann refuses a number that overflows a double (here 1e999), so an infinite quantity
    // cannot arrive through this file. The Validator's finite-quantity check covers any other route.
    const std::string path = "tests/importer/_tmp_trans_overflow.json";
    {
        std::ofstream out(path);
        out << R"({"REQUEST_ID":"X","STR":[{"LOCFRNO":"1","LOCTONO":"2","MATNR":"M1",)"
            << R"("DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":1e999,"UNITOFMEAS":"CS"}]})";
    }
    CHECK_THROWS_WITH_AS(Importer::load_demand(path), doctest::Contains("number overflow"),
                         std::runtime_error);
    std::remove(path.c_str());
}

TEST_CASE("a demand file whose root is an array throws instead of loading as empty") {
    const std::string path = "tests/importer/_tmp_root_array.json";
    {
        std::ofstream out(path);
        out << R"([{"STR":[]}])";
    }
    CHECK_THROWS_WITH_AS(Importer::load_demand(path), doctest::Contains("root must be a JSON object"),
                         std::runtime_error);
    std::remove(path.c_str());
}

TEST_CASE("a demand file whose root is a string throws instead of loading as empty") {
    const std::string path = "tests/importer/_tmp_root_string.json";
    {
        std::ofstream out(path);
        out << R"("STR")";
    }
    CHECK_THROWS_WITH_AS(Importer::load_demand(path), doctest::Contains("root must be a JSON object"),
                         std::runtime_error);
    std::remove(path.c_str());
}

namespace {

STRRecord loadOneLine(const std::string& lineJson) {
    const std::string path = "tests/importer/_tmp_str_integer_fields.json";
    {
        std::ofstream out(path);
        out << R"({"REQUEST_ID":"X","STR":[)" << lineJson << "]}";
    }
    const DemandFile demand = Importer::load_demand(path);
    std::remove(path.c_str());
    REQUIRE(demand.str.size() == 1);
    return demand.str[0];
}

} // namespace

TEST_CASE("a missing TPRIO loads as the unreadable sentinel, never as priority 0") {
    CHECK(loadOneLine(R"({"LOCFRNO":"2027"})").tprio == kUnreadablePriority);
}

TEST_CASE("a fractional TPRIO loads as the unreadable sentinel, not truncated") {
    CHECK(loadOneLine(R"({"TPRIO":2.9})").tprio == kUnreadablePriority);
}

TEST_CASE("a boolean or string TPRIO loads as the unreadable sentinel") {
    CHECK(loadOneLine(R"({"TPRIO":true})").tprio == kUnreadablePriority);
    CHECK(loadOneLine(R"({"TPRIO":"3"})").tprio == kUnreadablePriority);
}

TEST_CASE("an integer TPRIO of 0 loads as 0") {
    CHECK(loadOneLine(R"({"TPRIO":0})").tprio == 0);
}

TEST_CASE("a missing or fractional AVAIL_QTY loads as the unreadable sentinel") {
    CHECK(loadOneLine(R"({"LOCFRNO":"2027"})").avail_qty == kUnreadableAvailableQuantity);
    CHECK(loadOneLine(R"({"AVAIL_QTY":12.5})").avail_qty == kUnreadableAvailableQuantity);
}

TEST_CASE("a fractional BNFPO falls back to 0 rather than truncating") {
    CHECK(loadOneLine(R"({"BNFPO":10.5})").bnfpo == 0);
    CHECK(loadOneLine(R"({"BNFPO":20})").bnfpo == 20);
}
