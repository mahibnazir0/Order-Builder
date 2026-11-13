// M5 on the real 4-6 August extracts with the shipped params (production profile: acceptance OFF,
// 14 in rear limit, date first, can-go fill on, 1 s per load). Confidential and gitignored, so the
// full run skips this suite; the m5RealData ctest entry runs it with --no-skip, where a missing
// file fails by name rather than passing.
#include "doctest.h"
#include "../importer/crossDayFixtures.hpp"
#include "../m4AugustFixtures.hpp"
#include "paramsFields.hpp"
#include "pipeline.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <tuple>

using namespace std;
using namespace ob;

namespace {

const string kLargeLaneDate = "2026-08-07";

PipelineInputs augustInputs(size_t runIndex, const string& paramsPath) {
    PipelineInputs inputs;
    inputs.product_path = m4AugustTests::kAugustProduct;
    inputs.demand_path = m4AugustTests::kAugustRunFiles[runIndex].demandPath;
    inputs.placeholder_path = m4AugustTests::kAugustRunFiles[runIndex].placeholderPath;
    inputs.paramsPath = paramsPath;
    inputs.palletPath = crossDayTests::kPalletTableForOlderMasters;
    inputs.layerSizePath = crossDayTests::kLayerSizesForOlderMasters;
    inputs.runSelection = true;
    return inputs;
}

const PipelineResult& withM5(size_t runIndex) {
    static const array<PipelineResult, 3> runs = [] {
        array<PipelineResult, 3> loaded;
        for (size_t index = 0; index < loaded.size(); ++index) loaded[index] = Pipeline::run(augustInputs(index, crossDayTests::kStrictParamsPath));
        return loaded;
    }();
    return runs[runIndex];
}

// Aug5 with the shipped params less their "m5" block: M4 as it was, the large lane escalated.
const PipelineResult& aug5WithoutM5() {
    static const PipelineResult run = [] {
        nlohmann::json root = readParamsFile(crossDayTests::kStrictParamsPath);
        root.erase("m5");
        const filesystem::path path = filesystem::temp_directory_path() / "obM5RealData_paramsWithoutM5.json";
        ofstream(path) << root.dump(2);
        return Pipeline::run(augustInputs(1, path.string()));
    }();
    return run;
}

bool onLargeLane(const PipelineResult& run, const LaneRoute& route) {
    const GroupKey& key = run.segregation.groups[route.groupIndex].key;
    return key.locationFrom == "2028" && key.locationTo == "2574" && key.shipCondition == "TL";
}

using TruckKey = tuple<string, string, string, string, size_t>;  // lane, date, group

map<TruckKey, multiset<vector<size_t>>> trucksAwayFromLargeLane(const PipelineResult& run) {
    map<TruckKey, multiset<vector<size_t>>> trucks;
    for (const PlannedTruck& truck : run.selectionPlan.trucks) {
        const LaneRoute& route = run.laneClassification.routes[truck.routeIndex];
        if (onLargeLane(run, route)) continue;
        const GroupKey& key = run.segregation.groups[route.groupIndex].key;
        trucks[{key.locationFrom, key.locationTo, key.shipCondition, truck.shipDate, route.groupIndex}].insert(truck.items);
    }
    return trucks;
}

}  // namespace

TEST_SUITE("m5RealData") {

TEST_CASE("m5RealData: every August extract M5 reads is present" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
}

TEST_CASE("RD-01 RD-02 X-E2E-01 Aug5: only 2028 -> 2574 on 7 Aug goes to M5, with the 747-stack hand-over"
          * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    const PipelineResult& run = withM5(1);
    REQUIRE(run.selectionPlan.largeLanes.size() == 1);
    const LargeLaneReport& lane = run.selectionPlan.largeLanes.front();
    CHECK(lane.origin == "2028");
    CHECK(lane.destination == "2574");
    CHECK(lane.shipDate == kLargeLaneDate);
    CHECK(lane.planned);
    size_t requiredStacks = 0, built = 0;
    double requiredUnitLoads = 0.0;
    for (size_t routeIndex = 0; routeIndex < run.laneClassification.routes.size(); ++routeIndex) {
        const LaneRoute& route = run.laneClassification.routes[routeIndex];
        if (route.method != SolveMethod::M5) continue;
        CHECK(onLargeLane(run, route));
        CHECK(route.shipDate == kLargeLaneDate);
        CHECK(route.reservedTrucks == 21);
        built += static_cast<size_t>(run.selectionPlan.routes[routeIndex].trucksBuilt);
        for (const size_t index : route.itemIndices) {
            const SelectionItem& item = run.selectionItems.items[index];
            if (item.shipDate > route.shipDate) continue;
            ++requiredStacks;
            requiredUnitLoads += item.unitLoads;
        }
    }
    CHECK(requiredStacks == 747);
    CHECK(requiredUnitLoads == doctest::Approx(763.0));
    CHECK(built == 21);
    CHECK(run.selectionViolations.empty());
    for (const LeftOverItem& left : run.selectionPlan.leftOver) {
        CHECK(left.reason != LeftOverReason::EscalatedToM5);
        CHECK(left.reason != LeftOverReason::M5NoResult);
    }
}

TEST_CASE("RD-10 Aug5: every dispatched M5 truck carries a fullness label and bucket" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    const string& text = withM5(1).selectionPlan.largeLanes.front().text;
    CHECK(text.find("minimum-load check: at export, after M5") != string::npos);
    CHECK(text.find("trucks loaded: 21") != string::npos);
    size_t labelled = 0;
    for (size_t found = text.find(" goods remain\n"); found != string::npos; found = text.find(" goods remain\n", found + 1)) ++labelled;
    for (size_t found = text.find(" rule-limited\n"); found != string::npos; found = text.find(" rule-limited\n", found + 1)) ++labelled;
    CHECK(labelled == 21);
}

TEST_CASE("U-PB-12 Aug5: products 105666801 and 105446303 each fill trucks of their own" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    const PipelineResult& run = withM5(1);
    map<string, size_t> singleProductTrucks;
    for (const PlannedTruck& truck : run.selectionPlan.trucks) {
        if (!onLargeLane(run, run.laneClassification.routes[truck.routeIndex]) || truck.shipDate != kLargeLaneDate) continue;
        set<string> products;
        for (const size_t index : truck.items) products.insert(run.selectionItems.items[index].productNumber);
        if (products.size() == 1) ++singleProductTrucks[*products.begin()];
    }
    CHECK(singleProductTrucks["105666801"] >= 1);
    CHECK(singleProductTrucks["105446303"] >= 1);
}

TEST_CASE("RD-09 INV-16 Aug5: every lane M5 does not touch plans exactly as without M5" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    CHECK(trucksAwayFromLargeLane(withM5(1)) == trucksAwayFromLargeLane(aug5WithoutM5()));
}

TEST_CASE("RD-05 Aug4 and Aug6 have no lane-date above the threshold, so M5 plans nothing" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    CHECK(withM5(0).selectionPlan.largeLanes.empty());
    CHECK(withM5(2).selectionPlan.largeLanes.empty());
}

}  // TEST_SUITE
