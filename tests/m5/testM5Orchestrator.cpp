#include "doctest.h"
#include "m5Report.hpp"
#include "support/m5Fixtures.hpp"

#include <algorithm>
#include <memory>

using namespace std;
using namespace ob::m5;
using namespace m5Fixtures;

namespace {

size_t countCause(const M5LaneOutcome& outcome, LeftOverCause cause) {
    return static_cast<size_t>(count_if(outcome.leftOver.begin(), outcome.leftOver.end(),
                                        [&](const LeftOver& left) { return left.cause == cause; }));
}

// A method that offers each group a fixed plan of consecutive atoms, so A and B can be compared
// on controlled results (X-RUN-13).
class FixedMethod final : public MethodPlanner {
public:
    FixedMethod(MethodId id, size_t trucks, size_t perTruck) : id_(id), trucks_(trucks), perTruck_(perTruck) {}
    MethodId id() const override { return id_; }
    GroupSupply supplyForGroup(const MethodContext& context, const GroupId& group) const override {
        GroupTrucks plan;
        for (size_t truck = 0; truck < trucks_; ++truck) {
            Truck atoms;
            for (size_t unit = 0; unit < perTruck_; ++unit) atoms.push_back(truck * perTruck_ + unit);
            plan.push_back(atoms);
        }
        GroupSupply supply;
        supply.group = group;
        supply.plans.push_back({});
        for (size_t trucks = 1; trucks <= min(plan.size(), context.maxTrucks); ++trucks) {
            supply.plans.emplace_back(plan.begin(), plan.begin() + static_cast<ptrdiff_t>(trucks));
        }
        return supply;
    }

private:
    MethodId id_;
    size_t trucks_;
    size_t perTruck_;
};

M5LaneOutcome planWith(const M5LaneDateInput& input, const M5PolicyProfile& profile, FakeClock& clock, optional<double> allowance,
                       vector<shared_ptr<const MethodPlanner>> methods, function<void(MethodId)> beforeMethod) {
    const M5RunServices services{clock, allowance, nullopt, std::move(methods), std::move(beforeMethod)};
    return planLaneDate(input, profile, physicalSettings(), services);
}

}  // namespace

TEST_CASE("X-RUN-02 X-CG-12 time out before any result: no result, every good kept" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 40), 2);
    FakeClock clock;
    const M5LaneOutcome outcome =
        planWith(input, syntheticProfile(), clock, 10.0, {}, [&](MethodId) { clock.advance(11.0); });
    CHECK(outcome.kind == OutcomeKind::TimeoutNoIncumbent);
    CHECK(outcome.stop == StopReason::BudgetExhausted);
    CHECK_FALSE(outcome.hasCandidate);
    CHECK(countCause(outcome, LeftOverCause::NoResult) == 40);
}

TEST_CASE("X-RUN-04 the careful method gets only what the quick one left" * doctest::test_suite("m5")) {
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
}

TEST_CASE("X-RUN-11 per-load maximum and amortised average are different figures" * doctest::test_suite("m5")) {
    vector<double> loads(9, 1.0);
    loads.push_back(11.0);
    const LoadTimeSummary summary = summarizeLoadTimes(loads);
    CHECK(summary.totalSeconds == 20.0);
    CHECK(summary.loads == 10);
    CHECK(*summary.averageSeconds == 2.0);
    CHECK(*summary.maximumSeconds == 11.0);
    CHECK_FALSE(*summary.maximumSeconds <= 10.0);
    CHECK(*summary.averageSeconds <= 10.0);
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
    vector<shared_ptr<const MethodPlanner>> methods{make_shared<FixedMethod>(MethodId::PatternRepeat, 3, 20),
                                                    make_shared<FixedMethod>(MethodId::RollingBlocks, 2, 30)};
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
    CHECK(production.find("98.0% of the trailer's weight, or rear inches <= 14.0 (default") != string::npos);
    CHECK(production.find("reserved trucks per lane and ship date > 20") != string::npos);
    CHECK(production.find("3A Run") != string::npos);
    CHECK(production.find("3E Summary") != string::npos);
    const M5PolicyProfile synthetic = withAcceptance(profile, 40000.0, 3300.0);
    const string withPolicy = renderAscii(buildReportData(plan(input, synthetic), input, &synthetic, {"SYNTHETIC", 20, "fixture"}));
    CHECK(withPolicy.find("weight > 40000.0 lb OR cube > 3300.0 cu ft") != string::npos);
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
