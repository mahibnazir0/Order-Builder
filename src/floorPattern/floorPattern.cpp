#include "floorPattern.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <deque>
#include <map>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

using namespace std;

namespace ob {

namespace {

// What a layout is judged on, in order: length used, rows with a lone top pallet, rows that
// are not the wide-wide default.
struct LayoutScore {
    double lengthIn = 0.0;
    size_t loneTopRows = 0;
    size_t nonDefaultRows = 0;

    // A FloorRow, or a row not yet built (RowShape).
    template <typename Row>
    LayoutScore plus(const Row& row) const {
        return {lengthIn + row.depthIn, loneTopRows + (row.loneTopPallet ? 1 : 0),
                nonDefaultRows + (row.pattern == FloorPatternKind::WideWide ? 0 : 1)};
    }
};

// Lengths within kQuantityEpsilon are equal, so the tie-breaks decide (E112).
bool betterThan(const LayoutScore& left, const LayoutScore& right) {
    if (fabs(left.lengthIn - right.lengthIn) > kQuantityEpsilon) return left.lengthIn < right.lengthIn;
    if (left.loneTopRows != right.loneTopRows) return left.loneTopRows < right.loneTopRows;
    return left.nonDefaultRows < right.nonDefaultRows;
}

template <typename Row>
bool betterRow(const Row& left, const Row& right) {
    return betterThan(LayoutScore{}.plus(left), LayoutScore{}.plus(right));
}

bool fitsWithin(double size, double limit) { return size <= limit + kQuantityEpsilon; }

// A candidate row before it is built, its placements held in place: an arrangement weighs
// dozens of candidates for every row it keeps, so they are compared without allocating.
struct RowShape {
    FloorPatternKind pattern = FloorPatternKind::Single;
    array<FloorPlacement, 4> placements{};
    size_t count = 0;
    double widthIn = 0.0;
    double depthIn = 0.0;
    bool loneTopPallet = false;
};

FloorRow rowOf(const RowShape& shape) {
    FloorRow row;
    row.pattern = shape.pattern;
    row.placements.assign(shape.placements.begin(), shape.placements.begin() + static_cast<ptrdiff_t>(shape.count));
    row.depthIn = shape.depthIn;
    row.widthIn = shape.widthIn;
    row.loneTopPallet = shape.loneTopPallet;
    return row;
}

class RowBuilder {
public:
    RowBuilder(const vector<FloorItem>& items, double trailerWidthIn,
               const FloorPatternSettings& settings)
        : items_(items), widthIn_(trailerWidthIn), settings_(settings) {}

    // The best row these members can form, or nullopt when no allowed pattern holds them.
    optional<FloorRow> best(const vector<size_t>& members) const {
        if (members.size() == 4) return bestPinwheel(members);
        optional<RowShape> chosen;
        forEachCandidate(members, [&](const RowShape& shape) {
            if (!fitsWithin(shape.widthIn, widthIn_)) return;
            if (!chosen || betterRow(shape, *chosen)) chosen = shape;
        });
        if (!chosen) return nullopt;
        return rowOf(*chosen);
    }

    vector<FloorRow> candidates(const vector<size_t>& members) const {
        vector<FloorRow> rows;
        forEachCandidate(members, [&](const RowShape& shape) { rows.push_back(rowOf(shape)); });
        return rows;
    }

    bool allowsPinwheel() const {
        return allows(FloorPatternKind::Pinwheel, true) && allows(FloorPatternKind::Pinwheel, false);
    }

private:
    // Hands `consider` every row the allowed patterns form from the members, too wide or not.
    template <typename Consider>
    void forEachCandidate(const vector<size_t>& members, Consider&& consider) const {
        auto offer = [&](RowShape shape) {
            shape.loneTopPallet = hasLoneTop(shape);
            consider(shape);
        };
        switch (members.size()) {
        case 1: {
            const size_t only = members[0];
            if (allows(FloorPatternKind::Single, false) && fitsWithin(longOf(only), widthIn_)) {
                offer(makeShape(FloorPatternKind::Single, {{only, false}}));
            } else if (allows(FloorPatternKind::Single, true)) {
                offer(makeShape(FloorPatternKind::Single, {{only, true}}));
            }
            break;
        }
        case 2: {
            const size_t first = members[0], second = members[1];
            if (allows(FloorPatternKind::WideWide, false)) {
                offer(makeShape(FloorPatternKind::WideWide, {{first, false}, {second, false}}));
            }
            if (allows(FloorPatternKind::WideNarrow, true) && allows(FloorPatternKind::WideNarrow, false)) {
                offer(makeShape(FloorPatternKind::WideNarrow, {{first, false}, {second, true}}));
                offer(makeShape(FloorPatternKind::WideNarrow, {{first, true}, {second, false}}));
            }
            if (allows(FloorPatternKind::NarrowNarrow, true)) {
                offer(makeShape(FloorPatternKind::NarrowNarrow, {{first, true}, {second, true}}));
            }
            break;
        }
        case 3:
            if (allows(FloorPatternKind::ThreeAcross, true)) {
                offer(makeShape(FloorPatternKind::ThreeAcross,
                                {{members[0], true}, {members[1], true}, {members[2], true}}));
            }
            break;
        case 4:
            if (allows(FloorPatternKind::Pinwheel, true) && allows(FloorPatternKind::Pinwheel, false)) {
                array<size_t, 4> order{members[0], members[1], members[2], members[3]};
                sort(order.begin(), order.end());
                do {
                    if (const optional<RowShape> shape = pinwheel(order)) offer(*shape);
                } while (next_permutation(order.begin(), order.end()));
            }
            break;
        default:
            break;
        }
    }

    double longOf(size_t item) const { return items_[item].longSideIn; }
    double shortOf(size_t item) const { return items_[item].shortSideIn; }
    double acrossOf(const FloorPlacement& placement) const {
        return placement.turned ? shortOf(placement.itemIndex) : longOf(placement.itemIndex);
    }
    double deepOf(const FloorPlacement& placement) const {
        return placement.turned ? longOf(placement.itemIndex) : shortOf(placement.itemIndex);
    }

    // Whether a pattern may be used with an item placed this way round. Without rotation every
    // item goes in the one fixed way, so no pattern can mix the two.
    bool allows(FloorPatternKind pattern, bool turned) const {
        const auto& allowed = settings_.allowedPatterns;
        if (find(allowed.begin(), allowed.end(), pattern) == allowed.end()) return false;
        if (settings_.allowRotation) return true;
        return turned == (settings_.fixedOrientation == FixedOrientation::ShortSideAcross);
    }

    RowShape makeShape(FloorPatternKind pattern, initializer_list<FloorPlacement> placements) const {
        RowShape shape;
        shape.pattern = pattern;
        for (const FloorPlacement& placement : placements) {
            shape.widthIn += acrossOf(placement);
            shape.depthIn = max(shape.depthIn, deepOf(placement));
            shape.placements[shape.count++] = placement;
        }
        return shape;
    }

