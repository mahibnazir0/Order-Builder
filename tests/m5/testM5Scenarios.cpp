// M5 scenarios across the allocation, block, repair, commit and state seams (v7 Sections 3, 4.4-4.8,
// 12, 14-17, 19-23). Small fixtures run through M5's direct seam (v7 0.4).
#include "doctest.h"
#include "floorPattern.hpp"
#include "isoDate.hpp"
#include "m5Commit.hpp"
#include "m5Integration.hpp"
#include "m5Ledger.hpp"
#include "m5Repair.hpp"
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
#include <numeric>
#include <random>
#include <regex>
#include <set>
#include <thread>
#include <tuple>

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

TEST_CASE("X-RUN-01/workCount: the same input stopped by a work count gives the same plan on 1, 2 and 8 threads" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 90), 3);
    const auto runOnce = [&](long long work) {
        FakeClock clock;
        M5RunServices services(clock);
        services.workAllowance = work;
        return planLaneDate(input, syntheticProfile(), physicalSettings(), services);
    };
    for (const long long work : {1LL, 3LL, 25LL, 1'000'000LL}) {
        INFO("work allowance " << work);
        const M5LaneOutcome reference = runOnce(work);
        requireInvariants(reference, input);
        for (const size_t threads : {size_t{1}, size_t{2}, size_t{8}}) {
            vector<M5LaneOutcome> outcomes(threads);
            vector<thread> workers;
            for (size_t index = 0; index < threads; ++index) workers.emplace_back([&, index] { outcomes[index] = runOnce(work); });
            for (thread& worker : workers) worker.join();
            for (const M5LaneOutcome& outcome : outcomes) {
                CHECK(outcome.kind == reference.kind);
                CHECK(outcome.stop == reference.stop);
                CHECK(outcome.leftOver.size() == reference.leftOver.size());
                REQUIRE(outcome.selected.has_value() == reference.selected.has_value());
                if (!outcome.selected) continue;
                REQUIRE(outcome.selected->trucks.size() == reference.selected->trucks.size());
                for (size_t truck = 0; truck < reference.selected->trucks.size(); ++truck) {
                    CHECK(outcome.selected->trucks[truck].atoms == reference.selected->trucks[truck].atoms);
                }
            }
        }
    }
    m5Evidence::note("expected", {"work allowances 1, 3, 25 and 1,000,000: each gives one plan whatever the thread count"});
}

TEST_CASE("X-RUN-01/wallClock: a run stopped by the clock is valid and honestly labelled, never a completed run" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 90), 3);
    for (const double allowance : {0.5, 5.0, 50.0}) {
        INFO("allowance " << allowance);
        FakeClock clock;
        M5RunServices services(clock);
        services.allowanceSeconds = allowance;
        services.methods = {make_shared<PatternPlanner>(), make_shared<BlockPlanner>()};
        services.beforeMethod = [&](MethodId) { clock.advance(3.0); };
        const M5LaneOutcome outcome = planLaneDate(input, syntheticProfile(), physicalSettings(), services);
        requireInvariants(outcome, input);
        const bool stopped = outcome.stop == StopReason::BudgetExhausted;
        if (stopped) {
            CHECK((outcome.kind == OutcomeKind::TimeoutWithIncumbent || outcome.kind == OutcomeKind::TimeoutNoIncumbent));
            CHECK(outcome.kind != OutcomeKind::CompletedWithDispatch);
            CHECK(outcome.kind != OutcomeKind::CompletedZeroDispatch);
        } else {
            CHECK(outcome.kind == OutcomeKind::CompletedWithDispatch);
        }
        if (outcome.selected) {
            CHECK(outcome.finalValidationPassed);
            REQUIRE(outcome.selected->validation.has_value());
            CHECK(outcome.selected->validation->allPassed());
        }
        const string text = renderAscii(buildReportData(outcome, input, nullptr, {}));
        CHECK(text.find(stopped ? "(allowance spent)" : "(completed)") != string::npos);
    }
    m5Evidence::note("expected", {"a clock stop is a timeout kind with a checked plan or none", "never labelled completed", "valid plans only"});
}

TEST_CASE("X-RUN-03/keepValidated: time runs out while a later method's candidate is being repaired; only checked candidates stand, never half-repaired contents" * doctest::test_suite("m5")) {
    // A is valid (two trucks of 30). B offers an overfull truck that repair must drop; the allowance is spent as B starts.
    const M5LaneDateInput input = laneWith(fx30Units("u", 61), 3);
    Truck firstThirty, secondThirty, overfull, lastThirty;
    for (size_t atom = 0; atom < 30; ++atom) firstThirty.push_back(atom);
    for (size_t atom = 30; atom < 60; ++atom) secondThirty.push_back(atom);
    for (size_t atom = 0; atom < 31; ++atom) overfull.push_back(atom);
    for (size_t atom = 31; atom < 61; ++atom) lastThirty.push_back(atom);
    FakeClock clock;
    M5RunServices services(clock);
    services.allowanceSeconds = 10.0;
    services.methods = {make_shared<FixedMethod>(MethodId::PatternRepeat, GroupTrucks{firstThirty, secondThirty}),
                        make_shared<FixedMethod>(MethodId::RollingBlocks, GroupTrucks{overfull, lastThirty})};
    services.beforeMethod = [&](MethodId method) {
        if (method == MethodId::RollingBlocks) clock.advance(100.0);
    };
    const M5LaneOutcome outcome = planLaneDate(input, syntheticProfile(), physicalSettings(), services);
    requireInvariants(outcome, input);
    CHECK(outcome.kind == OutcomeKind::TimeoutWithIncumbent);
    CHECK(outcome.stop == StopReason::BudgetExhausted);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 2);
    CHECK(shippedCount(outcome) == 60);
    for (const TruckPlan& truck : outcome.selected->trucks) {
        CHECK(truck.atoms.size() <= 30);
        CHECK(truck.atoms.size() > 0);
    }
    REQUIRE(outcome.selected->validation.has_value());
    CHECK(outcome.selected->validation->allPassed());
    CHECK(outcome.finalValidationPassed);
    CHECK(outcome.selected->evidence.proof != ProofStatus::ProvenOptimal);
    CHECK(outcome.proof != ProofStatus::ProvenOptimal);
    REQUIRE(outcome.delta.has_value());
    CHECK(outcome.delta->trucks.size() == 2);
    // Isolated until selected and committed: nothing is consumed by planning alone.
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    OperationalState state{snapshot.id, snapshot.input.pool.version, {}, 0, 3};
    CHECK(state.consumedAtoms.empty());
    CommitLedger ledger;
    CHECK(ledger.applyIfCurrent(*outcome.delta, snapshot, state) == ApplyStatus::Applied);
    CHECK(state.consumedAtoms.size() == 60);
    // The whole-lane-rollback variant is not the one selected here: the checked candidate is kept.
    CHECK_FALSE(services.wholeLaneRollback);
    m5Evidence::note("expected", {"A's two checked trucks of 30 stand", "B's overfull truck is never part of the result", "timeout status, unproven, committed only once selected"});
}

TEST_CASE("X-RUN-07/beforeCommit: a failure after provisional claims but before any commit releases them once; the retry plans the full original" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(blocks(39), 39);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    FakeClock clock;
    M5RunServices services(clock);
    services.allowanceSeconds = 100.0;
    services.afterBlock = [&](size_t trucks) {
        if (trucks >= 38) clock.advance(1000.0);
    };
    const M5LaneOutcome interrupted = planLaneDate(input, blocksOnly(), physicalSettings(), services);
    REQUIRE(interrupted.delta);
    CommitLedger ledger;
    OperationalState state{snapshot.id, snapshot.input.pool.version, {}, 0, 39};
    // The failure: the delta is never applied. Nothing was consumed, no slot was used, no claim is left.
    CHECK(state.consumedAtoms.empty());
    CHECK(state.slotsUsed == 0);
    // Retry from the full original: all 39 ship, and the earlier partial delta cannot be applied after it.
    const M5LaneOutcome retry = plan(input, blocksOnly());
    REQUIRE(retry.delta);
    CHECK(shippedCount(retry) == 39);
    CHECK(ledger.applyIfCurrent(*retry.delta, snapshot, state) == ApplyStatus::Applied);
    CHECK(state.consumedAtoms.size() == 39);
    CHECK(state.slotsUsed == 39);
    CHECK(ledger.applyIfCurrent(*interrupted.delta, snapshot, state) == ApplyStatus::PayloadConflict);
    CHECK(state.consumedAtoms.size() == 39);
    CHECK(state.slotsUsed == 39);
    CHECK(ledger.applyIfCurrent(*retry.delta, snapshot, state) == ApplyStatus::AlreadyApplied);
    m5Evidence::note("expected", {"before commit nothing is consumed", "retry from the full original ships 39", "the abandoned partial delta is a conflict, never a second claim"});
}

TEST_CASE("X-RUN-07/retainedBlocks: validated earlier blocks stay isolated until they are selected and committed" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(blocks(39), 39);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    FakeClock clock;
    M5RunServices services(clock);
    services.allowanceSeconds = 100.0;
    services.afterBlock = [&](size_t trucks) {
        if (trucks >= 38) clock.advance(1000.0);
    };
    const M5LaneOutcome partial = planLaneDate(input, blocksOnly(), physicalSettings(), services);
    requireInvariants(partial, input);
    REQUIRE(partial.selected);
    REQUIRE(partial.delta);
    CHECK(partial.selected->trucks.size() == 38);
    REQUIRE(partial.selected->validation.has_value());
    CHECK(partial.selected->validation->allPassed());
    CommitLedger ledger;
    OperationalState state{snapshot.id, snapshot.input.pool.version, {}, 0, 39};
    // Selected but not committed: the retained blocks claim nothing.
    CHECK(state.consumedAtoms.empty());
    CHECK(state.slotsUsed == 0);
    CHECK(ledger.applyIfCurrent(*partial.delta, snapshot, state) == ApplyStatus::Applied);
    CHECK(state.consumedAtoms.size() == 38);
    CHECK(state.slotsUsed == 38);
    const DemandSnapshot residual = residualAfter(snapshot, *partial.delta);
    const M5LaneOutcome rest = plan(residual.input, blocksOnly());
    REQUIRE(rest.delta);
    CHECK(shippedCount(rest) == 1);
    state.snapshot = residual.id;
    CHECK(ledger.applyIfCurrent(*rest.delta, residual, state) == ApplyStatus::Applied);
    CHECK(state.consumedAtoms.size() == 39);
    CHECK(state.slotsUsed == 39);
    CHECK(state.slotsUsed <= state.slotsReserved);
    m5Evidence::note("expected", {"38 validated blocks retained, nothing claimed until applied", "then the one remaining unit on the last slot", "39 shipped, 39 slots used, none twice"});
}

TEST_CASE("X-RUN-07/afterPartialCommit: after a partial commit the retry plans the reconciled residual; a fresh shipment of the full original is refused" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(blocks(39), 39);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    FakeClock clock;
    M5RunServices services(clock);
    services.allowanceSeconds = 100.0;
    services.afterBlock = [&](size_t trucks) {
        if (trucks >= 38) clock.advance(1000.0);
    };
    const M5LaneOutcome partial = planLaneDate(input, blocksOnly(), physicalSettings(), services);
    REQUIRE(partial.delta);
    CommitLedger ledger;
    OperationalState state{snapshot.id, snapshot.input.pool.version, {}, 0, 39};
    REQUIRE(ledger.applyIfCurrent(*partial.delta, snapshot, state) == ApplyStatus::Applied);
    const set<string> committed = state.consumedAtoms;
    const size_t slotsAfterPartial = state.slotsUsed;
    // Idempotent suppression: replaying the committed partial changes nothing.
    CHECK(ledger.applyIfCurrent(*partial.delta, snapshot, state) == ApplyStatus::AlreadyApplied);
    CHECK(state.consumedAtoms == committed);
    // A fresh shipment of the full original (all 39 units again) cannot be committed.
    const M5LaneOutcome full = plan(input, blocksOnly());
    REQUIRE(full.delta);
    CHECK(shippedCount(full) == 39);
    CHECK(ledger.applyIfCurrent(*full.delta, snapshot, state) == ApplyStatus::PayloadConflict);
    CHECK(state.consumedAtoms == committed);
    CHECK(state.slotsUsed == slotsAfterPartial);
    // The retry from the reconciled residual ships only the one unit left, on the last slot.
    const DemandSnapshot residual = residualAfter(snapshot, *partial.delta);
    CHECK(residual.input.atoms.size() == 1);
    CHECK(residual.input.pool.slots.size() == 1);
    const M5LaneOutcome retry = plan(residual.input, blocksOnly());
    REQUIRE(retry.delta);
    CHECK(shippedCount(retry) == 1);
    state.snapshot = residual.id;
    CHECK(ledger.applyIfCurrent(*retry.delta, residual, state) == ApplyStatus::Applied);
    CHECK(state.consumedAtoms.size() == 39);
    CHECK(state.slotsUsed == 39);
    // No stale acceptance or proof survives: the residual's plan carries its own validation context.
    REQUIRE(retry.selected);
    REQUIRE(retry.selected->validation.has_value());
    CHECK(retry.selected->validation->sourceSnapshot == residual.id);
    CHECK(partial.selected->validation->sourceSnapshot == snapshot.id);
    m5Evidence::note("expected", {"38 committed once, replay suppressed", "full original again: conflict, nothing consumed", "residual: one unit, last slot; 39 and 39 in the end"});
}

TEST_CASE("X-E2E-05/keepValidated: blocks stopped in block 3's repair: the checked 38 trucks stand with one item left; never a 39-truck success; the deadline is not renewed" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(blocks(39), 39);
    FakeClock clock;
    M5RunServices services(clock);
    services.allowanceSeconds = 100.0;
    services.afterBlock = [&](size_t trucks) {
        if (trucks >= 38) clock.advance(1000.0);
    };
    const M5LaneOutcome keep = planLaneDate(input, blocksOnly(), physicalSettings(), services);
    requireInvariants(keep, input);
    CHECK(keep.kind == OutcomeKind::TimeoutWithIncumbent);
    REQUIRE(keep.selected);
    CHECK(keep.selected->trucks.size() == 38);
    CHECK(keep.selected->trucks.size() != 39);
    CHECK(keep.leftOver.size() == 1);
    CHECK(keep.allowanceSeconds == 100.0);
    REQUIRE(keep.selected->validation.has_value());
    CHECK(keep.selected->validation->allPassed());
    CHECK(keep.selected->validation->physical == SectionStatus::Pass);
    CHECK(keep.finalValidationPassed);
    set<size_t> seen;
    for (const TruckPlan& truck : keep.selected->trucks) {
        for (const size_t atom : truck.atoms) CHECK(seen.insert(atom).second);
    }
    CHECK(seen.size() == 38);
    // The rollback variant returns no candidate; the original input still reaches 39 only by a fresh, unstopped run.
    FakeClock rollbackClock;
    M5RunServices rollbackServices(rollbackClock);
    rollbackServices.allowanceSeconds = 100.0;
    rollbackServices.wholeLaneRollback = true;
    rollbackServices.afterBlock = [&](size_t trucks) {
        if (trucks >= 38) rollbackClock.advance(1000.0);
    };
    const M5LaneOutcome rolled = planLaneDate(input, blocksOnly(), physicalSettings(), rollbackServices);
    CHECK_FALSE(rolled.selected.has_value());
    CHECK(countCause(rolled, LeftOverCause::NoResult) == 39);
    CHECK(shippedCount(plan(input, blocksOnly())) == 39);
    m5Evidence::note("expected", {"keep variant (the CL-10 contract chosen here): 38 checked trucks, 1 left, timeout, allowance 100", "rollback variant: no candidate, 39 NoResult", "never a 39-truck success from the stopped run"});
}

TEST_CASE("X-E2E-02/synthetic X-E2E-02/productionProfile priority after repair: B wins valid; with B's truck over the payload the winner is decided again by brute force; removed goods return once; no old proof is kept" * doctest::test_suite("m5")) {
    // R-2: A has 1 P0 + 29 P11, B has 20 P0; one reservation. Both valid: B wins.
    vector<Atom> atoms{fx30("a0", "A", 0)};
    atoms = concat(atoms, fx30Units("a11-", 29, "A", 11));
    atoms = concat(atoms, fx30Units("b0-", 20, "B", 0));
    const M5LaneDateInput valid = laneWith(atoms, 1);
    const M5LaneOutcome first = plan(valid, withAcceptance(syntheticProfile(), 40000.0, 2400.0));
    requireInvariants(first, valid);
    CHECK(shippedOfGroup(first, valid, "B") == 20);
    CHECK(shippedOfGroup(first, valid, "A") == 0);
    CHECK(shippedOfGroup(plan(valid, syntheticProfile()), valid, "B") == 20);

    // B's layout turns invalid: its units now weigh 2,500 lb, so 20 are 50,000 lb on a 45,000 lb payload.
    vector<Atom> heavy = atoms;
    for (Atom& atom : heavy) {
        if (atom.group == "B") atom.weightLb = 2500.0;
    }
    const M5LaneDateInput invalid = laneWith(heavy, 1);
    vector<size_t> allOfB;
    for (size_t atom = 0; atom < invalid.atoms.size(); ++atom) {
        if (invalid.atoms[atom].group == "B") allOfB.push_back(atom);
    }
    CHECK_FALSE(checkTruck(invalid, allOfB, kDefaultTrailer, physicalSettings()).fits);
    // Brute force over every legal single-group truck by M4's rule (P0 units first, then P11): B takes 18.
    int bestP0 = 0;
    string bestGroup;
    for (const string group : {"A", "B"}) {
        vector<size_t> members;
        for (size_t atom = 0; atom < invalid.atoms.size(); ++atom) {
            if (invalid.atoms[atom].group == group) members.push_back(atom);
        }
        sort(members.begin(), members.end(), [&](size_t left, size_t right) { return invalid.atoms[left].components[0].priority < invalid.atoms[right].components[0].priority; });
        double weight = 0.0;
        int p0 = 0;
        for (size_t position = 0; position < members.size() && position < 30; ++position) {
            const Atom& atom = invalid.atoms[members[position]];
            if (weight + atom.weightLb > 45000.0) break;
            weight += atom.weightLb;
            if (atom.components[0].priority == 0) ++p0;
        }
        if (p0 > bestP0) {
            bestP0 = p0;
            bestGroup = group;
        }
    }
    CHECK(bestGroup == "B");
    CHECK(bestP0 == 18);
    const M5LaneOutcome decided = plan(invalid, syntheticProfile());
    requireInvariants(decided, invalid);
    REQUIRE(decided.selected);
    CHECK(decided.selected->trucks.size() == 1);
    CHECK(shippedOfGroup(decided, invalid, "B") == 18);
    CHECK(shippedOfGroup(decided, invalid, "A") == 0);
    size_t bLeft = 0;
    for (const LeftOver& left : decided.leftOver) bLeft += invalid.atoms[left.atom].group == "B" ? 1 : 0;
    CHECK(bLeft == 2);
    set<size_t> seen;
    for (const TruckPlan& truck : decided.selected->trucks) {
        for (const size_t atom : truck.atoms) CHECK(seen.insert(atom).second);
    }
    for (const LeftOver& left : decided.leftOver) CHECK(seen.insert(left.atom).second);
    CHECK(seen.size() == invalid.atoms.size());

    // A repaired truck keeps no old proof or validation.
    const M5LaneOutcome original = plan(valid, syntheticProfile());
    REQUIRE(original.selected);
    CandidatePlan repaired = *original.selected;
    CandidateEditor(repaired).removeToLeftOver(0, repaired.trucks[0].atoms.back(), LeftOverCause::OverReservation);
    CHECK(repaired.evidence.proof != ProofStatus::ProvenOptimal);
    CHECK_FALSE(repaired.validation.has_value());
    m5Evidence::note("expected", {"valid: group B ships (R-2), also with the production profile", "B over the payload: decided again, B 18 P0 beats A, one truck", "the two removed goods are left over once; no old proof or validation"});
}

