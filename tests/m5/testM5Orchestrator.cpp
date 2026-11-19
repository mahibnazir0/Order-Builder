#include "doctest.h"
#include "m5Ledger.hpp"
#include "m5Report.hpp"
#include "m5Solver.hpp"
#include "m5Validation.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Fixtures.hpp"
#include "support/m5Oracles.hpp"
#include "support/m5StubMethods.hpp"

#include <algorithm>
#include <memory>
#include <set>

using namespace std;
using namespace ob::m5;
using namespace m5Fixtures;

namespace {

size_t countCause(const M5LaneOutcome& outcome, LeftOverCause cause) {
    return static_cast<size_t>(count_if(outcome.leftOver.begin(), outcome.leftOver.end(),
                                        [&](const LeftOver& left) { return left.cause == cause; }));
}

void requireInvariants(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    const vector<string> violations = m5Oracles::invariantViolations(outcome, input, physicalSettings().floor);
    for (const string& violation : violations) INFO(violation);
    REQUIRE(violations.empty());
}

M5LaneOutcome planWith(const M5LaneDateInput& input, const M5PolicyProfile& profile, FakeClock& clock, optional<double> allowance,
                       vector<shared_ptr<const MethodPlanner>> methods, function<void(MethodId)> beforeMethod) {
    M5RunServices services(clock);
    services.allowanceSeconds = allowance;
    services.methods = std::move(methods);
    services.beforeMethod = std::move(beforeMethod);
    return planLaneDate(input, profile, physicalSettings(), services);
}

}  // namespace

TEST_CASE("X-RUN-02/synthetic X-CG-12 time out before any result: no result, every good kept, not a zero-truck success" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 40), 2);
    FakeClock clock;
    const M5LaneOutcome outcome =
        planWith(input, syntheticProfile(), clock, 10.0, {}, [&](MethodId) { clock.advance(11.0); });
    CHECK(outcome.kind == OutcomeKind::TimeoutNoIncumbent);
    CHECK(outcome.kind != OutcomeKind::CompletedZeroDispatch);
    CHECK(outcome.stop == StopReason::BudgetExhausted);
    CHECK_FALSE(outcome.hasCandidate);
    CHECK_FALSE(outcome.selected.has_value());
    CHECK_FALSE(outcome.delta.has_value());
    CHECK(outcome.proof != ProofStatus::ProvenInfeasible);
    CHECK(countCause(outcome, LeftOverCause::NoResult) == 40);
    CHECK(countCause(outcome, LeftOverCause::Unfittable) == 0);
    CHECK(outcome.leftOver.size() == 40);
    const string text = renderAscii(buildReportData(outcome, input, nullptr, {}));
    CHECK(text.find("outcome: timed out: no checked result") != string::npos);
    CHECK(text.find("impossible") == string::npos);
    m5Evidence::note("expected", {"TimeoutNoIncumbent, all 40 kept as NoResult", "no delta, no proof of impossibility"});
}

TEST_CASE("X-CG-12/synthetic: stopped before any result: no result found, every good kept; not a zero-truck success, not impossible" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 40), 2);
    FakeClock clock;
    const M5LaneOutcome outcome =
        planWith(input, syntheticProfile(), clock, 10.0, {}, [&](MethodId) { clock.advance(11.0); });
    CHECK(outcome.kind == OutcomeKind::TimeoutNoIncumbent);
    CHECK(outcome.kind != OutcomeKind::CompletedNoEligibleInput);
    CHECK_FALSE(outcome.hasCandidate);
    CHECK(outcome.proof != ProofStatus::ProvenInfeasible);
    CHECK(outcome.leftOver.size() == 40);
    CHECK(countCause(outcome, LeftOverCause::NoResult) == 40);
    CHECK(countCause(outcome, LeftOverCause::Unfittable) == 0);
    // The scalar solver stopped at once: no packing, Budget, nothing called impossible or proven.
    ScalarModel model;
    model.sizes = {6, 5, 3, 2, 2, 2};
    model.capacity = 10;
    const ScalarResult scalar = solveCuttingStock(model, ScalarLimits(), [] { return true; });
    CHECK_FALSE(scalar.packing.has_value());
    CHECK(scalar.impossible.empty());
    CHECK(scalar.evidence.termination == Termination::Budget);
    CHECK(scalar.evidence.proof != ProofStatus::ProvenOptimal);
    CHECK(scalar.evidence.proof != ProofStatus::ProvenInfeasible);
    m5Evidence::note("expected", {"TimeoutNoIncumbent, 40 left NoResult", "scalar: no packing, Budget, not proven"});
}