    bool hasLoneTop(const RowShape& shape) const {
        if (shape.count == 1) return items_[shape.placements[0].itemIndex].levels > 1;
        const int firstLevels = items_[shape.placements[0].itemIndex].levels;
        return any_of(shape.placements.begin(), shape.placements.begin() + static_cast<ptrdiff_t>(shape.count),
                      [&](const FloorPlacement& placement) { return items_[placement.itemIndex].levels != firstLevels; });
    }

    struct Rectangle {
        double left, right, front, back;
    };

    static bool overlap(const Rectangle& a, const Rectangle& b) {
        return a.left < b.right - kQuantityEpsilon && b.left < a.right - kQuantityEpsilon
               && a.front < b.back - kQuantityEpsilon && b.front < a.back - kQuantityEpsilon;
    }

    // The trailer length a pinwheel in this order takes: the deepest back edge of pinwheel()'s
    // rectangles, figured the same way, so the two agree exactly.
    double pinwheelDepth(const array<size_t, 4>& order) const {
        const size_t frontLeft = order[0], frontRight = order[1], backLeft = order[2], backRight = order[3];
        return max({shortOf(frontLeft), longOf(frontRight), shortOf(frontLeft) + longOf(backLeft),
                    longOf(frontRight) + shortOf(backRight)});
    }

    // Every pinwheel of the same four items is the same pattern with the same lone-top flag, so
    // best()'s comparison comes down to depth: the shallowest order that fits, the first of equal
    // depth. An order no shallower than the one kept is passed over before its overlaps are tested.
    optional<FloorRow> bestPinwheel(const vector<size_t>& members) const {
        if (!allows(FloorPatternKind::Pinwheel, true) || !allows(FloorPatternKind::Pinwheel, false)) return nullopt;
        array<size_t, 4> order{members[0], members[1], members[2], members[3]};
        sort(order.begin(), order.end());
        optional<RowShape> chosen;
        do {
            if (chosen && !(pinwheelDepth(order) < chosen->depthIn - kQuantityEpsilon)) continue;
            const optional<RowShape> shape = pinwheel(order);
            if (shape && fitsWithin(shape->widthIn, widthIn_)) chosen = shape;
        } while (next_permutation(order.begin(), order.end()));
        if (!chosen) return nullopt;
        chosen->loneTopPallet = hasLoneTop(*chosen);
        return rowOf(*chosen);
    }

    // Four items round a centre: front-left and back-right long side across, front-right and
    // back-left turned. Each item is pushed back against the one in front of it, so the two
    // rows interlock where the turned items reach past the wide ones.
    optional<RowShape> pinwheel(const array<size_t, 4>& order) const {
        const size_t frontLeft = order[0], frontRight = order[1], backLeft = order[2],
                     backRight = order[3];
        const array<Rectangle, 4> rectangles{{
            {0.0, longOf(frontLeft), 0.0, shortOf(frontLeft)},
            {longOf(frontLeft), longOf(frontLeft) + shortOf(frontRight), 0.0, longOf(frontRight)},
            {0.0, shortOf(backLeft), shortOf(frontLeft), shortOf(frontLeft) + longOf(backLeft)},
            {shortOf(backLeft), shortOf(backLeft) + longOf(backRight), longOf(frontRight),
             longOf(frontRight) + shortOf(backRight)},
        }};
        for (size_t first = 0; first < rectangles.size(); ++first) {
            for (size_t second = first + 1; second < rectangles.size(); ++second) {
                if (overlap(rectangles[first], rectangles[second])) return nullopt;
            }
        }
        RowShape shape;
        shape.pattern = FloorPatternKind::Pinwheel;
        shape.placements = {{{frontLeft, false}, {frontRight, true}, {backLeft, true}, {backRight, false}}};
        shape.count = 4;
        for (const Rectangle& rectangle : rectangles) {
            shape.widthIn = max(shape.widthIn, rectangle.right);
            shape.depthIn = max(shape.depthIn, rectangle.back);
        }
        return shape;
    }