TEST_CASE("X-E2E-06/synthetic: a pattern that fits only the big trailer, saved under master V1, is never used on the small trailer or under V2" * doctest::test_suite("m5")) {
    const TrailerCapability small{630.0, 100.0, 108.0, 30000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput v1 = laneWith(fx30Units("u", 50), 2);
    // The big trailer holds the 30-unit pattern, the small one only 20: the pattern does not fit it.
    CHECK(bestSingleClassLoad(v1, 0, kDefaultTrailer, physicalSettings(), 100) == 30);
    CHECK(bestSingleClassLoad(v1, 0, small, physicalSettings(), 100) == 20);
    vector<size_t> thirty(30);
    iota(thirty.begin(), thirty.end(), size_t{0});
    CHECK(checkTruck(v1, thirty, kDefaultTrailer, physicalSettings()).fits);
    CHECK_FALSE(checkTruck(v1, thirty, small, physicalSettings()).fits);
    // Master V2: heavier units. The V1 class signature differs, so a V1-only load is not reused: 28 at most.
    vector<Atom> heavier = fx30Units("u", 50);
    for (Atom& atom : heavier) atom.weightLb = 1600.0;
    const M5LaneDateInput v2 = laneWith(heavier, 2);
    CHECK(classSignatureOf(v1.atoms[0]) != classSignatureOf(v2.atoms[0]));
    CHECK(bestSingleClassLoad(v2, 0, kDefaultTrailer, physicalSettings(), 100) == 28);
    CHECK_FALSE(checkTruck(v2, thirty, kDefaultTrailer, physicalSettings()).fits);
    const M5LaneOutcome outcome = plan(v2, syntheticProfile());
    requireInvariants(outcome, v2);
    for (const TruckPlan& truck : outcome.selected->trucks) CHECK(truck.atoms.size() <= 28);
    m5Evidence::note("expected", {"30-unit pattern: big trailer yes, small trailer no", "V2 weight: 28 at most, the V1 30 never reused"});
    m5Evidence::note("notCovered", "the report naming the master and the trailers: the product-master identity is passed to the M5 run header only after the feature/m4 merge (U-RP-01, PR-13)");
}

namespace {

// A clock whose zero is wherever the test puts it, for deadline arithmetic.
class StartedClock final : public Clock {
public:
    explicit StartedClock(double start) : now_(start) {}
    double nowSeconds() const override { return now_; }
    void advance(double seconds) { now_ += seconds; }

private:
    double now_;
};

M5LaneOutcome planUnderClock(const M5LaneDateInput& input, const M5PolicyProfile& profile, const Clock& clock) {
    const M5RunServices services(clock);
    return planLaneDate(input, profile, physicalSettings(), services);
}

}  // namespace

TEST_CASE("U-PF-06/deadlineOverflow: a clock whose start plus the allowance cannot be represented is refused before any search, never read as unlimited time" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 30), 3);
    for (const double start : {numeric_limits<double>::max(), 1.7e308, 1.0e300, numeric_limits<double>::infinity()}) {
        StartedClock clock(start);
        INFO("clock start " << start);
        const M5LaneOutcome outcome = planUnderClock(input, syntheticProfile(), clock);
        CHECK(outcome.kind == OutcomeKind::ConfigurationIncomplete);
        CHECK(shippedCount(outcome) == 0);
        CHECK(outcome.floorSearch.statesCharged == 0);
        CHECK(outcome.leftOver.size() == 30);
        CHECK_FALSE(outcome.delta.has_value());
    }
    // An ordinary clock, including one that has run for a year, is accepted.
    for (const double start : {0.0, 1000.0, 3.2e7}) {
        StartedClock clock(start);
        CHECK(planUnderClock(input, syntheticProfile(), clock).kind == OutcomeKind::CompletedWithDispatch);
    }
    m5Evidence::note("expected", {"a start of 1e300 or more with a 30 s allowance: configuration incomplete", "a normal clock (0, 1,000 s, one year) plans"});
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

namespace {

string sourceText(const string& path) {
    ifstream file(path, ios::binary);
    REQUIRE_MESSAGE(file.good(), path);
    return string((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
}

multiset<size_t> truckSizes(const M5LaneOutcome& outcome) {
    multiset<size_t> sizes;
    if (outcome.selected) {
        for (const TruckPlan& truck : outcome.selected->trucks) sizes.insert(truck.atoms.size());
    }
    return sizes;
}

M5PolicyProfile patternOnly() {
    M5PolicyProfile profile = syntheticProfile();
    profile.method = MethodPolicy::PatternOnly;
    return profile;
}

}  // namespace

TEST_CASE("U-RM-01/synthetic: the remainder goes to M4's packing and checking kernels, never back through the selector" * doctest::test_suite("m5")) {
    // 65 FX-30 on 3: two pattern trucks of 30, the remainder of 5 packed on the third; each unit once.
    const M5LaneDateInput input = laneWith(fx30Units("u", 65), 3);
    const M5LaneOutcome outcome = plan(input, patternOnly());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(truckSizes(outcome) == multiset<size_t>{5, 30, 30});
    CHECK(outcome.leftOver.empty());
    CHECK(outcome.finalValidationPassed);
    // The remainder truck's floor is M4's own arrangement of its goods.
    for (const TruckPlan& truck : outcome.selected->trucks) {
        vector<ob::FloorItem> floors;
        for (const size_t atom : truck.atoms) floors.push_back(input.atoms[atom].floor);
        const ob::FloorLayout m4 = ob::arrangeFloor(floors, trailerOf(kDefaultTrailer), physicalSettings().floor);
        CHECK(m4.fitsLength);
        CHECK(truck.layout.usedLengthIn == m4.usedLengthIn);
        CHECK(truck.layout.positions == m4.positions);
    }
    // Code review: the planning units reach M4 only through its packing and checking kernels; none includes
    // the selector, the lane classifier, the large-lane hand-over or the pipeline, so no residual can re-enter
    // routing and escalate into M5 again.
    const vector<string> routingHeaders{"selector.hpp", "demandSelector.hpp", "laneClassifier.hpp", "largeLanePlanner.hpp",
                                        "pipeline.hpp", "m5Integration.hpp"};
    const vector<string> planningUnits{"src/m5Methods/m5Methods.cpp", "src/m5Physical/m5Physical.cpp",
                                       "src/m5Orchestrator/m5Orchestrator.cpp", "src/m5Repair/m5Repair.cpp",
                                       "src/m5Allocation/m5Allocation.cpp", "src/m5Validation/m5Validation.cpp",
                                       "include/m5Methods.hpp", "include/m5Physical.hpp", "include/m5Orchestrator.hpp",
                                       "include/m5Repair.hpp", "include/m5Allocation.hpp", "include/m5Validation.hpp"};
    for (const string& path : planningUnits) {
        const string text = sourceText(path);
        for (const string& header : routingHeaders) CHECK_MESSAGE(text.find("#include \"" + header + "\"") == string::npos, path << " includes " << header);
    }
    const string physicalHeader = sourceText("include/m5Physical.hpp");
    CHECK(physicalHeader.find("#include \"binPacker.hpp\"") != string::npos);
    CHECK(physicalHeader.find("#include \"floorPattern.hpp\"") != string::npos);
    CHECK(sourceText("src/m5Physical/m5Physical.cpp").find("#include \"truckCheck.hpp\"") != string::npos);
    // The adapter hands M5's leftovers back as leftovers with their reasons; it does not plan them again.
    CHECK(sourceText("src/m5Integration/m5Integration.cpp").find("result.leftOver.push_back({itemOfAtom[left.atom], reasonOf(left.cause)})")
          != string::npos);
    m5Evidence::note("expected", {"65 on 3: 30 + 30 + remainder 5, each once", "remainder floor = M4 arrangeFloor",
                                  "planning units include no selector/classifier/pipeline header",
                                  "binPacker, floorPattern, truckCheck used"});
}

TEST_CASE("U-RM-02/synthetic: an empty remainder creates no extra truck" * doctest::test_suite("m5")) {
    for (const auto& [units, slots, trucks] : vector<tuple<size_t, size_t, size_t>>{{60, 3, 2}, {30, 5, 1}, {90, 4, 3}}) {
        INFO(units << " units on " << slots << " reserved");
        const M5LaneDateInput input = laneWith(fx30Units("u", units), slots);
        for (const M5PolicyProfile& profile : {patternOnly(), syntheticProfile()}) {
            const M5LaneOutcome outcome = plan(input, profile);
            requireInvariants(outcome, input);
            REQUIRE(outcome.selected);
            const vector<size_t> expected(trucks, 30);
            CHECK(truckSizes(outcome) == multiset<size_t>(expected.begin(), expected.end()));
            CHECK(outcome.leftOver.empty());
            for (const TruckPlan& truck : outcome.selected->trucks) CHECK_FALSE(truck.atoms.empty());
        }
    }
    // No goods at all: no truck.
    const M5LaneDateInput none = laneWith({}, 3);
    const M5LaneOutcome nothing = plan(none, syntheticProfile());
    CHECK(shippedCount(nothing) == 0);
    CHECK((!nothing.selected || nothing.selected->trucks.empty()));
    CHECK(nothing.kind != OutcomeKind::CompletedWithDispatch);
    m5Evidence::note("expected", {"60 on 3: 2 trucks", "30 on 5: 1 truck", "90 on 4: 3 trucks", "no goods: no truck"});
}

namespace {

// 50 FX-30 of one group on two reserved trucks, built 30 + 20, with what a repair needs.
struct RebalanceFixture {
    M5LaneDateInput input = laneWith(fx30Units("u", 50), 2);
    DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    GroupTrucks thirtyTwenty() const {
        Truck first, second;
        for (size_t atom = 0; atom < 30; ++atom) first.push_back(atom);
        for (size_t atom = 30; atom < 50; ++atom) second.push_back(atom);
        return {first, second};
    }
};

double weightOf(const M5LaneDateInput& input, const Truck& truck) {
    double weight = 0.0;
    for (const size_t atom : truck) weight += input.atoms[atom].weightLb;
    return weight;
}

}  // namespace

TEST_CASE("U-RM-03/synthetic: with the optional policy selected, goods move between trucks of the group so the plan is accepted" * doctest::test_suite("m5")) {
    // x = 40,000 / y = 3,000: 30 + 20 has a failing 20 (30,000 lb, 2,500 cu ft); 25 + 25 passes by cube (3,125).
    const M5PolicyProfile policy = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    const RebalanceFixture fixture;
    GroupTrucks trucks = fixture.thirtyTwenty();
    const vector<size_t> withheld = rebalanceForAcceptance(trucks, fixture.snapshot, policy, physicalSettings(),
                                                           fixture.input.pool.slots, StopCheck());
    CHECK(withheld.empty());
    REQUIRE(trucks.size() == 2);
    set<size_t> seen;
    for (size_t truck = 0; truck < trucks.size(); ++truck) {
        INFO("truck " << truck);
        // Donor and recipient both checked: each is legal on its slot and passes the policy.
        CHECK(checkTruck(fixture.input, trucks[truck], fixture.input.pool.slots[truck], physicalSettings()).fits);
        double cube = 0.0;
        for (const size_t atom : trucks[truck]) cube += fixture.input.atoms[atom].floor.cubeCubicIn;
        CHECK(evaluateAcceptance(weightOf(fixture.input, trucks[truck]), cube, *policy.acceptance).accepted);
        for (const size_t atom : trucks[truck]) CHECK(seen.insert(atom).second);
    }
    CHECK(seen.size() == 50);
    // Through the planner: both trucks ship accepted, 25 + 25, nothing withheld.
    const M5LaneOutcome outcome = plan(fixture.input, policy);
    requireInvariants(outcome, fixture.input);
    CHECK(truckSizes(outcome) == multiset<size_t>{25, 25});
    CHECK(countCause(outcome, LeftOverCause::AcceptanceWithheld) == 0);
    REQUIRE(outcome.selected->validation);
    CHECK(outcome.selected->validation->acceptance == SectionStatus::Pass);
    m5Evidence::note("expected", {"30 + 20 rebalanced: both accepted, donor and recipient legal", "planner: 25 + 25, none withheld"});
}

TEST_CASE("U-RM-03/productionProfile: production moves goods only when the complete plan improves under CL-02; donor and recipient rechecked" * doctest::test_suite("m5")) {
    const RebalanceFixture fixture;
    // Acceptance OFF: the acceptance rebalance does nothing.
    GroupTrucks trucks = fixture.thirtyTwenty();
    CHECK(rebalanceForAcceptance(trucks, fixture.snapshot, syntheticProfile(), physicalSettings(), fixture.input.pool.slots, StopCheck())
              .empty());
    CHECK(trucks == fixture.thirtyTwenty());
    // The planner serves all 50 and makes no move for fill alone: 30 + 20.
    const M5LaneOutcome outcome = plan(fixture.input, syntheticProfile());
    requireInvariants(outcome, fixture.input);
    CHECK(truckSizes(outcome) == multiset<size_t>{20, 30});

    // Repair keeps a move only when the complete plan is strictly better under CL-02. Both plans below serve
    // all 50 (the same class vector), so fill never decides: ED-21's canonical records do, and the order is
    // strict, so of the two opposite moves 30 + 20 -> 25 + 25 and 25 + 25 -> 30 + 20 exactly one is kept.
    const unique_ptr<AllocationComparator> comparator = makeComparator(ComparatorKind::M4Urgency, tieOrderFor(fixture.snapshot));
    const UrgencyDomain domain(fixture.snapshot);
    auto checkedPlan = [&](const GroupTrucks& built) {
        CandidatePlan plan;
        for (size_t truck = 0; truck < built.size(); ++truck) {
            TruckPlan placed;
            placed.slot = truck;
            placed.group = "A";
            placed.atoms = built[truck];
            plan.trucks.push_back(placed);
        }
        storeRows(plan, fixture.input);
        plan.validation = validateFinal(plan, fixture.snapshot, syntheticProfile(), physicalSettings());
        REQUIRE(plan.validation->allPassed());
        return plan;
    };
    const CandidatePlan start = checkedPlan(fixture.thirtyTwenty());
    GroupTrucks evenTrucks = fixture.thirtyTwenty();
    for (int move = 0; move < 5; ++move) {
        evenTrucks[1].push_back(evenTrucks[0].back());
        evenTrucks[0].pop_back();
    }
    sort(evenTrucks[1].begin(), evenTrucks[1].end());
    const CandidatePlan evenStart = checkedPlan(evenTrucks);
    auto shift = [&](size_t from, size_t to) {
        return RepairProposal([&, from, to](CandidatePlan& work, size_t iteration) {
            if (iteration > 0) return false;
            CandidateEditor editor(work);
            for (int move = 0; move < 5; ++move) editor.moveAtom(from, to, work.trucks[from].atoms.back());
            storeRows(work, fixture.input);
            return true;
        });
    };
    const RepairRun toEven = runRepair(start, fixture.snapshot, syntheticProfile(), physicalSettings(), *comparator, domain, shift(0, 1), 3, StopCheck());
    const RepairRun toUneven =
        runRepair(evenStart, fixture.snapshot, syntheticProfile(), physicalSettings(), *comparator, domain, shift(1, 0), 3, StopCheck());
    REQUIRE(toEven.incumbent);
    REQUIRE(toUneven.incumbent);
    const bool evenKept = toEven.incumbent->trucks[0].atoms.size() == 25;
    const bool unevenKept = toUneven.incumbent->trucks[0].atoms.size() == 30;
    CHECK(evenKept != unevenKept);
    CHECK(toEven.checkedRevisions.size() == 1);
    CHECK(toUneven.checkedRevisions.size() == 1);
    for (const RepairRun* run : {&toEven, &toUneven}) {
        size_t shipped = 0;
        for (const TruckPlan& truck : run->incumbent->trucks) shipped += truck.atoms.size();
        CHECK(shipped == 50);
        CHECK(run->incumbent->validation->allPassed());
    }
    // A move that breaks the recipient (31 on it, 46,500 lb) fails its recheck and is discarded.
    const RepairProposal overload = [&](CandidatePlan& work, size_t iteration) {
        if (iteration > 0) return false;
        CandidateEditor editor(work);
        while (work.trucks[1].atoms.size() > 19) editor.moveAtom(1, 0, work.trucks[1].atoms.back());
        storeRows(work, fixture.input);
        return true;
    };
    const RepairRun broken = runRepair(start, fixture.snapshot, syntheticProfile(), physicalSettings(), *comparator, domain, overload, 3, StopCheck());
    REQUIRE(broken.incumbent);
    CHECK(broken.incumbent->trucks[0].atoms.size() == 30);
    CHECK(broken.incumbent->trucks[1].atoms.size() == 20);
    CHECK(weightOf(fixture.input, broken.incumbent->trucks[0].atoms) <= 45000.0);
    m5Evidence::note("expected", {"acceptance OFF: no acceptance rebalance", "planner 30 + 20 (no fill-only move)",
                                  "equal class vectors: of 30+20 <-> 25+25 exactly one move kept, by ED-21 records, never by fill",
                                  "31-unit recipient fails recheck: discarded"});
}

namespace {

// Usable reservations are used, and unused ones only for a stated reason. Shared by both U-RM-05 variants.
void requireReservationsUsed(const M5PolicyProfile& profile) {
    // Goods for three trucks on three: all three are used rather than saving one.
    const M5LaneDateInput seventy = laneWith(fx30Units("u", 70), 3);
    const M5LaneOutcome used = plan(seventy, profile);
    requireInvariants(used, seventy);
    CHECK(truckSizes(used) == multiset<size_t>{10, 30, 30});
    CHECK(used.leftOver.empty());
    // Nothing eligible left: a slot stays unused, with nothing left over.
    const M5LaneDateInput fifty = laneWith(fx30Units("u", 50), 3);
    const M5LaneOutcome spare = plan(fifty, profile);
    requireInvariants(spare, fifty);
    CHECK(truckSizes(spare) == multiset<size_t>{20, 30});
    CHECK(spare.leftOver.empty());
    // Physically impossible cargo: two units too tall for any slot leave two slots unused, reported as
    // fits no reserved trailer; the run ends with no illegal truck and no retry loop.
    vector<Atom> withTall = fx30Units("u", 30);
    withTall.push_back(unitOf("tall1", "A", 48, 40, 120, 500));
    withTall.push_back(unitOf("tall2", "A", 48, 40, 120, 500));
    const M5LaneDateInput impossible = laneWith(withTall, 3);
    const M5LaneOutcome stuck = plan(impossible, profile);
    requireInvariants(stuck, impossible);
    CHECK(stuck.kind == OutcomeKind::CompletedWithDispatch);
    CHECK(stuck.stop == StopReason::Completed);
    CHECK(truckSizes(stuck) == multiset<size_t>{30});
    CHECK(countCause(stuck, LeftOverCause::Unfittable) == 2);
    const string text = renderAscii(buildReportData(stuck, impossible, &profile, {}));
    CHECK(text.find("unused reservations: 2") != string::npos);
    CHECK(text.find("left: fits no reserved trailer: 2.00 unit loads") != string::npos);
    // Can-go under the working contract: 10 goods due D+3 fill the room 30 + 20 required goods leave.
    vector<Atom> canGo = fx30Units("r", 50);
    for (Atom& atom : fx30Units("g", 10, "A", 0, dayAfter(3))) canGo.push_back(atom);
    const M5LaneDateInput pulled = laneWith(canGo, 2, kDefaultTrailer, true);
    const M5LaneOutcome withCanGo = plan(pulled, profile);
    requireInvariants(withCanGo, pulled);
    CHECK(truckSizes(withCanGo) == multiset<size_t>{30, 30});
    CHECK(withCanGo.leftOver.empty());
    // Never an empty truck; every unit whole, on one truck or left over, once.
    for (const M5LaneOutcome* outcome : {&used, &spare, &stuck, &withCanGo}) {
        REQUIRE(outcome->selected);
        for (const TruckPlan& truck : outcome->selected->trucks) CHECK_FALSE(truck.atoms.empty());
    }
}

}  // namespace

TEST_CASE("U-RM-05/synthetic: usable reservations serve eligible goods; unused slots only for a stated reason" * doctest::test_suite("m5")) {
    requireReservationsUsed(syntheticProfile());
    m5Evidence::note("expected", {"70 on 3: 30 + 30 + 10", "50 on 3: 2 used, nothing left", "2 too tall: 2 unused, reported, no retry",
                                  "can-go fills to 30 + 30", "no empty truck"});
}

TEST_CASE("U-RM-05/productionProfile: acceptance OFF, FX-FULL labels: the same use of reservations; every truck labelled" * doctest::test_suite("m5")) {
    REQUIRE_FALSE(syntheticProfile().acceptance.has_value());
    requireReservationsUsed(syntheticProfile());
    const M5LaneDateInput seventy = laneWith(fx30Units("u", 70), 3);
    const M5LaneOutcome outcome = plan(seventy, syntheticProfile());
    REQUIRE(outcome.selected);
    REQUIRE(outcome.fullness.size() == 3);
    for (const FullnessLabel& label : outcome.fullness) CHECK(label.bucket.has_value());
    REQUIRE(outcome.selected->validation);
    CHECK(outcome.selected->validation->acceptance == SectionStatus::NotApplicable);
    m5Evidence::note("expected", {"as synthetic, acceptance not applicable", "3 trucks each labelled with a bucket"});
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

namespace {

// A hand-built plan of `trucks` (truck t on slot t), its rows stored and validated on its own revision.
CandidatePlan checkedPlanOf(const M5LaneDateInput& input, const DemandSnapshot& snapshot, const GroupTrucks& trucks) {
    CandidatePlan plan;
    vector<char> used(input.atoms.size(), 0);
    for (size_t truck = 0; truck < trucks.size(); ++truck) {
        TruckPlan placed;
        placed.slot = truck;
        placed.group = input.atoms[trucks[truck].front()].group;
        placed.atoms = trucks[truck];
        for (const size_t atom : placed.atoms) used[atom] = 1;
        plan.trucks.push_back(placed);
    }
    for (size_t atom = 0; atom < input.atoms.size(); ++atom) {
        if (!used[atom]) plan.leftOver.push_back({atom, LeftOverCause::OverReservation});
    }
    storeRows(plan, input);
    plan.validation = validateFinal(plan, snapshot, syntheticProfile(), physicalSettings());
    return plan;
}

TEST_CASE("X-RES-09/synthetic: selection compares A after repair, so B's 20 P0 beats A's 10 P0" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("p0", 50), fx30Units("p11", 30, "A", 11)), 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const UrgencyDomain domain(snapshot);
    const unique_ptr<AllocationComparator> comparator = makeComparator(ComparatorKind::FxPriority);
    Truck firstA, finalA, planB;
    for (size_t atom = 0; atom < 30; ++atom) firstA.push_back(atom);
    for (size_t atom = 0; atom < 10; ++atom) finalA.push_back(atom);
    for (size_t atom = 50; atom < 70; ++atom) finalA.push_back(atom);
    for (size_t atom = 30; atom < 50; ++atom) planB.push_back(atom);
    for (size_t atom = 70; atom < 80; ++atom) planB.push_back(atom);
    CandidatePlan first = checkedPlanOf(input, snapshot, {firstA});
    REQUIRE(first.validation->allPassed());
    const CandidatePlan b = checkedPlanOf(input, snapshot, {planB});
    REQUIRE(b.validation->allPassed());
    CHECK(comparator->better(keyOfPlan(snapshot, domain, first), keyOfPlan(snapshot, domain, b)));
    // A's stored witness fails the final check. Repair replaces twenty urgent units with twenty P11.
    first.trucks[0].layout.rows.clear();
    first.validation = validateFinal(first, snapshot, syntheticProfile(), physicalSettings());
    REQUIRE_FALSE(first.validation->allPassed());
    const RepairProposal repair = [&](CandidatePlan& work, size_t iteration) {
        if (iteration != 0) return false;
        work.trucks[0].atoms = finalA;
        work.leftOver.clear();
        vector<char> shipped(input.atoms.size(), 0);
        for (const size_t atom : finalA) shipped[atom] = 1;
        for (size_t atom = 0; atom < shipped.size(); ++atom) {
            if (!shipped[atom]) work.leftOver.push_back({atom, LeftOverCause::OverReservation});
        }
        ++work.revision;
        work.validation.reset();
        storeRows(work, input);
        return true;
    };
    const RepairRun repaired = runRepair(first, snapshot, syntheticProfile(), physicalSettings(), *comparator, domain,
                                         repair, 1, StopCheck());
    REQUIRE(repaired.incumbent);
    REQUIRE(repaired.incumbent->validation->allPassed());
    CHECK(repaired.incumbent->trucks[0].atoms == finalA);
    CHECK(keyOfPlan(snapshot, domain, *repaired.incumbent).shippedByPriority[0] == 10000);
    CHECK(keyOfPlan(snapshot, domain, *repaired.incumbent).shippedByPriority[11] == 20000);
    CHECK(comparator->better(keyOfPlan(snapshot, domain, b), keyOfPlan(snapshot, domain, *repaired.incumbent)));
    CHECK(reconcile(snapshot, *repaired.incumbent).balanced);
    CHECK(repaired.incumbent->leftOver.size() == 50);
    m5Evidence::note("expected", {"A first 30 P0; repaired 10 P0 + 20 P11", "B 20 P0 + 10 P11 wins",
                                  "twenty removed P0 units return once"});
}

TEST_CASE("X-RES-11/synthetic: failed final validation releases both provisional reservations before a valid alternative commits" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 61), 3);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    Truck overfull, second, validFirst;
    for (size_t atom = 0; atom < 31; ++atom) overfull.push_back(atom);
    for (size_t atom = 31; atom < 61; ++atom) second.push_back(atom);
    for (size_t atom = 0; atom < 30; ++atom) validFirst.push_back(atom);
    const CandidatePlan failed = checkedPlanOf(input, snapshot, {overfull, second});
    REQUIRE_FALSE(failed.validation->allPassed());
    CHECK(failed.trucks.size() == 2);
    OperationalState state{snapshot.id, snapshot.input.pool.version, {}, 0, 3};
    CommitLedger ledger;
    string error;
    CHECK_FALSE(buildDelta(failed, snapshot, syntheticProfile(), &error));
    CHECK(error.find("failed validation") != string::npos);
    CHECK(state.slotsUsed == 0);
    CHECK(state.consumedAtoms.empty());
    CandidatePlan alternative = checkedPlanOf(input, snapshot, {validFirst, second});
    REQUIRE(alternative.validation->allPassed());
    const optional<CommitDelta> delta = buildDelta(alternative, snapshot, syntheticProfile(), &error);
    REQUIRE(delta);
    CHECK(delta->reservationsUsed == 2);
    CHECK(ledger.applyIfCurrent(*delta, snapshot, state) == ApplyStatus::Applied);
    CHECK(state.slotsUsed == 2);
    CHECK(state.consumedAtoms.size() == 60);
    CHECK(ledger.applyIfCurrent(*delta, snapshot, state) == ApplyStatus::AlreadyApplied);
    CHECK(state.slotsUsed == 2);
    m5Evidence::note("expected", {"failed 2-of-3 candidate commits nothing", "valid 2-of-3 alternative commits once",
                                  "one reservation remains unused; no goods duplicated"});
}

// Every atom of the input ends exactly once: on one truck or left over.
bool eachAtomOnce(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    vector<int> ends(input.atoms.size(), 0);
    if (outcome.selected) {
        for (const TruckPlan& truck : outcome.selected->trucks) {
            for (const size_t atom : truck.atoms) ++ends[atom];
        }
    }
    for (const LeftOver& left : outcome.leftOver) ++ends[left.atom];
    return all_of(ends.begin(), ends.end(), [](int end) { return end == 1; });
}

bool singleGroupTrucks(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    if (!outcome.selected) return true;
    for (const TruckPlan& truck : outcome.selected->trucks) {
        for (const size_t atom : truck.atoms) {
            if (input.atoms[atom].group != truck.group) return false;
        }
    }
    return true;
}

// X-REB scalar sizes on the 630 in floor: size s stands 63 s in long and 100 in wide, 1,000 lb.
Atom sized(const string& id, const string& group, int size, const string& available = kD) {
    return unitOf(id, group, 63.0 * size, 100, 108, 1000, 0, kD, available);
}

}  // namespace

