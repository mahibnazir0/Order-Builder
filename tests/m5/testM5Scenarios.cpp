// M5 scenarios across the allocation, block, repair, commit and state seams (v7 Sections 3, 4.4-4.8,
// 12, 14-17, 19-23). Small fixtures run through M5's direct seam (v7 0.4).
#include "doctest.h"
#include "floorPattern.hpp"
#include "isoDate.hpp"
#include "m5Commit.hpp"
#include "m5Integration.hpp"
#include "m5Ledger.hpp"
#include "m5Report.hpp"
#include "m5Solver.hpp"
#include "m5Validation.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Fixtures.hpp"
#include "support/m5Oracles.hpp"
#include "support/m5StubMethods.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <regex>
#include <set>

using namespace std;
using namespace ob::m5;
using namespace m5Fixtures;
using m5Stubs::FixedMethod;

namespace {

vector<Atom> concat(vector<Atom> left, const vector<Atom>& right) {
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

// A scalar model built in one step. Assigning a brace list to a new model's empty `sizes` makes
// GCC 16.1 at -O3 report a false -Wnonnull inside vector::assign.
ScalarModel scalarModel(vector<long long> sizes, long long capacity) {
    ScalarModel model;
    model.sizes = std::move(sizes);
    model.capacity = capacity;
    return model;
}

size_t countCause(const M5LaneOutcome& outcome, LeftOverCause cause) {
    return static_cast<size_t>(count_if(outcome.leftOver.begin(), outcome.leftOver.end(),
                                        [&](const LeftOver& left) { return left.cause == cause; }));
}

void requireInvariants(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    const vector<string> violations = m5Oracles::invariantViolations(outcome, input, physicalSettings().floor);
    for (const string& violation : violations) INFO(violation);
    REQUIRE(violations.empty());
}

M5LaneOutcome planWithServices(const M5LaneDateInput& input, const M5PolicyProfile& profile,
                               const function<void(M5RunServices&)>& configure) {
    FakeClock clock;
    M5RunServices services(clock);
    configure(services);
    return planLaneDate(input, profile, physicalSettings(), services);
}

vector<Atom> blocks(size_t count, const string& group = "A") {
    vector<Atom> atoms;
    for (size_t unit = 0; unit < count; ++unit) atoms.push_back(fxBlock(group + to_string(1000 + unit), group));
    return atoms;
}

M5PolicyProfile blocksOnly() {
    M5PolicyProfile profile = syntheticProfile();
    profile.method = MethodPolicy::BlocksOnly;
    return profile;
}

// Blocks method B built, as the trucks it had after each block.
vector<size_t> blockSizes(const M5LaneDateInput& input) {
    vector<size_t> after;
    planWithServices(input, blocksOnly(), [&](M5RunServices& services) {
        services.afterBlock = [&](size_t trucks) { after.push_back(trucks); };
    });
    vector<size_t> sizes;
    size_t previous = 0;
    for (const size_t trucks : after) {
        sizes.push_back(trucks - previous);
        previous = trucks;
    }
    return sizes;
}

}  // namespace

// ---------------------------------------------------------------------------- rolling blocks

// Every block truck ships once, each its one whole-truck item, and nothing is left.
void requireEachBlockOnce(const M5LaneOutcome& outcome, const M5LaneDateInput& input, size_t trucks) {
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == trucks);
    vector<int> shipped(input.atoms.size(), 0);
    for (const TruckPlan& truck : outcome.selected->trucks) {
        CHECK(truck.atoms.size() == 1);
        for (const size_t atom : truck.atoms) ++shipped[atom];
    }
    CHECK(all_of(shipped.begin(), shipped.end(), [](int count) { return count == 1; }));
    CHECK(outcome.leftOver.empty());
    requireInvariants(outcome, input);
}

TEST_CASE("U-RB-01/synthetic: goods for exactly 38 full trucks on enough reservations go 19 + 19" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(blocks(38), 39);
    CHECK(blockSizes(input) == vector<size_t>{19, 19});
    requireEachBlockOnce(plan(input, blocksOnly()), input, 38);
    m5Evidence::note("expected", {"19", "19", "38 trucks, one reservation unused"});
}

TEST_CASE("U-RB-02/synthetic: goods for exactly 21 full trucks on 21 reserved go 19 + 2, no padding truck" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(blocks(21), 21);
    CHECK(blockSizes(input) == vector<size_t>{19, 2});
    const M5LaneOutcome outcome = plan(input, blocksOnly());
    requireEachBlockOnce(outcome, input, 21);
    for (const TruckPlan& truck : outcome.selected->trucks) CHECK_FALSE(truck.atoms.empty());
    m5Evidence::note("expected", {"19", "2", "21 trucks, none empty"});
}

TEST_CASE("U-RB-01 X-BLK-01 blocks of 19, the last never padded" * doctest::test_suite("m5")) {
    const vector<pair<size_t, vector<size_t>>> expected{{0, {}},           {1, {1}},         {18, {18}},         {19, {19}},
                                                        {20, {19, 1}},     {37, {19, 18}},   {38, {19, 19}},     {39, {19, 19, 1}}};
    for (const pair<size_t, vector<size_t>>& entry : expected) {
        INFO(entry.first << " trucks of goods");
        CHECK(blockSizes(laneWith(blocks(entry.first), 39)) == entry.second);
    }
}

TEST_CASE("U-RB-01/thirtyNineBlocks: 39 whole-truck items on 39 reservations go 19 + 19 + 1, each once" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(blocks(39), 39);
    CHECK(blockSizes(input) == vector<size_t>{19, 19, 1});
    requireEachBlockOnce(plan(input, blocksOnly()), input, 39);
    m5Evidence::note("expected", {"19", "19", "1"});
}

TEST_CASE("X-BLK-04 several groups share 21: a first block of 19 leaves 2 for everything else" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(blocks(19, "A"), blocks(5, "B")), 21);
    const M5LaneOutcome outcome = plan(input, blocksOnly());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 21);
}

TEST_CASE("X-BLK-05 a candidate with a truck failing its floor cannot win; both invalid is no plan" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 61), 3);
    auto planWith = [&](GroupTrucks first, GroupTrucks second) {
        return planWithServices(input, syntheticProfile(), [&](M5RunServices& services) {
            services.methods = {make_shared<FixedMethod>(MethodId::PatternRepeat, first),
                                make_shared<FixedMethod>(MethodId::RollingBlocks, second)};
        });
    };
    Truck overfull;
    for (size_t atom = 0; atom < 31; ++atom) overfull.push_back(atom);
    Truck rest;
    for (size_t atom = 31; atom < 61; ++atom) rest.push_back(atom);
    Truck first30(overfull.begin(), overfull.begin() + 30);
    const M5LaneOutcome valid = planWith({overfull}, {first30, rest});
    REQUIRE(valid.selected);
    CHECK(valid.selected->method == MethodId::RollingBlocks);
    CHECK(shippedCount(valid) == 60);
    const M5LaneOutcome neither = planWith({overfull}, {overfull});
    CHECK_FALSE(neither.hasCandidate);
    CHECK(shippedCount(neither) == 0);
    requireInvariants(neither, input);
}

TEST_CASE("X-BLK-06 A ships 30 in three, B 20 in two: fewer trucks is not better" * doctest::test_suite("m5")) {
    vector<Atom> atoms;
    for (int unit = 0; unit < 30; ++unit) atoms.push_back(unitOf("t" + to_string(100 + unit), "A", 100, 63, 108, 4500));
    const M5LaneDateInput input = laneWith(atoms, 3);
    M5PolicyProfile profile = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    profile.comparator = ComparatorKind::FxPriority;
    const M5LaneOutcome outcome = planWithServices(input, profile, [&](M5RunServices& services) {
        services.methods = {make_shared<FixedMethod>(MethodId::PatternRepeat, 3, 10), make_shared<FixedMethod>(MethodId::RollingBlocks, 2, 10)};
    });
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->method == MethodId::PatternRepeat);
    CHECK(shippedCount(outcome) == 30);
}

TEST_CASE("X-BLK-08 X-E2E-05 X-RUN-03 stopped in the third block: keep 38 checked, or roll back to none" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(blocks(39), 39);
    auto stoppedAfterTwoBlocks = [&](bool rollback) {
        FakeClock clock;
        M5RunServices services(clock);
        services.allowanceSeconds = 100.0;
        services.wholeLaneRollback = rollback;
        services.afterBlock = [&](size_t trucks) {
            if (trucks >= 38) clock.advance(1000.0);
        };
        return planLaneDate(input, blocksOnly(), physicalSettings(), services);
    };
    const M5LaneOutcome keep = stoppedAfterTwoBlocks(false);
    CHECK(keep.kind == OutcomeKind::TimeoutWithIncumbent);
    REQUIRE(keep.selected);
    CHECK(keep.selected->trucks.size() == 38);
    CHECK(keep.leftOver.size() == 1);
    CHECK(keep.allowanceSeconds == 100.0);
    requireInvariants(keep, input);
    const M5LaneOutcome rollback = stoppedAfterTwoBlocks(true);
    CHECK_FALSE(rollback.selected.has_value());
    CHECK(countCause(rollback, LeftOverCause::NoResult) == 39);
}

TEST_CASE("X-RUN-07 retry before commit plans the original; after a partial commit only the residual" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(blocks(39), 39);
    CHECK(shippedCount(plan(input, blocksOnly())) == 39);
    FakeClock clock;
    M5RunServices services(clock);
    services.allowanceSeconds = 100.0;
    services.afterBlock = [&](size_t trucks) {
        if (trucks >= 38) clock.advance(1000.0);
    };
    const M5LaneOutcome partial = planLaneDate(input, blocksOnly(), physicalSettings(), services);
    REQUIRE(partial.delta);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CommitLedger ledger;
    OperationalState state{snapshot.id, snapshot.input.pool.version, {}, 0, 39};
    REQUIRE(ledger.applyIfCurrent(*partial.delta, snapshot, state) == ApplyStatus::Applied);
    const DemandSnapshot residual = residualAfter(snapshot, *partial.delta);
    CHECK(residual.input.atoms.size() == 1);
    CHECK(residual.input.pool.slots.size() == 1);
    const M5LaneOutcome retry = plan(residual.input, blocksOnly());
    CHECK(shippedCount(retry) == 1);
    CHECK(ledger.applyIfCurrent(*partial.delta, snapshot, state) == ApplyStatus::AlreadyApplied);
    CHECK(state.consumedAtoms.size() == 38);
}

