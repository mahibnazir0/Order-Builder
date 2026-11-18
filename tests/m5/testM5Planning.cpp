// M5 planning through its direct seam (v7 0.4): small fixtures call planLaneDate, never M4 routing.
#include "doctest.h"
#include "m5Commit.hpp"
#include "m5FloorSearch.hpp"
#include "m5Integration.hpp"
#include "m5Ledger.hpp"
#include "m5Methods.hpp"
#include "m5Repair.hpp"
#include "m5Report.hpp"
#include "m5Solver.hpp"
#include "m5Validation.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Fixtures.hpp"
#include "support/m5Oracles.hpp"
#include "../m4SmallDay.hpp"
#include "../runTempFolder.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>

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

size_t shippedWithPrefix(const set<string>& shipped, const string& prefix);

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

namespace {

// FX-30's pattern by hand: 20 in deep rows on 630 in give 31 positions, 45,000 / 1,500 lb gives 30.
constexpr size_t kFx30PatternSize = min<size_t>(630 / 20, 45000 / 1500);

M5PolicyProfile patternOnlyProfile() {
    M5PolicyProfile profile = syntheticProfile();
    profile.method = MethodPolicy::PatternOnly;
    return profile;
}

multiset<size_t> truckSizes(const M5LaneOutcome& outcome) {
    multiset<size_t> sizes;
    if (outcome.selected) {
        for (const TruckPlan& truck : outcome.selected->trucks) sizes.insert(truck.atoms.size());
    }
    return sizes;
}

// How many times each atom ends: on a truck or left over. Every atom must end exactly once.
vector<int> endsPerAtom(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    vector<int> ends(input.atoms.size(), 0);
    if (outcome.selected) {
        for (const TruckPlan& truck : outcome.selected->trucks) {
            for (const size_t atom : truck.atoms) ++ends[atom];
        }
    }
    for (const LeftOver& left : outcome.leftOver) ++ends[left.atom];
    return ends;
}

}  // namespace

TEST_CASE("U-PB-01/synthetic: goods for exactly N full trucks give N pattern copies and no remainder" * doctest::test_suite("m5")) {
    REQUIRE(kFx30PatternSize == 30);
    for (const size_t trucks : {size_t{1}, size_t{2}, size_t{3}}) {
        INFO(trucks << " trucks of goods on " << trucks << " reserved");
        const M5LaneDateInput input = laneWith(fx30Units("u", kFx30PatternSize * trucks), trucks);
        CHECK(bestSingleClassLoad(input, 0, kDefaultTrailer, physicalSettings(), 100) == static_cast<int>(kFx30PatternSize));
        CHECK(patternCopies(static_cast<long long>(input.atoms.size()), 30, static_cast<long long>(trucks)) == static_cast<long long>(trucks));
        for (const M5PolicyProfile& profile : {patternOnlyProfile(), syntheticProfile()}) {
            const M5LaneOutcome outcome = plan(input, profile);
            requireInvariants(outcome, input);
            REQUIRE(outcome.selected);
            CHECK(outcome.kind == OutcomeKind::CompletedWithDispatch);
            CHECK(outcome.finalValidationPassed);
            const vector<size_t> expected(trucks, kFx30PatternSize);
            CHECK(truckSizes(outcome) == multiset<size_t>(expected.begin(), expected.end()));
            CHECK(outcome.leftOver.empty());
        }
    }
    // Only if reservations allow: 2 trucks of goods on 1 reserved give 1 copy; the other 30 wait over it.
    const M5LaneDateInput short1 = laneWith(fx30Units("u", 60), 1);
    const M5LaneOutcome limited = plan(short1, patternOnlyProfile());
    requireInvariants(limited, short1);
    CHECK(truckSizes(limited) == multiset<size_t>{30});
    CHECK(countCause(limited, LeftOverCause::OverReservation) == 30);
    m5Evidence::note("expected", {"pattern 30 = min(630/20, 45,000/1,500)", "N = 1, 2, 3: N trucks of 30, nothing left",
                                  "60 on 1 reserved: 1 copy, 30 over the reservation"});
}

TEST_CASE("U-PB-02/synthetic: N trucks plus a part truck: N copies, the part goes to the remainder exactly once" * doctest::test_suite("m5")) {
    // 65 = 2 x 30 + 5: two pattern copies; the 5 are remainder, packed once on the third slot.
    const M5LaneDateInput input = laneWith(fx30Units("u", 65), 3);
    CHECK(patternCopies(65, 30, 3) == 2);
    for (const M5PolicyProfile& profile : {patternOnlyProfile(), syntheticProfile()}) {
        const M5LaneOutcome outcome = plan(input, profile);
        requireInvariants(outcome, input);
        REQUIRE(outcome.selected);
        CHECK(truckSizes(outcome) == multiset<size_t>{5, 30, 30});
        CHECK(outcome.leftOver.empty());
        const vector<int> ends = endsPerAtom(outcome, input);
        CHECK(all_of(ends.begin(), ends.end(), [](int end) { return end == 1; }));
    }
    // 95 on 3: three copies use every slot; the part of 5 is remainder with no slot, left once.
    const M5LaneDateInput full = laneWith(fx30Units("u", 95), 3);
    CHECK(patternCopies(95, 30, 3) == 3);
    const M5LaneOutcome noRoom = plan(full, patternOnlyProfile());
    requireInvariants(noRoom, full);
    CHECK(truckSizes(noRoom) == multiset<size_t>{30, 30, 30});
    CHECK(countCause(noRoom, LeftOverCause::OverReservation) == 5);
    const vector<int> ends = endsPerAtom(noRoom, full);
    CHECK(all_of(ends.begin(), ends.end(), [](int end) { return end == 1; }));
    m5Evidence::note("expected", {"65 on 3: copies 2 (30 + 30), remainder 5 on the third truck, each unit once",
                                  "95 on 3: copies 3, remainder 5 left over once"});
}

TEST_CASE("U-PB-03/synthetic: less than one truck of goods forms no pattern; all go to the remainder" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 20), 2);
    CHECK(bestSingleClassLoad(input, 0, kDefaultTrailer, physicalSettings(), 100) == 30);
    CHECK(patternCopies(20, 30, 2) == 0);
    for (const M5PolicyProfile& profile : {patternOnlyProfile(), syntheticProfile()}) {
        const M5LaneOutcome outcome = plan(input, profile);
        requireInvariants(outcome, input);
        REQUIRE(outcome.selected);
        // The remainder packer puts all 20 on one truck; none is lost or left.
        CHECK(truckSizes(outcome) == multiset<size_t>{20});
        CHECK(outcome.leftOver.empty());
        const vector<int> ends = endsPerAtom(outcome, input);
        CHECK(all_of(ends.begin(), ends.end(), [](int end) { return end == 1; }));
    }
    m5Evidence::note("expected", {"pattern 30 > stock 20: 0 copies", "all 20 packed as remainder on 1 truck"});
}

TEST_CASE("X-PAT-01/synthetic: a pattern of 5 on goods 0, 4, 5, 15, 17 makes 0, 0, 1, 3, 3 copies and leaves 0, 4, 0, 0, 2; no extra truck at exact multiples" * doctest::test_suite("m5")) {
    const vector<long long> goods{0, 4, 5, 15, 17}, expectedCopies{0, 0, 1, 3, 3}, expectedLeft{0, 4, 0, 0, 2};
    // A 126 x 100 in unit stands 5 deep in 630 in: the pattern holds 5. Ten slots never limit.
    const vector<size_t> expectedTrucks{0, 1, 1, 3, 4};
    for (size_t index = 0; index < goods.size(); ++index) {
        INFO("goods " << goods[index]);
        const long long copies = patternCopies(goods[index], 5, 10);
        CHECK(copies == expectedCopies[index]);
        CHECK(goods[index] - copies * 5 == expectedLeft[index]);
        vector<Atom> units;
        for (long long unit = 0; unit < goods[index]; ++unit) units.push_back(unitOf("u" + to_string(100 + unit), "A", 126, 100, 100, 1000));
        const M5LaneDateInput input = laneWith(units, 10);
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        CHECK(shippedCount(outcome) == static_cast<size_t>(goods[index]));
        const size_t trucks = outcome.selected ? outcome.selected->trucks.size() : 0;
        CHECK(trucks == expectedTrucks[index]);
        if (!outcome.selected) continue;
        requireInvariants(outcome, input);
        size_t fullCopies = 0;
        for (const TruckPlan& truck : outcome.selected->trucks) fullCopies += truck.atoms.size() == 5 ? 1 : 0;
        CHECK(static_cast<long long>(fullCopies) == expectedCopies[index]);
    }
    m5Evidence::note("expected", {"copies 0, 0, 1, 3, 3", "left over 0, 4, 0, 0, 2", "trucks 0, 1, 1, 3, 4 (15 makes exactly 3)"});
}

TEST_CASE("X-PAT-03/synthetic: an empty pattern, an all-zero pattern and a pattern needing a product with no stock make zero copies and use nothing" * doctest::test_suite("m5")) {
    CHECK(patternCopies(17, 0, 10) == 0);
    CHECK(patternCopies(0, 5, 10) == 0);
    CHECK(patternCopies(17, 5, 0) == 0);
    CHECK(mixedPatternCopies({}, {}, 10) == 0);
    CHECK(mixedPatternCopies({5, 4}, {0, 0}, 10) == 0);
    const vector<long long> stock{5, 0}, perCopy{2, 1};
    const long long copies = mixedPatternCopies(stock, perCopy, 10);
    CHECK(copies == 0);
    for (size_t product = 0; product < stock.size(); ++product) CHECK(stock[product] - copies * perCopy[product] == stock[product]);
    CHECK_THROWS_AS(patternCopies(-1, 5, 1), invalid_argument);
    CHECK_THROWS_AS(mixedPatternCopies({5}, {2, 1}, 10), invalid_argument);
    m5Evidence::note("expected", {"empty, all-zero, no-stock: 0 copies", "stock untouched", "negative stock and mismatched pattern refused"});
}

TEST_CASE("X-PAT-04/synthetic: 2^31 units on a pattern of 64 make 33,554,432 copies; past the range is refused, never wrapped" * doctest::test_suite("m5")) {
    const long long units = 1LL << 31;
    const long long copies = patternCopies(units, 64, 1LL << 40);
    CHECK(copies == 33'554'432);
    CHECK(copies * 64 == units);
    CHECK(patternCopies(units, 64, 1000) == 1000);
    CHECK_THROWS_AS(patternCopies(numeric_limits<long long>::max(), 64, 1), out_of_range);
    m5Evidence::note("expected", {"33,554,432 copies", "LLONG_MAX: out_of_range"});
}

TEST_CASE("X-CG-10/synthetic: no goods gives zero bins; reservations with no eligible goods give zero trucks and use no reservation" * doctest::test_suite("m5")) {
    ScalarModel empty;
    empty.capacity = 10;
    const ScalarResult none = solveCuttingStock(empty, ScalarLimits());
    REQUIRE(none.packing.has_value());
    CHECK(none.packing->bins.empty());
    CHECK(none.impossible.empty());
    CHECK(none.evidence.lowerBound == 0);
    const M5LaneOutcome noGoods = plan(laneWith({}, 3), syntheticProfile());
    CHECK(shippedCount(noGoods) == 0);
    CHECK((!noGoods.selected || noGoods.selected->trucks.empty()));
    // Ten units due five days after the departure, pull-forward off: none is eligible on 3 slots.
    vector<Atom> later;
    for (int unit = 0; unit < 10; ++unit) later.push_back(fx30("late" + to_string(100 + unit), "A", 0, dayAfter(5)));
    const M5LaneDateInput input = laneWith(later, 3);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(shippedCount(outcome) == 0);
    CHECK((!outcome.selected || outcome.selected->trucks.empty()));
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 10);
    CHECK(outcome.leftOver.size() == 10);
    CHECK_FALSE(outcome.hasCandidate);
    m5Evidence::note("expected", {"no goods: 0 bins, bound 0", "3 slots, 10 not eligible: 0 trucks, 10 left not eligible"});
}

TEST_CASE("X-CG-14/synthetic: a result of 2 with a claimed bound of 3, a bound from other goods, or no bound: refused or shown N/A, never a clipped zero gap" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 60), 2);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    REQUIRE(outcome.selected);
    REQUIRE(outcome.selected->evidence.incumbent == 2);
    CandidatePlan overBound = *outcome.selected;
    overBound.evidence.lowerBound = 3;
    overBound.evidence.proof = ProofStatus::Unproven;
    const ValidationReport overReport = validateFinal(overBound, snapshot, syntheticProfile(), physicalSettings());
    CHECK(overReport.metadata == SectionStatus::Fail);
    CHECK_FALSE(overReport.allPassed());
    CHECK(gapText(2, 3).find("0 trucks") == string::npos);
    CHECK(gapText(2, 3).rfind("-1 truck", 0) == 0);

    CandidatePlan otherGoods = *outcome.selected;
    otherGoods.evidence.lowerBound = 2;
    otherGoods.evidence.proof = ProofStatus::ProvenOptimal;
    otherGoods.evidence.boundModel = "count bound of a different goods set";
    const ValidationReport otherReport = validateFinal(otherGoods, snapshot, syntheticProfile(), physicalSettings());
    CHECK(otherReport.metadata == SectionStatus::Fail);

    M5LaneOutcome noBound = outcome;
    noBound.selected->evidence.lowerBound.reset();
    noBound.selected->evidence.proof = ProofStatus::Unproven;
    const M5ReportData data = buildReportData(noBound, input, nullptr, {});
    CHECK(data.countEvidence.find("bound N/A") != string::npos);
    CHECK(gapText(2, nullopt) == "N/A (no valid bound)");
    const ValidationReport noBoundReport = validateFinal(*noBound.selected, snapshot, syntheticProfile(), physicalSettings());
    CHECK(noBoundReport.metadata == SectionStatus::Pass);
    m5Evidence::note("expected", {"bound 3 over result 2: metadata FAIL", "proof on another model's bound: FAIL", "no bound: N/A, not proven"});
}

