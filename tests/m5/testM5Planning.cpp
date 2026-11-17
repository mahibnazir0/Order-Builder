// M5 planning through its direct seam (v7 0.4): small fixtures call planLaneDate, never M4 routing.
#include "doctest.h"
#include "m5Commit.hpp"
#include "m5Ledger.hpp"
#include "m5Methods.hpp"
#include "m5Repair.hpp"
#include "m5Validation.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Fixtures.hpp"
#include "support/m5Oracles.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

using namespace std;
using namespace ob::m5;
using namespace m5Fixtures;

namespace {

void requireInvariants(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    const vector<string> violations = m5Oracles::invariantViolations(outcome, input, physicalSettings().floor);
    for (const string& violation : violations) INFO(violation);
    REQUIRE(violations.empty());
}

vector<Atom> concat(vector<Atom> left, const vector<Atom>& right) {
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

size_t countCause(const M5LaneOutcome& outcome, LeftOverCause cause) {
    return static_cast<size_t>(count_if(outcome.leftOver.begin(), outcome.leftOver.end(),
                                        [&](const LeftOver& left) { return left.cause == cause; }));
}

CandidatePlan oneTruck(const M5LaneDateInput& input, size_t carried) {
    CandidatePlan plan;
    TruckPlan truck;
    truck.group = input.atoms.front().group;
    for (size_t atom = 0; atom < carried; ++atom) truck.atoms.push_back(atom);
    plan.trucks.push_back(truck);
    for (size_t atom = carried; atom < input.atoms.size(); ++atom) plan.leftOver.push_back({atom, LeftOverCause::OverReservation});
    storeRows(plan, input);
    return plan;
}

}  // namespace

TEST_CASE("U-PB-01 goods for exactly N full trucks give N copies, no remainder" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 60), 2);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 2);
    CHECK(shippedCount(outcome) == 60);
    CHECK(outcome.kind == OutcomeKind::CompletedWithDispatch);
}

TEST_CASE("U-PB-02 N trucks plus a part truck: the part goes to the remainder once" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 65), 3);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 3);
    CHECK(shippedCount(outcome) == 65);
}

TEST_CASE("U-PB-03 less than one truck of goods forms no pattern but still ships" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 20), 2);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 1);
    CHECK(shippedCount(outcome) == 20);
}

TEST_CASE("X-PAT-01 X-PAT-03 X-PAT-04 pattern copies by stock and slots, checked" * doctest::test_suite("m5")) {
    const vector<pair<long long, long long>> stockAndCopies{{0, 0}, {4, 0}, {5, 1}, {15, 3}, {17, 3}};
    for (const auto& [stock, copies] : stockAndCopies) CHECK(patternCopies(stock, 5, 10) == copies);
    CHECK(patternCopies(17, 5, 2) == 2);
    CHECK(patternCopies(17, 0, 10) == 0);
    CHECK(patternCopies(1LL << 31, 64, 1LL << 40) == 33'554'432);
    CHECK_THROWS_AS(patternCopies(numeric_limits<long long>::max(), 64, 1), out_of_range);
    CHECK_THROWS_AS(patternCopies(-1, 5, 1), invalid_argument);
}

