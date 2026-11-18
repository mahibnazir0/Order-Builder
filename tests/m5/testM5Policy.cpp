#include "doctest.h"
#include "m5Budget.hpp"
#include "m5Policy.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Fixtures.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

using namespace std;
using namespace ob::m5;
using namespace m5Fixtures;
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

// The message a refused block gives, or "" when it loads.
string refusalOf(const json& block) {
    try {
        loadProfile(block);
    } catch (const runtime_error& error) {
        return error.what();
    }
    return "";
}

}  // namespace

TEST_CASE("U-AC-01/synthetic: of the four trucks only 40,001 lb and 3,000.1 cu ft are accepted" * doctest::test_suite("m5")) {
    const NumericAcceptance rule{40000.0, 3000.0};
    const double cubicInchesPerCubicFoot = 1728.0;
    const AcceptanceDecision atWeight = evaluateAcceptance(40000.0, 2000.0 * cubicInchesPerCubicFoot, rule);
    CHECK_FALSE(atWeight.byWeight);
    CHECK_FALSE(atWeight.byCube);
    CHECK_FALSE(atWeight.accepted);
    const AcceptanceDecision overWeight = evaluateAcceptance(40001.0, 2000.0 * cubicInchesPerCubicFoot, rule);
    CHECK(overWeight.byWeight);
    CHECK_FALSE(overWeight.byCube);
    CHECK(overWeight.accepted);
    const AcceptanceDecision atCube = evaluateAcceptance(20000.0, 3000.0 * cubicInchesPerCubicFoot, rule);
    CHECK_FALSE(atCube.byCube);
    CHECK_FALSE(atCube.accepted);
    const AcceptanceDecision overCube = evaluateAcceptance(20000.0, 3000.1 * cubicInchesPerCubicFoot, rule);
    CHECK_FALSE(overCube.byWeight);
    CHECK(overCube.byCube);
    CHECK(overCube.accepted);
    m5Evidence::note("expected", {"40000/2000 no", "40001/2000 yes", "20000/3000 no", "20000/3000.1 yes"});
}

TEST_CASE("U-AC-01/productionProfile: no acceptance block is valid and OFF; a malformed one is still refused" * doctest::test_suite("m5")) {
    json block = productionBlock();
    const M5PolicyProfile profile = loadProfile(block);
    CHECK_FALSE(profile.acceptance.has_value());
    CHECK(profile.profileId == "customer2-production");
    block["acceptance"] = json::array({40000, 3000});
    requireRejected(block, "acceptance given as an array");
    block["acceptance"] = {{"weightLb", 40000}, {"cubeCuFt", "3000"}};
    requireRejected(block, "y as text");
    // The predicate is not consulted: a 20,000 lb, 2,000 cu ft truck ships with the production profile.
    const M5LaneDateInput input = m5Fixtures::laneWith(m5Fixtures::fx30Units("u", 16), 1);
    M5PolicyProfile production = m5Fixtures::syntheticProfile();
    production.acceptance = profile.acceptance;
    CHECK(m5Fixtures::shippedCount(m5Fixtures::plan(input, production)) == 16);
    m5Evidence::note("expected", {"absent block valid, policy OFF", "malformed block refused", "16 of 16 ship"});
}

