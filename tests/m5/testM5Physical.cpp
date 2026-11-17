// M5's physical side: patterns, footprints and decks (through M4's own rules), witnesses, and the
// fullness labels' boundaries (v7 4.3, 13, 4.12 and 33).
#include "doctest.h"
#include "m5Ledger.hpp"
#include "m5Methods.hpp"
#include "m5Repair.hpp"
#include "m5Validation.hpp"
#include "paramsTypes.hpp"
#include "stackRules.hpp"
#include "support/m5Fixtures.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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

TEST_CASE("U-AC-05 productionProfile: a floor-full display truck ships; the x/y-only policy withholds it" * doctest::test_suite("m5")) {
    vector<Atom> displays;
    for (int unit = 0; unit < 31; ++unit) displays.push_back(display("d" + to_string(100 + unit)));
    const M5LaneDateInput input = laneWith(displays, 1);
    const M5LaneOutcome production = plan(input, syntheticProfile());
    CHECK(shippedCount(production) == 30);
    REQUIRE(production.fullness.size() == 1);
    CHECK(production.fullness.front().byFloorLimit);
    CHECK(shippedCount(plan(input, withAcceptance(syntheticProfile(), 40000.0, 3000.0))) == 0);
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
