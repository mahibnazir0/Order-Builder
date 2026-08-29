// NOTE: no doctest main define here — main lives in one implementing TU only.
#include "doctest.h"
#include "converter.hpp"
#include "importer.hpp"
#include "product_importer.hpp"
#include "joiner.hpp"

#include <algorithm>
#include <string>

using namespace ob;

static const char* DEMAND_PATH  = "tests/importer/Demand-1.json";
static const char* PRODUCT_PATH = "tests/importer/Customer2-Product-Data.csv";

namespace {

// Build a master record by hand so the arithmetic tests do not depend on
// finding a particular row. Rows copied verbatim from the real CSV are used
// where the point of the test is that the real data behaves as expected.
ProductRecord make_product(int cases_unit_load,
                           double case_weight_lb,
                           const std::string& pallet_id) {
    ProductRecord p;
    p.id               = "TEST";
    p.cases_unit_load  = cases_unit_load;
    p.weight_lb        = case_weight_lb;
    p.pallet_id        = pallet_id;
    return p;
}

const ProductRecord& find_product(const std::vector<ProductRecord>& products,
                                  const std::string& id,
                                  const std::string& pallet_id) {
    auto it = std::find_if(products.begin(), products.end(),
        [&](const ProductRecord& p){ return p.id == id && p.pallet_id == pallet_id; });
    REQUIRE(it != products.end());
    return *it;
}

} // anonymous namespace

// ─── Length ────────────────────────────────────────────────────────────────

TEST_CASE("inches and centimetres convert by the exact definition") {
    CHECK(Converter::inches_to_cm(1.0) == doctest::Approx(2.54));
    CHECK(Converter::inches_to_cm(0.0) == doctest::Approx(0.0));

    // 630 in — the interior length of the 53-foot trailer that a blank
    // ZZNA_EQUIP_SIZE defaults to (43 of the 189 placeholder records).
    CHECK(Converter::inches_to_cm(630.0) == doctest::Approx(1600.2));

    CHECK(Converter::cm_to_inches(2.54) == doctest::Approx(1.0));
    CHECK(Converter::cm_to_inches(1600.2) == doctest::Approx(630.0));
}

TEST_CASE("length conversion round-trips in both directions") {
    // 48 x 40 x 19.5 — product 100802205, a real master row.
    for (double in : {48.0, 40.0, 19.5, 12.875, 9.563, 7.875}) {
        CHECK(Converter::cm_to_inches(Converter::inches_to_cm(in)) == doctest::Approx(in));
    }
    for (double cm : {121.92, 101.6, 49.53}) {
        CHECK(Converter::inches_to_cm(Converter::cm_to_inches(cm)) == doctest::Approx(cm));
    }
}

TEST_CASE("length conversion handles negatives without special-casing them") {
    // Not expected in the data, but the function must stay pure arithmetic.
    CHECK(Converter::inches_to_cm(-10.0) == doctest::Approx(-25.4));
    CHECK(Converter::cm_to_inches(-25.4) == doctest::Approx(-10.0));
}

// ─── Quantity to pallets ───────────────────────────────────────────────────

TEST_CASE("CS divides the case count by Cases_Unit_Load") {
    // Real line: MATNR 105521103, TRANS 1439 CS; master row is TLD, 48 cases
    // per unit load. 1439 / 48 = 29.9791666...
    ProductRecord p = make_product(48, 6.768, "TLD");
    CHECK(Converter::to_pallets(1439.0, "CS", p) == doctest::Approx(1439.0 / 48.0));
    CHECK(Converter::to_pallets(1439.0, "CS", p) == doctest::Approx(29.9791666667));
}

