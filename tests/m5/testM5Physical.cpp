// M5's physical side: patterns, footprints and decks (through M4's own rules), witnesses, and the
// fullness labels' boundaries (v7 4.3, 13, 4.12 and 33).
#include "doctest.h"
#include "m5Ledger.hpp"
#include "m5Methods.hpp"
#include "m5Repair.hpp"
#include "m5UnitLoads.hpp"
#include "m5Validation.hpp"
#include "paramsTypes.hpp"
#include "stackRules.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Fixtures.hpp"
#include "support/m5LayoutChecker.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>

using namespace std;
using namespace ob::m5;
using namespace m5Fixtures;

namespace {

CandidatePlan truckOf(const M5LaneDateInput& input, const vector<size_t>& atoms, size_t slot = 0) {
    CandidatePlan plan;
    TruckPlan truck;
    truck.slot = slot;
    truck.group = input.atoms[atoms.front()].group;
    truck.atoms = atoms;
    sort(truck.atoms.begin(), truck.atoms.end());
    plan.trucks.push_back(truck);
    for (size_t atom = 0; atom < input.atoms.size(); ++atom) {
        if (find(atoms.begin(), atoms.end(), atom) == atoms.end()) plan.leftOver.push_back({atom, LeftOverCause::OverReservation});
    }
    storeRows(plan, input);
    return plan;
}

FullnessLabel labelOf(const M5LaneDateInput& input, const vector<size_t>& atoms, M5PolicyProfile profile = syntheticProfile()) {
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const CandidatePlan plan = truckOf(input, atoms);
    return evaluateFullness(plan, snapshot, remainingAfterSelected(snapshot, plan), profile, physicalSettings()).front();
}

Atom stackOfLevels(const string& id, double length, double width, double levelHeight, int levels, double weightPerLevel) {
    Atom atom = unitOf(id, "A", length, width, levelHeight * levels, weightPerLevel * levels);
    atom.floor.levels = levels;
    atom.unitLoads = levels;
    atom.components[0].unitLoads = levels;
    return atom;
}

vector<Atom> concat(vector<Atom> left, const vector<Atom>& right) {
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

bool hasPattern(const ob::FloorLayout& layout, ob::FloorPatternKind kind) {
    return any_of(layout.rows.begin(), layout.rows.end(), [&](const ob::FloorRow& row) { return row.pattern == kind; });
}

}  // namespace

TEST_CASE("U-PB-04 patterns come from M4's floorPattern: wide-wide and pinwheel where they fit" * doctest::test_suite("m5")) {
    vector<Atom> small, large;
    for (int unit = 0; unit < 30; ++unit) small.push_back(display("s" + to_string(100 + unit)));
    for (int unit = 0; unit < 8; ++unit) large.push_back(unitOf("l" + to_string(100 + unit), "A", 60, 60, 50, 500));
    const M5LaneDateInput wide = laneWith(small, 1), square = laneWith(large, 1);
    vector<size_t> all30(30), all8(8);
    for (size_t index = 0; index < 30; ++index) all30[index] = index;
    for (size_t index = 0; index < 8; ++index) all8[index] = index;
    CHECK(hasPattern(checkTruck(wide, all30, kDefaultTrailer, physicalSettings()).layout, ob::FloorPatternKind::WideWide));
    // Long sides over 50 in cannot stand wide-wide in 100 in: M4's rows offer the pinwheel instead.
    vector<ob::FloorItem> squares(4, unitOf("q", "A", 56, 52, 50, 500).floor);
    const vector<ob::FloorRow> rows = ob::candidateRows(squares, {0, 1, 2, 3}, trailerOf(kDefaultTrailer), physicalSettings().floor);
    CHECK(any_of(rows.begin(), rows.end(), [](const ob::FloorRow& row) { return row.pattern == ob::FloorPatternKind::Pinwheel; }));
    CHECK(checkTruck(square, all8, kDefaultTrailer, physicalSettings()).fits);
}

TEST_CASE("U-PB-05 X-PAT-06 footprints: TLD and GMA use the layer, PTL and PGM the larger per side" * doctest::test_suite("m5")) {
    ob::ProductRecord product;
    product.layerLengthIn = 49.0;
    product.layerWidthIn = 39.0;
    const ob::PalletSpec ptl{"PTL", 60.0, 6.0, 48.0, 40.0}, tld{"TLD", 0.0, 0.0, 48.0, 40.0};
    const ob::UnitLoadFootprint onPtl = ob::unitLoadFootprint(product, ptl, ob::FootprintRule::WrittenRule);
    CHECK(onPtl.longSideIn == 49.0);
    CHECK(onPtl.shortSideIn == 40.0);
    product.layerLengthIn = 47.0;
    product.layerWidthIn = 41.0;
    const ob::UnitLoadFootprint other = ob::unitLoadFootprint(product, ptl, ob::FootprintRule::WrittenRule);
    CHECK(other.longSideIn == 48.0);
    CHECK(other.shortSideIn == 41.0);
    const ob::UnitLoadFootprint onTld = ob::unitLoadFootprint(product, tld, ob::FootprintRule::WrittenRule);
    CHECK(onTld.longSideIn == 47.0);
    CHECK(onTld.shortSideIn == 41.0);
}

TEST_CASE("U-PB-06 X-PAT-07 the deck counts once per pallet: two PTL of 51 in exceed 108, two TLD do not" * doctest::test_suite("m5")) {
    ob::ProductRecord product;
    product.height_in = 51.0;
    product.layers_unit_load = 1;
    ob::M2Params params;
    params.floorDeckHeight = ob::DeckHeightRule::Included;
    const ob::PalletSpec ptl{"PTL", 60.0, 6.0, 48.0, 40.0}, tld{"TLD", 0.0, 0.0, 48.0, 40.0};
    CHECK(2 * ob::unitLoadHeightIn(product, tld, params) == 102.0);
    CHECK(2 * ob::unitLoadHeightIn(product, ptl, params) == 114.0);
    // M5 holds the stacked height M2 built and only checks it against the ceiling.
    const M5LaneDateInput input = laneWith({unitOf("tld", "A", 48, 40, 102, 500), unitOf("ptl", "A", 48, 40, 114, 500)}, 1);
    CHECK(checkTruck(input, {0}, kDefaultTrailer, physicalSettings()).fits);
    CHECK_FALSE(checkTruck(input, {1}, kDefaultTrailer, physicalSettings()).fits);
}

TEST_CASE("U-PB-07 X-PAT-15 M5 never stacks: every atom takes its own floor position" * doctest::test_suite("m5")) {
    vector<Atom> layers;
    for (int unit = 0; unit < 9; ++unit) layers.push_back(unitOf("x" + to_string(100 + unit), "A", 48, 40, 36, 300));
    layers.push_back(stackOfLevels("three", 48, 40, 36, 3, 300));
    layers.push_back(stackOfLevels("two", 48, 40, 36, 2, 300));
    const M5LaneDateInput input = laneWith(layers, 1);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    REQUIRE(outcome.selected);
    const TruckPlan& truck = outcome.selected->trucks.front();
    CHECK(truck.atoms.size() == 11);
    CHECK(truck.layout.positions == 11);
    CHECK(input.atoms[9].floor.levels == 3);
    CHECK(input.atoms[10].floor.levels == 2);
}

TEST_CASE("U-PB-08 EC-05 a weight-bound product's pattern stops at the payload, not the floor" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith({unitOf("heavy", "A", 48, 40, 50, 2000)}, 1);
    CHECK(bestSingleClassLoad(input, 0, kDefaultTrailer, physicalSettings(), 100) == 22);
    const M5LaneDateInput light = laneWith({display("light")}, 1);
    CHECK(bestSingleClassLoad(light, 0, kDefaultTrailer, physicalSettings(), 100) == 30);
}

TEST_CASE("X-PAT-08 a pattern is rechecked on a smaller trailer, not copied" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith({unitOf("long", "A", 625, 100, 108, 1000), unitOf("heavy", "A", 600, 100, 108, 42000),
                                            unitOf("fine", "A", 590, 100, 108, 30000)},
                                           1);
    const TrailerCapability shortTrailer{600.0, 100.0, 108.0, 40000.0, kFixtureUnitLoadCap};
    CHECK(checkTruck(input, {0}, kDefaultTrailer, physicalSettings()).fits);
    CHECK_FALSE(checkTruck(input, {0}, shortTrailer, physicalSettings()).fits);
    CHECK(checkTruck(input, {1}, kDefaultTrailer, physicalSettings()).fits);
    CHECK_FALSE(checkTruck(input, {1}, shortTrailer, physicalSettings()).fits);
    CHECK(checkTruck(input, {2}, shortTrailer, physicalSettings()).fits);
}

TEST_CASE("X-PAT-09 X-DATA-06 a heavier master revision changes the pattern: 30 at 1,500 lb, 28 at 1,600" * doctest::test_suite("m5")) {
    const M5LaneDateInput versionOne = laneWith({fx30("v1")}, 1);
    Atom heavier = fx30("v2");
    heavier.weightLb = 1600.0;
    const M5LaneDateInput versionTwo = laneWith({heavier}, 1);
    CHECK(bestSingleClassLoad(versionOne, 0, kDefaultTrailer, physicalSettings(), 100) == 30);
    CHECK(bestSingleClassLoad(versionTwo, 0, kDefaultTrailer, physicalSettings(), 100) == 28);
    CHECK(physicalClassOf(laneWith({fx30("a"), heavier}, 1)) == vector<size_t>{0, 1});
    const M5LaneOutcome outcome = plan(laneWith(fx30Units("u", 30), 1), syntheticProfile());
    vector<Atom> heavierUnits = fx30Units("u", 30);
    for (Atom& atom : heavierUnits) atom.weightLb = 1600.0;
    const M5LaneOutcome heavierOutcome = plan(laneWith(heavierUnits, 1), syntheticProfile());
    CHECK(shippedCount(outcome) == 30);
    CHECK(shippedCount(heavierOutcome) == 28);
}

TEST_CASE("X-PAT-02 a mixed pattern's copies are limited by every product it uses" * doctest::test_suite("m5")) {
    CHECK(mixedPatternCopies({5, 4}, {2, 1}, 10) == 2);
    CHECK(mixedPatternCopies({5, 4}, {2, 1}, 1) == 1);
    CHECK(mixedPatternCopies({5, 0}, {2, 1}, 10) == 0);
    CHECK(mixedPatternCopies({5, 4}, {0, 0}, 10) == 0);
    CHECK_THROWS_AS(mixedPatternCopies({5}, {2, 1}, 10), invalid_argument);
}

TEST_CASE("X-PAT-05 copies draw only eligible goods and keep each line's own dates" * doctest::test_suite("m5")) {
    vector<Atom> atoms;
    for (int unit = 0; unit < 30; ++unit) atoms.push_back(unitOf("now" + to_string(100 + unit), "A", 100, 63, 108, 4500));
    for (int unit = 0; unit < 70; ++unit) atoms.push_back(unitOf("late" + to_string(100 + unit), "A", 100, 63, 108, 4500, 0, dayAfter(11)));
    const M5LaneDateInput input = laneWith(atoms, 10, kDefaultTrailer, true);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 3);
    for (const TruckPlan& truck : outcome.selected->trucks) {
        CHECK(truck.atoms.size() == 10);
        for (const size_t atom : truck.atoms) CHECK(input.atoms[atom].components[0].dueDate == kD);
    }
}