TEST_CASE("X-PAT-11 cube never stands in for a floor: one of two 60 x 60 in a 100 x 100" * doctest::test_suite("m5")) {
    const TrailerCapability trailer{100.0, 100.0, 100.0, 45000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput input = laneWith({unitOf("a", "A", 60, 60, 100, 100), unitOf("b", "A", 60, 60, 100, 100)}, 1, trailer);
    CHECK_FALSE(checkTruck(input, {0, 1}, trailer, physicalSettings()).fits);
    CHECK(checkTruck(input, {0}, trailer, physicalSettings()).fits);
}

TEST_CASE("X-PAT-12 a load turned to fit a narrow trailer fits" * doctest::test_suite("m5")) {
    const TrailerCapability trailer{100.0, 60.0, 100.0, 45000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput input = laneWith({unitOf("a", "A", 100, 60, 100, 100)}, 1, trailer);
    CHECK(checkTruck(input, {0}, trailer, physicalSettings()).fits);
}

TEST_CASE("X-PAT-13 limits are exact: at the limit fits, a step over does not" * doctest::test_suite("m5")) {
    const TrailerCapability trailer = kDefaultTrailer;
    M5LaneDateInput input = laneWith({unitOf("at", "A", 630, 100, 108, 45000), unitOf("over", "A", 630.001, 100, 108, 45000),
                                      unitOf("heavy", "A", 600, 100, 108, 45000.001), unitOf("tall", "A", 600, 100, 108.001, 10)},
                                     1, trailer);
    CHECK(checkTruck(input, {0}, trailer, physicalSettings()).fits);
    CHECK_FALSE(checkTruck(input, {1}, trailer, physicalSettings()).fits);
    CHECK_FALSE(checkTruck(input, {2}, trailer, physicalSettings()).fits);
    CHECK_FALSE(checkTruck(input, {3}, trailer, physicalSettings()).fits);
}

TEST_CASE("X-PAT-14 planning by floor never turns the weight limit off" * doctest::test_suite("m5")) {
    const TrailerCapability trailer{630.0, 100.0, 108.0, 10000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput input = laneWith({unitOf("a", "A", 48, 40, 50, 6000), unitOf("b", "A", 48, 40, 50, 6000)}, 2, trailer);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 2);
}

TEST_CASE("U-PB-09 U-PB-10 a unit taller or heavier than the trailer is reported, never placed" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith({unitOf("tall", "A", 48, 40, 120, 500), unitOf("heavy", "A", 48, 40, 50, 46000),
                                            unitOf("fine", "A", 48, 40, 50, 500)},
                                           2);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(countCause(outcome, LeftOverCause::Unfittable) == 2);
    CHECK(shippedIds(outcome, input) == set<string>{"fine"});
}

TEST_CASE("R-2 one reserved truck: group B's 20 P0 beat A's 1 P0 + 29 P11" * doctest::test_suite("m5")) {
    vector<Atom> atoms{fx30("a-p0", "A", 0)};
    atoms = concat(atoms, fx30Units("a-p11-", 29, "A", 11));
    atoms = concat(atoms, fx30Units("b-p0-", 20, "B", 0));
    const M5LaneDateInput input = laneWith(atoms, 1);
    for (const M5PolicyProfile& profile : {syntheticProfile(), withAcceptance(syntheticProfile(), 40000.0, 2400.0)}) {
        const M5LaneOutcome outcome = plan(input, profile);
        requireInvariants(outcome, input);
        REQUIRE(outcome.selected);
        CHECK(outcome.selected->trucks.size() == 1);
        CHECK(shippedOfGroup(outcome, input, "B") == 20);
        CHECK(shippedOfGroup(outcome, input, "A") == 0);
    }
}

TEST_CASE("R-4a R-4b R-4c U-RS-06 U-RS-07 X-DATE-08 overdue P11 against current P0, by past-due order" * doctest::test_suite("m5")) {
    const vector<Atom> atoms = concat(fx30Units("old-", 30, "A", 11, kDayBefore, kDayBefore), fx30Units("now-", 30, "A", 0));
    M5LaneDateInput input = laneWith(atoms, 1);
    auto shippedPrefix = [&](const M5LaneOutcome& outcome) {
        set<string> prefixes;
        for (const string& id : shippedIds(outcome, input)) prefixes.insert(id.substr(0, id.find('-')));
        return prefixes;
    };
    input.pastDueOrder = ob::PastDueOrder::AgeFirst;
    CHECK(shippedPrefix(plan(input, syntheticProfile())) == set<string>{"old"});
    input.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    CHECK(shippedPrefix(plan(input, syntheticProfile())) == set<string>{"now"});
    const M5LaneDateInput defaulted = laneWith(atoms, 1);
    const M5LaneOutcome outcome = plan(defaulted, syntheticProfile());
    set<string> prefixes;
    for (const string& id : shippedIds(outcome, defaulted)) prefixes.insert(id.substr(0, id.find('-')));
    CHECK(prefixes == set<string>{"old"});
}

TEST_CASE("X-RES-06 two reserved, priority first: C and B ship, A is left" * doctest::test_suite("m5")) {
    vector<Atom> atoms{fx30("a0", "A", 0)};
    atoms = concat(atoms, fx30Units("a11-", 29, "A", 11));
    atoms = concat(atoms, fx30Units("b0-", 20, "B", 0));
    atoms = concat(atoms, fx30Units("b11-", 10, "B", 11));
    atoms = concat(atoms, fx30Units("c0-", 30, "C", 0));
    M5LaneDateInput input = laneWith(atoms, 2);
    input.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedOfGroup(outcome, input, "A") == 0);
    CHECK(shippedOfGroup(outcome, input, "B") == 30);
    CHECK(shippedOfGroup(outcome, input, "C") == 30);
}

TEST_CASE("X-E2E-07 choosing across groups is independent of input order" * doctest::test_suite("m5")) {
    vector<Atom> atoms{fx30("a0", "A", 0)};
    atoms = concat(atoms, fx30Units("a11-", 29, "A", 11));
    atoms = concat(atoms, fx30Units("b0-", 20, "B", 0));
    atoms = concat(atoms, fx30Units("b11-", 10, "B", 11));
    atoms = concat(atoms, fx30Units("c0-", 19, "C", 0));
    atoms = concat(atoms, fx30Units("c11-", 11, "C", 11));
    const M5LaneDateInput input = laneWith(atoms, 2);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(shippedOfGroup(outcome, input, "B") == 30);
    CHECK(shippedOfGroup(outcome, input, "C") == 30);

    vector<Atom> reversed(atoms.rbegin(), atoms.rend());
    const M5LaneDateInput permuted = laneWith(reversed, 2);
    CHECK(shippedIds(plan(permuted, syntheticProfile()), permuted) == shippedIds(outcome, input));
}

TEST_CASE("X-RUN-05 an early low-priority block cannot starve urgent goods" * doctest::test_suite("m5")) {
    const vector<Atom> atoms = concat(fx30Units("a", 600, "A", 11), fx30Units("b", 60, "B", 0));
    M5LaneDateInput input = laneWith(atoms, 21);
    input.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    M5PolicyProfile profile = syntheticProfile();
    profile.method = MethodPolicy::BlocksOnly;
    const M5LaneOutcome outcome = plan(input, profile);
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(shippedOfGroup(outcome, input, "B") == 60);
    CHECK(shippedOfGroup(outcome, input, "A") == 19 * 30);
    CHECK(outcome.selected->trucks.size() == 21);
}

TEST_CASE("X-RES-01 X-ROUTE-02 groups share one pool: never two pools of the same size" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 90, "A"), fx30Units("b", 60, "B")), 2);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 2);
    CHECK(shippedCount(outcome) == 60);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 90);
}