TEST_CASE("X-RUN-04/synthetic: the careful method gets only what the quick one left; no fresh deadline; only a checked result" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 40), 2);
    FakeClock clock;
    vector<shared_ptr<const MethodPlanner>> methods{make_shared<PatternPlanner>(), make_shared<BlockPlanner>()};
    const M5LaneOutcome outcome = planWith(input, syntheticProfile(), clock, 100.0, methods, [&](MethodId method) {
        clock.advance(method == MethodId::PatternRepeat ? 85.0 : 20.0);
    });
    CHECK(outcome.allowanceSeconds == 100.0);
    CHECK(outcome.stop == StopReason::BudgetExhausted);
    CHECK(outcome.kind == OutcomeKind::TimeoutWithIncumbent);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->method == MethodId::PatternRepeat);
    CHECK(shippedCount(outcome) == 40);
    CHECK(outcome.timing.elapsedSeconds == doctest::Approx(105.0));
    // The allowance is the one declared up front (100), whatever B needed, and the plan that stands is checked.
    CHECK(outcome.timing.elapsedSeconds > outcome.allowanceSeconds);
    CHECK(outcome.finalValidationPassed);
    REQUIRE(outcome.selected->validation.has_value());
    CHECK(outcome.selected->validation->allPassed());
    REQUIRE(outcome.methods.size() == 2);
    CHECK(outcome.methods[0].selected);
    CHECK_FALSE(outcome.methods[1].selected);
    const M5ReportData report = buildReportData(outcome, input, nullptr, {});
    const string text = renderAscii(report);
    CHECK(text.find("time: 105.000 s of 100.00 s allowed") != string::npos);
    CHECK(report.loadedTrucks == 2);
    CHECK(report.secondsPerLoadedTruck.has_value());
    // With no accepted truck the average is N/A.
    const M5LaneOutcome none = planWith(input, withAcceptance(syntheticProfile(), 400000.0, 90000.0), clock, 100.0, {}, {});
    CHECK(none.kind == OutcomeKind::CompletedZeroDispatch);
    CHECK(renderAscii(buildReportData(none, input, nullptr, {})).find("seconds per loaded truck: N/A") != string::npos);
    m5Evidence::note("expected", {"A used 85 of 100; B stopped at the same deadline, A's checked plan stands", "allowance 100, never renewed", "total time printed; N/A average for zero trucks"});
}

TEST_CASE("X-RUN-11/synthetic: per-load maximum and amortised average are different figures; CL-05 judges the lane on total time within its allowance" * doctest::test_suite("m5")) {
    vector<double> loads(9, 1.0);
    loads.push_back(11.0);
    const LoadTimeSummary summary = summarizeLoadTimes(loads);
    CHECK(summary.totalSeconds == 20.0);
    CHECK(summary.loads == 10);
    CHECK(*summary.averageSeconds == 2.0);
    CHECK(*summary.maximumSeconds == 11.0);
    CHECK_FALSE(*summary.maximumSeconds <= 10.0);
    CHECK(*summary.averageSeconds <= 10.0);
    // CL-05: the allowance is 10 s per load x 10 reserved trucks = 100 s; 20 s of total time passes it, and
    // the 11 s maximum is reported but is not the verdict.
    string error;
    const optional<double> allowance = laneAllowanceSeconds(10.0, 10, &error);
    REQUIRE(allowance.has_value());
    CHECK(*allowance == 100.0);
    CHECK(summary.totalSeconds <= *allowance);
    CHECK(*summary.maximumSeconds > 10.0);
    CHECK(*summary.averageSeconds != *summary.maximumSeconds);
    M5PolicyProfile profile = syntheticProfile();
    profile.secondsPerLoad = 10.0;
    CHECK(plan(laneWith(fx30Units("u", 10), 10), profile).allowanceSeconds == 100.0);
    m5Evidence::note("expected", {"total 20 s, 10 loads, average 2 s, maximum 11 s", "max <= 10 false, average <= 10 true", "CL-05: 20 s within 10 s x 10 = 100 s passes"});
    CHECK_FALSE(summarizeLoadTimes({}).averageSeconds.has_value());
}

TEST_CASE("X-RUN-12 X-REPORT-06 zero dispatch, no work and no result are told apart" * doctest::test_suite("m5")) {
    const M5LaneOutcome empty = plan(laneWith({}, 1), syntheticProfile());
    CHECK(empty.kind == OutcomeKind::CompletedNoEligibleInput);

    const M5LaneDateInput twenty = laneWith(fx30Units("u", 20), 1);
    const M5LaneOutcome zero = plan(twenty, withAcceptance(syntheticProfile(), 40000.0, 3000.0));
    CHECK(zero.kind == OutcomeKind::CompletedZeroDispatch);
    CHECK(countCause(zero, LeftOverCause::AcceptanceWithheld) == 20);
    const M5ReportData report = buildReportData(zero, twenty, nullptr, {});
    CHECK_FALSE(report.secondsPerLoadedTruck.has_value());
    CHECK(renderAscii(report).find("seconds per loaded truck: N/A") != string::npos);

    FakeClock clock;
    const M5LaneOutcome timedOut = planWith(twenty, syntheticProfile(), clock, 5.0, {}, [&](MethodId) { clock.advance(6.0); });
    CHECK(timedOut.kind == OutcomeKind::TimeoutNoIncumbent);
}