TEST_CASE("U-AC-04/valuesAndMalformed: x and y are read, change the verdict, and bad values are errors" * doctest::test_suite("m5")) {
    json block = productionBlock();
    const auto acceptanceOf = [&](double x, double y) {
        block["acceptance"] = {{"weightLb", x}, {"cubeCuFt", y}};
        const M5PolicyProfile profile = loadProfile(block);
        REQUIRE(profile.acceptance.has_value());
        CHECK(profile.acceptance->weightLb == x);
        CHECK(profile.acceptance->cubeCuFt == y);
        return *profile.acceptance;
    };
    // The same 30,000 lb, 2,500 cu ft truck under three declared thresholds.
    const double weight = 30000.0, cube = 2500.0 * 1728.0;
    CHECK(evaluateAcceptance(weight, cube, acceptanceOf(40000, 2400)).accepted);
    CHECK_FALSE(evaluateAcceptance(weight, cube, acceptanceOf(40000, 3000)).accepted);
    CHECK(evaluateAcceptance(weight, cube, acceptanceOf(29999.5, 3000)).accepted);
    const string configKey = loadProfile(block).key();
    CHECK(acceptanceOf(40000, 3000).cubeCuFt == 3000.0);
    CHECK(loadProfile(block).key() != configKey);

    const double infinity = numeric_limits<double>::infinity(), notANumber = numeric_limits<double>::quiet_NaN();
    block["acceptance"] = {{"cubeCuFt", 3000}};
    requireRejected(block, "x missing");
    block["acceptance"] = {{"weightLb", 40000}};
    requireRejected(block, "y missing");
    block["acceptance"] = json::object();
    requireRejected(block, "both missing");
    for (const pair<json, json>& bad : {pair<json, json>{"40000", 3000}, {40000, true}, {json::array({40000}), 3000},
                                        {notANumber, 3000}, {40000, notANumber}, {infinity, 3000}, {40000, -infinity},
                                        {1e300, 3000}, {40000, 1e300}}) {
        block["acceptance"] = {{"weightLb", bad.first}, {"cubeCuFt", bad.second}};
        requireRejected(block, "acceptance " + block["acceptance"].dump());
    }
    // A literal beyond double range overflows: the profile text is refused, never read as a default.
    const string overflowing = R"({"weightLb": 1e400, "cubeCuFt": 3000})";
    bool refused = false;
    try {
        block["acceptance"] = json::parse(overflowing);
        (void)loadProfile(block);
    } catch (const exception&) {
        refused = true;
    }
    CHECK(refused);
    m5Evidence::note("expected", {"40000/2400 accept", "40000/3000 reject", "29999.5/3000 accept",
                                  "missing, malformed, NaN, infinity, overflow refused"});
}

namespace {

// U-AC-09's binary-exact neighbours of the thresholds: 1/4096 cu ft and 1/256 lb.
constexpr double kCubeGrainCuFt = 1.0 / 4096.0;
constexpr double kWeightGrainLb = 1.0 / 256.0;
const NumericAcceptance kSubGrainRule{40000.0, 3000.0};

bool acceptedByCube(double cubeCuFt) {
    const AcceptanceDecision result = evaluateAcceptance(20000.0, cubeCuFt * 1728.0, kSubGrainRule);
    CHECK_FALSE(result.byWeight);
    CHECK(result.byCube == result.accepted);
    return result.accepted;
}

bool acceptedByWeight(double weightLb) {
    const AcceptanceDecision result = evaluateAcceptance(weightLb, 2000.0 * 1728.0, kSubGrainRule);
    CHECK_FALSE(result.byCube);
    CHECK(result.byWeight == result.accepted);
    return result.accepted;
}

}  // namespace

TEST_CASE("U-AC-09/cubeBelow: 3,000 - 1/4096 cu ft is not accepted" * doctest::test_suite("m5")) {
    CHECK(3000.0 - kCubeGrainCuFt == 2999.999755859375);
    CHECK_FALSE(acceptedByCube(3000.0 - kCubeGrainCuFt));
}

TEST_CASE("U-AC-09/cubeEqual: exactly 3,000 cu ft is not accepted (strict)" * doctest::test_suite("m5")) {
    CHECK_FALSE(acceptedByCube(3000.0));
}

TEST_CASE("U-AC-09/cubeAbove: 3,000 + 1/4096 cu ft is accepted although it prints as 3,000.000" * doctest::test_suite("m5")) {
    CHECK(3000.0 + kCubeGrainCuFt == 3000.000244140625);
    CHECK(acceptedByCube(3000.0 + kCubeGrainCuFt));
}

TEST_CASE("U-AC-09/weightBelow: 40,000 - 1/256 lb is not accepted" * doctest::test_suite("m5")) {
    CHECK_FALSE(acceptedByWeight(40000.0 - kWeightGrainLb));
}

TEST_CASE("U-AC-09/weightEqual: exactly 40,000 lb is not accepted (strict)" * doctest::test_suite("m5")) {
    CHECK_FALSE(acceptedByWeight(40000.0));
}

TEST_CASE("U-AC-09/weightAbove: 40,000 + 1/256 lb is accepted" * doctest::test_suite("m5")) {
    CHECK(acceptedByWeight(40000.0 + kWeightGrainLb));
}

