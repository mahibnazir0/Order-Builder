#include "doctest.h"
#include "../m4AugustFixtures.hpp"
#include "floorPattern.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

using namespace std;
using namespace ob;
using m4AugustTests::AugustItems;

namespace {

const TrailerSpec kTrailer{"53FT_NA", 630.0, 100.0, 108.0, 45000.0, 32, nullopt};

FloorPatternSettings defaultSettings() {
    FloorPatternSettings settings;
    settings.fullCubeFraction = 0.84;
    return settings;
}

FloorItem footprint(double longSide, double shortSide, int levels = 1, double height = 50.0) {
    return {longSide, shortSide, height, levels, longSide * shortSide * height};
}

vector<FloorItem> copies(const FloorItem& item, size_t count) { return vector<FloorItem>(count, item); }

FloorLayout arranged(const vector<FloorItem>& items, const FloorPatternSettings& settings = defaultSettings()) {
    return arrangeFloor(items, kTrailer, settings);
}

// Every item is on the floor exactly once or named unplaceable, no row is wider than the
// trailer, and the length used is the sum of the rows.
void checkLayoutInvariants(const FloorLayout& layout, size_t itemCount) {
    vector<int> seen(itemCount, 0);
    double length = 0.0;
    for (const FloorRow& row : layout.rows) {
        REQUIRE(row.widthIn <= kTrailer.interiorWidthIn + kQuantityEpsilon);
        REQUIRE(!row.placements.empty());
        length += row.depthIn;
        for (const FloorPlacement& placement : row.placements) ++seen[placement.itemIndex];
    }
    for (const size_t index : layout.unplaceableItems) ++seen[index];
    REQUIRE(all_of(seen.begin(), seen.end(), [](int count) { return count == 1; }));
    REQUIRE(layout.usedLengthIn == doctest::Approx(length));
    REQUIRE(layout.rearWasteIn == doctest::Approx(kTrailer.interiorLengthIn - length));
}

vector<FloorItem> randomItems(mt19937& random, size_t count) {
    uniform_real_distribution<double> shortSide(30.0, 48.0);
    uniform_real_distribution<double> extra(0.0, 10.0);
    uniform_int_distribution<int> levels(1, 2);
    vector<FloorItem> items;
    items.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const double width = shortSide(random);
        items.push_back(footprint(width + extra(random), width, levels(random)));
    }
    return items;
}

} // anonymous namespace

TEST_CASE("floorPattern: wideWideIsDefault") {
    const FloorLayout layout = arranged(copies(footprint(48, 40), 2));
    REQUIRE(layout.rows.size() == 1);
    CHECK(layout.rows[0].pattern == FloorPatternKind::WideWide);
    CHECK(layout.rows[0].widthIn == doctest::Approx(96.0));
    CHECK(layout.rows[0].depthIn == doctest::Approx(40.0));
    CHECK(layout.turnedItems == 0);
}

TEST_CASE("floorPattern: thirty 48 x 40 stand in 15 wide-wide rows") {
    const FloorLayout layout = arranged(copies(footprint(48, 40), 30));
    CHECK(layout.rows.size() == 15);
    CHECK(all_of(layout.rows.begin(), layout.rows.end(),
                 [](const FloorRow& row) { return row.pattern == FloorPatternKind::WideWide; }));
    CHECK(layout.usedLengthIn == doctest::Approx(600.0));
    CHECK(layout.rearWasteIn == doctest::Approx(30.0));
    CHECK(layout.fitsLength);
    checkLayoutInvariants(layout, 30);
}

TEST_CASE("floorPattern: shortSideUnder39p4GivesSixteenRows - 32 positions in exactly 630 in") {
    const FloorLayout layout = arranged(copies(footprint(48, 630.0 / 16.0), 32));
    CHECK(layout.rows.size() == 16);
    CHECK(layout.positions == 32);
    CHECK(layout.fitsLength);
    CHECK(layout.rearWasteIn == doctest::Approx(0.0));
}

TEST_CASE("floorPattern: rows summing to 630 in plus epsilon do not fit") {
    const FloorLayout layout = arranged(copies(footprint(48, 630.0 / 16.0 + 1e-6), 32));
    CHECK_FALSE(layout.fitsLength);
    CHECK(layout.rearWasteIn < 0.0);
}

TEST_CASE("floorPattern: widths summing exactly to the trailer width fit wide-wide") {
    CHECK(arranged(copies(footprint(50, 40), 2)).rows[0].pattern == FloorPatternKind::WideWide);
    const FloorLayout over = arranged(copies(footprint(50.0 + 1e-6, 40), 2));
    CHECK(over.rows[0].pattern != FloorPatternKind::WideWide);
}

