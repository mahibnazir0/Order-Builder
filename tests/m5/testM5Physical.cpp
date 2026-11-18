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
#include "support/m5StubMethods.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>

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

namespace {

// Each row kind M4 offers for `members` of `items` on the default trailer, with the width it fills.
map<ob::FloorPatternKind, double> offeredWidths(const vector<ob::FloorItem>& items, const vector<size_t>& members) {
    map<ob::FloorPatternKind, double> widths;
    for (const ob::FloorRow& row : ob::candidateRows(items, members, trailerOf(kDefaultTrailer), physicalSettings().floor)) {
        widths[row.pattern] = row.widthIn;
    }
    return widths;
}

}  // namespace

TEST_CASE("U-PB-04/synthetic: wide-wide, wide-narrow, narrow-narrow, three-across and pinwheel are tried where they fit, all M4's" * doctest::test_suite("m5")) {
    // 48 x 40 in on the 100 in floor, widths by hand: wide-wide 96, wide-narrow 88, narrow-narrow 80 all fit;
    // three across needs 120 and does not.
    const vector<ob::FloorItem> displays(3, display("d").floor);
    const map<ob::FloorPatternKind, double> pairs = offeredWidths(displays, {0, 1});
    REQUIRE(pairs.count(ob::FloorPatternKind::WideWide) == 1);
    REQUIRE(pairs.count(ob::FloorPatternKind::WideNarrow) == 1);
    REQUIRE(pairs.count(ob::FloorPatternKind::NarrowNarrow) == 1);
    CHECK(pairs.at(ob::FloorPatternKind::WideWide) == 96.0);
    CHECK(pairs.at(ob::FloorPatternKind::WideNarrow) == 88.0);
    CHECK(pairs.at(ob::FloorPatternKind::NarrowNarrow) == 80.0);
    const map<ob::FloorPatternKind, double> threeDisplays = offeredWidths(displays, {0, 1, 2});
    if (threeDisplays.count(ob::FloorPatternKind::ThreeAcross)) CHECK(threeDisplays.at(ob::FloorPatternKind::ThreeAcross) > 100.0);
    // 32 x 30 in: three short sides take 90 in, so three across fits.
    const vector<ob::FloorItem> narrow(3, unitOf("n", "A", 32, 30, 50, 300).floor);
    const map<ob::FloorPatternKind, double> threeNarrow = offeredWidths(narrow, {0, 1, 2});
    REQUIRE(threeNarrow.count(ob::FloorPatternKind::ThreeAcross) == 1);
    CHECK(threeNarrow.at(ob::FloorPatternKind::ThreeAcross) == 90.0);
    // 56 x 52 in: no two stand side by side in 100 in (112, 108, 104); four interlock as a pinwheel.
    const vector<ob::FloorItem> squares(4, unitOf("q", "A", 56, 52, 50, 500).floor);
    const map<ob::FloorPatternKind, double> pairsOfSquares = offeredWidths(squares, {0, 1});
    for (const auto& [kind, width] : pairsOfSquares) {
        if (kind != ob::FloorPatternKind::Single) CHECK(width > 100.0);
    }
    CHECK(offeredWidths(squares, {0, 1, 2, 3}).count(ob::FloorPatternKind::Pinwheel) == 1);

    // No pattern generator of M5's own: M5's truck check returns exactly M4's arrangeFloor for the same items.
    for (const vector<ob::FloorItem>* items : {&displays, &narrow, &squares}) {
        vector<Atom> atoms;
        for (size_t index = 0; index < 12; ++index) {
            Atom atom = unitOf("a" + to_string(100 + index), "A", (*items)[0].longSideIn, (*items)[0].shortSideIn, 50, 300);
            atoms.push_back(atom);
        }
        const M5LaneDateInput input = laneWith(atoms, 1);
        vector<size_t> all(atoms.size());
        iota(all.begin(), all.end(), size_t{0});
        const PhysicalCheck check = checkTruck(input, all, kDefaultTrailer, physicalSettings());
        vector<ob::FloorItem> floors;
        for (const Atom& atom : atoms) floors.push_back(atom.floor);
        const ob::FloorLayout m4 = ob::arrangeFloor(floors, trailerOf(kDefaultTrailer), physicalSettings().floor);
        CHECK(check.fits == m4.fitsLength);
        CHECK(check.layout.usedLengthIn == m4.usedLengthIn);
        REQUIRE(check.layout.rows.size() == m4.rows.size());
        for (size_t row = 0; row < m4.rows.size(); ++row) CHECK(check.layout.rows[row].pattern == m4.rows[row].pattern);
    }
    m5Evidence::note("expected", {"48 x 40: WW 96, WN 88, NN 80 fit; three across 120 does not",
                                  "32 x 30: three across 90", "56 x 52: no pair fits, pinwheel offered",
                                  "M5's layout = M4 arrangeFloor (rows, kinds, length)"});
}

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