TEST_CASE("X-RUN-13 the budget is frozen and only the chosen result counts" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 60), 3);
    M5PolicyProfile profile = syntheticProfile();
    profile.comparator = ComparatorKind::FxPriority;
    FakeClock clock;
    vector<shared_ptr<const MethodPlanner>> methods{make_shared<m5Stubs::FixedMethod>(MethodId::PatternRepeat, 3, 20),
                                                    make_shared<m5Stubs::FixedMethod>(MethodId::RollingBlocks, 2, 30)};
    const M5LaneOutcome outcome = planWith(input, profile, clock, 20.0, methods, [&](MethodId method) {
        clock.advance(method == MethodId::PatternRepeat ? 8.0 : 7.0);
    });
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->method == MethodId::RollingBlocks);
    CHECK(outcome.selected->trucks.size() == 2);
    REQUIRE(outcome.delta);
    CHECK(outcome.delta->trucks.size() == 2);
    CHECK(outcome.allowanceSeconds == 20.0);
    const M5ReportData report = buildReportData(outcome, input, &profile, {});
    CHECK(report.loadedTrucks == 2);
    CHECK(*report.secondsPerLoadedTruck == doctest::Approx(report.elapsedSeconds / 2.0));
    CHECK(report.unitLoadsShipped == 60.0);
}

TEST_CASE("X-RUN-12/noGoods: no goods and a completed run is a completed empty result that uses no reservation" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith({}, 1);
    const M5PolicyProfile profile = syntheticProfile();
    const M5LaneOutcome outcome = plan(input, profile);
    CHECK(outcome.kind == OutcomeKind::CompletedNoEligibleInput);
    CHECK(outcome.stop == StopReason::Completed);
    CHECK_FALSE(outcome.selected.has_value());
    CHECK_FALSE(outcome.delta.has_value());
    CHECK(outcome.leftOver.empty());
    const M5ReportData report = buildReportData(outcome, input, &profile, {});
    CHECK(report.loadedTrucks == 0);
    CHECK(report.unusedReservations == 1);
    CHECK_FALSE(report.secondsPerLoadedTruck.has_value());
    const string text = renderAscii(report);
    CHECK(text.find("seconds per loaded truck: N/A") != string::npos);
    CHECK(text.find("nan") == string::npos);
    CHECK(text.find("inf") == string::npos);
    CHECK(text.find("time: 0.000 s") != string::npos);
    m5Evidence::note("expected", {"completed, no eligible goods, no truck, no reservation used", "seconds per loaded truck N/A, total time printed"});
}

TEST_CASE("X-RUN-12/completedZeroDispatch: twenty units under x = 40,000 and y = 3,000 complete as zero dispatch with all 20 accounted for and the acceptance reason" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 20), 1);
    const M5PolicyProfile profile = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    const M5LaneOutcome outcome = plan(input, profile);
    requireInvariants(outcome, input);
    CHECK(outcome.kind == OutcomeKind::CompletedZeroDispatch);
    CHECK(outcome.stop == StopReason::Completed);
    CHECK(outcome.leftOver.size() == 20);
    CHECK(countCause(outcome, LeftOverCause::AcceptanceWithheld) == 20);
    CHECK(countCause(outcome, LeftOverCause::Unfittable) == 0);
    CHECK(countCause(outcome, LeftOverCause::NoResult) == 0);
    CHECK(outcome.proof != ProofStatus::ProvenInfeasible);
    const M5ReportData report = buildReportData(outcome, input, &profile, {});
    CHECK(report.loadedTrucks == 0);
    CHECK(report.unitLoadsLeftByCause.at("withheld by minimum-load policy") == 20.0);
    CHECK_FALSE(report.secondsPerLoadedTruck.has_value());
    const string text = renderAscii(report);
    CHECK(text.find("outcome: completed: no truck dispatched") != string::npos);
    CHECK(text.find("seconds per loaded truck: N/A") != string::npos);
    CHECK(text.find("nan") == string::npos);
    CHECK(text.find("inf") == string::npos);
    m5Evidence::note("expected", {"completed zero dispatch under the policy", "20 withheld by minimum-load policy, none unfittable or unknown", "N/A seconds per accepted load"});
}