TEST_CASE("X-PAT-11/synthetic: two 60 x 60 x 100 in loads in a 100 x 100 x 100 in trailer: one fits at 72 percent cube; cube never decides" * doctest::test_suite("m5")) {
    const TrailerCapability trailer{100.0, 100.0, 100.0, 45000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput input = laneWith({unitOf("a", "A", 60, 60, 100, 100), unitOf("b", "A", 60, 60, 100, 100)}, 1, trailer);
    CHECK((2.0 * 60 * 60 * 100) / (100.0 * 100 * 100) == doctest::Approx(0.72));
    CHECK_FALSE(checkTruck(input, {0, 1}, trailer, physicalSettings()).fits);
    CHECK(checkTruck(input, {0}, trailer, physicalSettings()).fits);
    const M5LaneOutcome one = plan(input, syntheticProfile());
    requireInvariants(one, input);
    CHECK(shippedCount(one) == 1);
    CHECK(countCause(one, LeftOverCause::OverReservation) == 1);
    const M5LaneDateInput twoSlots = laneWith(input.atoms, 2, trailer);
    const M5LaneOutcome two = plan(twoSlots, syntheticProfile());
    REQUIRE(two.selected);
    CHECK(two.selected->trucks.size() == 2);
    m5Evidence::note("expected", {"cube 72%", "one per truck: 1 slot ships 1 and leaves 1; 2 slots use 2 trucks"});
}

TEST_CASE("X-PAT-12/synthetic: a 60 x 100 x 100 in load fits a 100 x 60 x 100 in trailer turned; every copy records the turn" * doctest::test_suite("m5")) {
    const TrailerCapability trailer{100.0, 60.0, 100.0, 45000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput single = laneWith({unitOf("a", "A", 100, 60, 100, 100)}, 1, trailer);
    CHECK(checkTruck(single, {0}, trailer, physicalSettings()).fits);
    const M5LaneDateInput input = laneWith({unitOf("a", "A", 100, 60, 100, 100), unitOf("b", "A", 100, 60, 100, 100),
                                            unitOf("c", "A", 100, 60, 100, 100)},
                                           3, trailer);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    REQUIRE(outcome.selected->trucks.size() == 3);
    for (const TruckPlan& truck : outcome.selected->trucks) {
        CHECK(truck.layout.turnedItems == 1);
        REQUIRE(truck.layout.rows.size() == 1);
        REQUIRE(truck.layout.rows.front().placements.size() == 1);
        CHECK(truck.layout.rows.front().placements.front().turned);
        CHECK(checkStoredRows(input, truck.atoms, truck.layout, trailer, physicalSettings().floor).fits);
    }
    m5Evidence::note("expected", {"fits turned", "3 copies, each truck records 1 turned item in its stored row"});
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

TEST_CASE("X-PAT-14/synthetic: limits W = 10 and V = 10, two items of W 6 and V 1, then swapped: never on one truck either way" * doctest::test_suite("m5")) {
    // Scalar two-limit model, acceptance off (x = y = 0: any nonempty bin passes).
    for (const bool weightBinds : {true, false}) {
        const string caseName = weightBinds ? "W 6, V 1" : "W 1, V 6";
        INFO(caseName);
        AcceptedModel model;
        model.weight = weightBinds ? vector<double>{6, 6} : vector<double>{1, 1};
        model.cube = weightBinds ? vector<double>{1, 1} : vector<double>{6, 6};
        model.weightCapacity = 10;
        model.cubeCapacity = 10;
        model.bins = 2;
        const AcceptedResult result = solveAccepted(model);
        CHECK(result.allGoods == ProofStatus::ProvenOptimal);
        REQUIRE(result.allGoodsPacking.has_value());
        CHECK(result.allGoodsPacking->bins.size() == 2);
        for (const auto& bin : result.allGoodsPacking->bins) CHECK(bin.size() == 1);
    }
    // On the floor: weight 6,000 lb each on a 10,000 lb trailer, then floor 330 in each on 630 in at 1 lb.
    const TrailerCapability trailer{630.0, 100.0, 108.0, 10000.0, kFixtureUnitLoadCap};
    const vector<pair<string, vector<Atom>>> cases{
        {"weight binds", {unitOf("a", "A", 48, 40, 50, 6000), unitOf("b", "A", 48, 40, 50, 6000)}},
        {"floor binds", {unitOf("a", "A", 330, 100, 50, 1), unitOf("b", "A", 330, 100, 50, 1)}}};
    for (const auto& [name, atoms] : cases) {
        INFO(name);
        const M5LaneDateInput input = laneWith(atoms, 2, trailer);
        CHECK_FALSE(checkTruck(input, {0, 1}, trailer, physicalSettings()).fits);
        CHECK(checkTruck(input, {0}, trailer, physicalSettings()).fits);
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        requireInvariants(outcome, input);
        REQUIRE(outcome.selected);
        CHECK(outcome.selected->trucks.size() == 2);
        for (const TruckPlan& truck : outcome.selected->trucks) CHECK(truck.atoms.size() == 1);
    }
    m5Evidence::note("expected", {"scalar W/V both ways: 2 bins of 1", "weight-bound and floor-bound lanes: 2 trucks of 1"});
}

namespace {

// One oversized unit beside a fitting one on two slots: it is left as Unfittable, on no truck, and the
// report names the cause; the fitting unit ships.
void requireRejectedAndReported(const Atom& oversized) {
    const M5LaneDateInput input = laneWith({oversized, unitOf("fine", "A", 48, 40, 50, 500)}, 2);
    CHECK_FALSE(checkTruck(input, {0}, kDefaultTrailer, physicalSettings()).fits);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(shippedIds(outcome, input) == set<string>{"fine"});
    CHECK(causeOf(outcome, input, oversized.id) == LeftOverCause::Unfittable);
    CHECK(outcome.leftOver.size() == 1);
    const string text = renderAscii(buildReportData(outcome, input, nullptr, {}));
    CHECK(text.find("left: fits no reserved trailer: 1.00 unit loads") != string::npos);
}

}  // namespace

TEST_CASE("U-PB-09/synthetic: a product taller than the trailer is rejected and reported, never placed" * doctest::test_suite("m5")) {
    // Ceiling 108 in: 108.01 in is over by the 0.01 in step; 120 in is plainly over.
    requireRejectedAndReported(unitOf("tall", "A", 48, 40, 120, 500));
    requireRejectedAndReported(unitOf("tallByStep", "A", 48, 40, 108.01, 500));
    const M5LaneDateInput atCeiling = laneWith({unitOf("atCeiling", "A", 48, 40, 108, 500)}, 1);
    CHECK(shippedCount(plan(atCeiling, syntheticProfile())) == 1);
    m5Evidence::note("expected", {"120 in and 108.01 in: Unfittable, on no truck, reported", "108 in ships"});
}

TEST_CASE("U-PB-10/synthetic: a single unit load heavier than the trailer limit is rejected and reported" * doctest::test_suite("m5")) {
    requireRejectedAndReported(unitOf("heavy", "A", 48, 40, 50, 46000));
    requireRejectedAndReported(unitOf("heavyByStep", "A", 48, 40, 50, 45000.01));
    const M5LaneDateInput atLimit = laneWith({unitOf("atLimit", "A", 48, 40, 50, 45000)}, 1);
    CHECK(shippedCount(plan(atLimit, syntheticProfile())) == 1);
    m5Evidence::note("expected", {"46,000 lb and 45,000.01 lb: Unfittable, on no truck, reported", "45,000 lb ships"});
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

namespace {
LinkedDateBenchmark date08Linked(ob::PastDueOrder order) {
    M5LaneDateInput first = laneWith(fx30Units("old-", 30, "A", 11), 0);
    M5LaneDateInput later = laneWith(fx30Units("now-", 30, "A", 0, dayAfter(4)), 1);
    later.departure = dayAfter(4);
    later.pastDueOrder = order;
    for (Atom& atom : later.atoms) atom.components[0].offerKind = OfferKind::Required;
    FakeClock clock;
    return benchmarkLinkedDates({first, later}, syntheticProfile(), physicalSettings(), clock);
}
}  // namespace

TEST_CASE("X-DATE-08/ageFirst: carried overdue P11 keeps its age and takes the D+4 truck" * doctest::test_suite("m5")) {
    const LinkedDateBenchmark linked = date08Linked(ob::PastDueOrder::AgeFirst);
    REQUIRE(linked.outcomes.size() == 2);
    CHECK(linked.outcomes[0].reservedTrucks == 0);
    CHECK(linked.inputs[1].atoms.size() == 60);
    const set<string> shipped = shippedIds(linked.outcomes[1], linked.inputs[1]);
    CHECK(shippedWithPrefix(shipped, "old-") == 30);
    CHECK(shippedWithPrefix(shipped, "now-") == 0);
    CHECK(linked.outcomes[1].leftOver.size() == 30);
    for (const Atom& atom : linked.inputs[1].atoms) {
        if (atom.id.rfind("old-", 0) == 0) {
            CHECK(atom.components[0].dueDate == kD);
            CHECK(atom.components[0].priority == 11);
        }
    }
    m5Evidence::note("expected", {"30 P11 carried D to D+4 with age intact", "AgeFirst ships P11, leaves new P0"});
}

TEST_CASE("X-DATE-08/priorityFirst: current P0 beats carried overdue P11 on D+4" * doctest::test_suite("m5")) {
    const LinkedDateBenchmark linked = date08Linked(ob::PastDueOrder::PriorityFirst);
    REQUIRE(linked.outcomes.size() == 2);
    const set<string> shipped = shippedIds(linked.outcomes[1], linked.inputs[1]);
    CHECK(shippedWithPrefix(shipped, "old-") == 0);
    CHECK(shippedWithPrefix(shipped, "now-") == 30);
    CHECK(linked.outcomes[1].leftOver.size() == 30);
    for (const Atom& atom : linked.inputs[1].atoms) {
        if (atom.id.rfind("old-", 0) == 0) CHECK(atom.components[0].dueDate == kD);
    }
    m5Evidence::note("expected", {"30 P11 carried D to D+4 with age intact", "PriorityFirst ships new P0, leaves P11"});
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

TEST_CASE("X-E2E-07/inputOrderABC X-E2E-07/inputOrderPermuted: two reserved, groups A, B, C: B + C win by brute force, whatever the input order" * doctest::test_suite("m5")) {
    vector<Atom> atoms{fx30("a0", "A", 0)};
    atoms = concat(atoms, fx30Units("a11-", 29, "A", 11));
    atoms = concat(atoms, fx30Units("b0-", 20, "B", 0));
    atoms = concat(atoms, fx30Units("b11-", 10, "B", 11));
    atoms = concat(atoms, fx30Units("c0-", 19, "C", 0));
    atoms = concat(atoms, fx30Units("c11-", 11, "C", 11));
    const M5LaneDateInput input = laneWith(atoms, 2);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedOfGroup(outcome, input, "B") == 30);
    CHECK(shippedOfGroup(outcome, input, "C") == 30);
    CHECK(shippedOfGroup(outcome, input, "A") == 0);

    // Brute force over every pair of groups: P0 units first, then P11 units (M4's priority rule).
    const map<string, pair<int, int>> byGroup{{"A", {1, 29}}, {"B", {20, 10}}, {"C", {19, 11}}};
    pair<int, int> best{0, 0};
    set<string> bestPair;
    for (const string first : {"A", "B", "C"}) {
        for (const string second : {"A", "B", "C"}) {
            if (first >= second) continue;
            const pair<int, int> total{byGroup.at(first).first + byGroup.at(second).first, byGroup.at(first).second + byGroup.at(second).second};
            if (total > best) {
                best = total;
                bestPair = {first, second};
            }
        }
    }
    CHECK(bestPair == set<string>{"B", "C"});
    CHECK(best.first == 39);
    CHECK(best.second == 21);
    int p0 = 0, p11 = 0;
    for (const TruckPlan& truck : outcome.selected->trucks) {
        for (const size_t atom : truck.atoms) (input.atoms[atom].components[0].priority == 0 ? p0 : p11) += 1;
    }
    CHECK(p0 == 39);
    CHECK(p11 == 21);

    vector<Atom> reversed(atoms.rbegin(), atoms.rend());
    const M5LaneDateInput permuted = laneWith(reversed, 2);
    const M5LaneOutcome permutedOutcome = plan(permuted, syntheticProfile());
    CHECK(shippedIds(permutedOutcome, permuted) == shippedIds(outcome, input));
    vector<Atom> rotated(atoms.begin() + 30, atoms.end());
    rotated.insert(rotated.end(), atoms.begin(), atoms.begin() + 30);
    const M5LaneDateInput rotatedInput = laneWith(rotated, 2);
    CHECK(shippedIds(plan(rotatedInput, syntheticProfile()), rotatedInput) == shippedIds(outcome, input));
    m5Evidence::note("expected", {"A 1 P0 + 29 P11, B 20 P0 + 10 P11, C 19 P0 + 11 P11", "B + C win: 39 P0 + 21 P11", "reversed and rotated group order: the same goods"});
}

TEST_CASE("X-RUN-05/synthetic: an early low-priority block cannot starve urgent goods" * doctest::test_suite("m5")) {
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
    // Both P0 trucks ship, and the P11 trucks are at most 19.
    size_t p0Trucks = 0, p11Trucks = 0;
    for (const TruckPlan& truck : outcome.selected->trucks) {
        (truck.group == "B" ? p0Trucks : p11Trucks) += 1;
    }
    CHECK(p0Trucks == 2);
    CHECK(p11Trucks <= 19);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 30);
    m5Evidence::note("expected", {"21 reserved; A 20 trucks of P11 listed first, B 2 trucks of P0", "both P0 trucks ship, at most 19 P11", "the first block does not lock out urgent goods"});
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

TEST_CASE("U-RS-04 X-STATE-05/synthetic: running M5 twice from one hand-over gives the same plan and leaves the hand-over alone" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 40, "A", 3), fx30Units("b", 40, "B", 3)), 2);
    const M5LaneDateInput handOver = input;
    const M5LaneOutcome first = plan(input, syntheticProfile());
    const M5LaneOutcome second = plan(input, syntheticProfile());
    REQUIRE(first.selected);
    REQUIRE(second.selected);
    CHECK(first.kind == second.kind);
    CHECK(first.selected->trucks.size() == second.selected->trucks.size());
    for (size_t truck = 0; truck < first.selected->trucks.size(); ++truck) {
        CHECK(first.selected->trucks[truck].atoms == second.selected->trucks[truck].atoms);
        CHECK(first.selected->trucks[truck].group == second.selected->trucks[truck].group);
    }
    CHECK(shippedCount(first) == shippedCount(second));
    CHECK(shippedIds(first, input) == shippedIds(second, input));
    CHECK(first.leftOver.size() == second.leftOver.size());
    REQUIRE(input.atoms.size() == handOver.atoms.size());
    CHECK(input.pool.slots.size() == handOver.pool.slots.size());
    for (size_t atom = 0; atom < input.atoms.size(); ++atom) {
        CHECK(input.atoms[atom].id == handOver.atoms[atom].id);
        CHECK(input.atoms[atom].unitLoads == handOver.atoms[atom].unitLoads);
    }
    m5Evidence::note("expected", {"two runs, one hand-over: same trucks, goods and left-overs", "the hand-over is unchanged after the first run"});
}

TEST_CASE("X-STATE-01/synthetic: A and B give the same result in either order or alone" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 75), 3);
    const M5LaneDateInput untouched = input;
    M5PolicyProfile patternOnly = syntheticProfile(), blocksOnly = syntheticProfile();
    patternOnly.method = MethodPolicy::PatternOnly;
    blocksOnly.method = MethodPolicy::BlocksOnly;
    const M5LaneOutcome aAlone = plan(input, patternOnly);
    const M5LaneOutcome bAlone = plan(input, blocksOnly);
    const set<string> alone = shippedIds(aAlone, input);
    CHECK(shippedIds(bAlone, input) == alone);
    CHECK(shippedIds(plan(input, syntheticProfile()), input) == alone);
    CHECK(alone.size() == 75);

    const auto recordOf = [](const M5LaneOutcome& outcome, MethodId method) {
        const auto found = find_if(outcome.methods.begin(), outcome.methods.end(), [&](const MethodRecord& record) { return record.method == method; });
        REQUIRE(found != outcome.methods.end());
        return *found;
    };
    const auto planInOrder = [&](const vector<shared_ptr<const MethodPlanner>>& order) {
        FakeClock clock;
        M5RunServices services(clock);
        services.methods = order;
        return planLaneDate(input, syntheticProfile(), physicalSettings(), services);
    };
    const shared_ptr<const MethodPlanner> methodA = make_shared<PatternPlanner>(), methodB = make_shared<BlockPlanner>();
    const M5LaneOutcome aThenB = planInOrder({methodA, methodB});
    const M5LaneOutcome bThenA = planInOrder({methodB, methodA});
    for (const MethodId method : {MethodId::PatternRepeat, MethodId::RollingBlocks}) {
        const MethodRecord forward = recordOf(aThenB, method), backward = recordOf(bThenA, method);
        CHECK(forward.ran);
        CHECK(backward.ran);
        CHECK(forward.validated);
        CHECK(backward.validated);
        CHECK(forward.unitLoads == backward.unitLoads);
        CHECK(forward.trucks == backward.trucks);
        CHECK(forward.unitLoads == 75.0);
        CHECK(forward.trucks == 3);
    }
    CHECK(shippedIds(aThenB, input) == alone);
    CHECK(shippedIds(bThenA, input) == alone);
    REQUIRE(aThenB.selected);
    REQUIRE(bThenA.selected);
    REQUIRE(aAlone.selected);
    CHECK(aThenB.selected->trucks.size() == aAlone.selected->trucks.size());
    CHECK(bThenA.selected->trucks.size() == aAlone.selected->trucks.size());
    for (size_t atom = 0; atom < input.atoms.size(); ++atom) CHECK(input.atoms[atom].id == untouched.atoms[atom].id);
    m5Evidence::note("expected", {"A alone, B alone, A then B, B then A: the same 75 goods on 3 trucks", "each method's own record is the same in either order"});
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

namespace {

// Acceptance OFF: every truck ships, each with its own CL-03 fullness label, none withheld for minimum load.
void requireNoneWithheld(const M5LaneOutcome& outcome, const M5LaneDateInput& input, size_t trucks, size_t units) {
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == trucks);
    CHECK(shippedCount(outcome) == units);
    CHECK(countCause(outcome, LeftOverCause::AcceptanceWithheld) == 0);
    REQUIRE(outcome.fullness.size() == trucks);
    set<size_t> labelled;
    for (const FullnessLabel& label : outcome.fullness) labelled.insert(label.truck);
    CHECK(labelled.size() == trucks);
    for (const TruckPlan& truck : outcome.selected->trucks) {
        for (const size_t atom : truck.atoms) CHECK(input.atoms[atom].group == truck.group);
    }
}

}  // namespace