TEST_CASE("X-PAT-06/synthetic: PTL layers 49 x 39 and 47 x 41 stand 49 x 40 and 48 x 41, TLD keeps the layer; one product id is not one pattern" * doctest::test_suite("m5")) {
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
    product.layerLengthIn = 49.0;
    product.layerWidthIn = 39.0;
    const ob::UnitLoadFootprint firstOnTld = ob::unitLoadFootprint(product, tld, ob::FootprintRule::WrittenRule);
    CHECK(firstOnTld.longSideIn == 49.0);
    CHECK(firstOnTld.shortSideIn == 39.0);
    // The same product id on two footprints: a saved pattern is keyed by the physical class, never the id.
    const Atom first = unitOf("first", "A", 49, 40, 50, 500), second = unitOf("second", "A", 48, 41, 50, 500);
    REQUIRE(first.components[0].product == second.components[0].product);
    CHECK(physicalClassOf(laneWith({first, second}, 1)) == vector<size_t>{0, 1});
    CHECK(classSignatureOf(first) != classSignatureOf(second));
    CHECK(classSignatureOf(first) == classSignatureOf(unitOf("twin", "B", 49, 40, 50, 500)));
    m5Evidence::note("expected", {"PTL 49 x 40 and 48 x 41", "TLD 47 x 41 and 49 x 39", "same product id, two footprints: two classes"});
}

TEST_CASE("X-PAT-07/synthetic: content 51 in per unit load: two TLD stand 102 in and fit 108; two PTL with their own 6 in decks stand 114 and do not" * doctest::test_suite("m5")) {
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
    m5Evidence::note("expected", {"2 x 51 TLD = 102 in fits 108", "2 x (51 + 6) PTL = 114 in does not"});
}

TEST_CASE("X-PAT-15/synthetic: 36 in layers in 108 in: only the stackable product stands three high; copies and remainders keep their levels" * doctest::test_suite("m5")) {
    // M2 builds the stacks from each product's flags; M5 receives them as rigid atoms and never re-stacks.
    // Stackable S: 6 layers as two stacks of 3. Not stackable N: 6 singles. Two-high T: 5 layers as 2 + 2 + 1.
    // A 315 x 100 in footprint puts two atoms on a truck, so the 11 atoms need 6 trucks; 4 slots leave 3.
    const auto layersOf = [](const string& id, const string& product, int levels) {
        Atom atom = stackOfLevels(id, 315, 100, 36, levels, 300);
        atom.components[0].product = product;
        return atom;
    };
    vector<Atom> atoms{layersOf("s1", "S", 3), layersOf("s2", "S", 3)};
    for (int unit = 0; unit < 6; ++unit) atoms.push_back(layersOf("n" + to_string(unit), "N", 1));
    for (const int levels : {2, 2, 1}) atoms.push_back(layersOf("t" + to_string(atoms.size()), "T", levels));
    const map<string, int> maxLevelsOf{{"S", 3}, {"N", 1}, {"T", 2}};
    const M5LaneDateInput input = laneWith(atoms, 4);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(outcome.finalValidationPassed);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 4);
    CHECK(shippedCount(outcome) == 8);
    CHECK(outcome.leftOver.size() == 3);
    map<string, double> layersSeen;
    for (const TruckPlan& truck : outcome.selected->trucks) {
        CHECK(truck.layout.positions == static_cast<int>(truck.atoms.size()));
        for (const size_t atom : truck.atoms) {
            const Atom& placed = input.atoms[atom];
            CHECK(placed.floor.levels <= maxLevelsOf.at(placed.components[0].product));
            CHECK(placed.floor.heightIn == doctest::Approx(36.0 * placed.floor.levels));
            CHECK(placed.floor.heightIn <= 108.0);
            layersSeen[placed.components[0].product] += placed.unitLoads;
        }
    }
    for (const LeftOver& left : outcome.leftOver) {
        const Atom& remainder = input.atoms[left.atom];
        CHECK(left.cause == LeftOverCause::OverReservation);
        CHECK(remainder.floor.levels <= maxLevelsOf.at(remainder.components[0].product));
        layersSeen[remainder.components[0].product] += remainder.unitLoads;
    }
    CHECK(layersSeen == map<string, double>{{"N", 6.0}, {"S", 6.0}, {"T", 5.0}});
    const auto threeHigh = count_if(input.atoms.begin(), input.atoms.end(), [](const Atom& atom) { return atom.floor.levels == 3; });
    CHECK(threeHigh == 2);
    m5Evidence::note("expected", {"only S stands 3 high (108 in)", "N single, T at most 2 high", "8 ship on 4 trucks, 3 left, layers 6/6/5 conserved"});
}

