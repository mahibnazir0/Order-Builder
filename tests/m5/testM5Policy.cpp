#include "doctest.h"
#include "m5Budget.hpp"
#include "m5Policy.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

using namespace std;
using namespace ob::m5;
using json = nlohmann::json;

namespace {

json productionBlock() {
    return json::parse(R"({
        "enabled": true,
        "profileId": "customer2-production",
        "profileRevision": 1,
        "routing": {"countMode": "reservedTrucks", "scope": "perLaneShipDate"},
        "secondsPerLoad": 1,
        "fullness": {"weightFullPercent": 98, "rearInchLimit": 14},
        "methodPolicy": "SequentialAThenB",
        "methodAShare": 0.6,
        "blockSize": 19,
        "scarcityWindow": 2,
        "gapTrucks": 1,
        "canGoFill": true,
        "repair": {"maxRounds": 8, "maxMovesPerTruck": 64},
        "penaltyPerUnusedTruck": null
    })");
}

void requireRejected(json block, const string& why) {
    INFO(why);
    CHECK_THROWS_AS(loadProfile(block), runtime_error);
}

}  // namespace

TEST_CASE("U-AC-01 acceptance is strict on both branches" * doctest::test_suite("m5")) {
    const NumericAcceptance rule{40000.0, 3000.0};
    const double cubicInchesPerCubicFoot = 1728.0;
    CHECK_FALSE(evaluateAcceptance(40000.0, 2000.0 * cubicInchesPerCubicFoot, rule).accepted);
    CHECK(evaluateAcceptance(40001.0, 2000.0 * cubicInchesPerCubicFoot, rule).accepted);
    CHECK_FALSE(evaluateAcceptance(20000.0, 3000.0 * cubicInchesPerCubicFoot, rule).accepted);
    CHECK(evaluateAcceptance(20000.0, 3000.1 * cubicInchesPerCubicFoot, rule).accepted);
}

TEST_CASE("U-AC-09 acceptance keeps distinctions finer than 0.001" * doctest::test_suite("m5")) {
    const NumericAcceptance rule{40000.0, 3000.0};
    CHECK(evaluateAcceptance(40000.0001, 0.0, rule).byWeight);
    CHECK_FALSE(evaluateAcceptance(40000.0, 0.0, rule).byWeight);
    const double exactlyY = 3000.0 * 1728.0;
    CHECK_FALSE(evaluateAcceptance(0.0, exactlyY, rule).byCube);
    CHECK(evaluateAcceptance(0.0, exactlyY + 0.0001, rule).byCube);
}

TEST_CASE("X-REPORT-04 cube decides at full precision beside y = 3,300 cu ft" * doctest::test_suite("m5")) {
    const NumericAcceptance rule{40000.0, 3300.0};
    CHECK_FALSE(evaluateAcceptance(20000.0, 3299.9 * 1728.0, rule).accepted);
    CHECK_FALSE(evaluateAcceptance(20000.0, 3300.0 * 1728.0, rule).accepted);
    CHECK(evaluateAcceptance(20000.0, 3300.1 * 1728.0, rule).accepted);
    CHECK(evaluateAcceptance(20000.0, 3307.5 * 1728.0, rule).accepted);
}

TEST_CASE("U-AC-04 productionProfile: an absent acceptance block is OFF" * doctest::test_suite("m5")) {
    const M5PolicyProfile profile = loadProfile(productionBlock());
    CHECK_FALSE(profile.acceptance.has_value());
    CHECK(profile.profileId == "customer2-production");
    CHECK_FALSE(profile.penaltyPerUnusedTruck.has_value());
}

TEST_CASE("U-AC-04 a present acceptance block is read, and a malformed one refused" * doctest::test_suite("m5")) {
    json block = productionBlock();
    block["acceptance"] = {{"weightLb", 40000}, {"cubeCuFt", 3000}};
    const M5PolicyProfile profile = loadProfile(block);
    REQUIRE(profile.acceptance.has_value());
    CHECK(profile.acceptance->weightLb == 40000.0);
    CHECK(profile.acceptance->cubeCuFt == 3000.0);

    block["acceptance"] = "on";
    requireRejected(block, "acceptance given as a string");
    block["acceptance"] = {{"weightLb", 40000}};
    requireRejected(block, "acceptance without y");
    block["acceptance"] = {{"weightLb", -1}, {"cubeCuFt", 3000}};
    requireRejected(block, "negative x");
    block["acceptance"] = {{"weightLb", "40000"}, {"cubeCuFt", 3000}};
    requireRejected(block, "x as text");
}

TEST_CASE("U-AC-08 a profile's key changes with its revision and settings" * doctest::test_suite("m5")) {
    json block = productionBlock();
    const string first = loadProfile(block).key();
    block["profileRevision"] = 2;
    CHECK(loadProfile(block).key() != first);
    block["profileRevision"] = 1;
    block["fullness"]["rearInchLimit"] = 30;
    CHECK(loadProfile(block).key() != first);
}