TEST_CASE("X-REB-01/synthetic: two A of 6 and two B of 4 on capacity 10 fill two bins [6,4], not three single-product bins" * doctest::test_suite("m5")) {
    const vector<long long> sizes{6, 6, 4, 4};
    CHECK(m5Oracles::bruteForceBins(sizes, 10) == 2);
    // Single-product patterns first: one 6 per bin (12 > 10), two 4s in one: 3 bins.
    CHECK(2 + (4 + 4 <= 10 ? 1 : 2) == 3);
    const ScalarModel model = scalarModel(sizes, 10);
    const ScalarResult solved = solveCuttingStock(model, ScalarLimits());
    REQUIRE(solved.packing);
    CHECK(m5Oracles::packingValid(model, *solved.packing, {}));
    REQUIRE(solved.packing->bins.size() == 2);
    for (const auto& bin : solved.packing->bins) {
        long long fill = 0;
        for (const size_t item : bin) fill += sizes[item];
        CHECK(fill == 10);
    }
    // On the floor: 6 -> 378 in, 4 -> 252 in; two trucks of 630 in, each one A and one B, all four ship.
    const M5LaneDateInput input = laneWith({sized("a1", "A", 6), sized("a2", "A", 6), sized("b1", "A", 4), sized("b2", "A", 4)}, 2);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 2);
    CHECK(shippedCount(outcome) == 4);
    for (const TruckPlan& truck : outcome.selected->trucks) {
        CHECK(truck.atoms.size() == 2);
        CHECK(truck.layout.usedLengthIn == 630.0);
    }
    m5Evidence::note("expected", {"brute force 2 bins", "single-product first 3", "solver 2 bins of 10", "floor: 2 trucks [378 + 252]"});
}

TEST_CASE("X-REB-02/synthetic: A and B in different groups never mix; B not yet available is left; the run ends" * doctest::test_suite("m5")) {
    // Separate groups on two reserved: A needs 2 trucks (6 + 6 > 10), B 1; only 2 slots, so 3 trucks cannot be had.
    const M5LaneDateInput apart = laneWith({sized("a1", "A", 6), sized("a2", "A", 6), sized("b1", "B", 4), sized("b2", "B", 4)}, 2);
    const M5LaneOutcome outcome = plan(apart, syntheticProfile());
    requireInvariants(outcome, apart);
    REQUIRE(outcome.selected);
    CHECK(outcome.stop == StopReason::Completed);
    CHECK(singleGroupTrucks(outcome, apart));
    CHECK(outcome.selected->trucks.size() == 2);
    CHECK(eachAtomOnce(outcome, apart));
    CHECK(shippedCount(outcome) + countCause(outcome, LeftOverCause::OverReservation) == 4);
    CHECK(shippedCount(outcome) < 4);
    // B available only tomorrow: B is left as not eligible; A's two ship on their own trucks.
    const M5LaneDateInput later = laneWith({sized("a1", "A", 6), sized("a2", "A", 6), sized("b1", "B", 4, dayAfter(1)),
                                            sized("b2", "B", 4, dayAfter(1))},
                                           2);
    const M5LaneOutcome waiting = plan(later, syntheticProfile());
    requireInvariants(waiting, later);
    CHECK(shippedIds(waiting, later) == set<string>{"a1", "a2"});
    CHECK(countCause(waiting, LeftOverCause::NotEligible) == 2);
    CHECK(waiting.stop == StopReason::Completed);
    m5Evidence::note("expected", {"groups apart: 2 trucks, no truck mixes A and B, rest over the reservation",
                                  "B not available: a1, a2 ship, b1, b2 not eligible", "completed, no loop"});
}

TEST_CASE("X-REB-03/synthetic: copies use up all goods with no extra truck; an ineligible remainder takes no truck" * doctest::test_suite("m5")) {
    const M5LaneDateInput exact = laneWith(fx30Units("u", 60), 3);
    const M5LaneOutcome copies = plan(exact, syntheticProfile());
    requireInvariants(copies, exact);
    CHECK(copies.selected->trucks.size() == 2);
    CHECK(copies.leftOver.empty());
    const M5LaneDateInput input = laneWith(concat(fx30Units("now", 30), fx30Units("later", 5, "A", 0, dayAfter(1))), 3);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 1);
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 5);
    for (int unit = 0; unit < 5; ++unit) CHECK(shippedIds(outcome, input).count("later" + to_string(100 + unit)) == 0);
    const string text = renderAscii(buildReportData(outcome, input, nullptr, {}));
    CHECK(text.find("unused reservations: 2") != string::npos);
    CHECK(text.find("left: not eligible on this date: 5.00 unit loads") != string::npos);
    m5Evidence::note("expected", {"60 on 3: 2 trucks", "5 due D+1 (pull off): no truck, 2 unused, reported not eligible"});
}

TEST_CASE("X-REB-05/synthetic: removing goods from a proven truck clears its proof; the goods come back once and the choice is made again" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 60), 2);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    REQUIRE(outcome.selected);
    CandidatePlan candidate = *outcome.selected;
    CHECK(candidate.evidence.proof == ProofStatus::ProvenOptimal);
    const size_t removed = candidate.trucks[0].atoms.back();
    CandidateEditor(candidate).removeToLeftOver(0, removed, LeftOverCause::OverReservation);
    storeRows(candidate, input);
    CHECK(candidate.evidence.proof == ProofStatus::Unproven);
    CHECK_FALSE(candidate.validation.has_value());
    CHECK(count_if(candidate.leftOver.begin(), candidate.leftOver.end(), [&](const LeftOver& left) { return left.atom == removed; }) == 1);
    CHECK(reconcile(snapshot, candidate).balanced);
    // Checked again on its contents, the trimmed plan is legal; planned afresh, the better plan ships all 60.
    CHECK(validateFinal(candidate, snapshot, syntheticProfile(), physicalSettings()).allPassed());
    const M5LaneOutcome again = plan(input, syntheticProfile());
    CHECK(again.finalValidationPassed);
    CHECK(shippedCount(again) == 60);
    m5Evidence::note("expected", {"proof ProvenOptimal -> Unproven, validation cleared", "removed unit left once, ledger balanced",
                                  "trimmed plan rechecked", "fresh choice ships 60"});
}

TEST_CASE("X-REB-06/synthetic: a rebalance making a mixed-date stack with pull-forward off is refused; the later line never ships early" * doctest::test_suite("m5")) {
    Atom stack = unitOf("mixed", "A", 100, 20, 100, 3000);
    stack.unitLoads = 2.0;
    Component later = stack.components[0];
    later.source = "line-later";
    later.dueDate = dayAfter(1);
    later.offerKind = OfferKind::CanGo;
    stack.components.push_back(later);
    const M5LaneDateInput off = laneWith({fx30("now"), stack}, 1);
    const DemandSnapshot snapshot = *buildSnapshot(off).snapshot;
    // A move that puts the mixed stack on the truck: each unit's date is checked again, and it fails.
    const CandidatePlan moved = checkedPlanOf(off, snapshot, {{0, 1}});
    REQUIRE(moved.validation);
    CHECK(moved.validation->eligibility == SectionStatus::Fail);
    CHECK_FALSE(moved.validation->allPassed());
    const M5LaneOutcome outcome = plan(off, syntheticProfile());
    requireInvariants(outcome, off);
    CHECK(shippedIds(outcome, off) == set<string>{"now"});
    CHECK(causeOf(outcome, off, "mixed") == LeftOverCause::NotEligible);
    m5Evidence::note("expected", {"mixed-date stack on a truck: eligibility fails", "planner: only 'now' ships, the stack not eligible"});
}

namespace {

// X-REB-07: P0 cube 8 and P11 cube 9 (each 400 in long, so never together on 630 in), one reservation.
M5LaneDateInput priorityPair() {
    Atom urgent = unitOf("p0", "A", 400, 100, 1, 1);
    urgent.floor.cubeCubicIn = 8.0 * 1728.0;
    Atom later = unitOf("p11", "A", 400, 100, 1, 1, 11);
    later.floor.cubeCubicIn = 9.0 * 1728.0;
    M5LaneDateInput input = laneWith({urgent, later}, 1);
    input.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    return input;
}

}  // namespace

TEST_CASE("X-REB-07/variantA: y = 7.9: both trucks would be accepted; P0 wins and is not replaced for fill" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = priorityPair();
    CHECK_FALSE(checkTruck(input, {0, 1}, kDefaultTrailer, physicalSettings()).fits);
    const NumericAcceptance rule{10.0, 7.9};
    CHECK(evaluateAcceptance(1.0, 8.0 * 1728.0, rule).accepted);
    CHECK(evaluateAcceptance(1.0, 9.0 * 1728.0, rule).accepted);
    const M5LaneOutcome outcome = plan(input, withAcceptance(syntheticProfile(), 10.0, 7.9));
    requireInvariants(outcome, input);
    CHECK(shippedIds(outcome, input) == set<string>{"p0"});
    CHECK(causeOf(outcome, input, "p11") == LeftOverCause::OverReservation);
    m5Evidence::note("expected", {"cannot fit together", "both accepted at y 7.9", "P0 ships, P11 over the reservation"});
}

TEST_CASE("X-REB-07/variantB: y = 8: the P0 truck fails on equality; the accepted P11 truck ships" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = priorityPair();
    const NumericAcceptance rule{10.0, 8.0};
    CHECK_FALSE(evaluateAcceptance(1.0, 8.0 * 1728.0, rule).accepted);
    CHECK(evaluateAcceptance(1.0, 9.0 * 1728.0, rule).accepted);
    const M5LaneOutcome outcome = plan(input, withAcceptance(syntheticProfile(), 10.0, 8.0));
    requireInvariants(outcome, input);
    CHECK(shippedIds(outcome, input) == set<string>{"p11"});
    CHECK(causeOf(outcome, input, "p0").has_value());
    REQUIRE(outcome.selected);
    REQUIRE(outcome.selected->validation);
    CHECK(outcome.selected->validation->acceptance == SectionStatus::Pass);
    m5Evidence::note("expected", {"8 is not > 8: P0 truck fails", "P11 truck accepted and ships", "priority never ships an unaccepted truck"});
}

TEST_CASE("X-REB-07/productionProfile: acceptance OFF: choice by CL-02, the P0 truck" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = priorityPair();
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedIds(outcome, input) == set<string>{"p0"});
    CHECK(causeOf(outcome, input, "p11") == LeftOverCause::OverReservation);
    REQUIRE(outcome.selected->validation);
    CHECK(outcome.selected->validation->acceptance == SectionStatus::NotApplicable);
    m5Evidence::note("expected", {"P0 ships", "acceptance not applicable"});
}