TEST_CASE("U-PB-08 EC-05 a weight-bound product's pattern stops at the payload, not the floor" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith({unitOf("heavy", "A", 48, 40, 50, 2000)}, 1);
    CHECK(bestSingleClassLoad(input, 0, kDefaultTrailer, physicalSettings(), 100) == 22);
    const M5LaneDateInput light = laneWith({display("light")}, 1);
    CHECK(bestSingleClassLoad(light, 0, kDefaultTrailer, physicalSettings(), 100) == 30);
}

TEST_CASE("X-PAT-08/synthetic: a 625 in and a 42,000 lb pattern are refused on a 600 in, 40,000 lb trailer; a 590 in, 30,000 lb one still fits" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith({unitOf("long", "A", 625, 100, 108, 1000), unitOf("heavy", "A", 600, 100, 108, 42000),
                                            unitOf("fine", "A", 590, 100, 108, 30000)},
                                           1);
    const TrailerCapability shortTrailer{600.0, 100.0, 108.0, 40000.0, kFixtureUnitLoadCap};
    CHECK(checkTruck(input, {0}, kDefaultTrailer, physicalSettings()).fits);
    CHECK_FALSE(checkTruck(input, {0}, shortTrailer, physicalSettings()).fits);
    CHECK(checkTruck(input, {1}, kDefaultTrailer, physicalSettings()).fits);
    CHECK_FALSE(checkTruck(input, {1}, shortTrailer, physicalSettings()).fits);
    CHECK(checkTruck(input, {2}, shortTrailer, physicalSettings()).fits);
    // A pattern saved for the long trailer is keyed by the trailer's capability, so it is not reused as is.
    CHECK(kDefaultTrailer.signature() != shortTrailer.signature());
    // Planned on the short trailer: the two that no longer fit are left as unfittable, the fitting one ships.
    const M5LaneDateInput onShort = laneWith(input.atoms, 3, shortTrailer);
    const M5LaneOutcome outcome = plan(onShort, syntheticProfile());
    CHECK(shippedIds(outcome, onShort) == set<string>{"fine"});
    CHECK(causeOf(outcome, onShort, "long") == LeftOverCause::Unfittable);
    CHECK(causeOf(outcome, onShort, "heavy") == LeftOverCause::Unfittable);
    m5Evidence::note("expected", {"625 in and 42,000 lb refused on 600 in / 40,000 lb", "590 in / 30,000 lb ships"});
}

TEST_CASE("X-PAT-09/synthetic: master V2 of the same id, heavier or taller, never ships a V1-only load (also X-DATA-06)" * doctest::test_suite("m5")) {
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
    CHECK(classSignatureOf(fx30("v1")) != classSignatureOf(heavier));
    // V2 taller than the ceiling, same id: the V1 load of one unit no longer ships.
    Atom taller = fx30("v1");
    taller.floor.heightIn = 108.01;
    CHECK(classSignatureOf(fx30("v1")) != classSignatureOf(taller));
    const M5LaneDateInput tallerLane = laneWith({taller}, 1);
    const M5LaneOutcome tallerOutcome = plan(tallerLane, syntheticProfile());
    CHECK(shippedCount(tallerOutcome) == 0);
    CHECK(causeOf(tallerOutcome, tallerLane, "v1") == LeftOverCause::Unfittable);
    m5Evidence::note("expected", {"V1 1,500 lb: 30 per truck", "V2 1,600 lb: 28", "V2 108.01 in: not shipped"});
}

TEST_CASE("X-DATA-06/synthetic: a saved V1 shape is not reused at V2 weight" * doctest::test_suite("m5")) {
    vector<Atom> one = fx30Units("load-", 30);
    const M5LaneDateInput v1 = laneWith(one, 1);
    const M5LaneOutcome before = plan(v1, syntheticProfile());
    CHECK(shippedCount(before) == 30);
    for (Atom& atom : one) atom.weightLb = 1600.0;
    const M5LaneDateInput v2 = laneWith(one, 1);
    CHECK(classSignatureOf(v1.atoms[0]) != classSignatureOf(v2.atoms[0]));
    const M5LaneOutcome after = plan(v2, syntheticProfile());
    CHECK(shippedCount(after) == 28);
    CHECK(28 * 1600 == 44800);
    CHECK(29 * 1600 == 46400);
    m5Evidence::note("expected", {"V1 30 at 1,500 lb", "V2 28 at 1,600 lb", "29 would exceed 45,000 lb"});
}