TEST_CASE("X-RUN-12/timeoutNoIncumbent: eligible goods and a timeout before any checked result is a timeout, never an empty success or proven infeasibility" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 30), 1);
    const M5PolicyProfile profile = syntheticProfile();
    FakeClock clock;
    const M5LaneOutcome outcome = planWith(input, profile, clock, 5.0, {}, [&](MethodId) { clock.advance(6.0); });
    requireInvariants(outcome, input);
    CHECK(outcome.kind == OutcomeKind::TimeoutNoIncumbent);
    CHECK(outcome.kind != OutcomeKind::CompletedZeroDispatch);
    CHECK(outcome.kind != OutcomeKind::CompletedNoEligibleInput);
    CHECK(outcome.stop == StopReason::BudgetExhausted);
    CHECK(outcome.proof != ProofStatus::ProvenInfeasible);
    CHECK(countCause(outcome, LeftOverCause::NoResult) == 30);
    const M5ReportData report = buildReportData(outcome, input, &profile, {});
    CHECK_FALSE(report.secondsPerLoadedTruck.has_value());
    CHECK(report.elapsedSeconds >= 6.0);
    const string text = renderAscii(report);
    CHECK(text.find("outcome: timed out: no checked result") != string::npos);
    CHECK(text.find("seconds per loaded truck: N/A") != string::npos);
    CHECK(text.find("infeasible") == string::npos);
    m5Evidence::note("expected", {"timed out, no checked result, 30 kept as NoResult", "not a zero-dispatch success, no infeasibility claim", "total time printed, average N/A"});
}

TEST_CASE("X-RUN-13/synthetic: the frozen 20 s allowance counts A, B, reselection and final validation; only the chosen B is counted; 20 / 2 = 10 s per load, not 20 / 5" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 60), 3);
    M5PolicyProfile profile = withAcceptance(syntheticProfile(), 40000.0, 2000.0);
    profile.comparator = ComparatorKind::FxPriority;
    const auto run = [&](double extraSeconds) {
        FakeClock clock;
        M5RunServices services(clock);
        services.allowanceSeconds = 20.0;
        services.methods = {make_shared<m5Stubs::FixedMethod>(MethodId::PatternRepeat, 3, 20),
                            make_shared<m5Stubs::FixedMethod>(MethodId::RollingBlocks, 2, 30)};
        services.beforeMethod = [&](MethodId method) { clock.advance(method == MethodId::PatternRepeat ? 8.0 : 7.0); };
        // Reselection (3 s) and final validation (2 s) land after B, inside the same interval.
        services.afterMethod = [&](MethodId method) {
            if (method == MethodId::RollingBlocks) clock.advance(5.0 + extraSeconds);
        };
        return planLaneDate(input, profile, physicalSettings(), services);
    };
    const M5LaneOutcome exact = run(0.0);
    requireInvariants(exact, input);
    CHECK(exact.allowanceSeconds == 20.0);
    CHECK(exact.timing.elapsedSeconds == doctest::Approx(20.0));
    REQUIRE(exact.selected);
    CHECK(exact.selected->method == MethodId::RollingBlocks);
    CHECK(exact.selected->trucks.size() == 2);
    REQUIRE(exact.delta);
    CHECK(exact.delta->trucks.size() == 2);
    CHECK(shippedCount(exact) == 60);
    const M5ReportData report = buildReportData(exact, input, &profile, {});
    CHECK(report.loadedTrucks == 2);
    CHECK(report.unitLoadsShipped == 60.0);
    REQUIRE(report.secondsPerLoadedTruck.has_value());
    CHECK(*report.secondsPerLoadedTruck == doctest::Approx(10.0));
    CHECK(*report.secondsPerLoadedTruck != doctest::Approx(4.0));
    // The three A candidates are in no production figure: the methods list shows them, the totals do not.
    REQUIRE(exact.methods.size() == 2);
    CHECK(exact.methods[0].trucks == 3);
    CHECK(exact.methods[1].trucks == 2);
    CHECK_FALSE(exact.methods[0].selected);
    CHECK(exact.methods[1].selected);

    // Over the allowance: the deadline is the same 20 s, the real stop is shown, and only a checked result stands.
    const M5LaneOutcome over = run(5.0);
    CHECK(over.allowanceSeconds == 20.0);
    CHECK(over.stop == StopReason::BudgetExhausted);
    CHECK(over.timing.elapsedSeconds == doctest::Approx(25.0));
    if (over.selected) {
        REQUIRE(over.selected->validation.has_value());
        CHECK(over.selected->validation->allPassed());
        CHECK(over.kind == OutcomeKind::TimeoutWithIncumbent);
    } else {
        CHECK(over.kind == OutcomeKind::TimeoutNoIncumbent);
    }
    const M5ReportData overReport = buildReportData(over, input, &profile, {});
    if (overReport.loadedTrucks > 0) {
        REQUIRE(overReport.secondsPerLoadedTruck.has_value());
        CHECK(*overReport.secondsPerLoadedTruck == doctest::Approx(overReport.elapsedSeconds / static_cast<double>(overReport.loadedTrucks)));
    }
    m5Evidence::note("expected", {"A 8 s, B 7 s, reselection 3 s, final validation 2 s = 20 s of a frozen 20 s", "B selected: 2 trucks, 60 units; A's 3 trucks not counted", "10 s per accepted load, not 4", "25 s: same 20 s deadline, real stop, checked result only"});
}