TEST_CASE("U-AC-02/productionProfile: with acceptance OFF the light second truck of five ships and every truck is labelled" * doctest::test_suite("m5")) {
    const M5PolicyProfile production = syntheticProfile();
    REQUIRE_FALSE(production.acceptance.has_value());
    const M5LaneDateInput input = laneWith(fx30Units("u", 130), 5);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan candidate;
    size_t next = 0;
    for (const size_t carried : {30, 10, 30, 30, 30}) {
        TruckPlan truck;
        truck.slot = candidate.trucks.size();
        truck.group = "A";
        for (size_t unit = 0; unit < carried; ++unit) truck.atoms.push_back(next++);
        candidate.trucks.push_back(truck);
    }
    storeRows(candidate, input);
    candidate.validation = validateFinal(candidate, snapshot, production, physicalSettings());
    CHECK(candidate.validation->allPassed());
    CHECK(candidate.validation->acceptance != SectionStatus::Fail);
    CHECK(candidate.validation->findings.empty());
    string error;
    CHECK(buildDelta(candidate, snapshot, production, &error).has_value());
    const vector<FullnessLabel> labels =
        evaluateFullness(candidate, snapshot, remainingAfterSelected(snapshot, candidate), production, physicalSettings());
    REQUIRE(labels.size() == 5);
    CHECK(labels[1].state == FullnessState::NotFull);
    for (const size_t full : {0, 2, 3, 4}) CHECK(labels[full].state == FullnessState::Full);
    requireNoneWithheld(plan(input, production), input, 5, 130);
    m5Evidence::note("expected", {"30,10,30,30,30 all pass and ship", "truck 2 labelled NOT FULL, the rest FULL", "0 withheld"});
}

TEST_CASE("U-AC-03/productionProfile: with acceptance OFF each group's light truck ships on its own, labelled, unmixed" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 40, "A"), fx30Units("b", 40, "B")), 4);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireNoneWithheld(outcome, input, 4, 80);
    CHECK(shippedOfGroup(outcome, input, "A") == 40);
    CHECK(shippedOfGroup(outcome, input, "B") == 40);
    m5Evidence::note("expected", {"A 30 + 10 and B 30 + 10 ship", "4 labels", "no mixed truck", "0 withheld"});
}

namespace {

// U-AC-06: one hard constraint broken on the otherwise valid 30-unit FX-30 truck (45,000 lb, 3,750 cu ft),
// which meets x = 40,000 lb / y = 3,000 cu ft on both branches. The validator rejects it, names the
// broken section, and nothing is committed.
void requireHardConstraintWins(const M5LaneDateInput& input, const M5PolicyProfile& profile, const string& section) {
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan candidate = oneTruck(laneWith(fx30Units("u", 30), 1), 30);
    candidate.validation = validateFinal(candidate, snapshot, profile, physicalSettings());
    const ValidationReport& report = *candidate.validation;
    CHECK_FALSE(report.allPassed());
    if (profile.acceptance) CHECK(report.acceptance == SectionStatus::Pass);
    const bool named = any_of(report.findings.begin(), report.findings.end(),
                              [&](const ValidationFinding& finding) { return finding.section == section; });
    CHECK(named);
    for (const ValidationFinding& finding : report.findings) CHECK(finding.section != "acceptance");
    string error;
    CHECK_FALSE(buildDelta(candidate, snapshot, profile, &error).has_value());
    CHECK_FALSE(error.empty());
    m5Evidence::note("expected", {"rejected before dispatch", "section " + section, "nothing committed"});
}

M5PolicyProfile uac06Policy() { return withAcceptance(syntheticProfile(), 40000.0, 3000.0); }

void requireControlPasses(const M5PolicyProfile& profile) {
    const M5LaneDateInput control = laneWith(fx30Units("u", 30), 1);
    const DemandSnapshot snapshot = *buildSnapshot(control).snapshot;
    CandidatePlan full = oneTruck(control, 30);
    full.validation = validateFinal(full, snapshot, profile, physicalSettings());
    CHECK(full.validation->allPassed());
    string error;
    CHECK(buildDelta(full, snapshot, profile, &error).has_value());
}

}  // namespace

TEST_CASE("U-AC-06/weightLimit40k: a 45,000 lb truck on a 40,000 lb trailer is rejected although it meets x/y" * doctest::test_suite("m5")) {
    requireControlPasses(uac06Policy());
    requireHardConstraintWins(laneWith(fx30Units("u", 30), 1, {630.0, 100.0, 108.0, 40000.0, kFixtureUnitLoadCap}), uac06Policy(),
                              "physical");
}

TEST_CASE("U-AC-06/dimension: a 600 in load on a 599 in floor is rejected although it meets x/y" * doctest::test_suite("m5")) {
    requireControlPasses(uac06Policy());
    requireHardConstraintWins(laneWith(fx30Units("u", 30), 1, {599.0, 100.0, 108.0, 45000.0, kFixtureUnitLoadCap}), uac06Policy(),
                              "physical");
}

TEST_CASE("U-AC-06/dueAfterPullOff: a unit due after D with pull-forward off is rejected although the truck meets x/y" * doctest::test_suite("m5")) {
    requireControlPasses(uac06Policy());
    M5LaneDateInput dueLater = laneWith(fx30Units("u", 30), 1);
    REQUIRE_FALSE(dueLater.dates.pullForward);
    dueLater.atoms[4].components[0].dueDate = dayAfter(2);
    requireHardConstraintWins(dueLater, uac06Policy(), "eligibility");
}

TEST_CASE("U-AC-06/unavailable: a component not available on D is rejected although the truck meets x/y" * doctest::test_suite("m5")) {
    requireControlPasses(uac06Policy());
    M5LaneDateInput unavailable = laneWith(fx30Units("u", 30), 1);
    unavailable.atoms[3].components[0].availableDate = dayAfter(1);
    requireHardConstraintWins(unavailable, uac06Policy(), "eligibility");
}

TEST_CASE("U-AC-06/control: the unchanged 30-unit candidate passes the validator and commits once" * doctest::test_suite("m5")) {
    for (const M5PolicyProfile& profile : {uac06Policy(), syntheticProfile()}) {
        INFO("acceptance " << (profile.acceptance ? "on" : "off"));
        const M5LaneDateInput control = laneWith(fx30Units("u", 30), 1);
        const DemandSnapshot snapshot = *buildSnapshot(control).snapshot;
        CandidatePlan full = oneTruck(control, 30);
        full.validation = validateFinal(full, snapshot, profile, physicalSettings());
        CHECK(full.validation->allPassed());
        CHECK(full.validation->findings.empty());
        CHECK(full.validation->physical == SectionStatus::Pass);
        string error;
        const optional<CommitDelta> delta = buildDelta(full, snapshot, profile, &error);
        REQUIRE(delta.has_value());
        CommitLedger ledger;
        OperationalState state{snapshot.id, snapshot.input.pool.version, {}, 0, 1};
        CHECK(ledger.applyIfCurrent(*delta, snapshot, state) == ApplyStatus::Applied);
        CHECK(state.consumedAtoms.size() == 30);
        CHECK(state.slotsUsed == 1);
    }
    m5Evidence::note("expected", {"control passes (acceptance on and off)", "30 units, 1 reservation committed"});
}

TEST_CASE("U-AC-06/mixedGroups: a truck mixing two do-not-mix groups is rejected although it meets x/y" * doctest::test_suite("m5")) {
    requireControlPasses(uac06Policy());
    M5LaneDateInput mixed = laneWith(fx30Units("u", 30), 1);
    mixed.atoms[5].group = "B";
    requireHardConstraintWins(mixed, uac06Policy(), "groups");
}

TEST_CASE("U-AC-06/zeroReservations: a truck with no reservation is rejected although it meets x/y" * doctest::test_suite("m5")) {
    requireControlPasses(uac06Policy());
    requireHardConstraintWins(laneWith(fx30Units("u", 30), 0), uac06Policy(), "reservations");
}

TEST_CASE("U-AC-06/productionProfile: with acceptance OFF every hard-constraint break still rejects the truck" * doctest::test_suite("m5")) {
    const M5PolicyProfile production = syntheticProfile();
    requireControlPasses(production);
    requireHardConstraintWins(laneWith(fx30Units("u", 30), 1, {630.0, 100.0, 108.0, 40000.0, kFixtureUnitLoadCap}), production,
                              "physical");
    requireHardConstraintWins(laneWith(fx30Units("u", 30), 1, {599.0, 100.0, 108.0, 45000.0, kFixtureUnitLoadCap}), production,
                              "physical");
    M5LaneDateInput dueLater = laneWith(fx30Units("u", 30), 1);
    dueLater.atoms[4].components[0].dueDate = dayAfter(2);
    requireHardConstraintWins(dueLater, production, "eligibility");
    M5LaneDateInput mixed = laneWith(fx30Units("u", 30), 1);
    mixed.atoms[5].group = "B";
    requireHardConstraintWins(mixed, production, "groups");
    requireHardConstraintWins(laneWith(fx30Units("u", 30), 0), production, "reservations");
}

namespace {

// U-AC-07: 25 FX-30 units (37,500 lb, 3,125 cu ft, accepted on cube) lose one to a repair: 36,000 lb and
// exactly 3,000 cu ft, which fails both strict branches.
struct RepairedTruck {
    M5LaneDateInput input = laneWith(fx30Units("u", 25), 1);
    DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan accepted = oneTruck(input, 25);

    explicit RepairedTruck(const M5PolicyProfile& profile) {
        accepted.validation = validateFinal(accepted, snapshot, profile, physicalSettings());
        REQUIRE(accepted.validation->allPassed());
    }
};

double weightOf(const M5LaneDateInput& input, const TruckPlan& truck) {
    double weight = 0.0;
    for (const size_t atom : truck.atoms) weight += input.atoms[atom].weightLb;
    return weight;
}

double cubeCuFtOf(const M5LaneDateInput& input, const TruckPlan& truck) {
    double cube = 0.0;
    for (const size_t atom : truck.atoms) cube += input.atoms[atom].floor.cubeCubicIn;
    return cube / 1728.0;
}

// Every atom once: on a truck or in leftOver.
void requireReconciled(const CandidatePlan& plan, size_t atoms) {
    vector<int> seen(atoms, 0);
    for (const TruckPlan& truck : plan.trucks) {
        for (const size_t atom : truck.atoms) seen[atom] += 1;
    }
    for (const LeftOver& left : plan.leftOver) seen[left.atom] += 1;
    CHECK(count(seen.begin(), seen.end(), 1) == static_cast<ptrdiff_t>(atoms));
}

RepairRun repairRemovingOne(const RepairedTruck& fixture, const M5PolicyProfile& profile, const StopCheck& stop) {
    const auto comparator = makeComparator(ComparatorKind::M4Urgency, tieOrderFor(fixture.snapshot));
    const UrgencyDomain domain(fixture.snapshot);
    const RepairProposal removeOne = [](CandidatePlan& work, size_t) {
        if (work.trucks.empty() || work.trucks[0].atoms.size() < 25) return false;
        CandidateEditor(work).removeToLeftOver(0, work.trucks[0].atoms.back(), LeftOverCause::OverReservation);
        return true;
    };
    return runRepair(fixture.accepted, fixture.snapshot, profile, physicalSettings(), *comparator, domain, removeOne, 1, stop);
}

}  // namespace

TEST_CASE("U-AC-07/mutation: removing one of 25 units fails acceptance at 3,000 cu ft; stale flags cannot authorise it" * doctest::test_suite("m5")) {
    const M5PolicyProfile policy = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    const RepairedTruck fixture(policy);
    CHECK(weightOf(fixture.input, fixture.accepted.trucks[0]) == 37500.0);
    CHECK(cubeCuFtOf(fixture.input, fixture.accepted.trucks[0]) == 3125.0);
    CHECK(fixture.accepted.validation->acceptance == SectionStatus::Pass);

    CandidatePlan mutated = fixture.accepted;
    const uint64_t before = mutated.revision;
    CandidateEditor(mutated).removeToLeftOver(0, 24, LeftOverCause::OverReservation);
    CHECK(mutated.revision == before + 1);
    CHECK_FALSE(mutated.validation.has_value());
    CHECK(weightOf(fixture.input, mutated.trucks[0]) == 36000.0);
    CHECK(cubeCuFtOf(fixture.input, mutated.trucks[0]) == 3000.0);
    requireReconciled(mutated, 25);

    storeRows(mutated, fixture.input);  // as runRepair re-stores the edited truck's rows
    // The pre-mutation report put back by a fault cannot authorise the changed truck.
    CandidatePlan stale = mutated;
    stale.validation = fixture.accepted.validation;
    string error;
    CHECK_FALSE(buildDelta(stale, fixture.snapshot, policy, &error).has_value());
    const ValidationReport recomputed = validateFinal(mutated, fixture.snapshot, policy, physicalSettings());
    CHECK(recomputed.acceptance == SectionStatus::Fail);
    CHECK(recomputed.conservation == SectionStatus::Pass);
    CHECK(recomputed.physical == SectionStatus::Pass);
    for (const ValidationFinding& finding : recomputed.findings) CHECK(finding.section == "acceptance");

    // Through the repair loop: the mutated copy fails, so the independently valid 25-unit incumbent stays.
    const RepairRun run = repairRemovingOne(fixture, policy, StopCheck());
    REQUIRE(run.incumbent);
    CHECK(run.incumbent->trucks[0].atoms.size() == 25);
    REQUIRE(run.incumbent->validation);
    CHECK(run.incumbent->validation->revision == run.incumbent->revision);
    CHECK(validateFinal(*run.incumbent, fixture.snapshot, policy, physicalSettings()).allPassed());
    m5Evidence::note("expected", {"25: 37,500 lb, 3,125 cu ft accepted", "24: 36,000 lb, 3,000 cu ft rejected",
                                  "stale report refused", "valid incumbent kept"});
}

TEST_CASE("U-AC-07/interruptAfterMutation: a stop right after the mutation publishes no stale acceptance" * doctest::test_suite("m5")) {
    const M5PolicyProfile policy = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    const RepairedTruck fixture(policy);
    bool mutated = false;
    const auto comparator = makeComparator(ComparatorKind::M4Urgency, tieOrderFor(fixture.snapshot));
    const UrgencyDomain domain(fixture.snapshot);
    const RepairProposal removeOne = [&](CandidatePlan& work, size_t) {
        CandidateEditor(work).removeToLeftOver(0, work.trucks[0].atoms.back(), LeftOverCause::OverReservation);
        mutated = true;
        return true;
    };
    const StopCheck stopAfterMutation = [&] { return mutated; };
    const RepairRun run =
        runRepair(fixture.accepted, fixture.snapshot, policy, physicalSettings(), *comparator, domain, removeOne, 4, stopAfterMutation);
    CHECK(mutated);
    CHECK(run.iterations == 1);
    // The 24-unit copy was never validated, so it is discarded; only the valid start can stand.
    for (const uint64_t revision : run.checkedRevisions) CHECK(revision == fixture.accepted.revision);
    REQUIRE(run.incumbent);
    CHECK(run.incumbent->trucks[0].atoms.size() == 25);
    CHECK(run.incumbent->leftOver.empty());
    REQUIRE(run.incumbent->validation);
    CHECK(run.incumbent->validation->revision == run.incumbent->revision);
    string error;
    CHECK(buildDelta(*run.incumbent, fixture.snapshot, policy, &error).has_value());
    m5Evidence::note("expected", {"stopped after the mutation", "unvalidated copy discarded", "25-unit incumbent only"});
}

TEST_CASE("U-AC-07/productionProfile: a repair clears stale validation and fullness labels; both are recomputed" * doctest::test_suite("m5")) {
    const M5PolicyProfile production = syntheticProfile();
    const RepairedTruck fixture(production);
    const vector<FullnessLabel> before = evaluateFullness(fixture.accepted, fixture.snapshot,
                                                          remainingAfterSelected(fixture.snapshot, fixture.accepted), production,
                                                          physicalSettings());
    REQUIRE(before.size() == 1);
    CHECK(before[0].rearInches == doctest::Approx(130.0));
    CHECK(before[0].candidateRevision == fixture.accepted.revision);

    CandidatePlan mutated = fixture.accepted;
    CandidateEditor(mutated).removeToLeftOver(0, 24, LeftOverCause::OverReservation);
    CHECK_FALSE(mutated.validation.has_value());
    CHECK(before[0].candidateRevision != mutated.revision);
    storeRows(mutated, fixture.input);  // as runRepair re-stores the edited truck's rows
    const vector<FullnessLabel> after =
        evaluateFullness(mutated, fixture.snapshot, remainingAfterSelected(fixture.snapshot, mutated), production, physicalSettings());
    REQUIRE(after.size() == 1);
    CHECK(after[0].candidateRevision == mutated.revision);
    CHECK(after[0].rearInches == doctest::Approx(150.0));
    CHECK(after[0].weightPercent == doctest::Approx(80.0));
    CHECK(after[0].state == FullnessState::NotFull);
    const ValidationReport recomputed = validateFinal(mutated, fixture.snapshot, production, physicalSettings());
    CHECK(recomputed.allPassed());
    CHECK(recomputed.acceptance == SectionStatus::NotApplicable);
    CHECK(recomputed.revision == mutated.revision);
    requireReconciled(mutated, 25);
}