TEST_CASE("X-PAT-02/synthetic: a pattern of 2 A + 1 B on stock A = 5, B = 4 makes 2 copies, uses A 4 and B 2, leaves A 1 and B 2" * doctest::test_suite("m5")) {
    const vector<long long> stock{5, 4}, perCopy{2, 1};
    const long long copies = mixedPatternCopies(stock, perCopy, 10);
    CHECK(copies == 2);
    vector<long long> used, left;
    for (size_t product = 0; product < stock.size(); ++product) {
        used.push_back(copies * perCopy[product]);
        left.push_back(stock[product] - used.back());
    }
    CHECK(used == vector<long long>{4, 2});
    CHECK(left == vector<long long>{1, 2});
    m5Evidence::note("expected", {"2 copies", "uses A 4, B 2", "left A 1, B 2"});
    CHECK(mixedPatternCopies({5, 4}, {2, 1}, 10) == 2);
    CHECK(mixedPatternCopies({5, 4}, {2, 1}, 1) == 1);
    CHECK(mixedPatternCopies({5, 0}, {2, 1}, 10) == 0);
    CHECK(mixedPatternCopies({5, 4}, {0, 0}, 10) == 0);
    CHECK_THROWS_AS(mixedPatternCopies({5}, {2, 1}, 10), invalid_argument);
}

TEST_CASE("X-PAT-05/synthetic: 30 eligible and 70 due 11 days later, pull limit 10, pattern 10: 3 copies, each keeping its lines, dates and priority" * doctest::test_suite("m5")) {
    vector<Atom> atoms;
    for (int unit = 0; unit < 30; ++unit) atoms.push_back(unitOf("now" + to_string(100 + unit), "A", 100, 63, 108, 4500));
    for (int unit = 0; unit < 70; ++unit) atoms.push_back(unitOf("late" + to_string(100 + unit), "A", 100, 63, 108, 4500, 0, dayAfter(11)));
    const M5LaneDateInput input = laneWith(atoms, 10, kDefaultTrailer, true);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 3);
    for (const TruckPlan& truck : outcome.selected->trucks) {
        CHECK(truck.atoms.size() == 10);
        for (const size_t atom : truck.atoms) {
            const Component& line = input.atoms[atom].components[0];
            CHECK(line.dueDate == kD);
            CHECK(line.availableDate == kD);
            CHECK(line.priority == 0);
            CHECK(line.source == "line-" + input.atoms[atom].id);
            CHECK(input.atoms[atom].id.rfind("now", 0) == 0);
        }
    }
    CHECK(outcome.leftOver.size() == 70);
    for (const LeftOver& left : outcome.leftOver) {
        CHECK(left.cause == LeftOverCause::NotEligible);
        CHECK(input.atoms[left.atom].components[0].dueDate == dayAfter(11));
    }
    m5Evidence::note("expected", {"3 copies of 10, all due D, priority 0, own source lines", "70 due D+11 left not eligible"});
}