// ------------------------------------------------------------------- repair and final contents

TEST_CASE("R-1c X-REB-05 X-RES-09 X-RES-11 X-E2E-02 a broken truck is repaired on a copy; goods return once" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 31, "A"), fx30Units("b", 20, "B")), 2);
    Truck overfull;
    for (size_t atom = 0; atom < 31; ++atom) overfull.push_back(atom);
    Truck groupB;
    for (size_t atom = 31; atom < 51; ++atom) groupB.push_back(atom);
    const M5LaneOutcome outcome = planWithServices(input, syntheticProfile(), [&](M5RunServices& services) {
        services.methods = {make_shared<FixedMethod>(MethodId::PatternRepeat, GroupTrucks{overfull, groupB})};
    });
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 1);
    CHECK(outcome.selected->trucks.front().group == "B");
    CHECK(outcome.selected->evidence.proof != ProofStatus::ProvenOptimal);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 31);
    REQUIRE_FALSE(outcome.methods.front().findings.empty());
    // Decided again on final contents: M5's own methods find B's 20 and 30 of A's in two trucks.
    const M5LaneOutcome replanned = plan(input, syntheticProfile());
    CHECK(shippedCount(replanned) == 50);
}

TEST_CASE("U-RM-01 U-RM-02 the remainder packs through M5's packer layer, and an empty one adds no truck" * doctest::test_suite("m5")) {
    CHECK(plan(laneWith(fx30Units("u", 65), 3), syntheticProfile()).selected->trucks.size() == 3);
    CHECK(plan(laneWith(fx30Units("u", 60), 3), syntheticProfile()).selected->trucks.size() == 2);
}

TEST_CASE("U-RM-03 production moves goods only when the whole plan is better" * doctest::test_suite("m5")) {
    const M5LaneOutcome outcome = plan(laneWith(fx30Units("u", 50), 2), syntheticProfile());
    REQUIRE(outcome.selected);
    multiset<size_t> loads;
    for (const TruckPlan& truck : outcome.selected->trucks) loads.insert(truck.atoms.size());
    CHECK(loads == multiset<size_t>{20, 30});
}

TEST_CASE("U-RM-05 usable reservations serve eligible goods; unused slots only with nothing left" * doctest::test_suite("m5")) {
    CHECK(plan(laneWith(fx30Units("u", 70), 3), syntheticProfile()).selected->trucks.size() == 3);
    const M5LaneOutcome spare = plan(laneWith(fx30Units("u", 50), 3), syntheticProfile());
    CHECK(spare.selected->trucks.size() == 2);
    CHECK(shippedCount(spare) == 50);
}

TEST_CASE("X-REB-01 single-product patterns must not lock in 3 bins where 2 mixed bins serve" * doctest::test_suite("m5")) {
    const ScalarModel model = scalarModel({6, 6, 4, 4}, 10);
    CHECK(solveCuttingStock(model, ScalarLimits()).packing->bins.size() == 2);
    const M5LaneDateInput input = laneWith({unitOf("a1", "A", 378, 100, 108, 1000), unitOf("a2", "A", 378, 100, 108, 1000),
                                            unitOf("b1", "A", 252, 100, 108, 1000), unitOf("b2", "A", 252, 100, 108, 1000)},
                                           2);
    CHECK(shippedCount(plan(input, syntheticProfile())) == 4);
}

TEST_CASE("X-REB-02 X-REB-08 groups never mix and no move breaks the weight limit" * doctest::test_suite("m5")) {
    const TrailerCapability tenThousand{630.0, 100.0, 108.0, 10000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput apart = laneWith({unitOf("a", "A", 100, 60, 50, 4000), unitOf("b", "B", 100, 40, 50, 4000)}, 2, tenThousand);
    const M5LaneOutcome outcome = plan(apart, syntheticProfile());
    requireInvariants(outcome, apart);
    CHECK(outcome.selected->trucks.size() == 2);
    const M5LaneDateInput heavy = laneWith({unitOf("a", "A", 100, 60, 50, 4000), unitOf("b", "A", 100, 40, 50, 7000)}, 1, tenThousand);
    CHECK_FALSE(checkTruck(heavy, {0, 1}, tenThousand, physicalSettings()).fits);
    CHECK(shippedCount(plan(heavy, syntheticProfile())) == 1);
}

TEST_CASE("X-REB-03 a remainder ineligible by date takes no truck and is reported" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("now", 30), fx30Units("later", 5, "A", 0, dayAfter(1))), 3);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(outcome.selected->trucks.size() == 1);
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 5);
}

TEST_CASE("X-REB-06 R-3a a later line cannot ride a mixed stack when pull-forward is off" * doctest::test_suite("m5")) {
    Atom stack = unitOf("mixed", "A", 100, 20, 100, 3000);
    stack.unitLoads = 2.0;
    Component later = stack.components[0];
    later.source = "line-later";
    later.dueDate = dayAfter(1);
    later.offerKind = OfferKind::CanGo;
    stack.components.push_back(later);
    const M5LaneDateInput off = laneWith({stack}, 1);
    const M5LaneOutcome outcome = plan(off, syntheticProfile());
    CHECK(shippedCount(outcome) == 0);
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 1);
    CHECK(shippedCount(plan(laneWith({stack}, 1, kDefaultTrailer, true), syntheticProfile())) == 1);
}

TEST_CASE("R-3b with pull-forward on, a line due within the window may ship early" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith({fx30("aug8", "A", 0, dayAfter(1), kD)}, 1, kDefaultTrailer, true);
    CHECK(shippedCount(plan(input, syntheticProfile())) == 1);
}

TEST_CASE("X-REB-07 priority never authorises an unaccepted shipment" * doctest::test_suite("m5")) {
    Atom urgent = unitOf("p0", "A", 400, 100, 1, 1);
    urgent.floor.cubeCubicIn = 8.0 * 1728.0;
    Atom later = unitOf("p11", "A", 400, 100, 1, 1, 11);
    later.floor.cubeCubicIn = 9.0 * 1728.0;
    M5LaneDateInput input = laneWith({urgent, later}, 1);
    input.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    CHECK(shippedIds(plan(input, withAcceptance(syntheticProfile(), 10.0, 7.9)), input) == set<string>{"p0"});
    CHECK(shippedIds(plan(input, withAcceptance(syntheticProfile(), 10.0, 8.0)), input) == set<string>{"p11"});
    CHECK(shippedIds(plan(input, syntheticProfile()), input) == set<string>{"p0"});
}

TEST_CASE("U-AC-02 a failing second truck of five is withheld, the others ship" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 130), 5);
    const M5LaneOutcome outcome = plan(input, withAcceptance(syntheticProfile(), 40000.0, 3000.0));
    REQUIRE(outcome.selected);
    for (const TruckPlan& truck : outcome.selected->trucks) CHECK(truck.atoms.size() >= 25);
    CHECK(shippedCount(outcome) == 130);
}

// ------------------------------------------------------------------ priority and reservations

TEST_CASE("U-RS-03 X-RES-02 X-RES-03 fewer trucks than goods: the pool caps it, priority decides" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("x", 60, "A", 5);
    atoms = concat(atoms, fx30Units("y", 15, "A", 5));
    atoms = concat(atoms, fx30Units("z", 15, "A", 5));
    const M5LaneOutcome three = plan(laneWith(atoms, 3), syntheticProfile());
    CHECK(three.selected->trucks.size() == 3);
    CHECK(shippedCount(three) == 90);
    const M5LaneOutcome two = plan(laneWith(fx30Units("u", 120), 2), syntheticProfile());
    CHECK(two.selected->trucks.size() == 2);
}

TEST_CASE("X-RES-07 two reserved, priority first: all 30 P0 and 30 P11 ship" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("x", 90, "A", 11);
    atoms = concat(atoms, fx30Units("y", 15, "A", 0));
    atoms = concat(atoms, fx30Units("z", 15, "A", 0));
    M5LaneDateInput input = laneWith(atoms, 2);
    input.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    size_t urgent = 0;
    for (const string& id : shippedIds(outcome, input)) urgent += id[0] != 'x' ? 1 : 0;
    CHECK(urgent == 30);
    CHECK(shippedCount(outcome) == 60);
}

TEST_CASE("X-RES-08 unavailable or oversized P0 stays with its reason; P11 ships" * doctest::test_suite("m5")) {
    M5LaneDateInput late = laneWith(concat(fx30Units("p0-", 30, "A", 0, kD, dayAfter(1)), fx30Units("p11-", 30, "A", 11)), 1);
    late.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    const M5LaneOutcome first = plan(late, syntheticProfile());
    CHECK(shippedCount(first) == 30);
    CHECK(countCause(first, LeftOverCause::NotEligible) == 30);
    vector<Atom> oversized;
    for (int unit = 0; unit < 30; ++unit) oversized.push_back(unitOf("big" + to_string(100 + unit), "A", 700, 100, 108, 100));
    M5LaneDateInput tooBig = laneWith(concat(oversized, fx30Units("p11-", 30, "A", 11)), 1);
    tooBig.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    const M5LaneOutcome second = plan(tooBig, syntheticProfile());
    CHECK(shippedCount(second) == 30);
    CHECK(countCause(second, LeftOverCause::Unfittable) == 30);
}

TEST_CASE("U-RS-01 trucks never exceed the reserved count, whatever the goods" * doctest::test_suite("m5")) {
    for (const size_t slots : {size_t{1}, size_t{2}, size_t{5}}) {
        const M5LaneDateInput input = laneWith(fx30Units("u", 200), slots);
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        CHECK(outcome.selected->trucks.size() == slots);
        requireInvariants(outcome, input);
    }
}

TEST_CASE("U-RS-01/hundredAndOneReserved: one unit on 101 reservations ships on one truck, 100 unused, no empty loads" * doctest::test_suite("m5")) {
    const M5PolicyProfile profile = syntheticProfile();
    const M5LaneDateInput input = laneWith(fx30Units("u", 1), 101);
    const M5LaneOutcome outcome = plan(input, profile);
    REQUIRE(outcome.selected);
    REQUIRE(outcome.selected->trucks.size() == 1);
    CHECK(outcome.selected->trucks[0].atoms == vector<size_t>{0});
    REQUIRE(outcome.delta);
    CHECK(outcome.delta->trucks.size() == 1);
    CHECK(outcome.leftOver.empty());
    const M5ReportData report = buildReportData(outcome, input, &profile, {});
    CHECK(report.loadedTrucks == 1);
    CHECK(report.unitLoadsShipped == 1.0);
    CHECK(report.unusedReservations == 100);
    CHECK_FALSE(report.penaltyAmount.has_value());
    CHECK(report.penalty == "unknown");
    requireInvariants(outcome, input);
    m5Evidence::note("expected", {"1 loaded truck", "100 unused", "penalty unknown"});
}