TEST_CASE("floorPattern: longSideOver50FallsBack - never two long sides forced into 100 in") {
    const FloorLayout layout = arranged(copies(footprint(52, 40), 2));
    REQUIRE(layout.rows.size() == 1);
    CHECK(layout.rows[0].pattern != FloorPatternKind::WideWide);
    CHECK(layout.rows[0].depthIn == doctest::Approx(52.0));
    CHECK(layout.rows[0].widthIn <= 100.0);
}

TEST_CASE("floorPattern: pinwheelChosenWhenBest") {
    // Four 52 x 40 interlock in 92 in; any two-row pairing needs 104.
    const FloorLayout layout = arranged(copies(footprint(52, 40), 4));
    REQUIRE(layout.rows.size() == 1);
    CHECK(layout.rows[0].pattern == FloorPatternKind::Pinwheel);
    CHECK(layout.rows[0].depthIn == doctest::Approx(92.0));
    CHECK(layout.rows[0].widthIn == doctest::Approx(92.0));
}

TEST_CASE("floorPattern: turningCountedAndReported") {
    CHECK(arranged(copies(footprint(52, 40), 4)).turnedItems == 2);
    CHECK(arranged(copies(footprint(48, 40), 4)).turnedItems == 0);
}

TEST_CASE("floorPattern: allFourPatternsGenerated") {
    // 60 x 30 and 55 x 38 cannot go wide-wide (115 in); wide-narrow needs 55, narrow-narrow 60.
    const FloorLayout wideNarrow = arranged({footprint(60, 30), footprint(55, 38)});
    REQUIRE(wideNarrow.rows.size() == 1);
    CHECK(wideNarrow.rows[0].pattern == FloorPatternKind::WideNarrow);
    CHECK(wideNarrow.rows[0].depthIn == doctest::Approx(55.0));

    FloorPatternSettings shortAcross = defaultSettings();
    shortAcross.allowRotation = false;
    shortAcross.fixedOrientation = FixedOrientation::ShortSideAcross;
    const FloorLayout narrowNarrow = arranged(copies(footprint(48, 40), 2), shortAcross);
    REQUIRE(narrowNarrow.rows.size() == 1);
    CHECK(narrowNarrow.rows[0].pattern == FloorPatternKind::NarrowNarrow);
    CHECK(narrowNarrow.rows[0].depthIn == doctest::Approx(48.0));

    CHECK(arranged(copies(footprint(48, 40), 2)).rows[0].pattern == FloorPatternKind::WideWide);
    CHECK(arranged(copies(footprint(52, 40), 4)).rows[0].pattern == FloorPatternKind::Pinwheel);
}

TEST_CASE("floorPattern: three across when the footprint is 33.3 in or less") {
    const FloorLayout layout = arranged(copies(footprint(40, 33), 3));
    REQUIRE(layout.rows.size() == 1);
    CHECK(layout.rows[0].pattern == FloorPatternKind::ThreeAcross);
    CHECK(layout.rows[0].widthIn == doctest::Approx(99.0));
    CHECK(layout.rows[0].depthIn == doctest::Approx(40.0));
}

TEST_CASE("floorPattern: an odd count leaves one single row, reported") {
    const FloorLayout layout = arranged(copies(footprint(48, 40), 3));
    REQUIRE(layout.rows.size() == 2);
    const size_t singles = static_cast<size_t>(count_if(layout.rows.begin(), layout.rows.end(),
        [](const FloorRow& row) { return row.pattern == FloorPatternKind::Single; }));
    CHECK(singles == 1);
    CHECK(layout.usedLengthIn == doctest::Approx(80.0));
}

TEST_CASE("floorPattern: noLoneTopPallet - stacks of matching height share rows") {
    const vector<FloorItem> items{footprint(48, 40, 2), footprint(48, 40, 1), footprint(48, 40, 2),
                                  footprint(48, 40, 1)};
    for (const FloorLayout& layout : {arrangeFloorExact(items, kTrailer, defaultSettings()),
                                      arrangeFloorSorted(items, kTrailer, defaultSettings())}) {
        CHECK(layout.loneTopPalletRows == 0);
        CHECK(layout.usedLengthIn == doctest::Approx(80.0));
    }
}

TEST_CASE("floorPattern: an unavoidable lone top pallet is flagged") {
    // Three double stacks: one must stand alone with its top pallet unsupported.
    const FloorLayout layout = arranged(copies(footprint(48, 40, 2), 3));
    CHECK(layout.loneTopPalletRows == 1);
    const auto lone = find_if(layout.rows.begin(), layout.rows.end(),
                              [](const FloorRow& row) { return row.loneTopPallet; });
    REQUIRE(lone != layout.rows.end());
    CHECK(lone->pattern == FloorPatternKind::Single);
}