    const vector<FloorItem>& items_;
    double widthIn_;
    const FloorPatternSettings& settings_;
};

bool isNonNegativeFinite(double value) { return isfinite(value) && value >= 0.0; }
bool isPositiveFinite(double value) { return isfinite(value) && value > 0.0; }

void validate(const vector<FloorItem>& items, const TrailerSpec& trailer,
              const FloorPatternSettings& settings) {
    if (!isPositiveFinite(trailer.interiorLengthIn) || !isPositiveFinite(trailer.interiorWidthIn)
        || !isPositiveFinite(trailer.stackHeightCeilingIn)) {
        throw invalid_argument("Trailer " + trailer.trailerCode
                               + ": length, width and ceiling must be positive and finite");
    }
    // Every grouping search ends with any leftover item in a row of its own.
    const auto& allowed = settings.allowedPatterns;
    if (find(allowed.begin(), allowed.end(), FloorPatternKind::Single) == allowed.end()) {
        throw invalid_argument("Floor pattern settings must allow single rows: an odd item has to stand somewhere");
    }
    if (settings.maxPositions && *settings.maxPositions <= 0) {
        throw invalid_argument("Floor pattern settings: maximum positions must be at least 1");
    }
    if (!(settings.fullCubeFraction > 0.0 && settings.fullCubeFraction <= 1.0)) {
        throw invalid_argument("Floor pattern settings: full-cube share must be in (0, 1]");
    }
    if (settings.searchStateLimit < 1 || settings.routeSearchStateLimit < 1) {
        throw invalid_argument("Floor pattern settings: search state limits must be at least 1");
    }
    for (size_t index = 0; index < items.size(); ++index) {
        const FloorItem& item = items[index];
        if (!isPositiveFinite(item.longSideIn) || !isPositiveFinite(item.shortSideIn)
            || !isNonNegativeFinite(item.heightIn) || !isNonNegativeFinite(item.cubeCubicIn)
            || item.longSideIn < item.shortSideIn || item.levels < 1) {
            throw invalid_argument("Floor item " + to_string(index)
                                   + " has a non-positive, non-finite or inconsistent figure");
        }
    }
}

// An item fits the trailer when it can stand in a row of its own in some allowed orientation.
bool placeable(size_t index, const RowBuilder& rows, const TrailerSpec& trailer) {
    const optional<FloorRow> alone = rows.best({index});
    return alone && fitsWithin(alone->depthIn, trailer.interiorLengthIn);
}

// Splits the items into those that fit the trailer, and the rest.
vector<size_t> placeableItems(const vector<FloorItem>& items, const RowBuilder& rows,
                              const TrailerSpec& trailer, FloorLayout& layout) {
    vector<size_t> placeableIndices;
    placeableIndices.reserve(items.size());
    for (size_t index = 0; index < items.size(); ++index) {
        if (placeable(index, rows, trailer)) {
            placeableIndices.push_back(index);
        } else {
            layout.unplaceableItems.push_back(index);
        }
    }
    return placeableIndices;
}

void finishLayout(FloorLayout& layout, vector<FloorRow> rows, const vector<FloorItem>& items,
                  const TrailerSpec& trailer, const FloorPatternSettings& settings) {
    double placedCube = 0.0;
    for (const FloorRow& row : rows) {
        layout.usedLengthIn += row.depthIn;
        if (row.loneTopPallet) ++layout.loneTopPalletRows;
        for (const FloorPlacement& placement : row.placements) {
            ++layout.positions;
            if (placement.turned) ++layout.turnedItems;
            placedCube += items[placement.itemIndex].cubeCubicIn;
        }
    }
    layout.rows = std::move(rows);
    layout.rearWasteIn = trailer.interiorLengthIn - layout.usedLengthIn;
    layout.fitsLength = fitsWithin(layout.usedLengthIn, trailer.interiorLengthIn);
    layout.withinMaxPositions = !settings.maxPositions || layout.positions <= *settings.maxPositions;
    layout.cubeFraction = placedCube / (trailer.interiorLengthIn * trailer.interiorWidthIn
                                        * trailer.stackHeightCeilingIn);
    layout.cubeFull = layout.cubeFraction >= settings.fullCubeFraction - kQuantityEpsilon;
}

struct SearchState {
    bool reached = false;
    LayoutScore score;
    size_t previous = 0;
    FloorRow row;
};

// Rows of up to four neighbours in `order`, by dynamic programming over its prefixes.
vector<FloorRow> groupNeighbours(const vector<size_t>& order, const RowBuilder& rows,
                                 LayoutScore& score) {
    vector<SearchState> states(order.size() + 1);
    states[0].reached = true;
    vector<size_t> members;
    members.reserve(4);
    for (size_t end = 1; end <= order.size(); ++end) {
        for (size_t size = 1; size <= 4 && size <= end; ++size) {
            const SearchState& from = states[end - size];
            if (!from.reached) continue;
            members.assign(order.begin() + static_cast<ptrdiff_t>(end - size),
                           order.begin() + static_cast<ptrdiff_t>(end));
            optional<FloorRow> row = rows.best(members);
            if (!row) continue;
            const LayoutScore candidate = from.score.plus(*row);
            if (!states[end].reached || betterThan(candidate, states[end].score)) {
                states[end] = {true, candidate, end - size, std::move(*row)};
            }
        }
    }
    vector<FloorRow> result;
    for (size_t end = order.size(); end > 0; end = states[end].previous) {
        result.push_back(states[end].row);
    }
    reverse(result.begin(), result.end());
    score = states[order.size()].score;
    return result;
}

vector<FloorRow> sortedRows(const vector<FloorItem>& items, const vector<size_t>& placeableIndices,
                            const RowBuilder& rows) {
    // By levels first, so stacks of matching height stand together; by footprint first, so
    // similar footprints share rows. Index last, so the order never depends on input order
    // among items that differ.
    auto byLevels = [&](size_t left, size_t right) {
        const FloorItem& a = items[left];
        const FloorItem& b = items[right];
        return make_tuple(-a.levels, -a.longSideIn, -a.shortSideIn, -a.heightIn, left)
               < make_tuple(-b.levels, -b.longSideIn, -b.shortSideIn, -b.heightIn, right);
    };
    auto byFootprint = [&](size_t left, size_t right) {
        const FloorItem& a = items[left];
        const FloorItem& b = items[right];
        return make_tuple(-a.longSideIn, -a.shortSideIn, -a.levels, -a.heightIn, left)
               < make_tuple(-b.longSideIn, -b.shortSideIn, -b.levels, -b.heightIn, right);
    };
    vector<size_t> levelOrder = placeableIndices;
    sort(levelOrder.begin(), levelOrder.end(), byLevels);
    vector<size_t> footprintOrder = placeableIndices;
    sort(footprintOrder.begin(), footprintOrder.end(), byFootprint);

    LayoutScore levelScore, footprintScore;
    vector<FloorRow> levelRows = groupNeighbours(levelOrder, rows, levelScore);
    vector<FloorRow> footprintRows = groupNeighbours(footprintOrder, rows, footprintScore);
    if (betterThan(footprintScore, levelScore)) return footprintRows;
    return levelRows;
}

// At most kExactFloorItems placeable items.
vector<FloorRow> exactRows(const vector<size_t>& placeableIndices, const RowBuilder& rows) {
    const size_t count = placeableIndices.size();
    // Every set of placed items, built up by adding one row that holds the lowest unplaced
    // item, so each grouping into rows is reached exactly once.
    const size_t full = (size_t{1} << count) - 1;
    vector<SearchState> states(full + 1);
    states[0].reached = true;
    vector<size_t> members;
    members.reserve(4);
    for (size_t placed = 0; placed < full; ++placed) {
        if (!states[placed].reached) continue;
        size_t lowest = 0;
        while (placed & (size_t{1} << lowest)) ++lowest;
        const size_t others = full & ~placed & ~(size_t{1} << lowest);
        // Every subset of `others`, by the standard decreasing-submask walk, plus the empty one.
        // A row holds at most four, so larger subsets are passed over before any work.
        for (size_t extra = others;; extra = (extra - 1) & others) {
            const size_t rowMask = extra | (size_t{1} << lowest);
            if (bitset<kExactFloorItems>(rowMask).count() <= 4) {
                members.clear();
                for (size_t bit = 0; bit < count; ++bit) {
                    if (rowMask & (size_t{1} << bit)) members.push_back(placeableIndices[bit]);
                }
                if (optional<FloorRow> row = rows.best(members)) {
                    const LayoutScore candidate = states[placed].score.plus(*row);
                    SearchState& next = states[placed | rowMask];
                    if (!next.reached || betterThan(candidate, next.score)) {
                        next = {true, candidate, placed, std::move(*row)};
                    }
                }
            }
            if (extra == 0) break;
        }
    }

    vector<FloorRow> result;
    if (count > 0) {
        for (size_t placed = full; placed != 0; placed = states[placed].previous) {
            result.push_back(states[placed].row);
        }
        reverse(result.begin(), result.end());
    }
    return result;
}

enum class GroupingSearchEnd { Fits, NoneFits, OutOfStates };

// Items by footprint. A row's depth depends on its members' footprints alone, so a search over
// groupings counts each footprint's copies: loads that differ only in which copy stands where are
// one state.
struct FootprintGroups {
    vector<vector<size_t>> membersOf;  // item indices of each footprint, most levels first
    vector<size_t> footprintOfItem;    // parallel to the items; meaningful for those grouped
};

FootprintGroups footprintGroupsOf(const vector<FloorItem>& items, const vector<size_t>& indices) {
    auto footprintOf = [&](size_t index) { return make_pair(-items[index].longSideIn, -items[index].shortSideIn); };
    // By levels within a footprint, so copies that stand alike are placed together.
    vector<size_t> order = indices;
    sort(order.begin(), order.end(), [&](size_t left, size_t right) {
        return make_tuple(footprintOf(left), -items[left].levels, left)
               < make_tuple(footprintOf(right), -items[right].levels, right);
    });
    FootprintGroups groups;
    groups.footprintOfItem.assign(items.size(), 0);
    for (const size_t index : order) {
        if (groups.membersOf.empty() || footprintOf(groups.membersOf.back().front()) != footprintOf(index)) {
            groups.membersOf.emplace_back();
        }
        groups.membersOf.back().push_back(index);
        groups.footprintOfItem[index] = groups.membersOf.size() - 1;
    }
    return groups;
}

// The states a grouping search reached, by index in the order reached, each with how many of each
// footprint it has placed, the length of its rows and the row that reached it.
struct GroupingStates {
    GroupingSearchEnd end = GroupingSearchEnd::NoneFits;
    size_t footprintCount = 0;
    vector<uint64_t> numberOf;     // the state as one mixed-radix number
    vector<uint32_t> placedOf;     // footprintCount counts a state
    vector<double> lengthOf;
    vector<size_t> previousOf;     // the state the row was added to; the first state is its own
    vector<uint64_t> rowKeyOf;     // names the row that reached it
    size_t everyItemPlaced = 0;    // its index, when reached