namespace {

// U-AC-08: the same 20-unit candidate (30,000 lb, 2,500 cu ft) under A (y 2,400) and B (y 3,000).
struct ProfileRun {
    bool accepted = false;
    size_t shipped = 0;
    string profileKey;
};

ProfileRun runUnder(double y, const string& profileId = "fx-synthetic", int revision = 1) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 20), 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    M5PolicyProfile profile = withAcceptance(syntheticProfile(), 40000.0, y);
    profile.profileId = profileId;
    profile.profileRevision = revision;
    ProfileRun run;
    const ValidationReport report = validateFinal(oneTruck(input, 20), snapshot, profile, physicalSettings());
    run.accepted = report.acceptance == SectionStatus::Pass;
    run.profileKey = report.profileKey;
    const M5LaneOutcome outcome = plan(input, profile);
    run.shipped = shippedCount(outcome);
    return run;
}

}  // namespace

TEST_CASE("U-AC-08/ABAinProcess: A, B, A in one process give accepted, rejected, accepted" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 20), 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const CandidatePlan candidate = oneTruck(input, 20);
    const M5PolicyProfile a = withAcceptance(syntheticProfile(), 40000.0, 2400.0);
    const M5PolicyProfile b = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    vector<bool> verdicts;
    vector<size_t> shipped;
    vector<string> keys;
    for (const M5PolicyProfile* profile : {&a, &b, &a}) {
        const ValidationReport report = validateFinal(candidate, snapshot, *profile, physicalSettings());
        CHECK(report.physical == SectionStatus::Pass);
        verdicts.push_back(report.allPassed());
        keys.push_back(report.profileKey);
        shipped.push_back(shippedCount(plan(input, *profile)));
    }
    CHECK(verdicts == vector<bool>{true, false, true});
    CHECK(shipped == vector<size_t>{20, 0, 20});
    CHECK(keys[0] == keys[2]);
    CHECK(keys[0] != keys[1]);
    m5Evidence::note("expected", {"true", "false", "true"});
}

namespace {

// One profile run as its own process prints these lines; the parent reads them back.
void printFreshProcessRun(double y) {
    // The first route budget of a fresh process takes identity 1 from the process-wide counter.
    const RouteSearchBudget firstBudget("freshProcess", 1, 1);
    const ProfileRun run = runUnder(y);
    cout << "freshProcess.firstRouteIdentity=" << firstBudget.identity() << "\n"
         << "freshProcess.accepted=" << (run.accepted ? 1 : 0) << "\n"
         << "freshProcess.shipped=" << run.shipped << "\n"
         << "freshProcess.profileKey=" << run.profileKey << "\n";
}

struct ChildRun {
    int status = -1;
    map<string, string> fields;
};

ChildRun runInFreshProcess(const string& childCase) {
    const filesystem::path output = testTemp::runTempFolder() / ("freshProcess_" + childCase + ".txt");
    const auto quoted = [](const string& text) { return "\"" + text + "\""; };
    string command = quoted(filesystem::path(OB_TESTS_PATH).make_preferred().string()) + " " +
                     quoted("-tc=m5 fresh-process child " + childCase + ":*") + " > " + quoted(output.string()) + " 2>&1";
#ifdef _WIN32
    // cmd.exe strips the outer quotes of a line that starts with one; give it a pair to strip.
    command = "\"" + command + "\"";
#endif
    ChildRun child;
    child.status = system(command.c_str());
    ifstream lines(output);
    const string prefix = "freshProcess.";
    for (string line; getline(lines, line);) {
        if (line.rfind(prefix, 0) != 0) continue;
        const size_t equals = line.find('=');
        if (equals != string::npos) child.fields[line.substr(prefix.size(), equals - prefix.size())] = line.substr(equals + 1);
    }
    return child;
}

}  // namespace

// Children of U-AC-08/freshProcess: each runs one profile and prints its result. Run alone they check the
// same outcome, so they are never empty.
TEST_CASE("m5 fresh-process child A: y 2,400 cu ft" * doctest::test_suite("m5")) {
    printFreshProcessRun(2400.0);
    CHECK(runUnder(2400.0).accepted);
}

TEST_CASE("m5 fresh-process child B: y 3,000 cu ft" * doctest::test_suite("m5")) {
    printFreshProcessRun(3000.0);
    CHECK_FALSE(runUnder(3000.0).accepted);
}

TEST_CASE("U-AC-08/freshProcess: each profile run in a fresh process equals the in-process A, B, A results" * doctest::test_suite("m5")) {
    // M5 has process-wide state: the route-budget identity counter (nextRouteIdentity, m5FloorSearch.cpp;
    // search of src/m5* and include/m5* in pr11b-report.txt). So each run is a real second process.
    const ProfileRun inA = runUnder(2400.0), inB = runUnder(3000.0), inAAgain = runUnder(2400.0);
    const RouteSearchBudget probe("inProcess", 1, 1);
    REQUIRE(probe.identity() > 1);
    const ChildRun freshA = runInFreshProcess("A"), freshB = runInFreshProcess("B"), freshAAgain = runInFreshProcess("A");
    vector<string> verdicts;
    for (const auto& [inProcess, fresh] : {pair{&inA, &freshA}, pair{&inB, &freshB}, pair{&inAAgain, &freshAAgain}}) {
        CHECK(fresh->status == 0);
        REQUIRE(fresh->fields.size() == 4);
        CHECK(fresh->fields.at("firstRouteIdentity") == "1");
        CHECK(fresh->fields.at("accepted") == (inProcess->accepted ? "1" : "0"));
        CHECK(fresh->fields.at("shipped") == to_string(inProcess->shipped));
        CHECK(fresh->fields.at("profileKey") == inProcess->profileKey);
        verdicts.push_back(fresh->fields.at("accepted"));
    }
    CHECK(verdicts == vector<string>{"1", "0", "1"});
    CHECK(inA.shipped == 20);
    CHECK(inB.shipped == 0);
    CHECK(freshA.fields.at("profileKey") == freshAAgain.fields.at("profileKey"));
    CHECK(freshA.fields.at("profileKey") != freshB.fields.at("profileKey"));
    m5Evidence::note("expected", {"true", "false", "true", "three child processes of ob_tests, identities start at 1"});
}

TEST_CASE("U-AC-08/sameIdRevision: revising one profile id's thresholds re-decides acceptance" * doctest::test_suite("m5")) {
    const ProfileRun first = runUnder(2400.0, "customer-same", 1);
    const ProfileRun revised = runUnder(3000.0, "customer-same", 2);
    const ProfileRun restored = runUnder(2400.0, "customer-same", 3);
    CHECK(first.accepted);
    CHECK_FALSE(revised.accepted);
    CHECK(restored.accepted);
    CHECK(first.shipped == 20);
    CHECK(revised.shipped == 0);
    CHECK(restored.shipped == 20);
    CHECK(first.profileKey != revised.profileKey);
    CHECK(first.profileKey != restored.profileKey);
    m5Evidence::note("expected", {"true", "false", "true", "keys differ by revision"});
}

namespace {

// X-STATE-09: twenty FX-30 units (30,000 lb, 2,500 cu ft), one group, one reservation, planned from a
// fresh snapshot each time. ON is x = 40,000 lb / y = 3,000 cu ft, OFF has no acceptance section.
struct StateRun {
    size_t shipped = 0;
    size_t trucks = 0;
    size_t withheld = 0;
    string report;
    string profileKey;
    bool balanced = false;
};

StateRun stateRun(const M5PolicyProfile& profile) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 20), 1);
    const M5LaneOutcome outcome = plan(input, profile);
    requireInvariants(outcome, input);
    StateRun run;
    run.shipped = shippedCount(outcome);
    run.trucks = outcome.selected ? outcome.selected->trucks.size() : 0;
    run.withheld = countCause(outcome, LeftOverCause::AcceptanceWithheld);
    run.report = renderAscii(buildReportData(outcome, input, &profile, {}));
    run.profileKey = profile.key();
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan accounted = outcome.selected ? *outcome.selected : CandidatePlan{};
    accounted.leftOver = outcome.leftOver;
    run.balanced = reconcile(snapshot, accounted).balanced;
    return run;
}

M5PolicyProfile acceptanceOn(MethodPolicy method) {
    M5PolicyProfile profile = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    profile.method = method;
    return profile;
}

M5PolicyProfile acceptanceOff(MethodPolicy method) {
    M5PolicyProfile profile = syntheticProfile();
    profile.method = method;
    return profile;
}

// ON, OFF, ON on fresh snapshots: ON withholds all 20, OFF ships all 20 on one checked truck with the
// export-after-M5 statement and no trace of the earlier thresholds, ON withholds again identically.
void requireOnOffOn(const M5PolicyProfile& on, const M5PolicyProfile& off) {
    const StateRun first = stateRun(on), second = stateRun(off), third = stateRun(on);
    for (const StateRun* withheld : {&first, &third}) {
        CHECK(withheld->shipped == 0);
        CHECK(withheld->trucks == 0);
        CHECK(withheld->withheld == 20);
        CHECK(withheld->balanced);
        CHECK(withheld->report.find("optional policy ON: weight > 40000.0 lb OR cube > 3000.0 cu ft (strict)") != string::npos);
    }
    CHECK(second.shipped == 20);
    CHECK(second.trucks == 1);
    CHECK(second.withheld == 0);
    CHECK(second.balanced);
    CHECK(second.report.find("at export, after M5") != string::npos);
    CHECK(second.report.find("withheld by minimum-load policy") == string::npos);
    CHECK(second.report.find("40000.0") == string::npos);
    CHECK(second.report.find("3000.0") == string::npos);
    CHECK(second.report.find("optional policy ON") == string::npos);
    CHECK(third.report == first.report);
    CHECK(third.profileKey == first.profileKey);
    CHECK(second.profileKey != first.profileKey);
}

}  // namespace

TEST_CASE("X-STATE-09/methodA: ON withholds all 20, OFF ships all 20 on one truck, ON withholds again (Method A alone)" * doctest::test_suite("m5")) {
    requireOnOffOn(acceptanceOn(MethodPolicy::PatternOnly), acceptanceOff(MethodPolicy::PatternOnly));
    m5Evidence::note("expected", {"ON: 20 withheld, 0 shipped", "OFF: 20 shipped on 1 truck, report says at export, after M5", "ON: 20 withheld again"});
}

TEST_CASE("X-STATE-09/methodB: ON withholds all 20, OFF ships all 20 on one truck, ON withholds again (Method B alone)" * doctest::test_suite("m5")) {
    requireOnOffOn(acceptanceOn(MethodPolicy::BlocksOnly), acceptanceOff(MethodPolicy::BlocksOnly));
    m5Evidence::note("expected", {"ON: 20 withheld, 0 shipped", "OFF: 20 shipped on 1 truck", "ON: 20 withheld again"});
}

TEST_CASE("X-STATE-09/remainder: twenty units are below one pattern truck, so A's remainder packing and the allocation both obey ON, OFF, ON" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 20), 1);
    const M5LaneOutcome offOutcome = plan(input, syntheticProfile());
    REQUIRE(offOutcome.selected);
    const auto methodA = find_if(offOutcome.methods.begin(), offOutcome.methods.end(), [](const MethodRecord& record) { return record.method == MethodId::PatternRepeat; });
    REQUIRE(methodA != offOutcome.methods.end());
    CHECK(methodA->unitLoads == 20.0);
    CHECK(methodA->trucks == 1);
    const M5LaneDateInput oneFullTruck = laneWith(fx30Units("u", 30), 1);
    CHECK(shippedCount(plan(oneFullTruck, syntheticProfile())) == 30);
    requireOnOffOn(acceptanceOn(MethodPolicy::SequentialAThenB), acceptanceOff(MethodPolicy::SequentialAThenB));
    m5Evidence::note("expected", {"20 units: no whole pattern truck of 30, all 20 are A's remainder", "ON: 20 withheld; OFF: 20 shipped; ON: 20 withheld"});
}

TEST_CASE("X-STATE-09/repair: a truck failing ON is dropped by repair, the same truck stands under OFF, ON drops it again" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 20), 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const UrgencyDomain domain(snapshot);
    const auto comparator = makeComparator(ComparatorKind::M4Urgency);
    const M5PolicyProfile on = acceptanceOn(MethodPolicy::SequentialAThenB), off = acceptanceOff(MethodPolicy::SequentialAThenB);
    const RepairProposal dropTruck = [](CandidatePlan& work, size_t) {
        if (work.trucks.empty()) return false;
        CandidateEditor(work).removeTruck(0, LeftOverCause::OverReservation);
        return true;
    };
    for (const M5PolicyProfile* profile : {&on, &off, &on}) {
        CandidatePlan start = oneTruck(input, 20);
        start.validation = validateFinal(start, snapshot, *profile, physicalSettings());
        const bool startPassed = start.validation->allPassed();
        const RepairRun run = runRepair(start, snapshot, *profile, physicalSettings(), *comparator, domain, dropTruck, 3, StopCheck());
        REQUIRE(run.incumbent.has_value());
        CHECK(run.incumbent->validation->allPassed());
        CHECK(run.incumbent->validation->profileKey == profile->key());
        if (profile == &off) {
            CHECK(startPassed);
            CHECK(run.incumbent->trucks.size() == 1);
            CHECK(run.incumbent->leftOver.empty());
        } else {
            CHECK_FALSE(startPassed);
            CHECK(run.incumbent->trucks.empty());
            CHECK(run.incumbent->leftOver.size() == 20);
        }
        CHECK(reconcile(snapshot, *run.incumbent).balanced);
    }
    m5Evidence::note("expected", {"ON: the 20-unit truck fails x/y, repair leaves nothing shipped, 20 left over", "OFF: the truck passes unchanged", "ON: as the first run"});
}

TEST_CASE("X-STATE-09/finalValidation: the final validator applies x and y only while the section is present" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 20), 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const CandidatePlan candidate = oneTruck(input, 20);
    const M5PolicyProfile on = acceptanceOn(MethodPolicy::SequentialAThenB), off = acceptanceOff(MethodPolicy::SequentialAThenB);
    const ValidationReport first = validateFinal(candidate, snapshot, on, physicalSettings());
    const ValidationReport second = validateFinal(candidate, snapshot, off, physicalSettings());
    const ValidationReport third = validateFinal(candidate, snapshot, on, physicalSettings());
    CHECK(first.physical == SectionStatus::Pass);
    CHECK(first.acceptance == SectionStatus::Fail);
    CHECK_FALSE(first.allPassed());
    CHECK(second.physical == SectionStatus::Pass);
    CHECK(second.acceptance != SectionStatus::Fail);
    CHECK(second.allPassed());
    CHECK(third.acceptance == SectionStatus::Fail);
    CHECK_FALSE(third.allPassed());
    CHECK(first.profileKey == third.profileKey);
    CHECK(second.profileKey != first.profileKey);
    CHECK(third.findings.size() == first.findings.size());
    m5Evidence::note("expected", {"ON: acceptance Fail", "OFF: acceptance not run, plan passes", "ON: acceptance Fail again"});
}

