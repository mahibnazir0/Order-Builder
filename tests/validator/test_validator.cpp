// NOTE: no doctest main define here — main lives in one implementing TU only.
#include "doctest.h"
#include "validator.hpp"
#include "importer.hpp"
#include "product_importer.hpp"
#include "placeholder_importer.hpp"
#include "reporter.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>

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
    CHECK(r.unknown_ship_cond == 0);
    CHECK(r.non_positive_qty  == 0);
    CHECK(r.zero_unit_load    == 0);
    CHECK(r.zero_dimension    == 0);
    CHECK(r.blank_uom_product == 0);
    CHECK(r.unrecognized_pallet_id == 0);
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

TEST_CASE("a demand line with a blank SHIP_COND is an error and is excluded") {
    Fixture f;
    JoinResult j = f.join;
    static STRRecord broken = *j.lines[0].str;
    broken.ship_cond = "";
    j.lines[0].str = &broken;

    ValidationReport r = Validator::validate(j);
    CHECK(r.missing_fields == 1);
    CHECK(r.errors == 1);
    CHECK(r.unknown_ship_cond == 0);       // blank is missing, not also "unknown"
    CHECK(Validator::excludedLineFlags(r, j.lines.size())[0]);
}

TEST_CASE("an unrecognised SHIP_COND on a demand line is a warning, not an error") {
    Fixture f;
    JoinResult j = f.join;
    static STRRecord odd = *j.lines[0].str;
    odd.ship_cond = "TX";
    j.lines[0].str = &odd;

    ValidationReport r = Validator::validate(j);
    CHECK(r.unknown_ship_cond == 1);
    CHECK(r.errors == 0);
    CHECK(count_rule(r, "unknown_ship_cond") == 1);
    CHECK_FALSE(Validator::excludedLineFlags(r, j.lines.size())[0]);
}

TEST_CASE("the allowed SHIP_COND set is configurable") {
    Fixture f;
    ValidationConfig config;
    config.allowed_ship_cond = {"TL"};
    CHECK(Validator::validate(f.join, {}, config).unknown_ship_cond > 0);   // TF lines now unknown
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

namespace {

// A non-finite TRANS on line 0 must be an Error and drop the line from derived totals.
void checkNonFiniteQuantityIsExcluded(double trans) {
    Fixture f;
    JoinResult j = f.join;
    static STRRecord bad_line;
    bad_line = *j.lines[0].str;
    bad_line.trans = trans;
    j.lines[0].str = &bad_line;

    ValidationReport r = Validator::validate(j);
    CHECK(r.non_positive_qty == 1);
    CHECK(r.errors == 1);
    CHECK(count_rule(r, "non_positive_qty") == 1);
    REQUIRE(!r.issues.empty());
    CHECK(r.issues[0].severity == ValidationIssue::Severity::Error);
    CHECK(Validator::excludedLineFlags(r, j.lines.size())[0]);
}

} // namespace

TEST_CASE("a NaN quantity is an error and the line is excluded") {
    checkNonFiniteQuantityIsExcluded(std::numeric_limits<double>::quiet_NaN());
}

TEST_CASE("an infinite quantity is an error and the line is excluded") {
    checkNonFiniteQuantityIsExcluded(std::numeric_limits<double>::infinity());
}

TEST_CASE("a negative infinite quantity is an error and the line is excluded") {
    checkNonFiniteQuantityIsExcluded(-std::numeric_limits<double>::infinity());
}

TEST_CASE("a CS line whose product has Cases_Unit_Load 0 is an error") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    static ProductRecord bad_product = *j.lines[0].product;
    bad_product.cases_unit_load = 0;
    j.lines[0].product = &bad_product;

    static STRRecord cs_line = *j.lines[0].str;
    cs_line.unitofmeas = "CS";
    j.lines[0].str = &cs_line;

    ValidationReport r = Validator::validate(j);
    CHECK(r.zero_unit_load == 1);
    CHECK(r.errors >= 1);
    CHECK(count_rule(r, "zero_unit_load") == 1);
}