TEST_CASE("U-PF-08 each lane-date declares its own allowance" * doctest::test_suite("m5")) {
    M5PolicyProfile profile = syntheticProfile();
    profile.secondsPerLoad = 1.0;
    CHECK(plan(laneWith(fx30Units("u", 30), 21), profile).allowanceSeconds == 21.0);
    CHECK(plan(laneWith(fx30Units("u", 30), 20), profile).allowanceSeconds == 20.0);
}

TEST_CASE("X-STATE-02 X-STATE-09 methods' results are never added together" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 60), 3);
    FakeClock clock;
    vector<shared_ptr<const MethodPlanner>> methods{make_shared<PatternPlanner>(), make_shared<BlockPlanner>()};
    const M5LaneOutcome outcome = planWith(input, syntheticProfile(), clock, nullopt, methods, {});
    REQUIRE(outcome.methods.size() == 2);
    CHECK(outcome.methods[0].ran);
    CHECK(outcome.methods[1].ran);
    const M5ReportData report = buildReportData(outcome, input, nullptr, {});
    CHECK(report.unitLoadsShipped == 60.0);
    CHECK(report.loadedTrucks == 2);
    CHECK(report.unusedReservations == 1);
    CHECK(count_if(outcome.methods.begin(), outcome.methods.end(), [](const MethodRecord& record) { return record.selected; }) >= 1);
    // Acceptance ON then OFF then ON on the same input: nothing carries over between profiles.
    const M5PolicyProfile on = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    const M5LaneDateInput small = laneWith(fx30Units("s", 20), 1);
    CHECK(plan(small, on).kind == OutcomeKind::CompletedZeroDispatch);
    CHECK(plan(small, syntheticProfile()).kind == OutcomeKind::CompletedWithDispatch);
    CHECK(plan(small, on).kind == OutcomeKind::CompletedZeroDispatch);
}

TEST_CASE("X-STATE-02/unselectedMethodProof: B's own-model certificate is never attached to the selected A" * doctest::test_suite("m5")) {
    // Atoms 0-29 are P0, atom 30 is P11; one slot holds 30.
    vector<Atom> atoms = fx30Units("p0-", 30);
    atoms.push_back(fx30("p11-100", "A", 11));
    const M5LaneDateInput input = laneWith(atoms, 1);
    Truck thirtyP0, twentyNineP0AndP11;
    for (size_t atom = 0; atom < 30; ++atom) thirtyP0.push_back(atom);
    for (size_t atom = 0; atom < 29; ++atom) twentyNineP0AndP11.push_back(atom);
    twentyNineP0AndP11.push_back(30);
    auto methodB = make_shared<m5Stubs::FixedMethod>(MethodId::RollingBlocks, GroupTrucks{twentyNineP0AndP11});
    methodB->markExact();
    FakeClock clock;
    M5RunServices services(clock);
    services.methods = {make_shared<m5Stubs::FixedMethod>(MethodId::PatternRepeat, GroupTrucks{thirtyP0}), methodB};
    const M5LaneOutcome outcome = planLaneDate(input, syntheticProfile(), physicalSettings(), services);
    REQUIRE(outcome.methods.size() == 2);
    CHECK(outcome.methods[0].selected);
    CHECK(outcome.methods[1].ran);
    CHECK(outcome.methods[1].validated);
    CHECK_FALSE(outcome.methods[1].selected);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->method == MethodId::PatternRepeat);
    REQUIRE(outcome.selected->trucks.size() == 1);
    CHECK(outcome.selected->trucks[0].atoms == thirtyP0);
    CHECK(outcome.selected->evidence.proof == ProofStatus::Unproven);
    CHECK_FALSE(outcome.selected->evidence.lowerBound.has_value());
    CHECK(outcome.selected->evidence.boundModel.empty());
    CHECK(outcome.selected->evidence.proofModel.empty());
    CHECK(outcome.proof == ProofStatus::Unproven);
    REQUIRE(outcome.delta);
    REQUIRE(outcome.delta->trucks.size() == 1);
    REQUIRE(outcome.leftOver.size() == 1);
    CHECK(outcome.leftOver[0].atom == 30);
    m5Evidence::note("expected", {"A selected: 30 P0", "proof Unproven", "no bound or model from B"});
}

TEST_CASE("Q6 equality stops the count phase: B is skipped with its reason" * doctest::test_suite("m5")) {
    const M5LaneOutcome outcome = plan(laneWith(fx30Units("u", 60), 3), syntheticProfile());
    REQUIRE(outcome.methods.size() == 2);
    CHECK(outcome.methods[0].ran);
    CHECK_FALSE(outcome.methods[1].ran);
    CHECK(outcome.methods[1].skippedBecause.find("equal the valid bound") != string::npos);
    CHECK(outcome.escalation.quickTrucks == 2);
    CHECK(outcome.escalation.bound == 2);
    CHECK(outcome.proof == ProofStatus::ProvenOptimal);
}