TEST_CASE("X-STATE-09/sameIdRevision: one profile id revised ON, OFF, ON is read afresh each time and the identity records the revision" * doctest::test_suite("m5")) {
    const auto revision = [](M5PolicyProfile profile, int number) {
        profile.profileId = "customer-same";
        profile.profileRevision = number;
        return profile;
    };
    const M5PolicyProfile one = revision(acceptanceOn(MethodPolicy::SequentialAThenB), 1);
    const M5PolicyProfile two = revision(acceptanceOff(MethodPolicy::SequentialAThenB), 2);
    const M5PolicyProfile three = revision(acceptanceOn(MethodPolicy::SequentialAThenB), 3);
    const StateRun first = stateRun(one), second = stateRun(two), third = stateRun(three);
    CHECK(first.shipped == 0);
    CHECK(first.withheld == 20);
    CHECK(second.shipped == 20);
    CHECK(second.withheld == 0);
    CHECK(third.shipped == 0);
    CHECK(third.withheld == 20);
    CHECK(first.profileKey != second.profileKey);
    CHECK(first.profileKey != third.profileKey);
    CHECK(second.profileKey != third.profileKey);
    for (const StateRun* run : {&first, &second, &third}) CHECK(run->profileKey.find("customer-same") != string::npos);
    CHECK(second.report.find("40000.0") == string::npos);
    // A validation made under revision 1 is stale under revision 3 although the settings are the same.
    const M5LaneDateInput input = laneWith(fx30Units("u", 20), 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan checked = oneTruck(input, 20);
    checked.validation = validateFinal(checked, snapshot, one, physicalSettings());
    CHECK(validationCurrent(checked, snapshot, one));
    CHECK_FALSE(validationCurrent(checked, snapshot, three));
    m5Evidence::note("expected", {"rev 1 ON: 20 withheld", "rev 2 OFF: 20 shipped", "rev 3 ON: 20 withheld", "three different identities, same id"});
}

namespace {

// Children of X-STATE-09/freshProcess: each runs one profile and prints its result.
void printStateRun(const M5PolicyProfile& profile) {
    const StateRun run = stateRun(profile);
    cout << "freshProcess.shipped=" << run.shipped << "\n"
         << "freshProcess.trucks=" << run.trucks << "\n"
         << "freshProcess.withheld=" << run.withheld << "\n"
         << "freshProcess.profileKey=" << run.profileKey << "\n"
         << "freshProcess.reportHash=" << digestOf(run.report) << "\n";
}

}  // namespace

TEST_CASE("m5 fresh-process child StateOn: acceptance x 40,000 y 3,000" * doctest::test_suite("m5")) {
    printStateRun(acceptanceOn(MethodPolicy::SequentialAThenB));
    CHECK(stateRun(acceptanceOn(MethodPolicy::SequentialAThenB)).shipped == 0);
}

TEST_CASE("m5 fresh-process child StateOff: acceptance section absent" * doctest::test_suite("m5")) {
    printStateRun(acceptanceOff(MethodPolicy::SequentialAThenB));
    CHECK(stateRun(acceptanceOff(MethodPolicy::SequentialAThenB)).shipped == 20);
}

TEST_CASE("X-STATE-09/freshProcess: ON, OFF, ON each in a fresh process give the same results as one process running them in turn" * doctest::test_suite("m5")) {
    const StateRun inOn = stateRun(acceptanceOn(MethodPolicy::SequentialAThenB));
    const StateRun inOff = stateRun(acceptanceOff(MethodPolicy::SequentialAThenB));
    const StateRun inOnAgain = stateRun(acceptanceOn(MethodPolicy::SequentialAThenB));
    const ChildRun freshOn = runInFreshProcess("StateOn"), freshOff = runInFreshProcess("StateOff"), freshOnAgain = runInFreshProcess("StateOn");
    for (const auto& [inProcess, fresh] : {pair{&inOn, &freshOn}, pair{&inOff, &freshOff}, pair{&inOnAgain, &freshOnAgain}}) {
        CHECK(fresh->status == 0);
        REQUIRE(fresh->fields.size() == 5);
        CHECK(fresh->fields.at("shipped") == to_string(inProcess->shipped));
        CHECK(fresh->fields.at("trucks") == to_string(inProcess->trucks));
        CHECK(fresh->fields.at("withheld") == to_string(inProcess->withheld));
        CHECK(fresh->fields.at("profileKey") == inProcess->profileKey);
        CHECK(fresh->fields.at("reportHash") == digestOf(inProcess->report));
    }
    CHECK(freshOn.fields.at("shipped") == "0");
    CHECK(freshOff.fields.at("shipped") == "20");
    CHECK(freshOnAgain.fields.at("reportHash") == freshOn.fields.at("reportHash"));
    CHECK(freshOff.fields.at("reportHash") != freshOn.fields.at("reportHash"));
    m5Evidence::note("expected", {"three child processes of ob_tests: 0, 20, 0 shipped", "reports and profile keys equal the in-process runs"});
}

TEST_CASE("X-STATE-09/malformedOffConfig: an absent acceptance section is OFF; a present malformed one is a configuration error, not OFF" * doctest::test_suite("m5")) {
    nlohmann::json block = nlohmann::json::parse(R"({
        "enabled": true, "profileId": "customer2-production", "profileRevision": 1,
        "routing": {"countMode": "reservedTrucks", "scope": "perLaneShipDate"},
        "secondsPerLoad": 1, "fullness": {"weightFullPercent": 98, "rearInchLimit": 14},
        "methodPolicy": "SequentialAThenB", "methodAShare": 0.6, "blockSize": 19, "scarcityWindow": 2,
        "gapTrucks": 1, "canGoFill": true, "repair": {"maxRounds": 8, "maxMovesPerTruck": 64},
        "penaltyPerUnusedTruck": null})");
    const M5PolicyProfile off = loadProfile(block);
    CHECK_FALSE(off.acceptance.has_value());
    const M5LaneDateInput input = laneWith(fx30Units("u", 20), 1);
    CHECK(shippedCount(plan(input, off)) == 20);
    for (const nlohmann::json& malformed : {nlohmann::json("on"), nlohmann::json::array({40000, 3000}), nlohmann::json::object(),
                                            nlohmann::json{{"weightLb", 40000}}, nlohmann::json{{"weightLb", 40000}, {"cubeCuFt", "3000"}},
                                            nlohmann::json{{"weightLb", -1}, {"cubeCuFt", 3000}}}) {
        nlohmann::json bad = block;
        bad["acceptance"] = malformed;
        INFO(malformed.dump());
        string error;
        try {
            loadProfile(bad);
        } catch (const runtime_error& thrown) {
            error = thrown.what();
        }
        REQUIRE_FALSE(error.empty());
        // The lane-date is not planned as if acceptance were off: every good is left with the configuration reason.
        const M5LaneOutcome refused = configurationIncomplete(input, error);
        CHECK(refused.kind == OutcomeKind::ConfigurationIncomplete);
        CHECK(shippedCount(refused) == 0);
        REQUIRE(refused.leftOver.size() == 20);
        for (const LeftOver& left : refused.leftOver) CHECK(left.cause == LeftOverCause::ConfigurationIncomplete);
        // And the failed read leaves nothing behind: the OFF block still plans afterwards.
        CHECK(shippedCount(plan(input, loadProfile(block))) == 20);
    }
    m5Evidence::note("expected", {"absent section: valid, OFF, 20 ship", "present but malformed: configuration error, nothing planned", "OFF again afterwards: 20 ship"});
}

TEST_CASE("U-AC-02/synthetic: of five trucks the failing second one is withheld; first, middle and last ship" * doctest::test_suite("m5")) {
    // 130 FX-30 units on five slots arranged 30, 10, 30, 30, 30: only the 10-unit truck (15,000 lb,
    // 1,250 cu ft) fails x = 40,000 lb / y = 3,000 cu ft.
    const M5PolicyProfile policy = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    const M5LaneDateInput input = laneWith(fx30Units("u", 130), 5);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan candidate;
    size_t next = 0;
    for (const size_t carried : {30, 10, 30, 30, 30}) {
        TruckPlan truck;
        truck.slot = candidate.trucks.size();
        truck.group = "A";
        for (size_t unit = 0; unit < carried; ++unit) truck.atoms.push_back(next++);
        candidate.trucks.push_back(truck);
    }
    storeRows(candidate, input);
    const ValidationReport report = validateFinal(candidate, snapshot, policy, physicalSettings());
    CHECK(report.acceptance == SectionStatus::Fail);
    set<size_t> failing;
    for (const ValidationFinding& finding : report.findings) {
        CHECK(finding.section == "acceptance");
        REQUIRE(finding.truck.has_value());
        failing.insert(*finding.truck);
    }
    CHECK(failing == set<size_t>{1});

    CandidateEditor(candidate).removeTruck(1, LeftOverCause::AcceptanceWithheld);
    const ValidationReport after = validateFinal(candidate, snapshot, policy, physicalSettings());
    CHECK(after.allPassed());
    CHECK(candidate.trucks.size() == 4);
    size_t shipped = 0;
    for (const TruckPlan& truck : candidate.trucks) shipped += truck.atoms.size();
    CHECK(shipped == 120);
    CHECK(candidate.leftOver.size() == 10);
    for (const LeftOver& left : candidate.leftOver) CHECK(left.cause == LeftOverCause::AcceptanceWithheld);
    m5Evidence::note("expected", {"truck 2 of 5 withheld", "120 ship", "10 withheld"});
}

TEST_CASE("U-AC-03/synthetic: a failing truck in each of two groups fails on its own; groups are never mixed to pass" * doctest::test_suite("m5")) {
    // Groups A and B each hold 40 FX-30 units: one full truck of 30 and one of 10 per group; each 10-unit
    // truck fails, and A + B together (20 units) would still fail, so no mixing could pass either.
    const M5PolicyProfile policy = withAcceptance(syntheticProfile(), 40000.0, 3000.0);
    const M5LaneDateInput input = laneWith(concat(fx30Units("a", 40, "A"), fx30Units("b", 40, "B")), 4);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan candidate;
    for (const auto& [group, first, carried] : vector<tuple<string, size_t, size_t>>{{"A", 0, 30}, {"A", 30, 10}, {"B", 40, 30}, {"B", 70, 10}}) {
        TruckPlan truck;
        truck.slot = candidate.trucks.size();
        truck.group = group;
        for (size_t unit = 0; unit < carried; ++unit) truck.atoms.push_back(first + unit);
        candidate.trucks.push_back(truck);
    }
    storeRows(candidate, input);
    const ValidationReport report = validateFinal(candidate, snapshot, policy, physicalSettings());
    set<size_t> failing;
    for (const ValidationFinding& finding : report.findings) {
        CHECK(finding.section == "acceptance");
        REQUIRE(finding.truck.has_value());
        failing.insert(*finding.truck);
    }
    CHECK(failing == set<size_t>{1, 3});
    CHECK(report.groups == SectionStatus::Pass);

    const M5LaneOutcome outcome = plan(input, policy);
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 2);
    for (const TruckPlan& truck : outcome.selected->trucks) {
        CHECK(truck.atoms.size() == 30);
        for (const size_t atom : truck.atoms) CHECK(input.atoms[atom].group == truck.group);
    }
    CHECK(shippedOfGroup(outcome, input, "A") == 30);
    CHECK(shippedOfGroup(outcome, input, "B") == 30);
    CHECK(countCause(outcome, LeftOverCause::AcceptanceWithheld) == 20);
    m5Evidence::note("expected", {"A: 30 ship, 10 withheld", "B: 30 ship, 10 withheld", "no mixed truck"});
}

TEST_CASE("U-AC-08/productionProfile: rear limits 14, 30, 14 give NOT FULL, FULL by rear, NOT FULL; also by revision of one ID" * doctest::test_suite("m5")) {
    // 31 nonstackable TLD units 20 x 100 x 108 in, 300 lb; the controlled candidate holds 30 (rear 30 in,
    // 9,000 lb) and the 31st, eligible, still fits. Every run builds a fresh snapshot.
    auto labelUnder = [](double limit, const string& profileId, uint32_t revision) {
        vector<Atom> units;
        for (int unit = 0; unit < 31; ++unit) units.push_back(unitOf("t" + to_string(100 + unit), "A", 100, 20, 108, 300));
        const M5LaneDateInput input = laneWith(units, 1);
        const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
        const CandidatePlan candidate = oneTruck(input, 30);
        M5PolicyProfile profile = syntheticProfile();
        profile.profileId = profileId;
        profile.profileRevision = revision;
        profile.fullness.rearInchLimit = limit;
        const vector<FullnessLabel> labels =
            evaluateFullness(candidate, snapshot, remainingAfterSelected(snapshot, candidate), profile, physicalSettings());
        REQUIRE(labels.size() == 1);
        CHECK(labels.front().rearInches == doctest::Approx(30.0));
        CHECK(labels.front().weightPercent == doctest::Approx(9000.0 / 450.0));
        CHECK(labels.front().fitStatus == RemainingFitStatus::Fits);
        CHECK_FALSE(labels.front().byWeight);
        CHECK_FALSE(labels.front().byFloorLimit);
        return pair<FullnessLabel, string>{labels.front(), profile.key()};
    };
    const vector<double> limits{14.0, 30.0, 14.0};
    vector<FullnessState> states, revisedStates;
    vector<string> keys;
    for (const double limit : limits) states.push_back(labelUnder(limit, "fx-synthetic", 1).first.state);
    for (size_t run = 0; run < limits.size(); ++run) {
        const pair<FullnessLabel, string> revised = labelUnder(limits[run], "customer-same", static_cast<uint32_t>(run + 1));
        revisedStates.push_back(revised.first.state);
        CHECK(revised.first.byRearInches == (limits[run] == 30.0));
        keys.push_back(revised.second);
    }
    const vector<FullnessState> expected{FullnessState::NotFull, FullnessState::Full, FullnessState::NotFull};
    CHECK(states == expected);
    CHECK(revisedStates == expected);
    CHECK(set<string>(keys.begin(), keys.end()).size() == 3);
    m5Evidence::note("expected", {"NOT FULL, FULL by rear (30 in), NOT FULL", "same with one profile ID revised 1, 2, 3", "rear 30 in, 9,000 lb"});
}

TEST_CASE("U-FL-01/synthetic: 29 then 30 FX-30 units: NOT FULL with goods left, FULL by weight with none" * doctest::test_suite("m5")) {
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
    CHECK(label.weightPercent == doctest::Approx(100.0));
    // No-fit is not inferred from an empty stock: with the 30th unit gone, nothing is said about the floor.
    CHECK(label.fitStatus == RemainingFitStatus::NotApplicable);
    CHECK_FALSE(label.byRearInches);
    m5Evidence::note("expected", {"29: 43,500 lb, rear 50, the 30th fits: NOT FULL, GoodsRemain", "30: 45,000 lb, rear 30: FULL by weight, NoGoodsRemain"});
}

