#include "doctest.h"
#include "importer.hpp"
#include "paramsLoader.hpp"
#include "product_importer.hpp"
#include "stackRules.hpp"

#include <cmath>
#include <limits>
#include <unordered_set>

using namespace ob;
using Reason = StackFeasibility::Reason;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();
constexpr double kCeilingIn = 108.0;

M2Params testParams() {
    M2Params params;
    params.cri.safeLimitLb = {0, 5, 299, 549, 799, 1149, 1499, 1849, 2199, 3099, 3599};
    params.pallets = {{"PTL", 60.0, 5.5, 48.0, 40.0}, {"TLD", 0.0, 0.0, 48.0, 40.0}};
    return params;
}

ProductRecord product() {
    ProductRecord record;
    record.id = "P1";
    record.height_in = 10.0;
    record.weight_lb = 9.0;
    record.strength = 5;
    record.cases_layer = 4;
    record.layers_unit_load = 3;
    record.cases_unit_load = 12;
    record.pallet_id = "PTL";
    return record;
}

JoinedLine matchedLine(const ProductRecord& record) {
    JoinedLine line;
    line.product = &record;
    line.matched = true;
    return line;
}

UnitLoad load(double heightIn, double weightLb, double weightAboveLb, int cri) {
    UnitLoad unitLoad;
    unitLoad.id = "L";
    unitLoad.footprintLengthIn = 48.0;
    unitLoad.footprintWidthIn = 40.0;
    unitLoad.heightIn = heightIn;
    unitLoad.weightLb = weightLb;
    unitLoad.ownWeightAboveLb = weightAboveLb;
    unitLoad.cri = cri;
    return unitLoad;
}

UnitLoadError buildError(const ProductRecord& record) {
    return buildUnitLoad(matchedLine(record), testParams()).error;
}

} // namespace

TEST_CASE("stackRules: unit load uses cases per unit and the pallet spec, not case footprint") {
    const ProductRecord record = product();
    const UnitLoad unitLoad = buildUnitLoad(matchedLine(record), testParams());
    REQUIRE(unitLoad.error == UnitLoadError::None);
    CHECK(unitLoad.heightIn == doctest::Approx(10.0 * 3 + 5.5));
    CHECK(unitLoad.weightLb == doctest::Approx(9.0 * 12 + 60.0));
    CHECK(unitLoad.ownWeightAboveLb == doctest::Approx((3 - 1) * 4 * 9.0));
    CHECK(unitLoad.footprintLengthIn == 48.0);
    CHECK(unitLoad.footprintWidthIn == 40.0);
    CHECK(unitLoad.cri == 5);
}

TEST_CASE("stackRules: a single-layer unit load carries no weight above itself") {
    ProductRecord record = product();
    record.layers_unit_load = 1;
    CHECK(buildUnitLoad(matchedLine(record), testParams()).ownWeightAboveLb == 0.0);
}

TEST_CASE("stackRules: a supplied weight above wins over derivation, including zero") {
    const ProductRecord record = product();
    CHECK(buildUnitLoad(matchedLine(record), testParams(), 250.0).ownWeightAboveLb == 250.0);
    CHECK(buildUnitLoad(matchedLine(record), testParams(), 0.0).ownWeightAboveLb == 0.0);
}

TEST_CASE("stackRules: an invalid supplied weight above is rejected, never replaced by derivation") {
    const ProductRecord record = product();
    for (double supplied : {-1.0, kNaN, kInf}) {
        CHECK(buildUnitLoad(matchedLine(record), testParams(), supplied).error
              == UnitLoadError::InvalidData);
    }
}

TEST_CASE("stackRules: an unmatched line has no unit load") {
    JoinedLine line;
    CHECK(buildUnitLoad(line, testParams()).error == UnitLoadError::MissingProduct);
}

TEST_CASE("stackRules: a pallet id without a spec is reported, not guessed") {
    ProductRecord record = product();
    record.pallet_id = "PTL ";
    CHECK(buildError(record) == UnitLoadError::MissingPalletSpec);
}