TEST_CASE("PAL and DIS pass the quantity straight through") {
    // Real lines: 105675700 TRANS 6 PAL, and 104764204 TRANS 12 DIS.
    // Both are already expressed in unit loads, so Cases_Unit_Load is not used.
    ProductRecord p = make_product(1, 235.2, "PGM");
    CHECK(Converter::to_pallets(6.0,  "PAL", p) == doctest::Approx(6.0));
    CHECK(Converter::to_pallets(12.0, "DIS", p) == doctest::Approx(12.0));

    // Cases_Unit_Load must be ignored for these codes, not divided by.
    ProductRecord many = make_product(168, 9.7, "TLD");
    CHECK(Converter::to_pallets(6.0, "PAL", many) == doctest::Approx(6.0));
    CHECK(Converter::to_pallets(6.0, "DIS", many) == doctest::Approx(6.0));
}

TEST_CASE("pallet-equivalents are never rounded") {
    // Tom's ruling on '0.9 of a pallet?': fractions are summed across the
    // lane, so the fraction has to survive this function intact.
    ProductRecord p = make_product(10, 9.0, "TLD");
    CHECK(Converter::to_pallets(9.0, "CS", p) == doctest::Approx(0.9));
    CHECK(Converter::to_pallets(1.0, "CS", p) == doctest::Approx(0.1));
    CHECK(Converter::to_pallets(11.0, "CS", p) == doctest::Approx(1.1));

    // Explicitly not 1, not 0, not truncated.
    CHECK(Converter::to_pallets(9.0, "CS", p) != doctest::Approx(1.0));
    CHECK(Converter::to_pallets(9.0, "CS", p) > 0.0);
}

TEST_CASE("Cases_Unit_Load of zero returns zero instead of dividing by zero") {
    // Real row: 106052500, a ROL product with Cases_Layer, Layers_Unit_Load
    // and Cases_Unit_Load all 0. Exactly one row in the master is like this.
    ProductRecord p = make_product(0, 21.7, "TLD");
    const double pallets = Converter::to_pallets(100.0, "CS", p);
    CHECK(pallets == doctest::Approx(0.0));
    // Must be a real zero, not an infinity or a NaN.
    CHECK(pallets == pallets);
    CHECK(pallets < 1e300);
}

TEST_CASE("unrecognised units of measure return zero and never throw") {
    ProductRecord p = make_product(48, 6.768, "TLD");

    // The demand file only ever carries CS, DIS and PAL. Anything else is the
    // Validator's problem to report, not the Converter's to reject.
    CHECK_NOTHROW(Converter::to_pallets(100.0, "EA", p));
    CHECK(Converter::to_pallets(100.0, "EA", p) == doctest::Approx(0.0));
    CHECK(Converter::to_pallets(100.0, "",   p) == doctest::Approx(0.0));
    CHECK(Converter::to_pallets(100.0, "ROL", p) == doctest::Approx(0.0));

    // Codes are matched exactly — no case folding is applied anywhere else
    // in the pipeline either.
    CHECK(Converter::to_pallets(100.0, "cs", p) == doctest::Approx(0.0));
}

// ─── Weight ────────────────────────────────────────────────────────────────

TEST_CASE("weight is on a unit-load basis, not a single case") {
    // 105553001 TLD: 9.7 lb per case, 168 cases per unit load, no wood.
    ProductRecord tld = make_product(168, 9.7, "TLD");
    CHECK(Converter::to_weight_lb(1.0, tld) == doctest::Approx(9.7 * 168.0));
    CHECK(Converter::to_weight_lb(1.0, tld) == doctest::Approx(1629.6));

    // The case weight alone would be badly wrong — guard against a regression
    // back to that reading.
    CHECK(Converter::to_weight_lb(1.0, tld) != doctest::Approx(9.7));
}

TEST_CASE("wood pallets add their weight, non-wood pallets add nothing") {
    CHECK(Converter::pallet_has_wood("PTL"));
    CHECK(Converter::pallet_has_wood("PGM"));
    CHECK_FALSE(Converter::pallet_has_wood("TLD"));
    CHECK_FALSE(Converter::pallet_has_wood("GMA"));
    CHECK_FALSE(Converter::pallet_has_wood(""));

    // Real row 100802205: PTL, 315 lb per case, 3 cases per unit load.
    // 315 * 3 = 945, plus 60 lb of wood = 1005.
    ProductRecord ptl = make_product(3, 315.0, "PTL");
    CHECK(Converter::to_weight_lb(1.0, ptl) == doctest::Approx(1005.0));

    // Same numbers on a TLD pallet get no wood at all.
    ProductRecord tld = make_product(3, 315.0, "TLD");
    CHECK(Converter::to_weight_lb(1.0, tld) == doctest::Approx(945.0));

    CHECK(Converter::to_weight_lb(1.0, ptl) - Converter::to_weight_lb(1.0, tld)
          == doctest::Approx(60.0));
}