TEST_CASE("U-AC-09/policyOff: with acceptance OFF the same sub-grain trucks all ship" * doctest::test_suite("m5")) {
    // Policy OFF: no predicate, so a truck below, at or above the thresholds ships alike.
    const M5LaneDateInput input = m5Fixtures::laneWith(m5Fixtures::fx30Units("u", 20), 1);
    const M5PolicyProfile off = m5Fixtures::syntheticProfile();
    REQUIRE_FALSE(off.acceptance.has_value());
    const M5LaneOutcome outcome = m5Fixtures::plan(input, off);
    CHECK(m5Fixtures::shippedCount(outcome) == 20);
    REQUIRE(outcome.selected);
    REQUIRE(outcome.selected->validation);
    CHECK(outcome.selected->validation->acceptance == SectionStatus::NotApplicable);
}

TEST_CASE("X-REPORT-04 cube decides at full precision beside y = 3,300 cu ft" * doctest::test_suite("m5")) {
    const NumericAcceptance rule{40000.0, 3300.0};
    CHECK_FALSE(evaluateAcceptance(20000.0, 3299.9 * 1728.0, rule).accepted);
    CHECK_FALSE(evaluateAcceptance(20000.0, 3300.0 * 1728.0, rule).accepted);
    CHECK(evaluateAcceptance(20000.0, 3300.1 * 1728.0, rule).accepted);
    CHECK(evaluateAcceptance(20000.0, 3307.5 * 1728.0, rule).accepted);
}

TEST_CASE("U-AC-04/productionProfile: an absent acceptance block is valid and OFF; a present malformed one is still an error" * doctest::test_suite("m5")) {
    const M5PolicyProfile profile = loadProfile(productionBlock());
    CHECK_FALSE(profile.acceptance.has_value());
    CHECK(profile.profileId == "customer2-production");
    CHECK_FALSE(profile.penaltyPerUnusedTruck.has_value());
    json block = productionBlock();
    block["acceptance"] = "on";
    requireRejected(block, "acceptance given as a string");
    block["acceptance"] = {{"weightLb", 40000}};
    requireRejected(block, "acceptance without y");
    block["acceptance"] = json::array();
    requireRejected(block, "acceptance given as an array");
    m5Evidence::note("expected", {"absent: OFF, valid", "malformed: configuration error"});
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
    for (const json& bad : {json(-1), json(0), json(0.001), json("14"), json(nullptr)}) {
        block["fullness"]["rearInchLimit"] = bad;
        requireRejected(block, "rear-inch limit " + bad.dump());
    }
}

TEST_CASE("U-RT-04/synthetic: only the reserved-truck count is built; a demand-based count is refused with a clear message" * doctest::test_suite("m5")) {
    CHECK(loadProfile(productionBlock()).profileId == "customer2-production");
    for (const string mode : {"demandTrucks", "demandUnitLoads", "", "ReservedTrucks"}) {
        json block = productionBlock();
        block["routing"]["countMode"] = mode;
        const string message = refusalOf(block);
        INFO(mode << ": " << message);
        CHECK(message.find("m5.routing.countMode '" + mode + "' is not supported") != string::npos);
        CHECK(message.find("reservedTrucks") != string::npos);
    }
    json missing = productionBlock();
    missing["routing"].erase("countMode");
    CHECK(refusalOf(missing).find("countMode") != string::npos);
    json wrongType = productionBlock();
    wrongType["routing"]["countMode"] = 21;
    CHECK(refusalOf(wrongType).find("countMode") != string::npos);
    m5Evidence::note("expected", {"reservedTrucks accepted", "demand counts, empty, wrong case, missing and non-text refused by name"});
}

TEST_CASE("U-RT-05/synthetic: only per lane and ship date is built; a per-lane count is refused with a clear message" * doctest::test_suite("m5")) {
    for (const string scope : {"perLane", "perShipDate", "", "PerLaneShipDate"}) {
        json block = productionBlock();
        block["routing"]["scope"] = scope;
        const string message = refusalOf(block);
        INFO(scope << ": " << message);
        CHECK(message.find("m5.routing.scope '" + scope + "' is not supported") != string::npos);
        CHECK(message.find("per lane and ship date") != string::npos);
    }
    json missing = productionBlock();
    missing["routing"].erase("scope");
    CHECK(refusalOf(missing).find("scope") != string::npos);
    m5Evidence::note("expected", {"perLaneShipDate accepted", "per lane, per date, empty, wrong case and missing refused by name"});
}