TEST_CASE("U-RS-09/zeroDayWindow: pull forward on with a 0-day window ships due-D goods only" * doctest::test_suite("m5")) {
    M5LaneDateInput input = laneWith({fx30("dueD"), fx30("dueD1", "A", 0, dayAfter(1), kD)}, 1, kDefaultTrailer, true);
    input.dates.maxDaysPulledForward = 0;
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(shippedIds(outcome, input) == set<string>{"dueD"});
    REQUIRE(outcome.leftOver.size() == 1);
    CHECK(input.atoms[outcome.leftOver[0].atom].id == "dueD1");
    CHECK(outcome.leftOver[0].cause == LeftOverCause::NotEligible);
    CHECK(input.atoms[1].components[0].dueDate == dayAfter(1));
    requireInvariants(outcome, input);
    input.dates.maxDaysPulledForward = 1;
    CHECK(shippedIds(plan(input, syntheticProfile()), input) == set<string>{"dueD", "dueD1"});
    m5Evidence::note("expected", {"dueD ships", "dueD1 left NotEligible for its own date"});
}

namespace {

// A monotonic clock beside a system wall clock that tests may move by days; the budget reads only the
// monotonic one.
class DualClock final : public Clock {
public:
    double nowSeconds() const override { return monotonic_; }
    double wallSeconds() const { return wall_; }
    void advance(double seconds) {
        monotonic_ += seconds;
        wall_ += seconds;
    }
    void shiftWall(double seconds) { wall_ += seconds; }

private:
    double monotonic_ = 500.0;
    double wall_ = 1'785'000'000.0;
};

constexpr double kDaySeconds = 24.0 * 3600.0;

}  // namespace

TEST_CASE("U-PF-07/wallClockShift: wall-clock jumps neither extend nor cut a 10-second monotonic allowance" * doctest::test_suite("m5")) {
    DualClock clock;
    Budget budget({10.0, nullopt, "FX-CLOCK 10 s"}, clock);
    clock.advance(3.0);
    CHECK(budget.remainingSeconds() == 7.0);
    clock.shiftWall(kDaySeconds);
    CHECK(budget.remainingSeconds() == 7.0);
    CHECK_FALSE(budget.exhausted());
    clock.shiftWall(-kDaySeconds);
    CHECK(budget.remainingSeconds() == 7.0);
    clock.shiftWall(-kDaySeconds);
    CHECK(budget.elapsedSeconds() == 3.0);
    CHECK_FALSE(budget.exhausted());
    clock.advance(6.5);
    CHECK_FALSE(budget.exhausted());
    clock.advance(0.5);
    CHECK(budget.exhausted());
    CHECK(budget.remainingSeconds() == 0.0);
    CHECK(budget.elapsedSeconds() == 10.0);

    const M5LaneDateInput input = laneWith(fx30Units("u", 60), 3);
    auto run = [&](double secondsBeforeB) {
        DualClock laneClock;
        M5RunServices services(laneClock);
        services.allowanceSeconds = 10.0;
        services.methods = {make_shared<PatternPlanner>(), make_shared<BlockPlanner>()};
        services.beforeMethod = [&](MethodId method) {
            if (method == MethodId::PatternRepeat) {
                laneClock.advance(3.0);
                laneClock.shiftWall(kDaySeconds);
                laneClock.shiftWall(-2.0 * kDaySeconds);
            } else {
                laneClock.advance(secondsBeforeB);
            }
        };
        return planLaneDate(input, syntheticProfile(), physicalSettings(), services);
    };
    const M5LaneOutcome inTime = run(6.5);
    CHECK(inTime.stop == StopReason::Completed);
    CHECK_FALSE(inTime.timing.exhausted);
    CHECK(inTime.timing.elapsedSeconds == 9.5);
    CHECK(shippedCount(inTime) == 60);
    const M5LaneOutcome stopped = run(7.0);
    CHECK(stopped.stop == StopReason::BudgetExhausted);
    CHECK(stopped.timing.exhausted);
    CHECK(stopped.timing.elapsedSeconds == 10.0);
    CHECK(stopped.kind == OutcomeKind::TimeoutWithIncumbent);
    requireInvariants(stopped, input);
    m5Evidence::note("expected", {"remaining 7 after 3 s and +/-24 h", "exhausted at 10 monotonic s"});
}

TEST_CASE("U-RS-02 X-RES-10 unused reservations and their penalty: known rates add, missing is unknown" * doctest::test_suite("m5")) {
    M5PolicyProfile five = syntheticProfile(), seven = syntheticProfile(), unknown = syntheticProfile(), zero = syntheticProfile();
    five.penaltyPerUnusedTruck = 5.0;
    seven.penaltyPerUnusedTruck = 7.0;
    zero.penaltyPerUnusedTruck = 0.0;
    const M5LaneDateInput twoUnused = laneWith(fx30Units("u", 30), 3), oneUnused = laneWith(fx30Units("v", 30), 2);
    const M5ReportData first = buildReportData(plan(twoUnused, five), twoUnused, &five, {});
    const M5ReportData second = buildReportData(plan(oneUnused, seven), oneUnused, &seven, {});
    CHECK(first.unusedReservations == 2);
    CHECK(second.unusedReservations == 1);
    CHECK(*first.penaltyAmount + *second.penaltyAmount == 17.0);
    const M5ReportData missing = buildReportData(plan(oneUnused, unknown), oneUnused, &unknown, {});
    CHECK_FALSE(missing.penaltyAmount.has_value());
    CHECK(missing.penalty == "unknown");
    CHECK(*buildReportData(plan(oneUnused, zero), oneUnused, &zero, {}).penaltyAmount == 0.0);
}

TEST_CASE("X-STATE-02 committed totals count the selected plan once: 60 units, 2 trucks, penalty 5" * doctest::test_suite("m5")) {
    M5PolicyProfile profile = syntheticProfile();
    profile.penaltyPerUnusedTruck = 5.0;
    const M5LaneDateInput input = laneWith(fx30Units("u", 60), 3);
    const M5LaneOutcome outcome = planWithServices(input, profile, [](M5RunServices& services) {
        services.methods = {make_shared<PatternPlanner>(), make_shared<BlockPlanner>()};
    });
    const M5ReportData report = buildReportData(outcome, input, &profile, {});
    CHECK(report.unitLoadsShipped == 60.0);
    CHECK(report.loadedTrucks == 2);
    CHECK(*report.penaltyAmount == 5.0);
    REQUIRE(outcome.delta);
    CHECK(outcome.delta->trucks.size() == 2);
}

TEST_CASE("X-STATE-07 two groups competing for the last reservation: exactly one wins" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 30, "A", 2), fx30Units("b", 30, "B", 2)), 1);
    const M5LaneOutcome first = plan(input, syntheticProfile());
    REQUIRE(first.selected->trucks.size() == 1);
    for (int run = 0; run < 5; ++run) CHECK(shippedIds(plan(input, syntheticProfile()), input) == shippedIds(first, input));
}

TEST_CASE("U-RB-05/synthetic: A and B with the same trucks are compared on goods and priority first; only a true tie uses the tie rule" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 30, "A", 0), fx30Units("b", 30, "B", 1)), 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const UrgencyDomain domain(snapshot);
    Truck a, b, b29;
    for (size_t atom = 0; atom < 30; ++atom) {
        a.push_back(atom);
        b.push_back(atom + 30);
        if (atom < 29) b29.push_back(atom + 30);
    }
    const auto comparator = makeComparator(ComparatorKind::M4Urgency);
    // One truck each: P0 goods beat P1 goods.
    CHECK(comparator->better(keyOf(snapshot, domain, {a}), keyOf(snapshot, domain, {b})));
    CHECK_FALSE(comparator->better(keyOf(snapshot, domain, {b}), keyOf(snapshot, domain, {a})));
    // Same priority, one truck each: more goods win.
    CHECK(comparator->better(keyOf(snapshot, domain, {b}), keyOf(snapshot, domain, {b29})));
    // A true tie: neither is better, so ED-21's rule decides.
    CHECK_FALSE(comparator->better(keyOf(snapshot, domain, {a}), keyOf(snapshot, domain, {a})));
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedOfGroup(outcome, input, "A") == 30);
    CHECK(shippedOfGroup(outcome, input, "B") == 0);
    m5Evidence::note("expected", {"30 P0 beat 30 P1 on 1 truck", "30 P1 beat 29 P1", "equal plans: no winner by class, tie rule applies"});
}

TEST_CASE("X-STATE-08 U-RB-05 equal trucks are not a tie: 30 P0 strictly beats 30 P1" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 30, "A", 0), fx30Units("b", 30, "B", 1)), 1);
    CHECK(shippedOfGroup(plan(input, syntheticProfile()), input, "A") == 30);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const UrgencyDomain domain(snapshot);
    Truck a, b;
    for (size_t atom = 0; atom < 30; ++atom) {
        a.push_back(atom);
        b.push_back(atom + 30);
    }
    const auto comparator = makeComparator(ComparatorKind::M4Urgency);
    CHECK(comparator->better(keyOf(snapshot, domain, {a}), keyOf(snapshot, domain, {b})));
    CHECK_FALSE(comparator->better(keyOf(snapshot, domain, {b}), keyOf(snapshot, domain, {a})));
}