    size_t size() const { return numberOf.size(); }
};

// State numbers to their index in GroupingStates, by open addressing: the search's hottest step.
class StateIndex {
public:
    static constexpr size_t kAbsent = numeric_limits<size_t>::max();

    StateIndex() : slots_(1024, kEmpty), indexOf_(1024, 0) {}

    size_t find(uint64_t number) const {
        for (size_t slot = slotOf(number);; slot = (slot + 1) & (slots_.size() - 1)) {
            if (slots_[slot] == kEmpty) return kAbsent;
            if (slots_[slot] == number) return indexOf_[slot];
        }
    }

    // Records `number` at `index`; it must not be held already.
    void add(uint64_t number, size_t index) {
        if (2 * (held_ + 1) > slots_.size()) grow();
        size_t slot = slotOf(number);
        while (slots_[slot] != kEmpty) slot = (slot + 1) & (slots_.size() - 1);
        slots_[slot] = number;
        indexOf_[slot] = index;
        ++held_;
    }

private:
    // No state number reaches it: numbers stay below the product of every footprint's count plus one.
    static constexpr uint64_t kEmpty = numeric_limits<uint64_t>::max();

    size_t slotOf(uint64_t number) const {
        return static_cast<size_t>((number * 0x9E3779B97F4A7C15ULL) >> 32) & (slots_.size() - 1);
    }

    void grow() {
        vector<uint64_t> slots(slots_.size() * 2, kEmpty);
        vector<size_t> indexOf(slots.size(), 0);
        slots.swap(slots_);
        indexOf.swap(indexOf_);
        held_ = 0;
        for (size_t slot = 0; slot < slots.size(); ++slot) {
            if (slots[slot] != kEmpty) add(slots[slot], indexOf[slot]);
        }
    }