TEST_CASE("X-PAT-10 X-STATE-06 repairing one copy leaves the others; removed goods return once" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 90), 3);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan candidate = *plan(input, syntheticProfile()).selected;
    const vector<TruckPlan> before = candidate.trucks;
    const size_t removed = candidate.trucks[1].atoms.back();
    CandidateEditor(candidate).removeToLeftOver(1, removed, LeftOverCause::OverReservation);
    CHECK(candidate.trucks[0].atoms == before[0].atoms);
    CHECK(candidate.trucks[2].atoms == before[2].atoms);
    CHECK(reconcile(snapshot, candidate).balanced);
    set<size_t> seen;
    for (const TruckPlan& truck : candidate.trucks) {
        for (const size_t atom : truck.atoms) CHECK(seen.insert(atom).second);
    }
}

TEST_CASE("U-PB-11 R-1c a changed truck loses its proof and its validation" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 60), 2);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    REQUIRE(outcome.selected);
    CandidatePlan candidate = *outcome.selected;
    CHECK(candidate.evidence.proof == ProofStatus::ProvenOptimal);
    CandidateEditor(candidate).moveAtom(0, 1, candidate.trucks[0].atoms.front());
    CHECK(candidate.evidence.proof == ProofStatus::Unproven);
    CHECK_FALSE(candidate.validation.has_value());
    const ValidationReport recheck = validateFinal(candidate, *buildSnapshot(input).snapshot, syntheticProfile(), physicalSettings());
    CHECK_FALSE(recheck.allPassed());
}

