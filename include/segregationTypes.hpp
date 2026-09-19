#pragma once

#include "demand_types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ob {

enum class SplitReason { None, DoNotMix, OwnStack, SameSite };

using DoNotMixPair = DNMRecord;

struct GroupKey {
    std::string locationFrom;
    std::string locationTo;
    std::string shipCondition;
    // Kept apart from segregant so that no PLANNER_SNP value can collide with the
    // flagged/unflagged distinction and merge segregated product into a normal group.
    bool isSegregated = false;
    // The line's PLANNER_SNP when segregated under Strict; empty otherwise.
    std::string segregant;

    bool operator<(const GroupKey& other) const;
};

struct SegregatedGroup {
    GroupKey key;
    // Indices preserve copy safety without borrowing pointers into demand storage.
    std::vector<std::size_t> lineIndices;
    SplitReason splitReason = SplitReason::None;
    bool sameSiteFlag = false;
};

struct SegregationResult {
    std::vector<SegregatedGroup> groups;
    std::size_t linesIn = 0;
    std::size_t linesSegregated = 0;
    std::size_t lanesIn = 0;
    std::size_t lanesSplit = 0;
    std::size_t doNotMixPairsLoaded = 0;
    std::size_t doNotMixPairsWithDemand = 0;
};

} // namespace ob