TEST_CASE("U-RS-04 X-STATE-05 the same input plans the same way every time" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 40, "A", 3), fx30Units("b", 40, "B", 3)), 2);
    const M5LaneOutcome first = plan(input, syntheticProfile());
    const M5LaneOutcome second = plan(input, syntheticProfile());
    REQUIRE(first.selected);
    REQUIRE(second.selected);
    CHECK(first.selected->trucks.size() == second.selected->trucks.size());
    for (size_t truck = 0; truck < first.selected->trucks.size(); ++truck) {
        CHECK(first.selected->trucks[truck].atoms == second.selected->trucks[truck].atoms);
    }
}

TEST_CASE("X-STATE-01 A and B give the same result in either order or alone" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 75), 3);
    M5PolicyProfile patternOnly = syntheticProfile(), blocksOnly = syntheticProfile();
    patternOnly.method = MethodPolicy::PatternOnly;
    blocksOnly.method = MethodPolicy::BlocksOnly;
    const set<string> alone = shippedIds(plan(input, patternOnly), input);
    CHECK(shippedIds(plan(input, blocksOnly), input) == alone);
    CHECK(shippedIds(plan(input, syntheticProfile()), input) == alone);
    CHECK(alone.size() == 75);
}

TEST_CASE("X-REB-09 rebalance both sides of a strict acceptance boundary" * doctest::test_suite("m5")) {
    const M5PolicyProfile policy = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    SUBCASE("fifty: rebalance 25 + 25") {
        const M5LaneDateInput input = laneWith(fx30Units("u", 50), 2);
        const M5LaneOutcome outcome = plan(input, policy);
        requireInvariants(outcome, input);
        REQUIRE(outcome.selected);
        REQUIRE(outcome.selected->trucks.size() == 2);
        CHECK(outcome.selected->trucks[0].atoms.size() == 25);
        CHECK(outcome.selected->trucks[1].atoms.size() == 25);
    }
    SUBCASE("fortyNine: one truck of 30, 19 left, one slot unused") {
        const M5LaneDateInput input = laneWith(fx30Units("u", 49), 2);
        const M5LaneOutcome outcome = plan(input, policy);
        requireInvariants(outcome, input);
        REQUIRE(outcome.selected);
        REQUIRE(outcome.selected->trucks.size() == 1);
        CHECK(outcome.selected->trucks[0].atoms.size() == 30);
        CHECK(countCause(outcome, LeftOverCause::AcceptanceWithheld) == 19);
    }
    SUBCASE("productionProfile: all 49 ship in two legal trucks") {
        const M5LaneDateInput input = laneWith(fx30Units("u", 49), 2);
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        requireInvariants(outcome, input);
        CHECK(shippedCount(outcome) == 49);
        CHECK(outcome.selected->trucks.size() == 2);
    }
}

TEST_CASE("U-AC-02 U-AC-03 productionProfile: no truck is withheld for minimum load" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 5, "A"), fx30Units("b", 5, "B")), 2);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(shippedCount(outcome) == 10);
    CHECK(countCause(outcome, LeftOverCause::AcceptanceWithheld) == 0);
    CHECK(outcome.fullness.size() == 2);
    const M5LaneOutcome withPolicy = plan(input, withAcceptance(syntheticProfile(), 40000.0, 3000.0));
    CHECK(shippedCount(withPolicy) == 0);
    CHECK(countCause(withPolicy, LeftOverCause::AcceptanceWithheld) == 10);
}

