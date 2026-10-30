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
// The arrangement that uses least length is chosen. Between equal lengths,
// fewer rows with a lone top pallet win, then wide-wide over the others.
//
// A row whose members stand at different numbers of levels, or a single item
// stacked more than one high, leaves a top pallet with no side support: the
// client's "59 pallets leaves one stacked in the air". It is avoided where the
// length allows, and flagged where it does not.
// ============================================================================

#include "selectionItem.hpp"
#include "trailerSpec.hpp"

#include <cstddef>
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
};

// Items up to this many are arranged by exact search over every grouping into rows; more are
// arranged by arrangeFloorSorted.
constexpr std::size_t kExactFloorItems = 10;

// Arranges the items on one truck's floor. Throws std::invalid_argument for a trailer with a
// zero, negative or non-finite length, width or ceiling; settings that do not allow single
// rows, a cap of zero or less, or a full-cube share outside (0, 1]; or an item with a negative or
// non-finite figure, a long side shorter than its short side, or fewer than one level.
FloorLayout arrangeFloor(const std::vector<FloorItem>& items, const TrailerSpec& trailer,
                         const FloorPatternSettings& settings);

// The two arrangements arrangeFloor chooses between, exposed so each can be checked against
// the other. Exact tries every grouping and is for small inputs only (throws when more than
// kExactFloorItems items fit the trailer); sorted groups neighbours in two sort orders and keeps the better.
FloorLayout arrangeFloorExact(const std::vector<FloorItem>& items, const TrailerSpec& trailer,
                              const FloorPatternSettings& settings);
FloorLayout arrangeFloorSorted(const std::vector<FloorItem>& items, const TrailerSpec& trailer,
                               const FloorPatternSettings& settings);

// Whether two of this footprint fit side by side across the trailer in some orientation.
bool twoAcrossFits(double longSideIn, double shortSideIn, double trailerWidthIn);

// "wide-wide", "pinwheel", ... Plain ASCII.
const char* floorPatternName(FloorPatternKind pattern);

} // namespace ob