namespace {

// Line 0 with the given UoM on a product whose Cases_Unit_Load is 0.
void checkZeroUnitLoadIsExcluded(const std::string& uom) {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    static ProductRecord bad_product;
    bad_product = *j.lines[0].product;
    bad_product.cases_unit_load = 0;
    j.lines[0].product = &bad_product;

    static STRRecord line;
    line = *j.lines[0].str;
    line.unitofmeas = uom;
    j.lines[0].str = &line;

    ValidationReport r = Validator::validate(j);
    CHECK(r.zero_unit_load == 1);
    CHECK(count_rule(r, "zero_unit_load") == 1);
    CHECK(Validator::excludedLineFlags(r, j.lines.size())[0]);
}

} // namespace

TEST_CASE("a PAL line on a zero-Cases_Unit_Load product is an error, its weight would omit the cargo") {
    checkZeroUnitLoadIsExcluded("PAL");
}

TEST_CASE("a DIS line on a zero-Cases_Unit_Load product is an error, its weight would omit the cargo") {
    checkZeroUnitLoadIsExcluded("DIS");
}

TEST_CASE("an unknown-UoM line is not also flagged for zero Cases_Unit_Load, it already converts to 0 pallets") {
    Fixture f;
    JoinResult j = f.join;
    static ProductRecord bad_product;
    bad_product = *j.lines[0].product;
    bad_product.cases_unit_load = 0;
    j.lines[0].product = &bad_product;
    static STRRecord line;
    line = *j.lines[0].str;
    line.unitofmeas = "EA";
    j.lines[0].str = &line;

    CHECK(Validator::validate(j).zero_unit_load == 0);
}

TEST_CASE("a negative Cases_Unit_Load is an error, not a silently negative pallet count") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    static ProductRecord bad_product = *j.lines[0].product;
    bad_product.cases_unit_load = -10;
    j.lines[0].product = &bad_product;

    ValidationReport r = Validator::validate(j);
    CHECK(r.negative_unit_load == 1);
    CHECK(r.errors >= 1);
    CHECK(count_rule(r, "negative_unit_load") == 1);
}

TEST_CASE("a non-finite product weight is an error, not a silently NaN total") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    static ProductRecord bad_product = *j.lines[0].product;
    bad_product.weight_lb = std::nan("");
    j.lines[0].product = &bad_product;

    ValidationReport r = Validator::validate(j);
    CHECK(r.invalid_weight == 1);
    CHECK(r.errors >= 1);
    CHECK(count_rule(r, "invalid_weight") == 1);
}

TEST_CASE("a negative product weight is an error") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    static ProductRecord bad_product = *j.lines[0].product;
    bad_product.weight_lb = -5.0;
    j.lines[0].product = &bad_product;

    ValidationReport r = Validator::validate(j);
    CHECK(r.invalid_weight == 1);
}

TEST_CASE("a product weight above the maximum is an invalid_weight error and the line is excluded") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    static ProductRecord heavyProduct = *j.lines[0].product;
    heavyProduct.weight_lb = kMaxCaseWeightLb * 2;
    j.lines[0].product = &heavyProduct;

    ValidationReport r = Validator::validate(j);
    CHECK(r.invalid_weight == 1);
    const auto weightIssue = std::find_if(r.issues.begin(), r.issues.end(),
        [](const ValidationIssue& i){ return i.rule == "invalid_weight"; });
    REQUIRE(weightIssue != r.issues.end());
    CHECK(weightIssue->severity == ValidationIssue::Severity::Error);
    CHECK(Validator::excludedLineFlags(r, j.lines.size())[0]);
}