TEST_CASE("X-PAT-10/synthetic X-STATE-06/synthetic: one pattern on 3 trucks, the middle one repaired: the others unchanged, the removed good back once" * doctest::test_suite("m5")) {
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
    CHECK(before[0].atoms.size() == 30);
    CHECK(before[2].atoms.size() == 30);
    CHECK(candidate.trucks[1].atoms.size() == 29);
    CHECK(count_if(candidate.leftOver.begin(), candidate.leftOver.end(), [&](const LeftOver& left) { return left.atom == removed; }) == 1);
    CHECK(seen.count(removed) == 0);
    CHECK(seen.size() + candidate.leftOver.size() == input.atoms.size());
    m5Evidence::note("expected", {"trucks 1 and 3 unchanged (30 each)", "middle 29", "removed unit left exactly once", "ledger balanced"});
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

TEST_CASE("U-PB-05/synthetic: TLD and GMA use the layer exactly; PGM and PTL the larger of 48 x 40 and the layer, per side" * doctest::test_suite("m5")) {
    // Decks as the pallet table gives them: 6 in on PTL and PGM, none on TLD and GMA; all 48 x 40 in.
    const vector<ob::PalletSpec> pallets{{"PTL", 60.0, 6.0, 48.0, 40.0}, {"PGM", 50.0, 6.0, 48.0, 40.0},
                                         {"TLD", 0.0, 0.0, 48.0, 40.0}, {"GMA", 0.0, 0.0, 48.0, 40.0}};
    struct Layer {
        double length, width;
    };
    // Longer on one side, shorter on the other; smaller on both; larger on both.
    for (const Layer layer : {Layer{49.0, 39.0}, Layer{47.0, 41.0}, Layer{44.0, 36.0}, Layer{52.0, 45.0}}) {
        ob::ProductRecord product;
        product.layerLengthIn = layer.length;
        product.layerWidthIn = layer.width;
        const double layerLong = max(layer.length, layer.width), layerShort = min(layer.length, layer.width);
        for (const ob::PalletSpec& pallet : pallets) {
            INFO(pallet.palletId << " under a " << layer.length << " x " << layer.width << " in layer");
            const ob::UnitLoadFootprint footprint = ob::unitLoadFootprint(product, pallet, ob::FootprintRule::WrittenRule);
            const bool realPallet = pallet.palletId == "PTL" || pallet.palletId == "PGM";
            CHECK(footprint.longSideIn == (realPallet ? max(layerLong, 48.0) : layerLong));
            CHECK(footprint.shortSideIn == (realPallet ? max(layerShort, 40.0) : layerShort));
            CHECK_FALSE(footprint.fromPalletOnly);
        }
    }
    m5Evidence::note("expected", {"TLD, GMA: layer exactly", "PTL, PGM: max(layer, 48 x 40) per side",
                                  "layers 49x39, 47x41, 44x36, 52x45"});
}

TEST_CASE("U-PB-06/synthetic: the deck is 6 in for PTL and PGM, 0 for TLD and GMA, counted once per pallet" * doctest::test_suite("m5")) {
    ob::M2Params params;
    params.floorDeckHeight = ob::DeckHeightRule::Included;
    ob::ProductRecord product;
    product.height_in = 10.0;
    product.layers_unit_load = 4;
    const vector<pair<ob::PalletSpec, double>> decks{{{"PTL", 60.0, 6.0, 48.0, 40.0}, 6.0}, {{"PGM", 50.0, 6.0, 48.0, 40.0}, 6.0},
                                                     {{"TLD", 0.0, 0.0, 48.0, 40.0}, 0.0}, {{"GMA", 0.0, 0.0, 48.0, 40.0}, 0.0}};
    for (const auto& [pallet, deck] : decks) {
        INFO(pallet.palletId);
        // Four 10 in layers on one pallet: one deck, never one per layer (4 x 6 = 24).
        CHECK(ob::unitLoadHeightIn(product, pallet, params) == 40.0 + deck);
    }
    // Two pallets stacked carry two decks: 2 x (51 + 6) = 114 in passes the 108 in ceiling, 2 x 51 = 102 does not.
    product.height_in = 51.0;
    product.layers_unit_load = 1;
    CHECK(2 * ob::unitLoadHeightIn(product, decks[0].first, params) == 114.0);
    CHECK(2 * ob::unitLoadHeightIn(product, decks[2].first, params) == 102.0);
    const M5LaneDateInput input = laneWith({unitOf("tld", "A", 48, 40, 102, 500), unitOf("ptl", "A", 48, 40, 114, 500)}, 1);
    CHECK(checkTruck(input, {0}, kDefaultTrailer, physicalSettings()).fits);
    CHECK_FALSE(checkTruck(input, {1}, kDefaultTrailer, physicalSettings()).fits);
    m5Evidence::note("expected", {"4 layers of 10 in: PTL/PGM 46, TLD/GMA 40", "two 51 in PTL: 114 > 108 refused; two TLD 102 fit"});
}

TEST_CASE("U-PB-07/synthetic: stacks stand as high as M4 built them (2, 3, 4 high); a non-stackable unit is never stacked" * doctest::test_suite("m5")) {
    // M4 built stacks of 2, 3 and 4 levels of 25 in (50, 75, 100 in, under 108): M5 places each as it is.
    vector<Atom> stacks{stackOfLevels("two", 48, 40, 25, 2, 300), stackOfLevels("three", 48, 40, 25, 3, 300),
                        stackOfLevels("four", 48, 40, 25, 4, 300)};
    const M5LaneDateInput stacked = laneWith(stacks, 1);
    const M5LaneOutcome placed = plan(stacked, syntheticProfile());
    REQUIRE(placed.selected);
    REQUIRE(placed.selected->trucks.size() == 1);
    CHECK(placed.selected->trucks.front().layout.positions == 3);
    double unitLoads = 0.0;
    for (const size_t atom : placed.selected->trucks.front().atoms) unitLoads += stacked.atoms[atom].unitLoads;
    CHECK(unitLoads == 9.0);
    // 32 non-stackable units 20 x 100 x 50 in, 100 lb: two would stand 100 in high, but M5 never stacks
    // what M4 left single, so 31 rows of 20 in fill 630 in and one is left.
    vector<Atom> singles;
    for (int unit = 0; unit < 32; ++unit) singles.push_back(unitOf("s" + to_string(100 + unit), "A", 100, 20, 50, 100));
    const M5LaneDateInput flat = laneWith(singles, 1);
    const M5LaneOutcome outcome = plan(flat, syntheticProfile());
    REQUIRE(outcome.selected);
    CHECK(shippedCount(outcome) == 630 / 20);
    CHECK(outcome.selected->trucks.front().layout.positions == 31);
    CHECK(outcome.leftOver.size() == 1);
    for (const Atom& atom : flat.atoms) CHECK(atom.floor.levels == 1);
    m5Evidence::note("expected", {"stacks of 2, 3, 4 levels: 3 positions, 9 unit loads", "32 non-stackable 50 in units: 31 ship single, 1 left"});
}

TEST_CASE("U-PB-08/synthetic: a weight-bound product's pattern stops at the payload, not the floor" * doctest::test_suite("m5")) {
    // 48 x 40 in, 2,000 lb: the floor holds 30 (15 wide-wide rows of 40 in), the payload floor(45,000 / 2,000) = 22.
    const M5LaneDateInput input = laneWith(vector<Atom>(1, unitOf("h", "A", 48, 40, 50, 2000)), 1);
    CHECK(bestSingleClassLoad(input, 0, kDefaultTrailer, physicalSettings(), 100) == 45000 / 2000);
    CHECK(bestSingleClassLoad(laneWith({display("light")}, 1), 0, kDefaultTrailer, physicalSettings(), 100) == 30);
    vector<Atom> heavy;
    for (int unit = 0; unit < 50; ++unit) heavy.push_back(unitOf("h" + to_string(100 + unit), "A", 48, 40, 50, 2000));
    const M5LaneDateInput lane = laneWith(heavy, 2);
    const M5LaneOutcome outcome = plan(lane, syntheticProfile());
    REQUIRE(outcome.selected);
    REQUIRE(outcome.selected->trucks.size() == 2);
    for (const TruckPlan& truck : outcome.selected->trucks) {
        CHECK(truck.atoms.size() == 22);
        CHECK(truck.layout.positions == 22);
        CHECK(truck.atoms.size() * 2000.0 <= 45000.0);
    }
    CHECK(count_if(outcome.leftOver.begin(), outcome.leftOver.end(),
                   [](const LeftOver& left) { return left.cause == LeftOverCause::OverReservation; }) == 6);
    m5Evidence::note("expected", {"pattern 22 = floor(45,000 / 2,000), not the floor's 30", "50 on 2: 22 + 22, 6 left"});
}

TEST_CASE("U-PB-11/synthetic: a pattern is judged best on its final contents; without proof it is labelled not proven" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 50), 2);
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    REQUIRE(outcome.selected);
    CandidatePlan candidate = *outcome.selected;
    REQUIRE(candidate.trucks.size() == 2);
    REQUIRE(candidate.validation);
    const uint64_t built = candidate.revision;
    // A legal trim (one unit moved from the fuller truck to the other): the proof and the validation both
    // go with the change; checked again on the final contents it passes, but stays not proven.
    const size_t fuller = candidate.trucks[0].atoms.size() >= candidate.trucks[1].atoms.size() ? 0 : 1;
    CandidateEditor(candidate).moveAtom(fuller, 1 - fuller, candidate.trucks[fuller].atoms.back());
    storeRows(candidate, input);
    CHECK(candidate.revision > built);
    CHECK(candidate.evidence.proof == ProofStatus::Unproven);
    CHECK_FALSE(candidate.validation.has_value());
    const ValidationReport recheck = validateFinal(candidate, snapshot, syntheticProfile(), physicalSettings());
    CHECK(recheck.allPassed());
    CHECK(recheck.revision == candidate.revision);
    CHECK(candidate.evidence.proof == ProofStatus::Unproven);
    // An illegal change (31 on one truck, 46,500 lb) is caught on the final contents.
    CandidatePlan overfull = *outcome.selected;
    while (overfull.trucks[1 - fuller].atoms.size() > 19) {
        CandidateEditor(overfull).moveAtom(1 - fuller, fuller, overfull.trucks[1 - fuller].atoms.back());
    }
    storeRows(overfull, input);
    CHECK(overfull.trucks[fuller].atoms.size() == 31);
    CHECK_FALSE(validateFinal(overfull, snapshot, syntheticProfile(), physicalSettings()).allPassed());
    m5Evidence::note("expected", {"legal trim: proof Unproven, validation dropped, recheck passes, still Unproven",
                                  "31 on a truck: final-contents recheck fails"});
}

