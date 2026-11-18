// M5 on the real 4-6 August extracts with the shipped params (production profile: acceptance OFF,
// 14 in rear limit, date first, can-go fill on, 1 s per load). Confidential and gitignored, so the
// full run skips this suite; the m5RealData ctest entry runs it with --no-skip, where a missing
// file fails by name rather than passing.
#include "doctest.h"
#include "../importer/crossDayFixtures.hpp"
#include "../m4AugustFixtures.hpp"
#include "../runTempFolder.hpp"
#include "m5Integration.hpp"
#include "m5Report.hpp"
#include "paramsFields.hpp"
#include "selectionParams.hpp"
#include "pipeline.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
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
        const filesystem::path path = testTemp::runTempFolder() / "obM5RealData_paramsWithoutM5.json";
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

// The production profile as the shipped params give it.
m5::M5PolicyProfile shippedProfile() {
    return m5::loadProfile(readParamsFile(crossDayTests::kStrictParamsPath).at("m5"));
}

m5::DateRules shippedDateRules(const PipelineResult& run) {
    return {run.selectionParams.shipDates.pullForward, run.selectionParams.maxDaysPulledForward};
}

// One lane's dates gathered from a run's classification, as M4 handed them over, for the labelled
// benchmark. Dates with no route on the lane are skipped.
vector<m5::M5LaneDateInput> laneDates(const PipelineResult& run, const m5::LaneKey& lane, const vector<string>& dates) {
    vector<m5::M5LaneDateInput> inputs;
    for (const string& date : dates) {
        const optional<LargeLaneRequest> request = m5::forcedRequest(run, run.laneClassification, lane, date);
        if (!request) continue;
        inputs.push_back(m5::gatherLaneDate(*request, run, run.selectionItems, shippedDateRules(run), nullopt,
                                                  run.selectionParams.selector.maxUnitLoadsPerTruck));
    }
    return inputs;
}