TEST_CASE("U-AC-05/productionProfile: the U-FL-04 display truck ships; FULL by floor limit only with its no-fit witness" * doctest::test_suite("m5")) {
    // 31 X-E2E-09 displays (48 x 40 in, 50 in, 800 lb), one group and reservation: 30 stand in 600 x 96 in.
    vector<Atom> displays;
    for (int unit = 0; unit < 31; ++unit) displays.push_back(display("d" + to_string(100 + unit)));
    const M5LaneDateInput input = laneWith(displays, 1);
    const M5LaneOutcome production = plan(input, syntheticProfile());
    REQUIRE(production.selected);
    CHECK(shippedCount(production) == 30);
    CHECK(none_of(production.leftOver.begin(), production.leftOver.end(),
                  [](const LeftOver& left) { return left.cause == LeftOverCause::AcceptanceWithheld; }));
    REQUIRE(production.fullness.size() == 1);
    const FullnessLabel& label = production.fullness.front();
    CHECK(label.state == FullnessState::Full);
    CHECK(label.byFloorLimit);
    CHECK_FALSE(label.byWeight);
    CHECK(label.fitStatus == RemainingFitStatus::ProvenNoFit);
    CHECK_FALSE(label.fitScope.empty());
    CHECK(label.rearInches == doctest::Approx(30.0));
    CHECK(label.weightPercent == doctest::Approx(24000.0 / 450.0));
    CHECK(label.bucket == FullnessBucket::GoodsRemain);
    // A fitting same-group residual clears the floor-limit reason; the label never changes dispatch.
    vector<Atom> withSmall(displays.begin(), displays.begin() + 30);
    withSmall.push_back(unitOf("small", "A", 20.0, 20.0, 20.0, 50.0));
    const M5LaneDateInput control = laneWith(withSmall, 1);
    const M5LaneOutcome fitting = plan(control, syntheticProfile());
    CHECK(shippedCount(fitting) == 31);
    REQUIRE(fitting.fullness.size() == 1);
    CHECK_FALSE(fitting.fullness.front().byFloorLimit);
    m5Evidence::note("expected", {"30 ship", "FULL by floor limit, no-fit witness recorded", "rear 30 in, 24,000 lb", "fitting control clears it"});
}

TEST_CASE("U-AC-05/synthetic: 30 floor-full displays below x/y are withheld; floor fullness is no override" * doctest::test_suite("m5")) {
    vector<Atom> displays;
    for (int unit = 0; unit < 30; ++unit) displays.push_back(display("d" + to_string(100 + unit)));
    const M5LaneDateInput input = laneWith(displays, 1);
    vector<size_t> all(input.atoms.size());
    iota(all.begin(), all.end(), size_t{0});
    // The truck is legal and floor-full: one display per position, none stacked, 600 in of the 630 in floor.
    const PhysicalCheck floorFull = checkTruck(input, all, kDefaultTrailer, physicalSettings());
    REQUIRE(floorFull.fits);
    CHECK(floorFull.layout.positions == 30);
    CHECK(floorFull.layout.usedLengthIn == doctest::Approx(600.0));
    double weight = 0.0, cube = 0.0;
    for (const Atom& atom : input.atoms) {
        CHECK(atom.floor.levels == 1);
        weight += atom.weightLb;
        cube += atom.floor.cubeCubicIn;
    }
    CHECK(weight == 24000.0);
    CHECK(cube == 2'880'000.0);
    const AcceptanceDecision decision = evaluateAcceptance(weight, cube, NumericAcceptance{40000.0, 3000.0});
    CHECK_FALSE(decision.byWeight);
    CHECK_FALSE(decision.byCube);

    const M5LaneOutcome withheld = plan(input, withAcceptance(syntheticProfile(), 40000.0, 3000.0));
    CHECK(shippedCount(withheld) == 0);
    CHECK(withheld.leftOver.size() == 30);
    for (const LeftOver& left : withheld.leftOver) CHECK(left.cause == LeftOverCause::AcceptanceWithheld);
    m5Evidence::note("expected", {"withheld", "30 positions, 600 in", "24,000 lb", "1,666.67 cu ft"});
}

TEST_CASE("X-E2E-09 30 displays: exact cube 5000/3 cu ft; y = 3,000 withholds, y = 1,000 dispatches" * doctest::test_suite("m5")) {
    vector<Atom> displays;
    for (int unit = 0; unit < 30; ++unit) displays.push_back(display("d" + to_string(100 + unit)));
    const M5LaneDateInput input = laneWith(displays, 1);
    double cube = 0.0;
    for (const Atom& atom : input.atoms) cube += atom.floor.cubeCubicIn;
    CHECK(cube == 2'880'000.0);
    CHECK(cube / 1728.0 == doctest::Approx(5000.0 / 3.0));
    CHECK(shippedCount(plan(input, withAcceptance(syntheticProfile(), 40000.0, 3000.0))) == 0);
    const M5LaneOutcome dispatched = plan(input, withAcceptance(syntheticProfile(), 40000.0, 1000.0));
    CHECK(shippedCount(dispatched) == 30);
    CHECK(dispatched.selected->trucks.front().layout.positions == 30);
}

TEST_CASE("X-E2E-04 acceptance applies to each truck on its own, whatever its group" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith({unitOf("a", "A", 378, 100, 108, 10000), unitOf("b", "B", 378, 100, 108, 10000)}, 2);
    CHECK(input.atoms[0].floor.cubeCubicIn / 1728.0 == doctest::Approx(2362.5));
    CHECK(shippedCount(plan(input, withAcceptance(syntheticProfile(), 40000.0, 2000.0))) == 2);
    CHECK(shippedCount(plan(input, withAcceptance(syntheticProfile(), 40000.0, 2800.0))) == 0);
    const M5LaneOutcome production = plan(input, syntheticProfile());
    CHECK(shippedCount(production) == 2);
    for (const FullnessLabel& label : production.fullness) CHECK(label.bucket == FullnessBucket::NoGoodsRemain);
}

TEST_CASE("U-FL-05 buckets come from the final residual, whatever the truck order" * doctest::test_suite("m5")) {
    M5LaneDateInput input = laneWith(concat(fx30Units("a", 40, "A"), fx30Units("b", 30, "B")), 3);
    input.excluded.push_back({"later-b", "B", 4.0, "due after the pull-forward window"});
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan forward = *plan(input, syntheticProfile()).selected;
    CandidatePlan reversed = forward;
    reverse(reversed.trucks.begin(), reversed.trucks.end());
    auto buckets = [&](const CandidatePlan& candidate) {
        multiset<pair<string, int>> result;
        for (const FullnessLabel& label :
             evaluateFullness(candidate, snapshot, remainingAfterSelected(snapshot, candidate), syntheticProfile(), physicalSettings())) {
            result.insert({candidate.trucks[label.truck].group, static_cast<int>(*label.bucket)});
        }
        return result;
    };
    CHECK(buckets(forward) == buckets(reversed));
    const auto all = buckets(forward);
    CHECK(all.count({"A", static_cast<int>(FullnessBucket::NoGoodsRemain)}) == 2);
    CHECK(all.count({"B", static_cast<int>(FullnessBucket::RuleLimited)}) == 1);
}