namespace {

// FX-30's cube, 100 x 20 x 108 in, in cubic feet: 125.
constexpr double kFx30CubeCuFt = 100.0 * 20.0 * 108.0 / 1728.0;

M5LaneOutcome planFixed(const M5LaneDateInput& input, const M5PolicyProfile& profile, const ob::m5::GroupTrucks& trucks) {
    FakeClock clock;
    M5RunServices services(clock);
    services.methods = {make_shared<m5Stubs::FixedMethod>(MethodId::PatternRepeat, trucks)};
    return planLaneDate(input, profile, physicalSettings(), services);
}

vector<size_t> firstAtoms(size_t count) {
    vector<size_t> atoms(count);
    iota(atoms.begin(), atoms.end(), size_t{0});
    return atoms;
}

// 28 of 29 available FX-30: 42,000 lb, 3,500 cu ft, rear 70 in, the 29th still fits -> NOT FULL. With the
// optional x = 40,000 / y = 3,000 it is accepted; with acceptance OFF it ships all the same.
void requireBelowFull(const M5PolicyProfile& profile) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 29), 1);
    CHECK(28 * 1500.0 == 42000.0);
    CHECK(28 * kFx30CubeCuFt == 3500.0);
    const FullnessLabel label = labelOf(input, firstAtoms(28), profile);
    CHECK(label.state == FullnessState::NotFull);
    CHECK(label.rearInches == 70.0);
    CHECK(label.fitStatus == RemainingFitStatus::Fits);
    CHECK_FALSE(label.byWeight);
    const M5LaneOutcome outcome = planFixed(input, profile, {firstAtoms(28)});
    REQUIRE(outcome.selected);
    CHECK(shippedCount(outcome) == 28);
    CHECK(none_of(outcome.leftOver.begin(), outcome.leftOver.end(),
                  [](const LeftOver& left) { return left.cause == LeftOverCause::AcceptanceWithheld; }));
    REQUIRE(outcome.fullness.size() == 1);
    CHECK(outcome.fullness.front().state == FullnessState::NotFull);
    CHECK(outcome.fullness.front().bucket == FullnessBucket::GoodsRemain);
    REQUIRE(outcome.selected->validation);
    CHECK(outcome.selected->validation->acceptance == (profile.acceptance ? SectionStatus::Pass : SectionStatus::NotApplicable));
}

