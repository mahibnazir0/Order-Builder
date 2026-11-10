#include "floorPattern.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
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
    const bool footprintWins = betterThan(footprintScore, levelScore);
    finishLayout(layout, footprintWins ? std::move(footprintRows) : std::move(levelRows), items,
                 trailer, settings);
    return layout;
}

FloorLayout arrangeFloorExact(const vector<FloorItem>& items, const TrailerSpec& trailer,
                              const FloorPatternSettings& settings) {
    validate(items, trailer, settings);
    FloorLayout layout;
    const RowBuilder rows(items, trailer.interiorWidthIn, settings);
    const vector<size_t> placeableIndices = placeableItems(items, rows, trailer, layout);
    const size_t count = placeableIndices.size();
    if (count > kExactFloorItems) {
        throw invalid_argument("Exact floor search takes at most " + to_string(kExactFloorItems)
                               + " items, given " + to_string(count));
    }

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
    finishLayout(layout, std::move(result), items, trailer, settings);
    return layout;
}

FloorLayout arrangeFloor(const vector<FloorItem>& items, const TrailerSpec& trailer,
                         const FloorPatternSettings& settings) {
    validate(items, trailer, settings);
    const RowBuilder rows(items, trailer.interiorWidthIn, settings);
    size_t placeableCount = 0;
    for (size_t index = 0; index < items.size(); ++index) {
        if (placeable(index, rows, trailer)) ++placeableCount;
    }
    return placeableCount <= kExactFloorItems ? arrangeFloorExact(items, trailer, settings)
                                              : arrangeFloorSorted(items, trailer, settings);
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