TEST_CASE("U-AC-06 acceptance never overrides a hard constraint" * doctest::test_suite("m5")) {
    const M5PolicyProfile policy = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    const M5LaneDateInput control = laneWith(fx30Units("u", 30), 1);
    const CandidatePlan full = oneTruck(control, 30);
    CHECK(validateFinal(full, *buildSnapshot(control).snapshot, policy, physicalSettings()).allPassed());

    auto rejected = [&](const M5LaneDateInput& input, const CandidatePlan& candidate, const string& section) {
        const ValidationReport report = validateFinal(candidate, *buildSnapshot(input).snapshot, policy, physicalSettings());
        CHECK_FALSE(report.allPassed());
        CHECK(any_of(report.findings.begin(), report.findings.end(), [&](const ValidationFinding& finding) { return finding.section == section; }));
    };
    rejected(laneWith(fx30Units("u", 30), 1, {630.0, 100.0, 108.0, 40000.0, kFixtureUnitLoadCap}), full, "physical");
    rejected(laneWith(fx30Units("u", 30), 1, {599.0, 100.0, 108.0, 45000.0, kFixtureUnitLoadCap}), full, "physical");
    M5LaneDateInput unavailable = laneWith(fx30Units("u", 30), 1);
    unavailable.atoms[3].components[0].availableDate = dayAfter(1);
    rejected(unavailable, full, "eligibility");
    M5LaneDateInput dueLater = laneWith(fx30Units("u", 30), 1);
    dueLater.atoms[4].components[0].dueDate = dayAfter(2);
    rejected(dueLater, full, "eligibility");
    M5LaneDateInput mixed = laneWith(fx30Units("u", 30), 1);
    mixed.atoms[5].group = "B";
    rejected(mixed, full, "groups");
    rejected(laneWith(fx30Units("u", 30), 0), full, "reservations");
}

TEST_CASE("U-AC-07 a repair clears stale acceptance; validation recomputes 3,000 cu ft exactly" * doctest::test_suite("m5")) {
    const M5PolicyProfile policy = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    const M5LaneDateInput input = laneWith(fx30Units("u", 25), 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan candidate = oneTruck(input, 25);
    candidate.validation = validateFinal(candidate, snapshot, policy, physicalSettings());
    REQUIRE(candidate.validation->allPassed());
    const uint64_t before = candidate.revision;
    CandidateEditor(candidate).removeToLeftOver(0, 24, LeftOverCause::OverReservation);
    CHECK(candidate.revision == before + 1);
    CHECK_FALSE(candidate.validation.has_value());
    // A stale report restored by a fault cannot authorise the changed truck.
    string error;
    CandidatePlan stale = candidate;
    stale.validation = validateFinal(oneTruck(input, 25), snapshot, policy, physicalSettings());
    stale.validation->revision = before;
    CHECK_FALSE(buildDelta(stale, snapshot, policy, &error).has_value());
    const ValidationReport recomputed = validateFinal(candidate, snapshot, policy, physicalSettings());
    CHECK(recomputed.acceptance == SectionStatus::Fail);
    CHECK(recomputed.conservation == SectionStatus::Pass);
}

TEST_CASE("U-AC-08 acceptance is recomputed when the profile changes: A, B, A" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 20), 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const CandidatePlan candidate = oneTruck(input, 20);
    const M5PolicyProfile a = withAcceptance(syntheticProfile(), 40000.0, 2400.0);
    const M5PolicyProfile b = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    CHECK(validateFinal(candidate, snapshot, a, physicalSettings()).allPassed());
    CHECK_FALSE(validateFinal(candidate, snapshot, b, physicalSettings()).allPassed());
    CHECK(validateFinal(candidate, snapshot, a, physicalSettings()).allPassed());
}

TEST_CASE("U-AC-08 productionProfile: rear limits 14, 30, 14 give NOT FULL, FULL, NOT FULL" * doctest::test_suite("m5")) {
    vector<Atom> units;
    for (int unit = 0; unit < 31; ++unit) units.push_back(unitOf("t" + to_string(100 + unit), "A", 100, 20, 108, 300));
    const M5LaneDateInput input = laneWith(units, 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const CandidatePlan candidate = oneTruck(input, 30);
    vector<FullnessState> states;
    for (const double limit : {14.0, 30.0, 14.0}) {
        M5PolicyProfile profile = syntheticProfile();
        profile.fullness.rearInchLimit = limit;
        const vector<FullnessLabel> labels =
            evaluateFullness(candidate, snapshot, remainingAfterSelected(snapshot, candidate), profile, physicalSettings());
        states.push_back(labels.front().state);
    }
    CHECK(states == vector<FullnessState>{FullnessState::NotFull, FullnessState::Full, FullnessState::NotFull});
}

TEST_CASE("U-FL-01 29 then 30 FX-30 units: NOT FULL with goods left, FULL by weight with none" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 30), 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const CandidatePlan partial = oneTruck(input, 29);
    const FullnessLabel notFull =
        evaluateFullness(partial, snapshot, remainingAfterSelected(snapshot, partial), syntheticProfile(), physicalSettings()).front();
    CHECK(notFull.state == FullnessState::NotFull);
    CHECK(notFull.rearInches == doctest::Approx(50.0));
    CHECK(notFull.weightPercent == doctest::Approx(43500.0 / 450.0));
    CHECK(notFull.fitStatus == RemainingFitStatus::Fits);
    CHECK(notFull.bucket == FullnessBucket::GoodsRemain);

    const CandidatePlan full = oneTruck(input, 30);
    const FullnessLabel label =
        evaluateFullness(full, snapshot, remainingAfterSelected(snapshot, full), syntheticProfile(), physicalSettings()).front();
    CHECK(label.state == FullnessState::Full);
    CHECK(label.byWeight);
    CHECK_FALSE(label.byFloorLimit);
    CHECK(label.rearInches == doctest::Approx(30.0));
    CHECK(label.bucket == FullnessBucket::NoGoodsRemain);
}