double unitLoadsShipped(const m5::M5LaneOutcome& outcome, const m5::M5LaneDateInput& input) {
    double shipped = 0.0;
    if (!outcome.selected) return shipped;
    for (const m5::TruckPlan& truck : outcome.selected->trucks) {
        for (const size_t atom : truck.atoms) shipped += input.atoms[atom].unitLoads;
    }
    return shipped;
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

TEST_CASE("U-RT-08/existingTest: the benchmark asserts the chosen threshold; the shipped 20 routes exactly the lane-dates above it" * doctest::skip()) {
    // Code review: the backwards asserts once near lines 552 and 554 of testM4Benchmark.cpp ("no rule is
    // supportable") are gone; the benchmark now checks the shipped threshold against the data.
    string benchmarkSource;
    {
        ifstream file("tests/benchmarkHarness/testM4Benchmark.cpp", ios::binary);
        REQUIRE(file);
        benchmarkSource.assign(istreambuf_iterator<char>(file), istreambuf_iterator<char>());
    }
    CHECK(benchmarkSource.find("supportable") == string::npos);
    CHECK(benchmarkSource.find("CHECK(shipped.m5Threshold < *overBudget);") != string::npos);
    REQUIRE(m4AugustTests::augustPresent());
    CHECK(loadSelectionParams(crossDayTests::kStrictParamsPath).routing.m5Threshold == 20);
    for (size_t runIndex = 0; runIndex < 3; ++runIndex) {
        for (const LaneRoute& route : withM5(runIndex).laneClassification.routes) {
            CHECK((route.method == SolveMethod::M5) == (!route.itemIndices.empty() && route.reservedTrucks > 20));
        }
    }
}

TEST_CASE("RD-03 X-REPORT-01 Aug5 7 Aug: at most 21 trucks, shipped and left shown, reserved never 38" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    const PipelineResult& run = withM5(1);
    const string& text = run.selectionPlan.largeLanes.front().text;
    CHECK(text.find("reserved: 21\n") != string::npos);
    CHECK(text.find("reserved: 38") == string::npos);
    CHECK(text.find("unit loads in / shipped / of them can-go") != string::npos);
    CHECK(text.find("left: over the reservation") != string::npos);
    CHECK(text.find("FULL trucks:") != string::npos);
    MESSAGE("7 Aug, 2028 -> 2574: M5 built 21 against the client's historical 18");
}

TEST_CASE("RD-04 RD-13 7 and 8 Aug of 2028 -> 2574 reconcile item by item across both dates" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    const PipelineResult& run = withM5(1);
    for (const LaneRoute& route : run.laneClassification.routes) {
        if (onLargeLane(run, route) && route.shipDate == "2026-08-08") CHECK(route.method != SolveMethod::M5);
    }
    CHECK(run.selectionViolations.empty());
    // Every item of the lane that reached either date ends exactly once, on a truck or left over.
    vector<int> ends(run.selectionItems.items.size(), 0);
    for (const PlannedTruck& truck : run.selectionPlan.trucks) {
        for (const size_t index : truck.items) ++ends[index];
    }
    for (const LeftOverItem& left : run.selectionPlan.leftOver) ++ends[left.itemIndex];
    for (const LaneRoute& route : run.laneClassification.routes) {
        if (!onLargeLane(run, route)) continue;
        for (const size_t index : route.itemIndices) CHECK(ends[index] == 1);
    }
}

TEST_CASE("RD-11 reservations reconcile: reserved = built + unused, M5's lane within its 21" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    for (size_t runIndex = 0; runIndex < 3; ++runIndex) {
        const PipelineResult& run = withM5(runIndex);
        long long reserved = 0, built = 0, unused = 0;
        for (const ReservationUse& use : run.selectionPlan.reservations) {
            reserved += use.reserved;
            built += use.built;
            unused += use.unused();
            CHECK(use.built <= use.reserved);
        }
        CHECK(reserved == built + unused);
        CHECK(reserved == run.reservationBook.reservedTrucks);
        MESSAGE("run " << runIndex << ": " << reserved << " reserved, " << built << " built, " << unused << " unused");
    }
}

TEST_CASE("RD-12 RD-10 M5's trucks report fullness reasons and rear-inch averages per bucket" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    const string& text = withM5(1).selectionPlan.largeLanes.front().text;
    CHECK(text.find("by weight") != string::npos);
    CHECK(text.find("by rear inches") != string::npos);
    CHECK(text.find("floor-limited") != string::npos);
    CHECK(text.find("average rear inches, goods remain") != string::npos);
}

TEST_CASE("ACC-2 RD-06 X-E2E-01 X-REPORT-02 the labelled both-dates benchmark keeps 21 and 20 and commits nothing"
          * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    const PipelineResult& production = withM5(1);
    const size_t trucksBefore = production.selectionPlan.trucks.size();
    // Fresh hand-overs: the run without M5 leaves both dates as M4 gave them.
    const vector<m5::M5LaneDateInput> dates = laneDates(aug5WithoutM5(), {"2028", "2574", "TL"}, {"2026-08-07", "2026-08-08"});
    REQUIRE(dates.size() == 2);
    CHECK(dates[0].pool.slots.size() == 21);
    CHECK(dates[1].pool.slots.size() == 20);
    const m5::FunctionClock clock(nullptr);
    const m5::LinkedDateBenchmark benchmark =
        m5::benchmarkLinkedDates(dates, shippedProfile(), m5::physicalSettingsOf(production.selectionParams.selector), clock);
    REQUIRE(benchmark.outcomes.size() == 2);
    size_t trucks = 0;
    for (size_t date = 0; date < 2; ++date) {
        const m5::M5LaneOutcome& outcome = benchmark.outcomes[date];
        CHECK(outcome.kind == m5::OutcomeKind::CompletedWithDispatch);
        CHECK(outcome.reservedTrucks == static_cast<long long>(dates[date].pool.slots.size()));
        CHECK(outcome.timing.elapsedSeconds <= outcome.allowanceSeconds);
        trucks += outcome.selected->trucks.size();
    }
    CHECK(trucks <= 41);
    CHECK(benchmark.text.find("BENCHMARK") != string::npos);
    CHECK(production.selectionPlan.largeLanes.front().text.find("scope: PRODUCTION") != string::npos);
    CHECK(production.selectionPlan.trucks.size() == trucksBefore);
    MESSAGE("both-dates benchmark: " << trucks << " trucks against the client's historical 38");
}

TEST_CASE("U-PF-02 X-RUN-10 M4, A and B on the same goods, in either order" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    const PipelineResult& baseline = aug5WithoutM5();
    const vector<m5::M5LaneDateInput> sevenAug = laneDates(baseline, {"2028", "2574", "TL"}, {"2026-08-07"});
    REQUIRE(sevenAug.size() == 1);
    const m5::PhysicalSettings physical = m5::physicalSettingsOf(baseline.selectionParams.selector);
    const m5::FunctionClock clock(nullptr);
    auto planOnly = [&](m5::MethodPolicy policy) {
        m5::M5PolicyProfile profile = shippedProfile();
        profile.method = policy;
        const m5::M5RunServices services(clock);
        return m5::planLaneDate(sevenAug[0], profile, physical, services);
    };
    const m5::M5LaneOutcome firstA = planOnly(m5::MethodPolicy::PatternOnly), firstB = planOnly(m5::MethodPolicy::BlocksOnly);
    const m5::M5LaneOutcome secondB = planOnly(m5::MethodPolicy::BlocksOnly), secondA = planOnly(m5::MethodPolicy::PatternOnly);
    CHECK(unitLoadsShipped(firstA, sevenAug[0]) == unitLoadsShipped(secondA, sevenAug[0]));
    CHECK(unitLoadsShipped(firstB, sevenAug[0]) == unitLoadsShipped(secondB, sevenAug[0]));
    CHECK(firstA.selected->trucks.size() <= 21);
    CHECK(firstB.selected->trucks.size() <= 21);
    MESSAGE("7 Aug, 21 reserved: A " << unitLoadsShipped(firstA, sevenAug[0]) << " unit loads in " << firstA.timing.elapsedSeconds
                                     << " s; B " << unitLoadsShipped(firstB, sevenAug[0]) << " in " << firstB.timing.elapsedSeconds
                                     << " s; M4 does not plan it (escalated)");
}

TEST_CASE("RD-07 Aug6 2028 -> 2190, both dates joined, completes and is reported" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    const PipelineResult& run = withM5(2);
    vector<string> dates;
    m5::LaneKey lane{"2028", "2190", ""};
    for (const Reservation& reservation : run.reservationBook.reservations) {
        if (reservation.origin != "2028" || reservation.destination != "2190") continue;
        lane.shipCondition = reservation.shipCondition;
        dates.push_back(reservation.shipDate);
    }
    REQUIRE_FALSE(dates.empty());
    const vector<m5::M5LaneDateInput> inputs = laneDates(run, lane, dates);
    REQUIRE_FALSE(inputs.empty());
    const m5::FunctionClock clock(nullptr);
    const m5::LinkedDateBenchmark benchmark =
        m5::benchmarkLinkedDates(inputs, shippedProfile(), m5::physicalSettingsOf(run.selectionParams.selector), clock);
    size_t trucks = 0;
    for (const m5::M5LaneOutcome& outcome : benchmark.outcomes) {
        CHECK((outcome.kind == m5::OutcomeKind::CompletedWithDispatch || outcome.kind == m5::OutcomeKind::CompletedNoEligibleInput));
        if (outcome.selected) trucks += outcome.selected->trucks.size();
    }
    CHECK_FALSE(benchmark.text.empty());
    MESSAGE("Aug6 2028 -> 2190 over " << inputs.size() << " dates: " << trucks << " trucks against the historical 23");
}

TEST_CASE("X-REPORT-03 the report never calls the lane infeasible without a certificate" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    const string& text = withM5(1).selectionPlan.largeLanes.front().text;
    CHECK(text.find("infeasible") == string::npos);
    CHECK(text.find("635") == string::npos);
}

}  // TEST_SUITE
