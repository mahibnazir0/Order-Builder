// M5 on the real 4-6 August extracts with the shipped params (production profile: acceptance OFF,
// 14 in rear limit, date first, can-go fill on, 1 s per load). Confidential and gitignored, so the
// full run skips this suite; the m5RealData ctest entry runs it with --no-skip. Where the extracts are
// absent (a fresh clone) each test skips itself by name and its variants are recorded BLOCKED.
#include "doctest.h"
#include "../importer/crossDayFixtures.hpp"
#include "../m4AugustFixtures.hpp"
#include "../runTempFolder.hpp"
#include "m5Integration.hpp"
#include "m5Report.hpp"
#include "paramsFields.hpp"
#include "selectionParams.hpp"
#include "support/m5Evidence.hpp"
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

const char* kTomReferencePath = "tests/m5/data/tomReference.local.json";

// Tom's figures live in a local, git-ignored file. A test that compares with them is skipped by name,
// and recorded BLOCKED with the reason, when the file is not present; it never fails for that.
// Null when the file is missing: the caller returns at once.
nlohmann::json tomReferenceOrSkip(const string& testName) {
    ifstream file(kTomReferencePath);
    if (!file.good()) {
        const string reason = string("skipped: ") + kTomReferencePath + " is not present (Tom's reference figures, git-ignored)";
        m5Evidence::blocked(reason);
        MESSAGE("SKIPPED " << testName << " - " << reason);
        return nlohmann::json();
    }
    return nlohmann::json::parse(file);
}

// The customer's August extracts are kept out of git by design, so a fresh clone does not hold them. A real-data test then
// skips itself by name: its variants are recorded BLOCKED with this reason, the message names the test, and it never fails.
const char* kCustomerDataAbsent = "customer data not present on this machine (kept out of git by design)";

// True when the 4-6 August extracts are here. When they are not, records the test BLOCKED and says it was skipped; the
// caller returns at once.
bool augustOrSkip() {
    if (m4AugustTests::augustPresent()) return true;
    m5Evidence::blocked(kCustomerDataAbsent);
    MESSAGE("SKIPPED " << m5Evidence::currentTestCase() << " - " << kCustomerDataAbsent);
    return false;
}

}  // namespace