    vector<uint64_t> slots_;
    vector<size_t> indexOf_;
    size_t held_ = 0;
};

// A row the grouping search may add, listed in preorder: each row is followed by the rows that
// add footprints to it, so a row that cannot be formed is skipped with all of them.
struct CandidateRow {
    size_t footprint = 0;   // the last footprint the row adds
    size_t copiesOfIt = 0;  // copies of that footprint the row holds
    size_t size = 0;
    size_t extensionsEnd = 0;  // the entry after every row extending this one
    uint64_t stateStep = 0;    // added to a state's number
    double shareSum = 0.0;
    array<size_t, 4> footprints{};
    double depthIn = numeric_limits<double>::quiet_NaN();  // asked of rowDepth when first offered
    uint64_t rowKey = 0;
};

// The shortest grouping into rows, within lengthLimitIn, of countOf[f] items of each footprint f,
// by dynamic programming over how many of each are placed. Each state is reached by adding one row
// that holds an item of its first footprint not yet placed, so every grouping is reached: thirty
// stacks of three footprints have at most 11^3 states where the stacks have 2^30 subsets. A state
// is passed over once its rows and its unplaced items' shares (each footprint's least share of a
// row's depth) pass the limit. rowDepth(footprints, size, rowKey) gives the depth of a row of
// those footprints, infinite for none, and a key naming it; it is asked at most once for each row.
template <typename RowDepth>
GroupingStates searchGroupingStates(const vector<size_t>& countOf, const vector<double>& shareOf, double lengthLimitIn,
                                    long long stateLimit, bool fourMemberRows, RowDepth&& rowDepth) {
    GroupingStates search;
    const size_t footprintCount = countOf.size();
    // A state is how many of each footprint are placed, as one mixed-radix number. A load of more
    // than 63 footprints cannot be numbered, and is left unsearched.
    vector<uint64_t> stride(footprintCount);
    uint64_t stateSpace = 1;
    size_t itemCount = 0;
    double totalShare = 0.0;
    for (size_t footprint = 0; footprint < footprintCount; ++footprint) {
        const uint64_t radix = countOf[footprint] + 1;
        if (stateSpace > numeric_limits<uint64_t>::max() / radix) {
            search.end = GroupingSearchEnd::OutOfStates;
            return search;
        }
        stride[footprint] = stateSpace;
        stateSpace *= radix;
        itemCount += countOf[footprint];
        totalShare += shareOf[footprint] * static_cast<double>(countOf[footprint]);
    }
    if (totalShare > lengthLimitIn) return search;

    // Every row the load's items can form, by first footprint, in the order offered.
    vector<CandidateRow> candidates;
    vector<size_t> candidatesFrom(footprintCount + 1, 0);
    vector<size_t> left = countOf;
    auto addCandidate = [&](const CandidateRow* shorter, size_t footprint) {
        CandidateRow row;
        if (shorter) row = *shorter;
        row.footprint = footprint;
        row.copiesOfIt = shorter && shorter->footprint == footprint ? shorter->copiesOfIt + 1 : 1;
        row.footprints[row.size++] = footprint;
        row.stateStep += stride[footprint];
        row.shareSum += shareOf[footprint];
        candidates.push_back(row);
        return candidates.size() - 1;
    };
    for (size_t first = 0; first < footprintCount; ++first) {
        candidatesFrom[first] = candidates.size();
        --left[first];
        const size_t oneMember = addCandidate(nullptr, first);
        for (size_t second = first; second < footprintCount; ++second) {
            if (left[second] == 0) continue;
            --left[second];
            const size_t twoMembers = addCandidate(&candidates[oneMember], second);
            for (size_t third = second; third < footprintCount; ++third) {
                if (left[third] == 0) continue;
                --left[third];
                const size_t threeMembers = addCandidate(&candidates[twoMembers], third);
                for (size_t fourth = third; fourMemberRows && fourth < footprintCount; ++fourth) {
                    if (left[fourth] == 0) continue;
                    const size_t fourMembers = addCandidate(&candidates[threeMembers], fourth);
                    candidates[fourMembers].extensionsEnd = fourMembers + 1;
                }
                candidates[threeMembers].extensionsEnd = candidates.size();
                ++left[third];
            }
            candidates[twoMembers].extensionsEnd = candidates.size();
            ++left[second];
        }
        candidates[oneMember].extensionsEnd = candidates.size();
        ++left[first];
    }
    candidatesFrom[footprintCount] = candidates.size();

    search.footprintCount = footprintCount;
    StateIndex indexOfNumber;
    auto addState = [&](uint64_t number, const uint32_t* placed, double lengthIn, size_t previous, uint64_t rowKey) {
        const size_t index = search.size();
        search.numberOf.push_back(number);
        search.placedOf.insert(search.placedOf.end(), placed, placed + footprintCount);
        search.lengthOf.push_back(lengthIn);
        search.previousOf.push_back(previous);
        search.rowKeyOf.push_back(rowKey);
        indexOfNumber.add(number, index);
        return index;
    };
    vector<uint32_t> placed(footprintCount, 0);
    addState(0, placed.data(), 0.0, 0, 0);
    // States by items placed: every row adds at least one, so a state is final before it is expanded.
    vector<vector<size_t>> statesByPlaced(itemCount + 1);
    statesByPlaced[0].push_back(0);
    vector<size_t> available(footprintCount);
    vector<uint32_t> nextPlaced(footprintCount);
    for (size_t placedCount = 0; placedCount < itemCount; ++placedCount) {
        for (const size_t from : statesByPlaced[placedCount]) {
            const uint64_t number = search.numberOf[from];
            const double fromLength = search.lengthOf[from];
            copy_n(search.placedOf.begin() + static_cast<ptrdiff_t>(from * footprintCount), footprintCount, placed.begin());
            double unplacedShare = totalShare;
            size_t firstFootprint = footprintCount;
            for (size_t footprint = 0; footprint < footprintCount; ++footprint) {
                available[footprint] = countOf[footprint] - placed[footprint];
                unplacedShare -= shareOf[footprint] * static_cast<double>(placed[footprint]);
                if (available[footprint] > 0 && firstFootprint == footprintCount) firstFootprint = footprint;
            }
            for (size_t next = candidatesFrom[firstFootprint]; next < candidatesFrom[firstFootprint + 1];) {
                CandidateRow& row = candidates[next];
                if (available[row.footprint] < row.copiesOfIt) {
                    next = row.extensionsEnd;
                    continue;
                }
                ++next;
                if (isnan(row.depthIn)) row.depthIn = rowDepth(row.footprints, row.size, row.rowKey);
                if (!isfinite(row.depthIn)) continue;
                const double lengthIn = fromLength + row.depthIn;
                if (lengthIn + unplacedShare - row.shareSum > lengthLimitIn) continue;
                const uint64_t nextNumber = number + row.stateStep;
                const size_t known = indexOfNumber.find(nextNumber);
                if (known != StateIndex::kAbsent) {
                    if (lengthIn < search.lengthOf[known] - kQuantityEpsilon) {
                        search.lengthOf[known] = lengthIn;
                        search.previousOf[known] = from;
                        search.rowKeyOf[known] = row.rowKey;
                    }
                    continue;
                }
                if (static_cast<long long>(search.size()) >= stateLimit) {
                    search.end = GroupingSearchEnd::OutOfStates;
                    return search;
                }
                nextPlaced = placed;
                for (size_t position = 0; position < row.size; ++position) ++nextPlaced[row.footprints[position]];
                statesByPlaced[placedCount + row.size].push_back(
                    addState(nextNumber, nextPlaced.data(), lengthIn, from, row.rowKey));
            }
        }
    }
    const size_t full = indexOfNumber.find(stateSpace - 1);
    if (full != StateIndex::kAbsent) {
        search.end = GroupingSearchEnd::Fits;
        search.everyItemPlaced = full;
    }
    return search;
}

// A multiset of up to four footprints (indices below footprintCount, ascending) as one number.
uint64_t rowKeyOf(const array<size_t, 4>& footprints, size_t size, size_t footprintCount) {
    uint64_t key = 0;
    for (size_t position = 0; position < size; ++position) key = key * (footprintCount + 1) + footprints[position] + 1;
    return key;
}

// The states one search may hold: the setting's, and no more than the budget has left.
long long stateLimitOf(const FloorPatternSettings& settings, const FloorSearchBudget* budget) {
    return budget ? min(settings.searchStateLimit, budget->statesLeft) : settings.searchStateLimit;
}

void drawStates(FloorSearchBudget* budget, size_t states) {
    if (budget) budget->statesLeft -= static_cast<long long>(states);
}

struct GroupingSearch {
    GroupingSearchEnd end = GroupingSearchEnd::NoneFits;
    vector<FloorRow> rows;  // front to back, when a grouping fits
};

// The shortest grouping of the placeable items into rows that fits the trailer.
GroupingSearch searchGroupings(const vector<FloorItem>& items, const vector<size_t>& placeableIndices,
                               const RowBuilder& rows, const TrailerSpec& trailer,
                               const FloorPatternSettings& settings, FloorSearchBudget* budget) {
    const FootprintGroups groups = footprintGroupsOf(items, placeableIndices);
    const size_t footprintCount = groups.membersOf.size();
    const vector<double> shares = rowLengthShares(items, trailer, settings);
    vector<size_t> countOf(footprintCount);
    vector<double> shareOf(footprintCount);
    for (size_t footprint = 0; footprint < footprintCount; ++footprint) {
        countOf[footprint] = groups.membersOf[footprint].size();
        shareOf[footprint] = shares[groups.membersOf[footprint].front()];
    }

    // Each row is weighed once, on the first copies of its footprints.
    unordered_map<uint64_t, optional<FloorRow>> rowOf;
    vector<size_t> members;
    members.reserve(4);
    auto rowDepth = [&](const array<size_t, 4>& footprints, size_t size, uint64_t& rowKey) {
        rowKey = rowKeyOf(footprints, size, footprintCount);
        const auto [found, inserted] = rowOf.try_emplace(rowKey);
        if (inserted) {
            members.clear();
            for (size_t position = 0; position < size; ++position) {
                const auto first = footprints.begin();
                const size_t copy = static_cast<size_t>(count(first, first + static_cast<ptrdiff_t>(position), footprints[position]));
                members.push_back(groups.membersOf[footprints[position]][copy]);
            }
            found->second = rows.best(members);
        }
        return found->second ? found->second->depthIn : numeric_limits<double>::infinity();
    };
    const GroupingStates states = searchGroupingStates(countOf, shareOf, trailer.interiorLengthIn + kQuantityEpsilon,
                                                       stateLimitOf(settings, budget), rows.allowsPinwheel(), rowDepth);
    drawStates(budget, states.size());
    if (states.end != GroupingSearchEnd::Fits) return {states.end, {}};

    vector<uint64_t> rowKeys;
    for (size_t state = states.everyItemPlaced; state != 0; state = states.previousOf[state]) {
        rowKeys.push_back(states.rowKeyOf[state]);
    }
    vector<vector<size_t>> footprintsOfRow;  // front to back
    for (auto rowKey = rowKeys.rbegin(); rowKey != rowKeys.rend(); ++rowKey) {
        vector<size_t>& footprints = footprintsOfRow.emplace_back();
        for (const FloorPlacement& placement : rowOf.at(*rowKey)->placements) {
            footprints.push_back(groups.footprintOfItem[placement.itemIndex]);
        }
    }
    // Which copy of a footprint stands where changes no row's depth, only whether a row's members
    // stand at the same levels. So each row, fullest first, takes copies of one number of levels
    // its footprints all still have, a single row one level where it can; the rest take what is
    // left. Each row is then rebuilt on its copies, with their own lone-top flag.
    vector<map<int, vector<size_t>>> copiesLeft(footprintCount);  // by footprint, then levels
    for (size_t footprint = 0; footprint < footprintCount; ++footprint) {
        for (const size_t index : groups.membersOf[footprint]) copiesLeft[footprint][items[index].levels].push_back(index);
    }
    auto take = [&](size_t footprint, int levels) {
        vector<size_t>& copies = copiesLeft[footprint][levels];
        const size_t index = copies.back();
        copies.pop_back();
        if (copies.empty()) copiesLeft[footprint].erase(levels);
        return index;
    };
    vector<size_t> rowOrder(footprintsOfRow.size());
    for (size_t row = 0; row < rowOrder.size(); ++row) rowOrder[row] = row;
    stable_sort(rowOrder.begin(), rowOrder.end(),
                [&](size_t left, size_t right) { return footprintsOfRow[left].size() > footprintsOfRow[right].size(); });
    vector<vector<size_t>> membersOfRow(footprintsOfRow.size());
    for (const size_t row : rowOrder) {
        map<size_t, size_t> needOf;  // copies of each footprint
        for (const size_t footprint : footprintsOfRow[row]) ++needOf[footprint];
        auto spareAt = [&](int levels) {
            size_t spare = numeric_limits<size_t>::max();
            for (const auto& [footprint, need] : needOf) {
                const auto copies = copiesLeft[footprint].find(levels);
                const size_t held = copies == copiesLeft[footprint].end() ? 0 : copies->second.size();
                if (held < need) return optional<size_t>();
                spare = min(spare, held - need);
            }
            return optional<size_t>(spare);
        };
        optional<int> chosenLevels;
        if (footprintsOfRow[row].size() == 1 && spareAt(1)) {
            chosenLevels = 1;
        } else {
            optional<size_t> mostSpare;
            for (const auto& [levels, copies] : copiesLeft[footprintsOfRow[row].front()]) {
                const optional<size_t> spare = spareAt(levels);
                if (spare && (!mostSpare || *spare > *mostSpare)) {
                    mostSpare = spare;
                    chosenLevels = levels;
                }
            }
        }
        if (!chosenLevels) continue;
        for (const size_t footprint : footprintsOfRow[row]) membersOfRow[row].push_back(take(footprint, *chosenLevels));
    }
    for (size_t row = 0; row < footprintsOfRow.size(); ++row) {
        if (!membersOfRow[row].empty()) continue;
        for (const size_t footprint : footprintsOfRow[row]) {
            membersOfRow[row].push_back(take(footprint, copiesLeft[footprint].begin()->first));
        }
    }
    GroupingSearch search{GroupingSearchEnd::Fits, {}};
    for (const vector<size_t>& rowMembers : membersOfRow) search.rows.push_back(rows.best(rowMembers).value());
    return search;
}

// What loadGrowthCheck's check keeps for one set of goods. Its RowBuilder refers to its own copies
// and settings, so it is never copied: the check holds it by pointer.
class LoadGrowth {
public:
    LoadGrowth(vector<FloorItem> items, const TrailerSpec& trailer, const FloorPatternSettings& settings,
               shared_ptr<FloorSearchBudget> budget)
        : items_(std::move(items)), trailer_(trailer), settings_(settings), budget_(std::move(budget)),
          groups_(footprintGroupsOf(items_, allIndices(items_.size()))),
          copies_(copiesOf(items_, groups_)), rows_(copies_, trailer.interiorWidthIn, settings_) {
        const vector<double> shares = rowLengthShares(items_, trailer_, settings_);
        for (const vector<size_t>& members : groups_.membersOf) shareOf_.push_back(shares[members.front()]);
        countOf_.assign(groups_.membersOf.size(), 0u);
    }
    LoadGrowth(const LoadGrowth&) = delete;
    LoadGrowth& operator=(const LoadGrowth&) = delete;