TEST_CASE("U-RB-03/synthetic X-ROUTE-06: the block size is read from the settings and changes the blocks; 0, negative and non-whole refused" * doctest::test_suite("m5")) {
    for (const json& bad : {json(0), json(-19), json(1.5), json("19"), json(nullptr)}) {
        json block = productionBlock();
        block["blockSize"] = bad;
        const string message = refusalOf(block);
        INFO("block size " << bad.dump() << ": " << message);
        CHECK(message.find("blockSize") != string::npos);
    }
    json missing = productionBlock();
    missing.erase("blockSize");
    CHECK(refusalOf(missing).find("blockSize") != string::npos);
    CHECK(loadProfile(productionBlock()).blockSize == 19);

    // The loaded size is the one the blocks method uses: 15 whole-truck items in blocks of 19 and of 7.
    const auto blocksUnder = [](const json& block) {
        M5PolicyProfile profile = loadProfile(block);
        profile.method = MethodPolicy::BlocksOnly;
        vector<Atom> atoms;
        for (int unit = 0; unit < 15; ++unit) atoms.push_back(fxBlock("b" + to_string(100 + unit)));
        vector<size_t> after;
        FakeClock clock;
        M5RunServices services(clock);
        services.afterBlock = [&](size_t trucks) { after.push_back(trucks); };
        const M5LaneOutcome outcome = planLaneDate(laneWith(atoms, 39), profile, physicalSettings(), services);
        CHECK(shippedCount(outcome) == 15);
        return after;
    };
    CHECK(blocksUnder(productionBlock()) == vector<size_t>{15});
    json seven = productionBlock();
    seven["blockSize"] = 7;
    CHECK(blocksUnder(seven) == vector<size_t>{7, 14, 15});
    m5Evidence::note("expected", {"blockSize 19: one block of 15", "blockSize 7: 7 + 7 + 1", "0, -19, 1.5, text, null, missing refused"});
}

