#include "doctest.h"
#include "../importer/crossDayFixtures.hpp"
#include "paramsLoader.hpp"
#include "stackRules.hpp"
#include "unitLoadMetrics.hpp"
#include "validator.hpp"

#include <cmath>
#include <limits>
#include <string>

using namespace std;
using namespace ob;
using namespace crossDayTests;

namespace {

const double kNaN = numeric_limits<double>::quiet_NaN();
const double kInfinity = numeric_limits<double>::infinity();

// The shipped params carry no pallets, so the confirmed table stands in for the pallet table.
const M2Params& shippedParams() {
    static const M2Params params = [] {
        M2Params loaded = loadParams(kStrictParamsPath);
        loaded.pallets = confirmedPalletSpecs();
        return loaded;
    }();
    return params;
}

const TrailerSpec& shippedTrailer() {
    return shippedParams().trailers.front();
}

const PalletSpec& shippedPallet(const string& palletId) {
    const PalletSpec* pallet = palletSpecFor(shippedParams().pallets, palletId);
    REQUIRE(pallet != nullptr);
    return *pallet;
}

ProductRecord product(const string& palletId, double caseHeightIn, double caseWeightLb,
                      int casesLayer, int layersUnitLoad, int casesUnitLoad) {
    ProductRecord record;
    record.id = "P";
    record.pallet_id = palletId;
    record.height_in = caseHeightIn;
    record.weight_lb = caseWeightLb;
    record.cases_layer = casesLayer;
    record.layers_unit_load = layersUnitLoad;
    record.cases_unit_load = casesUnitLoad;
    return record;
}

ProductRecord ptlProduct() {
    return product("PTL", 10.0, 9.0, 12, 4, 48);
}

STRRecord demand(double quantity, const string& unitOfMeasure) {
    STRRecord record;
    record.matnr = "P";
    record.trans = quantity;
    record.unitofmeas = unitOfMeasure;
    return record;
}

UnitLoadMetrics metricsFor(const ProductRecord& productRecord, const STRRecord& demandRecord,
                           DeckHeightRule deckHeight = DeckHeightRule::Excluded) {
    JoinedLine line;
    line.str = &demandRecord;
    line.product = &productRecord;
    line.matched = true;
    M2Params params = shippedParams();
    params.floorDeckHeight = deckHeight;
    return unitLoadMetricsFor(line, params);
}

UnitLoadMetricsError errorFor(const ProductRecord& productRecord, const STRRecord& demandRecord) {
    return metricsFor(productRecord, demandRecord).error;
}

UnitLoadMetrics metricsWithUnitLoadHeight(double unitLoadHeightIn) {
    return metricsFor(product("TLD", unitLoadHeightIn, 9.0, 1, 1, 1), demand(1.0, "PAL"));
}

} // namespace

TEST_CASE("unitLoadMetrics: cases divide by cases per unit load") {
    const UnitLoadMetrics metrics = metricsFor(ptlProduct(), demand(96.0, "CS"));
    REQUIRE(metrics.error == UnitLoadMetricsError::None);
    CHECK(metrics.unitLoads == doctest::Approx(2.0));
}

TEST_CASE("unitLoadMetrics: a PAL quantity counts the product's base unit, divided by Cases_Unit_Load") {
    const UnitLoadMetrics metrics = metricsFor(ptlProduct(), demand(96.0, "PAL"));
    REQUIRE(metrics.error == UnitLoadMetricsError::None);
    CHECK(metrics.unitLoads == doctest::Approx(2.0));
}

TEST_CASE("unitLoadMetrics: 60 PAL of a two-per-unit-load product is 30 unit loads, not 60") {
    // 104578600: one PAL is a 48 x 40 x 49.375 in piece of 421.4 lb, two stack into a unit load.
    const UnitLoadMetrics metrics = metricsFor(product("TLD", 49.375, 421.4, 1, 2, 2),
                                               demand(60.0, "PAL"));
    REQUIRE(metrics.error == UnitLoadMetricsError::None);
    CHECK(metrics.unitLoads == doctest::Approx(30.0));
    CHECK(metrics.weightLb == doctest::Approx(30.0 * (2 * 421.4 + shippedPallet("TLD").addedWeightLb)));
}