TEST_CASE("stackRules: zero, negative and non-finite case measurements fail construction") {
    for (double bad : {0.0, -1.0, kNaN, kInf}) {
        ProductRecord heightBad = product();
        heightBad.height_in = bad;
        CHECK(buildError(heightBad) == UnitLoadError::InvalidData);
        ProductRecord weightBad = product();
        weightBad.weight_lb = bad;
        CHECK(buildError(weightBad) == UnitLoadError::InvalidData);
    }
}

TEST_CASE("stackRules: zero and negative counts fail construction") {
    for (int bad : {0, -1}) {
        ProductRecord layers = product();
        layers.layers_unit_load = bad;
        CHECK(buildError(layers) == UnitLoadError::InvalidData);
        ProductRecord casesPerLayer = product();
        casesPerLayer.cases_layer = bad;
        CHECK(buildError(casesPerLayer) == UnitLoadError::InvalidData);
        ProductRecord casesPerUnit = product();
        casesPerUnit.cases_unit_load = bad;
        CHECK(buildError(casesPerUnit) == UnitLoadError::InvalidData);
    }
}

TEST_CASE("stackRules: strength outside 0..10 fails construction and blank is kept as 0") {
    for (int bad : {-1, 11}) {
        ProductRecord record = product();
        record.strength = bad;
        CHECK(buildError(record) == UnitLoadError::InvalidData);
    }
    ProductRecord blank = product();
    blank.strength = 0;
    CHECK(buildUnitLoad(matchedLine(blank), testParams()).cri == 0);
}

TEST_CASE("stackRules: a pallet spec with a bad footprint fails construction") {
    M2Params params = testParams();
    params.pallets[0].footprintLengthIn = kNaN;
    const ProductRecord record = product();
    CHECK(buildUnitLoad(matchedLine(record), params).error == UnitLoadError::InvalidData);
}

TEST_CASE("stackRules: missing pallet ids are exact, distinct, sorted and collected together") {
    ProductRecord good = product();
    ProductRecord spaced = product();
    spaced.pallet_id = "PTL ";
    ProductRecord unknown = product();
    unknown.pallet_id = "ABC";
    const std::vector<JoinedLine> lines{matchedLine(good), matchedLine(spaced),
                                        matchedLine(unknown), matchedLine(spaced), JoinedLine{}};
    const std::vector<std::string> expected{"ABC", "PTL "};
    CHECK(missingPalletIds(lines, testParams()) == expected);
}

TEST_CASE("stackRules: height exactly at the ceiling passes and just over it fails") {
    const M2Params params = testParams();
    CHECK(canStack(load(54, 10, 0, 5), load(54, 10, 0, 5), params, kCeilingIn).isFeasible);
    const auto over = canStack(load(54, 10, 0, 5), load(54.5, 10, 0, 5), params, kCeilingIn);
    CHECK_FALSE(over.isFeasible);
    CHECK(over.reason == Reason::HeightCeiling);
    CHECK(over.marginInOrLb == doctest::Approx(0.5));
}

TEST_CASE("stackRules: a feasible stack reports its height headroom") {
    const auto result = canStack(load(40, 10, 0, 5), load(30, 10, 0, 5), testParams(), kCeilingIn);
    CHECK(result.isFeasible);
    CHECK(result.reason == Reason::Ok);
    CHECK(result.marginInOrLb == doctest::Approx(38.0));
}

TEST_CASE("stackRules: height is judged before blank CRI and CRI weight") {
    const auto result = canStack(load(100, 1e9, 1e9, 0), load(100, 1e9, 0, 5), testParams(), kCeilingIn);
    CHECK(result.reason == Reason::HeightCeiling);
}

TEST_CASE("stackRules: a blank-CRI base is not stackable by default") {
    const auto result = canStack(load(20, 10, 0, 0), load(20, 10, 0, 5), testParams(), kCeilingIn);
    CHECK_FALSE(result.isFeasible);
    CHECK(result.reason == Reason::BlankCri);
}

TEST_CASE("stackRules: a blank-CRI base stacks when the parameter allows it") {
    M2Params params = testParams();
    params.blankCriIsStackable = true;
    CHECK(canStack(load(20, 10, 0, 0), load(20, 10, 0, 5), params, kCeilingIn).isFeasible);
}