// 30 FX-30: 45,000 lb, FULL by weight. Under x = 50,000 / y = 4,000 both strict branches fail (45,000 and
// 3,750 cu ft): withheld whatever its label. With acceptance OFF it ships, FULL.
void requireFullTruck(const M5PolicyProfile& profile) {
    const M5LaneDateInput input = laneWith(fx30Units("u", 30), 1);
    const FullnessLabel label = labelOf(input, firstAtoms(30), profile);
    CHECK(label.state == FullnessState::Full);
    CHECK(label.byWeight);
    const M5LaneOutcome outcome = plan(input, profile);
    REQUIRE(outcome.fullness.size() == (profile.acceptance ? 0u : 1u));
    if (profile.acceptance) {
        const AcceptanceDecision decision = evaluateAcceptance(45000.0, 30 * 216000.0, *profile.acceptance);
        CHECK_FALSE(decision.byWeight);
        CHECK_FALSE(decision.byCube);
        CHECK(shippedCount(outcome) == 0);
        CHECK(outcome.leftOver.size() == 30);
        for (const LeftOver& left : outcome.leftOver) CHECK(left.cause == LeftOverCause::AcceptanceWithheld);
    } else {
        CHECK(shippedCount(outcome) == 30);
        CHECK(outcome.fullness.front().state == FullnessState::Full);
        CHECK(outcome.leftOver.empty());
    }
}

}  // namespace

TEST_CASE("U-RM-04/belowFullAccepted: 28 of 29 FX-30 pass optional x = 40,000 / y = 3,000 yet are NOT FULL; the truck ships" * doctest::test_suite("m5")) {
    requireBelowFull(withAcceptance(syntheticProfile(), 40000.0, 3000.0));
    m5Evidence::note("expected", {"42,000 lb, 3,500 cu ft, rear 70 in, 29th fits: NOT FULL", "accepted (strict OR), ships 28"});
}

TEST_CASE("U-RM-04/fullButWithheld: 30 FX-30 are FULL by weight but fail x = 50,000 / y = 4,000 and are withheld" * doctest::test_suite("m5")) {
    requireFullTruck(withAcceptance(syntheticProfile(), 50000.0, 4000.0));
    m5Evidence::note("expected", {"FULL by weight (45,000 lb)", "45,000 <= 50,000 and 3,750 <= 4,000: withheld, 30 left"});
}

TEST_CASE("U-RM-04/synthetic: both optional-policy controls: FULL never stands in for acceptance, nor acceptance for FULL" * doctest::test_suite("m5")) {
    requireBelowFull(withAcceptance(syntheticProfile(), 40000.0, 3000.0));
    requireFullTruck(withAcceptance(syntheticProfile(), 50000.0, 4000.0));
    m5Evidence::note("expected", {"below-full accepted truck ships NOT FULL", "FULL truck failing the policy is withheld"});
}