TEST_CASE("U-FL-03 rear-inch limit: default, overrides and invalid values" * doctest::test_suite("m5")) {
    json block = productionBlock();
    block["fullness"].erase("rearInchLimit");
    const M5PolicyProfile defaulted = loadProfile(block);
    CHECK(defaulted.fullness.rearInchLimit == 14.0);
    CHECK(defaulted.fullness.rearInchLimitDefaulted);
    for (const double limit : {14.0, 10.0, 30.0}) {
        block["fullness"]["rearInchLimit"] = limit;
        const M5PolicyProfile profile = loadProfile(block);
        CHECK(profile.fullness.rearInchLimit == limit);
        CHECK_FALSE(profile.fullness.rearInchLimitDefaulted);
    }
    for (const json& bad : {json(-1), json(0), json(kMaxRearInchLimit + 1), json("14"), json(nullptr)}) {
        block["fullness"]["rearInchLimit"] = bad;
        requireRejected(block, "rear-inch limit " + bad.dump());
    }
}

TEST_CASE("U-RT-04 U-RT-05 routing other than reserved trucks per lane-date is refused" * doctest::test_suite("m5")) {
    json block = productionBlock();
    block["routing"]["countMode"] = "demandTrucks";
    requireRejected(block, "demand-count routing");
    block = productionBlock();
    block["routing"]["scope"] = "perLane";
    requireRejected(block, "per-lane scope");
}

TEST_CASE("U-RB-03 X-ROUTE-06 U-FF-04 block size and gap must be whole counts of at least 1" * doctest::test_suite("m5")) {
    for (const json& bad : {json(0), json(-19), json(1.5), json("19")}) {
        json block = productionBlock();
        block["blockSize"] = bad;
        requireRejected(block, "block size " + bad.dump());
        block = productionBlock();
        block["gapTrucks"] = bad;
        requireRejected(block, "gap " + bad.dump());
    }
    json block = productionBlock();
    block["blockSize"] = 7;
    CHECK(loadProfile(block).blockSize == 7);
    block.erase("blockSize");
    requireRejected(block, "block size missing");
}

TEST_CASE("U-PF-05 lane-date allowance is seconds per load times reserved trucks" * doctest::test_suite("m5")) {
    CHECK(*laneAllowanceSeconds(1.0, 21, nullptr) == 21.0);
    CHECK(*laneAllowanceSeconds(1.0, 20, nullptr) == 20.0);
    CHECK(*laneAllowanceSeconds(2.5, 21, nullptr) == 52.5);
    CHECK(*laneAllowanceSeconds(2.5, 20, nullptr) == 50.0);
    for (const json& bad : {json(0), json(-1), json("1"), json(nullptr)}) {
        json block = productionBlock();
        block["secondsPerLoad"] = bad;
        requireRejected(block, "seconds per load " + bad.dump());
    }
    json block = productionBlock();
    block.erase("secondsPerLoad");
    requireRejected(block, "seconds per load missing");
}

TEST_CASE("U-PF-06 allowance arithmetic refuses overflow and non-finite rates" * doctest::test_suite("m5")) {
    string error;
    CHECK(*laneAllowanceSeconds(1.0, 0, &error) == 0.0);
    CHECK_FALSE(laneAllowanceSeconds(1.0e300, 21, &error).has_value());
    CHECK_FALSE(error.empty());
    CHECK_FALSE(laneAllowanceSeconds(numeric_limits<double>::quiet_NaN(), 21, nullptr).has_value());
    CHECK_FALSE(laneAllowanceSeconds(numeric_limits<double>::infinity(), 21, nullptr).has_value());
    CHECK_FALSE(laneAllowanceSeconds(1.0, -1, nullptr).has_value());
}

TEST_CASE("M5 is off unless the params file enables it" * doctest::test_suite("m5")) {
    CHECK_FALSE(m5Enabled(json::object()));
    json root;
    root["m5"] = productionBlock();
    CHECK(m5Enabled(root));
    root["m5"]["enabled"] = false;
    CHECK_FALSE(m5Enabled(root));
    root["m5"]["enabled"] = "yes";
    CHECK_THROWS_AS(m5Enabled(root), runtime_error);
    root["m5"] = json::array();
    CHECK_THROWS_AS(m5Enabled(root), runtime_error);
}

TEST_CASE("CL-06 a penalty rate is read when given; null is unknown, never zero" * doctest::test_suite("m5")) {
    json block = productionBlock();
    CHECK_FALSE(loadProfile(block).penaltyPerUnusedTruck.has_value());
    block["penaltyPerUnusedTruck"] = 0;
    REQUIRE(loadProfile(block).penaltyPerUnusedTruck.has_value());
    CHECK(*loadProfile(block).penaltyPerUnusedTruck == 0.0);
    block["penaltyPerUnusedTruck"] = -5;
    requireRejected(block, "negative penalty");
    block.erase("penaltyPerUnusedTruck");
    requireRejected(block, "penalty key missing");
}

TEST_CASE("X-BLK-07 a slice never extends the lane-date's deadline" * doctest::test_suite("m5")) {
    class StepClock final : public Clock {
    public:
        double nowSeconds() const override { return now; }
        double now = 0.0;
    } clock;
    Budget budget({100.0, nullopt, "FX-CLOCK"}, clock);
    clock.now = 60.0;
    clock.now = 85.0;
    CHECK(budget.remainingSeconds() == doctest::Approx(15.0));
    const double end = budget.sliceEnd(1.0);
    CHECK(end == doctest::Approx(100.0));
    clock.now = 100.0;
    CHECK(budget.exhausted());
    CHECK(budget.pastSlice(end));
}