    bool mayFit(const vector<size_t>& load) {
        fill(countOf_.begin(), countOf_.end(), 0u);
        double totalShare = 0.0;
        for (const size_t index : load) {
            ++countOf_[groups_.footprintOfItem[index]];
            totalShare += shareOf_[groups_.footprintOfItem[index]];
        }
        if (totalShare > trailer_.interiorLengthIn + kQuantityEpsilon) return false;
        const auto known = verdictOf_.find(countOf_);
        if (known != verdictOf_.end()) return known->second;
        const optional<bool> verdict = verdictByDominance();
        const bool couldFit = verdict ? *verdict : search(load);
        if (verdictOf_.size() == kVerdictsKept) verdictOf_.clear();
        verdictOf_.emplace(countOf_, couldFit);
        return couldFit;
    }

private:
    using Counts = vector<uint32_t>;  // copies of each footprint in a load

    struct CountsHash {
        size_t operator()(const Counts& counts) const {
            size_t hash = counts.size();
            for (const uint32_t copies : counts) hash = hash * 1000003u ^ copies;
            return hash;
        }
    };

    // Relaxed rows only grow with goods: a load inside one that may fit may fit, and a load holding
    // one that cannot fit cannot.
    optional<bool> verdictByDominance() const {
        auto within = [](const Counts& inner, const Counts& outer) {
            for (size_t footprint = 0; footprint < inner.size(); ++footprint) {
                if (inner[footprint] > outer[footprint]) return false;
            }
            return true;
        };
        for (const Counts& fitting : mayFitLoads_) {
            if (within(countOf_, fitting)) return true;
        }
        for (const Counts& cut : cutLoads_) {
            if (within(cut, countOf_)) return false;
        }
        return nullopt;
    }

    // A grouping that fits the load is a grouping of its relaxed rows that fits, so the sorted rows
    // fitting settle it; otherwise the relaxed rows are searched.
    bool search(const vector<size_t>& load) {
        vector<FloorItem> loadItems;
        loadItems.reserve(load.size());
        for (const size_t index : load) loadItems.push_back(items_[index]);
        bool couldFit = arrangeFloorSorted(loadItems, trailer_, settings_).fitsLength;
        // With no states left to search, the load cannot be cut, nor does it show that any other may fit.
        if (!couldFit && budget_ && budget_->statesLeft <= 0) return true;
        if (!couldFit) {
            const size_t footprintCount = groups_.membersOf.size();
            vector<size_t> present;  // the load's footprints, ascending
            vector<size_t> presentCount;
            vector<double> presentShare;
            for (size_t footprint = 0; footprint < footprintCount; ++footprint) {
                if (countOf_[footprint] == 0) continue;
                present.push_back(footprint);
                presentCount.push_back(countOf_[footprint]);
                presentShare.push_back(shareOf_[footprint]);
            }
            auto rowDepth = [&](const array<size_t, 4>& positions, size_t size, uint64_t& rowKey) {
                array<size_t, 4> footprints{};
                for (size_t position = 0; position < size; ++position) footprints[position] = present[positions[position]];
                rowKey = rowKeyOf(footprints, size, footprintCount);
                return relaxedDepth(footprints, size);
            };
            const GroupingStates states = searchGroupingStates(presentCount, presentShare,
                                                               trailer_.interiorLengthIn + kQuantityEpsilon,
                                                               stateLimitOf(settings_, budget_.get()),
                                                               rows_.allowsPinwheel(), rowDepth);
            drawStates(budget_.get(), states.size());
            const GroupingSearchEnd end = states.end;
            // Out of states, the load cannot be cut, nor does it show that any other may fit.
            if (end == GroupingSearchEnd::OutOfStates) return true;
            couldFit = end == GroupingSearchEnd::Fits;
        }
        deque<Counts>& loads = couldFit ? mayFitLoads_ : cutLoads_;
        if (loads.size() == kLoadsRemembered) loads.pop_back();
        loads.push_front(countOf_);
        return couldFit;
    }