TEST_CASE("U-FL-02/synthetic: 31 light TLD units: 30 NOT FULL, 31 FULL by rear inches" * doctest::test_suite("m5")) {
    vector<Atom> units;
    for (int unit = 0; unit < 31; ++unit) units.push_back(unitOf("t" + to_string(100 + unit), "A", 100, 20, 108, 300));
    const M5LaneDateInput input = laneWith(units, 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const CandidatePlan thirty = oneTruck(input, 30);
    const FullnessLabel first =
        evaluateFullness(thirty, snapshot, remainingAfterSelected(snapshot, thirty), syntheticProfile(), physicalSettings()).front();
    CHECK(first.state == FullnessState::NotFull);
    CHECK(first.rearInches == doctest::Approx(30.0));
    CHECK(first.fitStatus == RemainingFitStatus::Fits);
    CHECK(first.weightPercent == doctest::Approx(9000.0 / 450.0));
    const CandidatePlan all = oneTruck(input, 31);
    const FullnessLabel second =
        evaluateFullness(all, snapshot, remainingAfterSelected(snapshot, all), syntheticProfile(), physicalSettings()).front();
    CHECK(second.state == FullnessState::Full);
    CHECK(second.byRearInches);
    CHECK_FALSE(second.byWeight);
    CHECK(second.rearInches == doctest::Approx(10.0));
    CHECK(second.weightPercent == doctest::Approx(9300.0 / 450.0));
    m5Evidence::note("expected", {"30: rear 30, 9,000 lb, the 31st fits: NOT FULL", "31: rear 10, 9,300 lb: FULL by rear (<= 14)"});
}

TEST_CASE("U-FL-04/synthetic X-E2E-09 floor-limited needs a remaining display that provably does not fit" * doctest::test_suite("m5")) {
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

    // Rear 30 in and 24,000 lb do not themselves establish FULL: the floor-limit reason is the witness alone.
    CHECK(label.weightPercent == doctest::Approx(24000.0 / 450.0));
    // A fitting same-group control (a 20 in unit) clears the floor-limit reason.
    vector<Atom> control(displays.begin(), displays.begin() + 30);
    control.push_back(unitOf("fits", "A", 100, 20, 50, 100));
    const M5LaneDateInput fitting = laneWith(control, 1);
    const DemandSnapshot fittingSnapshot = *buildSnapshot(fitting).snapshot;
    const CandidatePlan selected = oneTruck(fitting, 30);
    const FullnessLabel cleared = evaluateFullness(selected, fittingSnapshot, remainingAfterSelected(fittingSnapshot, selected),
                                                   syntheticProfile(), physicalSettings())
                                      .front();
    CHECK_FALSE(cleared.byFloorLimit);
    CHECK(cleared.fitStatus == RemainingFitStatus::Fits);
    CHECK(cleared.state == FullnessState::NotFull);
    // Labels never change dispatch: the same trucks ship with or without the 31st display.
    CHECK(shippedCount(plan(input, syntheticProfile())) == 30);
    CHECK(shippedCount(plan(exact, syntheticProfile())) == 30);
    m5Evidence::note("expected", {"31 displays, 30 selected: FULL by floor limit on the proven no-fit witness", "30 sources only: no floor-limit claim", "a fitting control clears it", "dispatch unchanged"});
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

namespace {

// U-FL-09: the residual of a 600 in candidate holds a 40 in unit that cannot fit and a 20 in unit that can.
struct InterruptedFit {
    M5LaneDateInput input;
    CandidatePlan candidate;
};

InterruptedFit interruptedFit(bool nonFittingFirst, double loadedWeightLb = 1000.0) {
    const Atom loaded = unitOf("load", "A", 600, 100, 108, loadedWeightLb);
    const Atom fits = unitOf("fits", "A", 100, 20, 108, 100);
    const Atom tooLong = unitOf("long", "A", 100, 40, 108, 100);
    InterruptedFit made;
    made.input = nonFittingFirst ? laneWith({loaded, tooLong, fits}, 1, kDefaultTrailer, true) : laneWith({loaded, fits, tooLong}, 1, kDefaultTrailer, true);
    made.candidate = oneTruck(made.input, 1);
    return made;
}

// A stop that lets `allowedChecks` fit checks through, then fires.
StopCheck stopAfterChecks(size_t allowedChecks) {
    const auto asked = make_shared<size_t>(0);
    return [asked, allowedChecks] { return (*asked)++ >= allowedChecks; };
}

FullnessLabel labelWithStop(const InterruptedFit& made, const StopCheck& stop) {
    const DemandSnapshot snapshot = *buildSnapshot(made.input).snapshot;
    return evaluateFullness(made.candidate, snapshot, remainingAfterSelected(snapshot, made.candidate), syntheticProfile(), physicalSettings(), stop)
        .front();
}

}  // namespace

TEST_CASE("U-FL-09/completed: stopped after the first class there is no certificate and the fit is Unknown; completed, the fitting 20 in unit gives NOT FULL" * doctest::test_suite("m5")) {
    const InterruptedFit made = interruptedFit(true);
    bool sawUnknown = false;
    for (size_t allowed = 0; allowed < 12; ++allowed) {
        const FullnessLabel label = labelWithStop(made, stopAfterChecks(allowed));
        // Whatever the stop, a floor-limit is never certified while the fitting class is unprobed.
        CHECK_FALSE(label.byFloorLimit);
        CHECK(label.fitStatus != RemainingFitStatus::ProvenNoFit);
        if (label.fitStatus == RemainingFitStatus::Unknown) {
            sawUnknown = true;
            CHECK(label.state == FullnessState::Undetermined);
            CHECK_FALSE(label.byWeight);
            CHECK_FALSE(label.byRearInches);
        }
    }
    CHECK(sawUnknown);
    const FullnessLabel completed = labelWithStop(made, StopCheck());
    CHECK(completed.fitStatus == RemainingFitStatus::Fits);
    CHECK(completed.state == FullnessState::NotFull);
    CHECK_FALSE(completed.byFloorLimit);
    CHECK(completed.bucket == FullnessBucket::GoodsRemain);
    m5Evidence::note("expected", {"stopped before the 20 in class is probed: Unknown, fullness not established, no floor-limit", "completed: the 20 in unit fits, NOT FULL"});
}

TEST_CASE("U-FL-09/reversedProbeOrder: with the fitting class probed first the witness appears at once and no stop ever produces a false no-fit" * doctest::test_suite("m5")) {
    const InterruptedFit made = interruptedFit(false);
    size_t firstFits = 99;
    for (size_t allowed = 0; allowed < 12; ++allowed) {
        const FullnessLabel label = labelWithStop(made, stopAfterChecks(allowed));
        CHECK_FALSE(label.byFloorLimit);
        CHECK(label.fitStatus != RemainingFitStatus::ProvenNoFit);
        if (label.fitStatus == RemainingFitStatus::Fits) firstFits = min(firstFits, allowed);
    }
    CHECK(firstFits < 99);
    // The nonfitting-first order needs more probes to reach the same fitting witness.
    const InterruptedFit nonFittingFirst = interruptedFit(true);
    size_t firstFitsLate = 99;
    for (size_t allowed = 0; allowed < 12; ++allowed) {
        if (labelWithStop(nonFittingFirst, stopAfterChecks(allowed)).fitStatus == RemainingFitStatus::Fits) firstFitsLate = min(firstFitsLate, allowed);
    }
    CHECK(firstFits <= firstFitsLate);
    const FullnessLabel completed = labelWithStop(made, StopCheck());
    CHECK(completed.fitStatus == RemainingFitStatus::Fits);
    CHECK(completed.state == FullnessState::NotFull);
    m5Evidence::note("expected", {"reversed class order: the fitting witness is reached no later than in the other order", "never a no-fit result from an interrupted check"});
}

TEST_CASE("U-FL-09/weightFullControl: a 44,100 lb candidate is FULL by weight even when the fit diagnostic is incomplete; the reasons stay separate" * doctest::test_suite("m5")) {
    const InterruptedFit made = interruptedFit(true, 44100.0);
    const FullnessLabel label = labelWithStop(made, stopAfterChecks(0));
    CHECK(label.byWeight);
    CHECK(label.state == FullnessState::Full);
    CHECK_FALSE(label.byFloorLimit);
    CHECK_FALSE(label.byRearInches);
    CHECK(label.fitStatus == RemainingFitStatus::Unknown);
    const FullnessLabel completed = labelWithStop(made, StopCheck());
    CHECK(completed.byWeight);
    CHECK(completed.state == FullnessState::Full);
    CHECK(completed.fitStatus == RemainingFitStatus::Fits);
    m5Evidence::note("expected", {"incomplete fit check: FULL by weight, fit Unknown, no floor-limit", "completed: FULL by weight, the 20 in unit fits"});
}

TEST_CASE("U-FL-09/budgetIntegration: the fit diagnostic draws on the run's own allowance; an incomplete label does not withhold a validated shipment" * doctest::test_suite("m5")) {
    const InterruptedFit made = interruptedFit(true);
    FakeClock clock;
    M5RunServices services(clock);
    services.allowanceSeconds = 10.0;
    services.afterMethod = [&](MethodId) { clock.advance(100.0); };
    const M5LaneOutcome outcome = planLaneDate(made.input, syntheticProfile(), physicalSettings(), services);
    requireInvariants(outcome, made.input);
    CHECK(outcome.allowanceSeconds == 10.0);
    CHECK(outcome.stop == StopReason::BudgetExhausted);
    REQUIRE(outcome.selected);
    CHECK(outcome.finalValidationPassed);
    CHECK(shippedCount(outcome) >= 1);
    for (const FullnessLabel& label : outcome.fullness) {
        // The allowance was spent before or during the diagnostic: no certificate, nothing withheld because of it.
        CHECK_FALSE((label.fitStatus == RemainingFitStatus::ProvenNoFit && label.byFloorLimit && label.state != FullnessState::Full));
        CHECK(label.profileKey == syntheticProfile().key());
    }
    const M5LaneOutcome relaxed = plan(made.input, syntheticProfile());
    CHECK(shippedCount(relaxed) == shippedCount(outcome));
    m5Evidence::note("expected", {"one declared allowance, spent: the plan is kept and checked", "no new budget for the diagnostic", "the shipment equals the unstopped run's"});
}

namespace {

// FX-CANGO helpers: every input below has pull-forward ON with a 10-day window unless a variant says otherwise.
Atom withDates(Atom atom, const string& due, const string& available = kD) {
    for (Component& component : atom.components) {
        component.dueDate = due;
        component.availableDate = available;
        component.offerKind = due > kD ? OfferKind::CanGo : OfferKind::Required;
    }
    return atom;
}

size_t shippedWithPrefix(const set<string>& shipped, const string& prefix) {
    return static_cast<size_t>(count_if(shipped.begin(), shipped.end(), [&](const string& id) { return id.rfind(prefix, 0) == 0; }));
}

size_t leftOverEntriesOf(const M5LaneOutcome& outcome, const M5LaneDateInput& input, const string& id) {
    return static_cast<size_t>(count_if(outcome.leftOver.begin(), outcome.leftOver.end(),
                                        [&](const LeftOver& left) { return input.atoms[left.atom].id == id; }));
}

EligibilityReason eligibilityOf(const M5LaneDateInput& input, const string& id) {
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    for (size_t atom = 0; atom < input.atoms.size(); ++atom) {
        if (input.atoms[atom].id == id) return snapshot.eligibility[atom].reason;
    }
    FAIL("no atom " << id);
    return EligibilityReason::Ok;
}

M5PolicyProfile profileWithMethod(MethodPolicy method) {
    M5PolicyProfile profile = syntheticProfile();
    profile.method = method;
    return profile;
}

double weightOfTruck(const M5LaneDateInput& input, const TruckPlan& truck) {
    double weight = 0.0;
    for (const size_t atom : truck.atoms) weight += input.atoms[atom].weightLb;
    return weight;
}

double cubicFeetOfTruck(const M5LaneDateInput& input, const TruckPlan& truck) {
    double cubicInches = 0.0;
    for (const size_t atom : truck.atoms) cubicInches += input.atoms[atom].floor.cubeCubicIn;
    return cubicInches / 1728.0;
}

// U-CF-04: group `requiredGroup` has 31 FX-30 units due D at P11; group `futureGroup` 30 at P0 due/available as
// given; B is presented first unless the rows are reversed.
M5LaneDateInput cf04Lane(size_t slots, const string& futureDue = dayAfter(1), const string& futureAvailable = kD,
                         bool reversed = false, const string& requiredGroup = "A", const string& futureGroup = "B") {
    vector<Atom> atoms = concat(fx30Units("b", 30, futureGroup, 0, futureDue, futureAvailable), fx30Units("a", 31, requiredGroup, 11));
    if (reversed) reverse(atoms.begin(), atoms.end());
    M5LaneDateInput input = laneWith(atoms, slots, kDefaultTrailer, true);
    input.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    return input;
}

// All 31 required units in two nonempty legal trucks of their own group; B's 30 remain over the reservations.
void requireRequiredWinsTwoSlots(const M5LaneOutcome& outcome, const M5LaneDateInput& input, const string& requiredGroup) {
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    REQUIRE(outcome.selected->trucks.size() == 2);
    vector<size_t> loads;
    for (const TruckPlan& truck : outcome.selected->trucks) {
        CHECK_FALSE(truck.atoms.empty());
        CHECK(truck.atoms.size() <= 30);
        CHECK(truck.group == requiredGroup);
        for (const size_t atom : truck.atoms) CHECK(input.atoms[atom].group == requiredGroup);
        loads.push_back(truck.atoms.size());
    }
    sort(loads.begin(), loads.end());
    CHECK(loads == vector<size_t>{1, 30});
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shippedWithPrefix(shipped, "a") == 31);
    CHECK(shippedWithPrefix(shipped, "b") == 0);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 30);
    CHECK(countCause(outcome, LeftOverCause::AcceptanceWithheld) == 0);
    CHECK(outcome.requiredService == ServiceLevel::Full);
    m5Evidence::note("expected", {"A 31 in two trucks (30 + 1)", "B 30 remain", "no mixed truck"});
}

// The third slot: every eligible unit ships, 61 in three trucks, none mixed.
void requireAllSixtyOne(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 3);
    for (const TruckPlan& truck : outcome.selected->trucks) {
        CHECK(truck.atoms.size() <= 30);
        for (const size_t atom : truck.atoms) CHECK(input.atoms[atom].group == truck.group);
    }
    CHECK(shippedCount(outcome) == 61);
    CHECK(outcome.leftOver.empty());
}

// The B controls (due D+11, or available D+1) cannot ship early even with a third slot.
void requireFutureControlStays(const M5LaneDateInput& input, EligibilityReason reason) {
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shippedWithPrefix(shipped, "a") == 31);
    CHECK(shippedWithPrefix(shipped, "b") == 0);
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 30);
    CHECK(eligibilityOf(input, "b100") == reason);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 2);
    m5Evidence::note("expected", {"A 31 ship", "B 30 not eligible", "third slot unused"});
}

}  // namespace

TEST_CASE("U-CF-01/synthetic: can-go goods fill only the room the goods due by D leave" * doctest::test_suite("m5")) {
    // One slot holds 30 FX-30. Ten due D and thirty due D+3, all available D, one group.
    const M5LaneDateInput input = laneWith(concat(fx30Units("req", 10), fx30Units("cango", 30, "A", 0, dayAfter(3))), 1,
                                           kDefaultTrailer, true);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shipped.size() == 30);
    CHECK(shippedWithPrefix(shipped, "req") == 10);
    CHECK(shippedWithPrefix(shipped, "cango") == 20);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 10);
    CHECK(outcome.requiredService == ServiceLevel::Full);
    REQUIRE(outcome.selected);
    CHECK(weightOfTruck(input, outcome.selected->trucks.front()) == 45000.0);

    // A full truck of goods due D: no can-go good displaces one of them.
    const M5LaneDateInput full = laneWith(concat(fx30Units("req", 30), fx30Units("cango", 30, "A", 0, dayAfter(3))), 1,
                                          kDefaultTrailer, true);
    const M5LaneOutcome fullOutcome = plan(full, syntheticProfile());
    requireInvariants(fullOutcome, full);
    const set<string> fullShipped = shippedIds(fullOutcome, full);
    CHECK(shippedWithPrefix(fullShipped, "req") == 30);
    CHECK(shippedWithPrefix(fullShipped, "cango") == 0);
    m5Evidence::note("expected", {"10 required + 20 can-go", "10 can-go over the reservation", "30 required: no can-go"});
}

TEST_CASE("U-CF-02/synthetic: X-DATE-01 the can-go window is the configured pull-forward limit; D+11 and unavailable goods stay" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("d0-", 10);
    atoms = concat(atoms, fx30Units("d10-", 10, "A", 0, dayAfter(10)));
    atoms = concat(atoms, fx30Units("d11-", 10, "A", 0, dayAfter(11)));
    atoms = concat(atoms, fx30Units("late-", 5, "A", 0, dayAfter(5), dayAfter(1)));
    M5LaneDateInput input = laneWith(atoms, 2, kDefaultTrailer, true);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shipped.size() == 20);
    CHECK(shippedWithPrefix(shipped, "d0-") == 10);
    CHECK(shippedWithPrefix(shipped, "d10-") == 10);
    CHECK(shippedWithPrefix(shipped, "d11-") == 0);
    CHECK(shippedWithPrefix(shipped, "late-") == 0);
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 15);
    CHECK(eligibilityOf(input, "d11-100") == EligibilityReason::DueBeyondPullWindow);
    CHECK(eligibilityOf(input, "late-100") == EligibilityReason::NotAvailable);

    // The window follows the setting: 11 days lets D+11 in; 9 days keeps D+10 out. Availability never moves.
    input.dates.maxDaysPulledForward = 11;
    const M5LaneOutcome wider = plan(input, syntheticProfile());
    requireInvariants(wider, input);
    const set<string> widerShipped = shippedIds(wider, input);
    CHECK(shippedWithPrefix(widerShipped, "d11-") == 10);
    CHECK(shippedWithPrefix(widerShipped, "late-") == 0);
    input.dates.maxDaysPulledForward = 9;
    const M5LaneOutcome narrower = plan(input, syntheticProfile());
    requireInvariants(narrower, input);
    CHECK(shippedWithPrefix(shippedIds(narrower, input), "d10-") == 0);
    CHECK(eligibilityOf(input, "d10-100") == EligibilityReason::DueBeyondPullWindow);
    m5Evidence::note("expected", {"window 10: D+10 ships, D+11 and unavailable stay (15)", "window 11: D+11 ships", "window 9: D+10 stays"});
}

TEST_CASE("X-DATE-01/synthetic: D, D+9 and D+10 fill one truck; D+11 stays out" * doctest::test_suite("m5")) {
    vector<Atom> atoms = fx30Units("d0-", 10);
    atoms = concat(atoms, fx30Units("d9-", 10, "A", 0, dayAfter(9)));
    atoms = concat(atoms, fx30Units("d10-", 10, "A", 0, dayAfter(10)));
    atoms = concat(atoms, fx30Units("d11-", 10, "A", 0, dayAfter(11)));
    const M5LaneDateInput input = laneWith(atoms, 1, kDefaultTrailer, true);
    CHECK(input.dates.maxDaysPulledForward == 10);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 1);
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shipped.size() == 30);
    for (size_t atom = 0; atom < atoms.size(); ++atom) {
        CHECK(shipped.count(atoms[atom].id) == (atom < 30 ? 1 : 0));
    }
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 10);
    m5Evidence::note("expected", {"D, D+9, D+10: 10 each on one truck", "D+11: all 10 left, each source once"});
}

TEST_CASE("U-CF-04/methodA: Method A ships all 31 required units; B's 30 future units remain" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = cf04Lane(2);
    requireRequiredWinsTwoSlots(plan(input, profileWithMethod(MethodPolicy::PatternOnly)), input, "A");
}

TEST_CASE("U-CF-04/methodB: Method B ships all 31 required units; B's 30 future units remain" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = cf04Lane(2);
    requireRequiredWinsTwoSlots(plan(input, profileWithMethod(MethodPolicy::BlocksOnly)), input, "A");
}

TEST_CASE("U-CF-04/inputPermutation: B first, reversed rows and swapped group names give the same answer" * doctest::test_suite("m5")) {
    for (const bool reversed : {false, true}) {
        for (const bool swapped : {false, true}) {
            INFO("reversed " << reversed << ", swapped " << swapped);
            const string requiredGroup = swapped ? "B" : "A", futureGroup = swapped ? "A" : "B";
            const M5LaneDateInput input = cf04Lane(2, dayAfter(1), kD, reversed, requiredGroup, futureGroup);
            requireRequiredWinsTwoSlots(plan(input, syntheticProfile()), input, requiredGroup);
        }
    }
}

TEST_CASE("U-CF-04/thirdReservation: with a third slot all 61 units ship in three trucks" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = cf04Lane(3);
    requireAllSixtyOne(plan(input, syntheticProfile()), input);
    m5Evidence::note("expected", {"61 ship", "3 trucks", "none mixed"});
}