TEST_CASE("a product weight exactly at the maximum raises no invalid_weight") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    static ProductRecord boundaryProduct = *j.lines[0].product;
    boundaryProduct.weight_lb = kMaxCaseWeightLb;
    j.lines[0].product = &boundaryProduct;

    ValidationReport r = Validator::validate(j);
    CHECK(r.invalid_weight == 0);
}

namespace {

ValidationReport validateWithFirstProduct(JoinResult& j, const ProductRecord& product) {
    static ProductRecord modifiedProduct;
    modifiedProduct = product;
    j.lines[0].product = &modifiedProduct;
    return Validator::validate(j);
}

} // namespace

TEST_CASE("a product with a negative height is an invalid_dimension error") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    ProductRecord badProduct = *j.lines[0].product;
    badProduct.height_in = -12.0;

    ValidationReport r = validateWithFirstProduct(j, badProduct);
    CHECK(r.invalid_dimension == 1);
    CHECK(count_rule(r, "invalid_dimension") == 1);
}

TEST_CASE("a product with a NaN length is an invalid_dimension error") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    ProductRecord badProduct = *j.lines[0].product;
    badProduct.length_in = std::nan("");

    ValidationReport r = validateWithFirstProduct(j, badProduct);
    CHECK(r.invalid_dimension == 1);
    CHECK(count_rule(r, "invalid_dimension") == 1);
}

TEST_CASE("a product with an infinite width is an invalid_dimension error") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    ProductRecord badProduct = *j.lines[0].product;
    badProduct.width_in = std::numeric_limits<double>::infinity();

    ValidationReport r = validateWithFirstProduct(j, badProduct);
    CHECK(r.invalid_dimension == 1);
    CHECK(count_rule(r, "invalid_dimension") == 1);
}

TEST_CASE("a line with an invalid dimension is excluded by excludedLineFlags") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    ProductRecord badProduct = *j.lines[0].product;
    badProduct.height_in = -12.0;

    ValidationReport r = validateWithFirstProduct(j, badProduct);
    CHECK(Validator::excludedLineFlags(r, j.lines.size())[0]);
}

TEST_CASE("a zero dimension is still a zero_dimension warning, not invalid_dimension") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    ProductRecord zeroDimensionProduct = *j.lines[0].product;
    zeroDimensionProduct.height_in = 0.0;

    ValidationReport r = validateWithFirstProduct(j, zeroDimensionProduct);
    CHECK(r.zero_dimension == 1);
    CHECK(r.invalid_dimension == 0);
    CHECK(count_rule(r, "invalid_dimension") == 0);
}

TEST_CASE("a product with a negative Layers_Unit_Load is an invalid_layer_data error") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    ProductRecord badProduct = *j.lines[0].product;
    badProduct.layers_unit_load = -3;

    ValidationReport r = validateWithFirstProduct(j, badProduct);
    CHECK(r.invalid_layer_data == 1);
    CHECK(count_rule(r, "invalid_layer_data") == 1);
}

TEST_CASE("a product with a zero Cases_Layer is an invalid_layer_data error") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    ProductRecord badProduct = *j.lines[0].product;
    badProduct.cases_layer = 0;

    ValidationReport r = validateWithFirstProduct(j, badProduct);
    CHECK(r.invalid_layer_data == 1);
    CHECK(count_rule(r, "invalid_layer_data") == 1);
}

TEST_CASE("a line with invalid layer data is excluded by excludedLineFlags") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    ProductRecord badProduct = *j.lines[0].product;
    badProduct.cases_layer = 0;

    ValidationReport r = validateWithFirstProduct(j, badProduct);
    CHECK(Validator::excludedLineFlags(r, j.lines.size())[0]);
}

TEST_CASE("the supplied fixtures raise no invalid_layer_data") {
    Fixture f;
    ValidationReport r = Validator::validate(f.join);
    CHECK(r.invalid_layer_data == 0);
    CHECK(count_rule(r, "invalid_layer_data") == 0);
}