TEST_CASE("U-FL-06 exact 98% weight and 14 in rear boundaries, on the slot's own limit" * doctest::test_suite("m5")) {
    for (const auto& [weight, full] : vector<pair<double, bool>>{{44099.999, false}, {44100.0, true}, {44100.001, true}}) {
        const M5LaneDateInput input = laneWith({unitOf("load", "A", 200, 100, 108, weight), unitOf("left", "A", 100, 20, 108, 1)}, 1);
        const FullnessLabel label = labelOf(input, {0});
        CHECK(label.byWeight == full);
        CHECK(label.rearInches == 430.0);
        CHECK(label.fitStatus == RemainingFitStatus::Fits);
    }
    const TrailerCapability light{630.0, 100.0, 108.0, 30000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput onLight = laneWith({unitOf("load", "A", 200, 100, 108, 29400), unitOf("left", "A", 100, 20, 108, 1)}, 1, light);
    CHECK(labelOf(onLight, {0}).byWeight);
    const M5LaneDateInput onHeavy = laneWith({unitOf("load", "A", 200, 100, 108, 29400), unitOf("left", "A", 100, 20, 108, 1)}, 1);
    CHECK_FALSE(labelOf(onHeavy, {0}).byWeight);

    const double step = 1.0 / 16384.0;
    const vector<tuple<double, double, bool>> rears{{616.0 - step, 14.00006103515625, false}, {616.0, 14.0, true}, {616.0 + step, 13.99993896484375, true}};
    for (const auto& [length, rear, full] : rears) {
        const M5LaneDateInput input = laneWith({unitOf("load", "A", length, 100, 108, 1000), unitOf("left", "A", 100, 1, 108, 1)}, 1);
        const FullnessLabel label = labelOf(input, {0});
        CHECK(label.rearInches == rear);
        CHECK(label.byRearInches == full);
    }
}

TEST_CASE("U-FL-07 rear space is measured after M2's legal stacking; old labels do not carry" * doctest::test_suite("m5")) {
    const Atom leftover = unitOf("left", "A", 100, 20, 108, 100);
    const M5LaneDateInput threeHigh = laneWith({stackOfLevels("s", 210, 100, 36, 3, 1000), leftover}, 1);
    const FullnessLabel three = labelOf(threeHigh, {0});
    CHECK(three.rearInches == 420.0);
    CHECK(three.state == FullnessState::NotFull);
    const M5LaneDateInput twoHigh = laneWith({stackOfLevels("s", 210, 100, 36, 2, 1000), stackOfLevels("t", 210, 100, 36, 1, 1000), leftover}, 1);
    const FullnessLabel two = labelOf(twoHigh, {0, 1});
    CHECK(two.rearInches == 210.0);
    CHECK(two.state == FullnessState::NotFull);
    vector<Atom> single;
    for (int unit = 0; unit < 3; ++unit) single.push_back(stackOfLevels("s" + to_string(unit), 210, 100, 36, 1, 1000));
    single.push_back(leftover);
    const FullnessLabel apart = labelOf(laneWith(single, 1), {0, 1, 2});
    CHECK(apart.rearInches == 0.0);
    CHECK(apart.byRearInches);
    // A label belongs to its candidate revision: after a change it no longer describes the truck.
    CandidatePlan candidate = truckOf(laneWith(single, 1), {0, 1, 2});
    const uint64_t labelled = candidate.revision;
    CandidateEditor(candidate).removeToLeftOver(0, 2, LeftOverCause::OverReservation);
    CHECK(candidate.revision != labelled);
}

TEST_CASE("U-FL-08 the six residual-scope variants" * doctest::test_suite("m5")) {
    const Atom loaded = unitOf("load", "A", 600, 100, 108, 1000);
    const Atom fits = unitOf("fits", "A", 100, 20, 108, 100);
    const Atom tooLong = unitOf("long", "A", 100, 40, 108, 100);
    const FullnessLabel one = labelOf(laneWith({loaded, fits}, 1, kDefaultTrailer, true), {0});
    CHECK(one.state == FullnessState::NotFull);
    CHECK(one.bucket == FullnessBucket::GoodsRemain);
    const FullnessLabel two = labelOf(laneWith({loaded, tooLong}, 1, kDefaultTrailer, true), {0});
    CHECK(two.byFloorLimit);
    CHECK(two.bucket == FullnessBucket::GoodsRemain);
    const FullnessLabel three = labelOf(laneWith({loaded}, 1, kDefaultTrailer, true), {0});
    CHECK_FALSE(three.byFloorLimit);
    CHECK(three.fitStatus == RemainingFitStatus::NotApplicable);
    CHECK(three.bucket == FullnessBucket::NoGoodsRemain);
    Atom farFuture = fits;
    farFuture.components[0].dueDate = dayAfter(11);
    farFuture.components[0].offerKind = OfferKind::CanGo;
    const FullnessLabel four = labelOf(laneWith({loaded, farFuture}, 1, kDefaultTrailer, true), {0});
    CHECK(four.bucket == FullnessBucket::RuleLimited);
    CHECK_FALSE(four.byFloorLimit);
    const FullnessLabel five = labelOf(laneWith({loaded, tooLong, farFuture}, 1, kDefaultTrailer, true), {0});
    CHECK(five.bucket == FullnessBucket::GoodsRemain);
    CHECK(five.probes == 1);
    Atom otherGroup = fits;
    otherGroup.group = "B";
    const FullnessLabel six = labelOf(laneWith({loaded, otherGroup}, 1, kDefaultTrailer, true), {0});
    CHECK(six.fitStatus == RemainingFitStatus::NotApplicable);
}

TEST_CASE("U-FL-10 rear comes from the farthest occupied extent of the supplied witness" * doctest::test_suite("m5")) {
    const WitnessMeasure sideBySide = measureSuppliedWitness({{0, 0, 500, 50}, {0, 50, 600, 50}}, kDefaultTrailer);
    CHECK(sideBySide.valid);
    CHECK(sideBySide.occupiedExtentIn == 600.0);
    CHECK(sideBySide.rearInches == 30.0);
    const WitnessMeasure reordered = measureSuppliedWitness({{0, 50, 600, 50}, {0, 0, 500, 50}}, kDefaultTrailer);
    CHECK(reordered.rearInches == 30.0);
    const WitnessMeasure translated = measureSuppliedWitness({{20, 0, 500, 50}, {20, 50, 600, 50}}, kDefaultTrailer);
    CHECK(translated.rearInches == 10.0);
    CHECK_FALSE(measureSuppliedWitness({{0, 0, 500, 50}, {31, 50, 600, 50}}, kDefaultTrailer).valid);
    CHECK_FALSE(measureSuppliedWitness({{0, 0, 500, 60}, {0, 50, 600, 50}}, kDefaultTrailer).valid);
    CHECK_FALSE(measureSuppliedWitness({{numeric_limits<double>::quiet_NaN(), 0, 500, 50}}, kDefaultTrailer).valid);
}

TEST_CASE("U-RM-04 fullness and the optional acceptance are independent" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 29), 1);
    vector<size_t> twentyEight(28);
    for (size_t index = 0; index < 28; ++index) twentyEight[index] = index;
    const FullnessLabel label = labelOf(input, twentyEight);
    CHECK(label.state == FullnessState::NotFull);
    CHECK(label.rearInches == 70.0);
    CHECK(evaluateAcceptance(42000.0, 28 * 216000.0, {40000.0, 3000.0}).accepted);
    const M5LaneDateInput thirty = laneWith(fx30Units("u", 30), 1);
    vector<size_t> all(30);
    for (size_t index = 0; index < 30; ++index) all[index] = index;
    CHECK(labelOf(thirty, all).state == FullnessState::Full);
    CHECK_FALSE(evaluateAcceptance(45000.0, 30 * 216000.0, {50000.0, 4000.0}).accepted);
}