TEST_CASE("U-RB-04/synthetic: A and B plan isolated copies of one snapshot; real statuses; only the selected checked result commits; a B not started is no zero-truck success" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 75), 3);
    const M5LaneDateInput untouched = input;
    const M5LaneOutcome outcome = planWithServices(input, syntheticProfile(), [](M5RunServices& services) {
        services.methods = {make_shared<PatternPlanner>(), make_shared<BlockPlanner>()};
    });
    requireInvariants(outcome, input);
    REQUIRE(outcome.methods.size() == 2);
    CHECK(outcome.methods[0].method == MethodId::PatternRepeat);
    CHECK(outcome.methods[1].method == MethodId::RollingBlocks);
    for (const MethodRecord& record : outcome.methods) {
        CHECK(record.ran);
        CHECK(record.skippedBecause.empty());
        CHECK(record.validated);
        CHECK(record.unitLoads == 75.0);
        CHECK(record.trucks == 3);
    }
    CHECK(count_if(outcome.methods.begin(), outcome.methods.end(), [](const MethodRecord& record) { return record.selected; }) == 1);
    REQUIRE(outcome.selected);
    REQUIRE(outcome.delta);
    CHECK(outcome.delta->trucks.size() == outcome.selected->trucks.size());
    CHECK(shippedCount(outcome) == 75);
    // The input both methods read is not changed by either.
    REQUIRE(input.atoms.size() == untouched.atoms.size());
    for (size_t atom = 0; atom < input.atoms.size(); ++atom) CHECK(input.atoms[atom].id == untouched.atoms[atom].id);

    // A spends the whole shared allowance: B never starts and is reported so, not as a plan of 0 trucks.
    FakeClock clock;
    M5RunServices services(clock);
    services.allowanceSeconds = 10.0;
    services.methods = {make_shared<PatternPlanner>(), make_shared<BlockPlanner>()};
    services.beforeMethod = [&](MethodId method) {
        if (method == MethodId::PatternRepeat) clock.advance(10.0);
    };
    const M5LaneOutcome late = planLaneDate(input, syntheticProfile(), physicalSettings(), services);
    requireInvariants(late, input);
    REQUIRE(late.methods.size() == 2);
    CHECK_FALSE(late.methods[1].ran);
    CHECK_FALSE(late.methods[1].skippedBecause.empty());
    CHECK_FALSE(late.methods[1].selected);
    CHECK_FALSE(late.methods[1].validated);
    CHECK(late.stop == StopReason::BudgetExhausted);
    CHECK(late.kind != OutcomeKind::CompletedZeroDispatch);
    CHECK(late.kind != OutcomeKind::CompletedWithDispatch);
    m5Evidence::note("expected", {"A and B both ran and validated, 75 unit loads on 3 trucks each", "one selected, its delta committed",
                                  "B after a spent allowance: not started, reported as such, run is a timeout"});
}

TEST_CASE("X-STATE-04 settings changed in one process plan as a fresh process would" * doctest::test_suite("m5")) {
    const vector<Atom> atoms = concat(fx30Units("old-", 30, "A", 11, kDayBefore, kDayBefore), fx30Units("now-", 30, "A", 0));
    M5LaneDateInput ageFirst = laneWith(atoms, 1), priorityFirst = laneWith(atoms, 1);
    priorityFirst.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    const set<string> freshAge = shippedIds(plan(ageFirst, syntheticProfile()), ageFirst);
    const set<string> freshPriority = shippedIds(plan(priorityFirst, syntheticProfile()), priorityFirst);
    for (int round = 0; round < 2; ++round) {
        CHECK(shippedIds(plan(ageFirst, syntheticProfile()), ageFirst) == freshAge);
        CHECK(shippedIds(plan(priorityFirst, syntheticProfile()), priorityFirst) == freshPriority);
    }
    const M5LaneDateInput pullOn = laneWith(fx30Units("c", 10, "A", 0, dayAfter(2)), 1, kDefaultTrailer, true);
    const M5LaneDateInput pullOff = laneWith(fx30Units("c", 10, "A", 0, dayAfter(2)), 1, kDefaultTrailer, false);
    CHECK(shippedCount(plan(pullOn, syntheticProfile())) == 10);
    CHECK(shippedCount(plan(pullOff, syntheticProfile())) == 0);
    CHECK(shippedCount(plan(pullOn, syntheticProfile())) == 10);
}

// --------------------------------------------------------------------------- R-5b and the core

TEST_CASE("R-5b seeded single trucks of 6-11 stacks are never worse than brute force" * doctest::test_suite("m5")) {
    struct Shape {
        double longSide, shortSide;
    };
    const vector<Shape> shapes{{60, 60}, {48, 40}, {120, 48}, {96, 40}, {100, 60}, {40, 33}};
    mt19937 generator(20261113);
    for (int round = 0; round < 60; ++round) {
        const size_t count = uniform_int_distribution<size_t>(6, 11)(generator);
        vector<Atom> atoms;
        for (size_t stack = 0; stack < count; ++stack) {
            const Shape shape = shapes[uniform_int_distribution<size_t>(0, shapes.size() - 1)(generator)];
            const int levels = uniform_int_distribution<int>(1, 3)(generator);
            const int priority = uniform_int_distribution<int>(0, 1)(generator) == 0 ? 0 : 11;
            Atom atom = unitOf("s" + to_string(100 + stack), "A", shape.longSide, shape.shortSide, 35.0 * levels, 2500.0 * levels, priority);
            atom.floor.levels = levels;
            atom.unitLoads = levels;
            atom.components[0].unitLoads = levels;
            atoms.push_back(atom);
        }
        M5LaneDateInput input = laneWith(atoms, 1);
        input.pastDueOrder = ob::PastDueOrder::PriorityFirst;
        // Brute force over every subset, judged by M4's floor and the payload: unit loads of P0,
        // then of P11.
        pair<double, double> best{0.0, 0.0};
        const ob::TrailerSpec trailer = trailerOf(kDefaultTrailer);
        for (unsigned mask = 1; mask < (1u << count); ++mask) {
            double weight = 0.0;
            pair<double, double> value{0.0, 0.0};
            vector<ob::FloorItem> items;
            for (size_t stack = 0; stack < count; ++stack) {
                if (!(mask & (1u << stack))) continue;
                weight += atoms[stack].weightLb;
                (atoms[stack].components[0].priority == 0 ? value.first : value.second) += atoms[stack].unitLoads;
                items.push_back(atoms[stack].floor);
            }
            if (weight > kDefaultTrailer.maxWeightLb || value <= best) continue;
            const ob::FloorLayout layout = ob::arrangeFloor(items, trailer, physicalSettings().floor);
            if (layout.fitsLength && layout.unplaceableItems.empty()) best = value;
        }
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        pair<double, double> shipped{0.0, 0.0};
        if (outcome.selected) {
            for (const size_t atom : outcome.selected->trucks.front().atoms) {
                (atoms[atom].components[0].priority == 0 ? shipped.first : shipped.second) += atoms[atom].unitLoads;
            }
        }
        INFO("round " << round << " with " << count << " stacks");
        REQUIRE(shipped >= best);
        requireInvariants(outcome, input);
    }
}

TEST_CASE("U-CG-01/synthetic: up to 10 items, column generation uses as many trucks as brute force, with a valid packing" * doctest::test_suite("m5")) {
    mt19937 generator(20261118);
    uniform_int_distribution<long long> size(1, 10);
    uniform_int_distribution<size_t> count(1, 10);
    size_t cases = 0;
    for (int round = 0; round < 300; ++round) {
        vector<long long> sizes(count(generator));
        for (long long& item : sizes) item = size(generator);
        const ScalarModel model = scalarModel(sizes, 10);
        const ScalarResult result = solveCuttingStock(model, ScalarLimits());
        INFO("round " << round);
        REQUIRE(result.packing.has_value());
        CHECK(result.impossible.empty());
        CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
        CHECK(static_cast<long long>(result.packing->bins.size()) == m5Oracles::bruteForceBins(sizes, 10));
        ++cases;
    }
    CHECK(cases == 300);
    m5Evidence::note("expected", {"300 seeded cases of 1-10 items, sizes 1-10, capacity 10", "bins == brute force on every case"});
}

TEST_CASE("U-CG-03/synthetic: one item type is one pattern repeated; the remainder is kept, never rounded up" * doctest::test_suite("m5")) {
    // Seventeen items of 2 in bins of 10: three bins of five and one of the remaining two.
    ScalarModel oneType;
    oneType.sizes.assign(17, 2);
    oneType.capacity = 10;
    const ScalarResult result = solveCuttingStock(oneType, ScalarLimits());
    REQUIRE(result.packing.has_value());
    CHECK(m5Oracles::packingValid(oneType, *result.packing, result.impossible));
    vector<size_t> perBin;
    size_t packed = 0;
    for (const auto& bin : result.packing->bins) {
        perBin.push_back(bin.size());
        packed += bin.size();
    }
    sort(perBin.begin(), perBin.end());
    CHECK(perBin == vector<size_t>{2, 5, 5, 5});
    CHECK(packed == 17);

    // The same on trailers: 35 FX-30 on two slots ship 30 + 5, never two full trucks; on one slot 30 ship and 5 stay.
    const M5LaneDateInput twoSlots = laneWith(fx30Units("u", 35), 2);
    const M5LaneOutcome both = plan(twoSlots, syntheticProfile());
    requireInvariants(both, twoSlots);
    REQUIRE(both.selected);
    vector<size_t> truckLoads;
    for (const TruckPlan& truck : both.selected->trucks) truckLoads.push_back(truck.atoms.size());
    sort(truckLoads.begin(), truckLoads.end());
    CHECK(truckLoads == vector<size_t>{5, 30});
    const M5LaneDateInput oneSlot = laneWith(fx30Units("u", 35), 1);
    const M5LaneOutcome single = plan(oneSlot, syntheticProfile());
    requireInvariants(single, oneSlot);
    CHECK(shippedCount(single) == 30);
    CHECK(single.leftOver.size() == 5);
    m5Evidence::note("expected", {"bins 5,5,5,2 (17 packed)", "two slots: 30 + 5", "one slot: 30 ship, 5 kept"});
}

TEST_CASE("U-CG-04/synthetic: an item exactly the trailer's width or length fits, with no rounding error" * doctest::test_suite("m5")) {
    const ScalarModel exact = scalarModel({10, 10}, 10);
    const ScalarResult result = solveCuttingStock(exact, ScalarLimits());
    REQUIRE(result.packing.has_value());
    CHECK(result.impossible.empty());
    CHECK(result.packing->bins.size() == 2);

    // 630 x 100 in, 108 in, 45,000 lb: exactly the trailer's length, width, height and payload.
    const M5LaneDateInput block = laneWith({fxBlock("block")}, 1);
    const PhysicalCheck blockCheck = checkTruck(block, {0}, kDefaultTrailer, physicalSettings());
    CHECK(blockCheck.fits);
    CHECK(blockCheck.findings.empty());
    const M5LaneOutcome outcome = plan(block, syntheticProfile());
    requireInvariants(outcome, block);
    CHECK(shippedCount(outcome) == 1);
    // A 100 in wide item, 300 in long: two stand nose to tail in exactly 600 in.
    const M5LaneDateInput wide = laneWith({unitOf("w1", "A", 300.0, 100.0, 50.0, 1000.0), unitOf("w2", "A", 300.0, 100.0, 50.0, 1000.0)}, 1);
    const PhysicalCheck wideCheck = checkTruck(wide, {0, 1}, kDefaultTrailer, physicalSettings());
    CHECK(wideCheck.fits);
    m5Evidence::note("expected", {"size 10 in bins of 10: one each", "630 x 100 x 108 in, 45,000 lb block fits", "two 300 x 100 in fit"});
}