TEST_CASE("U-FL-02 31 light TLD units: 30 NOT FULL, 31 FULL by rear inches" * doctest::test_suite("m5")) {
    vector<Atom> units;
    for (int unit = 0; unit < 31; ++unit) units.push_back(unitOf("t" + to_string(100 + unit), "A", 100, 20, 108, 300));
    const M5LaneDateInput input = laneWith(units, 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const CandidatePlan thirty = oneTruck(input, 30);
    const FullnessLabel first =
        evaluateFullness(thirty, snapshot, remainingAfterSelected(snapshot, thirty), syntheticProfile(), physicalSettings()).front();
    CHECK(first.state == FullnessState::NotFull);
    CHECK(first.rearInches == doctest::Approx(30.0));
    const CandidatePlan all = oneTruck(input, 31);
    const FullnessLabel second =
        evaluateFullness(all, snapshot, remainingAfterSelected(snapshot, all), syntheticProfile(), physicalSettings()).front();
    CHECK(second.state == FullnessState::Full);
    CHECK(second.byRearInches);
    CHECK_FALSE(second.byWeight);
    CHECK(second.rearInches == doctest::Approx(10.0));
}

TEST_CASE("U-FL-04 X-E2E-09 floor-limited needs a remaining display that provably does not fit" * doctest::test_suite("m5")) {
    vector<Atom> displays;
    for (int unit = 0; unit < 31; ++unit) displays.push_back(display("d" + to_string(100 + unit)));
    const M5LaneDateInput input = laneWith(displays, 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const CandidatePlan thirty = oneTruck(input, 30);
    const FullnessLabel label =
        evaluateFullness(thirty, snapshot, remainingAfterSelected(snapshot, thirty), syntheticProfile(), physicalSettings()).front();
    CHECK(label.rearInches == doctest::Approx(30.0));
    CHECK_FALSE(label.byWeight);
    CHECK_FALSE(label.byRearInches);
    CHECK(label.fitStatus == RemainingFitStatus::ProvenNoFit);
    CHECK(label.byFloorLimit);
    CHECK(label.state == FullnessState::Full);

    // Only the 30: stock exhaustion alone proves nothing about the floor.
    const M5LaneDateInput exact = laneWith(vector<Atom>(displays.begin(), displays.begin() + 30), 1);
    const DemandSnapshot exactSnapshot = *buildSnapshot(exact).snapshot;
    const FullnessLabel exhausted = evaluateFullness(thirty, exactSnapshot, remainingAfterSelected(exactSnapshot, thirty),
                                                     syntheticProfile(), physicalSettings())
                                        .front();
    CHECK_FALSE(exhausted.byFloorLimit);
    CHECK(exhausted.state == FullnessState::NotFull);
    CHECK(exhausted.bucket == FullnessBucket::NoGoodsRemain);
}

TEST_CASE("U-FL-08 buckets: eligible residual, none, or rule-excluded only" * doctest::test_suite("m5")) {
    M5LaneDateInput input = laneWith(fx30Units("u", 10), 1);
    input.excluded.push_back({"later", "A", 5.0, "due after the pull-forward window"});
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const CandidatePlan all = oneTruck(input, 10);
    CHECK(evaluateFullness(all, snapshot, remainingAfterSelected(snapshot, all), syntheticProfile(), physicalSettings()).front().bucket
          == FullnessBucket::RuleLimited);
    M5LaneDateInput incomplete = laneWith(fx30Units("u", 10), 1);
    incomplete.sourceScopeComplete = false;
    const DemandSnapshot incompleteSnapshot = *buildSnapshot(incomplete).snapshot;
    CHECK_FALSE(evaluateFullness(all, incompleteSnapshot, remainingAfterSelected(incompleteSnapshot, all), syntheticProfile(),
                                 physicalSettings())
                    .front()
                    .bucket.has_value());
}

TEST_CASE("U-FL-09 an interrupted fit check is Unknown, never a conclusive label" * doctest::test_suite("m5")) {
    vector<Atom> displays;
    for (int unit = 0; unit < 31; ++unit) displays.push_back(display("d" + to_string(100 + unit)));
    const M5LaneDateInput input = laneWith(displays, 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const CandidatePlan twenty = oneTruck(input, 20);
    const FullnessLabel label = evaluateFullness(twenty, snapshot, remainingAfterSelected(snapshot, twenty), syntheticProfile(),
                                                 physicalSettings(), [] { return true; })
                                    .front();
    CHECK(label.fitStatus == RemainingFitStatus::Unknown);
    CHECK(label.state == FullnessState::Undetermined);
}

TEST_CASE("U-CF-01 can-go goods fill only room the goods due by D leave" * doctest::test_suite("m5")) {
    const vector<Atom> atoms = concat(fx30Units("req", 10, "A", 5), fx30Units("cango", 30, "A", 0, dayAfter(3)));
    const M5LaneDateInput input = laneWith(atoms, 1, kDefaultTrailer, true);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shipped.size() == 30);
    for (int unit = 0; unit < 10; ++unit) CHECK(shipped.count("req" + to_string(100 + unit)) == 1);
    CHECK(outcome.requiredService == ServiceLevel::Full);
}

TEST_CASE("U-CF-02 X-DATE-01 can-go window: D+10 may load, D+11 and unavailable goods may not" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("d", 10);
    atoms = concat(atoms, fx30Units("d9-", 10, "A", 0, dayAfter(9)));
    atoms = concat(atoms, fx30Units("d10-", 10, "A", 0, dayAfter(10)));
    atoms = concat(atoms, fx30Units("d11-", 10, "A", 0, dayAfter(11)));
    atoms = concat(atoms, fx30Units("late-", 5, "A", 0, dayAfter(5), dayAfter(1)));
    const M5LaneDateInput input = laneWith(atoms, 2, kDefaultTrailer, true);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedCount(outcome) == 30);
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 15);
}