TEST_CASE("X-REB-08/synthetic: moving B (W 7) onto A (W 4) breaks W = 10 at 11; the move is refused and fully undone" * doctest::test_suite("m5")) {
    // V as floor length (6 -> 378 in, 4 -> 252 in of 630), W in thousands of lb on a 10,000 lb trailer.
    const TrailerCapability tenThousand{630.0, 100.0, 108.0, 10000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput input = laneWith({unitOf("a", "A", 378, 100, 50, 4000), unitOf("b", "A", 252, 100, 50, 7000)}, 2, tenThousand);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CHECK_FALSE(checkTruck(input, {0, 1}, tenThousand, physicalSettings()).fits);
    const CandidatePlan start = checkedPlanOf(input, snapshot, {{0}, {1}});
    REQUIRE(start.validation->allPassed());
    const unique_ptr<AllocationComparator> comparator = makeComparator(ComparatorKind::M4Urgency, tieOrderFor(snapshot));
    const UrgencyDomain domain(snapshot);
    const RepairProposal merge = [&](CandidatePlan& work, size_t iteration) {
        if (iteration > 0) return false;
        CandidateEditor(work).moveAtom(1, 0, 1);
        storeRows(work, input);
        return true;
    };
    const RepairRun run = runRepair(start, snapshot, syntheticProfile(), physicalSettings(), *comparator, domain, merge, 3, StopCheck());
    REQUIRE(run.incumbent);
    CHECK(run.incumbent->revision == start.revision);
    REQUIRE(run.incumbent->trucks.size() == 2);
    CHECK(run.incumbent->trucks[0].atoms == vector<size_t>{0});
    CHECK(run.incumbent->trucks[1].atoms == vector<size_t>{1});
    CHECK(run.checkedRevisions.size() == 1);
    // The planner never puts them together either: two trucks, each under 10,000 lb.
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(outcome.selected->trucks.size() == 2);
    m5Evidence::note("expected", {"4,000 + 7,000 = 11,000 > 10,000: refused", "incumbent unchanged (same revision, same trucks)",
                                  "planner: 2 trucks"});
}

TEST_CASE("X-BLK-01/synthetic: block 19 over 0, 1, 18, 19, 20, 37, 38, 39 trucks of goods: no empty block, each good once" * doctest::test_suite("m5")) {
    const vector<pair<size_t, vector<size_t>>> expected{{0, {}},       {1, {1}},       {18, {18}},     {19, {19}},
                                                        {20, {19, 1}}, {37, {19, 18}}, {38, {19, 19}}, {39, {19, 19, 1}}};
    for (const auto& [goods, sizes] : expected) {
        INFO(goods << " trucks of goods");
        const M5LaneDateInput input = laneWith(blocks(goods), 39);
        const vector<size_t> built = blockSizes(input);
        CHECK(built == sizes);
        for (const size_t size : built) CHECK(size > 0);
        const M5LaneOutcome outcome = plan(input, blocksOnly());
        if (goods == 0) {
            CHECK(shippedCount(outcome) == 0);
            CHECK(outcome.leftOver.empty());
        } else {
            requireEachBlockOnce(outcome, input, goods);
        }
    }
    m5Evidence::note("expected", {"[] [1] [18] [19] [19,1] [19,18] [19,19] [19,19,1]", "no empty block", "every good once"});
}

TEST_CASE("X-BLK-02/synthetic: 40 trucks of goods on 38 reserved: 19 + 19, 2 left; no third block; priority picks the 2" * doctest::test_suite("m5")) {
    vector<Atom> atoms = blocks(38);
    atoms.push_back(fxBlock("late1", "A", 11));
    atoms.push_back(fxBlock("late2", "A", 11));
    const M5LaneDateInput input = laneWith(atoms, 38);
    CHECK(blockSizes(input) == vector<size_t>{19, 19});
    const M5LaneOutcome outcome = plan(input, blocksOnly());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 38);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 2);
    CHECK(causeOf(outcome, input, "late1") == LeftOverCause::OverReservation);
    CHECK(causeOf(outcome, input, "late2") == LeftOverCause::OverReservation);
    m5Evidence::note("expected", {"blocks 19 + 19", "38 trucks", "the two P11 left over the reservation"});
}

TEST_CASE("X-BLK-03/synthetic: 30 trucks of goods on 38 reserved: 19 + 11, 8 unused reported, no empty truck" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(blocks(30), 38);
    CHECK(blockSizes(input) == vector<size_t>{19, 11});
    const M5LaneOutcome outcome = plan(input, blocksOnly());
    requireEachBlockOnce(outcome, input, 30);
    const string text = renderAscii(buildReportData(outcome, input, nullptr, {}));
    CHECK(text.find("reserved: 38\n") != string::npos);
    CHECK(text.find("trucks loaded: 30\n") != string::npos);
    CHECK(text.find("unused reservations: 8,") != string::npos);
    m5Evidence::note("expected", {"blocks 19 + 11", "30 trucks, none empty", "8 unused reported"});
}

TEST_CASE("X-BLK-04/synthetic: groups share the 21 reserved: a first block of 19 leaves 2 for all else, never 21 each" * doctest::test_suite("m5")) {
    vector<Atom> atoms = blocks(19, "A");
    for (Atom& atom : blocks(5, "B")) {
        for (Component& component : atom.components) component.priority = 5;
        atoms.push_back(atom);
    }
    const M5LaneDateInput input = laneWith(atoms, 21);
    const M5LaneOutcome outcome = plan(input, blocksOnly());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 21);
    CHECK(shippedOfGroup(outcome, input, "A") == 19);
    CHECK(shippedOfGroup(outcome, input, "B") == 2);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 3);
    CHECK(singleGroupTrucks(outcome, input));
    m5Evidence::note("expected", {"21 trucks in all", "A's 19 P0, then B's 2 P5", "3 of B over the reservation"});
}

TEST_CASE("X-BLK-05/synthetic: A with fewer trucks but one failing its floor cannot win; B valid wins; both invalid is no plan" * doctest::test_suite("m5")) {
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
    const Truck first30(overfull.begin(), overfull.begin() + 30);
    CHECK_FALSE(checkTruck(input, overfull, kDefaultTrailer, physicalSettings()).fits);
    const M5LaneOutcome valid = planWith({overfull}, {first30, rest});
    requireInvariants(valid, input);
    REQUIRE(valid.selected);
    CHECK(valid.selected->method == MethodId::RollingBlocks);
    CHECK(shippedCount(valid) == 60);
    CHECK(eachAtomOnce(valid, input));
    REQUIRE(valid.methods.size() == 2);
    CHECK_FALSE(valid.methods[0].selected);
    const M5LaneOutcome neither = planWith({overfull}, {overfull});
    requireInvariants(neither, input);
    CHECK_FALSE(neither.hasCandidate);
    CHECK((!neither.selected || neither.selected->trucks.empty()));
    CHECK(shippedCount(neither) == 0);
    CHECK(neither.leftOver.size() == 61);
    CHECK(eachAtomOnce(neither, input));
    m5Evidence::note("expected", {"A (1 overfull truck) not selected", "B ships 60", "both invalid: no plan, 61 kept"});
}

TEST_CASE("X-BLK-06/synthetic: A ships 30 P0 in three, B 20 in two; both pass acceptance; A wins, fewer trucks is not better" * doctest::test_suite("m5")) {
    vector<Atom> atoms;
    for (int unit = 0; unit < 30; ++unit) atoms.push_back(unitOf("t" + to_string(100 + unit), "A", 100, 63, 108, 4500));
    const M5LaneDateInput input = laneWith(atoms, 3);
    M5PolicyProfile profile = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    profile.comparator = ComparatorKind::FxPriority;
    // Ten of 63 in fill 630 in; 10 x 4,500 = 45,000 lb > 40,000: each truck of either candidate is accepted.
    CHECK(checkTruck(input, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, kDefaultTrailer, physicalSettings()).fits);
    CHECK(evaluateAcceptance(45000.0, 10 * 100.0 * 63.0 * 108.0, *profile.acceptance).accepted);
    const M5LaneOutcome outcome = planWithServices(input, profile, [&](M5RunServices& services) {
        services.methods = {make_shared<FixedMethod>(MethodId::PatternRepeat, 3, 10), make_shared<FixedMethod>(MethodId::RollingBlocks, 2, 10)};
    });
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->method == MethodId::PatternRepeat);
    CHECK(shippedCount(outcome) == 30);
    REQUIRE(outcome.methods.size() == 2);
    CHECK(outcome.methods[0].unitLoads == 30.0);
    CHECK(outcome.methods[1].unitLoads == 20.0);
    CHECK(outcome.methods[0].validated);
    CHECK(outcome.methods[1].validated);
    CHECK(eachAtomOnce(outcome, input));
    m5Evidence::note("expected", {"both candidates accepted", "A: 30 in 3, B: 20 in 2", "A selected", "same 30 source units accounted"});
}

namespace {

// 39 FX-BLOCK on 39: blocks 1 and 2 (38 trucks) finish; the clock passes the allowance inside block 3.
M5LaneOutcome stoppedInThirdBlock(const M5LaneDateInput& input, bool rollback) {
    FakeClock clock;
    M5RunServices services(clock);
    services.allowanceSeconds = 100.0;
    services.wholeLaneRollback = rollback;
    services.afterBlock = [&](size_t trucks) {
        if (trucks >= 38) clock.advance(1000.0);
    };
    return planLaneDate(input, blocksOnly(), physicalSettings(), services);
}

}  // namespace

TEST_CASE("X-BLK-08/keepValidated: the failing block is undone; only the checked blocks return, labelled partial; nothing is consumed" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(blocks(39), 39);
    const M5LaneOutcome keep = stoppedInThirdBlock(input, false);
    requireInvariants(keep, input);
    CHECK(keep.kind == OutcomeKind::TimeoutWithIncumbent);
    REQUIRE(keep.selected);
    CHECK(keep.selected->trucks.size() == 38);
    CHECK(keep.finalValidationPassed);
    CHECK(keep.proof != ProofStatus::ProvenOptimal);
    CHECK(keep.leftOver.size() == 1);
    CHECK(eachAtomOnce(keep, input));
    // Validation alone consumes nothing: the operational state moves only when the delta is applied.
    REQUIRE(keep.delta);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CommitLedger ledger;
    OperationalState state{snapshot.id, snapshot.input.pool.version, {}, 0, 39};
    CHECK(state.consumedAtoms.empty());
    REQUIRE(ledger.applyIfCurrent(*keep.delta, snapshot, state) == ApplyStatus::Applied);
    CHECK(state.consumedAtoms.size() == 38);
    m5Evidence::note("expected", {"timeout with incumbent: 38 checked trucks, 1 left", "not proven", "nothing consumed before commit"});
}

TEST_CASE("X-BLK-08/wholeLaneRollback: the same stop under whole-lane rollback returns no candidate; every good kept" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(blocks(39), 39);
    const M5LaneOutcome rollback = stoppedInThirdBlock(input, true);
    requireInvariants(rollback, input);
    CHECK_FALSE(rollback.selected.has_value());
    CHECK_FALSE(rollback.delta.has_value());
    CHECK(countCause(rollback, LeftOverCause::NoResult) == 39);
    CHECK(eachAtomOnce(rollback, input));
    m5Evidence::note("expected", {"no candidate, no delta", "39 left as no checked plan"});
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

namespace {

size_t trucksOfGroup(const M5LaneOutcome& outcome, const string& group) {
    if (!outcome.selected) return 0;
    return static_cast<size_t>(count_if(outcome.selected->trucks.begin(), outcome.selected->trucks.end(),
                                        [&](const TruckPlan& truck) { return truck.group == group; }));
}

void requireSingleGroupTrucks(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    if (!outcome.selected) return;
    for (const TruckPlan& truck : outcome.selected->trucks) {
        for (const size_t atom : truck.atoms) CHECK(input.atoms[atom].group == truck.group);
    }
}

}  // namespace

TEST_CASE("X-RES-01/synthetic: two reserved, group A 90 and B 60 at equal priority share the two: 2 trucks, 60 shipped, 90 left" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 90, "A"), fx30Units("b", 60, "B")), 2);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    requireSingleGroupTrucks(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 2);
    CHECK(shippedCount(outcome) == 60);
    CHECK(outcome.leftOver.size() == 90);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 90);
    CHECK(trucksOfGroup(outcome, "A") + trucksOfGroup(outcome, "B") == 2);
    m5Evidence::note("expected", {"one pool of 2 for both groups", "2 trucks, 60 shipped, 90 left over the reservation"});
}

TEST_CASE("X-RES-02/synthetic: three reserved, 60 X (two patterns) + 15 Y + 15 Z: 3 trucks, all 90; the remainder never takes a 4th reservation" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("x", 60, "A", 5);
    atoms = concat(atoms, fx30Units("y", 15, "A", 5));
    atoms = concat(atoms, fx30Units("z", 15, "A", 5));
    const M5LaneDateInput input = laneWith(atoms, 3);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 3);
    CHECK(shippedCount(outcome) == 90);
    CHECK(outcome.leftOver.empty());
    for (const TruckPlan& truck : outcome.selected->trucks) {
        CHECK(truck.atoms.size() == 30);
        CHECK(truck.slot < 3);
    }
    m5Evidence::note("expected", {"3 trucks of 30, 90 shipped", "no 4th truck, slots 0..2 only"});
}

TEST_CASE("X-RES-03/synthetic: 2 reserved on D, none on D+1, 3 on D+2 with goods for more than 2 on D: 2 leave on D, none on D+1; the lane total 5 never applies to D" * doctest::test_suite("m5")) {
    const M5LaneDateInput onD = laneWith(fx30Units("d", 150), 2);
    const M5LaneOutcome first = plan(onD, syntheticProfile());
    requireInvariants(first, onD);
    REQUIRE(first.selected);
    CHECK(first.selected->trucks.size() == 2);
    CHECK(shippedCount(first) == 60);
    CHECK(countCause(first, LeftOverCause::OverReservation) == 90);
    M5LaneDateInput nextDay = laneWith(fx30Units("e", 60), 0);
    nextDay.departure = dayAfter(1);
    for (Atom& atom : nextDay.atoms) atom.components[0].dueDate = atom.components[0].availableDate = dayAfter(1);
    const M5LaneOutcome none = plan(nextDay, syntheticProfile());
    CHECK(shippedCount(none) == 0);
    CHECK((!none.selected || none.selected->trucks.empty()));
    CHECK(none.leftOver.size() == 60);
    M5LaneDateInput third = laneWith(fx30Units("f", 150), 3);
    third.departure = dayAfter(2);
    for (Atom& atom : third.atoms) atom.components[0].dueDate = atom.components[0].availableDate = dayAfter(2);
    const M5LaneOutcome later = plan(third, syntheticProfile());
    REQUIRE(later.selected);
    CHECK(later.selected->trucks.size() == 3);
    m5Evidence::note("expected", {"D: 2 trucks, 60 shipped, 90 left", "D+1: 0 trucks", "D+2: 3 trucks"});
}

TEST_CASE("X-RES-06/synthetic: two reserved, priority first: A 1 P0 + 29 P11, B 20 P0 + 10 P11, C 30 P0: C and B ship, A is left" * doctest::test_suite("m5")) {
    vector<Atom> atoms{fx30("a0", "A", 0)};
    atoms = concat(atoms, fx30Units("a11-", 29, "A", 11));
    atoms = concat(atoms, fx30Units("b0-", 20, "B", 0));
    atoms = concat(atoms, fx30Units("b11-", 10, "B", 11));
    atoms = concat(atoms, fx30Units("c0-", 30, "C", 0));
    M5LaneDateInput input = laneWith(atoms, 2);
    input.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    requireSingleGroupTrucks(outcome, input);
    CHECK(shippedOfGroup(outcome, input, "A") == 0);
    CHECK(shippedOfGroup(outcome, input, "B") == 30);
    CHECK(shippedOfGroup(outcome, input, "C") == 30);
    CHECK(trucksOfGroup(outcome, "B") == 1);
    CHECK(trucksOfGroup(outcome, "C") == 1);
    // By hand, the two-truck choices by P0 shipped: B + C 50, A + C 31, A + B 21.
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 30);
    m5Evidence::note("expected", {"C 30 and B 30 ship (50 P0)", "A's 30 left"});
}

TEST_CASE("X-RES-07/synthetic: two reserved, 90 P11 of X and 15 P0 each of Y and Z, priority first: all 30 P0 and 30 P11 ship in two trucks, 60 P11 left" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("x", 90, "A", 11);
    atoms = concat(atoms, fx30Units("y", 15, "A", 0));
    atoms = concat(atoms, fx30Units("z", 15, "A", 0));
    M5LaneDateInput input = laneWith(atoms, 2);
    input.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 2);
    size_t urgent = 0;
    for (const string& id : shippedIds(outcome, input)) urgent += id[0] != 'x' ? 1 : 0;
    CHECK(urgent == 30);
    CHECK(shippedCount(outcome) == 60);
    CHECK(outcome.leftOver.size() == 60);
    for (const LeftOver& left : outcome.leftOver) CHECK(input.atoms[left.atom].components[0].priority == 11);
    CHECK(outcome.finalValidationPassed);
    m5Evidence::note("expected", {"2 trucks, 30 P0 + 30 P11 shipped", "60 P11 left"});
}

TEST_CASE("X-RES-08/case1: one reserved, priority first: 30 P0 available only on D+1 stay left over, not lost; the 30 P11 available on D ship" * doctest::test_suite("m5")) {
    M5LaneDateInput late = laneWith(concat(fx30Units("p0-", 30, "A", 0, kD, dayAfter(1)), fx30Units("p11-", 30, "A", 11)), 1);
    late.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    const M5LaneOutcome outcome = plan(late, syntheticProfile());
    requireInvariants(outcome, late);
    CHECK(shippedCount(outcome) == 30);
    for (const string& id : shippedIds(outcome, late)) CHECK(id.rfind("p11-", 0) == 0);
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 30);
    CHECK(outcome.leftOver.size() == 30);
    for (const LeftOver& left : outcome.leftOver) CHECK(late.atoms[left.atom].id.rfind("p0-", 0) == 0);
    m5Evidence::note("expected", {"30 P11 ship", "30 P0 left not eligible (available D+1), each once"});
}

TEST_CASE("X-RES-08/case2: the 30 P0 fit no trailer: classed unshippable with a reason; the scope is per atom, so the 30 P11 ship" * doctest::test_suite("m5")) {
    vector<Atom> oversized;
    for (int unit = 0; unit < 30; ++unit) oversized.push_back(unitOf("big" + to_string(100 + unit), "A", 700, 100, 108, 100));
    M5LaneDateInput tooBig = laneWith(concat(oversized, fx30Units("p11-", 30, "A", 11)), 1);
    tooBig.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    const M5LaneOutcome outcome = plan(tooBig, syntheticProfile());
    requireInvariants(outcome, tooBig);
    // Recorded failure scope: an atom that fits no slot alone is Unfittable on its own (U-CF-08); the lane
    // still plans the rest, so partial service applies.
    CHECK(shippedCount(outcome) == 30);
    for (const string& id : shippedIds(outcome, tooBig)) CHECK(id.rfind("p11-", 0) == 0);
    CHECK(countCause(outcome, LeftOverCause::Unfittable) == 30);
    CHECK(outcome.leftOver.size() == 30);
    const string text = renderAscii(buildReportData(outcome, tooBig, nullptr, {}));
    CHECK(text.find(leftOverCauseName(LeftOverCause::Unfittable)) != string::npos);
    m5Evidence::note("expected", {"30 P0 unfittable, reported with the cause", "30 P11 ship (per-atom scope)"});
}

TEST_CASE("X-RES-10/synthetic: lane-dates leaving 2 and 1 unused at rates 5 and 7 give 17; a missing rate is unknown, a zero rate is zero" * doctest::test_suite("m5")) {
    M5PolicyProfile five = syntheticProfile(), seven = syntheticProfile(), unknown = syntheticProfile(), zero = syntheticProfile();
    five.penaltyPerUnusedTruck = 5.0;
    seven.penaltyPerUnusedTruck = 7.0;
    zero.penaltyPerUnusedTruck = 0.0;
    const M5LaneDateInput twoUnused = laneWith(fx30Units("u", 30), 3), oneUnused = laneWith(fx30Units("v", 30), 2);
    const M5ReportData first = buildReportData(plan(twoUnused, five), twoUnused, &five, {});
    const M5ReportData second = buildReportData(plan(oneUnused, seven), oneUnused, &seven, {});
    CHECK(first.unusedReservations == 2);
    CHECK(second.unusedReservations == 1);
    REQUIRE(first.penaltyAmount.has_value());
    REQUIRE(second.penaltyAmount.has_value());
    CHECK(*first.penaltyAmount == 10.0);
    CHECK(*second.penaltyAmount == 7.0);
    CHECK(*first.penaltyAmount + *second.penaltyAmount == 2 * 5.0 + 1 * 7.0);
    const M5ReportData missing = buildReportData(plan(oneUnused, unknown), oneUnused, &unknown, {});
    CHECK(missing.unusedReservations == 1);
    CHECK_FALSE(missing.penaltyAmount.has_value());
    CHECK(missing.penalty == "unknown");
    const M5ReportData none = buildReportData(plan(oneUnused, zero), oneUnused, &zero, {});
    REQUIRE(none.penaltyAmount.has_value());
    CHECK(*none.penaltyAmount == 0.0);
    CHECK(none.penalty != "unknown");
    m5Evidence::note("expected", {"2 x 5 + 1 x 7 = 17", "missing: unknown", "zero: 0"});
}

