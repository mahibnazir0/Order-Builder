// NOTE: no doctest main define here — main lives in one implementing TU only.
#include "doctest.h"
#include "joiner.hpp"
#include "importer.hpp"
#include "product_importer.hpp"
#include <algorithm>

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

TEST_CASE("index groups variants rather than dropping them") {
    Fixture f;
    // 20,183 unique IDs in the master.
    CHECK(f.index.size() == 20183);

    // 18 of those IDs carry more than one pallet-type variant.
    int multi = 0;
    for (const auto& kv : f.index) if (kv.second.size() > 1) ++multi;
    CHECK(multi == 18);
}

TEST_CASE("every demand line matches a product") {
    Fixture f;
    JoinResult r = Joiner::join(f.demand.str, f.index);

    CHECK(r.lines.size()      == 24357);
    CHECK(r.matched_lines     == 24357);   // 100% match on the real files
    CHECK(r.unmatched_lines   == 0);
    CHECK(r.unmatched_matnrs.empty());
}

TEST_CASE("ambiguous pallet-type matches are counted, not hidden") {
    Fixture f;
    JoinResult r = Joiner::join(f.demand.str, f.index);

    // 144 lines hit a product with more than one pallet-type variant,
    // spanning 6 of the 18 multi-variant products.
    CHECK(r.ambiguous_lines == 144);
    CHECK(r.ambiguous_matnrs.size() == 6);
}

TEST_CASE("preference order decides which variant is chosen") {
    Fixture f;

    // ID 105553001 exists as GMA (84 cases/unit load) and TLD (168).
    auto it = f.index.find("105553001");
    REQUIRE(it != f.index.end());
    REQUIRE(it->second.size() == 2);

    // Default preference puts TLD first.
    JoinResult tld = Joiner::join(f.demand.str, f.index, {"TLD", "PTL", "PGM", "GMA"});
    // Reversing it must select the other variant.
    JoinResult gma = Joiner::join(f.demand.str, f.index, {"GMA", "PGM", "PTL", "TLD"});

    auto find_line = [](const JoinResult& r, const std::string& matnr) -> const JoinedLine* {
        for (const auto& l : r.lines) if (l.str->matnr == matnr) return &l;
        return nullptr;
    };

    const JoinedLine* a = find_line(tld, "105553001");
    const JoinedLine* b = find_line(gma, "105553001");
    if (a && b) {
        REQUIRE(a->product != nullptr);
        REQUIRE(b->product != nullptr);
        CHECK(a->product->pallet_id == "TLD");
        CHECK(b->product->pallet_id == "GMA");
        // The whole point: the choice changes the pallet divisor.
        CHECK(a->product->cases_unit_load != b->product->cases_unit_load);
    }
}

TEST_CASE("unmatched lines are kept with a null product, never dropped") {
    Fixture f;

    // Inject a demand line for a material that is not in the master.
    std::vector<STRRecord> demand = f.demand.str;
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

TEST_CASE("every matched line points at a real product") {
    Fixture f;
    JoinResult r = Joiner::join(f.demand.str, f.index);
    for (const auto& l : r.lines) {
        if (l.matched) {
            REQUIRE(l.product != nullptr);
            REQUIRE(l.product->id == l.str->matnr);
        }
    }
}