TEST_CASE("U-CF-04/outsideWindow: B due D+11 cannot ship early, even with a third slot" * doctest::test_suite("m5")) {
    requireFutureControlStays(cf04Lane(3, dayAfter(11)), EligibilityReason::DueBeyondPullWindow);
}

TEST_CASE("U-CF-04/unavailable: B available D+1 cannot ship early, even with a third slot" * doctest::test_suite("m5")) {
    requireFutureControlStays(cf04Lane(3, dayAfter(1), dayAfter(1)), EligibilityReason::NotAvailable);
}

namespace {

// U-CF-05: 21 reservations, no goods due by D, one indivisible FX-BLOCK due D+1 (as given), pull as given.
M5LaneDateInput cf05Lane(size_t slots, bool pullForward, const string& due = dayAfter(1), const string& available = kD) {
    return laneWith({withDates(fxBlock("block"), due, available)}, slots, kDefaultTrailer, pullForward);
}

void requireBlockRetained(const M5LaneDateInput& input, LeftOverCause cause) {
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedCount(outcome) == 0);
    CHECK_FALSE(outcome.hasCandidate);
    CHECK(outcome.kind != OutcomeKind::CompletedWithDispatch);
    REQUIRE(outcome.leftOver.size() == 1);
    CHECK(outcome.leftOver.front().cause == cause);
}

}  // namespace

TEST_CASE("U-CF-05/pullOff: with pull-forward off the can-go-only block stays; nothing ships on D" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = cf05Lane(21, false);
    requireBlockRetained(input, LeftOverCause::NotEligible);
    CHECK(eligibilityOf(input, "block") == EligibilityReason::PullForwardOff);
    m5Evidence::note("expected", {"0 ship", "block retained: pull-forward off"});
}

TEST_CASE("U-CF-05/zeroReservations: with no reservation nothing ships and no truck is made up" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = cf05Lane(0, true);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    CHECK(shippedCount(outcome) == 0);
    CHECK(outcome.reservedTrucks == 0);
    CHECK_FALSE(outcome.delta.has_value());
    REQUIRE(outcome.leftOver.size() == 1);
    CHECK(outcome.leftOver.front().cause != LeftOverCause::NotEligible);
    m5Evidence::note("expected", {"0 ship", "0 reservations, none manufactured"});
}

TEST_CASE("U-CF-05/outsideWindow: a block due D+11 stays" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = cf05Lane(21, true, dayAfter(11));
    requireBlockRetained(input, LeftOverCause::NotEligible);
    CHECK(eligibilityOf(input, "block") == EligibilityReason::DueBeyondPullWindow);
    m5Evidence::note("expected", {"0 ship", "block retained: due beyond the window"});
}

TEST_CASE("U-CF-05/unavailable: a block available D+1 stays" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = cf05Lane(21, true, dayAfter(1), dayAfter(1));
    requireBlockRetained(input, LeftOverCause::NotEligible);
    CHECK(eligibilityOf(input, "block") == EligibilityReason::NotAvailable);
    m5Evidence::note("expected", {"0 ship", "block retained: not available on D"});
}

namespace {

// U-CF-06 at the selection/commit seam: D has 21 reservations with R10 due D and F20 due D+1 (available D);
// D+1 has 20 reservations. Candidate A ships R10 + F20 in one truck on D, B ships R10 only.
struct Cf06Fixture {
    M5LaneDateInput today;
    M5LaneDateInput tomorrow;
    DemandSnapshot snapshot;
    CommitDelta deltaA;
    CommitDelta deltaB;
};

CommitDelta validatedDelta(const M5LaneDateInput& input, const DemandSnapshot& snapshot, size_t carried) {
    CandidatePlan candidate = oneTruck(input, carried);
    candidate.validation = validateFinal(candidate, snapshot, syntheticProfile(), physicalSettings());
    REQUIRE(candidate.validation->allPassed());
    string error;
    const optional<CommitDelta> delta = buildDelta(candidate, snapshot, syntheticProfile(), &error);
    INFO(error);
    REQUIRE(delta.has_value());
    return *delta;
}

Cf06Fixture cf06Fixture() {
    Cf06Fixture fixture;
    fixture.today = laneWith(concat(fx30Units("r", 10), fx30Units("f", 20, "A", 0, dayAfter(1))), 21, kDefaultTrailer, true);
    fixture.tomorrow = laneWith(fx30Units("f", 20, "A", 0, dayAfter(1)), 20, kDefaultTrailer, true);
    fixture.tomorrow.departure = dayAfter(1);
    for (Atom& atom : fixture.tomorrow.atoms) atom.components[0].offerKind = OfferKind::Required;
    fixture.snapshot = *buildSnapshot(fixture.today).snapshot;
    fixture.deltaA = validatedDelta(fixture.today, fixture.snapshot, 30);
    fixture.deltaB = validatedDelta(fixture.today, fixture.snapshot, 10);
    return fixture;
}

OperationalState freshState(const DemandSnapshot& snapshot) {
    return OperationalState{snapshot.id, snapshot.input.pool.version, {}, 0, snapshot.input.pool.slots.size()};
}

// What D+1 (an M4 date) is handed: its own goods less those a committed D delta consumed.
M5LaneDateInput laterInput(const Cf06Fixture& fixture, const OperationalState& state) {
    M5LaneDateInput later = fixture.tomorrow;
    later.atoms.erase(remove_if(later.atoms.begin(), later.atoms.end(), [&](const Atom& atom) { return state.consumedAtoms.count(atom.id) > 0; }),
                      later.atoms.end());
    return later;
}

// Per source over both dates: committed on D, then whatever D+1 ships of what it sees.
map<string, int> shipmentsBySource(const Cf06Fixture& fixture, const OperationalState& state) {
    map<string, int> uses;
    for (const string& id : state.consumedAtoms) ++uses[id];
    const M5LaneDateInput later = laterInput(fixture, state);
    for (const string& id : shippedIds(plan(later, syntheticProfile()), later)) ++uses[id];
    return uses;
}

void requireEachOnce(const map<string, int>& uses) {
    CHECK(uses.size() == 30);
    for (const auto& [id, count] : uses) {
        INFO(id);
        CHECK(count == 1);
    }
}

size_t futureSeenLater(const Cf06Fixture& fixture, const OperationalState& state) { return laterInput(fixture, state).atoms.size(); }

}  // namespace

TEST_CASE("U-CF-06/AthenB: A selected and committed; unselected B never consumes F" * doctest::test_suite("m5")) {
    const Cf06Fixture fixture = cf06Fixture();
    CommitLedger ledger;
    OperationalState state = freshState(fixture.snapshot);
    CHECK(ledger.applyIfCurrent(fixture.deltaA, fixture.snapshot, state) == ApplyStatus::Applied);
    CHECK(ledger.applyIfCurrent(fixture.deltaB, fixture.snapshot, state) == ApplyStatus::PayloadConflict);
    CHECK(state.consumedAtoms.size() == 30);
    CHECK(state.slotsUsed == 1);
    CHECK(state.reservationVersion == fixture.snapshot.input.pool.version + 1);
    CHECK(futureSeenLater(fixture, state) == 0);
    const M5LaneDateInput later = laterInput(fixture, state);
    CHECK(shippedCount(plan(later, syntheticProfile())) == 0);
    requireEachOnce(shipmentsBySource(fixture, state));
    m5Evidence::note("expected", {"A: R10 + F20 on D", "B refused (payload conflict)", "D+1 sees F0", "30 ship once"});
}

TEST_CASE("U-CF-06/BthenA: B selected and committed; unselected A never consumes F" * doctest::test_suite("m5")) {
    const Cf06Fixture fixture = cf06Fixture();
    CommitLedger ledger;
    OperationalState state = freshState(fixture.snapshot);
    CHECK(ledger.applyIfCurrent(fixture.deltaB, fixture.snapshot, state) == ApplyStatus::Applied);
    CHECK(ledger.applyIfCurrent(fixture.deltaA, fixture.snapshot, state) == ApplyStatus::PayloadConflict);
    CHECK(state.consumedAtoms.size() == 10);
    for (const string& id : state.consumedAtoms) CHECK(id.rfind("r", 0) == 0);
    CHECK(futureSeenLater(fixture, state) == 20);
    requireEachOnce(shipmentsBySource(fixture, state));
    m5Evidence::note("expected", {"B: R10 on D", "A refused (payload conflict)", "D+1 sees and ships F20", "30 ship once, never 50"});
}

TEST_CASE("U-CF-06/Aalone: A alone removes F20 from D+1 exactly once" * doctest::test_suite("m5")) {
    const Cf06Fixture fixture = cf06Fixture();
    CommitLedger ledger;
    OperationalState state = freshState(fixture.snapshot);
    CHECK(ledger.applyIfCurrent(fixture.deltaA, fixture.snapshot, state) == ApplyStatus::Applied);
    CHECK(futureSeenLater(fixture, state) == 0);
    const DemandSnapshot residual = residualAfter(fixture.snapshot, fixture.deltaA);
    CHECK(residual.input.atoms.empty());
    CHECK(residual.input.pool.slots.size() == 20);
    requireEachOnce(shipmentsBySource(fixture, state));
    m5Evidence::note("expected", {"30 on D", "D+1 sees F0 and builds no truck for them", "20 reservations left on D"});
}

TEST_CASE("U-CF-06/Balone: B alone ships R10 on D; D+1 sees and ships F20" * doctest::test_suite("m5")) {
    const Cf06Fixture fixture = cf06Fixture();
    CommitLedger ledger;
    OperationalState state = freshState(fixture.snapshot);
    CHECK(ledger.applyIfCurrent(fixture.deltaB, fixture.snapshot, state) == ApplyStatus::Applied);
    CHECK(futureSeenLater(fixture, state) == 20);
    const M5LaneDateInput later = laterInput(fixture, state);
    CHECK(shippedCount(plan(later, syntheticProfile())) == 20);
    const DemandSnapshot residual = residualAfter(fixture.snapshot, fixture.deltaB);
    CHECK(residual.input.atoms.size() == 20);  // the unused provisional offer is not a committed carry
    requireEachOnce(shipmentsBySource(fixture, state));
    m5Evidence::note("expected", {"R10 on D", "F20 on D+1", "30 ship once"});
}

TEST_CASE("U-CF-06/failureBeforeCommit: a commit refused before it applies leaves F20 for D+1" * doctest::test_suite("m5")) {
    const Cf06Fixture fixture = cf06Fixture();
    CommitLedger ledger;
    OperationalState moved = freshState(fixture.snapshot);
    ++moved.reservationVersion;  // the reservations changed after A was checked
    const OperationalState before = moved;
    CHECK(ledger.applyIfCurrent(fixture.deltaA, fixture.snapshot, moved) == ApplyStatus::StaleSnapshot);
    CHECK(moved.consumedAtoms == before.consumedAtoms);
    CHECK(moved.slotsUsed == before.slotsUsed);
    CHECK(moved.reservationVersion == before.reservationVersion);
    CHECK(futureSeenLater(fixture, moved) == 20);
    // The same delta can still commit once the state is the one it was checked against.
    OperationalState current = freshState(fixture.snapshot);
    CHECK(ledger.applyIfCurrent(fixture.deltaA, fixture.snapshot, current) == ApplyStatus::Applied);
    CHECK(futureSeenLater(fixture, current) == 0);
    m5Evidence::note("expected", {"stale: nothing applied", "F20 still available to D+1"});
}

TEST_CASE("U-CF-06/retryAfterCommit: retrying a committed delta replays idempotently; F is consumed once" * doctest::test_suite("m5")) {
    const Cf06Fixture fixture = cf06Fixture();
    CommitLedger ledger;
    OperationalState state = freshState(fixture.snapshot);
    CHECK(ledger.applyIfCurrent(fixture.deltaA, fixture.snapshot, state) == ApplyStatus::Applied);
    const OperationalState afterFirst = state;
    CHECK(ledger.applyIfCurrent(fixture.deltaA, fixture.snapshot, state) == ApplyStatus::AlreadyApplied);
    CHECK(state.consumedAtoms == afterFirst.consumedAtoms);
    CHECK(state.slotsUsed == 1);
    CHECK(state.reservationVersion == afterFirst.reservationVersion);
    // A retry from the reconciled residual has nothing left to take.
    const DemandSnapshot residual = residualAfter(fixture.snapshot, fixture.deltaA);
    CHECK(residual.input.atoms.empty());
    CHECK(residual.input.pool.version == fixture.snapshot.input.pool.version + 1);
    requireEachOnce(shipmentsBySource(fixture, state));
    m5Evidence::note("expected", {"second apply: already applied", "consumed 30 once", "residual empty"});
}

namespace {

// U-CF-07: one slot; 25 required P11 due D, ten can-go P0 due D+10, one due D+11, one due D+5 available D+1.
M5LaneDateInput cf07Lane(size_t required = 25, bool pullForward = true, bool reversed = false) {
    vector<Atom> atoms = fx30Units("req", required, "A", 11);
    atoms = concat(atoms, fx30Units("go", 10, "A", 0, dayAfter(10)));
    atoms.push_back(fx30("far", "A", 0, dayAfter(11)));
    atoms.push_back(fx30("unavailable", "A", 0, dayAfter(5), dayAfter(1)));
    if (reversed) reverse(atoms.begin(), atoms.end());
    M5LaneDateInput input = laneWith(atoms, 1, kDefaultTrailer, pullForward);
    input.pastDueOrder = ob::PastDueOrder::PriorityFirst;
    return input;
}

set<string> requireExactSpareFill(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    requireInvariants(outcome, input);
    REQUIRE(outcome.selected);
    REQUIRE(outcome.selected->trucks.size() == 1);
    const TruckPlan& truck = outcome.selected->trucks.front();
    CHECK(truck.atoms.size() == 30);
    CHECK(weightOfTruck(input, truck) == 45000.0);
    CHECK(cubicFeetOfTruck(input, truck) == 3750.0);
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shippedWithPrefix(shipped, "req") == 25);
    CHECK(shippedWithPrefix(shipped, "go") == 5);
    CHECK(shipped.count("far") == 0);
    CHECK(shipped.count("unavailable") == 0);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 5);
    CHECK(causeOf(outcome, input, "far") == LeftOverCause::NotEligible);
    CHECK(causeOf(outcome, input, "unavailable") == LeftOverCause::NotEligible);
    CHECK(eligibilityOf(input, "far") == EligibilityReason::DueBeyondPullWindow);
    CHECK(eligibilityOf(input, "unavailable") == EligibilityReason::NotAvailable);
    m5Evidence::note("expected", {"25 required + 5 can-go = 30", "45,000 lb", "3,750 cu ft", "5 can-go over, far and unavailable not eligible"});
    set<string> future;
    for (const string& id : shipped) {
        if (id.rfind("go", 0) == 0) future.insert(id);
    }
    return future;
}

}  // namespace

TEST_CASE("U-CF-07/methodA: Method A adds exactly five can-go units beside the 25 required" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = cf07Lane();
    requireExactSpareFill(plan(input, profileWithMethod(MethodPolicy::PatternOnly)), input);
}

TEST_CASE("U-CF-07/methodB: Method B adds exactly five can-go units beside the 25 required" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = cf07Lane();
    requireExactSpareFill(plan(input, profileWithMethod(MethodPolicy::BlocksOnly)), input);
}

TEST_CASE("U-CF-07/pullOff: with pull-forward off only the 25 required ship" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = cf07Lane(25, false);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shipped.size() == 25);
    CHECK(shippedWithPrefix(shipped, "req") == 25);
    CHECK(countCause(outcome, LeftOverCause::NotEligible) == 12);
    CHECK(eligibilityOf(input, "go100") == EligibilityReason::PullForwardOff);
    m5Evidence::note("expected", {"25 ship", "12 future units not eligible (pull-forward off)"});
}

TEST_CASE("U-CF-07/requiredAlreadyFull: with 30 required units no future unit is added" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = cf07Lane(30);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shipped.size() == 30);
    CHECK(shippedWithPrefix(shipped, "req") == 30);
    CHECK(shippedWithPrefix(shipped, "go") == 0);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 10);
    m5Evidence::note("expected", {"30 required ship", "0 future"});
}

TEST_CASE("U-CF-07/equalFutureIds: equal future units are chosen by the canonical record order, whatever the row order" * doctest::test_suite("m5")) {
    // ED-21 T2: among equal goods the lowest (source line, atom ID) records come first.
    const M5LaneDateInput forward = cf07Lane(25, true, false), backward = cf07Lane(25, true, true);
    const set<string> forwardFuture = requireExactSpareFill(plan(forward, syntheticProfile()), forward);
    const set<string> backwardFuture = requireExactSpareFill(plan(backward, syntheticProfile()), backward);
    CHECK(forwardFuture == set<string>{"go100", "go101", "go102", "go103", "go104"});
    CHECK(backwardFuture == forwardFuture);
}