TEST_CASE("unitLoadMetrics: a DIS quantity is cases") {
    const UnitLoadMetrics metrics = metricsFor(ptlProduct(), demand(24.0, "DIS"));
    REQUIRE(metrics.error == UnitLoadMetricsError::None);
    CHECK(metrics.unitLoads == doctest::Approx(24.0 / 48.0));
}

TEST_CASE("unitLoadMetrics: weight is per case times cases per unit load plus the pallet") {
    const double palletWeightLb = shippedPallet("PTL").addedWeightLb;
    const UnitLoadMetrics fromCases = metricsFor(ptlProduct(), demand(96.0, "CS"));
    const UnitLoadMetrics fromPallets = metricsFor(ptlProduct(), demand(96.0, "PAL"));
    CHECK(fromCases.weightLb == doctest::Approx(2.0 * (9.0 * 48 + palletWeightLb)));
    CHECK(fromPallets.weightLb == doctest::Approx(fromCases.weightLb));
}

TEST_CASE("unitLoadMetrics: a pallet type that adds no weight adds none") {
    const UnitLoadMetrics metrics = metricsFor(product("TLD", 10.0, 9.0, 12, 4, 48),
                                               demand(1.0 * 48.0, "CS"));
    CHECK(metrics.weightLb == doctest::Approx(9.0 * 48 + shippedPallet("TLD").addedWeightLb));
}

TEST_CASE("unitLoadMetrics: height is case height times layers and stacked inches scale by unit loads") {
    const UnitLoadMetrics metrics = metricsFor(ptlProduct(), demand(1.25 * 48.0, "CS"));
    CHECK(metrics.unitLoadHeightIn == doctest::Approx(40.0));
    CHECK(metrics.stackedInches == doctest::Approx(50.0));
}

TEST_CASE("unitLoadMetrics: the deck height is added only when the rule includes it") {
    const UnitLoadMetrics excluded = metricsFor(ptlProduct(), demand(1.0 * 48.0, "CS"),
                                                DeckHeightRule::Excluded);
    const UnitLoadMetrics included = metricsFor(ptlProduct(), demand(1.0 * 48.0, "CS"),
                                                DeckHeightRule::Included);
    CHECK(excluded.unitLoadHeightIn == doctest::Approx(40.0));
    CHECK(included.unitLoadHeightIn
          == doctest::Approx(40.0 + shippedPallet("PTL").addedHeightIn));
}

TEST_CASE("unitLoadMetrics: the shipped params count the deck height in the floor") {
    CHECK(shippedParams().floorDeckHeight == DeckHeightRule::Included);
}

TEST_CASE("unitLoadMetrics: a unit load exactly at the ceiling fits") {
    CHECK_FALSE(exceedsCeiling(metricsWithUnitLoadHeight(shippedTrailer().stackHeightCeilingIn),
                               shippedTrailer()));
}

TEST_CASE("unitLoadMetrics: a unit load one ULP over the ceiling fits") {
    const double ceilingIn = shippedTrailer().stackHeightCeilingIn;
    CHECK_FALSE(exceedsCeiling(metricsWithUnitLoadHeight(nextafter(ceilingIn, kInfinity)),
                               shippedTrailer()));
}

TEST_CASE("unitLoadMetrics: a case height that computes a hair over the ceiling fits") {
    const double ceilingIn = shippedTrailer().stackHeightCeilingIn;
    const ProductRecord sevenths = product("TLD", ceilingIn / 21.0, 9.0, 1, 21, 21);
    const UnitLoadMetrics metrics =
        metricsFor(sevenths, demand(21.0, "CS"), DeckHeightRule::Excluded);
    REQUIRE(metrics.unitLoadHeightIn > ceilingIn);
    CHECK_FALSE(exceedsCeiling(metrics, shippedTrailer()));
}

TEST_CASE("unitLoadMetrics: a unit load 1e-6 in over the ceiling does not fit") {
    CHECK(exceedsCeiling(metricsWithUnitLoadHeight(shippedTrailer().stackHeightCeilingIn + 1e-6),
                         shippedTrailer()));
}