namespace {

// X-RES-12: 50 FX-30 units on the two trailers, judged directly on the physical adapter and the
// acceptance rule (C9: a planning pool of mixed trailers is refused, so the planner itself is not the seam).
struct TwoTrailerResult {
    size_t big = 0, small = 0, left = 0, unusedReservations = 0;
};

TwoTrailerResult twoTrailers(const optional<NumericAcceptance>& rule) {
    const TrailerCapability big{630.0, 100.0, 108.0, 45000.0, kFixtureUnitLoadCap};
    const TrailerCapability small{630.0, 100.0, 108.0, 30000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput input = laneWith(fx30Units("u", 50), 2);
    const long long bigLoad = bestSingleClassLoad(input, 0, big, physicalSettings(), 100);
    const long long smallLoad = bestSingleClassLoad(input, 0, small, physicalSettings(), 100);
    CHECK(bigLoad == 30);
    CHECK(smallLoad == 20);
    vector<size_t> thirty(30), twenty(20);
    iota(thirty.begin(), thirty.end(), size_t{0});
    iota(twenty.begin(), twenty.end(), size_t{30});
    CHECK(checkTruck(input, thirty, big, physicalSettings()).fits);
    CHECK(checkTruck(input, twenty, small, physicalSettings()).fits);
    CHECK_FALSE(checkTruck(input, thirty, small, physicalSettings()).fits);
    const double cubePerUnit = input.atoms[0].floor.cubeCubicIn;
    const auto passes = [&](size_t units) {
        return !rule || evaluateAcceptance(1500.0 * static_cast<double>(units), cubePerUnit * static_cast<double>(units), *rule).accepted;
    };
    TwoTrailerResult result;
    result.big = passes(30) ? 30 : 0;
    result.small = passes(20) ? 20 : 0;
    result.left = 50 - result.big - result.small;
    result.unusedReservations = (result.big == 0 ? 1 : 0) + (result.small == 0 ? 1 : 0);
    return result;
}

}  // namespace

TEST_CASE("X-RES-12/variantA: 45,000 and 30,000 lb trailers, x = 40,000, y = 2,400: 30 on the big (by weight) and 20 on the small (2,500 cu ft by cube), all 50" * doctest::test_suite("m5")) {
    // 20 units: 30,000 lb and 20 x 20 x 100 x 108 / 1728 = 2,500 cu ft. 30 units: 45,000 lb.
    CHECK(20.0 * 20 * 100 * 108 / 1728 == doctest::Approx(2500.0));
    const TwoTrailerResult result = twoTrailers(NumericAcceptance{40000.0, 2400.0});
    CHECK(result.big == 30);
    CHECK(result.small == 20);
    CHECK(result.left == 0);
    CHECK(result.unusedReservations == 0);
    CHECK(evaluateAcceptance(45000.0, 30 * 216000.0, NumericAcceptance{40000.0, 2400.0}).byWeight);
    CHECK(evaluateAcceptance(30000.0, 20 * 216000.0, NumericAcceptance{40000.0, 2400.0}).byCube);
    m5Evidence::note("expected", {"big 30 by weight", "small 20 by cube 2,500 > 2,400", "50 shipped"});
}

TEST_CASE("X-RES-12/variantB: y = 3,000: only the big trailer ships 30; the small holds at most 20 (30,000 lb, 2,500 cu ft) and passes neither branch" * doctest::test_suite("m5")) {
    const TwoTrailerResult result = twoTrailers(NumericAcceptance{40000.0, 3000.0});
    CHECK(result.big == 30);
    CHECK(result.small == 0);
    CHECK(result.left == 20);
    CHECK(result.unusedReservations == 1);
    const AcceptanceDecision small = evaluateAcceptance(30000.0, 20 * 216000.0, NumericAcceptance{40000.0, 3000.0});
    CHECK_FALSE(small.accepted);
    CHECK_FALSE(small.byWeight);
    CHECK_FALSE(small.byCube);
    m5Evidence::note("expected", {"big 30", "small: 20 at most, rejected (30,000 <= 40,000; 2,500 <= 3,000)", "20 left, 1 unused"});
}

TEST_CASE("X-RES-12/productionProfile: minimum-load policy off: 30 on the big and 20 on the small both ship; a mixed pool is refused by the planner (C9)" * doctest::test_suite("m5")) {
    const TwoTrailerResult result = twoTrailers(nullopt);
    CHECK(result.big == 30);
    CHECK(result.small == 20);
    CHECK(result.left == 0);
    M5LaneDateInput mixed = laneWith(fx30Units("u", 50), 2);
    mixed.pool.slots[1] = TrailerCapability{630.0, 100.0, 108.0, 30000.0, kFixtureUnitLoadCap};
    const M5LaneOutcome outcome = plan(mixed, syntheticProfile());
    CHECK(outcome.kind == OutcomeKind::InvalidInput);
    REQUIRE_FALSE(outcome.findings.empty());
    CHECK(outcome.findings.front().find("C9") != string::npos);
    m5Evidence::note("expected", {"adapter: big 30 + small 20 = 50", "planner: mixed pool refused, C9"});
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

TEST_CASE("U-RS-01/synthetic: trucks used never exceed the reserved count of the lane-date, whatever the goods" * doctest::test_suite("m5")) {
    for (const size_t slots : {size_t{1}, size_t{2}, size_t{5}, size_t{21}}) {
        INFO(slots << " reserved, 1,000 units");
        const M5LaneDateInput input = laneWith(fx30Units("u", 1000), slots);
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        requireInvariants(outcome, input);
        REQUIRE(outcome.selected);
        CHECK(outcome.selected->trucks.size() == slots);
        CHECK(shippedCount(outcome) == 30 * slots);
        CHECK(countCause(outcome, LeftOverCause::OverReservation) == 1000 - 30 * slots);
        set<size_t> slotsUsed;
        for (const TruckPlan& truck : outcome.selected->trucks) slotsUsed.insert(truck.slot);
        CHECK(slotsUsed.size() == outcome.selected->trucks.size());
        CHECK(*slotsUsed.rbegin() < slots);
    }
    // Few goods: fewer trucks than reserved, never more.
    const M5LaneDateInput few = laneWith(fx30Units("v", 10), 5);
    const M5LaneOutcome fewOutcome = plan(few, syntheticProfile());
    requireInvariants(fewOutcome, few);
    CHECK(fewOutcome.selected->trucks.size() == 1);
    m5Evidence::note("expected", {"1/2/5/21 reserved: exactly that many trucks of 30, each slot once", "10 units on 5 reserved: 1 truck"});
}

TEST_CASE("U-RS-03/synthetic: fewer trucks than goods: full priority decides across groups" * doctest::test_suite("m5")) {
    // One truck: B's P0 beat A's P5 and C's P11, whatever the group order.
    const vector<Atom> atoms = concat(concat(fx30Units("a", 30, "A", 5), fx30Units("b", 30, "B", 0)), fx30Units("c", 30, "C", 11));
    const M5LaneDateInput one = laneWith(atoms, 1);
    const M5LaneOutcome first = plan(one, syntheticProfile());
    requireInvariants(first, one);
    CHECK(shippedOfGroup(first, one, "B") == 30);
    CHECK(shippedCount(first) == 30);
    // Two trucks: P0 then P5; P11 is left over the reservation.
    const M5LaneDateInput two = laneWith(atoms, 2);
    const M5LaneOutcome second = plan(two, syntheticProfile());
    requireInvariants(second, two);
    CHECK(shippedOfGroup(second, two, "B") == 30);
    CHECK(shippedOfGroup(second, two, "A") == 30);
    CHECK(shippedOfGroup(second, two, "C") == 0);
    CHECK(countCause(second, LeftOverCause::OverReservation) == 30);
    // Reversed input order: the same goods ship.
    const vector<Atom> reversed(atoms.rbegin(), atoms.rend());
    const M5LaneDateInput backward = laneWith(reversed, 2);
    CHECK(shippedIds(plan(backward, syntheticProfile()), backward) == shippedIds(second, two));
    m5Evidence::note("expected", {"1 truck: B's 30 P0", "2 trucks: B P0 and A P5, C P11 left", "input order irrelevant"});
}

TEST_CASE("U-RS-04/synthetic: a priority tie is broken the same way every run" * doctest::test_suite("m5")) {
    // Two groups of equal class compete for one truck.
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 40, "A", 3), fx30Units("b", 40, "B", 3)), 1);
    const M5LaneOutcome first = plan(input, syntheticProfile());
    requireInvariants(first, input);
    REQUIRE(first.selected);
    CHECK(first.selected->trucks.size() == 1);
    CHECK(shippedCount(first) == 30);
    CHECK((shippedOfGroup(first, input, "A") == 30) != (shippedOfGroup(first, input, "B") == 30));
    for (int run = 0; run < 5; ++run) {
        const M5LaneOutcome again = plan(input, syntheticProfile());
        REQUIRE(again.selected);
        REQUIRE(again.selected->trucks.size() == first.selected->trucks.size());
        for (size_t truck = 0; truck < first.selected->trucks.size(); ++truck) {
            CHECK(again.selected->trucks[truck].atoms == first.selected->trucks[truck].atoms);
            CHECK(again.selected->trucks[truck].slot == first.selected->trucks[truck].slot);
        }
    }
    m5Evidence::note("expected", {"equal classes, one truck: one group's 30 ship", "same trucks and slots in 6 runs"});
}

namespace {

// R-4 setup: 30 overdue P11 (due and available the day before) against 30 current P0, one truck.
M5LaneDateInput r4Lane(ob::PastDueOrder order) {
    M5LaneDateInput input = laneWith(concat(fx30Units("old-", 30, "A", 11, kDayBefore, kDayBefore), fx30Units("now-", 30, "A", 0)), 1);
    input.pastDueOrder = order;
    return input;
}

size_t shippedWithIdPrefix(const M5LaneOutcome& outcome, const M5LaneDateInput& input, const string& prefix) {
    const set<string> shipped = shippedIds(outcome, input);
    return static_cast<size_t>(count_if(shipped.begin(), shipped.end(), [&](const string& id) { return id.rfind(prefix, 0) == 0; }));
}

}  // namespace

TEST_CASE("U-RS-06/synthetic: order setting ship date first: the older goods ship first (R-4a)" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = r4Lane(ob::PastDueOrder::AgeFirst);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedWithIdPrefix(outcome, input, "old-") == 30);
    CHECK(shippedWithIdPrefix(outcome, input, "now-") == 0);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 30);
    m5Evidence::note("expected", {"AgeFirst: 30 overdue P11 ship", "30 current P0 left over the reservation"});
}

TEST_CASE("U-RS-07/synthetic: order setting priority first: the urgent goods ship first (R-4b)" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = r4Lane(ob::PastDueOrder::PriorityFirst);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedWithIdPrefix(outcome, input, "now-") == 30);
    CHECK(shippedWithIdPrefix(outcome, input, "old-") == 0);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 30);
    m5Evidence::note("expected", {"PriorityFirst: 30 current P0 ship", "30 overdue P11 left over the reservation"});
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

TEST_CASE("U-RS-02/synthetic: unused reservations are reserved minus used per lane-date; the penalty applies once; a missing rate is unknown, not zero" * doctest::test_suite("m5")) {
    M5PolicyProfile five = syntheticProfile(), unknown = syntheticProfile();
    five.penaltyPerUnusedTruck = 5.0;
    const M5LaneDateInput input = laneWith(fx30Units("u", 30), 3);
    const M5LaneOutcome outcome = plan(input, five);
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 1);
    const M5ReportData data = buildReportData(outcome, input, &five, {});
    CHECK(data.reservedTrucks == 3);
    CHECK(data.loadedTrucks == 1);
    CHECK(data.unusedReservations == 2);
    REQUIRE(data.penaltyAmount);
    CHECK(*data.penaltyAmount == 10.0);
    const string text = renderAscii(data);
    CHECK(text.find("unused reservations: 2, penalty 5.00 per unused truck = 10.00\n") != string::npos);
    // Once: the amount is on one line only, not repeated per truck or per method.
    CHECK(text.find("= 10.00") == text.rfind("= 10.00"));
    const M5ReportData missing = buildReportData(plan(input, unknown), input, &unknown, {});
    CHECK(missing.unusedReservations == 2);
    CHECK_FALSE(missing.penaltyAmount.has_value());
    const string missingText = renderAscii(missing);
    CHECK(missingText.find("unused reservations: 2, penalty unknown\n") != string::npos);
    CHECK(missingText.find("= 0.00") == string::npos);
    m5Evidence::note("expected", {"3 reserved - 1 used = 2 unused", "penalty 5 x 2 = 10, once", "no rate: unknown, no amount"});
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

TEST_CASE("X-STATE-02/synthetic: committed totals count the selected plan once: 60 units, 2 trucks, 1 unused, penalty 5" * doctest::test_suite("m5")) {
    M5PolicyProfile profile = syntheticProfile();
    profile.penaltyPerUnusedTruck = 5.0;
    const M5LaneDateInput input = laneWith(fx30Units("u", 60), 3);
    const M5LaneOutcome outcome = planWithServices(input, profile, [](M5RunServices& services) {
        services.methods = {make_shared<PatternPlanner>(), make_shared<BlockPlanner>()};
    });
    requireInvariants(outcome, input);
    REQUIRE(outcome.methods.size() == 2);
    for (const MethodRecord& record : outcome.methods) {
        CHECK(record.ran);
        CHECK(record.validated);
        CHECK(record.unitLoads == 60.0);
        CHECK(record.trucks == 2);
    }
    const M5ReportData report = buildReportData(outcome, input, &profile, {});
    CHECK(report.unitLoadsShipped == 60.0);
    CHECK(report.unitLoadsShipped != 120.0);
    CHECK(report.loadedTrucks == 2);
    CHECK(report.loadedTrucks != 4);
    CHECK(report.unusedReservations == 1);
    CHECK(*report.penaltyAmount == 5.0);
    REQUIRE(outcome.delta);
    CHECK(outcome.delta->trucks.size() == 2);
    CHECK(shippedCount(outcome) == 60);
    m5Evidence::note("expected", {"A and B each ship 60 in 2 of 3 reserved trucks", "committed once: 60 units, 2 trucks, 1 unused, penalty 5", "not 120 units or 4 trucks"});
}

TEST_CASE("X-STATE-07/synthetic: two groups competing for the last reservation: exactly one wins, never both" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 30, "A", 2), fx30Units("b", 30, "B", 2)), 1);
    const M5LaneOutcome first = plan(input, syntheticProfile());
    requireInvariants(first, input);
    REQUIRE(first.selected->trucks.size() == 1);
    CHECK(shippedCount(first) == 30);
    const size_t byA = shippedOfGroup(first, input, "A"), byB = shippedOfGroup(first, input, "B");
    CHECK(((byA == 30 && byB == 0) || (byA == 0 && byB == 30)));
    for (int run = 0; run < 5; ++run) CHECK(shippedIds(plan(input, syntheticProfile()), input) == shippedIds(first, input));

    const M5LaneDateInput urgent = laneWith(concat(fx30Units("a", 30, "A", 5), fx30Units("b", 30, "B", 0)), 1);
    const M5LaneOutcome byPriority = plan(urgent, syntheticProfile());
    CHECK(shippedOfGroup(byPriority, urgent, "B") == 30);
    CHECK(shippedOfGroup(byPriority, urgent, "A") == 0);
    m5Evidence::note("expected", {"equal priority: one group takes the only truck by the fixed tie rule, repeatably", "P0 against P5: the P0 group wins", "never both"});
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

TEST_CASE("X-STATE-08/synthetic U-RB-05 equal trucks are not a tie: 30 P0 strictly beats 30 P1" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 30, "A", 0), fx30Units("b", 30, "B", 1)), 1);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedOfGroup(outcome, input, "A") == 30);
    CHECK(shippedOfGroup(outcome, input, "B") == 0);
    CHECK(shippedCount(outcome) == 30);
    m5Evidence::note("expected", {"same trucks, fill and dates: A (30 P0) strictly dominates B (30 P1)", "A wins; no tie rule involved"});
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

TEST_CASE("X-STATE-04/synthetic: settings changed in one process plan as a fresh process would" * doctest::test_suite("m5")) {
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
    const set<string> freshPullOn = shippedIds(plan(pullOn, syntheticProfile()), pullOn);
    CHECK(shippedCount(plan(pullOn, syntheticProfile())) == 10);
    CHECK(shippedCount(plan(pullOff, syntheticProfile())) == 0);
    CHECK(shippedCount(plan(pullOn, syntheticProfile())) == 10);
    CHECK(shippedIds(plan(pullOn, syntheticProfile()), pullOn) == freshPullOn);
    CHECK(shippedIds(plan(pullOff, syntheticProfile()), pullOff).empty());
    m5Evidence::note("expected", {"pull on, off, on: 10, 0, 10, each equal to a fresh run", "age-first then priority-first each equal to a fresh run"});
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

TEST_CASE("X-CG-08/synthetic: pricing at its round cap, or returning the same pattern again (also reordered), stops with a checked result and a recorded reason" * doctest::test_suite("m5")) {
    // The same fresh pattern (one 5, two 2s) offered on every call, on the sizes in either order.
    for (const vector<long long>& sizes : {vector<long long>{5, 5, 2, 2, 2, 2, 2}, vector<long long>{2, 2, 2, 2, 2, 5, 5}}) {
        const ScalarModel repeated = scalarModel(sizes, 10);
        size_t calls = 0;
        ScalarLimits limits;
        limits.pricing = [&](const vector<long long>& types, long long, const vector<double>&, const vector<long long>&) {
            ++calls;
            vector<int> counts(types.size(), 0);
            for (size_t type = 0; type < types.size(); ++type) counts[type] = types[type] == 5 ? 1 : 2;
            return PricedPattern{counts, 2.0, true};
        };
        const ScalarResult result = solveCuttingStock(repeated, limits);
        REQUIRE(result.packing.has_value());
        CHECK(m5Oracles::packingValid(repeated, *result.packing, result.impossible));
        CHECK(result.evidence.columnsGenerated == 3);
        CHECK(calls <= 4);
        CHECK(result.evidence.termination == Termination::Completed);
        CHECK_FALSE(result.evidence.lpStatus.empty());
    }
    m5Evidence::note("expected", {"a repeated pattern enters once (3 columns)", "pricing calls <= 4", "round cap 1: IterationCap, pricingIncomplete"});
    const ScalarModel model = scalarModel({6, 5, 3, 2, 2, 2, 4, 4, 7, 1}, 10);
    ScalarLimits capped;
    capped.maxColumnRounds = 1;
    const ScalarResult result = solveCuttingStock(model, capped);
    REQUIRE(result.packing.has_value());
    CHECK(m5Oracles::packingValid(model, *result.packing, result.impossible));
    CHECK(result.evidence.columnsGenerated <= model.sizes.size() + 1);
    CHECK(static_cast<long long>(result.packing->bins.size()) == m5Oracles::bruteForceBins(model.sizes, 10));
    CHECK(result.evidence.termination == Termination::IterationCap);
    CHECK(result.evidence.lpStatus == "pricingIncomplete");
}

TEST_CASE("X-CG-11/synthetic: the FFD trap stopped before the better answer keeps the checked 3-bin result, labelled timed out, not best" * doctest::test_suite("m5")) {
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
    CHECK(after.evidence.incumbent == 3);
    CHECK(m5Oracles::bruteForceBins(model.sizes, 10) == 2);
    m5Evidence::note("expected", {"stopped first: no result, Budget", "stopped after FFD: 3 bins kept, Budget, Unproven (best is 2)"});
}

TEST_CASE("X-CG-07/synthetic: reduced costs just past the tolerance on either side: an improving pattern enters, a non-improving one does not, no endless loop" * doctest::test_suite("m5")) {
    const ScalarModel model = scalarModel({5, 5, 5, 5, 3, 3, 3, 3, 2, 2}, 10);
    const ScalarResult result = solveCuttingStock(model, ScalarLimits());
    CHECK(static_cast<long long>(result.packing->bins.size()) == m5Oracles::bruteForceBins(model.sizes, 10));
    CHECK(result.evidence.termination == Termination::Completed);
    // [5,5,2,2,2,2,2]: a fresh pattern (one 5, two 2s) priced at reduced cost -/+ 1e-6 with tolerance 1e-9.
    const ScalarModel small = scalarModel({5, 5, 2, 2, 2, 2, 2}, 10);
    for (const double reducedCost : {-1e-6, 1e-6}) {
        INFO("reduced cost " << reducedCost);
        size_t calls = 0;
        ScalarLimits limits;
        limits.pricing = [&](const vector<long long>&, long long, const vector<double>&, const vector<long long>&) {
            ++calls;
            return PricedPattern{{1, 2}, 1.0 - reducedCost, true};
        };
        const ScalarResult priced = solveCuttingStock(small, limits);
        REQUIRE(priced.packing.has_value());
        CHECK(m5Oracles::packingValid(small, *priced.packing, priced.impossible));
        CHECK(priced.evidence.columnsGenerated == (reducedCost < 0 ? 3u : 2u));
        CHECK(calls <= 4);
        CHECK(priced.evidence.termination == Termination::Completed);
        CHECK(static_cast<long long>(priced.packing->bins.size()) == m5Oracles::bruteForceBins(small.sizes, 10));
    }
    m5Evidence::note("expected", {"best answer, Completed", "rc -1e-6 enters (3 columns), +1e-6 does not (2)", "pricing calls <= 4"});
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

TEST_CASE("X-DATE-02/synthetic: goods available D+1 cannot ship D despite pull-forward" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("now-", 30),
                                                    fx30Units("later-", 30, "A", 0, dayAfter(5), dayAfter(1))),
                                            2, kDefaultTrailer, true);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedWithIdPrefix(outcome, input, "now-") == 30);
    CHECK(shippedWithIdPrefix(outcome, input, "later-") == 0);
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 30);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 1);
    m5Evidence::note("expected", {"30 due D ship on one of two reservations", "30 due D+5 but available D+1 stay"});
}