TEST_CASE("U-FF-04/synthetic: the gap is a positive whole number of trucks, G = 1 as shipped; 0, negative, fractional, NaN and overflow refused" * doctest::test_suite("m5")) {
    CHECK(loadProfile(productionBlock()).gapTrucks == 1);
    json two = productionBlock();
    two["gapTrucks"] = 2;
    CHECK(loadProfile(two).gapTrucks == 2);
    const vector<json> bad{json(0), json(-1), json(1.5), json(numeric_limits<double>::quiet_NaN()), json(numeric_limits<double>::infinity()),
                           json(1.0e300), json(9223372036854775807LL), json("1"), json(nullptr)};
    for (const json& value : bad) {
        json block = productionBlock();
        block["gapTrucks"] = value;
        const string message = refusalOf(block);
        INFO("gap " << value.dump() << ": " << message);
        CHECK(message.find("gapTrucks") != string::npos);
    }
    json missing = productionBlock();
    missing.erase("gapTrucks");
    CHECK(refusalOf(missing).find("gapTrucks") != string::npos);
    m5Evidence::note("expected", {"G = 1 shipped; 2 accepted as a synthetic setting", "0, -1, 1.5, NaN, inf, 1e300, 2^63-1, text, null, missing refused"});
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

TEST_CASE("X-BLK-07/synthetic: allowance 100; block 1 ends at 60, repair at 85: only 15 remain, and nothing resets or extends it" * doctest::test_suite("m5")) {
    class StepClock final : public Clock {
    public:
        double nowSeconds() const override { return now; }
        double now = 0.0;
    } clock;
    Budget budget({100.0, nullopt, "FX-CLOCK"}, clock);
    budget.startPhase("block1");
    clock.now = 60.0;
    budget.startPhase("repair");
    clock.now = 85.0;
    CHECK(budget.remainingSeconds() == 15.0);
    // Every later slice (block, method, repair, final checks) ends at the lane-date deadline, never past it.
    for (const double share : {0.5, 1.0, 2.0}) CHECK(budget.sliceEnd(share) <= 100.0);
    budget.startPhase("finalChecks");
    CHECK(budget.remainingSeconds() == 15.0);
    clock.now = 99.999;
    CHECK_FALSE(budget.exhausted());
    clock.now = 100.0;
    CHECK(budget.exhausted());
    CHECK(budget.remainingSeconds() <= 0.0);
    // Production's allowance is secondsPerLoad x the frozen reserved count (CL-05), not a work count.
    const M5LaneOutcome outcome = m5Fixtures::plan(m5Fixtures::laneWith(m5Fixtures::fx30Units("u", 30), 7), m5Fixtures::syntheticProfile());
    CHECK(outcome.allowanceSeconds == 10.0 * 7);
    m5Evidence::note("expected", {"60 + 25 used, 15 left", "slices never past 100", "exhausted at 100", "production: 10 s x 7 reserved = 70 s"});
}

TEST_CASE("U-AC-04/zeroAndRange: x and y must be above zero and at most 1,000,000 lb and 100,000 cu ft" * doctest::test_suite("m5")) {
    json block = productionBlock();
    const auto readsAs = [&](const json& x, const json& y) {
        block["acceptance"] = {{"weightLb", x}, {"cubeCuFt", y}};
        const M5PolicyProfile profile = loadProfile(block);
        REQUIRE(profile.acceptance.has_value());
        CHECK(profile.acceptance->weightLb == x.get<double>());
        CHECK(profile.acceptance->cubeCuFt == y.get<double>());
    };
    readsAs(0.01, 0.01);
    readsAs(1'000'000, 100'000);
    // Above one 45,000 lb trailer's payload: the cube branch may still pass, so x is not refused.
    readsAs(50'000, 3'000);
    CHECK(evaluateAcceptance(20'000.0, 3'000.5 * 1728.0, NumericAcceptance{50'000.0, 3'000.0}).accepted);
    const double infinity = numeric_limits<double>::infinity(), notANumber = numeric_limits<double>::quiet_NaN();
    for (const pair<json, json>& bad :
         {pair<json, json>{0, 3000}, {40000, 0}, {0.0, 0.0}, {-0.01, 3000}, {40000, -1}, {1'000'000.01, 3000}, {40000, 100'000.01},
          {infinity, 3000}, {40000, notANumber}, {1e300, 3000}, {"40000", 3000}, {40000, nullptr}}) {
        block["acceptance"] = {{"weightLb", bad.first}, {"cubeCuFt", bad.second}};
        requireRejected(block, "acceptance " + block["acceptance"].dump());
    }
    m5Evidence::note("expected", {"zero refused", "x up to 1,000,000 lb", "y up to 100,000 cu ft"});
}

TEST_CASE("U-FL-03/zeroAndMaximumRange: the rear-inch limit runs from 0.01 in to the slot's interior length" * doctest::test_suite("m5")) {
    json block = productionBlock();
    for (const json& bad : {json(0), json(0.009), json(-0.01), json(numeric_limits<double>::infinity()),
                            json(numeric_limits<double>::quiet_NaN()), json("0.01")}) {
        block["fullness"]["rearInchLimit"] = bad;
        requireRejected(block, "rear-inch limit " + bad.dump());
    }
    // 30 FX-30 units stand 600 in deep on the 630 in floor: 30 in at the rear.
    const M5LaneDateInput input = m5Fixtures::laneWith(m5Fixtures::fx30Units("u", 30), 1);
    const auto planWithLimit = [&](double limit) {
        block["fullness"]["rearInchLimit"] = limit;
        M5PolicyProfile profile = m5Fixtures::syntheticProfile();
        profile.fullness = loadProfile(block).fullness;
        return m5Fixtures::plan(input, profile);
    };
    const auto fullByRear = [](const M5LaneOutcome& outcome) {
        REQUIRE(outcome.fullness.size() == 1);
        return outcome.fullness[0].byRearInches;
    };
    CHECK_FALSE(fullByRear(planWithLimit(0.01)));
    CHECK_FALSE(fullByRear(planWithLimit(29.99)));
    CHECK(fullByRear(planWithLimit(30.0)));
    CHECK(fullByRear(planWithLimit(630.0)));
    for (const double overLength : {630.01, 1e6}) {
        INFO("rear-inch limit " << overLength);
        const M5LaneOutcome refused = planWithLimit(overLength);
        CHECK(refused.kind == OutcomeKind::ConfigurationIncomplete);
        CHECK_FALSE(refused.selected.has_value());
        REQUIRE(refused.leftOver.size() == 30);
        for (const LeftOver& left : refused.leftOver) CHECK(left.cause == LeftOverCause::ConfigurationIncomplete);
    }
    m5Evidence::note("expected", {"below 0.01 refused at load", "30 in FULL at limit 30, not at 29.99", "630.01 refused at planning"});
}