TEST_CASE("unitLoadMetrics: a unit load just over the ceiling does not fit") {
    CHECK(exceedsCeiling(metricsWithUnitLoadHeight(shippedTrailer().stackHeightCeilingIn + 0.01),
                         shippedTrailer()));
}

TEST_CASE("unitLoadMetrics: whether a load under the ceiling fits depends on the deck rule") {
    const double deckHeightIn = shippedPallet("PTL").addedHeightIn;
    REQUIRE(deckHeightIn > 0.0);
    const double caseHeightIn = shippedTrailer().stackHeightCeilingIn - deckHeightIn / 2.0;
    const ProductRecord tallPtl = product("PTL", caseHeightIn, 9.0, 1, 1, 1);
    CHECK_FALSE(exceedsCeiling(metricsFor(tallPtl, demand(1.0, "PAL"), DeckHeightRule::Excluded),
                               shippedTrailer()));
    CHECK(exceedsCeiling(metricsFor(tallPtl, demand(1.0, "PAL"), DeckHeightRule::Included),
                         shippedTrailer()));
}

TEST_CASE("unitLoadMetrics: cases per unit load disagreeing with layers is flagged, and each field keeps its role") {
    const UnitLoadMetrics metrics = metricsFor(product("PTL", 10.0, 9.0, 37, 2, 82),
                                               demand(164.0, "CS"));
    REQUIRE(metrics.error == UnitLoadMetricsError::None);
    CHECK(metrics.casesPerUnitLoadMismatch);
    CHECK(metrics.unitLoads == doctest::Approx(2.0));
    CHECK(metrics.unitLoadHeightIn == doctest::Approx(20.0));
}

TEST_CASE("unitLoadMetrics: consistent cases per unit load is not flagged") {
    CHECK_FALSE(metricsFor(ptlProduct(), demand(48.0, "CS")).casesPerUnitLoadMismatch);
}

TEST_CASE("unitLoadMetrics: a zero quantity gives zero metrics, not an error") {
    const UnitLoadMetrics metrics = metricsFor(ptlProduct(), demand(0.0, "CS"));
    CHECK(metrics.error == UnitLoadMetricsError::None);
    CHECK(metrics.unitLoads == 0.0);
    CHECK(metrics.weightLb == 0.0);
    CHECK(metrics.stackedInches == 0.0);
}

TEST_CASE("unitLoadMetrics: a zero case weight is a value, not a gap") {
    const UnitLoadMetrics metrics = metricsFor(product("TLD", 10.0, 0.0, 12, 4, 48),
                                               demand(1.0 * 48.0, "CS"));
    CHECK(metrics.error == UnitLoadMetricsError::None);
    CHECK(metrics.weightLb == doctest::Approx(shippedPallet("TLD").addedWeightLb));
}

TEST_CASE("unitLoadMetrics: an unmatched line is missing its product") {
    const STRRecord demandRecord = demand(1.0 * 48.0, "CS");
    JoinedLine line;
    line.str = &demandRecord;
    CHECK(unitLoadMetricsFor(line, shippedParams()).error
          == UnitLoadMetricsError::MissingProduct);
}

TEST_CASE("unitLoadMetrics: a blank or unknown pallet type has no spec, never a zero weight") {
    for (const string palletId : {"", "XYZ", "PTL "}) {
        CAPTURE(palletId);
        CHECK(errorFor(product(palletId, 10.0, 9.0, 12, 4, 48), demand(1.0 * 48.0, "CS"))
              == UnitLoadMetricsError::MissingPalletSpec);
    }
}

TEST_CASE("unitLoadMetrics: a product row's own pallet figures win over the pallet table's") {
    ProductRecord record = ptlProduct();
    record.palletWeightLb = 100.0;
    record.palletHeightIn = 7.0;
    const UnitLoadMetrics metrics = metricsFor(record, demand(1.0 * 48.0, "CS"), DeckHeightRule::Included);
    REQUIRE(metrics.error == UnitLoadMetricsError::None);
    CHECK(metrics.weightLb == doctest::Approx(9.0 * 48 + 100.0));
    CHECK(metrics.unitLoadHeightIn == doctest::Approx(40.0 + 7.0));
}

