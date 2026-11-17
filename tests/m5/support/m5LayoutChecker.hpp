#pragma once
// Independent check of a truck's floor witness (INV-03). It rebuilds the geometry from the stacks' own
// footprints and never calls the floor-pattern code that produced the witness. A witness passes when
// every stack of the truck is placed exactly once, every stack stands under the ceiling, the rows'
// depths add up to no more than the trailer length, and each row's stacks fit without overlap in a box
// as wide as the trailer and as deep as the row.
//
// Fitting a row is an exact search. Any arrangement of rectangles in a box can be pushed towards the
// front and one wall until each touches a wall or another rectangle; every coordinate is then a sum of
// other rectangles' sides. Trying those sums for every orientation finds an arrangement whenever one
// exists. A row holds at most four stacks, so the search stays small.

#include "floorPattern.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace m5LayoutChecker {

constexpr double kTolerance = 1e-9;

struct Shape {
    double acrossIn = 0.0;
    double alongIn = 0.0;
};

struct Placed {
    double acrossAt = 0.0;
    double alongAt = 0.0;
    Shape shape;
};

inline bool overlap(const Placed& left, const Placed& right) {
    return left.acrossAt + left.shape.acrossIn > right.acrossAt + kTolerance
           && right.acrossAt + right.shape.acrossIn > left.acrossAt + kTolerance
           && left.alongAt + left.shape.alongIn > right.alongAt + kTolerance
           && right.alongAt + right.shape.alongIn > left.alongAt + kTolerance;
}

// Every sum of a subset of `sides`, 0 included, without near-duplicates.
inline std::vector<double> subsetSums(const std::vector<double>& sides) {
    std::vector<double> sums{0.0};
    for (const double side : sides) {
        const std::size_t count = sums.size();
        for (std::size_t index = 0; index < count; ++index) sums.push_back(sums[index] + side);
    }
    std::sort(sums.begin(), sums.end());
    sums.erase(std::unique(sums.begin(), sums.end(), [](double left, double right) { return right - left <= kTolerance; }),
               sums.end());
    return sums;
}

inline bool placeFrom(std::size_t next, const std::vector<Shape>& shapes, const std::vector<std::vector<double>>& acrossAt,
                      const std::vector<std::vector<double>>& alongAt, double widthIn, double depthIn,
                      std::vector<Placed>& placed) {
    if (next == shapes.size()) return true;
    for (const double across : acrossAt[next]) {
        if (across + shapes[next].acrossIn > widthIn + kTolerance) continue;
        for (const double along : alongAt[next]) {
            if (along + shapes[next].alongIn > depthIn + kTolerance) continue;
            const Placed candidate{across, along, shapes[next]};
            if (std::any_of(placed.begin(), placed.end(), [&](const Placed& other) { return overlap(candidate, other); })) continue;
            placed.push_back(candidate);
            if (placeFrom(next + 1, shapes, acrossAt, alongAt, widthIn, depthIn, placed)) return true;
            placed.pop_back();
        }
    }
    return false;
}

// Whether the shapes, as oriented, fit without overlap in a box widthIn across and depthIn along.
inline bool fitInBox(const std::vector<Shape>& shapes, double widthIn, double depthIn) {
    std::vector<std::vector<double>> acrossAt(shapes.size()), alongAt(shapes.size());
    for (std::size_t index = 0; index < shapes.size(); ++index) {
        std::vector<double> otherAcross, otherAlong;
        for (std::size_t other = 0; other < shapes.size(); ++other) {
            if (other == index) continue;
            otherAcross.push_back(shapes[other].acrossIn);
            otherAlong.push_back(shapes[other].alongIn);
        }
        acrossAt[index] = subsetSums(otherAcross);
        alongAt[index] = subsetSums(otherAlong);
    }
    std::vector<Placed> placed;
    return placeFrom(0, shapes, acrossAt, alongAt, widthIn, depthIn, placed);
}

