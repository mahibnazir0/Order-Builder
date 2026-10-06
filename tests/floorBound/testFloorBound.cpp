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

double fullTrailerInches(const TrailerSpec& trailer) {
    return trailer.stackHeightCeilingIn * trailer.stackPositions;
}

// Unit loads of the given height, each light enough that weight never binds.
FloorTotals lightUnitLoads(double count, double unitLoadHeightIn) {
    return {count, count * unitLoadHeightIn, count};
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
    const double unitLoads = 2.0 * trailer.stackPositions;
    const FloorBoundResult result =
        floorBound(lightUnitLoads(unitLoads, trailer.stackHeightCeilingIn / 2.0), trailer);
    CHECK(result.floorTrucks == 1);
    CHECK(result.binding == FloorTerm::StackedHeight);
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

TEST_CASE("floorBound: a group exactly filling one trailer needs one truck, not two") {
    const TrailerSpec& trailer = shippedTrailer();
    const FloorBoundResult result = floorBound(
        lightUnitLoads(trailer.stackPositions, trailer.stackHeightCeilingIn), trailer);
    CHECK(result.boundTrucks == doctest::Approx(1.0));
    CHECK(result.floorTrucks == 1);
}

TEST_CASE("floorBound: float drift summing to just over a full trailer still rounds to one") {
    const TrailerSpec& trailer = shippedTrailer();
    double stackedInches = 0.0;
    const int slices = 10 * trailer.stackPositions;
    for (int slice = 0; slice < slices; ++slice) {
        stackedInches += fullTrailerInches(trailer) / slices;
    }
    CHECK(floorBound({0.0, stackedInches, 0.0}, trailer).floorTrucks == 1);
}

TEST_CASE("floorBound: one unit load over a full trailer needs two trucks") {
    const TrailerSpec& trailer = shippedTrailer();
    const FloorBoundResult result = floorBound(
        lightUnitLoads(trailer.stackPositions + 1.0, trailer.stackHeightCeilingIn), trailer);
    CHECK(result.floorTrucks == 2);
}

TEST_CASE("floorBound: a heavy group is weight-bound and says so") {
    const TrailerSpec& trailer = shippedTrailer();
    const FloorTotals totals{2.5 * trailer.weightLimitLb, fullTrailerInches(trailer), 32.0};
    const FloorBoundResult result = floorBound(totals, trailer);
    CHECK(result.binding == FloorTerm::Weight);
    CHECK(result.weightTrucks == doctest::Approx(2.5));
    CHECK(result.floorTrucks == 3);
}

TEST_CASE("floorBound: a tie between weight and stacked height is reported as stacked height") {
    const TrailerSpec& trailer = shippedTrailer();
    const FloorTotals totals{trailer.weightLimitLb, fullTrailerInches(trailer), 1.0};
    CHECK(floorBound(totals, trailer).binding == FloorTerm::StackedHeight);
}

TEST_CASE("floorBound: every term is reported, including the ones that do not bind") {
    const TrailerSpec trailer = trailerWithDepth(3);
    const FloorTotals totals{0.5 * trailer.weightLimitLb, 1.5 * fullTrailerInches(trailer),
                             trailer.stackPositions * 3.0};
    const FloorBoundResult result = floorBound(totals, trailer);
    CHECK(result.weightTrucks == doctest::Approx(0.5));
    CHECK(result.stackedHeightTrucks == doctest::Approx(1.5));
    REQUIRE(result.stackDepthTrucks.has_value());
    CHECK(*result.stackDepthTrucks == doctest::Approx(1.0));
    CHECK(result.noStackingBaselineTrucks == doctest::Approx(3.0));
    CHECK(result.binding == FloorTerm::StackedHeight);
    CHECK(result.floorTrucks == 2);
}

TEST_CASE("floorBound: with no depth limit the stack-depth term does not exist") {
    const TrailerSpec& trailer = shippedTrailer();
    REQUIRE_FALSE(trailer.maxStackDepth.has_value());
    const FloorBoundResult result = floorBound(lightUnitLoads(1000.0, 1.0), trailer);
    CHECK_FALSE(result.stackDepthTrucks.has_value());
    CHECK(result.binding == FloorTerm::StackedHeight);
}

TEST_CASE("floorBound: a configured depth limit binds short, numerous unit loads") {
    const TrailerSpec trailer = trailerWithDepth(3);
    const double unitLoads = trailer.stackPositions * 3.0 * 4.0;
    const FloorBoundResult result = floorBound(lightUnitLoads(unitLoads, 1.0), trailer);
    CHECK(result.binding == FloorTerm::StackDepth);
    CHECK(result.floorTrucks == 4);
}

TEST_CASE("floorBound: the no-stacking baseline is never the floor") {
    const TrailerSpec& trailer = shippedTrailer();
    const FloorBoundResult result =
        floorBound(lightUnitLoads(10.0 * trailer.stackPositions, 1.0), trailer);
    CHECK(result.noStackingBaselineRounded == 10);
    CHECK(result.floorTrucks == 1);
}

TEST_CASE("floorBound: zero or negative stack positions is a named error") {
    for (const int positions : {0, -32}) {
        CAPTURE(positions);
        TrailerSpec trailer = shippedTrailer();
        trailer.stackPositions = positions;
        CHECK(errorMessage(lightUnitLoads(1.0, 1.0), trailer).find("trailer.stackPositions")
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

TEST_CASE("floorBound: a negative or non-finite total weight is a named error") {
    for (const double weight : {-1.0, kNaN, kInfinity, -kInfinity}) {
        CAPTURE(weight);
        CHECK(errorMessage({weight, 1.0, 1.0}, shippedTrailer()).find("totalWeightLb")
              != string::npos);
    }
}

TEST_CASE("floorBound: negative or non-finite stacked inches is a named error") {
    for (const double inches : {-1.0, kNaN, kInfinity, -kInfinity}) {
        CAPTURE(inches);
        CHECK(errorMessage({1.0, inches, 1.0}, shippedTrailer()).find("stackedInches")
              != string::npos);
    }
}

TEST_CASE("floorBound: negative or non-finite unit loads is a named error") {
    for (const double unitLoads : {-1.0, kNaN, kInfinity, -kInfinity}) {
        CAPTURE(unitLoads);
        CHECK(errorMessage({1.0, 1.0, unitLoads}, shippedTrailer()).find("unitLoads")
              != string::npos);
    }
}

TEST_CASE("floorBound: finite totals that overflow against a tiny trailer are an error") {
    TrailerSpec trailer = shippedTrailer();
    trailer.weightLimitLb = numeric_limits<double>::denorm_min();
    CHECK(errorMessage({numeric_limits<double>::max(), 1.0, 1.0}, trailer)
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
    CHECK(string(floorTermName(FloorTerm::StackedHeight)) == "stacked_height");
    CHECK(string(floorTermName(FloorTerm::StackDepth)) == "stack_depth");
}