TEST_CASE("X-DATE-03/synthetic: pull off keeps the same product's D+1 goods from making a second copy" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("now-", 30), fx30Units("later-", 30, "A", 0, dayAfter(1))), 2);
    CHECK(input.atoms[0].components[0].product == input.atoms[30].components[0].product);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK_FALSE(input.dates.pullForward);
    CHECK(shippedWithIdPrefix(outcome, input, "now-") == 30);
    CHECK(shippedWithIdPrefix(outcome, input, "later-") == 0);
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 30);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 1);
    m5Evidence::note("expected", {"same product: 30 due D, 30 due D+1", "pull off: one copy on one truck, D+1 stays"});
}

TEST_CASE("U-RS-08/synthetic: carry forward on: goods not taken move once to the lane's next reserved date; with no next date they are left over" * doctest::test_suite("m5")) {
    M5LaneDateInput first = laneWith(fx30Units("u", 60, "A", 3), 1);
    M5LaneDateInput fourLater = laneWith({}, 1);
    fourLater.departure = dayAfter(4);
    FakeClock clock;
    const LinkedDateBenchmark carried = benchmarkLinkedDates({first, fourLater}, syntheticProfile(), physicalSettings(), clock);
    REQUIRE(carried.outcomes.size() == 2);
    const set<string> onD = shippedIds(carried.outcomes[0], carried.inputs[0]);
    const set<string> onLater = shippedIds(carried.outcomes[1], carried.inputs[1]);
    CHECK(onD.size() == 30);
    CHECK(onLater.size() == 30);
    // Moved, not copied: exactly the 30 not taken on D, each once, none shipped twice.
    CHECK(carried.inputs[1].atoms.size() == 30);
    for (const string& id : onLater) CHECK(onD.count(id) == 0);
    set<string> all(onD);
    all.insert(onLater.begin(), onLater.end());
    CHECK(all.size() == 60);
    CHECK(carried.outcomes[1].leftOver.empty());
    // The last reserved date: no date is made up; what does not fit is left over.
    const LinkedDateBenchmark last = benchmarkLinkedDates({first}, syntheticProfile(), physicalSettings(), clock);
    REQUIRE(last.outcomes.size() == 1);
    CHECK(shippedCount(last.outcomes[0]) == 30);
    CHECK(countCause(last.outcomes[0], LeftOverCause::OverReservation) == 30);
    m5Evidence::note("expected", {"D: 30 ship", "D+4: the other 30 ship, moved once", "no next date: 30 left over"});
}

TEST_CASE("U-RS-09/synthetic: pull forward on, 10 days: due D+9 and D+10 come in, D+11 does not; AVAIL_DATE still applies" * doctest::test_suite("m5")) {
    const vector<Atom> atoms = concat(concat(fx30Units("d9-", 5, "A", 0, dayAfter(9)), fx30Units("d10-", 5, "A", 0, dayAfter(10))),
                                      concat(fx30Units("d11-", 5, "A", 0, dayAfter(11)), fx30Units("late-", 5, "A", 0, dayAfter(10), dayAfter(1))));
    const M5LaneDateInput input = laneWith(atoms, 1, kDefaultTrailer, true);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedWithIdPrefix(outcome, input, "d9-") == 5);
    CHECK(shippedWithIdPrefix(outcome, input, "d10-") == 5);
    CHECK(shippedWithIdPrefix(outcome, input, "d11-") == 0);
    CHECK(shippedWithIdPrefix(outcome, input, "late-") == 0);
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 10);
    const DateRules rules{true, 10};
    CHECK(atomEligibility(input.atoms[10], kD, rules).reason == EligibilityReason::DueBeyondPullWindow);
    CHECK(atomEligibility(input.atoms[15], kD, rules).reason == EligibilityReason::NotAvailable);
    m5Evidence::note("expected", {"D+9 and D+10 ship", "D+11 beyond the window", "available D+1: not on D"});
}

TEST_CASE("U-RS-10/synthetic: pull forward off: nothing ships early, a mixed-date stack included (R-3a)" * doctest::test_suite("m5")) {
    Atom mixed = unitOf("mixed", "A", 100, 20, 108, 1500);
    mixed.unitLoads = 2.0;
    Component later = mixed.components[0];
    later.source = mixed.components[0].source + "-later";
    later.dueDate = dayAfter(1);
    later.offerKind = OfferKind::CanGo;
    mixed.components.push_back(later);
    const vector<Atom> atoms = concat(concat(fx30Units("d", 10), fx30Units("d1-", 10, "A", 0, dayAfter(1))), {mixed});
    const M5LaneDateInput input = laneWith(atoms, 1, kDefaultTrailer, false);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedWithIdPrefix(outcome, input, "d1-") == 0);
    CHECK(shippedIds(outcome, input).count("mixed") == 0);
    CHECK(shippedCount(outcome) == 10);
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 11);
    CHECK(atomEligibility(mixed, kD, DateRules{false, 10}).failingComponents == vector<size_t>{1});
    m5Evidence::note("expected", {"10 due D ship", "10 due D+1 and the mixed stack not eligible"});
}