TEST_CASE("U-CG-05/synthetic: one unit too big does not fit unless an allowed turn fits; no tolerance lets it in" * doctest::test_suite("m5")) {
    const ScalarResult scalar = solveCuttingStock(scalarModel({11, 10}, 10), ScalarLimits());
    CHECK(scalar.impossible == vector<size_t>{0});
    REQUIRE(scalar.packing.has_value());
    CHECK(scalar.packing->bins.size() == 1);

    // One step (0.01) over each limit, with no turn that helps.
    const vector<pair<string, Atom>> tooBig{
        {"length 630.01", unitOf("long", "A", 630.01, 20.0, 108.0, 1500.0)},
        {"both sides 100.01", unitOf("square", "A", 100.01, 100.01, 50.0, 1500.0)},
        {"height 108.01", unitOf("tall", "A", 100.0, 20.0, 108.01, 1500.0)},
        {"weight 45,000.01", unitOf("heavy", "A", 100.0, 20.0, 108.0, 45000.01)},
    };
    for (const auto& [label, atom] : tooBig) {
        INFO(label);
        const M5LaneDateInput input = laneWith({atom}, 1);
        const PhysicalCheck check = checkTruck(input, {0}, kDefaultTrailer, physicalSettings());
        CHECK_FALSE(check.fits);
        CHECK_FALSE(check.undecided);
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        CHECK(shippedCount(outcome) == 0);
        REQUIRE(outcome.leftOver.size() == 1);
        CHECK(outcome.leftOver.front().cause == LeftOverCause::Unfittable);
    }
    // 100.01 in on one side only: turned along the trailer it fits.
    const M5LaneDateInput turned = laneWith({unitOf("turn", "A", 100.01, 99.0, 50.0, 1500.0)}, 1);
    CHECK(checkTruck(turned, {0}, kDefaultTrailer, physicalSettings()).fits);
    CHECK(shippedCount(plan(turned, syntheticProfile())) == 1);
    m5Evidence::note("expected", {"11 in bins of 10 impossible", "0.01 over length, both sides, height, weight: no fit",
                                  "100.01 x 99 in fits turned"});
}

TEST_CASE("U-CG-06/synthetic: at the iteration cap it stops with status, the checked result, an honest gap and the goods left" * doctest::test_suite("m5")) {
    // Item 10 (size 11) is larger than the bin: it is the goods left.
    const ScalarModel model = scalarModel({6, 5, 3, 2, 2, 2, 4, 4, 7, 1, 11}, 10);
    ScalarLimits capped;
    capped.maxColumnRounds = 1;
    capped.exactMaxItems = 0;  // no exact shortcut: the column rounds are what stops
    const ScalarResult result = solveCuttingStock(model, capped);
    CHECK(result.evidence.termination == Termination::IterationCap);
    REQUIRE(result.packing.has_value());
    CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
    CHECK(result.impossible == vector<size_t>{10});
    CHECK(result.evidence.columnsGenerated <= model.sizes.size() + 1);
    REQUIRE(result.evidence.incumbent.has_value());
    CHECK(*result.evidence.incumbent == static_cast<long long>(result.packing->bins.size()));
    REQUIRE(result.evidence.lowerBound.has_value());
    const long long best = m5Oracles::bruteForceBins({6, 5, 3, 2, 2, 2, 4, 4, 7, 1}, 10);
    CHECK(*result.evidence.lowerBound <= best);
    CHECK(best <= *result.evidence.incumbent);
    CHECK(result.evidence.proof != ProofStatus::ProvenInfeasible);
    if (*result.evidence.lowerBound < *result.evidence.incumbent) CHECK(result.evidence.proof != ProofStatus::ProvenOptimal);
    CHECK(result.evidence.lpStatus == "pricingIncomplete");

    // The simplex's own iteration cap stops it the same way: a status, a checked result, the goods left.
    ScalarLimits simplexCapped;
    simplexCapped.simplexIterationLimit = 1;
    simplexCapped.exactMaxItems = 0;
    const ScalarResult simplexStop = solveCuttingStock(model, simplexCapped);
    CHECK(simplexStop.evidence.termination == Termination::IterationCap);
    REQUIRE(simplexStop.packing.has_value());
    CHECK(m5Oracles::packingValid(model, *simplexStop.packing, simplexStop.impossible));
    CHECK(simplexStop.impossible == vector<size_t>{10});
    CHECK(simplexStop.evidence.lpStatus != "converged");
    REQUIRE(simplexStop.evidence.incumbent.has_value());
    if (simplexStop.evidence.lowerBound) CHECK(*simplexStop.evidence.lowerBound <= best);
    m5Evidence::note("expected", {"round cap and simplex cap: termination iteration cap", "checked packing",
                                  "lower bound <= optimum <= incumbent", "item 10 left"});
}

TEST_CASE("U-CG-07/synthetic: stopped before a first result says none and keeps every good; after, the checked result and an honest gap" * doctest::test_suite("m5")) {
    const ScalarModel model = scalarModel({6, 5, 3, 2, 2, 2}, 10);
    const ScalarResult before = solveCuttingStock(model, ScalarLimits(), [] { return true; });
    CHECK_FALSE(before.packing.has_value());
    CHECK(before.evidence.termination == Termination::Budget);
    CHECK(before.impossible.empty());
    CHECK(before.evidence.proof != ProofStatus::ProvenInfeasible);
    CHECK_FALSE(before.evidence.incumbent.has_value());

    int checks = 0;
    const ScalarResult after = solveCuttingStock(model, ScalarLimits(), [&] { return ++checks > 1; });
    REQUIRE(after.packing.has_value());
    CHECK(m5Oracles::packingValid(model, *after.packing, after.impossible));
    CHECK(after.evidence.termination == Termination::Budget);
    CHECK(after.packing->bins.size() == 3);
    CHECK(after.evidence.proof == ProofStatus::Unproven);
    REQUIRE(after.evidence.incumbent.has_value());
    CHECK(*after.evidence.incumbent == 3);
    if (after.evidence.lowerBound) CHECK(*after.evidence.lowerBound <= m5Oracles::bruteForceBins(model.sizes, 10));

    // At lane level: a run stopped before any result ships nothing, keeps every good as "no result" and
    // never calls it impossible.
    const M5LaneDateInput input = laneWith(fx30Units("u", 40), 2);
    FakeClock clock;
    M5RunServices services(clock);
    services.workAllowance = 0;
    const M5LaneOutcome stopped = planLaneDate(input, syntheticProfile(), physicalSettings(), services);
    CHECK(shippedCount(stopped) == 0);
    CHECK(stopped.leftOver.size() == 40);
    CHECK(countCause(stopped, LeftOverCause::NoResult) == 40);
    CHECK(countCause(stopped, LeftOverCause::Unfittable) == 0);
    CHECK(stopped.proof != ProofStatus::ProvenInfeasible);
    m5Evidence::note("expected", {"before: no packing, no incumbent, nothing impossible", "after: 3 bins, unproven",
                                  "lane stopped first: 40 kept as no result"});
}

