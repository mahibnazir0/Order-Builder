#include "doctest.h"
#include "../importer/crossDayFixtures.hpp"
#include "paramsLoader.hpp"
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

const M2Params& shippedParams() {
    static const M2Params params = loadParams(kStrictParamsPath);
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
    return unitLoadMetricsFor(line, shippedParams().pallets, deckHeight);
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

TEST_CASE("unitLoadMetrics: PAL and DIS quantities are already unit loads") {
    for (const string unitOfMeasure : {"PAL", "DIS"}) {
        CAPTURE(unitOfMeasure);
        const UnitLoadMetrics metrics = metricsFor(ptlProduct(), demand(3.0, unitOfMeasure));
        REQUIRE(metrics.error == UnitLoadMetricsError::None);
        CHECK(metrics.unitLoads == doctest::Approx(3.0));
    }
}

TEST_CASE("unitLoadMetrics: weight is per case times cases per unit load plus the pallet") {
    const double palletWeightLb = shippedPallet("PTL").addedWeightLb;
    const UnitLoadMetrics fromCases = metricsFor(ptlProduct(), demand(96.0, "CS"));
    const UnitLoadMetrics fromPallets = metricsFor(ptlProduct(), demand(2.0, "PAL"));
    CHECK(fromCases.weightLb == doctest::Approx(2.0 * (9.0 * 48 + palletWeightLb)));
    CHECK(fromPallets.weightLb == doctest::Approx(fromCases.weightLb));
}

TEST_CASE("unitLoadMetrics: a pallet type that adds no weight adds none") {
    const UnitLoadMetrics metrics = metricsFor(product("TLD", 10.0, 9.0, 12, 4, 48),
                                               demand(1.0, "PAL"));
    CHECK(metrics.weightLb == doctest::Approx(9.0 * 48 + shippedPallet("TLD").addedWeightLb));
}

TEST_CASE("unitLoadMetrics: height is case height times layers and stacked inches scale by unit loads") {
    const UnitLoadMetrics metrics = metricsFor(ptlProduct(), demand(1.25, "PAL"));
    CHECK(metrics.unitLoadHeightIn == doctest::Approx(40.0));
    CHECK(metrics.stackedInches == doctest::Approx(50.0));
}

TEST_CASE("unitLoadMetrics: the deck height is added only when the rule includes it") {
    const UnitLoadMetrics excluded = metricsFor(ptlProduct(), demand(1.0, "PAL"),
                                                DeckHeightRule::Excluded);
    const UnitLoadMetrics included = metricsFor(ptlProduct(), demand(1.0, "PAL"),
                                                DeckHeightRule::Included);
    CHECK(excluded.unitLoadHeightIn == doctest::Approx(40.0));
    CHECK(included.unitLoadHeightIn
          == doctest::Approx(40.0 + shippedPallet("PTL").addedHeightIn));
}

TEST_CASE("unitLoadMetrics: the shipped params exclude the deck height from the floor") {
    CHECK(shippedParams().floorDeckHeight == DeckHeightRule::Excluded);
}

TEST_CASE("unitLoadMetrics: a unit load exactly at the ceiling fits") {
    CHECK_FALSE(exceedsCeiling(metricsWithUnitLoadHeight(shippedTrailer().stackHeightCeilingIn),
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
                                               demand(1.0, "PAL"));
    CHECK(metrics.error == UnitLoadMetricsError::None);
    CHECK(metrics.weightLb == doctest::Approx(shippedPallet("TLD").addedWeightLb));
}

TEST_CASE("unitLoadMetrics: an unmatched line is missing its product") {
    const STRRecord demandRecord = demand(1.0, "PAL");
    JoinedLine line;
    line.str = &demandRecord;
    CHECK(unitLoadMetricsFor(line, shippedParams().pallets, DeckHeightRule::Excluded).error
          == UnitLoadMetricsError::MissingProduct);
}

TEST_CASE("unitLoadMetrics: a blank or unknown pallet type has no spec, never a zero weight") {
    for (const string palletId : {"", "XYZ", "PTL "}) {
        CAPTURE(palletId);
        CHECK(errorFor(product(palletId, 10.0, 9.0, 12, 4, 48), demand(1.0, "PAL"))
              == UnitLoadMetricsError::MissingPalletSpec);
    }
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
        CHECK(errorFor(product("PTL", 10.0, 9.0, 12, layersUnitLoad, 48), demand(1.0, "PAL"))
              == UnitLoadMetricsError::InvalidLayersPerUnitLoad);
    }
}

TEST_CASE("unitLoadMetrics: a zero, negative or non-finite case height is rejected") {
    for (const double caseHeightIn : {0.0, -10.0, kNaN, kInfinity}) {
        CAPTURE(caseHeightIn);
        CHECK(errorFor(product("PTL", caseHeightIn, 9.0, 12, 4, 48), demand(1.0, "PAL"))
              == UnitLoadMetricsError::InvalidCaseHeight);
    }
}

TEST_CASE("unitLoadMetrics: a negative or non-finite case weight is rejected") {
    for (const double caseWeightLb : {-9.0, kNaN, kInfinity, -kInfinity}) {
        CAPTURE(caseWeightLb);
        CHECK(errorFor(product("PTL", 10.0, caseWeightLb, 12, 4, 48), demand(1.0, "PAL"))
              == UnitLoadMetricsError::InvalidCaseWeight);
    }
}

TEST_CASE("unitLoadMetrics: a blank weight is caught even when every other field is populated") {
    CHECK(errorFor(product("PTL", 10.0, kNaN, 12, 4, 48), demand(48.0, "CS"))
          == UnitLoadMetricsError::InvalidCaseWeight);
}

TEST_CASE("unitLoadMetrics: finite inputs that overflow are rejected rather than summed") {
    const double hugeHeightIn = numeric_limits<double>::max();
    CHECK(errorFor(product("PTL", hugeHeightIn, 9.0, 12, 4, 48), demand(1.0, "PAL"))
          == UnitLoadMetricsError::NonFiniteResult);
}

TEST_CASE("unitLoadMetrics: every error has a distinct plain-ASCII name") {
    CHECK(string(unitLoadMetricsErrorName(UnitLoadMetricsError::InvalidCasesPerUnitLoad))
          == "invalid_cases_per_unit_load");
    CHECK(string(unitLoadMetricsErrorName(UnitLoadMetricsError::MissingPalletSpec))
          == "missing_pallet_spec");
}

TEST_CASE("unitLoadMetrics: reproduces the published Milestone 1 pallet-equivalents and weight on every extract" * doctest::skip(!crossDayTests::allExtractsPresent())) {
    for (size_t dayIndex = 0; dayIndex < kDayCount; ++dayIndex) {
        CAPTURE(dayFiles()[dayIndex].label);
        const PipelineResult& run = pipelineRuns()[dayIndex];
        const vector<bool> excluded =
            Validator::excludedLineFlags(run.validation, run.join.lines.size());
        double totalUnitLoads = 0.0;
        double totalWeightLb = 0.0;
        size_t rejectedLines = 0;
        for (size_t lineIndex = 0; lineIndex < run.join.lines.size(); ++lineIndex) {
            if (excluded[lineIndex]) continue;
            const UnitLoadMetrics metrics = unitLoadMetricsFor(
                run.join.lines[lineIndex], run.params.pallets, DeckHeightRule::Excluded);
            if (metrics.error != UnitLoadMetricsError::None) {
                ++rejectedLines;
                continue;
            }
            totalUnitLoads += metrics.unitLoads;
            totalWeightLb += metrics.weightLb;
        }
        CHECK(rejectedLines == 0);
        CHECK(fabs(totalUnitLoads - expectedM1::palletEquivalents[dayIndex]) <= 0.05);
        CHECK(fabs(totalWeightLb - expectedM1::totalWeightLb[dayIndex]) <= 0.5);
    }
}
