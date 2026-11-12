#pragma once
// ============================================================================
// floorPattern.hpp - M4 module C3. How a truck's items sit on the trailer
// floor.
//
// Items stand in rows across the trailer width; each row costs the trailer
// length of its deepest member, and rear waste is the trailer length minus the
// rows. A row is one of:
//   wide-wide      two items, long sides across (the default: 48 x 40 gives
//                  96 in across, 40 in deep, so 15 rows; 16 at 39.4 in or less)
//   wide-narrow    one long side across, one short side across
//   narrow-narrow  two short sides across
//   three-across   three short sides across (a footprint of 33.3 in or less)
//   pinwheel       four items interlocked over two rows, alternating
//                  orientation; wins for long sides over 50 in, which cannot
//                  go wide-wide in a 100 in trailer
//   single         one item, an odd one out
// Up to kExactFloorItems items, the arrangement that uses least length is
// chosen. More are grouped in sorted order; where that does not fit the
// trailer, every grouping into rows is searched for the shortest that does, so
// a load fits exactly when some grouping of the allowed rows fits it. Between
// equal lengths, fewer rows with a lone top pallet win, then wide-wide over
// the others.
//
// A row whose members stand at different numbers of levels, or a single item
// stacked more than one high, leaves a top pallet with no side support: the
// client's "59 pallets leaves one stacked in the air". It is avoided where the
// length allows, and flagged where it does not.
// ============================================================================

#include "selectionItem.hpp"
#include "trailerSpec.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace ob {

enum class FloorPatternKind { WideWide, WideNarrow, NarrowNarrow, ThreeAcross, Pinwheel, Single };

// Which side goes across the trailer when items cannot be turned.
enum class FixedOrientation { LongSideAcross, ShortSideAcross };

struct FloorPatternSettings {
    // False for a site whose robotic forklifts cannot turn pallets: every item then goes in
    // with fixedOrientation's side across, so only the patterns that need no turning remain.
    bool allowRotation = true;
    FixedOrientation fixedOrientation = FixedOrientation::LongSideAcross;
    std::vector<FloorPatternKind> allowedPatterns{
        FloorPatternKind::WideWide,     FloorPatternKind::WideNarrow,
        FloorPatternKind::NarrowNarrow, FloorPatternKind::ThreeAcross,
        FloorPatternKind::Pinwheel,     FloorPatternKind::Single};
    // A customer's cap on pallets per truck (the client's Company F). Empty means none.
    std::optional<int> maxPositions;
    // Share of the trailer's cube at which a truck counts as full: 0.84 for Customer 2. No
    // default: it must come from the run's settings.
    double fullCubeFraction = 0.0;
    // Row groupings one search over every grouping may hold before it stops, leaving the load
    // refused unproven (FloorLayout::fitUnproven), and the groupings all the floor searches for one
    // route's knapsack may hold between them (FloorSearchBudget). No defaults: they must come from
    // the run's settings.
    long long searchStateLimit = 0;
    long long routeSearchStateLimit = 0;
};

// Row groupings several floor searches may hold between them: each draws its states from what is
// left, and one with none left stops at once. Bounds the floor work of a search that judges many
// loads, such as a route's knapsack, however many it judges.
struct FloorSearchBudget {
    long long statesLeft = 0;
};

struct FloorItem {
    double longSideIn = 0.0;
    double shortSideIn = 0.0;
    double heightIn = 0.0;
    int levels = 1;
    double cubeCubicIn = 0.0;
};

FloorItem floorItemFrom(const SelectionItem& item);

struct FloorPlacement {
    std::size_t itemIndex = 0;
    // Short side across: turned from the wide-wide default.
    bool turned = false;
};

struct FloorRow {
    FloorPatternKind pattern = FloorPatternKind::Single;
    std::vector<FloorPlacement> placements;
    double depthIn = 0.0;  // trailer length the row takes; a pinwheel's two rows together
    double widthIn = 0.0;  // trailer width its members fill
    bool loneTopPallet = false;
};

struct FloorLayout {
    std::vector<FloorRow> rows;  // front to back
    double usedLengthIn = 0.0;
    double rearWasteIn = 0.0;    // trailer length - usedLengthIn; negative when it does not fit
    bool fitsLength = false;
    int positions = 0;           // items placed
    bool withinMaxPositions = true;
    int turnedItems = 0;
    std::size_t loneTopPalletRows = 0;
    // Items that fit the trailer in no orientation. Left off the floor and named.
    std::vector<std::size_t> unplaceableItems;
    double cubeFraction = 0.0;   // placed items' cube / (length x width x ceiling)
    bool cubeFull = false;
    // The rows do not fit, and the search for a grouping that does stopped at its state limit
    // first: "does not fit" is then the sorted arrangement's verdict, not a proof.
    bool fitUnproven = false;
};