TEST_CASE("U-CG-08/structural: M5 uses M4's one columnGeneration implementation, with no second copy" * doctest::test_suite("m5")) {
    // A code-review check over the source tree: the covering LP and the pattern pricer are defined once,
    // in src/columnGeneration, and M5's solver calls them.
    const regex lpDefinition(R"(CoveringLpSolution\s+solveCoveringLp\s*\([^;]*\)\s*\{)");
    const regex pricerDefinition(R"(class\s+PatternPricer\b[^;]*\{)");
    const regex simplexLike(R"(\b(pivot|simplex)\w*\s*\()", regex::icase);
    map<string, size_t> lpDefinitions, pricerDefinitions;
    vector<string> m5SimplexCode;
    for (const string& root : {string("src"), string("include")}) {
        for (const auto& entry : filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;
            const string path = entry.path().generic_string();
            ifstream file(entry.path(), ios::binary);
            const string text((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
            lpDefinitions[path] += static_cast<size_t>(distance(sregex_iterator(text.begin(), text.end(), lpDefinition), sregex_iterator()));
            pricerDefinitions[path] += static_cast<size_t>(distance(sregex_iterator(text.begin(), text.end(), pricerDefinition), sregex_iterator()));
            if (path.find("/m5") != string::npos && regex_search(text, simplexLike)) m5SimplexCode.push_back(path);
        }
    }
    size_t lpTotal = 0, pricerTotal = 0;
    for (const auto& [path, count] : lpDefinitions) {
        lpTotal += count;
        if (count) CHECK(path == "src/columnGeneration/columnGeneration.cpp");
    }
    for (const auto& [path, count] : pricerDefinitions) {
        pricerTotal += count;
        if (count) CHECK(path.find("columnGeneration") != string::npos);
    }
    CHECK(lpTotal == 1);
    CHECK(pricerTotal == 1);
    CHECK(m5SimplexCode.empty());
    ifstream solver("src/m5Solver/m5Solver.cpp", ios::binary);
    REQUIRE(solver.good());
    const string solverText((istreambuf_iterator<char>(solver)), istreambuf_iterator<char>());
    CHECK(solverText.find("#include \"columnGeneration.hpp\"") != string::npos);
    CHECK(solverText.find("solveCoveringLp(") != string::npos);
    CHECK(solverText.find("PatternPricer") != string::npos);
    m5Evidence::note("expected", {"solveCoveringLp defined once (src/columnGeneration)", "PatternPricer defined once", "m5Solver calls both"});
}

TEST_CASE("X-CG-08 at its round cap pricing stops with a checked result and no runaway columns" * doctest::test_suite("m5")) {
    const ScalarModel model = scalarModel({6, 5, 3, 2, 2, 2, 4, 4, 7, 1}, 10);
    ScalarLimits capped;
    capped.maxColumnRounds = 1;
    const ScalarResult result = solveCuttingStock(model, capped);
    REQUIRE(result.packing.has_value());
    CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
    CHECK(result.evidence.columnsGenerated <= model.sizes.size() + 1);
    CHECK(static_cast<long long>(result.packing->bins.size()) == m5Oracles::bruteForceBins(model.sizes, 10));
}

TEST_CASE("X-CG-11 stopped before or after a first result: none, or the checked FFD" * doctest::test_suite("m5")) {
    const ScalarModel model = scalarModel({6, 5, 3, 2, 2, 2}, 10);
    const ScalarResult before = solveCuttingStock(model, ScalarLimits(), [] { return true; });
    CHECK_FALSE(before.packing.has_value());
    CHECK(before.evidence.termination == Termination::Budget);
    int checks = 0;
    const ScalarResult after = solveCuttingStock(model, ScalarLimits(), [&] { return ++checks > 1; });
    REQUIRE(after.packing.has_value());
    CHECK(m5Oracles::packingValid(model, *after.packing, after.impossible));
    CHECK(after.evidence.termination == Termination::Budget);
    CHECK(after.packing->bins.size() == 3);
    CHECK(after.evidence.proof == ProofStatus::Unproven);
}

TEST_CASE("X-CG-07 near-degenerate duals: the solve ends at the best answer" * doctest::test_suite("m5")) {
    const ScalarModel model = scalarModel({5, 5, 5, 5, 3, 3, 3, 3, 2, 2}, 10);
    const ScalarResult result = solveCuttingStock(model, ScalarLimits());
    CHECK(static_cast<long long>(result.packing->bins.size()) == m5Oracles::bruteForceBins(model.sizes, 10));
}

TEST_CASE("X-META-07 X-META-09 more capacity or one more reservation is never worse" * doctest::test_suite("m5")) {
    ScalarModel model = scalarModel({6, 5, 4, 3, 3, 2}, 10);
    const size_t tight = solveCuttingStock(model, ScalarLimits()).packing->bins.size();
    model.capacity = 11;
    CHECK(solveCuttingStock(model, ScalarLimits()).packing->bins.size() <= tight);
    model.sizes.pop_back();
    CHECK(solveCuttingStock(model, ScalarLimits()).packing->bins.size() <= tight);
    const M5LaneDateInput two = laneWith(concat(fx30Units("a", 40, "A", 0), fx30Units("b", 40, "B", 3)), 2);
    const M5LaneDateInput three = laneWith(concat(fx30Units("a", 40, "A", 0), fx30Units("b", 40, "B", 3)), 3);
    CHECK(shippedCount(plan(three, syntheticProfile())) >= shippedCount(plan(two, syntheticProfile())));
}

TEST_CASE("X-META-03 X-META-05 shuffled or renamed rows give the same shipments and trucks" * doctest::test_suite("m5")) {
    vector<Atom> atoms = concat(fx30Units("a", 45, "A", 0), fx30Units("b", 25, "B", 4));
    const M5LaneOutcome base = plan(laneWith(atoms, 2), syntheticProfile());
    mt19937 generator(7);
    shuffle(atoms.begin(), atoms.end(), generator);
    const M5LaneDateInput shuffled = laneWith(atoms, 2);
    const M5LaneOutcome reshuffled = plan(shuffled, syntheticProfile());
    CHECK(shippedCount(reshuffled) == shippedCount(base));
    CHECK(reshuffled.selected->trucks.size() == base.selected->trucks.size());
    vector<Atom> renamed = atoms;
    for (Atom& atom : renamed) {
        atom.id = "r-" + atom.id;
        atom.group = atom.group == "A" ? "Z" : "Y";
        atom.components[0].source = "r-" + atom.components[0].source;
    }
    const M5LaneDateInput renamedInput = laneWith(renamed, 2);
    const M5LaneOutcome afterRename = plan(renamedInput, syntheticProfile());
    CHECK(shippedCount(afterRename) == shippedCount(base));
    CHECK(shippedOfGroup(afterRename, renamedInput, "Z") == shippedOfGroup(base, laneWith(concat(fx30Units("a", 45, "A", 0), fx30Units("b", 25, "B", 4)), 2), "A"));
}

TEST_CASE("X-META-04 X-DATA-07 a line split across stacks plans the same and keeps its identity" * doctest::test_suite("m5")) {
    vector<Atom> split = fx30Units("u", 30);
    for (size_t unit = 0; unit < 15; ++unit) split[unit].components[0].source = "line-first-half";
    for (size_t unit = 15; unit < 30; ++unit) split[unit].components[0].source = "line-second-half";
    const M5LaneDateInput input = laneWith(split, 1);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(shippedCount(outcome) == 30);
    CHECK(reconcile(*buildSnapshot(input).snapshot, *outcome.selected).balanced);
}

TEST_CASE("X-META-10 seeded random inputs: either refused as invalid or planned within every invariant" * doctest::test_suite("m5")) {
    mt19937 generator(31337);
    const vector<double> odd{-1.0, 0.0, numeric_limits<double>::quiet_NaN(), numeric_limits<double>::infinity()};
    for (int round = 0; round < 40; ++round) {
        vector<Atom> atoms;
        const size_t count = uniform_int_distribution<size_t>(1, 40)(generator);
        for (size_t unit = 0; unit < count; ++unit) {
            const double length = uniform_real_distribution<double>(20, 140)(generator);
            const double width = uniform_real_distribution<double>(20, min(length, 100.0))(generator);
            atoms.push_back(unitOf("r" + to_string(unit), unit % 3 == 0 ? "B" : "A", length, width,
                                   uniform_real_distribution<double>(20, 120)(generator), uniform_real_distribution<double>(100, 6000)(generator),
                                   uniform_int_distribution<int>(0, 11)(generator)));
        }
        if (round % 4 == 0) atoms.front().weightLb = odd[static_cast<size_t>(round / 4) % odd.size()];
        const M5LaneDateInput input = laneWith(atoms, uniform_int_distribution<size_t>(0, 4)(generator));
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        INFO("round " << round);
        if (outcome.kind == OutcomeKind::InvalidInput) {
            CHECK(round % 4 == 0);
            continue;
        }
        requireInvariants(outcome, input);
    }
}

// ------------------------------------------------------------------------- dates and carry

TEST_CASE("X-DATE-02 X-DATE-03 U-RS-09 U-RS-10 U-RS-11 date rules on the departure" * doctest::test_suite("m5")) {
    const M5LaneDateInput lateAvailable =
        laneWith(concat(fx30Units("d", 30), fx30Units("d5-", 30, "A", 0, dayAfter(5), dayAfter(1))), 2, kDefaultTrailer, true);
    CHECK(shippedCount(plan(lateAvailable, syntheticProfile())) == 30);
    const M5LaneDateInput pullOff = laneWith(concat(fx30Units("d", 30), fx30Units("d1-", 30, "A", 0, dayAfter(1))), 2);
    const M5LaneOutcome off = plan(pullOff, syntheticProfile());
    CHECK(off.selected->trucks.size() == 1);
    CHECK(countCause(off, LeftOverCause::NotEligible) == 30);
    const M5LaneDateInput window = laneWith(concat(fx30Units("d9-", 10, "A", 0, dayAfter(9)), fx30Units("d11-", 10, "A", 0, dayAfter(11))), 1,
                                            kDefaultTrailer, true);
    const set<string> shipped = shippedIds(plan(window, syntheticProfile()), window);
    CHECK(shipped.size() == 10);
    CHECK(all_of(shipped.begin(), shipped.end(), [](const string& id) { return id.rfind("d9-", 0) == 0; }));
}

TEST_CASE("X-DATE-04 X-DATE-05 U-RS-08 carry: 30 ship on D, 30 move once to D+4 keeping their age" * doctest::test_suite("m5")) {
    M5LaneDateInput first = laneWith(fx30Units("u", 60, "A", 3), 1);
    M5LaneDateInput fourLater = laneWith({}, 1);
    fourLater.departure = dayAfter(4);
    FakeClock clock;
    const LinkedDateBenchmark carried = benchmarkLinkedDates({first, fourLater}, syntheticProfile(), physicalSettings(), clock);
    REQUIRE(carried.outcomes.size() == 2);
    CHECK(shippedCount(carried.outcomes[0]) == 30);
    CHECK(shippedCount(carried.outcomes[1]) == 30);
    for (const Atom& atom : carried.inputs[1].atoms) {
        CHECK(atom.components[0].dueDate == kD);
        CHECK(atom.components[0].priority == 3);
    }
    // The last reserved date: what does not fit stays over, no date is made up.
    const LinkedDateBenchmark last = benchmarkLinkedDates({first}, syntheticProfile(), physicalSettings(), clock);
    CHECK(shippedCount(last.outcomes[0]) == 30);
    CHECK(countCause(last.outcomes[0], LeftOverCause::OverReservation) == 30);
}

TEST_CASE("X-DATE-07 U-CF-06 carry and pull never hold the same goods twice across two dates" * doctest::test_suite("m5")) {
    const vector<Atom> required = fx30Units("r", 10), future = fx30Units("f", 20, "A", 0, dayAfter(1));
    M5LaneDateInput today = laneWith(concat(required, future), 21, kDefaultTrailer, true);
    M5LaneDateInput tomorrow = laneWith(future, 20, kDefaultTrailer, true);
    tomorrow.departure = dayAfter(1);
    for (Atom& atom : tomorrow.atoms) atom.components[0].offerKind = OfferKind::Required;
    FakeClock clock;
    const LinkedDateBenchmark selected = benchmarkLinkedDates({today, tomorrow}, syntheticProfile(), physicalSettings(), clock);
    CHECK(shippedCount(selected.outcomes[0]) == 30);
    CHECK(selected.inputs[1].atoms.empty());
    M5PolicyProfile requiredOnly = syntheticProfile();
    requiredOnly.canGoFill = false;
    const LinkedDateBenchmark control = benchmarkLinkedDates({today, tomorrow}, requiredOnly, physicalSettings(), clock);
    CHECK(shippedCount(control.outcomes[0]) == 10);
    CHECK(shippedCount(control.outcomes[1]) == 20);
    set<string> everShipped;
    for (size_t date = 0; date < 2; ++date) {
        for (const string& id : shippedIds(control.outcomes[date], control.inputs[date])) CHECK(everShipped.insert(id).second);
    }
    CHECK(everShipped.size() == 30);
}

TEST_CASE("X-DATE-07/reversedInputDates: reversing the source rows leaves each date's allocation unchanged" * doctest::test_suite("m5")) {
    const TrailerCapability capFifteen{630.0, 100.0, 108.0, 45000.0, 15};
    auto allocation = [&](bool reversed) {
        vector<Atom> future = fx30Units("t", 20, "A", 0, dayAfter(1));
        if (reversed) reverse(future.begin(), future.end());
        M5LaneDateInput today = laneWith(future, 1, capFifteen, true);
        M5LaneDateInput tomorrow = laneWith(future, 1, capFifteen, true);
        tomorrow.departure = dayAfter(1);
        for (Atom& atom : tomorrow.atoms) atom.components[0].offerKind = OfferKind::Required;
        FakeClock clock;
        const LinkedDateBenchmark linked = benchmarkLinkedDates({today, tomorrow}, syntheticProfile(), physicalSettings(), clock);
        REQUIRE(linked.outcomes.size() == 2);
        return vector<set<string>>{shippedIds(linked.outcomes[0], linked.inputs[0]), shippedIds(linked.outcomes[1], linked.inputs[1])};
    };
    const vector<set<string>> normal = allocation(false), reversed = allocation(true);
    CHECK(normal == reversed);
    CHECK(normal[0].size() == 15);
    CHECK(normal[1].size() == 5);
    set<string> everShipped;
    for (const set<string>& date : normal) {
        for (const string& id : date) CHECK(everShipped.insert(id).second);
    }
    CHECK(everShipped.size() == 20);
    m5Evidence::note("expected", {"15 on D", "5 on D+1", "same IDs in both row orders"});
}

TEST_CASE("X-DATE-10 date arithmetic crosses months, years and leap days with M4's helper" * doctest::test_suite("m5")) {
    CHECK(ob::addDays("2026-12-25", 10) == "2027-01-04");
    CHECK(ob::addDays("2028-02-20", 10) == "2028-03-01");
    Component component;
    component.dueDate = "2027-01-04";
    component.availableDate = "2026-12-25";
    CHECK(componentEligibility(component, "2026-12-25", {true, 10}).eligible);
    component.dueDate = "2027-01-05";
    CHECK_FALSE(componentEligibility(component, "2026-12-25", {true, 10}).eligible);
}

// ------------------------------------------------------------------------------ input data

TEST_CASE("X-DATA-01 X-DATA-08 X-DATA-14 M5 takes M4's unit loads as given and checks each quantity" * doctest::test_suite("m5")) {
    vector<Atom> atoms;
    for (const auto& [prefix, count] : vector<pair<string, size_t>>{{"cs", 24}, {"dis", 24}, {"pal", 30}}) {
        for (size_t unit = 0; unit < count; ++unit) atoms.push_back(display(prefix + to_string(100 + unit)));
    }
    const M5LaneDateInput input = laneWith(atoms, 3);
    for (const MethodPolicy policy : {MethodPolicy::PatternOnly, MethodPolicy::BlocksOnly}) {
        M5PolicyProfile profile = syntheticProfile();
        profile.method = policy;
        const M5LaneOutcome outcome = plan(input, profile);
        double unitLoads = 0.0;
        for (const TruckPlan& truck : outcome.selected->trucks) {
            for (const size_t atom : truck.atoms) unitLoads += input.atoms[atom].unitLoads;
        }
        CHECK(unitLoads == 78.0);
    }
    for (const double bad : {-1.0, 0.0, numeric_limits<double>::quiet_NaN()}) {
        M5LaneDateInput broken = laneWith(fx30Units("u", 2), 1);
        broken.atoms[0].unitLoads = bad;
        broken.atoms[0].components[0].unitLoads = bad;
        CHECK(plan(broken, syntheticProfile()).kind == OutcomeKind::InvalidInput);
    }
    M5LaneDateInput fraction = laneWith(fx30Units("u", 2), 1);
    fraction.atoms[0].unitLoads = fraction.atoms[0].components[0].unitLoads = 0.5;
    CHECK(plan(fraction, syntheticProfile()).kind == OutcomeKind::CompletedWithDispatch);
}

TEST_CASE("X-DATA-02 conservation is checked per source line, not only in total" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("x", 10);
    for (Atom& atom : atoms) atom.components[0].source = "line-X";
    vector<Atom> y = fx30Units("y", 20);
    for (Atom& atom : y) atom.components[0].source = "line-Y";
    atoms = concat(atoms, y);
    const M5LaneDateInput input = laneWith(atoms, 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan swapped;
    TruckPlan truck;
    truck.group = "A";
    for (size_t atom = 0; atom < 10; ++atom) truck.atoms.push_back(atom);
    swapped.trucks.push_back(truck);
    // Leftovers name an X atom twice and drop one Y atom: the same 30 in total.
    for (size_t atom = 10; atom < 29; ++atom) swapped.leftOver.push_back({atom, LeftOverCause::OverReservation});
    swapped.leftOver.push_back({0, LeftOverCause::OverReservation});
    const Reconciliation result = reconcile(snapshot, swapped);
    CHECK_FALSE(result.balanced);
    CHECK(any_of(result.findings.begin(), result.findings.end(), [](const string& finding) { return finding.find("line-Y") != string::npos; }));
}

TEST_CASE("X-DATA-09 identities stay as M4 resolved them: 001234 and 1234 are two products" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("u", 2);
    atoms[0].components[0].product = "001234";
    atoms[1].components[0].product = "1234";
    atoms[0].group = atoms[1].group = "group_1757_1724";
    const M5LaneDateInput input = laneWith(atoms, 1);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(shippedCount(outcome) == 2);
    CHECK(input.atoms[0].components[0].product != input.atoms[1].components[0].product);
}

TEST_CASE("X-DATA-10 a delta from another snapshot is refused as stale" * doctest::test_suite("m5")) {
    const M5LaneDateInput runOne = laneWith(fx30Units("u", 30), 1);
    M5LaneDateInput runTwo = runOne;
    runTwo.sourceVersion = "a different extract";
    const M5LaneOutcome planned = plan(runOne, syntheticProfile());
    const DemandSnapshot other = *buildSnapshot(runTwo).snapshot;
    CommitLedger ledger;
    OperationalState state{other.id, other.input.pool.version, {}, 0, 1};
    CHECK(ledger.applyIfCurrent(*planned.delta, other, state) == ApplyStatus::StaleSnapshot);
}

TEST_CASE("X-DATA-12 counts near 3e9 stay exact" * doctest::test_suite("m5")) {
    CHECK(patternCopies(3'000'000'000LL, 1, 4'000'000'000LL) == 3'000'000'000LL);
    AllocationKey total, part;
    part.shippedByClass = {3'000'000'000'000LL};
    part.shippedByPriority.assign(12, 0);
    addTo(total, part);
    addTo(total, part);
    CHECK(total.shippedByClass.front() == 6'000'000'000'000LL);
}

// ---------------------------------------------------------------------------------- report

TEST_CASE("X-REPORT-05 averages weight each truck once, never the average of lane averages" * doctest::test_suite("m5")) {
    FullnessLabel a, b, c;
    a.bucket = b.bucket = c.bucket = FullnessBucket::GoodsRemain;
    a.rearInches = 2000.0;
    b.rearInches = 3200.0;
    c.rearInches = 3400.0;
    CHECK(*rearInchesByBucket({a, b, c}).at("goods remain").averageRearInches == doctest::Approx(2866.6667));
}

TEST_CASE("X-REPORT-10 four trucks: bucket means 20, 100, 20; overall 40, not the mean of means" * doctest::test_suite("m5")) {
    vector<FullnessLabel> labels(4);
    const vector<double> rears{10, 30, 100, 20};
    const vector<FullnessBucket> buckets{FullnessBucket::GoodsRemain, FullnessBucket::GoodsRemain, FullnessBucket::NoGoodsRemain,
                                         FullnessBucket::RuleLimited};
    for (size_t truck = 0; truck < 4; ++truck) {
        labels[truck].rearInches = rears[truck];
        labels[truck].bucket = buckets[truck];
    }
    const auto averages = rearInchesByBucket(labels);
    CHECK(*averages.at("goods remain").averageRearInches == 20.0);
    CHECK(*averages.at("no goods remain").averageRearInches == 100.0);
    CHECK(*averages.at("rule-limited").averageRearInches == 20.0);
    double sum = 0.0;
    for (const double rear : rears) sum += rear;
    CHECK(sum / 4.0 == 40.0);
}

TEST_CASE("X-REPORT-07 U-RP-02 U-RP-05 90 units: 60 shipped, 30 left, counted once" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 90), 2);
    const M5PolicyProfile profile = syntheticProfile();
    const M5ReportData report = buildReportData(plan(input, profile), input, &profile, {"PRODUCTION", 20, "fixture"});
    CHECK(report.unitLoadsIn == 90.0);
    CHECK(report.unitLoadsShipped == 60.0);
    CHECK(report.unitLoadsLeftByCause.at("over the reservation") == 30.0);
    const string text = renderAscii(report);
    CHECK(text.find("groups sharing the reservation: A") != string::npos);
    CHECK(text.find("time:") != string::npos);
    CHECK(text.find("seconds per loaded truck") != string::npos);
}

// FX-GAP on a lane: `count` units of 60 x 60 in on ten slots. Two never stand across the 100 in floor
// and ten rows of 60 in fill 630 in, so a truck holds at most 10: ceil(count / 10) trucks are needed,
// and the floor-area bound B = ceil(count x 3,600 / 63,000) never exceeds that (a valid bound).
M5LaneDateInput gapLane(int count) {
    vector<Atom> atoms;
    for (int unit = 0; unit < count; ++unit) atoms.push_back(unitOf("s" + to_string(100 + unit), "A", 60, 60, 50, 1000));
    return laneWith(atoms, 10);
}

// The trucks gapLane(count) needs at least: ten to a truck.
long long gapLaneTrucks(int count) { return (count + 9) / 10; }

void requireEscalation(const M5LaneOutcome& outcome, int count, long long quick, long long bound, bool escalated) {
    REQUIRE(outcome.escalation.quickTrucks);
    REQUIRE(outcome.escalation.bound);
    CHECK(*outcome.escalation.quickTrucks == quick);
    CHECK(*outcome.escalation.bound == bound);
    CHECK(*outcome.escalation.bound <= gapLaneTrucks(count));
    CHECK(quick >= gapLaneTrucks(count));
    CHECK(outcome.escalation.escalated == escalated);
}

TEST_CASE("U-FF-01/approvedPolicy: A is exactly G = 1 above a valid bound with allowance left: B runs" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = gapLane(30);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(syntheticProfile().gapTrucks == 1);
    requireEscalation(outcome, 30, 3, 2, true);
    REQUIRE(outcome.methods.size() == 2);
    CHECK(outcome.methods[1].ran);
    CHECK(outcome.escalation.gap == "1 truck, 50.0% above the bound of 2");
    CHECK_FALSE(outcome.timing.exhausted);
    CHECK(outcome.allowanceSeconds - outcome.timing.elapsedSeconds > 0.0);
    CHECK(shippedCount(outcome) == 30);
    m5Evidence::note("expected", {"F 3, valid B 2, G 1, allowance left", "escalated: B ran", "gap 1 truck, 50.0%"});
}

TEST_CASE("U-FF-02/approvedPolicy: A is G + 1 above the bound: B runs with allowance left, and is recorded as not started without" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = gapLane(50);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    requireEscalation(outcome, 50, 5, 3, true);
    REQUIRE(outcome.methods.size() == 2);
    CHECK(outcome.methods[1].ran);
    CHECK(outcome.escalation.gap == "2 trucks, 66.7% above the bound of 3");

    // No allowance left: A finishes with F = 5 against B = 3, then the test hook spends the whole
    // allowance before B's turn. B starts no fresh time; the run records the exhaustion and keeps A's plan.
    FakeClock clock;
    M5RunServices services(clock);
    services.allowanceSeconds = 5.0;
    services.afterMethod = [&](MethodId method) {
        if (method == MethodId::PatternRepeat) clock.advance(5.0);
    };
    const M5LaneOutcome spent = planLaneDate(input, syntheticProfile(), physicalSettings(), services);
    requireInvariants(spent, input);
    requireEscalation(spent, 50, 5, 3, false);
    REQUIRE(spent.methods.size() == 2);
    CHECK(spent.methods[0].ran);
    CHECK(spent.methods[0].validated);
    CHECK(spent.methods[0].trucks == 5);
    CHECK(spent.methods[0].selected);
    CHECK_FALSE(spent.methods[1].ran);
    CHECK(spent.methods[1].skippedBecause.find("allowance was spent") != string::npos);
    CHECK(spent.stop == StopReason::BudgetExhausted);
    CHECK(spent.timing.exhausted);
    CHECK(spent.timing.elapsedSeconds == 5.0);
    CHECK(spent.kind == OutcomeKind::TimeoutWithIncumbent);
    CHECK(shippedCount(spent) == 50);
    CHECK(spent.leftOver.empty());
    m5Evidence::note("expected", {"F 5, B 3: escalated with allowance",
                                  "A done (F 5), allowance then spent: B not started, not escalated, exhausted at 5 s, A's 5 trucks kept"});
}

TEST_CASE("U-FF-03/approvedPolicy: A equal to a valid bound never escalates" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = gapLane(20);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    requireEscalation(outcome, 20, 2, 2, false);
    REQUIRE(outcome.methods.size() == 2);
    CHECK_FALSE(outcome.methods[1].ran);
    CHECK(outcome.methods[1].skippedBecause.find("equal the valid bound of 2") != string::npos);
    CHECK(outcome.escalation.gap == "0 trucks, 0.0% above the bound of 2");
    m5Evidence::note("expected", {"F 2 = B 2: B not run", "gap 0 trucks"});
}

TEST_CASE("U-FF-05/approvedPolicy: the report names every escalated lane with its gap in 3C" * doctest::test_suite("m5")) {
    for (const auto& [count, gap] : vector<pair<int, string>>{{30, "1 truck, 50.0% above the bound of 2"}, {50, "2 trucks, 66.7% above the bound of 3"}}) {
        const M5LaneDateInput input = gapLane(count);
        const string text = renderAscii(buildReportData(plan(input, syntheticProfile()), input, nullptr, {}));
        INFO(count << " units");
        const size_t box = text.find("3C ");
        REQUIRE(box != string::npos);
        CHECK(text.find("quick count vs bound: " + gap, box) != string::npos);
        CHECK(text.find("method B (rolling blocks): ran", box) != string::npos);
    }
    const M5LaneDateInput equal = gapLane(20);
    const string text = renderAscii(buildReportData(plan(equal, syntheticProfile()), equal, nullptr, {}));
    CHECK(text.find("quick count vs bound: 0 trucks, 0.0% above the bound of 2") != string::npos);
    CHECK(text.find("method B (rolling blocks): not run") != string::npos);
    m5Evidence::note("expected", {"30 units: 1 truck, 50.0%", "50 units: 2 trucks, 66.7%", "20 units: not escalated, 0 trucks"});
}

TEST_CASE("X-REPORT-08 U-FF-05 U-RP-03 every method shows its real status and the escalation gap" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 60), 3);
    const string text = renderAscii(buildReportData(plan(input, syntheticProfile()), input, nullptr, {}));
    CHECK(text.find("method A (repeated patterns): ran, validated, selected") != string::npos);
    CHECK(text.find("method B (rolling blocks): not run: A's 2 trucks equal the valid bound of 2") != string::npos);
    CHECK(text.find("quick count vs bound: 0 trucks, 0.0% above the bound of 2") != string::npos);
    CHECK(text.find("(run)") == string::npos);
}