TEST_CASE("U-CF-04 required goods win scarce reservations across groups" * doctest::test_suite("m5")) {
    const vector<Atom> atoms = concat(fx30Units("req", 20, "A", 11), fx30Units("cango", 30, "B", 0, dayAfter(2)));
    const M5LaneDateInput input = laneWith(atoms, 1, kDefaultTrailer, true);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(shippedOfGroup(outcome, input, "A") == 20);
    CHECK(shippedOfGroup(outcome, input, "B") == 0);
}

TEST_CASE("U-CF-05 an existing reserved date with only can-go goods still ships them" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith({fxBlock("future")}, 1, kDefaultTrailer, true);
    M5LaneDateInput canGoOnly = input;
    canGoOnly.atoms[0].components[0].dueDate = dayAfter(2);
    canGoOnly.atoms[0].components[0].offerKind = OfferKind::CanGo;
    const M5LaneOutcome outcome = plan(canGoOnly, syntheticProfile());
    CHECK(outcome.kind == OutcomeKind::CompletedWithDispatch);
    CHECK(shippedCount(outcome) == 1);
}

TEST_CASE("U-CF-08 an impossible required stack cannot block feasible can-go fill" * doctest::test_suite("m5")) {
    vector<Atom> atoms{unitOf("tall", "A", 48, 40, 130, 500)};
    atoms = concat(atoms, fx30Units("cango", 10, "A", 0, dayAfter(2)));
    const M5LaneDateInput input = laneWith(atoms, 1, kDefaultTrailer, true);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(shippedCount(outcome) == 10);
    CHECK(countCause(outcome, LeftOverCause::Unfittable) == 1);
}

TEST_CASE("X-DATE-06 every component's date is checked, inside a mixed stack too" * doctest::test_suite("m5")) {
    Atom stack = unitOf("mixed", "A", 100, 20, 100, 3000);
    stack.unitLoads = 2.0;
    stack.components[0].source = "line-d";
    Component later = stack.components[0];
    later.source = "line-d1";
    later.dueDate = dayAfter(1);
    later.offerKind = OfferKind::CanGo;
    stack.components.push_back(later);
    DateRules off{false, 10}, on{true, 10};
    const Eligibility pullOff = atomEligibility(stack, kD, off);
    CHECK_FALSE(pullOff.eligible);
    CHECK(pullOff.failingComponents == vector<size_t>{1});
    CHECK(atomEligibility(stack, kD, on).eligible);
}

TEST_CASE("X-DATE-09 eligibility depends on the departure: D+17 is out on D, in on D+7" * doctest::test_suite("m5")) {
    Component component;
    component.dueDate = "2026-08-24";
    component.availableDate = kD;
    const DateRules rules{true, 10};
    CHECK_FALSE(componentEligibility(component, kD, rules).eligible);
    CHECK(componentEligibility(component, "2026-08-14", rules).eligible);
    CHECK_FALSE(componentEligibility(component, kD, rules).eligible);
}

