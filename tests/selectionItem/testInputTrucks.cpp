#include "doctest.h"
#include "../acceptance/acceptanceHarness.hpp"
#include "../m4AugustFixtures.hpp"
#include "joiner.hpp"
#include "selectionItem.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;

namespace {

ProductRecord product(const string& id, const string& uom, int casesUnitLoad) {
    ProductRecord built;
    built.id = id;
    built.uom = uom;
    built.cases_unit_load = casesUnitLoad;
    return built;
}

const ProductIndex& products() {
    static const ProductIndex index =
        Joiner::build_index({product("DISPLAY", "DIS", 18), product("CASE", "CS", 100), product("BROKEN", "CS", 0),
                             product("NEGATIVE", "CS", -4)});
    return index;
}

InputTruck truckOf(vector<InputTruckLine> lines) {
    return {"T1", "791", "2026-08-06", std::move(lines)};
}

string gateError(const InputTruck& truck) {
    try {
        requirePlausibleTrucks({truck}, products(), kMaxUnitLoadEquivalentsPerTruck);
    } catch (const invalid_argument& error) {
        return error.what();
    }
    return "";
}

} // anonymous namespace

TEST_CASE("inputTrucks: unit-load equivalents divide each line by its product's Cases_Unit_Load") {
    const InputTruck truck = truckOf({{"A", "DISPLAY", 432.0, "DIS"}, {"B", "CASE", 800.0, "CS"}});
    CHECK(unitLoadEquivalents(truck, products()) == doctest::Approx(32.0));
    CHECK(gateError(truck).empty());
}

TEST_CASE("inputTrucks: a truck of exactly 64 unit-load equivalents passes the gate") {
    CHECK(gateError(truckOf({{"A", "DISPLAY", 64.0 * 18.0, "DIS"}})).empty());
}

TEST_CASE("inputTrucks: a truck over 64 unit-load equivalents stops the run, naming its line, product and unit") {
    const string error = gateError(truckOf({{"A", "CASE", 100.0, "CS"}, {"B", "DISPLAY", 64.0 * 18.0, "DIS"}}));
    REQUIRE_FALSE(error.empty());
    CHECK(error.find("truck T1") != string::npos);
    CHECK(error.find("IDPR B") != string::npos);
    CHECK(error.find("product DISPLAY") != string::npos);
    CHECK(error.find("DIS") != string::npos);
    CHECK(error.find("65") != string::npos);
}

TEST_CASE("inputTrucks: a quantity that is negative or not finite stops the run, named") {
    for (const double quantity : {-1.0, numeric_limits<double>::quiet_NaN(), numeric_limits<double>::infinity()}) {
        CAPTURE(quantity);
        const string error = gateError(truckOf({{"A", "CASE", quantity, "CS"}}));
        CHECK(error.find("IDPR A, product CASE, CS: quantity") != string::npos);
    }
}

TEST_CASE("inputTrucks: a product missing from the master, or in another unit, stops the run, named") {
    CHECK(gateError(truckOf({{"A", "UNKNOWN", 1.0, "CS"}})).find("product UNKNOWN, CS: the product is not in the master")
          != string::npos);
    CHECK(gateError(truckOf({{"A", "CASE", 1.0, "DIS"}})).find("product CASE, DIS: the master has no convertible row")
          != string::npos);
}

TEST_CASE("inputTrucks: a Cases_Unit_Load of zero or below stops the run rather than counting nothing") {
    CHECK(gateError(truckOf({{"A", "BROKEN", 1000.0, "CS"}})).find("Cases_Unit_Load 0 is not positive") != string::npos);
    CHECK(gateError(truckOf({{"A", "NEGATIVE", 1000.0, "CS"}})).find("Cases_Unit_Load -4 is not positive")
          != string::npos);
}

TEST_CASE("inputTrucks: lane 791 truck 1343393 of 4 Aug is 32 unit loads, DIS converted at 18 per unit load"
          * doctest::skip(!m4AugustTests::augustPresent() || !m4AugustTests::augustSolutionsPresent())) {
    const PipelineResult& run = m4AugustTests::augustItems(0).run;
    const acceptanceTests::AchievedRequest achieved = acceptanceTests::readAchievedLoads(
        m4AugustTests::kAugustSolutionDirectories[0], run.demand.request_id, "1343393");
    REQUIRE(achieved.trucks.size() == 1);
    const InputTruck& truck = achieved.trucks.front();
    CHECK(truck.laneId == "791");
    double displays = 0.0, cases = 0.0;
    for (const InputTruckLine& line : truck.lines) {
        if (line.matnr == "105456404" && line.unitOfMeasure == "DIS") displays += line.quantity;
        if (line.matnr == "105597501" && line.unitOfMeasure == "CS") cases += line.quantity;
    }
    CHECK(displays == doctest::Approx(432.0));
    CHECK(cases == doctest::Approx(800.0));
    CHECK(unitLoadEquivalents(truck, run.index) == doctest::Approx(24.0 + 8.0));
}
