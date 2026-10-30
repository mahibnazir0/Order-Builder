#include "doctest.h"
#include "floorBound.hpp"
#include "paramsLoader.hpp"

#include <limits>
#include <stdexcept>
#include <string>

using namespace std;
using namespace ob;

namespace {

const double kNaN = numeric_limits<double>::quiet_NaN();
const double kInfinity = numeric_limits<double>::infinity();

const TrailerSpec& shippedTrailer() {
    static const M2Params params = loadParams("config/orderBuilderParams.json");
    return selectTrailer(params.trailers, params.sourcePath, "53FT_NA");
}

// A 48 x 40 in pallet: 32.8 of them cover the 630 x 100 in floor.
constexpr double kPalletFootprintSqIn = 48.0 * 40.0;

double floorAreaSqIn(const TrailerSpec& trailer) {
    return trailer.interiorLengthIn * trailer.interiorWidthIn;
}

double fullTrailerCubicIn(const TrailerSpec& trailer) {
    return floorAreaSqIn(trailer) * trailer.stackHeightCeilingIn;
}

// Unit loads of the given height and footprint, each light enough that weight never binds.
FloorTotals lightUnitLoads(double count, double unitLoadHeightIn,
                           double footprintSqIn = kPalletFootprintSqIn) {
    return {count, count * unitLoadHeightIn, count, count * footprintSqIn,
            count * footprintSqIn * unitLoadHeightIn};
}

// Loads exactly the given number of full trailers' volume, and nothing else.
FloorTotals cargoOf(double trailers, const TrailerSpec& trailer, double weightLb = 0.0) {
    return {weightLb, 0.0, 0.0, 0.0, trailers * fullTrailerCubicIn(trailer)};
}

TrailerSpec trailerWithDepth(int maxStackDepth) {
    TrailerSpec trailer = shippedTrailer();
    trailer.maxStackDepth = maxStackDepth;
    return trailer;
}

string errorMessage(const FloorTotals& totals, const TrailerSpec& trailer) {
    try {
        floorBound(totals, trailer);
    } catch (const invalid_argument& error) {
        return error.what();
    }
    return "";
}

} // namespace

TEST_CASE("floorBound: 64 light pallets that stack two-high need one truck, not two") {
    const TrailerSpec& trailer = shippedTrailer();
    const FloorBoundResult result =
        floorBound(lightUnitLoads(64.0, trailer.stackHeightCeilingIn / 2.0), trailer);
    CHECK(result.floorTrucks == 1);
    CHECK(result.binding == FloorTerm::Volume);
    CHECK(result.noStackingBaselineRounded == 2);
}

TEST_CASE("floorBound: an empty group has a floor of zero and no binding term") {
    const FloorBoundResult result = floorBound(FloorTotals{}, shippedTrailer());
    CHECK(result.floorTrucks == 0);
    CHECK(result.boundTrucks == 0.0);
    CHECK(result.binding == FloorTerm::None);
    CHECK(result.noStackingBaselineRounded == 0);
}

TEST_CASE("floorBound: a single unit load needs one truck") {
    const FloorBoundResult result = floorBound(lightUnitLoads(1.0, 50.0), shippedTrailer());
    CHECK(result.floorTrucks == 1);
}

TEST_CASE("floorBound: cargo exactly filling one trailer needs one truck, not two") {
    const TrailerSpec& trailer = shippedTrailer();
    const FloorBoundResult result = floorBound(cargoOf(1.0, trailer), trailer);
    CHECK(result.boundTrucks == doctest::Approx(1.0));
    CHECK(result.floorTrucks == 1);
}

TEST_CASE("floorBound: float drift summing to just over a full trailer still rounds to one") {
    const TrailerSpec& trailer = shippedTrailer();
    FloorTotals totals;
    const int slices = 320;
    for (int slice = 0; slice < slices; ++slice) {
        totals.cargoCubicIn += fullTrailerCubicIn(trailer) / slices;
    }
    CHECK(floorBound(totals, trailer).floorTrucks == 1);
}

TEST_CASE("floorBound: cargo just over a full trailer needs two trucks") {
    const TrailerSpec& trailer = shippedTrailer();
    CHECK(floorBound(cargoOf(1.001, trailer), trailer).floorTrucks == 2);
}