TEST_CASE("unitLoadMetrics: a product row's pallet figures stand in for a pallet type the table lacks") {
    ProductRecord record = product("XYZ", 10.0, 9.0, 12, 4, 48);
    record.palletWeightLb = 50.0;
    record.palletHeightIn = 5.0;
    record.palletFootprintLengthIn = 48.0;
    record.palletFootprintWidthIn = 40.0;
    const UnitLoadMetrics metrics = metricsFor(record, demand(1.0 * 48.0, "CS"));
    REQUIRE(metrics.error == UnitLoadMetricsError::None);
    CHECK(metrics.weightLb == doctest::Approx(9.0 * 48 + 50.0));
}

TEST_CASE("unitLoadMetrics: a negative or non-finite pallet weight is rejected") {
    for (const double palletWeightLb : {-60.0, kNaN, kInfinity}) {
        CAPTURE(palletWeightLb);
        ProductRecord record = ptlProduct();
        record.palletWeightLb = palletWeightLb;
        CHECK(errorFor(record, demand(1.0 * 48.0, "CS")) == UnitLoadMetricsError::InvalidPalletSpec);
    }
}

TEST_CASE("unitLoadMetrics: a negative or non-finite deck height is rejected under either deck rule") {
    for (const double palletHeightIn : {-5.5, kNaN, kInfinity}) {
        for (const DeckHeightRule deckHeight : {DeckHeightRule::Excluded, DeckHeightRule::Included}) {
            CAPTURE(palletHeightIn);
            ProductRecord record = ptlProduct();
            record.palletHeightIn = palletHeightIn;
            CHECK(metricsFor(record, demand(1.0 * 48.0, "CS"), deckHeight).error
                  == UnitLoadMetricsError::InvalidPalletSpec);
        }
    }
}

TEST_CASE("unitLoadMetrics: a zero, negative or non-finite pallet footprint side is rejected") {
    for (const double side : {0.0, -48.0, kNaN, kInfinity}) {
        CAPTURE(side);
        ProductRecord longSide = ptlProduct();
        longSide.palletFootprintLengthIn = side;
        CHECK(errorFor(longSide, demand(1.0 * 48.0, "CS")) == UnitLoadMetricsError::InvalidPalletSpec);
        ProductRecord shortSide = ptlProduct();
        shortSide.palletFootprintWidthIn = side;
        CHECK(errorFor(shortSide, demand(1.0 * 48.0, "CS")) == UnitLoadMetricsError::InvalidPalletSpec);
    }
}

TEST_CASE("unitLoadMetrics: a unit load covers its pallet's footprint and fills footprint x height") {
    const PalletSpec pallet = shippedPallet("PTL");
    const double footprintSqIn = pallet.footprintLengthIn * pallet.footprintWidthIn;
    const UnitLoadMetrics metrics = metricsFor(ptlProduct(), demand(2.0 * 48.0, "CS"));
    CHECK(metrics.floorAreaSqIn == doctest::Approx(2.0 * footprintSqIn));
    CHECK(metrics.cargoCubicIn == doctest::Approx(2.0 * footprintSqIn * 40.0));
}

TEST_CASE("unitLoadMetrics: the product row's footprint wins over the pallet table") {
    ProductRecord record = ptlProduct();
    record.palletFootprintLengthIn = 40.0;
    record.palletFootprintWidthIn = 30.0;
    CHECK(metricsFor(record, demand(1.0 * 48.0, "CS")).floorAreaSqIn == doctest::Approx(1200.0));
}

TEST_CASE("unitLoadMetrics: a zero, negative or non-finite layer side is rejected") {
    for (const double side : {0.0, -47.0, kNaN, kInfinity}) {
        CAPTURE(side);
        ProductRecord longSide = ptlProduct();
        longSide.layerLengthIn = side;
        longSide.layerWidthIn = 40.0;
        CHECK(errorFor(longSide, demand(1.0 * 48.0, "CS")) == UnitLoadMetricsError::InvalidLayerSize);
        ProductRecord shortSide = ptlProduct();
        shortSide.layerLengthIn = 48.0;
        shortSide.layerWidthIn = side;
        CHECK(errorFor(shortSide, demand(1.0 * 48.0, "CS")) == UnitLoadMetricsError::InvalidLayerSize);
    }
}