TEST_CASE("the wood pallet weight is a parameter, not a baked-in literal") {
    ProductRecord ptl = make_product(3, 315.0, "PTL");

    // Tom has not confirmed 60 lb, so it must be overridable.
    CHECK(Converter::to_weight_lb(1.0, ptl, 0.0)  == doctest::Approx(945.0));
    CHECK(Converter::to_weight_lb(1.0, ptl, 45.0) == doctest::Approx(990.0));
    CHECK(Converter::to_weight_lb(1.0, ptl, 75.0) == doctest::Approx(1020.0));

    // The override must not leak onto pallet types that carry no wood.
    ProductRecord gma = make_product(3, 315.0, "GMA");
    CHECK(Converter::to_weight_lb(1.0, gma, 500.0) == doctest::Approx(945.0));
}

TEST_CASE("weight scales linearly with fractional pallets") {
    ProductRecord ptl = make_product(3, 315.0, "PTL");
    CHECK(Converter::to_weight_lb(0.5, ptl) == doctest::Approx(502.5));
    CHECK(Converter::to_weight_lb(2.0, ptl) == doctest::Approx(2010.0));
    CHECK(Converter::to_weight_lb(0.0, ptl) == doctest::Approx(0.0));
}

// ─── Against the real files ────────────────────────────────────────────────

TEST_CASE("converter reproduces the pallet-equivalent total for the whole file") {
    DemandFile        demand   = Importer::load_demand(DEMAND_PATH);
    ProductLoadResult products = ProductImporter::load(PRODUCT_PATH);
    ProductIndex      index    = Joiner::build_index(products.products);
    JoinResult        join     = Joiner::join(demand.str, index);

    double total_pallets = 0.0;
    double total_weight  = 0.0;
    for (const auto& line : join.lines) {
        if (line.product == nullptr) continue;
        const double pallets =
            Converter::to_pallets(line.str->trans, line.str->unitofmeas, *line.product);
        total_pallets += pallets;
        total_weight  += Converter::to_weight_lb(pallets, *line.product);
    }

    // The figure in the Milestone 1 summary Tom checks against his own.
    CHECK(total_pallets == doctest::Approx(152911.2).epsilon(1e-6));

    // Weight follows from the unit-load basis plus 60 lb of wood.
    CHECK(total_weight == doctest::Approx(103005832.85).epsilon(1e-9));
}

TEST_CASE("the two variants of an ambiguous product convert differently") {
    // This is why the joiner flags 144 lines: 105553001 is 84 cases per unit
    // load as GMA and 168 as TLD, so the same demand line is worth twice as
    // many pallets depending on which variant is chosen.
    ProductLoadResult products = ProductImporter::load(PRODUCT_PATH);

    const ProductRecord& gma = find_product(products.products, "105553001", "GMA");
    const ProductRecord& tld = find_product(products.products, "105553001", "TLD");

    REQUIRE(gma.cases_unit_load == 84);
    REQUIRE(tld.cases_unit_load == 168);

    const double as_gma = Converter::to_pallets(168.0, "CS", gma);
    const double as_tld = Converter::to_pallets(168.0, "CS", tld);

    CHECK(as_gma == doctest::Approx(2.0));
    CHECK(as_tld == doctest::Approx(1.0));
    CHECK(as_gma == doctest::Approx(as_tld * 2.0));

    // Neither variant is wood, so the choice does not change the weight.
    CHECK(Converter::to_weight_lb(as_gma, gma)
          == doctest::Approx(Converter::to_weight_lb(as_tld, tld)));
}