namespace {

// X-CG-20: five FX-BLOCK atoms on two slots, atoms 0..2 P11 and 3..4 P0, all due and available D.
M5LaneDateInput xcg20Lane() {
    return laneWith({fxBlock("p11-a", "A", 11), fxBlock("p11-b", "A", 11), fxBlock("p11-c", "A", 11), fxBlock("p0-a", "A", 0),
                     fxBlock("p0-b", "A", 0)},
                    2);
}

// The injected quick candidate: the two P11 atoms 0 and 1, one per truck, supplied as exact for its own subset.
shared_ptr<const MethodPlanner> xcg20Quick() {
    auto quick = make_shared<m5Stubs::FixedMethod>(MethodId::PatternRepeat, GroupTrucks{{0}, {1}});
    quick->markExact();
    return quick;
}

// Oracle: every two-of-five choice, scored by shipped count per priority class (P0 first); the best set.
set<size_t> bestTwoOfFive(const M5LaneDateInput& input) {
    set<size_t> best;
    pair<int, int> bestScore{-1, -1};
    for (size_t first = 0; first < input.atoms.size(); ++first) {
        for (size_t second = first + 1; second < input.atoms.size(); ++second) {
            int p0 = 0, p11 = 0;
            for (const size_t atom : {first, second}) (input.atoms[atom].components[0].priority == 0 ? p0 : p11) += 1;
            if (make_pair(p0, p11) > bestScore) {
                bestScore = {p0, p11};
                best = {first, second};
            }
        }
    }
    return best;
}

set<size_t> shippedAtoms(const M5LaneOutcome& outcome) {
    set<size_t> shipped;
    if (outcome.selected) {
        for (const TruckPlan& truck : outcome.selected->trucks) shipped.insert(truck.atoms.begin(), truck.atoms.end());
    }
    return shipped;
}

}  // namespace

TEST_CASE("X-CG-20/subsetBound: a quick plan of two P11 blocks with its subset bound 2 does not stop the search; both P0 blocks ship, three P11 stay" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = xcg20Lane();
    FakeClock clock;
    const M5LaneOutcome outcome = planWith(input, syntheticProfile(), clock, 1000.0, {xcg20Quick(), make_shared<BlockPlanner>()}, {});
    REQUIRE(outcome.selected);
    CHECK(bestTwoOfFive(input) == set<size_t>{3, 4});
    CHECK(shippedAtoms(outcome) == set<size_t>{3, 4});
    CHECK(outcome.selected->trucks.size() == 2);
    CHECK(outcome.leftOver.size() == 3);
    for (const LeftOver& left : outcome.leftOver) CHECK(input.atoms[left.atom].components[0].priority == 11);
    CHECK(outcome.offeredService == ServiceLevel::Partial);
    CHECK(outcome.proof != ProofStatus::ProvenOptimal);
    CHECK(outcome.selected->evidence.proof != ProofStatus::ProvenOptimal);
    CHECK(outcome.finalValidationPassed);
    m5Evidence::note("expected", {"ships p0-a and p0-b", "p11-a, p11-b, p11-c left", "partial service, not proven optimal"});
}

TEST_CASE("X-CG-20/wrongAllGoodsBound: F = 2 against the all-goods bound 5 is not clipped into a zero gap or a certificate" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = xcg20Lane();
    FakeClock clock;
    const M5LaneOutcome outcome = planWith(input, syntheticProfile(), clock, 1000.0, {xcg20Quick(), make_shared<BlockPlanner>()}, {});
    REQUIRE(outcome.selected);
    // All five whole-truck blocks need five trucks: the all-required bound is 5, analytically one per truck.
    CHECK(trucksAtLeast(input, {0, 1, 2, 3, 4}, kDefaultTrailer) == 5);
    CHECK(gapText(2, 5).find("0 trucks") == string::npos);
    CHECK(gapText(2, 5).rfind("-3 trucks", 0) == 0);
    const SolverEvidence& evidence = outcome.selected->evidence;
    if (evidence.lowerBound) CHECK(*evidence.lowerBound <= 2);
    CHECK(evidence.proof != ProofStatus::ProvenOptimal);
    // The count metric does not compare against a partial objective: no "0 trucks" gap is reported.
    CHECK(outcome.escalation.gap.find("0 trucks") == string::npos);
    CandidatePlan claimed = *outcome.selected;
    claimed.evidence.lowerBound = 5;
    claimed.evidence.proof = ProofStatus::ProvenOptimal;
    const ValidationReport report = validateFinal(claimed, *buildSnapshot(input).snapshot, syntheticProfile(), physicalSettings());
    CHECK(report.metadata == SectionStatus::Fail);
    m5Evidence::note("expected", {"all-goods bound 5", "gap on F = 2 never 0", "a proof claimed on bound 5 fails metadata"});
}