    static constexpr size_t kMostCopies = 4;
    // Loads searched, remembered each way to answer the loads within or holding them, newest first.
    // Every answer is sound, so the oldest are dropped only to bound the scan.
    static constexpr size_t kLoadsRemembered = 32;
    // Verdicts kept by load; forgotten together when this many are held, which bounds the memory.
    static constexpr size_t kVerdictsKept = size_t{1} << 16;

    static vector<size_t> allIndices(size_t count) {
        vector<size_t> indices(count);
        for (size_t index = 0; index < count; ++index) indices[index] = index;
        return indices;
    }

    // Four copies of each footprint, one level each: what its rows are built from.
    static vector<FloorItem> copiesOf(const vector<FloorItem>& items, const FootprintGroups& groups) {
        vector<FloorItem> copies;
        copies.reserve(groups.membersOf.size() * kMostCopies);
        for (const vector<size_t>& members : groups.membersOf) {
            const FloorItem& item = items[members.front()];
            copies.insert(copies.end(), kMostCopies, FloorItem{item.longSideIn, item.shortSideIn, 0.0, 1, 0.0});
        }
        return copies;
    }

    // The shallowest row over the goods holding these footprints (ascending), itself or with more
    // members: a row short of members keeps its depth. Rows only of goods there are.
    double relaxedDepth(const array<size_t, 4>& footprints, size_t size) {
        const size_t footprintCount = groups_.membersOf.size();
        const uint64_t key = rowKeyOf(footprints, size, footprintCount);
        const auto known = relaxedDepthOf_.find(key);
        if (known != relaxedDepthOf_.end()) return known->second;
        vector<size_t> members;
        for (size_t position = 0; position < size; ++position) {
            const auto first = footprints.begin();
            members.push_back(footprints[position] * kMostCopies
                              + static_cast<size_t>(count(first, first + static_cast<ptrdiff_t>(position), footprints[position])));
        }
        const optional<FloorRow> own = rows_.best(members);
        double depth = own ? own->depthIn : numeric_limits<double>::infinity();
        // Every row holding an item is at least as deep as the item stands in it, so a row no
        // deeper than its deepest member can stand is never beaten by one holding more.
        double deepestMember = 0.0;
        for (size_t position = 0; position < size; ++position) {
            deepestMember = max(deepestMember, leastDepthOf(footprints[position]));
        }
        if (size < 4 && depth > deepestMember + kQuantityEpsilon) {
            for (size_t added = 0; added < footprintCount; ++added) {
                const auto first = footprints.begin();
                const size_t held = static_cast<size_t>(count(first, first + static_cast<ptrdiff_t>(size), added));
                if (held >= min(groups_.membersOf[added].size(), kMostCopies)) continue;
                array<size_t, 4> wider = footprints;
                wider[size] = added;
                sort(wider.begin(), wider.begin() + static_cast<ptrdiff_t>(size + 1));
                depth = min(depth, relaxedDepth(wider, size + 1));
            }
        }
        relaxedDepthOf_.emplace(key, depth);
        return depth;
    }

    // The least trailer length the footprint can take in any row: its short side, unless it must be
    // turned to stand at all or the site never turns it long side across.
    double leastDepthOf(size_t footprint) const {
        const FloorItem& item = items_[groups_.membersOf[footprint].front()];
        const bool longSideAcross = item.longSideIn <= trailer_.interiorWidthIn + kQuantityEpsilon
                                    && (settings_.allowRotation || settings_.fixedOrientation == FixedOrientation::LongSideAcross);
        return longSideAcross ? item.shortSideIn : item.longSideIn;
    }