TEST_CASE("U-RS-05 X-DATA-13 X-DATA-11 bad input is refused before planning" * doctest::test_suite("m5")) {
    for (const int priority : {-1, 12, 99}) {
        M5LaneDateInput input = laneWith(fx30Units("u", 3), 1);
        input.atoms[1].components[0].priority = priority;
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        CHECK(outcome.kind == OutcomeKind::InvalidInput);
        CHECK(countCause(outcome, LeftOverCause::InvalidInput) == 3);
    }
    for (const double bad : {-1.0, 0.0, numeric_limits<double>::quiet_NaN(), numeric_limits<double>::infinity()}) {
        M5LaneDateInput input = laneWith(fx30Units("u", 3), 1);
        input.atoms[0].weightLb = bad == 0.0 ? -0.5 : bad;
        input.atoms[2].floor.longSideIn = bad;
        CHECK(plan(input, syntheticProfile()).kind == OutcomeKind::InvalidInput);
    }
    M5LaneDateInput conflicting = laneWith(fx30Units("u", 2), 1);
    conflicting.atoms[1].components[0].source = conflicting.atoms[0].components[0].source;
    conflicting.atoms[1].components[0].priority = 4;
    CHECK(plan(conflicting, syntheticProfile()).kind == OutcomeKind::InvalidInput);
    M5LaneDateInput repeated = laneWith(fx30Units("u", 2), 1);
    repeated.atoms[1].components[0].source = repeated.atoms[0].components[0].source;
    CHECK(plan(repeated, syntheticProfile()).kind == OutcomeKind::CompletedWithDispatch);
    M5LaneDateInput badSlot = laneWith(fx30Units("u", 2), 1, {630.0, 0.0, 108.0, 45000.0, kFixtureUnitLoadCap});
    CHECK(plan(badSlot, syntheticProfile()).kind == OutcomeKind::InvalidInput);
}

TEST_CASE("U-RT-06 zero reservations: zero dispatch, no capacity invented" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 10), 0);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(outcome.kind == OutcomeKind::CompletedZeroDispatch);
    CHECK_FALSE(outcome.hasCandidate);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 10);
}

TEST_CASE("X-RUN-06 no goods, or only impossible goods: a clean stop, no empty truck" * doctest::test_suite("m5")) {
    M5PolicyProfile blocks = syntheticProfile();
    blocks.method = MethodPolicy::BlocksOnly;
    const M5LaneOutcome empty = plan(laneWith({}, 100), blocks);
    CHECK(empty.kind == OutcomeKind::CompletedNoEligibleInput);
    const M5LaneDateInput impossible = laneWith({unitOf("tall", "A", 48, 40, 130, 500)}, 100);
    const M5LaneOutcome outcome = plan(impossible, blocks);
    CHECK(outcome.kind == OutcomeKind::CompletedZeroDispatch);
    CHECK(countCause(outcome, LeftOverCause::Unfittable) == 1);
}

TEST_CASE("U-RB-02 X-BLK-02 X-BLK-03 rolling blocks never pad and never exceed the pool" * doctest::test_suite("m5")) {
    M5PolicyProfile blocks = syntheticProfile();
    blocks.method = MethodPolicy::BlocksOnly;
    vector<Atom> twentyOne;
    for (int unit = 0; unit < 21; ++unit) twentyOne.push_back(fxBlock("b" + to_string(100 + unit)));
    CHECK(plan(laneWith(twentyOne, 21), blocks).selected->trucks.size() == 21);

    vector<Atom> forty;
    for (int unit = 0; unit < 40; ++unit) forty.push_back(fxBlock("b" + to_string(100 + unit), "A", unit < 38 ? 0 : 11));
    M5LaneDateInput scarce = laneWith(forty, 38);
    scarce.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    const M5LaneOutcome outcome = plan(scarce, blocks);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 38);
    CHECK(shippedIds(outcome, scarce).count("b138") == 0);
    CHECK(shippedIds(outcome, scarce).count("b139") == 0);

    vector<Atom> thirty(forty.begin(), forty.begin() + 30);
    const M5LaneOutcome spare = plan(laneWith(thirty, 38), blocks);
    CHECK(spare.selected->trucks.size() == 30);
}

TEST_CASE("R-6 one truck, two groups: eleven 48 x 40 beat twelve 60 x 60 of which ten fit" * doctest::test_suite("m5")) {
    vector<Atom> atoms;
    for (int unit = 0; unit < 12; ++unit) atoms.push_back(unitOf("big" + to_string(100 + unit), "A", 60, 60, 50, 1000));
    for (int unit = 0; unit < 11; ++unit) atoms.push_back(unitOf("small" + to_string(100 + unit), "B", 48, 40, 50, 1000));
    const M5LaneDateInput input = laneWith(atoms, 1);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedOfGroup(outcome, input, "B") == 11);
    CHECK(shippedOfGroup(outcome, input, "A") == 0);
}

TEST_CASE("X-STATE-10 commitment is atomic, idempotent and refuses stale or conflicting work" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 40), 2);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    REQUIRE(outcome.delta);
    CommitLedger ledger;
    OperationalState state{snapshot.id, snapshot.input.pool.version, {}, 0, 2};
    CHECK(ledger.applyIfCurrent(*outcome.delta, snapshot, state) == ApplyStatus::Applied);
    const OperationalState committed = state;
    CHECK(ledger.applyIfCurrent(*outcome.delta, snapshot, state) == ApplyStatus::AlreadyApplied);
    CommitDelta conflicting = *outcome.delta;
    conflicting.payloadHash = "different";
    CHECK(ledger.applyIfCurrent(conflicting, snapshot, state) == ApplyStatus::PayloadConflict);
    CommitDelta stale = *outcome.delta;
    stale.operationKey = "a new key over the original snapshot";
    CHECK(ledger.applyIfCurrent(stale, snapshot, state) == ApplyStatus::StaleSnapshot);
    CHECK(state.consumedAtoms == committed.consumedAtoms);
    CHECK(state.slotsUsed == committed.slotsUsed);

    const DemandSnapshot residual = residualAfter(snapshot, *outcome.delta);
    CHECK(residual.input.atoms.size() == 0);
    CHECK(residual.id != snapshot.id);
}