TEST_CASE("an unreadable Strength raises invalid_strength") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    ProductRecord badProduct = *j.lines[0].product;
    badProduct.strength = kUnreadableStrength;

    ValidationReport r = validateWithFirstProduct(j, badProduct);
    CHECK(r.invalid_strength == 1);
    CHECK(count_rule(r, "invalid_strength") == 1);
}

TEST_CASE("a Strength above 10 raises invalid_strength") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    ProductRecord badProduct = *j.lines[0].product;
    badProduct.strength = 11;

    ValidationReport r = validateWithFirstProduct(j, badProduct);
    CHECK(r.invalid_strength == 1);
    CHECK(count_rule(r, "invalid_strength") == 1);
}

TEST_CASE("a Strength of 0 and of 10 raise no invalid_strength") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    for (int boundaryStrength : {0, 10}) {
        ProductRecord boundaryProduct = *j.lines[0].product;
        boundaryProduct.strength = boundaryStrength;

        ValidationReport r = validateWithFirstProduct(j, boundaryProduct);
        CHECK(r.invalid_strength == 0);
    }
}

namespace {

ValidationReport validateWithFirstQuantity(JoinResult& j, double trans) {
    static STRRecord modifiedLine;
    modifiedLine = *j.lines[0].str;
    modifiedLine.unitofmeas = "CS";
    modifiedLine.trans = trans;
    j.lines[0].str = &modifiedLine;
    return Validator::validate(j);
}

} // namespace

TEST_CASE("a TRANS above the maximum is an excessive_quantity error and the line is excluded") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].matched);

    ValidationReport r = validateWithFirstQuantity(j, 1e308);
    CHECK(r.excessive_quantity == 1);
    CHECK(count_rule(r, "excessive_quantity") == 1);
    CHECK(Validator::excludedLineFlags(r, j.lines.size())[0]);
}

TEST_CASE("a TRANS exactly at the maximum raises no excessive_quantity") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].matched);

    ValidationReport r = validateWithFirstQuantity(j, kMaxDemandQuantity);
    CHECK(r.excessive_quantity == 0);
    CHECK(count_rule(r, "excessive_quantity") == 0);
}

TEST_CASE("the supplied fixtures raise no excessive_quantity") {
    Fixture f;
    ValidationReport r = Validator::validate(f.join);
    CHECK(r.excessive_quantity == 0);
    CHECK(count_rule(r, "excessive_quantity") == 0);
}

TEST_CASE("an unrecognized Pallet_ID is a warning, not silently 'no wood'") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    static ProductRecord bad_product = *j.lines[0].product;
    bad_product.pallet_id = "XYZ";   // not TLD/PTL/PGM/GMA
    j.lines[0].product = &bad_product;

    ValidationReport r = Validator::validate(j);
    CHECK(r.unrecognized_pallet_id == 1);
    CHECK(count_rule(r, "unrecognized_pallet_id") == 1);
    CHECK(r.errors == 0);   // a data-quality warning, never blocks the run
}

TEST_CASE("a blank Pallet_ID is also an unrecognized-pallet-id warning") {
    Fixture f;
    JoinResult j = f.join;
    REQUIRE(j.lines[0].product != nullptr);

    static ProductRecord bad_product = *j.lines[0].product;
    bad_product.pallet_id = "";
    j.lines[0].product = &bad_product;

    ValidationReport r = Validator::validate(j);
    CHECK(r.unrecognized_pallet_id == 1);
}

TEST_CASE("a placeholder with a negative NO_OF_LOADS is an error") {
    std::vector<PlaceholderRecord> placeholders(1);
    placeholders[0].locfrno    = "2023";
    placeholders[0].loctono    = "2528";
    placeholders[0].ship_cond   = "TL";
    placeholders[0].no_of_loads = -3;

    ValidationReport r;
    Validator::validate_placeholders(placeholders, r);

    CHECK(r.negative_load_count == 1);
    CHECK(r.errors == 1);
    CHECK(count_rule(r, "negative_load_count") == 1);
    REQUIRE(r.issues.size() == 1);
    CHECK(r.issues[0].placeholder_index == 0);
    CHECK(r.issues[0].line_index == -1);   // must never be read as a demand line
}