// Items up to this many are arranged by exact search over every grouping into rows; more are
// arranged by arrangeFloorSorted, and where that does not fit the trailer, by a search over every
// grouping for the shortest that does.
constexpr std::size_t kExactFloorItems = 10;

// Arranges the items on one truck's floor. Throws std::invalid_argument for a trailer with a
// zero, negative or non-finite length, width or ceiling; settings that do not allow single
// rows, a cap of zero or less, a full-cube share outside (0, 1] or a search state limit below 1;
// or an item with a negative or non-finite figure, a long side shorter than its short side, or
// fewer than one level.
//
// Above kExactFloorItems items the sorted arrangement is kept when it fits. When it does not,
// rows are searched over the items' footprints (a row's depth depends on nothing else): the
// shortest grouping that fits is returned, each row taking its footprints' copies by levels, so
// copies that stand alike share a row; when none fits, the sorted arrangement, which does not fit
// either; when the search passes searchStateLimit states, or what `budget` has left, first, the
// sorted arrangement flagged fitUnproven. An empty budget arranges without the search: exact up to
// kExactFloorItems items, in sorted order above.
FloorLayout arrangeFloor(const std::vector<FloorItem>& items, const TrailerSpec& trailer,
                         const FloorPatternSettings& settings, FloorSearchBudget* budget = nullptr);

// The two arrangements arrangeFloor chooses between, exposed so each can be checked against
// the other. Exact tries every grouping and is for small inputs only (throws when more than
// kExactFloorItems items fit the trailer); sorted groups neighbours in two sort orders and keeps the better.
FloorLayout arrangeFloorExact(const std::vector<FloorItem>& items, const TrailerSpec& trailer,
                              const FloorPatternSettings& settings);
FloorLayout arrangeFloorSorted(const std::vector<FloorItem>& items, const TrailerSpec& trailer,
                               const FloorPatternSettings& settings);

// The least trailer length each item can be charged on any floor of these items: the shallowest
// row the allowed patterns form with it from them, shared over the row's members (for a pinwheel,
// a bound below that). Every row arrangeFloor builds is at least its members' shares deep, so a
// load whose shares add up past the trailer does not fit, nor does any load holding it. Infinite
// for an item no row fits on the trailer. Throws as arrangeFloor does.
std::vector<double> rowLengthShares(const std::vector<FloorItem>& items, const TrailerSpec& trailer,
                                    const FloorPatternSettings& settings);

// Every row the allowed patterns form from `members` (one to four indices into items), too wide
// for the trailer or not: what arrangeFloor chooses each row from, to explain the choice. Throws
// as arrangeFloor does, and for no members, more than four, an index out of range or one repeated.
std::vector<FloorRow> candidateRows(const std::vector<FloorItem>& items, const std::vector<std::size_t>& members,
                                    const TrailerSpec& trailer, const FloorPatternSettings& settings);

// A check for a search that grows loads out of `items` (one route's goods): false only when no load
// of these items holding `load` (indices into items) fits the trailer. Rows are not monotone - a
// pinwheel or three across short of a stack can need more length than the full row - so it judges
// a relaxation that is: a row may hold any of the members of a row of these goods, at that row's
// depth, and adding goods never shortens such rows. A load whose relaxed rows pass the trailer can
// be cut with every load holding it. A search out of searchStateLimit states, or of what `budget`
// has left, cuts nothing. Throws as arrangeFloor does.
using LoadGrowthCheck = std::function<bool(const std::vector<std::size_t>& load)>;
LoadGrowthCheck loadGrowthCheck(std::vector<FloorItem> items, const TrailerSpec& trailer,
                                const FloorPatternSettings& settings,
                                std::shared_ptr<FloorSearchBudget> budget = nullptr);

// Whether two of this footprint fit side by side across the trailer in some orientation.
bool twoAcrossFits(double longSideIn, double shortSideIn, double trailerWidthIn);

// "wide-wide", "pinwheel", ... Plain ASCII.
const char* floorPatternName(FloorPatternKind pattern);

} // namespace ob