TEST_CASE("floorPattern: rotationOffRestrictsPatterns") {
    FloorPatternSettings longAcross = defaultSettings();
    longAcross.allowRotation = false;
    const FloorLayout layout = arranged(copies(footprint(52, 40), 4), longAcross);
    for (const FloorRow& row : layout.rows) {
        CHECK((row.pattern == FloorPatternKind::Single || row.pattern == FloorPatternKind::WideWide));
    }
    CHECK(layout.turnedItems == 0);
    CHECK(layout.usedLengthIn == doctest::Approx(160.0));
    CHECK(arranged(copies(footprint(52, 40), 4)).usedLengthIn == doctest::Approx(92.0));
}

TEST_CASE("floorPattern: rotation off with a footprint wider than the trailer leaves it unplaceable") {
    FloorPatternSettings longAcross = defaultSettings();
    longAcross.allowRotation = false;
    const FloorLayout layout = arranged({footprint(102, 40)}, longAcross);
    CHECK(layout.rows.empty());
    CHECK(layout.unplaceableItems == vector<size_t>{0});
    CHECK(arranged({footprint(102, 40)}).unplaceableItems.empty());
}

TEST_CASE("floorPattern: an item that fits no orientation is named, never placed") {
    const FloorLayout layout = arranged({footprint(48, 40), footprint(120, 101)});
    CHECK(layout.unplaceableItems == vector<size_t>{1});
    CHECK(layout.positions == 1);
    checkLayoutInvariants(layout, 2);
}

TEST_CASE("floorPattern: maxPalletsNeverExceeded is reported") {
    FloorPatternSettings capped = defaultSettings();
    capped.maxPositions = 2;
    CHECK_FALSE(arranged(copies(footprint(48, 40), 3), capped).withinMaxPositions);
    CHECK(arranged(copies(footprint(48, 40), 2), capped).withinMaxPositions);
}

TEST_CASE("floorPattern: eightyFourPercentCubeIsFull") {
    const double trailerCube = 630.0 * 100.0 * 108.0;
    FloorItem item = footprint(48, 40);
    item.cubeCubicIn = trailerCube * 0.84 / 30.0;
    CHECK(arranged(copies(item, 30)).cubeFull);
    item.cubeCubicIn = trailerCube * 0.83 / 30.0;
    const FloorLayout under = arranged(copies(item, 30));
    CHECK_FALSE(under.cubeFull);
    CHECK(under.cubeFraction == doctest::Approx(0.83));
}

TEST_CASE("floorPattern: empty is zero rows, not an error") {
    const FloorLayout layout = arranged({});
    CHECK(layout.rows.empty());
    CHECK(layout.usedLengthIn == 0.0);
    CHECK(layout.fitsLength);
    CHECK(arrangeFloorSorted({}, kTrailer, defaultSettings()).rows.empty());
}

TEST_CASE("floorPattern: bad trailer, settings or items are rejected") {
    const double nan = numeric_limits<double>::quiet_NaN();
    const double infinity = numeric_limits<double>::infinity();
    const vector<FloorItem> good{footprint(48, 40)};
    for (const double bad : {0.0, -630.0, nan, infinity}) {
        TrailerSpec trailer = kTrailer;
        trailer.interiorLengthIn = bad;
        CHECK_THROWS_AS(arrangeFloor(good, trailer, defaultSettings()), invalid_argument);
        trailer = kTrailer;
        trailer.interiorWidthIn = bad;
        CHECK_THROWS_AS(arrangeFloor(good, trailer, defaultSettings()), invalid_argument);
        CHECK_THROWS_AS(arranged({footprint(bad, 40)}), invalid_argument);
        CHECK_THROWS_AS(arranged({footprint(48, bad)}), invalid_argument);
        FloorItem badCube = footprint(48, 40);
        badCube.cubeCubicIn = bad == 0.0 ? -1.0 : bad;
        CHECK_THROWS_AS(arranged({badCube}), invalid_argument);
    }
    CHECK_THROWS_AS(arranged({footprint(40, 48)}), invalid_argument);
    CHECK_THROWS_AS(arranged({footprint(48, 40, 0)}), invalid_argument);
    for (const double share : {0.0, -0.84, 1.5, nan}) {
        FloorPatternSettings settings = defaultSettings();
        settings.fullCubeFraction = share;
        CHECK_THROWS_AS(arranged(good, settings), invalid_argument);
    }
    FloorPatternSettings capped = defaultSettings();
    capped.maxPositions = 0;
    CHECK_THROWS_AS(arranged(good, capped), invalid_argument);
    FloorPatternSettings noSingles = defaultSettings();
    noSingles.allowedPatterns = {FloorPatternKind::WideWide};
    CHECK_THROWS_AS(arranged(good, noSingles), invalid_argument);
    CHECK_THROWS_AS(arrangeFloorExact(copies(footprint(48, 40), kExactFloorItems + 1), kTrailer,
                                      defaultSettings()),
                    invalid_argument);
}