TEST_CASE("U-RP-04 U-RP-07 the benchmark section and bucket fill averages are printed apart" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 45), 2);
    const string text = renderAscii(buildReportData(plan(input, syntheticProfile()), input, nullptr, {}));
    CHECK(text.find("3D Threshold benchmark") != string::npos);
    CHECK(text.find("average rear inches, goods remain") != string::npos);
    CHECK(text.find("average rear inches, no goods remain") != string::npos);
    CHECK(text.find("average rear inches, rule-limited") != string::npos);
}

TEST_CASE("X-E2E-08 a comparison mixing bases is flagged: 1,440 - 1,308 is 132, not 154" * doctest::test_suite("m5")) {
    REQUIRE(unusedComparisonMismatch(1440, 1308, 154).has_value());
    CHECK(unusedComparisonMismatch(1440, 1308, 154)->find("132") != string::npos);
    CHECK_FALSE(unusedComparisonMismatch(1440, 1286, 154).has_value());
    CHECK_FALSE(unusedComparisonMismatch(1440, 1342, 98).has_value());
}

TEST_CASE("U-PF-01 U-PF-07 timing is recorded per phase on one deadline; sequential work adds" * doctest::test_suite("m5")) {
    FakeClock clock;
    Budget budget({21.0, nullopt, "secondsPerLoad x reserved trucks (CL-05)"}, clock);
    budget.startPhase("methodA");
    clock.advance(8.0);
    budget.startPhase("methodB");
    clock.advance(8.0);
    budget.finish();
    CHECK(budget.ledger().secondsByPhase.at("methodA") == 8.0);
    CHECK(budget.elapsedSeconds() == 16.0);
    CHECK(budget.remainingSeconds() == 5.0);
    clock.advance(4.999);
    CHECK_FALSE(budget.exhausted());
    clock.advance(0.001);
    CHECK(budget.exhausted());
    const M5LaneOutcome outcome = plan(laneWith(fx30Units("u", 30), 21), syntheticProfile());
    CHECK(outcome.allowanceSeconds == 210.0);
    CHECK(outcome.timing.secondsByPhase.count("methodA") == 1);
    CHECK(outcome.timing.secondsByPhase.count("validation") == 1);
}