TEST_CASE("stackRules: CRI weight limit is inclusive and the weight above includes the top") {
    const M2Params params = testParams();
    const double limitLb = params.cri.safeLimitLb[5];
    CHECK(canStack(load(20, 10, 100, 5), load(20, limitLb - 100, 0, 5), params, kCeilingIn).isFeasible);
    const auto over = canStack(load(20, 10, 100, 5), load(20, limitLb - 99, 0, 5), params, kCeilingIn);
    CHECK(over.reason == Reason::CriExceeded);
    CHECK(over.marginInOrLb == doctest::Approx(1.0));
}

TEST_CASE("stackRules: stacking is directional") {
    const M2Params params = testParams();
    const UnitLoad strongLight = load(20, 50, 0, 8);
    const UnitLoad weakHeavy = load(20, 500, 0, 1);
    CHECK(canStack(strongLight, weakHeavy, params, kCeilingIn).isFeasible);
    CHECK(canStack(weakHeavy, strongLight, params, kCeilingIn).reason == Reason::CriExceeded);
}

TEST_CASE("stackRules: a unit load taller than the ceiling cannot be stacked on or under") {
    const M2Params params = testParams();
    const UnitLoad tall = load(kCeilingIn + 1, 10, 0, 5);
    CHECK(canStack(tall, load(1, 10, 0, 5), params, kCeilingIn).reason == Reason::HeightCeiling);
    CHECK(canStack(load(1, 10, 0, 5), tall, params, kCeilingIn).reason == Reason::HeightCeiling);
}

TEST_CASE("stackRules: canStack rejects zero, negative and non-finite arithmetic inputs") {
    const M2Params params = testParams();
    for (double bad : {0.0, -1.0, kNaN, kInf}) {
        CHECK(canStack(load(bad, 10, 0, 5), load(20, 10, 0, 5), params, kCeilingIn).reason
              == Reason::InvalidData);
        CHECK(canStack(load(20, 10, 0, 5), load(20, bad, 0, 5), params, kCeilingIn).reason
              == Reason::InvalidData);
        CHECK(canStack(load(20, 10, 0, 5), load(20, 10, 0, 5), params, bad).reason
              == Reason::InvalidData);
    }
    for (double bad : {-1.0, kNaN, kInf}) {
        CHECK(canStack(load(20, 10, bad, 5), load(20, 10, 0, 5), params, kCeilingIn).reason
              == Reason::InvalidData);
    }
}

TEST_CASE("stackRules: canStack rejects out-of-range CRI and loads that failed construction") {
    const M2Params params = testParams();
    for (int badCri : {-1, 11}) {
        CHECK(canStack(load(20, 10, 0, badCri), load(20, 10, 0, 5), params, kCeilingIn).reason
              == Reason::InvalidData);
    }
    UnitLoad failed = load(20, 10, 0, 5);
    failed.error = UnitLoadError::MissingPalletSpec;
    CHECK(canStack(failed, load(20, 10, 0, 5), params, kCeilingIn).reason == Reason::InvalidData);
}

TEST_CASE("stackRules: real demand pair distribution at the confirmed 108 in ceiling") {
    const M2Params params = loadParams("config/orderBuilderParams.json");
    const DemandFile file = Importer::load_demand("tests/importer/Demand-1.json");
    const auto products = ProductImporter::load("tests/importer/Customer2-Product-Data.csv");
    const ProductIndex index = Joiner::build_index(products.products);
    const auto join = Joiner::join(file.str, index);

    CHECK(missingPalletIds(join.lines, params).empty());

    std::unordered_set<std::string> seenProducts;
    std::vector<UnitLoad> loads;
    for (const auto& line : join.lines) {
        if (!seenProducts.insert(line.product->id).second) continue;
        loads.push_back(buildUnitLoad(line, params));
    }

    std::size_t pairs = 0, passHeight = 0, passBoth = 0;
    for (const auto& base : loads) {
        for (const auto& top : loads) {
            ++pairs;
            const auto result = canStack(base, top, params, kCeilingIn);
            if (result.reason != Reason::HeightCeiling) ++passHeight;
            if (result.isFeasible) ++passBoth;
        }
    }
    CHECK(pairs == 2016400);
    CHECK(passHeight == 65295);
    CHECK(passBoth == 43021);
}