TEST_CASE("X-DATA-13/inconsistentSlotCapabilities: payload 0, cap 0, NaN width or a slot unlike its catalogue type is refused before packing" * doctest::test_suite("m5")) {
    const auto refused = [](const M5LaneDateInput& input, OutcomeKind kind, LeftOverCause cause, const string& reasonText) {
        const M5LaneDateInput before = input;
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        CHECK(outcome.kind == kind);
        CHECK_FALSE(outcome.selected.has_value());
        CHECK_FALSE(outcome.delta.has_value());
        CHECK(outcome.floorSearch.statesCharged == 0);
        REQUIRE(outcome.leftOver.size() == input.atoms.size());
        for (const LeftOver& left : outcome.leftOver) CHECK(left.cause == cause);
        const bool named = any_of(outcome.findings.begin(), outcome.findings.end(),
                                  [&](const string& finding) { return finding.find(reasonText) != string::npos; });
        INFO(reasonText);
        CHECK(named);
        CHECK(input.pool.slots.size() == before.pool.slots.size());
    };
    const M5LaneDateInput valid = laneWith(fx30Units("u", 30), 1);
    REQUIRE(shippedCount(plan(valid, syntheticProfile())) == 30);

    M5LaneDateInput noPayload = valid;
    noPayload.pool.slots[0].maxWeightLb = 0.0;
    refused(noPayload, OutcomeKind::InvalidInput, LeftOverCause::InvalidInput, "weight limit that is not finite and above zero");
    M5LaneDateInput noCap = valid;
    noCap.pool.slots[0].maxUnitLoads = 0;
    refused(noCap, OutcomeKind::ConfigurationIncomplete, LeftOverCause::ConfigurationIncomplete, "no unit-load cap");
    M5LaneDateInput nanWidth = valid;
    nanWidth.pool.slots[0].widthIn = numeric_limits<double>::quiet_NaN();
    refused(nanWidth, OutcomeKind::InvalidInput, LeftOverCause::InvalidInput, "trailer size or weight limit");

    M5LaneDateInput mismatch = valid;
    mismatch.trailerTypes = {{"53F", kDefaultTrailer}};
    mismatch.pool.slotEquipment = {"53F"};
    REQUIRE(shippedCount(plan(mismatch, syntheticProfile())) == 30);
    mismatch.pool.slots[0].maxWeightLb = 40000.0;
    refused(mismatch, OutcomeKind::InvalidInput, LeftOverCause::InvalidInput, "differs from the catalogue entry for '53F'");
    M5LaneDateInput undeclared = valid;
    undeclared.trailerTypes = {{"53F", kDefaultTrailer}};
    refused(undeclared, OutcomeKind::InvalidInput, LeftOverCause::InvalidInput, "names no equipment");
    m5Evidence::note("expected", {"payload 0 refused", "cap 0 refused", "NaN width refused", "catalogue mismatch refused"});
}

TEST_CASE("X-DATA-13 a slot-equipment list longer or shorter than the slots is refused by name before packing" * doctest::test_suite("m5")) {
    M5LaneDateInput matched = laneWith(fx30Units("u", 30), 2);
    matched.trailerTypes = {{"53F", kDefaultTrailer}};
    matched.pool.slotEquipment = {"53F", "53F"};
    CHECK(slotIdentityErrors(matched).empty());
    REQUIRE(shippedCount(plan(matched, syntheticProfile())) == 30);
    for (const vector<string>& codes : {vector<string>{"53F"}, vector<string>{"53F", "53F", "53F"}}) {
        M5LaneDateInput uneven = matched;
        uneven.pool.slotEquipment = codes;
        const string expected = "names equipment for " + to_string(codes.size()) + " trucks but reserves 2 slots";
        const vector<string> errors = slotIdentityErrors(uneven);
        CHECK(any_of(errors.begin(), errors.end(), [&](const string& error) { return error.find(expected) != string::npos; }));
        const M5LaneOutcome outcome = plan(uneven, syntheticProfile());
        CHECK(outcome.kind == OutcomeKind::InvalidInput);
        CHECK_FALSE(outcome.selected.has_value());
        CHECK(outcome.floorSearch.statesCharged == 0);
        REQUIRE(outcome.leftOver.size() == 30);
        for (const LeftOver& left : outcome.leftOver) CHECK(left.cause == LeftOverCause::InvalidInput);
    }
}