TEST_CASE("U-SP-04/synthetic: a group of one unit load gets a clear outcome: shipped, left over or rejected, never dropped" * doctest::test_suite("m5")) {
    // Shipped: room for it.
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 30, "A"), {fx30("lonely", "B")}), 2);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedOfGroup(outcome, input, "B") == 1);
    CHECK(shippedCount(outcome) + outcome.leftOver.size() == 31);
    // Left over: one slot, which the 30 of group A fill first by priority.
    vector<Atom> urgent = fx30Units("a", 30, "A");
    Atom later = fx30("lonely", "B");
    later.components[0].priority = 11;
    const M5LaneDateInput full = laneWith(concat(urgent, {later}), 1);
    const M5LaneOutcome fullOutcome = plan(full, syntheticProfile());
    requireInvariants(fullOutcome, full);
    CHECK(shippedOfGroup(fullOutcome, full, "B") == 0);
    CHECK(causeOf(fullOutcome, full, "lonely") == LeftOverCause::OverReservation);
    CHECK(shippedCount(fullOutcome) + fullOutcome.leftOver.size() == 31);
    // Rejected: taller than the trailer.
    const M5LaneDateInput tall = laneWith(concat(fx30Units("a", 30, "A"), {unitOf("lonely", "B", 48, 40, 130, 500)}), 2);
    const M5LaneOutcome tallOutcome = plan(tall, syntheticProfile());
    requireInvariants(tallOutcome, tall);
    CHECK(causeOf(tallOutcome, tall, "lonely") == LeftOverCause::Unfittable);
    CHECK(shippedCount(tallOutcome) + tallOutcome.leftOver.size() == 31);
    m5Evidence::note("expected", {"room: B's one unit ships", "one slot, B later: left over the reservation", "130 in tall: rejected as unfittable"});
}

TEST_CASE("X-E2E-03 patterns with pull-forward and unavailable stock" * doctest::test_suite("m5")) {
    auto units = [](const string& prefix, size_t count, const string& due, const string& available) {
        vector<Atom> atoms;
        for (size_t unit = 0; unit < count; ++unit) atoms.push_back(unitOf(prefix + to_string(100 + unit), "A", 100, 63, 108, 4500, 0, due, available));
        return atoms;
    };
    vector<Atom> atoms = units("a", 30, kD, kD);
    atoms = concat(atoms, units("b", 20, dayAfter(10), kD));
    atoms = concat(atoms, units("c", 20, dayAfter(11), kD));
    atoms = concat(atoms, units("e", 10, kD, dayAfter(1)));
    const M5PolicyProfile policy = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    const M5LaneDateInput off = laneWith(atoms, 8, kDefaultTrailer, false);
    const M5LaneOutcome withoutPull = plan(off, policy);
    CHECK(withoutPull.selected->trucks.size() == 3);
    CHECK(shippedCount(withoutPull) == 30);
    const M5LaneDateInput on = laneWith(atoms, 8, kDefaultTrailer, true);
    const M5LaneOutcome withPull = plan(on, policy);
    CHECK(withPull.selected->trucks.size() == 5);
    CHECK(shippedCount(withPull) == 50);
    for (const string& id : shippedIds(withPull, on)) CHECK((id[0] == 'a' || id[0] == 'b'));
    for (const TruckPlan& truck : withPull.selected->trucks) {
        double weight = 0.0, cube = 0.0;
        for (const size_t atom : truck.atoms) {
            weight += on.atoms[atom].weightLb;
            cube += on.atoms[atom].floor.cubeCubicIn;
        }
        CHECK(weight == 45000.0);
        CHECK(cube / 1728.0 == doctest::Approx(3937.5));
    }
}