TEST_CASE("X-CG-20/timedIncumbent: time out after the quick plan: its two P11 blocks are kept Unproven and do not pass as the expected selection" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = xcg20Lane();
    FakeClock clock;
    const M5LaneOutcome outcome = planWith(input, syntheticProfile(), clock, 100.0, {xcg20Quick(), make_shared<BlockPlanner>()},
                                           [&](MethodId method) { clock.advance(method == MethodId::PatternRepeat ? 101.0 : 0.0); });
    CHECK(outcome.stop == StopReason::BudgetExhausted);
    CHECK(outcome.kind == OutcomeKind::TimeoutWithIncumbent);
    REQUIRE(outcome.selected);
    CHECK(shippedAtoms(outcome) == set<size_t>{0, 1});
    CHECK(shippedAtoms(outcome) != bestTwoOfFive(input));
    CHECK(outcome.proof == ProofStatus::Unproven);
    CHECK(outcome.selected->evidence.proof != ProofStatus::ProvenOptimal);
    m5Evidence::note("expected", {"TimeoutWithIncumbent", "p11-a, p11-b shipped, Unproven", "not the expected P0 selection"});
}

TEST_CASE("X-CG-20/matchingCountControl: [6,6,4,4] on 10, quick 2 and same-model bound 2: count escalation is skipped, the proof scoped to that model" * doctest::test_suite("m5")) {
    ScalarModel model;
    model.sizes = {6, 6, 4, 4};
    model.capacity = 10;
    CHECK(scalarLowerBound(model) == 2);
    CHECK_FALSE(shouldEscalate(2, 2, 1, true));
    const ScalarResult result = solveCuttingStock(model, ScalarLimits());
    REQUIRE(result.packing.has_value());
    CHECK(result.packing->bins.size() == 2);
    CHECK(result.evidence.incumbent == 2);
    CHECK(result.evidence.lowerBound == 2);
    CHECK(result.evidence.proof == ProofStatus::ProvenOptimal);
    CHECK(result.evidence.proofModel == result.evidence.boundModel);
    CHECK_FALSE(result.evidence.proofModel.empty());
    CHECK(result.evidence.layoutScope.find("floor") == string::npos);
    m5Evidence::note("expected", {"2 bins, bound 2, ProvenOptimal for the scalar count model only", "no escalation (F = B)"});
}

TEST_CASE("X-CG-20 a count bound on shipped goods is not a service proof" * doctest::test_suite("m5")) {
    const vector<Atom> atoms = [] {
        vector<Atom> built = fx30Units("p0-", 20, "A", 0);
        const vector<Atom> low = fx30Units("p11-", 40, "A", 11);
        built.insert(built.end(), low.begin(), low.end());
        return built;
    }();
    const M5LaneOutcome outcome = plan(laneWith(atoms, 1), syntheticProfile());
    REQUIRE(outcome.selected);
    CHECK(outcome.offeredService == ServiceLevel::Partial);
    CHECK(outcome.proof == ProofStatus::Unproven);
    CHECK(outcome.selected->evidence.proof != ProofStatus::ProvenOptimal);
}

TEST_CASE("X-REPORT-10 bucket averages weight trucks once and print N/A when empty" * doctest::test_suite("m5")) {
    FullnessLabel first, second;
    first.bucket = second.bucket = FullnessBucket::GoodsRemain;
    first.rearInches = 10.0;
    second.rearInches = 20.0;
    first.byWeight = first.byRearInches = true;
    const auto buckets = rearInchesByBucket({first, second});
    CHECK(buckets.at("goods remain").trucks == 2);
    CHECK(*buckets.at("goods remain").averageRearInches == doctest::Approx(15.0));
    CHECK_FALSE(buckets.at("no goods remain").averageRearInches.has_value());
    CHECK_FALSE(buckets.at("rule-limited").averageRearInches.has_value());
}

TEST_CASE("U-RP-01 X-REPORT-04 the header names the export check and the fullness rule" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 30), 1);
    M5PolicyProfile profile = syntheticProfile();
    const string production = renderAscii(buildReportData(plan(input, profile), input, &profile, {"PRODUCTION", 20, "fixture"}));
    CHECK(production.find("minimum-load check: at export, after M5") != string::npos);
    CHECK(production.find("FULL when no other allowed item fits; rear-space target 14.0 in (default)") != string::npos);
    CHECK(production.find("reserved trucks per lane and ship date > 20") != string::npos);
    CHECK(production.find("3A Run") != string::npos);
    CHECK(production.find("3E Summary") != string::npos);
    const M5PolicyProfile synthetic = withAcceptance(profile, 40000.0, 3300.0);
    const string withPolicy = renderAscii(buildReportData(plan(input, synthetic), input, &synthetic, {"SYNTHETIC", 20, "fixture"}));
    CHECK(withPolicy.find("weight > 40000.0 lb OR cube > 3300.0 cu ft") != string::npos);
}