TEST_CASE("a placeholder loaded with an unreadable NO_OF_LOADS is flagged by the Validator") {
    const std::string path = "tests/importer/_tmp_validator_pholder.json";
    {
        std::ofstream out(path);
        out << R"({"PHOLDER":[{"LOCFRNO":"2023","LOCTONO":"2528","SHIP_COND":"TL","NO_OF_LOADS":"three"}]})";
    }
    const PlaceholderLoadResult loaded = PlaceholderImporter::load(path);
    std::remove(path.c_str());

    ValidationReport r;
    Validator::validate_placeholders(loaded.placeholders, r);
    CHECK(r.negative_load_count == 1);
    CHECK(r.errors == 1);
}

TEST_CASE("a placeholder loaded with a fractional NO_OF_LOADS is flagged by the Validator") {
    const std::string path = "tests/importer/_tmp_validator_pholder.json";
    {
        std::ofstream out(path);
        out << R"({"PHOLDER":[{"LOCFRNO":"2023","LOCTONO":"2528","SHIP_COND":"TL","NO_OF_LOADS":2.9}]})";
    }
    const PlaceholderLoadResult loaded = PlaceholderImporter::load(path);
    std::remove(path.c_str());

    ValidationReport r;
    Validator::validate_placeholders(loaded.placeholders, r);
    CHECK(r.negative_load_count == 1);
}

TEST_CASE("a product whose Weight cell was unreadable is an invalid_weight error and the line is excluded") {
    const std::string path = "tests/importer/_tmp_validator_weight.csv";
    {
        std::ofstream out(path);
        out << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
               "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n";
        out << "T1,Test,10,10,10,5,CS,9500x,4,2,8,TLD\n";
    }
    const ProductLoadResult loaded = ProductImporter::load(path);
    std::remove(path.c_str());

    const DemandFile demand = Importer::load_demand(DEMAND_PATH);
    REQUIRE_FALSE(demand.str.empty());
    std::vector<STRRecord> demandLines{demand.str[0]};
    demandLines[0].matnr = "T1";

    const ProductIndex index = Joiner::build_index(loaded.products);
    const JoinResult j = Joiner::join(demandLines, index);
    REQUIRE(j.lines[0].product != nullptr);

    ValidationReport r = Validator::validate(j);
    CHECK(r.invalid_weight == 1);
    CHECK(Validator::excludedLineFlags(r, j.lines.size())[0]);
}

TEST_CASE("a placeholder with NO_OF_LOADS above the maximum is an error") {
    std::vector<PlaceholderRecord> placeholders(2);
    placeholders[0].locfrno     = "2023";
    placeholders[0].loctono     = "2528";
    placeholders[0].ship_cond   = "TL";
    placeholders[0].no_of_loads = kMaxLoadsPerPlaceholder;       // the limit itself is allowed
    placeholders[1].locfrno     = "2023";
    placeholders[1].loctono     = "2529";
    placeholders[1].ship_cond   = "TL";
    placeholders[1].no_of_loads = kMaxLoadsPerPlaceholder + 1;

    ValidationReport r;
    Validator::validate_placeholders(placeholders, r);

    CHECK(r.excessive_load_count == 1);
    CHECK(r.errors == 1);
    CHECK(count_rule(r, "excessive_load_count") == 1);
    REQUIRE(r.issues.size() == 1);
    CHECK(r.issues[0].placeholder_index == 1);
    CHECK(r.issues[0].line_index == -1);
}