namespace {

// U-CF-08: one slot; H is required, F is 30 FX-30 units due D+1 and available D.
M5LaneDateInput cf08Lane(const Atom& required) {
    return laneWith(concat({required}, fx30Units("f", 30, "A", 0, dayAfter(1))), 1, kDefaultTrailer, true);
}

}  // namespace

TEST_CASE("U-CF-08/impossibleRequired: an overweight required TLD never ships or splits; all 30 future units do" * doctest::test_suite("m5")) {
    // H: nonstackable 20 x 100 x 108 in, 45,001 lb > 45,000 lb payload on every slot.
    const M5LaneDateInput input = cf08Lane(unitOf("H", "A", 100.0, 20.0, 108.0, 45001.0));
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shipped.count("H") == 0);
    CHECK(shippedWithPrefix(shipped, "f") == 30);
    CHECK(leftOverEntriesOf(outcome, input, "H") == 1);
    CHECK(causeOf(outcome, input, "H") == LeftOverCause::Unfittable);
    CHECK(outcome.leftOver.size() == 1);
    CHECK(shipped.size() + outcome.leftOver.size() == 31);
    CHECK(outcome.stop == StopReason::Completed);
    const PhysicalCheck alone = checkTruck(input, {0}, kDefaultTrailer, physicalSettings());
    CHECK_FALSE(alone.fits);
    CHECK_FALSE(alone.undecided);
    m5Evidence::note("expected", {"F30 ship", "H unserved once: fits no reserved trailer (45,001 lb)", "31 accounted"});
}

TEST_CASE("U-CF-08/feasibleRequiredControl: a valid required unit in H's place ships, with 29 future units" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = cf08Lane(fx30("H"));
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    requireInvariants(outcome, input);
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shipped.count("H") == 1);
    CHECK(shippedWithPrefix(shipped, "f") == 29);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 1);
    CHECK(outcome.requiredService == ServiceLevel::Full);
    m5Evidence::note("expected", {"H ships", "29 future ship", "1 future over the reservation"});
}

TEST_CASE("U-CF-08/unprovenRequiredControl: a required unit whose fit was never decided is not labelled impossible" * doctest::test_suite("m5")) {
    // H fits. A one-unit check needs no row search, so it is decided even with the route's search states
    // spent; with them spent every multi-unit truck is Undecided. H must never be reported as fitting no
    // trailer to make room for future goods.
    const M5LaneDateInput input = cf08Lane(fx30("H"));
    PhysicalSettings starved = physicalSettings();
    starved.routeBudget = make_shared<RouteSearchBudget>("unproven", 1, 1);
    REQUIRE(starved.routeBudget->admit(1) == 1);  // the route's only state is spent
    const PhysicalCheck alone = checkTruckInRoute(input, {0}, kDefaultTrailer, starved, 1);
    CHECK(alone.fits);
    CHECK_FALSE(alone.undecided);
    FakeClock clock;
    const M5RunServices services(clock);
    const M5LaneOutcome outcome = planLaneDate(input, syntheticProfile(), starved, services);
    for (const LeftOver& left : outcome.leftOver) {
        INFO(input.atoms[left.atom].id << ": " << leftOverCauseName(left.cause));
        CHECK(left.cause != LeftOverCause::Unfittable);
    }
    const set<string> shipped = shippedIds(outcome, input);
    if (shipped.count("H") == 0) CHECK(shippedWithPrefix(shipped, "f") == 0);
    m5Evidence::note("expected", {"H never labelled 'fits no reserved trailer' while undecided", "no future good ships in its place"});
}

namespace {

// The one-unit check a test forces: Undecided for the atoms named, the real check for the rest.
PhysicalCheck undecidedCheck() {
    PhysicalCheck check;
    check.undecided = true;
    check.stop = SearchStop::RouteLimit;
    return check;
}

M5LaneOutcome planWithAloneCheck(const M5LaneDateInput& input, function<PhysicalCheck(size_t)> aloneCheck) {
    FakeClock clock;
    M5RunServices services(clock);
    services.singleUnitCheck = std::move(aloneCheck);
    return planLaneDate(input, syntheticProfile(), physicalSettings(), services);
}

}  // namespace

TEST_CASE("U-CF-08 undecided alone: a required unit whose one-unit check is Undecided is offered and ships, never 'fits no trailer'" * doctest::test_suite("m5")) {
    // ED-19: only a decided refusal makes a good unfittable. The stub forces H's alone check Undecided.
    const M5LaneDateInput input = cf08Lane(fx30("H"));
    size_t stubCalls = 0;
    const M5LaneOutcome outcome = planWithAloneCheck(input, [&](size_t atom) {
        ++stubCalls;
        return atom == 0 ? undecidedCheck() : checkTruck(input, {atom}, kDefaultTrailer, physicalSettings());
    });
    requireInvariants(outcome, input);
    CHECK(stubCalls == 31);
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shipped.count("H") == 1);
    CHECK(shippedWithPrefix(shipped, "f") == 29);
    CHECK(countCause(outcome, LeftOverCause::Unfittable) == 0);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 1);
    CHECK(outcome.leftOver.size() == 1);
    CHECK(shipped.size() + outcome.leftOver.size() == 31);
    CHECK(outcome.requiredService == ServiceLevel::Full);
}

TEST_CASE("U-CF-08 undecided alone: every one-unit check Undecided leaves every good offered, none labelled unfittable" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = cf08Lane(fx30("H"));
    const M5LaneOutcome outcome = planWithAloneCheck(input, [](size_t) { return undecidedCheck(); });
    requireInvariants(outcome, input);
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shipped.size() == 30);
    CHECK(shipped.count("H") == 1);
    CHECK(countCause(outcome, LeftOverCause::Unfittable) == 0);
    CHECK(shipped.size() + outcome.leftOver.size() == 31);
    // The undecided goods stay in the service count: 30 of 31 offered is Partial, not Full of 0.
    CHECK(outcome.offeredService == ServiceLevel::Partial);
    CHECK(outcome.requiredService == ServiceLevel::Full);
}

TEST_CASE("U-CF-08 undecided alone: through the seam a decided refusal still marks the good unfittable" * doctest::test_suite("m5")) {
    // Control for the stub: a decided DoesNotFit for H keeps H out exactly as the real overweight case does.
    const M5LaneDateInput input = cf08Lane(fx30("H"));
    const M5LaneOutcome outcome = planWithAloneCheck(input, [&](size_t atom) {
        return atom == 0 ? PhysicalCheck{} : checkTruck(input, {atom}, kDefaultTrailer, physicalSettings());
    });
    requireInvariants(outcome, input);
    const set<string> shipped = shippedIds(outcome, input);
    CHECK(shipped.count("H") == 0);
    CHECK(shippedWithPrefix(shipped, "f") == 30);
    CHECK(causeOf(outcome, input, "H") == LeftOverCause::Unfittable);
    CHECK(outcome.leftOver.size() == 1);
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

namespace {
bool date06Recheck(bool pull) {
    Atom stack = unitOf("mixed", "A", 100, 20, 100, 3000);
    stack.floor.levels = 2;
    stack.unitLoads = 2.0;
    stack.components[0].source = "line-d";
    Component later = stack.components[0];
    later.source = "line-d1";
    later.dueDate = dayAfter(1);
    later.offerKind = OfferKind::CanGo;
    stack.components.push_back(later);
    const M5LaneDateInput input = laneWith({stack}, 1, kDefaultTrailer, pull);
    CHECK(input.dates.maxDaysPulledForward == 10);
    CHECK(input.atoms[0].floor.heightIn == 100.0);
    CHECK(input.atoms[0].weightLb == 3000.0);
    CHECK(checkTruck(input, {0}, kDefaultTrailer, physicalSettings()).fits);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan work = oneTruck(input, 0);
    CHECK(reconcile(snapshot, work).balanced);
    // Remainder/repair mutation: the mixed stack moves from left over onto the one truck.
    work.trucks[0].atoms.push_back(0);
    work.leftOver.clear();
    ++work.revision;
    work.validation.reset();
    storeRows(work, input);
    CHECK(reconcile(snapshot, work).balanced);
    const ValidationReport checked = validateFinal(work, snapshot, syntheticProfile(), physicalSettings());
    if (!pull) {
        CHECK(atomEligibility(stack, kD, input.dates).failingComponents == vector<size_t>{1});
        CHECK(checked.eligibility == SectionStatus::Fail);
    }
    return checked.allPassed();
}
}  // namespace

TEST_CASE("X-DATE-06/pullOff: a post-repair mixed stack cannot hide its D+1 component" * doctest::test_suite("m5")) {
    CHECK_FALSE(date06Recheck(false));
    m5Evidence::note("expected", {"two-high 100 in, 3000 lb stack fits physically", "pull off: D+1 component fails date validation"});
}

TEST_CASE("X-DATE-06/pullOn: a post-repair mixed stack passes both component dates within ten days" * doctest::test_suite("m5")) {
    CHECK(date06Recheck(true));
    m5Evidence::note("expected", {"two-high 100 in, 3000 lb stack fits physically", "pull on: both components date-eligible"});
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

namespace {
void date09Snapshots(bool reverseOrder) {
    const vector<string> dates = reverseOrder ? vector<string>{dayAfter(7), kD} : vector<string>{kD, dayAfter(7)};
    map<string, bool> eligible;
    map<string, SnapshotId> identities;
    for (const string& date : dates) {
        M5LaneDateInput input = laneWith(fx30Units("future-", 30, "A", 0, dayAfter(17)), 1, kDefaultTrailer, true);
        input.departure = date;
        REQUIRE(input.dates.maxDaysPulledForward == 10);
        const SnapshotBuild built = buildSnapshot(input);
        REQUIRE(built.snapshot);
        CHECK(built.snapshot->input.pool.slots.size() == 1);
        CHECK(built.snapshot->eligibility.size() == 30);
        eligible[date] = built.snapshot->eligibility[0].eligible;
        identities[date] = built.snapshot->id;
        for (const Eligibility& unit : built.snapshot->eligibility) CHECK(unit.eligible == (date == dayAfter(7)));
    }
    CHECK_FALSE(eligible.at(kD));
    CHECK(eligible.at(dayAfter(7)));
    CHECK(identities.at(kD) != identities.at(dayAfter(7)));
}
}  // namespace

TEST_CASE("X-DATE-09/orderDthenD7: fresh snapshots apply the pull window separately to D and D+7" * doctest::test_suite("m5")) {
    date09Snapshots(false);
    m5Evidence::note("expected", {"D: D+17 outside ten-day pull", "D+7: D+17 within ten days", "distinct snapshot identities"});
}

TEST_CASE("X-DATE-09/orderD7thenD: reversing evaluation order leaves both date eligibilities intact" * doctest::test_suite("m5")) {
    date09Snapshots(true);
    m5Evidence::note("expected", {"D+7 first eligible", "D second ineligible", "distinct snapshot identities"});
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

TEST_CASE("X-DATA-11/synthetic: repeated source identity is counted once or reported as conflicting" * doctest::test_suite("m5")) {
    M5LaneDateInput same = laneWith({fx30("same"), fx30("same")}, 1);
    const SnapshotBuild repeated = buildSnapshot(same);
    CHECK_FALSE(repeated.snapshot.has_value());
    CHECK(any_of(repeated.errors.begin(), repeated.errors.end(), [](const string& error) {
        return error.find("atom same is offered twice") != string::npos;
    }));
    const M5LaneOutcome refused = plan(same, syntheticProfile());
    CHECK(refused.kind == OutcomeKind::InvalidInput);
    CHECK(shippedCount(refused) == 0);

    M5LaneDateInput conflict = laneWith({fx30("first"), fx30("second")}, 1);
    conflict.atoms[1].components[0].source = conflict.atoms[0].components[0].source;
    conflict.atoms[1].components[0].product = "another-product";
    const SnapshotBuild conflicting = buildSnapshot(conflict);
    CHECK_FALSE(conflicting.snapshot.has_value());
    CHECK(any_of(conflicting.errors.begin(), conflicting.errors.end(), [](const string& error) {
        return error.find("source line line-first appears with conflicting product") != string::npos;
    }));
    CHECK(plan(conflict, syntheticProfile()).kind == OutcomeKind::InvalidInput);
    m5Evidence::note("expected", {"identical repeated atom rejected, never dispatched twice", "same line with changed product named as conflict"});
}

TEST_CASE("U-RT-06 forced M5 with zero reservations: zero dispatch, no capacity invented" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 10), 0);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(outcome.kind == OutcomeKind::CompletedZeroDispatch);
    CHECK_FALSE(outcome.hasCandidate);
    CHECK(countCause(outcome, LeftOverCause::OverReservation) == 10);
}

TEST_CASE("X-RUN-06/synthetic: Method B with 100 reserved and no goods, or only impossible goods, stops at once with a reason: no empty truck, no wait for the timeout" * doctest::test_suite("m5")) {
    M5PolicyProfile blocks = syntheticProfile();
    blocks.method = MethodPolicy::BlocksOnly;
    const M5LaneOutcome empty = plan(laneWith({}, 100), blocks);
    CHECK(empty.kind == OutcomeKind::CompletedNoEligibleInput);
    CHECK(empty.stop == StopReason::Completed);
    CHECK_FALSE(empty.selected.has_value());
    CHECK_FALSE(empty.delta.has_value());
    CHECK(empty.timing.elapsedSeconds == 0.0);
    const M5LaneDateInput impossible = laneWith({unitOf("tall", "A", 48, 40, 130, 500)}, 100);
    const M5LaneOutcome outcome = plan(impossible, blocks);
    CHECK(outcome.kind == OutcomeKind::CompletedZeroDispatch);
    CHECK(outcome.stop == StopReason::Completed);
    CHECK(countCause(outcome, LeftOverCause::Unfittable) == 1);
    CHECK(shippedCount(outcome) == 0);
    CHECK((!outcome.selected.has_value() || outcome.selected->trucks.empty()));
    CHECK(outcome.timing.elapsedSeconds == 0.0);
    CHECK(outcome.floorSearch.statesCharged < 1000);
    m5Evidence::note("expected", {"no goods: completed, no eligible input, no trucks", "only an impossible good: completed, zero dispatch, the good Unfittable", "fake clock never advanced: no loop until the allowance"});
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

TEST_CASE("X-DATA-13 finite but enormous dimensions that overflow when multiplied are refused before planning" * doctest::test_suite("m5")) {
    M5LaneDateInput overflow = laneWith({fx30("physical")}, 1);
    overflow.atoms[0].floor.cubeCubicIn = numeric_limits<double>::max();
    overflow.atoms[0].floor.longSideIn = numeric_limits<double>::max();
    const SnapshotBuild build = buildSnapshot(overflow);
    CHECK_FALSE(build.snapshot.has_value());
    CHECK(any_of(build.errors.begin(), build.errors.end(), [](const string& error) {
        return error.find("volume overflows") != string::npos;
    }));
    const M5LaneOutcome outcome = plan(overflow, syntheticProfile());
    CHECK(outcome.kind == OutcomeKind::InvalidInput);
    CHECK(shippedCount(outcome) == 0);
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

TEST_CASE("X-DATA-13/synthetic: invalid dimensions and capacity stop before packing; absent TLD deck is valid" * doctest::test_suite("m5")) {
    const double nan = numeric_limits<double>::quiet_NaN();
    const double infinity = numeric_limits<double>::infinity();
    for (const double bad : {0.0, -1.0, nan, infinity}) {
        M5LaneDateInput invalid = laneWith({fx30("physical")}, 1);
        invalid.pool.slots[0].lengthIn = bad;
        CHECK(plan(invalid, syntheticProfile()).kind == OutcomeKind::InvalidInput);
        invalid = laneWith({fx30("physical")}, 1);
        invalid.atoms[0].floor.heightIn = bad;
        CHECK(plan(invalid, syntheticProfile()).kind == OutcomeKind::InvalidInput);
    }
    M5LaneDateInput overflow = laneWith({fx30("physical")}, 1);
    overflow.atoms[0].floor.cubeCubicIn = numeric_limits<double>::max();
    overflow.atoms[0].floor.longSideIn = numeric_limits<double>::max();
    CHECK(plan(overflow, syntheticProfile()).kind == OutcomeKind::InvalidInput);

    const m4SmallDay::SmallDay day("xData13ZeroDeck");
    ofstream(day.directory / "pallets.csv") << "ID,Footprint_Length,Footprint_Width,Height,Weight\nTLD,48,40,0,1\n";
    const ob::PipelineResult result = ob::Pipeline::run(day.inputs);
    CHECK(result.stackReport.unstackedLines.empty());
    CHECK(result.validation.errors == 0);
    m5Evidence::note("expected", {"zero, negative, NaN and infinity physical dimensions refused", "overflow refused",
                                  "TLD deck height zero accepted as absent deck"});
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
