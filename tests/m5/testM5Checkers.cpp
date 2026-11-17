// Self-checks of the independent oracles the M5 tests rely on: the floor-witness checker (INV-03) and
// the day arithmetic of the date invariant (INV-08). Expected answers are worked out by hand here, not
// by the production code.
#include "doctest.h"
#include "support/m5LayoutChecker.hpp"
#include "support/m5Oracles.hpp"

#include <string>
#include <vector>

using namespace std;
using m5LayoutChecker::fitInBox;
using m5LayoutChecker::Shape;
using m5LayoutChecker::witnessProblems;

namespace {

ob::FloorItem stack(double longSideIn, double shortSideIn, double heightIn = 50.0) {
    ob::FloorItem item;
    item.longSideIn = longSideIn;
    item.shortSideIn = shortSideIn;
    item.heightIn = heightIn;
    return item;
}

ob::FloorRow row(const vector<size_t>& members, double depthIn) {
    ob::FloorRow floorRow;
    for (const size_t member : members) floorRow.placements.push_back({member, false});
    floorRow.depthIn = depthIn;
    return floorRow;
}

ob::FloorPatternSettings turning(bool allowed) {
    ob::FloorPatternSettings floor;
    floor.allowRotation = allowed;
    floor.fixedOrientation = ob::FixedOrientation::LongSideAcross;
    return floor;
}

}  // namespace

TEST_CASE("m5LayoutChecker: rows side by side, three across and a pinwheel fit their boxes" * doctest::test_suite("m5")) {
    CHECK(fitInBox({{48, 40}, {48, 40}}, 100, 40));
    CHECK(fitInBox({{33.3, 20}, {33.3, 20}, {33.3, 20}}, 100, 20));
    CHECK_FALSE(fitInBox({{33.4, 20}, {33.4, 20}, {33.4, 20}}, 100, 20));  // 100.2 in across
    // Four 60 x 40 stacks fill a 100 x 100 square only as a pinwheel round a 20 x 20 hole.
    const vector<Shape> pinwheel{{60, 40}, {40, 60}, {60, 40}, {40, 60}};
    CHECK(fitInBox(pinwheel, 100, 100));
    CHECK_FALSE(fitInBox(pinwheel, 100, 79));  // 9,600 sq in of stacks in 7,900
    CHECK_FALSE(fitInBox({{60, 40}, {60, 40}}, 100, 40));
}

TEST_CASE("m5LayoutChecker: a valid witness passes, and every defect is named" * doctest::test_suite("m5")) {
    const vector<ob::FloorItem> items{stack(48, 40), stack(48, 40), stack(48, 40)};
    ob::FloorLayout witness;
    witness.rows = {row({0, 1}, 40), row({2}, 40)};
    CHECK(witnessProblems(witness, items, 630, 100, 108, turning(true)).empty());

    ob::FloorLayout missing = witness;
    missing.rows.pop_back();
    CHECK(witnessProblems(missing, items, 630, 100, 108, turning(true)) == vector<string>{"item 2 is placed 0 times"});

    ob::FloorLayout twice = witness;
    twice.rows.push_back(row({2}, 40));
    CHECK(witnessProblems(twice, items, 630, 100, 108, turning(true)) == vector<string>{"item 2 is placed 2 times"});

    ob::FloorLayout tooShallow = witness;
    tooShallow.rows.front().depthIn = 39.5;
    CHECK(witnessProblems(tooShallow, items, 630, 100, 108, turning(true)).size() == 1);

    CHECK(witnessProblems(witness, items, 79, 100, 108, turning(true)).size() == 1);   // 80 in of rows on 79 in
    CHECK(witnessProblems(witness, items, 630, 100, 49, turning(true)).size() == 3);   // every stack above 49 in

    ob::FloorLayout stranger = witness;
    stranger.rows.back().placements.push_back({7, false});
    CHECK(witnessProblems(stranger, items, 630, 100, 108, turning(true)).size() == 1);
}

TEST_CASE("m5LayoutChecker: turning is used only where the settings allow it" * doctest::test_suite("m5")) {
    const vector<ob::FloorItem> items{stack(48, 40), stack(48, 40)};
    ob::FloorLayout witness;
    witness.rows = {row({0, 1}, 48)};
    // 90 in across takes two stacks short side across (80 in), not long side across (96 in).
    CHECK(witnessProblems(witness, items, 630, 90, 108, turning(true)).empty());
    CHECK(witnessProblems(witness, items, 630, 90, 108, turning(false)).size() == 1);
}

TEST_CASE("m5Oracles: day counts cross month, year and leap-day ends" * doctest::test_suite("m5")) {
    using m5Oracles::daysSinceEpoch;
    CHECK(daysSinceEpoch("1970-01-01") == 0);
    CHECK(daysSinceEpoch("2026-08-31") - daysSinceEpoch("2026-08-29") == 2);
    CHECK(daysSinceEpoch("2026-09-02") - daysSinceEpoch("2026-08-31") == 2);
    CHECK(daysSinceEpoch("2027-01-01") - daysSinceEpoch("2026-12-31") == 1);
    CHECK(daysSinceEpoch("2028-03-01") - daysSinceEpoch("2028-02-28") == 2);
    CHECK(daysSinceEpoch("2026-08-17") == 20682);
}