TEST_CASE("a placeholder with a blank SHIP_COND is an error and its trucks are excluded") {
    std::vector<PlaceholderRecord> placeholders(1);
    placeholders[0].locfrno     = "2023";
    placeholders[0].loctono     = "2528";
    placeholders[0].ship_cond   = "";
    placeholders[0].no_of_loads = 4;

    ValidationReport r;
    Validator::validate_placeholders(placeholders, r);
    CHECK(r.missing_ship_cond == 1);
    CHECK(r.unknown_ship_cond == 0);
    CHECK(r.errors == 1);
    REQUIRE(r.issues.size() == 1);
    CHECK(r.issues[0].placeholder_index == 0);

    DaySummary day = Reporter::build(JoinResult{}, placeholders, {}, {}, "", r);
    CHECK(day.excluded_placeholders == 1);
    CHECK(day.trucks_requested == 0);
}

TEST_CASE("a placeholder with an unrecognised SHIP_COND is a warning, not an error") {
    std::vector<PlaceholderRecord> placeholders(1);
    placeholders[0].locfrno     = "2023";
    placeholders[0].loctono     = "2528";
    placeholders[0].ship_cond   = "TX";
    placeholders[0].no_of_loads = 4;

    ValidationReport r;
    Validator::validate_placeholders(placeholders, r);
    CHECK(r.unknown_ship_cond == 1);
    CHECK(r.errors == 0);
    CHECK(Reporter::build(JoinResult{}, placeholders, {}, {}, "", r).trucks_requested == 4);
}

TEST_CASE("a placeholder with a blank lane identifier is an error") {
    std::vector<PlaceholderRecord> placeholders(1);
    placeholders[0].locfrno     = "";
    placeholders[0].loctono     = "2528";
    placeholders[0].ship_cond   = "TL";
    placeholders[0].no_of_loads = 2;

    ValidationReport r;
    Validator::validate_placeholders(placeholders, r);

    CHECK(r.missing_lane_identifier == 1);
    CHECK(count_rule(r, "missing_lane_identifier") == 1);
}

TEST_CASE("a DNM entry with a blank PLANNER_SNP raises invalid_do_not_mix_pair") {
    std::vector<DNMRecord> pairs(1);
    pairs[0].planner_snp = "";
    pairs[0].locfrno     = "2027";

    ValidationReport r;
    Validator::validate_do_not_mix(pairs, {}, r);

    CHECK(r.invalid_do_not_mix_pair == 1);
    CHECK(count_rule(r, "invalid_do_not_mix_pair") == 1);
}

TEST_CASE("a DNM entry with a blank LOCFRNO raises invalid_do_not_mix_pair") {
    std::vector<DNMRecord> pairs(1);
    pairs[0].planner_snp = "S20";
    pairs[0].locfrno     = " ";

    ValidationReport r;
    Validator::validate_do_not_mix(pairs, {}, r);

    CHECK(r.invalid_do_not_mix_pair == 1);
    CHECK(count_rule(r, "invalid_do_not_mix_pair") == 1);
}

TEST_CASE("the supplied DNM block raises no invalid_do_not_mix_pair") {
    const DemandFile demand = Importer::load_demand(DEMAND_PATH);
    REQUIRE_FALSE(demand.dnm.empty());

    ValidationReport r;
    Validator::validate_do_not_mix(demand.dnm, demand.str, r);

    CHECK(r.invalid_do_not_mix_pair == 0);
    CHECK(r.issues.empty());
}

namespace {
std::vector<DNMRecord> doNotMixAt2027() {
    std::vector<DNMRecord> pairs(1);
    pairs[0].planner_snp = "S20";
    pairs[0].locfrno     = "2027";
    return pairs;
}

std::vector<STRRecord> oneDemandLine(const std::string& locfrno, const std::string& plannerSnp) {
    std::vector<STRRecord> lines(1);
    lines[0].matnr       = "M1";
    lines[0].locfrno     = locfrno;
    lines[0].planner_snp = plannerSnp;
    return lines;
}
} // namespace