TEST_CASE("floorPattern: noRowWiderThanTrailer and totalLengthWithinTrailer (property)") {
    mt19937 random(20261025);
    uniform_int_distribution<size_t> count(0, 34);
    for (int trial = 0; trial < 300; ++trial) {
        const vector<FloorItem> items = randomItems(random, count(random));
        checkLayoutInvariants(arranged(items), items.size());
        checkLayoutInvariants(arrangeFloorSorted(items, kTrailer, defaultSettings()), items.size());
    }
}

TEST_CASE("floorPattern: sorted never beats exact on small inputs (differential)") {
    mt19937 random(16102026);
    uniform_int_distribution<size_t> count(1, 8);
    for (int trial = 0; trial < 300; ++trial) {
        const vector<FloorItem> items = randomItems(random, count(random));
        const FloorLayout exact = arrangeFloorExact(items, kTrailer, defaultSettings());
        const FloorLayout sorted = arrangeFloorSorted(items, kTrailer, defaultSettings());
        checkLayoutInvariants(exact, items.size());
        REQUIRE(exact.usedLengthIn <= sorted.usedLengthIn + kQuantityEpsilon);
    }
}

TEST_CASE("floorPattern: pairingBeatsSequential on mixed footprints") {
    // In input order the long footprints pair with the short ones and every row is 48 deep;
    // pairing like with like puts the 36 in rows together.
    vector<FloorItem> items;
    for (int index = 0; index < 6; ++index) {
        items.push_back(footprint(48, 48));
        items.push_back(footprint(48, 36));
    }
    double sequential = 0.0;
    for (size_t index = 0; index < items.size(); index += 2) {
        sequential += max(items[index].shortSideIn, items[index + 1].shortSideIn);
    }
    const FloorLayout layout = arranged(items);
    CHECK(layout.usedLengthIn < sequential);
    CHECK(layout.usedLengthIn == doctest::Approx(3 * 48.0 + 3 * 36.0));
}

TEST_CASE("floorPattern: shuffleSamePattern (metamorphic)") {
    mt19937 random(25102026);
    for (int trial = 0; trial < 100; ++trial) {
        const vector<FloorItem> items = randomItems(random, trial % 2 == 0 ? 8 : 30);
        vector<FloorItem> shuffled = items;
        shuffle(shuffled.begin(), shuffled.end(), random);
        const FloorLayout original = arranged(items);
        const FloorLayout reordered = arranged(shuffled);
        REQUIRE(original.usedLengthIn == doctest::Approx(reordered.usedLengthIn));
        REQUIRE(original.loneTopPalletRows == reordered.loneTopPalletRows);
    }
}

TEST_CASE("floorPattern: 48 distinct footprints on one truck arrange within budget") {
    vector<FloorItem> items;
    for (int index = 0; index < 48; ++index) items.push_back(footprint(40.0 + index * 0.25, 33.0 + index * 0.2));
    const FloorLayout layout = arranged(items);
    checkLayoutInvariants(layout, items.size());
}

TEST_CASE("floorPattern: twoAcrossAlwaysFound for every August footprint"
          * doctest::skip(!m4AugustTests::augustPresent())) {
    for (size_t run = 0; run < m4AugustTests::kAugustRunFiles.size(); ++run) {
        CAPTURE(run);
        const AugustItems& august = m4AugustTests::augustItems(run);
        for (const SelectionItem& item : august.built.items) {
            REQUIRE(twoAcrossFits(item.footprintLongIn, item.footprintShortIn,
                                  august.run.trailer.interiorWidthIn));
        }
    }
}

TEST_CASE("floorPattern: real August loads keep every invariant"
          * doctest::skip(!m4AugustTests::augustPresent())) {
    const AugustItems& august = m4AugustTests::augustItems(0);
    FloorPatternSettings settings = defaultSettings();
    // The first 32 items of each group: a full truck's worth of positions.
    vector<vector<FloorItem>> trucks(august.run.segregation.groups.size());
    for (const SelectionItem& item : august.built.items) {
        if (trucks[item.groupIndex].size() < 32) trucks[item.groupIndex].push_back(floorItemFrom(item));
    }
    for (const vector<FloorItem>& truck : trucks) {
        const FloorLayout layout = arrangeFloor(truck, august.run.trailer, settings);
        CHECK(layout.unplaceableItems.empty());
        checkLayoutInvariants(layout, truck.size());
    }
}