TEST_CASE("U-DA-05 a reserved trailer of its own size overrides the run's default" * doctest::test_suite("m5")) {
    const TrailerCapability own{600.0, 100.0, 108.0, 40000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput input = laneWith(fx30Units("u", 30), 1, own);
    CHECK(bestSingleClassLoad(input, 0, own, physicalSettings(), 100) == 26);
    CHECK(shippedCount(plan(input, syntheticProfile())) == 26);
}

TEST_CASE("X-RES-12 X-E2E-06 the physical adapter sizes each trailer; a mixed pool is refused (C9)" * doctest::test_suite("m5")) {
    const TrailerCapability small{630.0, 100.0, 108.0, 30000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput input = laneWith(fx30Units("u", 50), 2);
    CHECK(bestSingleClassLoad(input, 0, kDefaultTrailer, physicalSettings(), 100) == 30);
    CHECK(bestSingleClassLoad(input, 0, small, physicalSettings(), 100) == 20);
    M5LaneDateInput mixed = input;
    mixed.pool.slots[1] = small;
    const M5LaneOutcome outcome = plan(mixed, syntheticProfile());
    CHECK(outcome.kind == OutcomeKind::InvalidInput);
    CHECK(outcome.findings.front().find("C9") != string::npos);
}

// ---------------------------------------------------------------------------------------------------
// Physical boundaries at one supported input step (tests v12 Section 42.2; D3: the step is 0.01 in and
// 0.01 lb). Oracles work in whole hundredths written out by hand and in rectangles of their own; they
// never call the floor-pattern code or the exact check under test.
// ---------------------------------------------------------------------------------------------------

namespace {

ob::m5::PhysicalSettings fixedOrientation(ob::FixedOrientation side) {
    ob::m5::PhysicalSettings settings = physicalSettings();
    settings.floor.allowRotation = false;
    settings.floor.fixedOrientation = side;
    return settings;
}

const ob::FloorRow* rowOfKind(const vector<ob::FloorRow>& rows, ob::FloorPatternKind kind) {
    const auto found = find_if(rows.begin(), rows.end(), [&](const ob::FloorRow& row) { return row.pattern == kind; });
    return found == rows.end() ? nullptr : &*found;
}

vector<ob::FloorItem> floorsOf(const M5LaneDateInput& input) {
    vector<ob::FloorItem> floors;
    for (const Atom& atom : input.atoms) floors.push_back(atom.floor);
    return floors;
}

bool physicalSectionPasses(const M5LaneDateInput& input, const vector<size_t>& atoms) {
    const ValidationReport report = validateFinal(truckOf(input, atoms), *buildSnapshot(input).snapshot, syntheticProfile(), physicalSettings());
    return report.physical == SectionStatus::Pass && report.allPassed();
}

}  // namespace

TEST_CASE("X-PAT-13 U-PB-08 hundredths: a measure rounds up and a limit down off the 0.01 step; bad values are refused" * doctest::test_suite("m5")) {
    CHECK(hundredthsRoundedUp(630.0) == 63000);
    CHECK(hundredthsRoundedUp(630.01) == 63001);
    CHECK(hundredthsRoundedUp(33.3) == 3330);
    CHECK(hundredthsRoundedUp(45000.01) == 4500001);
    CHECK(hundredthsRoundedUp(629.999) == 63000);
    CHECK(hundredthsRoundedDown(629.999) == 62999);
    CHECK(hundredthsRoundedDown(100.0 + 1e-12) == 10000);
    for (const double bad : {-0.01, numeric_limits<double>::quiet_NaN(), numeric_limits<double>::infinity(), 1e300}) {
        CHECK_FALSE(hundredthsRoundedUp(bad));
        CHECK_FALSE(hundredthsRoundedDown(bad));
    }
}

TEST_CASE("X-PAT-13/twoAcross100: two fixed 50.00 in widths make a 100.00 in row; 50.01 makes that row too wide, not the floor" * doctest::test_suite("m5")) {
    const ob::m5::PhysicalSettings fixed = fixedOrientation(ob::FixedOrientation::LongSideAcross);
    const ob::TrailerSpec trailer = trailerOf(kDefaultTrailer);
    // Oracle in hundredths of an inch: 5000 + 5000 = 10000 <= 10000; 5001 + 5000 = 10001 > 10000.
    constexpr int64_t kWidth = 10000;
    CHECK(5000 + 5000 <= kWidth);
    CHECK(5001 + 5000 > kWidth);

    const M5LaneDateInput exact = laneWith({unitOf("w0", "A", 50.00, 40, 80, 100), unitOf("w1", "A", 50.00, 40, 80, 100)}, 1);
    const ob::FloorRow* pair = rowOfKind(ob::candidateRows(floorsOf(exact), {0, 1}, trailer, fixed.floor), ob::FloorPatternKind::WideWide);
    REQUIRE(pair != nullptr);
    CHECK(hundredthsRoundedUp(pair->widthIn) == 10000);
    const PhysicalCheck together = checkTruck(exact, {0, 1}, kDefaultTrailer, fixed);
    CHECK(together.fits);
    REQUIRE(together.layout.rows.size() == 1);
    CHECK(together.layout.rows[0].pattern == ob::FloorPatternKind::WideWide);
    CHECK(m5LayoutChecker::witnessProblems(together.layout, floorsOf(exact), 630, 100, 108, fixed.floor).empty());

    const M5LaneDateInput wider = laneWith({unitOf("w0", "A", 50.01, 40, 80, 100), unitOf("w1", "A", 50.00, 40, 80, 100)}, 1);
    const ob::FloorRow* widened = rowOfKind(ob::candidateRows(floorsOf(wider), {0, 1}, trailer, fixed.floor), ob::FloorPatternKind::WideWide);
    REQUIRE(widened != nullptr);
    CHECK(hundredthsRoundedUp(widened->widthIn) == 10001);
    // The declared row fails, but the floor does not: each stack takes a row of its own.
    const PhysicalCheck apart = checkTruck(wider, {0, 1}, kDefaultTrailer, fixed);
    CHECK(apart.fits);
    CHECK_FALSE(hasPattern(apart.layout, ob::FloorPatternKind::WideWide));
    CHECK(apart.layout.rows.size() == 2);
    for (const ob::FloorRow& row : apart.layout.rows) CHECK(hundredthsRoundedUp(row.widthIn) <= kWidth);
    CHECK(m5LayoutChecker::witnessProblems(apart.layout, floorsOf(wider), 630, 100, 108, fixed.floor).empty());
    m5Evidence::note("expected", {{"rowWidthHundredths", {10000, 10001}}, {"floorFitsAfterStep", true}});
}

TEST_CASE("X-PAT-13/microLengthBoundary: 630.00 in fits the 630 in floor exactly; 630.01 in does not, whatever the rounding" * doctest::test_suite("m5")) {
    // Oracle: 63000 <= 63000; 63001 > 63000; turned, 63001 > 10000 across.
    CHECK(63000 <= 63000);
    CHECK(63001 > 63000);
    const M5LaneDateInput exact = laneWith({unitOf("long", "A", 630.00, 100, 80, 100)}, 1);
    const PhysicalCheck fits = checkTruck(exact, {0}, kDefaultTrailer, physicalSettings());
    CHECK(fits.fits);
    CHECK(rearInchesOf(fits.layout, kDefaultTrailer) == 0.0);
    CHECK(m5LayoutChecker::witnessProblems(fits.layout, floorsOf(exact), 630, 100, 108, physicalSettings().floor).empty());
    CHECK(physicalSectionPasses(exact, {0}));

    const M5LaneDateInput over = laneWith({unitOf("long", "A", 630.01, 100, 80, 100)}, 1);
    const PhysicalCheck refused = checkTruck(over, {0}, kDefaultTrailer, physicalSettings());
    CHECK_FALSE(refused.fits);
    CHECK_FALSE(refused.undecided);
    CHECK_FALSE(refused.findings.empty());
    CHECK_FALSE(physicalSectionPasses(over, {0}));
    CHECK(bestSingleClassLoad(over, 0, kDefaultTrailer, physicalSettings(), 1) == 0);

    // Supplied witnesses are measured as given.
    CHECK(measureSuppliedWitness({{0, 0, 630.00, 100}}, kDefaultTrailer).valid);
    CHECK(measureSuppliedWitness({{0, 0, 630.00, 100}}, kDefaultTrailer).rearInches == 0.0);
    CHECK_FALSE(measureSuppliedWitness({{0, 0, 630.01, 100}}, kDefaultTrailer).valid);
    m5Evidence::note("expected", {{"lengthHundredths", {63000, 63001}}, {"fits", {true, false}}});
}

TEST_CASE("U-PB-04/rotationOnly: an 80 x 55 in stack fits a 60 in floor only turned, 55 in along and 80 in across" * doctest::test_suite("m5")) {
    const TrailerCapability shortFloor{60.0, 100.0, 108.0, 45000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput input = laneWith({unitOf("turn", "A", 80, 55, 80, 100)}, 1, shortFloor);
    // Oracle: both orientations in hundredths. 80 along: 8000 > 6000. 55 along: 5500 <= 6000 and 8000 <= 10000.
    CHECK(8000 > 6000);
    CHECK((5500 <= 6000 && 8000 <= 10000));

    const PhysicalCheck turned = checkTruck(input, {0}, shortFloor, physicalSettings());
    CHECK(turned.fits);
    REQUIRE(turned.layout.rows.size() == 1);
    CHECK(turned.layout.rows[0].depthIn == 55.0);
    CHECK(turned.layout.rows[0].widthIn == 80.0);
    CHECK(rearInchesOf(turned.layout, shortFloor) == 5.0);
    CHECK(m5LayoutChecker::witnessProblems(turned.layout, floorsOf(input), 60, 100, 108, physicalSettings().floor).empty());
    CHECK(measureSuppliedWitness({{0, 0, 55, 80}}, shortFloor).valid);
    CHECK_FALSE(measureSuppliedWitness({{0, 0, 80, 55}}, shortFloor).valid);

    // A packer that never turns the stack (80 in along) cannot load it.
    const PhysicalCheck unturned = checkTruck(input, {0}, shortFloor, fixedOrientation(ob::FixedOrientation::ShortSideAcross));
    CHECK_FALSE(unturned.fits);
    CHECK_FALSE(unturned.undecided);
    m5Evidence::note("expected", {{"fitsTurned", true}, {"fitsUnturned", false}, {"rearInches", 5}});
}

TEST_CASE("U-PB-08/payloadExactAndEpsilon: 45,000.00 lb is legal on a 45,000 lb truck; 45,000.01 lb is not, FULL or not" * doctest::test_suite("m5")) {
    // Oracle in hundredths of a pound: 4500000 <= 4500000; 4500001 > 4500000.
    CHECK(4500000 <= 4500000);
    CHECK(4500001 > 4500000);
    const M5LaneDateInput exact = laneWith({unitOf("heavy", "A", 100, 20, 90, 45000.00)}, 1);
    CHECK(checkTruck(exact, {0}, kDefaultTrailer, physicalSettings()).fits);
    CHECK(physicalSectionPasses(exact, {0}));
    CHECK(labelOf(exact, {0}).byWeight);

    const M5LaneDateInput over = laneWith({unitOf("heavy", "A", 100, 20, 90, 45000.01)}, 1);
    const PhysicalCheck refused = checkTruck(over, {0}, kDefaultTrailer, physicalSettings());
    CHECK_FALSE(refused.fits);
    CHECK_FALSE(refused.undecided);
    CHECK_FALSE(refused.findings.empty());
    // Full by weight is a label; it never makes an over-payload truck legal.
    CHECK(labelOf(over, {0}).byWeight);
    CHECK_FALSE(physicalSectionPasses(over, {0}));
    CHECK(bestSingleClassLoad(over, 0, kDefaultTrailer, physicalSettings(), 1) == 0);
    m5Evidence::note("expected", {{"payloadHundredths", {4500000, 4500001}}, {"legal", {true, false}}});
}

TEST_CASE("U-PB-08/simultaneousBinding: a 630 x 100 in, 45,000 lb stack binds floor and payload at once; a 1 lb extra is refused" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith({unitOf("block", "A", 630, 100, 80, 45000), unitOf("extra", "A", 1, 1, 80, 1)}, 1);
    // Oracle: floor 63000 + 1 = 63001 sq in > 63000; payload 4500000 + 100 = 4500100 hundredths > 4500000.
    CHECK(630 * 100 + 1 * 1 > 630 * 100);
    CHECK(4500000 + 100 > 4500000);

    CHECK(checkTruck(input, {0}, kDefaultTrailer, physicalSettings()).fits);
    CHECK(physicalSectionPasses(input, {0}));
    CHECK(checkTruck(input, {1}, kDefaultTrailer, physicalSettings()).fits);
    const PhysicalCheck both = checkTruck(input, {0, 1}, kDefaultTrailer, physicalSettings());
    CHECK_FALSE(both.fits);
    CHECK_FALSE(both.undecided);
    CHECK_FALSE(physicalSectionPasses(input, {0, 1}));

    // Witnesses claiming both: past the floor's end, or on top of the block.
    CHECK_FALSE(measureSuppliedWitness({{0, 0, 630, 100}, {630, 0, 1, 1}}, kDefaultTrailer).valid);
    CHECK_FALSE(measureSuppliedWitness({{0, 0, 630, 100}, {0, 0, 1, 1}}, kDefaultTrailer).valid);
    m5Evidence::note("expected", {{"aloneFits", true}, {"bothFit", false}});
}

TEST_CASE("X-PAT-19/threeAcrossM4Cutoff: M4 forms three-across while the three sides sum to the width; a missing row is no no-fit proof" * doctest::test_suite("m5")) {
    // Spec Section 43: M4's three-across turns the short side across and is formed whenever the three
    // short sides sum to no more than the trailer width; there is no separate side <= 33.3 in test.
    // Footprints 40 x s in, never turned, 80 in tall, 100 lb.
    const ob::m5::PhysicalSettings fixed = fixedOrientation(ob::FixedOrientation::ShortSideAcross);
    const ob::TrailerSpec trailer = trailerOf(kDefaultTrailer);
    struct Step {
        double sideIn;
        int64_t sumHundredths;  // oracle: 3 x the side, written out
        bool rowFormed;
    };
    for (const Step& step : {Step{33.30, 9990, true}, Step{33.31, 9993, true}, Step{33.34, 10002, false}}) {
        CAPTURE(step.sideIn);
        CHECK((step.sumHundredths <= 10000) == step.rowFormed);
        const M5LaneDateInput input = laneWith({unitOf("t0", "A", 40, step.sideIn, 80, 100), unitOf("t1", "A", 40, step.sideIn, 80, 100),
                                                unitOf("t2", "A", 40, step.sideIn, 80, 100)},
                                               1);
        const ob::FloorRow* three =
            rowOfKind(ob::candidateRows(floorsOf(input), {0, 1, 2}, trailer, fixed.floor), ob::FloorPatternKind::ThreeAcross);
        REQUIRE(three != nullptr);
        CHECK(hundredthsRoundedUp(three->widthIn) == step.sumHundredths);

        // Whatever rows M4 offers, the floor holds all three: a missing three-across is not a no-fit proof.
        const PhysicalCheck truck = checkTruck(input, {0, 1, 2}, kDefaultTrailer, fixed);
        CHECK(truck.fits);
        CHECK_FALSE(truck.undecided);
        CHECK(hasPattern(truck.layout, ob::FloorPatternKind::ThreeAcross) == step.rowFormed);
        CHECK(m5LayoutChecker::witnessProblems(truck.layout, floorsOf(input), 630, 100, 108, fixed.floor).empty());

        // A supplied three-across witness is judged on its own rectangles.
        const vector<PlacedRectangle> acrossWitness{{0, 0, 40, step.sideIn}, {0, step.sideIn, 40, step.sideIn}, {0, 2 * step.sideIn, 40, step.sideIn}};
        CHECK(measureSuppliedWitness(acrossWitness, kDefaultTrailer).valid == step.rowFormed);
    }
    m5Evidence::note("expected", {{"rule", "three short sides sum <= trailer width (Section 43)"},
                                  {"sumHundredths", {9990, 9993, 10002}},
                                  {"threeAcrossFormed", {true, true, false}},
                                  {"floorFits", {true, true, true}}});
}

// ------------------------------------------------------------------------- stored rows as the witness

namespace {

ob::FloorRow storedRow(ob::FloorPatternKind pattern, const vector<pair<size_t, bool>>& placements, double depthIn, double widthIn) {
    ob::FloorRow row;
    row.pattern = pattern;
    for (const auto& [position, turned] : placements) row.placements.push_back({position, turned});
    row.depthIn = depthIn;
    row.widthIn = widthIn;
    return row;
}

bool physicalFindingHas(const ValidationReport& report, const string& text) {
    return any_of(report.findings.begin(), report.findings.end(), [&](const ValidationFinding& finding) {
        return finding.section == "physical" && finding.reason.find(text) != string::npos;
    });
}

}  // namespace

TEST_CASE("X-PAT-18 final validation judges the stored rows: illegal rows fail although a legal arrangement of the same stacks exists"
          * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith({unitOf("a", "A", 48, 40, 50, 1000), unitOf("b", "A", 48, 40, 50, 1000),
                                            unitOf("c", "A", 48, 40, 50, 1000)},
                                           1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const CandidatePlan legal = truckOf(input, {0, 1, 2});
    REQUIRE(validateFinal(legal, snapshot, syntheticProfile(), physicalSettings()).allPassed());
    REQUIRE(checkTruck(input, {0, 1, 2}, kDefaultTrailer, physicalSettings()).fits);

    // Three turned 48 x 40 stacks side by side take 120 in of a 100 in floor.
    CandidatePlan tooWide = legal;
    tooWide.trucks[0].layout.rows = {storedRow(ob::FloorPatternKind::ThreeAcross, {{0, true}, {1, true}, {2, true}}, 48, 120)};
    const ValidationReport wide = validateFinal(tooWide, snapshot, syntheticProfile(), physicalSettings());
    CHECK(wide.physical == SectionStatus::Fail);
    CHECK(physicalFindingHas(wide, "outside the trailer floor"));

    // A row stored shallower than its members claims rear space the floor does not have.
    CandidatePlan shallow = legal;
    shallow.trucks[0].layout.rows = {storedRow(ob::FloorPatternKind::WideWide, {{0, false}, {1, false}}, 20, 96),
                                     storedRow(ob::FloorPatternKind::Single, {{2, false}}, 40, 48)};
    const ValidationReport depth = validateFinal(shallow, snapshot, syntheticProfile(), physicalSettings());
    CHECK(depth.physical == SectionStatus::Fail);
    CHECK(physicalFindingHas(depth, "stored 2000 hundredths deep, but its members take 4000"));

    // Rows of a truck before an edit: a position it no longer carries.
    CandidatePlan stale = truckOf(input, {0, 1});
    stale.trucks[0].layout = legal.trucks[0].layout;
    const ValidationReport staleReport = validateFinal(stale, snapshot, syntheticProfile(), physicalSettings());
    CHECK(staleReport.physical == SectionStatus::Fail);
    CHECK(physicalFindingHas(staleReport, "which the truck does not carry"));

    // No rows at all is no witness, though the stacks would fit.
    CandidatePlan noRows = legal;
    noRows.trucks[0].layout = ob::FloorLayout();
    CHECK(physicalFindingHas(validateFinal(noRows, snapshot, syntheticProfile(), physicalSettings()), "off the floor"));

    // The site's position cap holds on the stored rows too.
    PhysicalSettings twoPositions = physicalSettings();
    twoPositions.floor.maxPositions = 2;
    CHECK(physicalFindingHas(validateFinal(legal, snapshot, syntheticProfile(), twoPositions), "over the site's cap of 2"));
    m5Evidence::note("expected", {"legal rows pass", "120 in row fails", "shallow row fails", "stale rows fail", "no rows fail",
                                  "position cap fails"});
}

TEST_CASE("X-PAT-18 two legal pinwheels stored by M4 pass final validation as their own rectangles" * doctest::test_suite("m5")) {
    vector<Atom> atoms;
    for (int unit = 0; unit < 8; ++unit) atoms.push_back(unitOf("p" + to_string(unit), "A", 56, 40, 50, 1000));
    const M5LaneDateInput input = laneWith(atoms, 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const CandidatePlan pinwheels = truckOf(input, {0, 1, 2, 3, 4, 5, 6, 7});
    const ob::FloorLayout& layout = pinwheels.trucks[0].layout;
    REQUIRE(layout.rows.size() == 2);
    for (const ob::FloorRow& row : layout.rows) REQUIRE(row.pattern == ob::FloorPatternKind::Pinwheel);

    vector<string> unreadable;
    const LayoutWitness witness = witnessOfStoredRows(input, pinwheels.trucks[0].atoms, layout, unreadable);
    CHECK(unreadable.empty());
    REQUIRE(witness.placements.size() == 8);
    CHECK(witness.claimedExtentIn == 192.0);
    // By hand: each pinwheel is 96 in deep and 96 in wide; front-left 56 across, front-right turned beside
    // it, back-left turned behind front-left, back-right behind front-right. (x along, y across, length, width)
    const vector<array<double, 4>> expected{{0, 0, 40, 56},   {0, 56, 56, 40},  {40, 0, 56, 40},  {56, 40, 40, 56},
                                            {96, 0, 40, 56},  {96, 56, 56, 40}, {136, 0, 56, 40}, {152, 40, 40, 56}};
    for (size_t index = 0; index < expected.size(); ++index) {
        const WitnessPlacement& placed = witness.placements[index];
        INFO("rectangle " << index);
        CHECK(placed.xIn == expected[index][0]);
        CHECK(placed.yIn == expected[index][1]);
        CHECK(placed.lengthIn == expected[index][2]);
        CHECK(placed.widthIn == expected[index][3]);
    }
    const ValidationReport report = validateFinal(pinwheels, snapshot, syntheticProfile(), physicalSettings());
    CHECK(report.allPassed());
    CHECK(report.physical == SectionStatus::Pass);

    // The same four stacks with the turns swapped are no pinwheel: refused as stored, not rearranged.
    CandidatePlan swapped = pinwheels;
    for (ob::FloorPlacement& placement : swapped.trucks[0].layout.rows[0].placements) placement.turned = !placement.turned;
    CHECK(physicalFindingHas(validateFinal(swapped, snapshot, syntheticProfile(), physicalSettings()), "do not form that pattern"));
    m5Evidence::note("expected", {{"pinwheels", 2}, {"extentIn", 192}, {"verdict", "pass"}, {"swappedTurns", "fail"}});
}

TEST_CASE("X-PAT-18 with pallets that cannot turn, a stored turned footprint fails; the unturned rows pass" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith({unitOf("a", "A", 48, 40, 50, 1000), unitOf("b", "A", 48, 40, 50, 1000)}, 1);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    PhysicalSettings noTurning = physicalSettings();
    noTurning.floor.allowRotation = false;
    noTurning.floor.fixedOrientation = ob::FixedOrientation::LongSideAcross;

    CandidatePlan unturned = truckOf(input, {0, 1});
    unturned.trucks[0].layout.rows = {storedRow(ob::FloorPatternKind::WideWide, {{0, false}, {1, false}}, 40, 96)};
    CHECK(validateFinal(unturned, snapshot, syntheticProfile(), noTurning).allPassed());

    CandidatePlan turned = unturned;
    turned.trucks[0].layout.rows = {storedRow(ob::FloorPatternKind::NarrowNarrow, {{0, true}, {1, true}}, 48, 80)};
    CHECK(validateFinal(turned, snapshot, syntheticProfile(), physicalSettings()).allPassed());  // a site that turns pallets
    const ValidationReport refused = validateFinal(turned, snapshot, syntheticProfile(), noTurning);
    CHECK(refused.physical == SectionStatus::Fail);
    CHECK(physicalFindingHas(refused, "cannot turn pallets"));
    m5Evidence::note("expected", {{"unturnedNoTurning", "pass"}, {"turnedTurningSite", "pass"}, {"turnedNoTurning", "fail"}});
}

TEST_CASE("D3 a witness's edges are judged in whole hundredths: 629.995 in reaches 630.00, 630.001 in reaches 630.01"
          * doctest::test_suite("m5")) {
    CHECK(measureSuppliedWitness({{0, 0, 629.995, 100}}, kDefaultTrailer).valid);
    CHECK_FALSE(measureSuppliedWitness({{0, 0, 630.001, 100}}, kDefaultTrailer).valid);
    CHECK(measureSuppliedWitness({{0, 0, 300, 50}, {300, 0, 330, 50}}, kDefaultTrailer).valid);  // touching is not overlap
    CHECK_FALSE(measureSuppliedWitness({{0, 0, 300.001, 50}, {300, 0, 330, 50}}, kDefaultTrailer).valid);
}