    vector<FloorItem> items_;
    TrailerSpec trailer_;
    FloorPatternSettings settings_;
    shared_ptr<FloorSearchBudget> budget_;  // none: each search holds up to searchStateLimit
    FootprintGroups groups_;
    vector<FloorItem> copies_;
    RowBuilder rows_;
    vector<double> shareOf_;
    unordered_map<uint64_t, double> relaxedDepthOf_;
    Counts countOf_;  // the load being judged
    unordered_map<Counts, bool, CountsHash> verdictOf_;
    deque<Counts> mayFitLoads_;
    deque<Counts> cutLoads_;
};

} // anonymous namespace

FloorItem floorItemFrom(const SelectionItem& item) {
    return {item.footprintLongIn, item.footprintShortIn, item.heightIn,
            static_cast<int>(item.lineIndices.size()), item.cubeCubicIn};
}

FloorLayout arrangeFloorSorted(const vector<FloorItem>& items, const TrailerSpec& trailer,
                               const FloorPatternSettings& settings) {
    validate(items, trailer, settings);
    FloorLayout layout;
    const RowBuilder rows(items, trailer.interiorWidthIn, settings);
    const vector<size_t> placeableIndices = placeableItems(items, rows, trailer, layout);
    finishLayout(layout, sortedRows(items, placeableIndices, rows), items, trailer, settings);
    return layout;
}

FloorLayout arrangeFloorExact(const vector<FloorItem>& items, const TrailerSpec& trailer,
                              const FloorPatternSettings& settings) {
    validate(items, trailer, settings);
    FloorLayout layout;
    const RowBuilder rows(items, trailer.interiorWidthIn, settings);
    const vector<size_t> placeableIndices = placeableItems(items, rows, trailer, layout);
    if (placeableIndices.size() > kExactFloorItems) {
        throw invalid_argument("Exact floor search takes at most " + to_string(kExactFloorItems)
                               + " items, given " + to_string(placeableIndices.size()));
    }
    finishLayout(layout, exactRows(placeableIndices, rows), items, trailer, settings);
    return layout;
}

FloorLayout arrangeFloor(const vector<FloorItem>& items, const TrailerSpec& trailer,
                         const FloorPatternSettings& settings, FloorSearchBudget* budget) {
    validate(items, trailer, settings);
    FloorLayout layout;
    const RowBuilder rows(items, trailer.interiorWidthIn, settings);
    const vector<size_t> placeableIndices = placeableItems(items, rows, trailer, layout);
    if (placeableIndices.size() <= kExactFloorItems) {
        finishLayout(layout, exactRows(placeableIndices, rows), items, trailer, settings);
        return layout;
    }
    FloorLayout sorted = layout;
    finishLayout(sorted, sortedRows(items, placeableIndices, rows), items, trailer, settings);
    if (sorted.fitsLength) return sorted;
    // The sorted order is one grouping of many: eleven stacks it needs 640 in for stand in 586 in
    // grouped otherwise (Tom Moore, 12 Nov 2026), and a load it refuses must not be refused unseen.
    if (budget && budget->statesLeft <= 0) {
        sorted.fitUnproven = true;
        return sorted;
    }
    GroupingSearch search = searchGroupings(items, placeableIndices, rows, trailer, settings, budget);
    if (search.end == GroupingSearchEnd::Fits) {
        finishLayout(layout, std::move(search.rows), items, trailer, settings);
        return layout;
    }
    sorted.fitUnproven = search.end == GroupingSearchEnd::OutOfStates;
    return sorted;
}

vector<double> rowLengthShares(const vector<FloorItem>& items, const TrailerSpec& trailer,
                               const FloorPatternSettings& settings) {
    validate(items, trailer, settings);
    // Depth depends on footprint alone, so each footprint is tried once, with as many copies of it
    // as the items hold, up to three, to stand beside itself in a row.
    vector<pair<double, double>> footprints;
    footprints.reserve(items.size());
    for (const FloorItem& item : items) footprints.emplace_back(item.longSideIn, item.shortSideIn);
    sort(footprints.begin(), footprints.end());
    constexpr size_t kMostCopies = 3;
    vector<size_t> copiesOf;
    vector<FloorItem> copies;
    for (size_t start = 0; start < footprints.size();) {
        size_t end = start;
        while (end < footprints.size() && footprints[end] == footprints[start]) ++end;
        copiesOf.push_back(min(end - start, kMostCopies));
        copies.insert(copies.end(), kMostCopies, FloorItem{footprints[start].first, footprints[start].second, 0.0, 1, 0.0});
        start = end;
    }
    footprints.erase(unique(footprints.begin(), footprints.end()), footprints.end());
    const RowBuilder rows(copies, trailer.interiorWidthIn, settings);
    auto copyOf = [&](size_t footprint, size_t copy) { return footprint * kMostCopies + copy; };
    // The shallowest row of these footprints, or infinite when the items hold too few of one, or
    // no allowed pattern holds them.
    vector<size_t> members;
    members.reserve(kMostCopies);
    auto depthOf = [&](initializer_list<size_t> footprintsInRow) {
        members.clear();
        for (auto footprint = footprintsInRow.begin(); footprint != footprintsInRow.end(); ++footprint) {
            const size_t copy = static_cast<size_t>(count(footprintsInRow.begin(), footprint, *footprint));
            if (copy >= copiesOf[*footprint]) return numeric_limits<double>::infinity();
            members.push_back(copyOf(*footprint, copy));
        }
        const optional<FloorRow> row = rows.best(members);
        return row ? row->depthIn : numeric_limits<double>::infinity();
    };

    const size_t footprintCount = footprints.size();
    vector<char> placeableFootprint(footprintCount, 0);
    double smallestLongSide = numeric_limits<double>::infinity();
    double smallestShortSide = numeric_limits<double>::infinity();
    for (size_t footprint = 0; footprint < footprintCount; ++footprint) {
        placeableFootprint[footprint] = placeable(copyOf(footprint, 0), rows, trailer) ? 1 : 0;
        if (!placeableFootprint[footprint]) continue;
        smallestLongSide = min(smallestLongSide, footprints[footprint].first);
        smallestShortSide = min(smallestShortSide, footprints[footprint].second);
    }

    vector<double> shareOf(footprintCount, numeric_limits<double>::infinity());
    for (size_t first = 0; first < footprintCount; ++first) {
        if (!placeableFootprint[first]) continue;
        double share = depthOf({first});
        for (size_t second = 0; second < footprintCount; ++second) {
            if (!placeableFootprint[second]) continue;
            share = min(share, depthOf({first, second}) / 2.0);
            for (size_t third = second; third < footprintCount; ++third) {
                if (!placeableFootprint[third]) continue;
                share = min(share, depthOf({first, second, third}) / 3.0);
            }
        }
        // A pinwheel's depth is at least the column the item stands in: long side across, its
        // short side behind or in front of a turned item's long side; turned, its long side and an
        // unturned item's short side. The other three are bounded by the smallest sides placed.
        if (rows.allowsPinwheel() && items.size() >= 4) {
            const double longSide = footprints[first].first, shortSide = footprints[first].second;
            if (fitsWithin(longSide + smallestShortSide, trailer.interiorWidthIn)) {
                share = min(share, (shortSide + smallestLongSide) / 4.0);
            }
            if (fitsWithin(shortSide + smallestLongSide, trailer.interiorWidthIn)) {
                share = min(share, (longSide + smallestShortSide) / 4.0);
            }
        }
        shareOf[first] = share;
    }

    vector<double> shares;
    shares.reserve(items.size());
    for (const FloorItem& item : items) {
        const auto found = lower_bound(footprints.begin(), footprints.end(), make_pair(item.longSideIn, item.shortSideIn));
        shares.push_back(shareOf[static_cast<size_t>(found - footprints.begin())]);
    }
    return shares;
}

vector<FloorRow> candidateRows(const vector<FloorItem>& items, const vector<size_t>& members,
                               const TrailerSpec& trailer, const FloorPatternSettings& settings) {
    validate(items, trailer, settings);
    if (members.empty() || members.size() > 4) {
        throw invalid_argument("A floor row holds one to four items, given " + to_string(members.size()));
    }
    for (size_t position = 0; position < members.size(); ++position) {
        if (members[position] >= items.size()) {
            throw invalid_argument("Floor row member " + to_string(members[position]) + " is not one of "
                                   + to_string(items.size()) + " items");
        }
        if (find(members.begin(), members.begin() + static_cast<ptrdiff_t>(position), members[position])
            != members.begin() + static_cast<ptrdiff_t>(position)) {
            throw invalid_argument("Floor row member " + to_string(members[position]) + " is listed twice");
        }
    }
    return RowBuilder(items, trailer.interiorWidthIn, settings).candidates(members);
}

LoadGrowthCheck loadGrowthCheck(vector<FloorItem> items, const TrailerSpec& trailer,
                                const FloorPatternSettings& settings, shared_ptr<FloorSearchBudget> budget) {
    validate(items, trailer, settings);
    auto check = make_shared<LoadGrowth>(std::move(items), trailer, settings, std::move(budget));
    return [check](const vector<size_t>& load) { return check->mayFit(load); };
}

bool twoAcrossFits(double longSideIn, double shortSideIn, double trailerWidthIn) {
    return fitsWithin(2.0 * min(longSideIn, shortSideIn), trailerWidthIn);
}

const char* floorPatternName(FloorPatternKind pattern) {
    switch (pattern) {
    case FloorPatternKind::WideWide: return "wide-wide";
    case FloorPatternKind::WideNarrow: return "wide-narrow";
    case FloorPatternKind::NarrowNarrow: return "narrow-narrow";
    case FloorPatternKind::ThreeAcross: return "three-across";
    case FloorPatternKind::Pinwheel: return "pinwheel";
    case FloorPatternKind::Single: return "single";
    }
    return "unknown";
}

} // namespace ob
