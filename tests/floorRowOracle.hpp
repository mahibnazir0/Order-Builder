#pragma once

#include "floorPattern.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <limits>
#include <vector>

// The least trailer length of every subset of `items` (indexed by bitmask) over every grouping
// into rows of one to four, each row the shallowest candidateRows gives within the trailer width.
// It tries every grouping by dynamic programming over subsets and shares no code with
// arrangeFloor's own searches, so it can judge them. Infinite where no grouping exists. For up to
// about sixteen items: it holds 2^n lengths.
inline std::vector<double> shortestGroupingOfEverySubset(const std::vector<ob::FloorItem>& items,
                                                         const ob::TrailerSpec& trailer,
                                                         const ob::FloorPatternSettings& settings) {
    const double infinite = std::numeric_limits<double>::infinity();
    const std::size_t count = items.size();
    const std::size_t subsets = std::size_t{1} << count;
    // The rows that hold a given item as their lowest, each with its shallowest depth.
    std::vector<std::vector<std::pair<std::size_t, double>>> rowsFrom(count);
    std::vector<std::size_t> members;
    for (std::size_t mask = 1; mask < subsets; ++mask) {
        if (std::bitset<32>(mask).count() > 4) continue;
        members.clear();
        for (std::size_t index = 0; index < count; ++index) {
            if (mask & (std::size_t{1} << index)) members.push_back(index);
        }
        double depth = infinite;
        for (const ob::FloorRow& row : ob::candidateRows(items, members, trailer, settings)) {
            if (row.widthIn <= trailer.interiorWidthIn + ob::kQuantityEpsilon) depth = std::min(depth, row.depthIn);
        }
        if (depth < infinite) rowsFrom[members.front()].emplace_back(mask, depth);
    }
    std::vector<double> shortest(subsets, infinite);
    shortest[0] = 0.0;
    for (std::size_t subset = 1; subset < subsets; ++subset) {
        std::size_t lowest = 0;
        while (!(subset & (std::size_t{1} << lowest))) ++lowest;
        for (const auto& [row, depth] : rowsFrom[lowest]) {
            if ((subset & row) == row) shortest[subset] = std::min(shortest[subset], depth + shortest[subset & ~row]);
        }
    }
    return shortest;
}
