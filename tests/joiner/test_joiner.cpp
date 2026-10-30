// NOTE: no doctest main define here — main lives in one implementing TU only.
#include "doctest.h"
#include "../importer/crossDayFixtures.hpp"
#include "joiner.hpp"
#include "importer.hpp"
#include "product_importer.hpp"
#include <algorithm>
#include <limits>

using namespace std;

using namespace ob;

static const char* DEMAND_PATH  = "tests/importer/Demand-1.json";
static const char* PRODUCT_PATH = "tests/importer/Customer2-Product-Data.csv";

namespace {
// Load both files once per test case that needs them.
struct Fixture {
    DemandFile        demand;
    ProductLoadResult products;
    ProductIndex      index;
    Fixture() {
        demand   = Importer::load_demand(DEMAND_PATH);
        products = ProductImporter::load(PRODUCT_PATH);
        index    = Joiner::build_index(products.products);
    }
};
} // namespace

TEST_CASE("index groups variants rather than dropping them" * doctest::skip(!crossDayTests::august17Present())) {
    Fixture f;
    // 20,183 unique IDs in the master.
    CHECK(f.index.size() == 20183);

    // 18 of those IDs carry more than one pallet-type variant.
    int multi = 0;
    for (const auto& kv : f.index) if (kv.second.size() > 1) ++multi;
    CHECK(multi == 18);
}

TEST_CASE("every demand line matches a product" * doctest::skip(!crossDayTests::august17Present())) {
    Fixture f;
    JoinResult r = Joiner::join(f.demand.str, f.index);

    CHECK(r.lines.size()      == 24357);
    CHECK(r.matched_lines     == 24357);   // 100% match on the real files
    CHECK(r.unmatched_lines   == 0);
    CHECK(r.unmatched_matnrs.empty());
}

TEST_CASE("lines the quantity does not settle are counted, not hidden" * doctest::skip(!crossDayTests::august17Present())) {
    Fixture f;
    JoinResult r = Joiner::join(f.demand.str, f.index);

    // 144 lines hit a product with more than one pallet type; the quantity settles all but 36
    // (35 that several types divide evenly, 1 that none does), on 3 of the 18 such products.
    CHECK(r.multiPalletTypeLines == 144);
    CHECK(r.ambiguous_lines == 36);
    CHECK(r.ambiguous_matnrs.size() == 3);
}

namespace {

ProductRecord palletType(const string& palletId, int casesUnitLoad) {
    ProductRecord record;
    record.id = "P";
    record.pallet_id = palletId;
    record.cases_unit_load = casesUnitLoad;
    return record;
}

string chosenType(const vector<ProductRecord>& types, double quantity,
                  const vector<string>& preference = Joiner::default_pallet_preference()) {
    const Joiner::PalletTypePick pick = Joiner::choosePalletType(types, quantity, preference);
    REQUIRE(pick.product != nullptr);
    return pick.product->pallet_id;
}

PalletTypeChoice choiceFor(const vector<ProductRecord>& types, double quantity) {
    return Joiner::choosePalletType(types, quantity).choice;
}

const vector<ProductRecord> kGmaOrTld{palletType("GMA", 84), palletType("TLD", 168)};

} // namespace

TEST_CASE("the quantity picks the pallet type whose unit load divides it evenly" * doctest::skip(!crossDayTests::august17Present())) {
    // ID 105553001 is listed as GMA (84 cases per unit load) and TLD (168): the client's
    // worked examples (m2BusinessRules section 2).
    Fixture f;
    const auto it = f.index.find("105553001");
    REQUIRE(it != f.index.end());
    const vector<ProductRecord>& types = it->second;
    REQUIRE(types.size() == 2);

    CHECK(chosenType(types, 252.0) == "GMA");
    CHECK(choiceFor(types, 252.0) == PalletTypeChoice::DividesEvenly);
    CHECK(chosenType(types, 336.0) == "GMA");
    CHECK(choiceFor(types, 336.0) == PalletTypeChoice::SeveralDivide);
    CHECK(chosenType(types, 100.0) == "TLD");
    CHECK(choiceFor(types, 100.0) == PalletTypeChoice::NoneDivides);
}

TEST_CASE("TLD is taken when only TLD divides the quantity evenly") {
    const vector<ProductRecord> types{palletType("TLD", 182), palletType("GMA", 104)};
    CHECK(chosenType(types, 182.0) == "TLD");
    CHECK(choiceFor(types, 182.0) == PalletTypeChoice::DividesEvenly);
}