TEST_CASE("unitLoadMetrics: a TLD load covers its layer, not the pallet table's footprint") {
    ProductRecord record = product("TLD", 10.0, 9.0, 12, 4, 48);
    record.layerLengthIn = 47.0;
    record.layerWidthIn = 36.0;
    const UnitLoadMetrics metrics = metricsFor(record, demand(2.0 * 48.0, "CS"));
    CHECK(metrics.floorAreaSqIn == doctest::Approx(2.0 * 47.0 * 36.0));
    CHECK_FALSE(metrics.footprintFromPalletOnly);
}

TEST_CASE("unitLoadMetrics: a product with no layer size is flagged as covering its pallet's footprint") {
    CHECK(metricsFor(ptlProduct(), demand(1.0 * 48.0, "CS")).footprintFromPalletOnly);
}

TEST_CASE("unitLoadMetrics: a unit of measure other than CS, PAL or DIS is rejected") {
    for (const string unitOfMeasure : {"", "EA", "ROL", "cs"}) {
        CAPTURE(unitOfMeasure);
        CHECK(errorFor(ptlProduct(), demand(1.0, unitOfMeasure))
              == UnitLoadMetricsError::UnconvertibleUom);
    }
}

TEST_CASE("unitLoadMetrics: a negative or non-finite quantity is rejected") {
    for (const double quantity : {-1.0, kNaN, kInfinity, -kInfinity}) {
        CAPTURE(quantity);
        CHECK(errorFor(ptlProduct(), demand(quantity, "CS")) == UnitLoadMetricsError::InvalidQuantity);
    }
}

TEST_CASE("unitLoadMetrics: zero or negative cases per unit load is rejected for every unit of measure") {
    for (const int casesUnitLoad : {0, -48}) {
        for (const string unitOfMeasure : {"CS", "PAL", "DIS"}) {
            CAPTURE(casesUnitLoad);
            CAPTURE(unitOfMeasure);
            CHECK(errorFor(product("PTL", 10.0, 9.0, 12, 4, casesUnitLoad), demand(1.0, unitOfMeasure))
                  == UnitLoadMetricsError::InvalidCasesPerUnitLoad);
        }
    }
}

TEST_CASE("unitLoadMetrics: zero or negative layers per unit load is rejected") {
    for (const int layersUnitLoad : {0, -4}) {
        CAPTURE(layersUnitLoad);
        CHECK(errorFor(product("PTL", 10.0, 9.0, 12, layersUnitLoad, 48), demand(1.0 * 48.0, "CS"))
              == UnitLoadMetricsError::InvalidLayersPerUnitLoad);
    }
}

TEST_CASE("unitLoadMetrics: a zero, negative or non-finite case height is rejected") {
    for (const double caseHeightIn : {0.0, -10.0, kNaN, kInfinity}) {
        CAPTURE(caseHeightIn);
        CHECK(errorFor(product("PTL", caseHeightIn, 9.0, 12, 4, 48), demand(1.0 * 48.0, "CS"))
              == UnitLoadMetricsError::InvalidCaseHeight);
    }
}

TEST_CASE("unitLoadMetrics: a negative or non-finite case weight is rejected") {
    for (const double caseWeightLb : {-9.0, kNaN, kInfinity, -kInfinity}) {
        CAPTURE(caseWeightLb);
        CHECK(errorFor(product("PTL", 10.0, caseWeightLb, 12, 4, 48), demand(1.0 * 48.0, "CS"))
              == UnitLoadMetricsError::InvalidCaseWeight);
    }
}

TEST_CASE("unitLoadMetrics: a blank weight is caught even when every other field is populated") {
    CHECK(errorFor(product("PTL", 10.0, kNaN, 12, 4, 48), demand(48.0, "CS"))
          == UnitLoadMetricsError::InvalidCaseWeight);
}

TEST_CASE("unitLoadMetrics: finite inputs that overflow are rejected rather than summed") {
    const double hugeHeightIn = numeric_limits<double>::max();
    CHECK(errorFor(product("PTL", hugeHeightIn, 9.0, 12, 4, 48), demand(1.0 * 48.0, "CS"))
          == UnitLoadMetricsError::NonFiniteResult);
}