TEST_CASE("floorBound: 34 single-high 48 x 40 pallets need two trucks; 34 smaller 45 x 34 ones need one") {
    const TrailerSpec& trailer = shippedTrailer();
    const double fullHeight = trailer.stackHeightCeilingIn;
    CHECK(floorBound(lightUnitLoads(34.0, fullHeight), trailer).floorTrucks == 2);
    CHECK(floorBound(lightUnitLoads(34.0, fullHeight, 45.0 * 34.0), trailer).floorTrucks == 1);
}

TEST_CASE("floorBound: the floor does not read stack positions") {
    TrailerSpec trailer = shippedTrailer();
    const FloorTotals totals = lightUnitLoads(40.0, 60.0);
    const long long floorTrucks = floorBound(totals, trailer).floorTrucks;
    trailer.stackPositions = 0;
    CHECK(floorBound(totals, trailer).floorTrucks == floorTrucks);
}

TEST_CASE("floorBound: a heavy group is weight-bound and says so") {
    const TrailerSpec& trailer = shippedTrailer();
    const FloorBoundResult result =
        floorBound(cargoOf(1.0, trailer, 2.5 * trailer.weightLimitLb), trailer);
    CHECK(result.binding == FloorTerm::Weight);
    CHECK(result.weightTrucks == doctest::Approx(2.5));
    CHECK(result.floorTrucks == 3);
}

TEST_CASE("floorBound: a tie between weight and volume is reported as volume") {
    const TrailerSpec& trailer = shippedTrailer();
    CHECK(floorBound(cargoOf(1.0, trailer, trailer.weightLimitLb), trailer).binding
          == FloorTerm::Volume);
}

TEST_CASE("floorBound: every term is reported, including the ones that do not bind") {
    const TrailerSpec trailer = trailerWithDepth(3);
    FloorTotals totals = cargoOf(1.5, trailer, 0.5 * trailer.weightLimitLb);
    totals.floorAreaSqIn = 3.0 * floorAreaSqIn(trailer);
    const FloorBoundResult result = floorBound(totals, trailer);
    CHECK(result.weightTrucks == doctest::Approx(0.5));
    CHECK(result.volumeTrucks == doctest::Approx(1.5));
    REQUIRE(result.stackDepthTrucks.has_value());
    CHECK(*result.stackDepthTrucks == doctest::Approx(1.0));
    CHECK(result.noStackingBaselineTrucks == doctest::Approx(3.0));
    CHECK(result.binding == FloorTerm::Volume);
    CHECK(result.floorTrucks == 2);
}

TEST_CASE("floorBound: with no depth limit the stack-depth term does not exist") {
    const TrailerSpec& trailer = shippedTrailer();
    REQUIRE_FALSE(trailer.maxStackDepth.has_value());
    const FloorBoundResult result = floorBound(lightUnitLoads(1000.0, 1.0), trailer);
    CHECK_FALSE(result.stackDepthTrucks.has_value());
    CHECK(result.binding == FloorTerm::Volume);
}

TEST_CASE("floorBound: a configured depth limit binds short, numerous unit loads") {
    const TrailerSpec trailer = trailerWithDepth(3);
    FloorTotals totals;
    totals.floorAreaSqIn = 4.0 * 3.0 * floorAreaSqIn(trailer);
    const FloorBoundResult result = floorBound(totals, trailer);
    CHECK(result.binding == FloorTerm::StackDepth);
    CHECK(result.floorTrucks == 4);
}

TEST_CASE("floorBound: the no-stacking baseline is never the floor") {
    const TrailerSpec& trailer = shippedTrailer();
    FloorTotals totals;
    totals.floorAreaSqIn = 10.0 * floorAreaSqIn(trailer);
    totals.cargoCubicIn = 0.5 * fullTrailerCubicIn(trailer);
    const FloorBoundResult result = floorBound(totals, trailer);
    CHECK(result.noStackingBaselineRounded == 10);
    CHECK(result.floorTrucks == 1);
}

