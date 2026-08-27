// NOTE: no doctest main define here — main lives in one implementing TU only.
#include "doctest.h"
#include "validator.hpp"
#include "importer.hpp"
#include "product_importer.hpp"
#include <algorithm>

using namespace ob;

static const char* DEMAND_PATH  = "tests/importer/Demand-1.json";
static const char* PRODUCT_PATH = "tests/importer/Customer2-Product-Data.csv";

namespace {
struct Fixture {
    DemandFile        demand;
    ProductLoadResult products;
    ProductIndex      index;
    JoinResult        join;
    Fixture() {
        demand   = Importer::load_demand(DEMAND_PATH);
        products = ProductImporter::load(PRODUCT_PATH);
        index    = Joiner::build_index(products.products);
        join     = Joiner::join(demand.str, index);
    }
};

int count_rule(const ValidationReport& r, const std::string& rule) {
    return static_cast<int>(std::count_if(r.issues.begin(), r.issues.end(),
        [&](const ValidationIssue& i){ return i.rule == rule; }));
}
} // namespace

TEST_CASE("clean input produces no errors") {
    Fixture f;
    ValidationReport r = Validator::validate(f.join);

    // The supplied files are clean: every mandatory field present, every UoM
    // recognised, every quantity positive, every material matched.
    CHECK(r.errors == 0);
    CHECK(r.missing_fields    == 0);
    CHECK(r.unmatched_product == 0);
    CHECK(r.unknown_uom       == 0);
    CHECK(r.non_positive_qty  == 0);
    CHECK(r.zero_dimension    == 0);
    CHECK(r.blank_uom_product == 0);
}

TEST_CASE("ambiguous pallet-type matches are surfaced as warnings") {
    Fixture f;
    ValidationReport r = Validator::validate(f.join);

    // 144 lines join to a product with more than one pallet-type variant.
    CHECK(r.ambiguous_pallet == 144);
    CHECK(count_rule(r, "ambiguous_pallet_type") == 144);
    CHECK(r.warnings >= 144);
}

TEST_CASE("missing mandatory field is an error, and stops further checks") {
    Fixture f;
    JoinResult j = f.join;
    // Blank out the destination on a copy of a real line.
    static STRRecord broken = *j.lines[0].str;
    broken.loctono = "";
    j.lines[0].str = &broken;

    ValidationReport r = Validator::validate(j);
    CHECK(r.missing_fields == 1);
    CHECK(r.errors == 1);
    CHECK(count_rule(r, "missing_fields") == 1);
}

TEST_CASE("unrecognised unit of measure is a warning, not an error") {
    Fixture f;
    JoinResult j = f.join;
    static STRRecord odd = *j.lines[0].str;
    odd.unitofmeas = "EA";          // eaches — not in the expected set
    j.lines[0].str = &odd;

    ValidationReport r = Validator::validate(j);
    CHECK(r.unknown_uom == 1);
    CHECK(r.errors == 0);           // never blocks the run
    CHECK(count_rule(r, "unknown_uom") == 1);
}

TEST_CASE("non-positive quantity is an error") {
    Fixture f;
    JoinResult j = f.join;
    static STRRecord zero = *j.lines[0].str;
    zero.trans = 0.0;
    j.lines[0].str = &zero;

    ValidationReport r = Validator::validate(j);
    CHECK(r.non_positive_qty == 1);
    CHECK(r.errors == 1);
}

TEST_CASE("unmatched product is an error and the line survives") {
    Fixture f;
    JoinResult j = f.join;
    j.lines[0].matched = false;
    j.lines[0].product = nullptr;

    ValidationReport r = Validator::validate(j);
    CHECK(r.unmatched_product == 1);
    CHECK(r.errors == 1);
    CHECK(j.lines.size() == f.join.lines.size());   // nothing dropped
}

TEST_CASE("pallet threshold is skipped when no pallet figures are supplied") {
    Fixture f;
    ValidationReport r = Validator::validate(f.join);   // no pallets passed
    CHECK(r.over_pallet_threshold == 0);
}

TEST_CASE("pallet threshold flags large lines as warnings, never errors") {
    Fixture f;
    // Give every line a pallet count above any sane threshold.
    std::vector<double> pallets(f.join.lines.size(), 500.0);

    ValidationConfig cfg;
    cfg.pallet_warn_threshold = 300.0;

    ValidationReport r = Validator::validate(f.join, pallets, cfg);
    CHECK(r.over_pallet_threshold == static_cast<int>(f.join.lines.size()));
    CHECK(r.errors == 0);           // this check must never block a run
    CHECK(count_rule(r, "large_line") == static_cast<int>(f.join.lines.size()));
}

TEST_CASE("pallet threshold is configurable") {
    Fixture f;
    std::vector<double> pallets(f.join.lines.size(), 100.0);

    ValidationConfig low;  low.pallet_warn_threshold  = 50.0;
    ValidationConfig high; high.pallet_warn_threshold = 1000.0;

    CHECK(Validator::validate(f.join, pallets, low).over_pallet_threshold
              == static_cast<int>(f.join.lines.size()));
    CHECK(Validator::validate(f.join, pallets, high).over_pallet_threshold == 0);
}

TEST_CASE("every issue carries a traceable line index and rule name") {
    Fixture f;
    ValidationReport r = Validator::validate(f.join);
    for (const auto& i : r.issues) {
        REQUIRE(i.line_index >= 0);
        REQUIRE_FALSE(i.rule.empty());
        REQUIRE_FALSE(i.message.empty());
    }
}