TEST_CASE("U-RM-04/productionProfile: acceptance OFF: the below-full truck and the FULL truck both ship with their labels" * doctest::test_suite("m5")) {
    REQUIRE_FALSE(syntheticProfile().acceptance.has_value());
    requireBelowFull(syntheticProfile());
    requireFullTruck(syntheticProfile());
    m5Evidence::note("expected", {"28 ship NOT FULL, goods remain", "30 ship FULL by weight", "acceptance not applicable"});
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

TEST_CASE("U-DA-05/adapterSeam: the physical adapter sizes a slot by its own capability, overriding the run's default trailer" * doctest::test_suite("m5")) {
    // FX-30 units are 1,500 lb: the default 45,000 lb trailer takes 30; a slot of its own 40,000 lb takes
    // floor(40,000 / 1,500) = 26, and 600 in still holds the rows (30 x 20 in).
    const TrailerCapability own{600.0, 100.0, 108.0, 40000.0, kFixtureUnitLoadCap};
    const M5LaneDateInput defaulted = laneWith(fx30Units("u", 30), 1);
    CHECK(bestSingleClassLoad(defaulted, 0, kDefaultTrailer, physicalSettings(), 100) == 30);
    const M5LaneDateInput input = laneWith(fx30Units("u", 30), 1, own);
    CHECK(bestSingleClassLoad(input, 0, own, physicalSettings(), 100) == 26);
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(outcome.finalValidationPassed);
    CHECK(shippedCount(outcome) == 26);
    REQUIRE(outcome.selected);
    REQUIRE(outcome.selected->trucks.size() == 1);
    double weight = 0.0;
    for (const size_t atom : outcome.selected->trucks[0].atoms) weight += input.atoms[atom].weightLb;
    CHECK(weight == 39000.0);
    CHECK(outcome.leftOver.size() == 4);
    for (const LeftOver& left : outcome.leftOver) CHECK(left.cause == LeftOverCause::OverReservation);
    m5Evidence::note("expected", {"default 53F slot: 30", "own 600 in / 40,000 lb slot: 26 (39,000 lb)", "4 left over the reservation"});
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

TEST_CASE("X-PAT-13/synthetic: length, width, height, stacked height and weight at the limit and 0.01 under fit; 0.01 over does not" * doctest::test_suite("m5")) {
    // Default trailer 630 x 100 x 108 in, 45,000 lb; every other measure of each unit is feasible.
    struct Probe {
        string name;
        Atom atom;
        bool fits;
    };
    const vector<Probe> probes{
        {"length at", unitOf("la", "A", 630.00, 50, 50, 500), true},
        {"length under", unitOf("lu", "A", 629.99, 50, 50, 500), true},
        {"length over", unitOf("lo", "A", 630.01, 50, 50, 500), false},
        {"length over by 0.001", unitOf("lm", "A", 630.001, 50, 50, 500), false},
        {"width at", unitOf("wa", "A", 630.00, 100.00, 50, 500), true},
        {"width under", unitOf("wu", "A", 630.00, 99.99, 50, 500), true},
        {"width over", unitOf("wo", "A", 630.00, 100.01, 50, 500), false},
        {"height at", unitOf("ha", "A", 48, 40, 108.00, 500), true},
        {"height under", unitOf("hu", "A", 48, 40, 107.99, 500), true},
        {"height over", unitOf("ho", "A", 48, 40, 108.01, 500), false},
        {"stacked at", stackOfLevels("sa", 48, 40, 54.00, 2, 500), true},
        {"stacked under", stackOfLevels("su", 48, 40, 53.995, 2, 500), true},
        {"stacked over", stackOfLevels("so", 48, 40, 54.005, 2, 500), false},
        {"weight at", unitOf("ga", "A", 48, 40, 50, 45000.00), true},
        {"weight under", unitOf("gu", "A", 48, 40, 50, 44999.99), true},
        {"weight over", unitOf("go", "A", 48, 40, 50, 45000.01), false},
    };
    for (const Probe& probe : probes) {
        INFO(probe.name);
        const M5LaneDateInput input = laneWith({probe.atom}, 1);
        CHECK(checkTruck(input, {0}, kDefaultTrailer, physicalSettings()).fits == probe.fits);
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        CHECK(shippedCount(outcome) == (probe.fits ? 1u : 0u));
        if (!probe.fits) CHECK(causeOf(outcome, input, probe.atom.id) == LeftOverCause::Unfittable);
    }
    m5Evidence::note("expected", {"at and 0.01 under: fit and ship", "0.01 over (and 630.001): unfittable", "length, width, height, stacked height, weight"});
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