TEST_CASE("U-RS-11/synthetic: goods available after the ship date are not shipped on that date" * doctest::test_suite("m5")) {
    const vector<Atom> atoms = concat(fx30Units("now-", 10), fx30Units("late-", 10, "A", 0, kD, dayAfter(1)));
    for (const bool pull : {false, true}) {
        INFO("pull forward " << pull);
        const M5LaneDateInput input = laneWith(atoms, 1, kDefaultTrailer, pull);
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        requireInvariants(outcome, input);
        CHECK(shippedWithIdPrefix(outcome, input, "now-") == 10);
        CHECK(shippedWithIdPrefix(outcome, input, "late-") == 0);
        CHECK(countCause(outcome, LeftOverCause::NotEligible) == 10);
    }
    // The same goods leave on D+1, the day they are available.
    M5LaneDateInput next = laneWith(fx30Units("late-", 10, "A", 0, kD, dayAfter(1)), 1);
    next.departure = dayAfter(1);
    CHECK(shippedCount(plan(next, syntheticProfile())) == 10);
    m5Evidence::note("expected", {"available D+1: not shipped on D, pull on or off", "shipped on D+1"});
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

TEST_CASE("X-DATE-04/synthetic: carry moves the unshipped 30 to D+4 with due date and priority intact" * doctest::test_suite("m5")) {
    const M5LaneDateInput first = laneWith(fx30Units("u", 60, "A", 3), 1);
    M5LaneDateInput later = laneWith({}, 1);
    later.departure = dayAfter(4);
    FakeClock clock;
    const LinkedDateBenchmark linked = benchmarkLinkedDates({first, later}, syntheticProfile(), physicalSettings(), clock);
    REQUIRE(linked.outcomes.size() == 2);
    CHECK(shippedCount(linked.outcomes[0]) == 30);
    CHECK(shippedCount(linked.outcomes[1]) == 30);
    CHECK(linked.inputs[1].atoms.size() == 30);
    const set<string> onD = shippedIds(linked.outcomes[0], linked.inputs[0]);
    const set<string> onD4 = shippedIds(linked.outcomes[1], linked.inputs[1]);
    for (const string& id : onD4) CHECK(onD.count(id) == 0);
    for (const Atom& atom : linked.inputs[1].atoms) {
        CHECK(atom.components[0].dueDate == kD);
        CHECK(atom.components[0].priority == 3);
    }
    CHECK(onD.size() + onD4.size() == 60);
    CHECK(linked.outcomes[1].leftOver.empty());
    m5Evidence::note("expected", {"30 ship D", "the other 30 ship D+4 once", "their due D and priority 3 persist"});
}

TEST_CASE("X-DATE-05/synthetic: the last reservation ships 30 and leaves 30 without inventing a date" * doctest::test_suite("m5")) {
    const M5LaneDateInput last = laneWith(fx30Units("u", 60), 1);
    FakeClock clock;
    const LinkedDateBenchmark linked = benchmarkLinkedDates({last}, syntheticProfile(), physicalSettings(), clock);
    REQUIRE(linked.outcomes.size() == 1);
    REQUIRE(linked.inputs.size() == 1);
    CHECK(linked.inputs[0].departure == kD);
    CHECK(shippedCount(linked.outcomes[0]) == 30);
    CHECK(countCause(linked.outcomes[0], LeftOverCause::OverReservation) == 30);
    CHECK(linked.outcomes[0].reservedTrucks == 1);
    requireInvariants(linked.outcomes[0], linked.inputs[0]);
    m5Evidence::note("expected", {"one final reserved date", "30 ship, 30 left once", "no later date created"});
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

TEST_CASE("X-DATE-07/synthetic: pull and carry on two dates never duplicate a source" * doctest::test_suite("m5")) {
    const vector<Atom> todayGoods = concat(fx30Units("now-", 10), fx30Units("future-", 20, "A", 0, dayAfter(1)));
    const M5LaneDateInput today = laneWith(todayGoods, 1, kDefaultTrailer, true);
    M5LaneDateInput tomorrow = laneWith(fx30Units("future-", 20, "A", 0, dayAfter(1)), 1, kDefaultTrailer, true);
    tomorrow.departure = dayAfter(1);
    FakeClock clock;
    const LinkedDateBenchmark linked = benchmarkLinkedDates({today, tomorrow}, syntheticProfile(), physicalSettings(), clock);
    REQUIRE(linked.outcomes.size() == 2);
    const set<string> first = shippedIds(linked.outcomes[0], linked.inputs[0]);
    const set<string> second = shippedIds(linked.outcomes[1], linked.inputs[1]);
    set<string> all = first;
    all.insert(second.begin(), second.end());
    CHECK(all.size() == 30);
    CHECK(first.size() + second.size() == 30);
    CHECK(linked.inputs[1].atoms.empty());
    CHECK(linked.outcomes[0].leftOver.empty());
    CHECK(linked.outcomes[1].leftOver.empty());
    m5Evidence::note("expected", {"carry and pull enabled", "30 source units ship across two dates once", "none left or copied"});
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

TEST_CASE("X-DATE-10/synthetic: date arithmetic crosses months, years and leap days with M4's helper" * doctest::test_suite("m5")) {
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

TEST_CASE("X-DATA-07/synthetic: a product can share a shape without losing line facts" * doctest::test_suite("m5")) {
    vector<Atom> atoms;
    atoms.push_back(fx30("first", "group_one", 0, kD));
    atoms.push_back(fx30("second", "group_one", 11, dayAfter(1)));
    atoms.push_back(fx30("third", "group_two", 4, kD));
    for (Atom& atom : atoms) atom.components[0].product = "same-product";
    const M5LaneDateInput input = laneWith(atoms, 2, kDefaultTrailer, true);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedCount(outcome) == 3);
    CHECK(atoms[0].components[0].source != atoms[1].components[0].source);
    CHECK(atoms[0].components[0].priority == 0);
    CHECK(atoms[1].components[0].priority == 11);
    CHECK(atoms[1].components[0].dueDate == dayAfter(1));
    CHECK(atoms[2].group == "group_two");
    CHECK(reconcile(*buildSnapshot(input).snapshot, *outcome.selected).balanced);
    m5Evidence::note("expected", {"one product on three source lines", "P0 and P11, D and D+1, two groups remain distinct"});
}

TEST_CASE("X-DATA-01/synthetic: sixty M4 unit loads stay sixty through either M5 method" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("unit-", 60);
    for (Atom& atom : atoms) atom.components[0].product = "cases-per-load-10";
    const M5LaneDateInput input = laneWith(atoms, 2);
    for (const MethodPolicy method : {MethodPolicy::PatternOnly, MethodPolicy::BlocksOnly}) {
        M5PolicyProfile profile = syntheticProfile();
        profile.method = method;
        const M5LaneOutcome outcome = plan(input, profile);
        requireInvariants(outcome, input);
        REQUIRE(outcome.selected);
        CHECK(outcome.selected->trucks.size() == 2);
        CHECK(shippedCount(outcome) == 60);
        CHECK(outcome.leftOver.empty());
        double shippedLoads = 0.0;
        for (const TruckPlan& truck : outcome.selected->trucks) {
            for (const size_t index : truck.atoms) shippedLoads += input.atoms[index].unitLoads;
        }
        CHECK(shippedLoads == 60.0);
    }
    m5Evidence::note("expected", {"M4 hand-over 60 unit loads, Cases_Unit_Load 10", "A and B each dispatch 60, never 6"});
}

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

TEST_CASE("X-DATA-02/synthetic: conservation is checked per source line, not only in total" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("x", 10);
    for (Atom& atom : atoms) { atom.components[0].source = "line-X"; atom.components[0].product = "X"; }
    vector<Atom> y = fx30Units("y", 20);
    for (Atom& atom : y) { atom.components[0].source = "line-Y"; atom.components[0].product = "Y"; }
    atoms = concat(atoms, y);
    const M5LaneDateInput input = laneWith(atoms, 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan swapped;
    TruckPlan truck;
    truck.group = "A";
    for (size_t atom = 0; atom < 10; ++atom) truck.atoms.push_back(atom);
    swapped.trucks.push_back(truck);
    // Faulty output is X=20, Y=10 while its grand total remains 30.
    for (size_t atom = 0; atom < 10; ++atom) swapped.leftOver.push_back({atom, LeftOverCause::OverReservation});
    for (size_t atom = 10; atom < 20; ++atom) swapped.leftOver.push_back({atom, LeftOverCause::OverReservation});
    const Reconciliation result = reconcile(snapshot, swapped);
    CHECK_FALSE(result.balanced);
    CHECK(any_of(result.findings.begin(), result.findings.end(), [](const string& finding) { return finding.find("line-X") != string::npos; }));
    CHECK(any_of(result.findings.begin(), result.findings.end(), [](const string& finding) { return finding.find("line-Y") != string::npos; }));

    // Earlier fault shape, kept: an X atom named twice and one Y atom dropped, again 30 in total.
    CandidatePlan repeated;
    repeated.trucks.push_back(truck);
    for (size_t atom = 10; atom < 29; ++atom) repeated.leftOver.push_back({atom, LeftOverCause::OverReservation});
    repeated.leftOver.push_back({0, LeftOverCause::OverReservation});
    const Reconciliation repeatedResult = reconcile(snapshot, repeated);
    CHECK_FALSE(repeatedResult.balanced);
    CHECK(any_of(repeatedResult.findings.begin(), repeatedResult.findings.end(), [](const string& finding) { return finding.find("line-Y") != string::npos; }));
    m5Evidence::note("expected", {"input X=10 Y=20", "faulty output X=20 Y=10", "both line identities named despite equal total"});
}

TEST_CASE("X-DATA-09/synthetic: identities stay as M4 resolved them: 001234 and 1234 are two products" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("u", 2);
    atoms[0].components[0].product = "001234";
    atoms[1].components[0].product = "1234";
    atoms[0].group = atoms[1].group = "group_1757_1724";
    const M5LaneDateInput input = laneWith(atoms, 1);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(shippedCount(outcome) == 2);
    CHECK(input.atoms[0].components[0].product != input.atoms[1].components[0].product);
    reverse(atoms.begin(), atoms.end());
    const M5LaneDateInput shuffled = laneWith(atoms, 1);
    const M5LaneOutcome reversed = plan(shuffled, syntheticProfile());
    CHECK(shippedCount(reversed) == 2);
    CHECK(shuffled.atoms[0].components[0].product == "1234");
    CHECK(shuffled.atoms[1].components[0].product == "001234");
    CHECK(shuffled.atoms[0].group == "group_1757_1724");
    m5Evidence::note("expected", {"001234 and 1234 remain distinct", "reversed row order ships both", "underscore group kept verbatim"});
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
    M5LaneDateInput otherReservations = runOne;
    otherReservations.pool.version = runOne.pool.version + 1;
    const DemandSnapshot reservationSnapshot = *buildSnapshot(otherReservations).snapshot;
    OperationalState reservationState{reservationSnapshot.id, reservationSnapshot.input.pool.version, {}, 0, 1};
    CHECK(ledger.applyIfCurrent(*planned.delta, reservationSnapshot, reservationState) == ApplyStatus::StaleSnapshot);
    CHECK(reservationState.consumedAtoms.empty());
    CHECK(reservationState.slotsUsed == 0);
    m5Evidence::note("expected", {"different demand extract rejected", "different reservation version rejected", "no state changed"});
}

TEST_CASE("X-DATA-12/synthetic: counts near 3e9 stay exact" * doctest::test_suite("m5")) {
    CHECK(patternCopies(3'000'000'000LL, 1, 4'000'000'000LL) == 3'000'000'000LL);
    AllocationKey total, part;
    part.shippedByClass = {3'000'000'000'000LL};
    part.shippedByPriority.assign(12, 0);
    addTo(total, part);
    addTo(total, part);
    CHECK(total.shippedByClass.front() == 6'000'000'000'000LL);
    CHECK(patternCopies(3'000'000'000LL, 2, 4'000'000'000LL) == 1'500'000'000LL);
    CHECK(patternCopies(3'000'000'000LL, 1, 2'000'000'000LL) == 2'000'000'000LL);
    m5Evidence::note("expected", {"3,000,000,000 counted exactly", "6,000,000,000,000 added without overflow"});
}

TEST_CASE("X-DATA-14/synthetic: invalid unit-load quantities stop before planning, a part load stays fractional" * doctest::test_suite("m5")) {
    for (const double bad : {-1.0, 0.0, numeric_limits<double>::quiet_NaN(), numeric_limits<double>::infinity(),
                             numeric_limits<double>::max()}) {
        M5LaneDateInput input = laneWith({fx30("quantity")}, 1);
        input.atoms[0].unitLoads = input.atoms[0].components[0].unitLoads = bad;
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        CHECK(outcome.kind == OutcomeKind::InvalidInput);
        CHECK(shippedCount(outcome) == 0);
    }
    M5LaneDateInput fractional = laneWith({fx30("quantity")}, 1);
    fractional.atoms[0].unitLoads = fractional.atoms[0].components[0].unitLoads = 1.5;
    const M5LaneOutcome outcome = plan(fractional, syntheticProfile());
    CHECK(outcome.kind == OutcomeKind::CompletedWithDispatch);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks[0].atoms.size() == 1);
    CHECK(fractional.atoms[0].unitLoads == 1.5);
    m5Evidence::note("expected", {"negative, zero, NaN, infinity and out-of-range quantity refused", "1.5 unit loads retained without rounding"});
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

namespace {

// X-REPORT-10: four selected trucks with supplied structured labels. The trucks carry 1, 3, 2 and 5 stacks, so
// a cargo-weighted mean differs from the truck-weighted one. The report reads the labels; it judges nothing new.
struct StructuredReport {
    M5LaneDateInput input;
    M5LaneOutcome outcome;
};

const vector<double> kRearInches{10.0, 30.0, 100.0, 20.0};
const vector<size_t> kStacksOnTruck{1, 3, 2, 5};

StructuredReport structuredReport(const vector<size_t>& order, const function<void(vector<FullnessLabel>&)>& adjust = {}) {
    StructuredReport report;
    report.input = laneWith(fx30Units("u", 11), 4);
    vector<size_t> firstStack(4, 0);
    for (size_t truck = 1; truck < 4; ++truck) firstStack[truck] = firstStack[truck - 1] + kStacksOnTruck[truck - 1];
    CandidatePlan selected;
    vector<FullnessLabel> labels;
    const vector<FullnessBucket> buckets{FullnessBucket::GoodsRemain, FullnessBucket::GoodsRemain, FullnessBucket::NoGoodsRemain,
                                         FullnessBucket::RuleLimited};
    for (const size_t original : order) {
        TruckPlan truck;
        truck.slot = original;
        truck.group = "A";
        for (size_t stack = 0; stack < kStacksOnTruck[original]; ++stack) truck.atoms.push_back(firstStack[original] + stack);
        selected.trucks.push_back(truck);
        FullnessLabel label;
        label.truck = selected.trucks.size() - 1;
        label.rearInches = kRearInches[original];
        label.bucket = buckets[original];
        label.state = FullnessState::NotFull;
        labels.push_back(label);
    }
    if (adjust) adjust(labels);
    report.outcome.kind = OutcomeKind::CompletedWithDispatch;
    report.outcome.selected = selected;
    report.outcome.fullness = labels;
    report.outcome.reservedTrucks = 4;
    return report;
}

M5ReportData readReport(const StructuredReport& report) {
    const M5PolicyProfile profile = syntheticProfile();
    return buildReportData(report.outcome, report.input, &profile, {});
}

}  // namespace

TEST_CASE("X-REPORT-10/bucketMeans: rear spaces 10, 30, 100, 20 give bucket means 20, 100, 20 and an overall truck-weighted mean of 40" * doctest::test_suite("m5")) {
    const M5ReportData data = readReport(structuredReport({0, 1, 2, 3}));
    CHECK(*data.rearInchesByBucket.at("goods remain").averageRearInches == 20.0);
    CHECK(*data.rearInchesByBucket.at("no goods remain").averageRearInches == 100.0);
    CHECK(*data.rearInchesByBucket.at("rule-limited").averageRearInches == 20.0);
    REQUIRE(data.rearInchesAllTrucks.averageRearInches.has_value());
    CHECK(data.rearInchesAllTrucks.trucks == 4);
    CHECK(*data.rearInchesAllTrucks.averageRearInches == 40.0);
    CHECK(*data.rearInchesAllTrucks.averageRearInches != doctest::Approx(140.0 / 3.0));
    double cargoWeighted = 0.0;
    for (size_t truck = 0; truck < 4; ++truck) cargoWeighted += kRearInches[truck] * static_cast<double>(kStacksOnTruck[truck]);
    cargoWeighted /= 11.0;
    CHECK(*data.rearInchesAllTrucks.averageRearInches != doctest::Approx(cargoWeighted));
    const string text = renderAscii(data);
    CHECK(text.find("average rear inches, goods remain: 20.0 over 2 trucks") != string::npos);
    CHECK(text.find("average rear inches, no goods remain: 100.0 over 1 trucks") != string::npos);
    CHECK(text.find("average rear inches, rule-limited: 20.0 over 1 trucks") != string::npos);
    CHECK(text.find("average rear inches, all trucks: 40.0 over 4 trucks") != string::npos);
    m5Evidence::note("expected", {"buckets 20, 100, 20 in", "overall truck-weighted 40 in", "not 140/3 and not the cargo-weighted 36.4"});
}

TEST_CASE("X-REPORT-10/emptyBucket: a bucket with no truck prints N/A over 0 trucks, never 0" * doctest::test_suite("m5")) {
    const M5ReportData data = readReport(structuredReport({0, 1}));
    CHECK(data.rearInchesByBucket.at("goods remain").trucks == 2);
    CHECK(*data.rearInchesByBucket.at("goods remain").averageRearInches == 20.0);
    for (const char* empty : {"no goods remain", "rule-limited"}) {
        CHECK(data.rearInchesByBucket.at(empty).trucks == 0);
        CHECK_FALSE(data.rearInchesByBucket.at(empty).averageRearInches.has_value());
    }
    const string text = renderAscii(data);
    CHECK(text.find("average rear inches, no goods remain: N/A over 0 trucks") != string::npos);
    CHECK(text.find("average rear inches, rule-limited: N/A over 0 trucks") != string::npos);
    CHECK(text.find("no goods remain: 0") == string::npos);
    CHECK(text.find("rule-limited: 0") == string::npos);
    const M5ReportData none = readReport(structuredReport({}));
    CHECK_FALSE(none.rearInchesAllTrucks.averageRearInches.has_value());
    CHECK(renderAscii(none).find("average rear inches, all trucks: N/A over 0 trucks") != string::npos);
    m5Evidence::note("expected", {"two GoodsRemain trucks: 20", "the two other buckets: N/A over 0 trucks", "no trucks at all: overall N/A"});
}

TEST_CASE("X-REPORT-10/reordered: reordering the trucks does not change any total" * doctest::test_suite("m5")) {
    vector<size_t> order{0, 1, 2, 3};
    const M5ReportData reference = readReport(structuredReport(order));
    int permutations = 0;
    do {
        const M5ReportData data = readReport(structuredReport(order));
        ++permutations;
        for (const char* bucket : {"goods remain", "no goods remain", "rule-limited"}) {
            CHECK(data.rearInchesByBucket.at(bucket).trucks == reference.rearInchesByBucket.at(bucket).trucks);
            CHECK(*data.rearInchesByBucket.at(bucket).averageRearInches == *reference.rearInchesByBucket.at(bucket).averageRearInches);
        }
        CHECK(*data.rearInchesAllTrucks.averageRearInches == 40.0);
        CHECK(data.unitLoadsShipped == reference.unitLoadsShipped);
        CHECK(data.loadedTrucks == reference.loadedTrucks);
        CHECK(data.fullTrucks == reference.fullTrucks);
    } while (next_permutation(order.begin(), order.end()));
    CHECK(permutations == 24);
    m5Evidence::note("expected", {"all 24 truck orders give buckets 20, 100, 20 and overall 40", "unit loads and truck count equal"});
}

TEST_CASE("X-REPORT-10/overlappingReasons: one FULL truck with a weight and a rear reason is one FULL, one weight, one rear; reasons are never summed" * doctest::test_suite("m5")) {
    const StructuredReport report = structuredReport({0, 1, 2, 3}, [](vector<FullnessLabel>& labels) {
        labels[0].state = FullnessState::Full;
        labels[0].byWeight = labels[0].byRearInches = true;
    });
    const M5ReportData data = readReport(report);
    CHECK(data.fullTrucks == 1);
    CHECK(data.fullByWeight == 1);
    CHECK(data.fullByRear == 1);
    CHECK(data.fullByFloor == 0);
    CHECK(data.fullByWeight + data.fullByRear != data.fullTrucks);
    REQUIRE(data.trucks.size() == 4);
    CHECK(data.trucks[0].reasons == "weight, rear inches");
    CHECK(data.trucks[1].fullness == "NOT FULL");
    CHECK(renderAscii(data).find("FULL trucks: 1 of 4 (by weight 1, by rear inches 1, floor-limited 0)") != string::npos);
    m5Evidence::note("expected", {"unique FULL count 1", "weight-reason count 1, rear-reason count 1", "the counters overlap and are never added as disjoint shipments"});
}

TEST_CASE("X-REPORT-10/unknownFit: an unknown remaining fit is reported apart and never counted as floor-limited" * doctest::test_suite("m5")) {
    const StructuredReport report = structuredReport({0, 1, 2, 3}, [](vector<FullnessLabel>& labels) {
        labels[1].fitStatus = RemainingFitStatus::Unknown;
        labels[1].state = FullnessState::Undetermined;
        labels[2].fitStatus = RemainingFitStatus::Unknown;
        labels[2].state = FullnessState::Undetermined;
        labels[3].fitStatus = RemainingFitStatus::ProvenNoFit;
        labels[3].byFloorLimit = true;
        labels[3].state = FullnessState::Full;
    });
    const M5ReportData data = readReport(report);
    CHECK(data.unknownFitTrucks == 2);
    CHECK(data.fullByFloor == 1);
    CHECK(data.fullTrucks == 1);
    REQUIRE(data.trucks.size() == 4);
    for (const size_t unknown : {size_t{1}, size_t{2}}) {
        CHECK(data.trucks[unknown].reasons.find("floor-limited") == string::npos);
        CHECK(data.trucks[unknown].fit == string(remainingFitName(RemainingFitStatus::Unknown)));
    }
    CHECK(data.trucks[3].reasons.find("floor-limited") != string::npos);
    const string text = renderAscii(data);
    CHECK(text.find("trucks with remaining fit unknown: 2") != string::npos);
    CHECK(text.find("floor-limited 1)") != string::npos);
    m5Evidence::note("expected", {"two Unknown trucks counted separately", "only the proven no-fit truck is floor-limited"});
}

TEST_CASE("X-REPORT-10/unselectedAndBenchmarkExcluded: unselected A/B candidates and benchmark-only results never enter the selected production totals" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 75), 3);
    const M5LaneOutcome outcome = planWithServices(input, syntheticProfile(), [](M5RunServices& services) {
        services.methods = {make_shared<PatternPlanner>(), make_shared<BlockPlanner>()};
    });
    REQUIRE(outcome.methods.size() == 2);
    M5LaneOutcome padded = outcome;
    padded.methods[0].unitLoads = padded.methods[1].unitLoads = 900.0;
    padded.methods[0].trucks = padded.methods[1].trucks = 40;
    const M5PolicyProfile profile = syntheticProfile();
    const M5ReportData data = buildReportData(padded, input, &profile, {"PRODUCTION", 20, "fixture"});
    CHECK(data.unitLoadsShipped == 75.0);
    CHECK(data.loadedTrucks == 3);
    CHECK(data.unitLoadsShipped == buildReportData(outcome, input, &profile, {"PRODUCTION", 20, "fixture"}).unitLoadsShipped);

    // The labelled benchmark commits nothing and its text is its own: the production report holds no benchmark rows.
    FakeClock clock;
    const LinkedDateBenchmark benchmark = benchmarkLinkedDates({input, laneWith(fx30Units("later-", 30), 1, kDefaultTrailer, true)}, profile, physicalSettings(), clock);
    CHECK(benchmark.text.find("BENCHMARK") != string::npos);
    const string production = renderAscii(data);
    CHECK(production.find("BENCHMARK") == string::npos);
    CHECK(production.find("none in a production run") != string::npos);
    CHECK(data.unitLoadsShipped == 75.0);
    m5Evidence::note("expected", {"production totals count the selected plan only: 75 unit loads, 3 trucks", "unselected methods' own figures never added", "benchmark text labelled and kept apart"});
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

TEST_CASE("X-REPORT-01/structuredInput: a 21-truck 7 Aug lane reports 21 reserved; the other date reports its own 20; 38 is never printed as reserved" * doctest::test_suite("m5")) {
    const M5PolicyProfile profile = syntheticProfile();
    const M5LaneDateInput aug7 = laneWith(fx30Units("u", 21 * 30), 21);
    const M5LaneOutcome outcome = plan(aug7, profile);
    requireInvariants(outcome, aug7);
    const M5ReportData data = buildReportData(outcome, aug7, &profile, {"PRODUCTION", 20, "fixture"});
    CHECK(data.reservedTrucks == 21);
    CHECK(data.loadedTrucks == 21);
    CHECK(data.unusedReservations == 0);
    const string text = renderAscii(data);
    CHECK(text.find("reserved: 21\n") != string::npos);
    CHECK(text.find("trucks loaded: 21\n") != string::npos);
    CHECK(text.find("reserved: 38") == string::npos);

    FakeClock clock;
    const M5LaneDateInput aug8 = laneWith(fx30Units("later-", 20 * 30), 20, kDefaultTrailer, true);
    const LinkedDateBenchmark benchmark = benchmarkLinkedDates({aug7, aug8}, profile, physicalSettings(), clock);
    REQUIRE(benchmark.outcomes.size() == 2);
    CHECK(benchmark.text.find("reserved: 21\n") != string::npos);
    CHECK(benchmark.text.find("reserved: 20\n") != string::npos);
    CHECK(benchmark.text.find("reserved: 38") == string::npos);
    CHECK(benchmark.text.find("reserved: 41") == string::npos);
    m5Evidence::note("expected", {"7 Aug: reserved 21, loaded 21", "8 Aug: reserved 20", "never reserved 38; the two dates are reported apart (21 + 20 = 41)"});
    m5Evidence::note("notCovered", "the comparison with the client's 18 on 7 Aug needs the local reference file (PR-13)");
}

TEST_CASE("X-REPORT-02/structuredInput: a both-dates benchmark is labelled and kept apart from the production report; its numbers are results, not targets" * doctest::test_suite("m5")) {
    const M5PolicyProfile profile = syntheticProfile();
    const M5LaneDateInput first = laneWith(fx30Units("a", 3 * 30), 3);
    const M5LaneDateInput second = laneWith(fx30Units("b", 2 * 30), 2, kDefaultTrailer, true);
    FakeClock clock;
    const LinkedDateBenchmark benchmark = benchmarkLinkedDates({first, second}, profile, physicalSettings(), clock);
    CHECK(benchmark.text.find("BENCHMARK (labelled, not production; nothing committed)") != string::npos);
    REQUIRE(benchmark.outcomes.size() == 2);
    const size_t trucks = benchmark.outcomes[0].selected->trucks.size() + benchmark.outcomes[1].selected->trucks.size();
    CHECK(trucks == 5);
    const M5LaneOutcome production = plan(first, profile);
    const string productionText = renderAscii(buildReportData(production, first, &profile, {"PRODUCTION", 20, "fixture"}));
    CHECK(productionText.find("BENCHMARK") == string::npos);
    CHECK(productionText.find("scope: PRODUCTION") != string::npos);
    CHECK(productionText.find("3D Threshold benchmark\n  rows: none in a production run: benchmark runs are labelled and kept apart\n") != string::npos);
    CHECK(shippedCount(production) == 90);
    m5Evidence::note("expected", {"benchmark text labelled BENCHMARK, nothing committed", "production report has no benchmark rows", "figures come from the data: 5 trucks here, not 23 or 38"});
}

TEST_CASE("X-REPORT-03/structuredInput: a plan without a certificate is never called infeasible, whatever an estimate says; no old baseline figure is printed" * doctest::test_suite("m5")) {
    const M5PolicyProfile profile = syntheticProfile();
    const M5LaneDateInput input = gapLane(30);
    const M5LaneOutcome outcome = plan(input, profile);
    requireInvariants(outcome, input);
    const M5ReportData data = buildReportData(outcome, input, &profile, {"PRODUCTION", 20, "fixture"});
    const string text = renderAscii(data);
    CHECK(outcome.proof != ProofStatus::ProvenInfeasible);
    CHECK(text.find("infeasible") == string::npos);
    CHECK(text.find("proven infeasible") == string::npos);
    CHECK(text.find("service proof: ") != string::npos);
    for (const char* oldFigure : {"635", "3,171", "3,209", "3,192"}) CHECK(text.find(oldFigure) == string::npos);
    // A figure an estimate gave is not turned into a proof: the quick count and the bound are shown as what they are.
    CHECK(text.find("quick count vs bound: ") != string::npos);
    CHECK(text.find("count evidence: ") != string::npos);
    m5Evidence::note("expected", {"no certificate: no infeasible wording", "estimate and bound shown as such", "no historical 635 or old means in a current report"});
    m5Evidence::note("notCovered", "747 stacks / 763 units and the means 3,171/3,209/3,192 are reproduced only in the real-data run (PR-13)");
}

TEST_CASE("X-REPORT-04/productionProfile: x = 40,000 lb and y = 3,300 cu ft decide by weight OR cube, strictly, from full precision; the report states the rule" * doctest::test_suite("m5")) {
    const M5PolicyProfile profile = withAcceptance(syntheticProfile(), 40000.0, 3300.0);
    struct Case {
        double cubeCuFt;
        bool passes;
    };
    for (const Case& value : {Case{3299.9, false}, Case{3300.0, false}, Case{3300.0 + 0.5 / 1728.0, true}, Case{3300.1, true}, Case{3307.5, true}}) {
        INFO("cube " << value.cubeCuFt);
        vector<Atom> atoms = fx30Units("u", 1);
        atoms[0].weightLb = 20000.0;
        atoms[0].floor.cubeCubicIn = value.cubeCuFt * 1728.0;
        const M5LaneDateInput input = laneWith(atoms, 1);
        const M5LaneOutcome outcome = plan(input, profile);
        requireInvariants(outcome, input);
        CHECK(shippedCount(outcome) == (value.passes ? 1u : 0u));
        const string text = renderAscii(buildReportData(outcome, input, &profile, {"SYNTHETIC", 20, "fixture"}));
        CHECK(text.find("optional policy ON: weight > 40000.0 lb OR cube > 3300.0 cu ft (strict)") != string::npos);
        CHECK(text.find("84%") == string::npos);
        CHECK(text.find("3307.5") == string::npos);
        CHECK(text.find(profile.key()) != string::npos);
    }
    // Weight cannot change the result: 20,000 lb alone never passes x = 40,000.
    CHECK_FALSE(evaluateAcceptance(20000.0, 3299.9 * 1728.0, *profile.acceptance).byWeight);
    CHECK(evaluateAcceptance(20000.0, 3300.1 * 1728.0, *profile.acceptance).byCube);
    CHECK(evaluateAcceptance(40000.5, 100.0 * 1728.0, *profile.acceptance).accepted);
    m5Evidence::note("expected", {"3,299.9 and 3,300.0 withheld; 3,300.1 and 3,307.5 ship", "a cube 0.5 cu in above y ships although both print 3,300.0", "numeric x/y, strict OR and the profile named; no 84% rule"});
}

TEST_CASE("X-REPORT-06/synthetic: no eligible input, a completed zero dispatch and a timeout with no result are told apart; empty averages are N/A" * doctest::test_suite("m5")) {
    const M5PolicyProfile profile = syntheticProfile();
    const M5LaneDateInput twenty = laneWith(fx30Units("u", 20), 1);
    const M5PolicyProfile strict = withAcceptance(profile, 40000.0, 3000.0);
    const M5LaneDateInput none = laneWith({}, 1);
    const M5LaneOutcome noWork = plan(none, profile);
    const M5LaneOutcome zero = plan(twenty, strict);
    FakeClock clock;
    const M5LaneOutcome timedOut = [&] {
        M5RunServices services(clock);
        services.allowanceSeconds = 5.0;
        services.beforeMethod = [&](MethodId) { clock.advance(6.0); };
        return planLaneDate(twenty, profile, physicalSettings(), services);
    }();
    CHECK(noWork.kind == OutcomeKind::CompletedNoEligibleInput);
    CHECK(zero.kind == OutcomeKind::CompletedZeroDispatch);
    CHECK(timedOut.kind == OutcomeKind::TimeoutNoIncumbent);
    const string noWorkText = renderAscii(buildReportData(noWork, none, &profile, {}));
    const string zeroText = renderAscii(buildReportData(zero, twenty, &strict, {}));
    const string timedOutText = renderAscii(buildReportData(timedOut, twenty, &profile, {}));
    CHECK(noWorkText.find("outcome: completed: no eligible goods") != string::npos);
    CHECK(zeroText.find("outcome: completed: no truck dispatched") != string::npos);
    CHECK(zeroText.find("withheld by minimum-load policy") != string::npos);
    CHECK(timedOutText.find("outcome: timed out: no checked result") != string::npos);
    CHECK(noWorkText != zeroText);
    CHECK(zeroText != timedOutText);
    for (const string* text : {&noWorkText, &zeroText, &timedOutText}) {
        CHECK(text->find("seconds per loaded truck: N/A") != string::npos);
        CHECK(text->find("average rear inches, all trucks: N/A over 0 trucks") != string::npos);
        CHECK(text->find("average rear inches, goods remain: N/A over 0 trucks") != string::npos);
        CHECK(text->find("nan") == string::npos);
        CHECK(text->find("inf") == string::npos);
        CHECK(text->find("trucks loaded: 0\n") != string::npos);
    }
    m5Evidence::note("expected", {"three different outcome lines", "zero dispatch names the 20 withheld", "averages and seconds per loaded truck N/A, no nan or inf"});
    m5Evidence::note("notCovered", "a run with no M5 lane-date at all has no M5 report to print; only the lane-date outcomes are covered");
}

TEST_CASE("X-REPORT-07/synthetic: 90 units, 60 shipped, 30 carried: 90 = 60 + 30, the 30 not counted twice" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 90), 2);
    const M5PolicyProfile profile = syntheticProfile();
    const M5LaneOutcome outcome = plan(input, profile);
    requireInvariants(outcome, input);
    const M5ReportData data = buildReportData(outcome, input, &profile, {"PRODUCTION", 20, "fixture"});
    double left = 0.0;
    for (const auto& [cause, loads] : data.unitLoadsLeftByCause) left += loads;
    CHECK(data.unitLoadsIn == 90.0);
    CHECK(data.unitLoadsShipped == 60.0);
    CHECK(left == 30.0);
    CHECK(data.unitLoadsShipped + left == data.unitLoadsIn);
    CHECK(data.unitLoadsLeftByCause.size() == 1);
    CHECK(data.unitLoadsLeftByCause.at("over the reservation") == 30.0);
    const string text = renderAscii(data);
    CHECK(text.find("unit loads in / shipped / of them can-go: 90.00 / 60.00 / 0.00") != string::npos);
    // The carried goods, planned again next day, ship once there and are not in today's shipped figure.
    const M5LaneDateInput carried = laneWith(fx30Units("u", 90), 2);
    CHECK(shippedCount(plan(carried, profile)) == 60);
    m5Evidence::note("expected", {"90 in = 60 shipped + 30 left", "the 30 appear once, under over the reservation"});
}

TEST_CASE("X-REPORT-08/synthetic: A finishes; B not started, timed out or failed shows its real status, with no old numbers and no (run) left" * doctest::test_suite("m5")) {
    const M5PolicyProfile profile = syntheticProfile();
    const M5LaneDateInput input = laneWith(fx30Units("u", 75), 3);
    // An earlier full run: both methods ran and shipped 75 on 3 trucks.
    const M5LaneOutcome earlier = planWithServices(input, profile, [](M5RunServices& services) {
        services.methods = {make_shared<PatternPlanner>(), make_shared<BlockPlanner>()};
    });
    REQUIRE(earlier.methods.size() == 2);
    CHECK(earlier.methods[1].ran);

    // Now A spends the whole allowance: B is not started.
    FakeClock clock;
    M5RunServices services(clock);
    services.allowanceSeconds = 10.0;
    services.methods = {make_shared<PatternPlanner>(), make_shared<BlockPlanner>()};
    services.beforeMethod = [&](MethodId method) {
        if (method == MethodId::PatternRepeat) clock.advance(10.0);
    };
    const M5LaneOutcome notStarted = planLaneDate(input, profile, physicalSettings(), services);
    REQUIRE(notStarted.methods.size() == 2);
    const string text = renderAscii(buildReportData(notStarted, input, &profile, {}));
    CHECK(text.find("method B (rolling blocks): not run:") != string::npos);
    CHECK(text.find("(run)") == string::npos);
    CHECK_FALSE(notStarted.methods[1].ran);
    CHECK(notStarted.methods[1].unitLoads == 0.0);
    CHECK(notStarted.methods[1].trucks == 0);
    // B's earlier figures are not reused: no method B line carries the earlier 75.00 unit loads.
    const size_t methodB = text.find("method B (rolling blocks)");
    REQUIRE(methodB != string::npos);
    const size_t methodBEnd = text.find('\n', methodB);
    CHECK(text.substr(methodB, methodBEnd - methodB).find("75") == string::npos);

    // B failed its own final validation: its status says so, not "selected" and not "not started".
    const M5LaneDateInput tall = laneWith(fx30Units("u", 31), 1);
    FakeClock failedClock;
    M5RunServices failing(failedClock);
    failing.methods = {make_shared<PatternPlanner>(), make_shared<m5Stubs::FixedMethod>(MethodId::RollingBlocks, 1, 31)};
    const M5LaneOutcome failed = planLaneDate(tall, profile, physicalSettings(), failing);
    const string failedText = renderAscii(buildReportData(failed, tall, &profile, {}));
    CHECK(failedText.find("(run)") == string::npos);
    CHECK(failedText.find("method B (rolling blocks): ") != string::npos);
    m5Evidence::note("expected", {"B not started after A used the allowance: reported not run, no old B figures", "no (run) placeholder anywhere", "a B that cannot be validated is shown with its own status"});
}

TEST_CASE("X-REPORT-09/synthetic: ids with non-ASCII, quotes, commas, newlines and very long text leave the report ASCII and its rows intact" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("u", 3);
    const string hostile = "gr\xc3\xbcppe, \"quoted\"\nnext\ttab " + string(600, 'x');
    for (Atom& atom : atoms) atom.group = hostile;
    atoms[1].group = "second, group";
    const M5LaneDateInput input = laneWith(atoms, 1);
    const M5PolicyProfile profile = syntheticProfile();
    const M5LaneOutcome outcome = plan(input, profile);
    const string text = renderAscii(buildReportData(outcome, input, &profile, {"SYNTHETIC", 20, "run, \"x\"\n\xe2\x82\xac"}));
    CHECK(all_of(text.begin(), text.end(), [](char character) {
        const auto code = static_cast<unsigned char>(character);
        return character == '\n' || (code >= 0x20 && code <= 0x7e);
    }));
    // Rows stay intact: between the lane header and the method section there is one line per truck (it starts
    // with the truck number), none broken by a newline or quote inside an id.
    const M5ReportData data = buildReportData(outcome, input, &profile, {});
    const size_t laneStart = text.find("3B Lane\n"), laneEnd = text.find("3C Method evidence\n");
    REQUIRE(laneStart != string::npos);
    REQUIRE(laneEnd != string::npos);
    size_t rowLines = 0;
    istringstream lane(text.substr(laneStart, laneEnd - laneStart));
    for (string line; getline(lane, line);) {
        if (!line.empty() && line.front() >= '0' && line.front() <= '9') ++rowLines;
    }
    CHECK(rowLines == data.trucks.size());
    CHECK(text.find("3E Summary\n") != string::npos);
    m5Evidence::note("expected", {"every byte 0x20..0x7e or newline", "one table row per truck", "sections 3A to 3E present"});
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

TEST_CASE("U-RP-02/synthetic: the lane report prints lane, groups, shared reservations, checked cargo, labels, rear, bucket, leftovers and penalty" * doctest::test_suite("m5")) {
    // Two groups share three reserved trucks: A's 40 and B's 25 FX-30 (each truck holds 30).
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 40, "A"), fx30Units("b", 25, "B")), 3);
    M5PolicyProfile profile = syntheticProfile();
    profile.penaltyPerUnusedTruck = 5.0;
    const M5LaneOutcome outcome = plan(input, profile);
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    REQUIRE(outcome.finalValidationPassed);
    const M5ReportData data = buildReportData(outcome, input, &profile, {"PRODUCTION", 20, "fixture"});
    const string text = renderAscii(data);
    INFO(text);
    const size_t lane = text.find("3B Lane\n");
    REQUIRE(lane != string::npos);
    CHECK(text.find("M5 large lane 2028 -> 2574 TL, 2026-08-07\n") == 0);
    CHECK(text.find("groups sharing the reservation: A, B\n", lane) != string::npos);
    CHECK(text.find("reserved: 3\n", lane) != string::npos);
    CHECK(text.find("trucks loaded: 3\n", lane) != string::npos);
    CHECK(text.find("unused reservations: 0, penalty 5.00 per unused truck = 0.00\n", lane) != string::npos);
    CHECK(text.find("truck group stacks unit loads can-go weight % rear in fullness reasons", lane) != string::npos);
    // One row per finally checked truck, carrying its own label, reasons, rear inches and bucket.
    REQUIRE(data.trucks.size() == outcome.selected->trucks.size());
    REQUIRE(outcome.fullness.size() == data.trucks.size());
    for (size_t truck = 0; truck < data.trucks.size(); ++truck) {
        const TruckPlan& planned = outcome.selected->trucks[truck];
        const FullnessLabel* label = nullptr;
        for (const FullnessLabel& candidate : outcome.fullness) label = candidate.truck == truck ? &candidate : label;
        REQUIRE(label);
        CHECK(data.trucks[truck].group == planned.group);
        CHECK(data.trucks[truck].stacks == planned.atoms.size());
        CHECK(data.trucks[truck].rearInches == label->rearInches);
        CHECK(data.trucks[truck].fullness == fullnessStateName(label->state));
        CHECK(data.trucks[truck].bucket == fullnessBucketName(*label->bucket));
        CHECK(data.trucks[truck].reasons != "");
    }
    CHECK(data.unitLoadsShipped == 65.0);
    CHECK(text.find("left: ", lane) == string::npos);
    // Leftovers are printed with their cause: 90 on 2 reserved leave 30 over the reservation.
    const M5LaneDateInput over = laneWith(fx30Units("u", 90), 2);
    const string overText = renderAscii(buildReportData(plan(over, profile), over, &profile, {"PRODUCTION", 20, "fixture"}));
    CHECK(overText.find("left: over the reservation: 30.00 unit loads\n") != string::npos);
    CHECK(overText.find("unused reservations: 0, penalty 5.00 per unused truck = 0.00\n") != string::npos);
    // AcceptanceWithheld appears only with the optional predicate ON.
    CHECK(text.find("withheld by minimum-load policy") == string::npos);
    const M5LaneDateInput fortyNine = laneWith(fx30Units("u", 49), 2);
    const M5PolicyProfile policy = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    const string withheld = renderAscii(buildReportData(plan(fortyNine, policy), fortyNine, &policy, {"SYNTHETIC", 20, "fixture"}));
    CHECK(withheld.find("left: withheld by minimum-load policy: 19.00 unit loads\n") != string::npos);
    // A truck count above its bound is labelled unproven, never presented as certain.
    const M5LaneDateInput gap = gapLane(30);
    const string estimate = renderAscii(buildReportData(plan(gap, profile), gap, &profile, {"PRODUCTION", 20, "fixture"}));
    CHECK(estimate.find("count evidence: incumbent 3, bound 2 (") != string::npos);
    CHECK(estimate.find("), unproven\n") != string::npos);
    m5Evidence::note("expected", {"lane, groups A and B, reserved 3, loaded 3, penalty line", "one labelled row per checked truck",
                                  "all 65 ship; 90 on 2 prints 30 left over the reservation", "withheld only with policy ON", "count 3 over bound 2 labelled unproven"});
}

TEST_CASE("U-RP-03/synthetic: method evidence shows the known optimum, the full check, bound and proof apart, and Q6's real trigger" * doctest::test_suite("m5")) {
    // FX-GAP 30: known optimum 3 trucks (ten 60 x 60 in to a truck), floor-area bound 2. A gives 3 > 2, so B runs (Q6: F > B).
    const M5LaneDateInput gap = gapLane(30);
    const M5LaneOutcome escalated = plan(gap, syntheticProfile());
    REQUIRE(escalated.selected);
    CHECK(static_cast<long long>(escalated.selected->trucks.size()) == gapLaneTrucks(30));
    const string text = renderAscii(buildReportData(escalated, gap, nullptr, {}));
    INFO(text);
    const size_t box = text.find("3C Method evidence\n");
    REQUIRE(box != string::npos);
    CHECK(text.find("method A (repeated patterns): ran, validated", box) != string::npos);
    CHECK(text.find("method B (rolling blocks): ran, validated", box) != string::npos);
    CHECK(text.find("quick count vs bound: 1 truck, 50.0% above the bound of 2\n", box) != string::npos);
    CHECK(text.find("careful method: run: ", box) != string::npos);
    // Count evidence (incumbent, bound and its model, count proof) is printed apart from the service proof.
    CHECK(text.find("count evidence: incumbent 3, bound 2 (capacityOnly", box) != string::npos);
    CHECK(text.find("service proof: ", box) != string::npos);
    CHECK(text.find("Q6") == string::npos);
    CHECK(text.find("pending") == string::npos);
    // Count equality: A's 2 trucks equal the bound of 2, so B does not run; the stop reason says so. The
    // service proof keeps its own scope: count equality is not a certificate of the whole allocation.
    const M5LaneDateInput equal = laneWith(fx30Units("u", 60), 3);
    const M5LaneOutcome stopped = plan(equal, syntheticProfile());
    const string equalText = renderAscii(buildReportData(stopped, equal, nullptr, {}));
    CHECK(equalText.find("method B (rolling blocks): not run: A's 2 trucks equal the valid bound of 2") != string::npos);
    CHECK(equalText.find("quick count vs bound: 0 trucks, 0.0% above the bound of 2\n") != string::npos);
    CHECK(equalText.find("careful method: not escalated: ") != string::npos);
    const size_t proof = equalText.find("service proof: ");
    REQUIRE(proof != string::npos);
    const string proofLine = equalText.substr(proof, equalText.find('\n', proof) - proof);
    CHECK(proofLine.find("service: ") != string::npos);
    CHECK(proofLine.find("count") == string::npos);
    m5Evidence::note("expected", {"FX-GAP 30: optimum 3, bound 2, gap 1 truck 50.0%, B ran", "A and B validated",
                                  "count evidence apart from service proof", "60 on 3: equal, B not run, stop reason printed",
                                  "no pending Q6"});
}

TEST_CASE("U-RP-05/synthetic: the summary prints elapsed, dispatched trucks, the average or N/A, and the allowance apart; no per-load cap" * doctest::test_suite("m5")) {
    // One truck on two reserved; the fake clock spends 15 s in method A: 15 s for the loaded truck is above
    // secondsPerLoad (10) yet inside the 20 s lane-date allowance, and that is no failure.
    const M5LaneDateInput input = laneWith(fx30Units("u", 30), 2);
    FakeClock clock;
    M5RunServices services(clock);
    services.afterMethod = [&](MethodId method) {
        if (method == MethodId::PatternRepeat) clock.advance(15.0);
    };
    const M5LaneOutcome outcome = planLaneDate(input, syntheticProfile(), physicalSettings(), services);
    REQUIRE(outcome.selected);
    CHECK(outcome.kind == OutcomeKind::CompletedWithDispatch);
    CHECK(outcome.allowanceSeconds == 20.0);
    const M5PolicyProfile profile = syntheticProfile();
    const M5ReportData data = buildReportData(outcome, input, &profile, {"PRODUCTION", 20, "fixture"});
    const string text = renderAscii(data);
    INFO(text);
    const size_t summary = text.find("3E Summary\n");
    REQUIRE(summary != string::npos);
    CHECK(text.find("lane-date allowance: 20.00 s (2 reserved)\n") != string::npos);
    CHECK(text.find("time: 15.000 s of 20.00 s allowed\n", summary) != string::npos);
    CHECK(text.find("seconds per loaded truck: 15.000\n", summary) != string::npos);
    CHECK(text.find("FULL trucks: 1 of 1", summary) != string::npos);
    CHECK(data.findings.empty());
    // No truck dispatched: the average is N/A, not 0.
    const M5LaneDateInput none = laneWith({unitOf("tall", "A", 48, 40, 120, 500)}, 2);
    const string empty = renderAscii(buildReportData(plan(none, profile), none, &profile, {}));
    CHECK(empty.find("seconds per loaded truck: N/A\n") != string::npos);
    // The production report carries no benchmark rows; a joined-dates run is labelled BENCHMARK apart.
    CHECK(text.find("rows: none in a production run: benchmark runs are labelled and kept apart\n") != string::npos);
    const vector<M5LaneDateInput> dates{laneWith(fx30Units("d", 30), 1), laneWith(fx30Units("e", 30), 1)};
    const FunctionClock still(nullptr);
    const LinkedDateBenchmark benchmark = benchmarkLinkedDates(dates, profile, physicalSettings(), still);
    CHECK(benchmark.text.find("scope: BENCHMARK (labelled, not production; nothing committed)") != string::npos);
    m5Evidence::note("expected", {"time 15.000 s of 20.00 s, average 15.000 s per loaded truck above 10 s/load: no failure",
                                  "no truck: N/A", "benchmark labelled apart"});
}

TEST_CASE("X-E2E-08/synthetic a comparison mixing bases is flagged: 1,440 - 1,308 is 132, not 154; a valid plan does not make it right" * doctest::test_suite("m5")) {
    // A valid plan: the figures are about the comparison, not the plan.
    const M5LaneDateInput input = laneWith(fx30Units("u", 60), 2);
    const M5LaneOutcome valid = plan(input, syntheticProfile());
    requireInvariants(valid, input);
    CHECK(valid.finalValidationPassed);
    CHECK(shippedCount(valid) == 60);
    REQUIRE(unusedComparisonMismatch(1440, 1308, 154).has_value());
    CHECK(unusedComparisonMismatch(1440, 1308, 154)->find("132") != string::npos);
    CHECK(unusedComparisonMismatch(1440, 1308, 154)->find("154") != string::npos);
    CHECK(valid.finalValidationPassed);
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

TEST_CASE("X-E2E-03/pullOff X-E2E-03/pullOn patterns with pull-forward and unavailable stock" * doctest::test_suite("m5")) {
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
    requireInvariants(withoutPull, off);
    CHECK(withoutPull.selected->trucks.size() == 3);
    CHECK(shippedCount(withoutPull) == 30);
    for (const string& id : shippedIds(withoutPull, off)) CHECK(id[0] == 'a');
    for (const TruckPlan& truck : withoutPull.selected->trucks) {
        CHECK(truck.atoms.size() == 10);
        CHECK(truck.layout.positions > 0);
    }
    const M5LaneDateInput on = laneWith(atoms, 8, kDefaultTrailer, true);
    const M5LaneOutcome withPull = plan(on, policy);
    requireInvariants(withPull, on);
    CHECK(withPull.selected->trucks.size() == 5);
    CHECK(shippedCount(withPull) == 50);
    CHECK(withPull.finalValidationPassed);
    for (const string& id : shippedIds(withPull, on)) CHECK((id[0] == 'a' || id[0] == 'b'));
    for (const string& id : shippedIds(withPull, on)) CHECK((id[0] != 'c' && id[0] != 'e'));
    for (const TruckPlan& truck : withPull.selected->trucks) {
        CHECK(truck.atoms.size() == 10);
        CHECK(truck.layout.positions > 0);
        double weight = 0.0, cube = 0.0;
        for (const size_t atom : truck.atoms) {
            weight += on.atoms[atom].weightLb;
            cube += on.atoms[atom].floor.cubeCubicIn;
        }
        CHECK(weight == 45000.0);
        CHECK(cube / 1728.0 == doctest::Approx(3937.5));
    }
}