TEST_CASE("U-RP-01/productionProfile: acceptance OFF: the run header prints the fullness rule and the export note, and accepts missing x/y" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 30), 2);
    const M5PolicyProfile profile = syntheticProfile();
    REQUIRE_FALSE(profile.acceptance.has_value());
    const string text = renderAscii(buildReportData(plan(input, profile), input, &profile, {"PRODUCTION", 20, "run-17"}));
    INFO(text);
    const size_t header = text.find("3A Run\n");
    const size_t lane = text.find("3B Lane\n");
    REQUIRE(header != string::npos);
    REQUIRE(lane != string::npos);
    const string run = text.substr(header, lane - header);
    CHECK(run.find("  minimum-load check: at export, after M5\n") != string::npos);
    CHECK(run.find("  fullness rule: FULL when no other allowed item fits; rear-space target 14.0 in (default)\n")
          != string::npos);
    CHECK(run.find("  scope: PRODUCTION\n") != string::npos);
    CHECK(run.find("  run: run-17\n") != string::npos);
    CHECK(run.find("  hand-over: fixture\n") != string::npos);
    CHECK(run.find("  profile: " + profile.key() + "\n") != string::npos);
    CHECK(run.find("  routing: reserved trucks per lane and ship date > 20\n") != string::npos);
    CHECK(run.find("  past-due order: AgeFirst\n") != string::npos);
    CHECK(run.find("  can-go fill: off (pull-forward off)\n") != string::npos);
    CHECK(run.find("  seconds per load: 10.00\n") != string::npos);
    CHECK(run.find("  lane-date allowance: 20.00 s (2 reserved)\n") != string::npos);
    CHECK(run.find("cu ft") == string::npos);
    // A user rear limit is printed as a user setting, not as the default.
    M5PolicyProfile userRear = profile;
    userRear.fullness.rearInchLimit = 20.0;
    userRear.fullness.rearInchLimitDefaulted = false;
    const string userText = renderAscii(buildReportData(plan(input, userRear), input, &userRear, {"PRODUCTION", 20, "run-17"}));
    CHECK(userText.find("rear-space target 20.0 in (user setting)") != string::npos);
    // Missing x/y is valid (loading an m5 block without one: U-AC-01/productionProfile): the header prints no thresholds.
    m5Evidence::note("expected", {"export note and fullness rule printed", "identity, scope, routing, order, can-go, secondsPerLoad, allowance",
                                  "user rear limit marked user setting", "no x/y printed"});
}

TEST_CASE("U-RP-06/synthetic: every byte of the whole report is plain ASCII, whatever the identities, groups and findings hold" * doctest::test_suite("m5")) {
    auto plainAscii = [](const string& text) {
        return all_of(text.begin(), text.end(), [](char character) {
            const auto code = static_cast<unsigned char>(character);
            return character == '\n' || (code >= 0x20 && code <= 0x7e);
        });
    };
    // Non-ASCII, quotes, tabs and line breaks in the group, lane, source version and run identity; goods
    // left over and a too-tall unit so every section has content; the optional policy ON for its header.
    vector<Atom> atoms = fx30Units("u", 40);
    atoms.push_back(unitOf("tall", "A", 48, 40, 120, 500));
    for (Atom& atom : atoms) atom.group = "gr\xc3\xbcppe\t\"quoted\"\r\nnext";
    M5LaneDateInput input = laneWith(atoms, 1);
    input.lane = {"20\xe2\x80\x93" "28", "25\x7f" "74", "TL"};
    input.sourceVersion = "v\xe2\x82\xac" "1";
    M5PolicyProfile profile = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    profile.profileId = "pr\xc3\xb6" "file";
    const M5LaneOutcome outcome = plan(input, profile);
    const string text = renderAscii(buildReportData(outcome, input, &profile, {"SYNTHETIC", 20, "run \xf0\x9f\x9a\x9a"}));
    CHECK(plainAscii(text));
    CHECK(text.find("3A Run\n") != string::npos);
    CHECK(text.find("3E Summary\n") != string::npos);
    CHECK(text.find("left: ") != string::npos);
    CHECK(plainAscii(renderAscii(buildReportData(outcome, input, nullptr, {}))));
    m5Evidence::note("expected", {"whole report bytes in 0x20..0x7e or newline", "with non-ASCII lane, group, version, profile, run"});
}

TEST_CASE("U-RP-06 X-REPORT-09 the report is plain ASCII whatever the identities hold" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("u", 3);
    atoms[0].group = "gr\xc3\xbcppe, \"quoted\"\nnext";
    atoms[1].group = atoms[2].group = atoms[0].group;
    const M5LaneDateInput input = laneWith(atoms, 1);
    const string text = renderAscii(buildReportData(plan(input, syntheticProfile()), input, nullptr, {}));
    CHECK(all_of(text.begin(), text.end(), [](char character) {
        const auto code = static_cast<unsigned char>(character);
        return character == '\n' || (code >= 0x20 && code <= 0x7e);
    }));
}