TEST_CASE("a demand line with a blank PLANNER_SNP at a do-not-mix site raises blank_planner_at_do_not_mix_site") {
    ValidationReport r;
    Validator::validate_do_not_mix(doNotMixAt2027(), oneDemandLine("2027", ""), r);

    CHECK(r.blankPlannerAtDoNotMixSite == 1);
    REQUIRE(count_rule(r, "blank_planner_at_do_not_mix_site") == 1);
    CHECK(r.issues[0].severity == ValidationIssue::Severity::Warning);
    CHECK(r.issues[0].line_index == 0);
}

TEST_CASE("a whitespace-only demand PLANNER_SNP at a do-not-mix site also raises blank_planner_at_do_not_mix_site") {
    ValidationReport r;
    Validator::validate_do_not_mix(doNotMixAt2027(), oneDemandLine("2027", " "), r);

    CHECK(r.blankPlannerAtDoNotMixSite == 1);
}

TEST_CASE("a blank demand PLANNER_SNP at a site with no do-not-mix entry raises nothing") {
    ValidationReport r;
    Validator::validate_do_not_mix(doNotMixAt2027(), oneDemandLine("2299", ""), r);

    CHECK(r.blankPlannerAtDoNotMixSite == 0);
    CHECK(r.issues.empty());
}

TEST_CASE("a blank PLANNER_SNP at a do-not-mix site does not exclude the line from the totals") {
    ValidationReport r;
    Validator::validate_do_not_mix(doNotMixAt2027(), oneDemandLine("2027", ""), r);

    CHECK_FALSE(Validator::excludedLineFlags(r, 1)[0]);
}

TEST_CASE("a demand line loaded with a wrong-type PLANNER_SNP at a do-not-mix site is flagged") {
    const std::string path = "tests/validator/_tmp_str_planner_wrong_type.json";
    {
        std::ofstream out(path);
        out << R"({"REQUEST_ID":"X",)"
               R"("STR":[{"MATNR":"M1","LOCFRNO":"2027","PLANNER_SNP":20}],)"
               R"("DNM":[{"PLANNER_SNP":"S20","LOCFRNO":"2027"}]})";
    }
    const DemandFile demand = Importer::load_demand(path);
    std::remove(path.c_str());

    ValidationReport r;
    Validator::validate_do_not_mix(demand.dnm, demand.str, r);

    CHECK(r.blankPlannerAtDoNotMixSite == 1);
}

TEST_CASE("the supplied fixtures raise no blank_planner_at_do_not_mix_site") {
    const DemandFile demand = Importer::load_demand(DEMAND_PATH);

    ValidationReport r;
    Validator::validate_do_not_mix(demand.dnm, demand.str, r);

    CHECK(r.blankPlannerAtDoNotMixSite == 0);
}

TEST_CASE("a well-formed placeholder produces no validation issues") {
    std::vector<PlaceholderRecord> placeholders(1);
    placeholders[0].locfrno     = "2023";
    placeholders[0].loctono     = "2528";
    placeholders[0].ship_cond   = "TL";
    placeholders[0].no_of_loads = 5;

    ValidationReport r;
    Validator::validate_placeholders(placeholders, r);

    CHECK(r.errors == 0);
    CHECK(r.issues.empty());
}

TEST_CASE("large-line warning message rounds rather than truncates") {
    Fixture f;
    std::vector<double> pallets(f.join.lines.size(), 300.97);

    ValidationConfig cfg;
    cfg.pallet_warn_threshold = 300.0;

    ValidationReport r = Validator::validate(f.join, pallets, cfg);
    bool found = false;
    for (const auto& issue : r.issues) {
        if (issue.rule == "large_line") {
            found = true;
            // 300.97 is meaningfully over threshold; truncating to 300 would
            // hide that.
            CHECK(issue.message.find("301 pallets") != std::string::npos);
        }
    }
    CHECK(found);
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