TEST_CASE("unitLoadMetrics: every error has a distinct plain-ASCII name") {
    CHECK(string(unitLoadMetricsErrorName(UnitLoadMetricsError::InvalidCasesPerUnitLoad))
          == "invalid_cases_per_unit_load");
    CHECK(string(unitLoadMetricsErrorName(UnitLoadMetricsError::MissingPalletSpec))
          == "missing_pallet_spec");
    CHECK(string(unitLoadMetricsErrorName(UnitLoadMetricsError::InvalidPalletSpec))
          == "invalid_pallet_spec");
    CHECK(string(unitLoadMetricsErrorName(UnitLoadMetricsError::InvalidLayerSize))
          == "invalid_layer_size");
}

TEST_CASE("unitLoadMetrics: reproduces the M1 pallet-equivalents and the M2 stacking weight on every extract" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const PipelineResult& run = pipelineRuns()[dayIndex];
        const vector<bool> excluded =
            Validator::excludedLineFlags(run.validation, run.join.lines.size());
        double totalUnitLoads = 0.0;
        double totalWeightLb = 0.0;
        double stackingWeightLb = 0.0;
        size_t rejectedLines = 0;
        for (size_t lineIndex = 0; lineIndex < run.join.lines.size(); ++lineIndex) {
            if (excluded[lineIndex]) continue;
            const UnitLoadMetrics metrics = unitLoadMetricsFor(run.join.lines[lineIndex], run.params);
            if (metrics.error != UnitLoadMetricsError::None) {
                ++rejectedLines;
                continue;
            }
            totalUnitLoads += metrics.unitLoads;
            totalWeightLb += metrics.weightLb;
            stackingWeightLb += run.weightForStacking[lineIndex];
        }
        CHECK(rejectedLines == 0);
        CHECK(fabs(totalUnitLoads - expectedM1::palletEquivalents[dayIndex]) <= 0.05);
        // M1 keeps the confirmed pallet weights; the floor weighs pallets as the stacks do.
        CHECK(fabs(totalWeightLb - stackingWeightLb) <= 0.5);
    }
}

TEST_CASE("unitLoadMetrics: a 106.25 in PTL unit load has the same height in Milestone 2 under either deck rule") {
    const ProductRecord tallPtl = product("PTL", 106.25, 9.0, 1, 1, 1);
    const STRRecord oneUnitLoad = demand(1.0, "PAL");
    JoinedLine line;
    line.str = &oneUnitLoad;
    line.product = &tallPtl;
    line.matched = true;
    for (const DeckHeightRule deckHeight : {DeckHeightRule::Excluded, DeckHeightRule::Included}) {
        M2Params params = shippedParams();
        params.floorDeckHeight = deckHeight;
        const UnitLoad stackingLoad = buildUnitLoad(line, params);
        const UnitLoadMetrics floorMetrics = unitLoadMetricsFor(line, params);
        REQUIRE(stackingLoad.error == UnitLoadError::None);
        REQUIRE(floorMetrics.error == UnitLoadMetricsError::None);
        CHECK(stackingLoad.heightIn == floorMetrics.unitLoadHeightIn);
    }
}

TEST_CASE("unitLoadMetrics: every extract line has the same unit-load height in Milestone 2 under either deck rule" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const PipelineResult& run = pipelineRuns()[dayIndex];
        for (const DeckHeightRule deckHeight : {DeckHeightRule::Excluded, DeckHeightRule::Included}) {
            M2Params params = run.params;
            params.floorDeckHeight = deckHeight;
            size_t disagreeingLines = 0;
            for (const JoinedLine& line : run.join.lines) {
                const UnitLoad stackingLoad = buildUnitLoad(line, params);
                const UnitLoadMetrics floorMetrics = unitLoadMetricsFor(line, params);
                if (stackingLoad.error != UnitLoadError::None
                    || floorMetrics.error != UnitLoadMetricsError::None) {
                    continue;
                }
                if (stackingLoad.heightIn != floorMetrics.unitLoadHeightIn) ++disagreeingLines;
            }
            CHECK(disagreeingLines == 0);
        }
    }
}