TEST_CASE("floorBound: a zero, negative or non-finite interior length or width is a named error") {
    for (const double side : {0.0, -1.0, kNaN, kInfinity, -kInfinity}) {
        CAPTURE(side);
        TrailerSpec shortTrailer = shippedTrailer();
        shortTrailer.interiorLengthIn = side;
        CHECK(errorMessage(lightUnitLoads(1.0, 1.0), shortTrailer).find("trailer.interiorLengthIn")
              != string::npos);
        TrailerSpec narrowTrailer = shippedTrailer();
        narrowTrailer.interiorWidthIn = side;
        CHECK(errorMessage(lightUnitLoads(1.0, 1.0), narrowTrailer).find("trailer.interiorWidthIn")
              != string::npos);
    }
}

TEST_CASE("floorBound: a zero, negative or non-finite weight limit is a named error") {
    for (const double limit : {0.0, -1.0, kNaN, kInfinity, -kInfinity}) {
        CAPTURE(limit);
        TrailerSpec trailer = shippedTrailer();
        trailer.weightLimitLb = limit;
        CHECK(errorMessage(lightUnitLoads(1.0, 1.0), trailer).find("trailer.weightLimitLb")
              != string::npos);
    }
}

TEST_CASE("floorBound: a zero, negative or non-finite ceiling is a named error") {
    for (const double ceiling : {0.0, -1.0, kNaN, kInfinity, -kInfinity}) {
        CAPTURE(ceiling);
        TrailerSpec trailer = shippedTrailer();
        trailer.stackHeightCeilingIn = ceiling;
        CHECK(errorMessage(lightUnitLoads(1.0, 1.0), trailer)
                  .find("trailer.stackHeightCeilingIn") != string::npos);
    }
}

TEST_CASE("floorBound: a zero or negative maximum stack depth is a named error") {
    for (const int depth : {0, -3}) {
        CAPTURE(depth);
        CHECK(errorMessage(lightUnitLoads(1.0, 1.0), trailerWithDepth(depth))
                  .find("trailer.maxStackDepth") != string::npos);
    }
}

TEST_CASE("floorBound: a negative or non-finite total is a named error") {
    for (const double bad : {-1.0, kNaN, kInfinity, -kInfinity}) {
        CAPTURE(bad);
        CHECK(errorMessage({bad, 1.0, 1.0, 1.0, 1.0}, shippedTrailer()).find("totalWeightLb")
              != string::npos);
        CHECK(errorMessage({1.0, bad, 1.0, 1.0, 1.0}, shippedTrailer()).find("stackedInches")
              != string::npos);
        CHECK(errorMessage({1.0, 1.0, bad, 1.0, 1.0}, shippedTrailer()).find("unitLoads")
              != string::npos);
        CHECK(errorMessage({1.0, 1.0, 1.0, bad, 1.0}, shippedTrailer()).find("floorAreaSqIn")
              != string::npos);
        CHECK(errorMessage({1.0, 1.0, 1.0, 1.0, bad}, shippedTrailer()).find("cargoCubicIn")
              != string::npos);
    }
}

TEST_CASE("floorBound: finite totals that overflow against a tiny trailer are an error") {
    TrailerSpec trailer = shippedTrailer();
    trailer.weightLimitLb = numeric_limits<double>::denorm_min();
    CHECK(errorMessage({numeric_limits<double>::max(), 1.0, 1.0, 1.0, 1.0}, trailer)
              .find("bound truck count") != string::npos);
}

TEST_CASE("floorBound: roundUpTrucks rounds partial trucks up and whole trucks unchanged") {
    CHECK(roundUpTrucks(0.0) == 0);
    CHECK(roundUpTrucks(0.01) == 1);
    CHECK(roundUpTrucks(1.0) == 1);
    CHECK(roundUpTrucks(1.5) == 2);
    CHECK(roundUpTrucks(1.0 + 1e-12) == 1);
}

TEST_CASE("floorBound: roundUpTrucks rejects negative, non-finite and oversized counts") {
    for (const double count : {-1.0, kNaN, kInfinity, -kInfinity, 1e300}) {
        CAPTURE(count);
        CHECK_THROWS_AS(roundUpTrucks(count), invalid_argument);
    }
}

TEST_CASE("floorBound: binding terms have stable plain-ASCII names") {
    CHECK(string(floorTermName(FloorTerm::None)) == "none");
    CHECK(string(floorTermName(FloorTerm::Weight)) == "weight");
    CHECK(string(floorTermName(FloorTerm::Volume)) == "volume");
    CHECK(string(floorTermName(FloorTerm::StackDepth)) == "stack_depth");
}