TEST_CASE("when several types divide the quantity evenly GMA is taken, whatever the preference") {
    CHECK(chosenType(kGmaOrTld, 168.0) == "GMA");
    CHECK(chosenType(kGmaOrTld, 168.0, {"TLD", "PTL", "PGM", "GMA"}) == "GMA");
}

TEST_CASE("when several types without GMA divide evenly, the preference order decides") {
    const vector<ProductRecord> types{palletType("PGM", 24), palletType("TLD", 48)};
    CHECK(chosenType(types, 96.0) == "TLD");
    CHECK(chosenType(types, 96.0, {"PGM", "TLD"}) == "PGM");
    CHECK(choiceFor(types, 96.0) == PalletTypeChoice::SeveralDivide);
}

TEST_CASE("when no type divides the quantity evenly, the preference order decides") {
    CHECK(chosenType(kGmaOrTld, 100.0) == "TLD");
    CHECK(chosenType(kGmaOrTld, 100.0, {"GMA", "PGM", "PTL", "TLD"}) == "GMA");
}

TEST_CASE("a quantity that is not a positive number never picks a type by divisibility") {
    for (const double quantity : {0.0, -168.0, numeric_limits<double>::quiet_NaN(),
                                  numeric_limits<double>::infinity(),
                                  -numeric_limits<double>::infinity()}) {
        CAPTURE(quantity);
        CHECK(choiceFor(kGmaOrTld, quantity) == PalletTypeChoice::NoneDivides);
        CHECK(chosenType(kGmaOrTld, quantity) == "TLD");
    }
}

TEST_CASE("a fractional quantity divides evenly by no type") {
    CHECK(choiceFor(kGmaOrTld, 168.5) == PalletTypeChoice::NoneDivides);
}

TEST_CASE("a type with no usable Cases_Unit_Load never divides a quantity") {
    for (const int casesUnitLoad : {0, -84}) {
        CAPTURE(casesUnitLoad);
        const vector<ProductRecord> types{palletType("GMA", casesUnitLoad), palletType("TLD", 168)};
        CHECK(chosenType(types, 168.0) == "TLD");
        CHECK(choiceFor(types, 168.0) == PalletTypeChoice::DividesEvenly);
    }
}

TEST_CASE("a product on one pallet type takes it, whatever the quantity") {
    const vector<ProductRecord> onlyTld{palletType("TLD", 168)};
    CHECK(chosenType(onlyTld, 100.0) == "TLD");
    CHECK(choiceFor(onlyTld, 100.0) == PalletTypeChoice::OnlyType);
    CHECK(Joiner::choosePalletType({}, 100.0).product == nullptr);
}

TEST_CASE("a joined line points at every pallet type its product is listed on" * doctest::skip(!crossDayTests::august17Present())) {
    Fixture f;
    const JoinResult r = Joiner::join(f.demand.str, f.index);
    for (const auto& line : r.lines) {
        if (line.str->matnr != "105553001") continue;
        REQUIRE(line.palletTypes != nullptr);
        CHECK(line.palletTypes->size() == 2);
        CHECK(line.product >= line.palletTypes->data());
        CHECK(line.product < line.palletTypes->data() + line.palletTypes->size());
    }
}

TEST_CASE("unmatched lines are kept with a null product, never dropped" * doctest::skip(!crossDayTests::august17Present())) {
    Fixture f;

    // Inject a demand line for a material that is not in the master.
    vector<STRRecord> demand = f.demand.str;
    STRRecord ghost = demand.front();
    ghost.matnr = "999999999";
    demand.push_back(ghost);

    JoinResult r = Joiner::join(demand, f.index);

    CHECK(r.lines.size()    == demand.size());   // nothing dropped
    CHECK(r.unmatched_lines == 1);
    REQUIRE(r.unmatched_matnrs.size() == 1);
    CHECK(r.unmatched_matnrs[0] == "999999999");

    const JoinedLine& last = r.lines.back();
    CHECK(last.matched == false);
    CHECK(last.product == nullptr);
    CHECK(last.str     != nullptr);              // the demand line survives
}

TEST_CASE("every matched line points at a real product" * doctest::skip(!crossDayTests::august17Present())) {
    Fixture f;
    JoinResult r = Joiner::join(f.demand.str, f.index);
    for (const auto& l : r.lines) {
        if (l.matched) {
            REQUIRE(l.product != nullptr);
            REQUIRE(l.product->id == l.str->matnr);
        }
    }
}
