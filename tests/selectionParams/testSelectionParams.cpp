#include "doctest.h"
#include "paramsFields.hpp"
#include "selectionParams.hpp"

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace ob;
using json = nlohmann::json;

namespace {

const string kShippedParamsPath = "config/orderBuilderParams.json";

json shippedRoot() { return readParamsFile(kShippedParamsPath); }

// The shipped params with one change made to the "selection" block.
json edited(const function<void(json&)>& change) {
    json root = shippedRoot();
    change(root["selection"]);
    return root;
}

// Whether parsing throws a message that names `key`.
bool rejectedNaming(const json& root, const string& key) {
    try {
        parseSelectionParams(root);
    } catch (const runtime_error& error) {
        return string(error.what()).find(key) != string::npos;
    }
    return false;
}

} // anonymous namespace

TEST_CASE("selectionParams: the shipped params read in full") {
    const SelectionParams params = loadSelectionParams(kShippedParamsPath);
    CHECK(params.sourcePath == kShippedParamsPath);
    CHECK(params.reservations.defaultEquipment == "53F");
    CHECK(params.reservations.knownEquipment == vector<string>{"53F"});
    CHECK(params.selector.floor.fullCubeFraction == doctest::Approx(0.84));
    CHECK(params.maxUnitLoadsPerTruck == doctest::Approx(64.0));
    CHECK(params.routing.m5Threshold == 20);
    CHECK(params.selector.knapsack.nodeLimit == 200'000);
    CHECK(params.selector.binPacker.maxSearchItems == 100);
    CHECK(params.selector.floor.allowRotation);
    CHECK_FALSE(params.selector.floor.maxPositions);
    CHECK(params.selector.floor.allowedPatterns.size() == 6);
    CHECK(params.selector.threads == 4);
    CHECK(params.reportDetail == ReportDetail::Standard);
    CHECK_FALSE(params.selector.clock);
}

TEST_CASE("selectionParams: missingSettingFailsAtStartup - no block, and every key, named") {
    json noBlock = shippedRoot();
    noBlock.erase("selection");
    CHECK(rejectedNaming(noBlock, "selection"));
    for (const auto& setting : shippedRoot()["selection"].items()) {
        const string key = setting.key();
        CAPTURE(key);
        CHECK(rejectedNaming(edited([&](json& block) { block.erase(key); }), key));
    }
    for (const char* block : {"knapsack", "patternPacker", "binPacker", "floor"}) {
        for (const auto& setting : shippedRoot()["selection"][block].items()) {
            const string key = setting.key();
            CAPTURE(key);
            CHECK(rejectedNaming(edited([&](json& selection) { selection[block].erase(key); }), key));
        }
    }
}

TEST_CASE("selectionParams: everySettingRejectsZeroNegativeAndTheWrongType") {
    const vector<pair<vector<string>, bool>> numbers{
        {{"fullCubeFraction"}, false},          {{"maxUnitLoadsPerTruck"}, false},
        {{"varietyRatio"}, false},              {{"secondsPerLane"}, false},
        {{"knapsack", "weightGrainLb"}, false}, {{"knapsack", "areaGrainSqIn"}, false},
        {{"knapsack", "nodeLimit"}, true},      {{"patternPacker", "weightGrainLb"}, false},
        {{"patternPacker", "maxPatterns"}, true}, {{"patternPacker", "pricingNodeLimit"}, true},
        {{"binPacker", "areaGrainSqIn"}, false}, {{"binPacker", "columnRoundsPerNode"}, true},
        {{"binPacker", "branchNodeLimit"}, true}, {{"binPacker", "maxSearchItems"}, true},
        {{"threads"}, true}};
    for (const auto& number : numbers) {
        const vector<string> path = number.first;
        const bool isCount = number.second;
        const string key = path.back();
        CAPTURE(key);
        auto set = [&](const json& value) {
            return edited([&](json& block) { (path.size() == 1 ? block : block[path[0]])[key] = value; });
        };
        CHECK(rejectedNaming(set(0), key));
        CHECK(rejectedNaming(set(-1), key));
        CHECK(rejectedNaming(set("NaN"), key));
        CHECK(rejectedNaming(set(nullptr), key));
        if (isCount) CHECK(rejectedNaming(set(2.5), key));
    }
}

TEST_CASE("selectionParams: ranges, names and lists out of bounds are rejected") {
    CHECK(rejectedNaming(edited([](json& block) { block["fullCubeFraction"] = 1.5; }), "fullCubeFraction"));
    CHECK(rejectedNaming(edited([](json& block) { block["m5Threshold"] = 1; }), "m5Threshold"));
    CHECK(rejectedNaming(edited([](json& block) { block["varietyRatio"] = 5000; }), "varietyRatio"));
    CHECK(rejectedNaming(edited([](json& block) { block["threads"] = 300; }), "threads"));
    CHECK(rejectedNaming(edited([](json& block) { block["reportDetail"] = "Verbose"; }), "reportDetail"));
    CHECK(rejectedNaming(edited([](json& block) { block["defaultEquipment"] = "48F"; }), "defaultEquipment"));
    CHECK(rejectedNaming(edited([](json& block) { block["defaultEquipment"] = ""; }), "defaultEquipment"));
    CHECK(rejectedNaming(edited([](json& block) { block["knownEquipment"] = json::array({"53F", "53F"}); }), "twice"));
    CHECK(rejectedNaming(edited([](json& block) { block["knownEquipment"] = json::array(); }), "knownEquipment"));
    CHECK(rejectedNaming(edited([](json& block) { block["floor"]["fixedOrientation"] = "Sideways"; }), "fixedOrientation"));
    CHECK(rejectedNaming(edited([](json& block) { block["floor"]["allowRotation"] = "yes"; }), "allowRotation"));
    CHECK(rejectedNaming(edited([](json& block) { block["floor"]["maxPositions"] = 0; }), "maxPositions"));
    CHECK(rejectedNaming(edited([](json& block) { block["floor"]["allowedPatterns"] = json::array({"WideWide"}); }), "Single"));
    CHECK(rejectedNaming(edited([](json& block) { block["floor"]["allowedPatterns"] = json::array({"Single", "Single"}); }), "twice"));
    CHECK(rejectedNaming(edited([](json& block) { block["floor"]["allowedPatterns"] = json::array({"Single", "Diagonal"}); }), "Diagonal"));
    CHECK(rejectedNaming(edited([](json& block) { block["floor"]["allowedPatterns"] = json::array(); }), "allowedPatterns"));
    CHECK(rejectedNaming(edited([](json& block) { block = json::array(); }), "selection"));
}

TEST_CASE("selectionParams: a cap on positions and a fixed orientation are read when given") {
    const SelectionParams params = parseSelectionParams(edited([](json& block) {
        block["floor"]["maxPositions"] = 30;
        block["floor"]["allowRotation"] = false;
        block["floor"]["fixedOrientation"] = "ShortSideAcross";
        block["reportDetail"] = "Detailed";
    }));
    REQUIRE(params.selector.floor.maxPositions);
    CHECK(*params.selector.floor.maxPositions == 30);
    CHECK_FALSE(params.selector.floor.allowRotation);
    CHECK(params.selector.floor.fixedOrientation == FixedOrientation::ShortSideAcross);
    CHECK(params.reportDetail == ReportDetail::Detailed);
}