TEST_SUITE("m5RealData") {

TEST_CASE("m5RealData: every August extract M5 reads is present" * doctest::skip()) {
    if (!augustOrSkip()) return;
    CHECK(m4AugustTests::augustPresent());
}

TEST_CASE("RD-01/realData RD-02/realData ACC-2/production7Aug ACC-2/productionProfile Aug5: only 2028 -> 2574 on 7 Aug goes to M5, 21 reserved, with the 747-stack hand-over"
          * doctest::skip()) {
    if (!augustOrSkip()) return;
    const PipelineResult& run = withM5(1);
    REQUIRE(run.selectionPlan.largeLanes.size() == 1);
    const LargeLaneReport& lane = run.selectionPlan.largeLanes.front();
    CHECK(lane.origin == "2028");
    CHECK(lane.destination == "2574");
    CHECK(lane.shipDate == kLargeLaneDate);
    CHECK(lane.planned);
    size_t requiredStacks = 0, built = 0, canGoStacks = 0;
    double requiredUnitLoads = 0.0, canGoUnitLoads = 0.0;
    map<string, double> unitLoadsBySegregant;  // the three groups of the hand-over: S01, S03, S45
    for (size_t routeIndex = 0; routeIndex < run.laneClassification.routes.size(); ++routeIndex) {
        const LaneRoute& route = run.laneClassification.routes[routeIndex];
        if (route.method != SolveMethod::M5) continue;
        CHECK(onLargeLane(run, route));
        CHECK(route.shipDate == kLargeLaneDate);
        CHECK(route.reservedTrucks == 21);
        built += static_cast<size_t>(run.selectionPlan.routes[routeIndex].trucksBuilt);
        for (const size_t index : route.itemIndices) {
            const SelectionItem& item = run.selectionItems.items[index];
            if (item.shipDate > route.shipDate) {
                ++canGoStacks;
                canGoUnitLoads += item.unitLoads;
                continue;
            }
            ++requiredStacks;
            requiredUnitLoads += item.unitLoads;
            unitLoadsBySegregant[run.segregation.groups[route.groupIndex].key.segregant] += item.unitLoads;
        }
    }
    CHECK(requiredStacks == 747);
    CHECK(requiredUnitLoads == doctest::Approx(763.0));
    CHECK(built == 21);
    // RD-02: the 763 unit loads split S01 = 506, S03 = 210, S45 = 47 (reproduced at 7cef94e, e51d9c4, f9d9945).
    CHECK(unitLoadsBySegregant.size() == 3);
    CHECK(unitLoadsBySegregant["S01"] == doctest::Approx(506.0));
    CHECK(unitLoadsBySegregant["S03"] == doctest::Approx(210.0));
    CHECK(unitLoadsBySegregant["S45"] == doctest::Approx(47.0));
    // The can-go goods the hand-over gains with can-go fill on are recorded apart from the 763.
    m5Evidence::note("actual", {{"stacks", requiredStacks}, {"unitLoads", requiredUnitLoads}, {"unitLoadsBySegregant", unitLoadsBySegregant},
                                {"canGoStacks", canGoStacks}, {"canGoUnitLoads", canGoUnitLoads}});
    MESSAGE("RD-02 split " << unitLoadsBySegregant["S01"] << " / " << unitLoadsBySegregant["S03"] << " / " << unitLoadsBySegregant["S45"]
                           << "; can-go " << canGoStacks << " stacks, " << canGoUnitLoads << " unit loads");
    CHECK(run.selectionViolations.empty());
    for (const LeftOverItem& left : run.selectionPlan.leftOver) {
        CHECK(left.reason != LeftOverReason::EscalatedToM5);
        CHECK(left.reason != LeftOverReason::M5NoResult);
    }
}

TEST_CASE("RD-10/realData Aug5: every dispatched M5 truck carries a fullness label and bucket" * doctest::skip()) {
    if (!augustOrSkip()) return;
    const string& text = withM5(1).selectionPlan.largeLanes.front().text;
    CHECK(text.find("minimum-load check: at export, after M5") != string::npos);
    CHECK(text.find("trucks loaded: 21") != string::npos);
    size_t labelled = 0;
    for (size_t found = text.find(" goods remain\n"); found != string::npos; found = text.find(" goods remain\n", found + 1)) ++labelled;
    for (size_t found = text.find(" rule-limited\n"); found != string::npos; found = text.find(" rule-limited\n", found + 1)) ++labelled;
    CHECK(labelled == 21);
    m5Evidence::note("notCovered", "M4's own average rear inches per bucket (f9d9945) beside M5's are not produced: M4 does not label buckets; the chart comparison is U-RP-07");
}

TEST_CASE("U-PB-12 Aug5: products 105666801 and 105446303 each fill trucks of their own" * doctest::skip()) {
    if (!augustOrSkip()) return;
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

TEST_CASE("RD-09/realData INV-16 Aug5: every lane M5 does not touch plans exactly as without M5" * doctest::skip()) {
    if (!augustOrSkip()) return;
    CHECK(trucksAwayFromLargeLane(withM5(1)) == trucksAwayFromLargeLane(aug5WithoutM5()));
}

TEST_CASE("RD-05/realData Aug4 and Aug6 have no lane-date above the threshold, so M5 plans nothing" * doctest::skip()) {
    if (!augustOrSkip()) return;
    CHECK(withM5(0).selectionPlan.largeLanes.empty());
    CHECK(withM5(2).selectionPlan.largeLanes.empty());
}

TEST_CASE("RD-08/realData Aug4 raw-material lanes (TL_N102) are never planned by M5" * doctest::skip()) {
    if (!augustOrSkip()) return;
    const PipelineResult& run = withM5(0);
    size_t rawMaterialRoutes = 0;
    for (const LaneRoute& route : run.laneClassification.routes) {
        const GroupKey& key = run.segregation.groups[route.groupIndex].key;
        if (key.shipCondition != "TL_N102") continue;
        ++rawMaterialRoutes;
        CHECK(route.method != SolveMethod::M5);
    }
    // Raw-material lines (ship condition TL_N102) exist in the extract and are held on no M5 route: no route carries that
    // condition, so none can be escalated to M5.
    size_t rawMaterialLines = 0;
    for (const STRRecord& line : run.demand.str) rawMaterialLines += line.ship_cond == "TL_N102" ? 1 : 0;
    INFO("raw-material demand lines on Aug4: " << rawMaterialLines);
    CHECK(rawMaterialRoutes == 0);
    if (rawMaterialLines == 0) m5Evidence::note("notCovered", "the Aug4 extract held here has no TL_N102 raw-material lines, so there is no raw-material lane to show is left alone");
    CHECK(run.selectionPlan.largeLanes.empty());
    for (const PlannedTruck& truck : run.selectionPlan.trucks) {
        const GroupKey& key = run.segregation.groups[run.laneClassification.routes[truck.routeIndex].groupIndex].key;
        if (key.shipCondition == "TL_N102") CHECK(run.laneClassification.routes[truck.routeIndex].method != SolveMethod::M5);
    }
}

TEST_CASE("U-RT-08/existingTest: the benchmark asserts the chosen threshold; the shipped 20 routes exactly the lane-dates above it" * doctest::skip()) {
    if (!augustOrSkip()) return;
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
    CHECK(loadSelectionParams(crossDayTests::kStrictParamsPath).routing.m5Threshold == 20);
    for (size_t runIndex = 0; runIndex < 3; ++runIndex) {
        for (const LaneRoute& route : withM5(runIndex).laneClassification.routes) {
            CHECK((route.method == SolveMethod::M5) == (!route.itemIndices.empty() && route.reservedTrucks > 20));
        }
    }
}

TEST_CASE("RD-03/productionProfile X-REPORT-01/realRun Aug5 7 Aug: at most 21 trucks, shipped and left shown, reserved never 38, compared with the client's historical count" * doctest::skip()) {
    if (!augustOrSkip()) return;
    const nlohmann::json reference = tomReferenceOrSkip("RD-03 and X-REPORT-01 realRun");
    if (reference.is_null()) return;
    const PipelineResult& run = withM5(1);
    const string& text = run.selectionPlan.largeLanes.front().text;
    CHECK(text.find("reserved: 21\n") != string::npos);
    CHECK(text.find("reserved: 38") == string::npos);
    CHECK(text.find("unit loads in / shipped / of them can-go") != string::npos);
    CHECK(text.find("left: over the reservation") != string::npos);
    CHECK(text.find("FULL trucks:") != string::npos);
    const int historical = reference.at("clientHistoricalTrucks").at("aug5SevenAug").get<int>();
    size_t builtTrucks = 0;
    for (const PlannedTruck& truck : run.selectionPlan.trucks) {
        if (onLargeLane(run, run.laneClassification.routes[truck.routeIndex]) && truck.shipDate == kLargeLaneDate) ++builtTrucks;
    }
    CHECK(builtTrucks <= 21);
    CHECK(text.find("reserved: " + to_string(reference.at("clientHistoricalTrucks").at("aug5BothDates").get<int>())) == string::npos);
    MESSAGE("7 Aug, 2028 -> 2574: M5 built " << builtTrucks << " against the client's historical " << historical << " (shipped and left are in the report above)");
}

// The 8 Aug lane-date of 2028 -> 2574 as one run planned it: its reservations, the trucks M4 built
// there and the unit loads of the goods offered to it (ship date up to 8 Aug).
struct EighthAugust {
    int routes = 0;
    bool everyRouteReserves20 = true;  // the lane-date's 20 reserved trucks, carried by each of its routes
    long long built = 0;
    double offeredUnitLoads = 0.0;  // everything the route holds
    double dueUnitLoads = 0.0;      // of it, ship date up to 8 Aug
    double shippedUnitLoads = 0.0;
    bool allPlannedByM4 = true;
};

EighthAugust eighthAugustOf(const PipelineResult& run) {
    EighthAugust result;
    for (size_t routeIndex = 0; routeIndex < run.laneClassification.routes.size(); ++routeIndex) {
        const LaneRoute& route = run.laneClassification.routes[routeIndex];
        if (!onLargeLane(run, route) || route.shipDate != "2026-08-08") continue;
        ++result.routes;
        result.everyRouteReserves20 = result.everyRouteReserves20 && route.reservedTrucks == 20;
        result.built += run.selectionPlan.routes[routeIndex].trucksBuilt;
        result.allPlannedByM4 = result.allPlannedByM4 && route.method != SolveMethod::M5;
        for (const size_t index : route.itemIndices) {
            const SelectionItem& item = run.selectionItems.items[index];
            result.offeredUnitLoads += item.unitLoads;
            if (item.shipDate <= route.shipDate) result.dueUnitLoads += item.unitLoads;
        }
    }
    for (const PlannedTruck& truck : run.selectionPlan.trucks) {
        if (!onLargeLane(run, run.laneClassification.routes[truck.routeIndex]) || truck.shipDate != "2026-08-08") continue;
        for (const size_t index : truck.items) result.shippedUnitLoads += run.selectionItems.items[index].unitLoads;
    }
    return result;
}

TEST_CASE("RD-04/realData RD-13 Aug5 8 Aug of 2028 -> 2574: M4 plans it with its 20 reservations, and the lane reconciles item by item across both dates" * doctest::skip()) {
    if (!augustOrSkip()) return;
    const PipelineResult& run = withM5(1);
    const EighthAugust eighth = eighthAugustOf(run);
    const EighthAugust baseline = eighthAugustOf(aug5WithoutM5());
    // M4 as it was (the shipped params without "m5"): the baseline of 7cef94e and f9d9945.
    CHECK(baseline.routes > 0);
    CHECK(baseline.everyRouteReserves20);
    CHECK(baseline.built == 20);
    CHECK(baseline.offeredUnitLoads == doctest::Approx(763.0));
    CHECK(baseline.shippedUnitLoads == doctest::Approx(678.0));
    // With M5 on: still planned by M4 with 20 reservations; carry and can-go change what it is offered.
    CHECK(eighth.routes == baseline.routes);
    CHECK(eighth.everyRouteReserves20);
    CHECK(eighth.allPlannedByM4);
    CHECK(eighth.built == 20);
    CHECK(eighth.shippedUnitLoads <= eighth.offeredUnitLoads);
    MESSAGE("RD-04 8 Aug of 2028 -> 2574, M5 off: built " << baseline.built << ", " << baseline.shippedUnitLoads << " of " << baseline.offeredUnitLoads
                                                          << " offered; M5 on: built " << eighth.built << ", " << eighth.shippedUnitLoads << " of "
                                                          << eighth.offeredUnitLoads << " offered");
    for (const LaneRoute& route : run.laneClassification.routes) {
        if (onLargeLane(run, route) && route.shipDate == "2026-08-08") CHECK(route.method != SolveMethod::M5);
    }
    CHECK(run.selectionViolations.empty());
    // Every item of the lane that reached either date ends exactly once, on a truck or left over, and
    // the unit loads shipped plus left over equal the unit loads of those items.
    vector<int> ends(run.selectionItems.items.size(), 0);
    double shipped = 0.0, leftOver = 0.0, laneUnitLoads = 0.0;
    for (const PlannedTruck& truck : run.selectionPlan.trucks) {
        for (const size_t index : truck.items) ++ends[index];
    }
    for (const LeftOverItem& left : run.selectionPlan.leftOver) ++ends[left.itemIndex];
    set<size_t> laneItems;
    for (const LaneRoute& route : run.laneClassification.routes) {
        if (!onLargeLane(run, route)) continue;
        for (const size_t index : route.itemIndices) laneItems.insert(index);
    }
    for (const size_t index : laneItems) {
        CHECK(ends[index] == 1);
        laneUnitLoads += run.selectionItems.items[index].unitLoads;
    }
    for (const PlannedTruck& truck : run.selectionPlan.trucks) {
        if (!onLargeLane(run, run.laneClassification.routes[truck.routeIndex])) continue;
        for (const size_t index : truck.items) shipped += run.selectionItems.items[index].unitLoads;
    }
    for (const LeftOverItem& left : run.selectionPlan.leftOver) {
        if (laneItems.count(left.itemIndex)) leftOver += run.selectionItems.items[left.itemIndex].unitLoads;
    }
    CHECK(laneItems.size() > 0);
    CHECK(shipped + leftOver == doctest::Approx(laneUnitLoads));
}

TEST_CASE("RD-11/reconciliation reservations reconcile: reserved = built + unused, M5's lane within its 21" * doctest::skip()) {
    if (!augustOrSkip()) return;
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
    m5Evidence::note("notCovered", "the client-basis figures (client trucks on reserved lane-dates, placeholder trucks less client loads) are not computed: only M4/M5's own reserved = built + unused is reconciled");
}

TEST_CASE("RD-12/productionProfile M5's trucks report fullness reasons and rear-inch averages per bucket" * doctest::skip()) {
    if (!augustOrSkip()) return;
    const string& text = withM5(1).selectionPlan.largeLanes.front().text;
    CHECK(text.find("by weight") != string::npos);
    CHECK(text.find("by rear inches") != string::npos);
    CHECK(text.find("floor-limited") != string::npos);
    CHECK(text.find("average rear inches, goods remain") != string::npos);
    m5Evidence::note("notCovered", "the client's historical trucks (M4 layout of their goods) and M4's own trucks are not counted by FULL reason; only M5's trucks are");
}

TEST_CASE("ACC-2/bothDatesBenchmark RD-06/benchmark RD-06/productionProfile X-E2E-01/productionProfile X-REPORT-02/realRun the labelled both-dates benchmark keeps 21 and 20 and commits nothing"
          * doctest::skip()) {
    if (!augustOrSkip()) return;
    const nlohmann::json reference = tomReferenceOrSkip("ACC-2/bothDatesBenchmark and its labelled variants");
    if (reference.is_null()) return;
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
    MESSAGE("both-dates benchmark: " << trucks << " trucks against the client's historical " << reference.at("clientHistoricalTrucks").at("aug5BothDates").get<int>());
    for (size_t date = 0; date < 2; ++date) {
        const m5::M5LaneOutcome& outcome = benchmark.outcomes[date];
        MESSAGE("date " << date << ": allowance " << outcome.allowanceSeconds << " s (CL-05), elapsed " << outcome.timing.elapsedSeconds << " s");
    }
}

TEST_CASE("ACC-2/syntheticDiagnostic RD-03/syntheticPolicyDiagnostic X-E2E-01/syntheticPolicyDiagnostic Aug5 7 Aug with the optional acceptance policy ON: a labelled diagnostic, committed nowhere"
          * doctest::skip()) {
    if (!augustOrSkip()) return;
    const vector<m5::M5LaneDateInput> sevenAug = laneDates(aug5WithoutM5(), {"2028", "2574", "TL"}, {"2026-08-07"});
    REQUIRE(sevenAug.size() == 1);
    m5::M5PolicyProfile diagnostic = shippedProfile();
    diagnostic.acceptance = m5::NumericAcceptance{40000.0, 3300.0};
    const m5::FunctionClock clock(nullptr);
    const m5::M5RunServices services(clock);
    const m5::M5LaneOutcome outcome = m5::planLaneDate(sevenAug[0], diagnostic, m5::physicalSettingsOf(aug5WithoutM5().selectionParams.selector), services);
    REQUIRE(outcome.selected.has_value());
    const string text = m5::renderAscii(m5::buildReportData(outcome, sevenAug[0], &diagnostic, {"SYNTHETIC", 20, "diagnostic: optional policy ON, not production"}));
    CHECK(text.find("scope: SYNTHETIC") != string::npos);
    CHECK(text.find("optional policy ON") != string::npos);
    CHECK(outcome.selected->trucks.size() <= 21);
    for (const m5::TruckPlan& truck : outcome.selected->trucks) {
        REQUIRE(truck.layout.rows.size() > 0);
    }
    MESSAGE("diagnostic, policy ON: " << outcome.selected->trucks.size() << " trucks; the production run is untouched (it never reads this profile)");
}

TEST_CASE("U-PF-02/benchmark X-RUN-10/benchmark M4, A and B on the same goods, in either order" * doctest::skip()) {
    if (!augustOrSkip()) return;
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
    m5Evidence::note("notCovered", "M4's own time on this lane-date is not measured because M4 escalates it; cold and warm cache runs are not compared (X-RUN-10/coldWarmCache)");
}

TEST_CASE("RD-07/benchmark RD-07/productionProfile Aug6 2028 -> 2190, both dates joined, completes and is reported" * doctest::skip()) {
    if (!augustOrSkip()) return;
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

TEST_CASE("X-REPORT-03/realRun the report never calls the lane infeasible without a certificate" * doctest::skip()) {
    if (!augustOrSkip()) return;
    const string& text = withM5(1).selectionPlan.largeLanes.front().text;
    CHECK(text.find("infeasible") == string::npos);
    CHECK(text.find("635") == string::npos);
}

// ---------------------------------------------------------------------------------------- reference file
// Tom's figures (chart ranges, client cube, the most-ever-seen cube line, the client's historical loads and truck
// counts) are never written into a test: they are read from a local, git-ignored file; a test that needs it is skipped
// by name, and recorded BLOCKED with the reason, when the file is missing.

TEST_CASE("U-RP-07/realData Aug5 7 Aug: bucket averages are printed beside Tom's chart ranges and the client's cube, as context and not pass or fail"
          * doctest::skip()) {
    if (!augustOrSkip()) return;
    const nlohmann::json reference = tomReferenceOrSkip("U-RP-07/realData");
    if (reference.is_null()) return;
    const string& text = withM5(1).selectionPlan.largeLanes.front().text;
    // The report's own figures: M5's average rear inches per bucket.
    for (const char* bucket : {"goods remain", "no goods remain", "rule-limited"}) {
        CHECK(text.find(string("average rear inches, ") + bucket) != string::npos);
    }
    // The comparison block this test composes from the report and the file; it states what each figure is.
    const auto range = [&](const char* key) {
        const nlohmann::json& pair = reference.at("weeklyChartRangesInches").at(key);
        return to_string(pair.at(0).get<int>()) + "-" + to_string(pair.at(1).get<int>()) + " in";
    };
    string comparison = "fill comparison (context, not pass or fail)\n";
    comparison += "  Tom's chart, weeks " + reference.at("weeklyChartRangesInches").at("weeks").get<string>() + ": goods remain " + range("goodsRemain")
                  + ", no goods remain " + range("noGoodsRemain") + ", priority " + range("priority") + "\n";
    comparison += "  client average cube " + to_string(reference.at("clientAverageCubeCuFt").at(0).get<int>()) + " / "
                  + to_string(reference.at("clientAverageCubeCuFt").at(1).get<int>()) + " / " + to_string(reference.at("clientAverageCubeCuFt").at(2).get<int>())
                  + " cu ft (same master, average with average)\n";
    comparison += "  " + to_string(reference.at("mostEverSeenCuFt").at(0).get<int>()) + "-" + to_string(reference.at("mostEverSeenCuFt").at(1).get<int>())
                  + " cu ft: the most ever seen, not a target\n";
    CHECK(comparison.find("context, not pass or fail") != string::npos);
    CHECK(comparison.find("most ever seen, not a target") != string::npos);
    CHECK(comparison.find("goods remain " + range("goodsRemain")) != string::npos);
    CHECK(comparison.find("cu ft (same master, average with average)") != string::npos);
    MESSAGE(comparison);
}

TEST_CASE("U-PB-12/productionProfile U-PB-12/configIndependentPatterns Aug5 7 Aug: each product fills trucks of its own, the difference from the client's historical loads is reported, and the patterns do not depend on the fullness or time settings"
          * doctest::skip()) {
    if (!augustOrSkip()) return;
    const nlohmann::json reference = tomReferenceOrSkip("U-PB-12 variants");
    if (reference.is_null()) return;
    const PipelineResult& run = withM5(1);
    map<string, vector<size_t>> loadsOfProduct;
    for (const PlannedTruck& truck : run.selectionPlan.trucks) {
        if (!onLargeLane(run, run.laneClassification.routes[truck.routeIndex]) || truck.shipDate != kLargeLaneDate) continue;
        set<string> products;
        for (const size_t index : truck.items) products.insert(run.selectionItems.items[index].productNumber);
        if (products.size() == 1) loadsOfProduct[*products.begin()].push_back(truck.items.size());
    }
    for (const string product : {"105666801", "105446303"}) {
        REQUIRE(!loadsOfProduct[product].empty());
        MESSAGE("product " << product << ": " << loadsOfProduct[product].size() << " single-product trucks, stacks per truck " << loadsOfProduct[product].front());
    }
    // The comparison with the client's historical loads: stated, not copied.
    string difference;
    for (const nlohmann::json& historical : reference.at("clientHistoricalLoads")) {
        difference += to_string(historical.at("trucks").get<int>()) + " trucks of " + to_string(historical.at("unitLoadsEach").get<int>()) + "; ";
    }
    CHECK_FALSE(difference.empty());
    MESSAGE("client's historical loads (older master): " << difference << "M5 reports its own loads above; it need not copy them");

    // Configuration independence: the fullness limits and the seconds per load do not change a product's truck loads.
    const vector<m5::M5LaneDateInput> sevenAug = laneDates(aug5WithoutM5(), {"2028", "2574", "TL"}, {"2026-08-07"});
    REQUIRE(sevenAug.size() == 1);
    const m5::PhysicalSettings physical = m5::physicalSettingsOf(aug5WithoutM5().selectionParams.selector);
    const m5::FunctionClock clock(nullptr);
    const auto loadsFor = [&](m5::M5PolicyProfile profile) {
        const m5::M5RunServices services(clock);
        const m5::M5LaneOutcome outcome = m5::planLaneDate(sevenAug[0], profile, physical, services);
        multiset<size_t> sizes;
        if (outcome.selected) {
            for (const m5::TruckPlan& truck : outcome.selected->trucks) sizes.insert(truck.atoms.size());
        }
        return sizes;
    };
    m5::M5PolicyProfile shipped = shippedProfile(), changed = shippedProfile();
    changed.fullness.rearInchLimit = 30.0;
    changed.fullness.weightFullPercent = 90.0;
    changed.secondsPerLoad = shipped.secondsPerLoad * 2.0;
    CHECK(loadsFor(shipped) == loadsFor(changed));
}

}  // TEST_SUITE