// Whether the stacks fit a row box in some orientation the settings allow: either side across when
// turning is allowed, otherwise the fixed side across.
inline bool rowFits(const std::vector<ob::FloorItem>& members, const ob::FloorPatternSettings& floor, double widthIn,
                    double depthIn) {
    const std::size_t count = members.size();
    const std::size_t orientations = floor.allowRotation ? (std::size_t{1} << count) : 1;
    for (std::size_t choice = 0; choice < orientations; ++choice) {
        std::vector<Shape> shapes;
        for (std::size_t index = 0; index < count; ++index) {
            const bool longAcross = floor.allowRotation ? ((choice >> index) & 1U) == 0
                                                        : floor.fixedOrientation == ob::FixedOrientation::LongSideAcross;
            const ob::FloorItem& item = members[index];
            shapes.push_back(longAcross ? Shape{item.longSideIn, item.shortSideIn} : Shape{item.shortSideIn, item.longSideIn});
        }
        if (fitInBox(shapes, widthIn, depthIn)) return true;
    }
    return false;
}

// What is wrong with `witness` as the floor of `items` (indexed as the witness indexes them) in a
// trailer lengthIn x widthIn with the given ceiling; empty when it is a valid floor.
inline std::vector<std::string> witnessProblems(const ob::FloorLayout& witness, const std::vector<ob::FloorItem>& items,
                                                double lengthIn, double widthIn, double ceilingIn,
                                                const ob::FloorPatternSettings& floor) {
    std::vector<std::string> problems;
    std::vector<int> placedCount(items.size(), 0);
    double usedLengthIn = 0.0;
    std::size_t positions = 0;
    for (std::size_t row = 0; row < witness.rows.size(); ++row) {
        const ob::FloorRow& floorRow = witness.rows[row];
        std::vector<ob::FloorItem> members;
        for (const ob::FloorPlacement& placement : floorRow.placements) {
            if (placement.itemIndex >= items.size()) {
                problems.push_back("row " + std::to_string(row) + " places item " + std::to_string(placement.itemIndex)
                                   + ", which the truck does not carry");
                continue;
            }
            ++placedCount[placement.itemIndex];
            members.push_back(items[placement.itemIndex]);
        }
        positions += members.size();
        if (!(std::isfinite(floorRow.depthIn) && floorRow.depthIn >= 0.0)) {
            problems.push_back("row " + std::to_string(row) + " has no valid depth");
            continue;
        }
        usedLengthIn += floorRow.depthIn;
        if (members.size() > 4) {
            problems.push_back("row " + std::to_string(row) + " holds " + std::to_string(members.size()) + " stacks");
        } else if (!members.empty() && !rowFits(members, floor, widthIn, floorRow.depthIn)) {
            problems.push_back("row " + std::to_string(row) + "'s stacks do not fit " + std::to_string(widthIn) + " in across and "
                               + std::to_string(floorRow.depthIn) + " in along");
        }
    }
    for (std::size_t item = 0; item < items.size(); ++item) {
        if (placedCount[item] != 1) {
            problems.push_back("item " + std::to_string(item) + " is placed " + std::to_string(placedCount[item]) + " times");
        }
        if (items[item].heightIn > ceilingIn + kTolerance) {
            problems.push_back("item " + std::to_string(item) + " stands above the " + std::to_string(ceilingIn) + " in ceiling");
        }
    }
    if (usedLengthIn > lengthIn + kTolerance) {
        problems.push_back("rows take " + std::to_string(usedLengthIn) + " in of the " + std::to_string(lengthIn) + " in floor");
    }
    if (floor.maxPositions && positions > static_cast<std::size_t>(*floor.maxPositions)) {
        problems.push_back(std::to_string(positions) + " positions over the cap of " + std::to_string(*floor.maxPositions));
    }
    return problems;
}

} // namespace m5LayoutChecker
