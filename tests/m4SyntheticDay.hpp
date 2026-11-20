#pragma once

#include "laneClassifier.hpp"
#include "pipeline.hpp"
#include "reservationBook.hpp"
#include "selectionItem.hpp"
#include "selector.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// A day built by hand for the selector and its report: groups, items and reservations go
// through the real reservation book and classifier, so the selector sees what a run gives it.
namespace m4SyntheticDay {

inline const ob::TrailerSpec kTrailer{"53FT_NA", 630.0, 100.0, 108.0, 45000.0, 32, std::nullopt};
inline const ob::LaneClassifierSettings kRouting{20, 5.0};
inline const std::string kPlanDate = "2026-08-04";  // a Tuesday: the window is 6-7 Aug
inline const std::vector<ob::CTLRecord> kCtl{{"", "TUESDAY", 2, 3, 2}};

inline ob::SelectorSettings selectorSettings() {
    ob::SelectorSettings settings;
    settings.knapsack = {1.0, 1.0, 200'000};
    settings.patternPacker = {1.0, 1.0, 200, 200'000, 100'000};
    settings.binPacker = {1.0, 1.0, 50, 100'000, 2'000, 100, 100'000};
    settings.floor.fullCubeFraction = 0.84;
    settings.floor.searchStateLimit = 200'000;
    settings.floor.routeSearchStateLimit = 1'000'000;
    settings.secondsPerLane = 10.0;
    settings.threads = 1;
    settings.fillMissesPerTruck = 10;
    settings.weightSpreadRoundsPerTruck = 4;
    settings.maxUnitLoadsPerTruck = 64.0;
    return settings;
}

inline ob::SegregatedGroup group(const std::string& origin, ob::SplitReason reason = ob::SplitReason::None, const std::string& segregant = "") {
    ob::SegregatedGroup built;
    built.key.locationFrom = origin;
    built.key.locationTo = "2528";
    built.key.shipCondition = "TL";
    built.key.isSegregated = !segregant.empty();
    built.key.segregant = segregant;
    built.splitReason = reason;
    return built;
}

// What the selector and classifier read of a run: groups, the products of the joined lines
// (line i is product "P<i>") and the trailer.
inline ob::PipelineResult runWith(std::vector<ob::SegregatedGroup> groups, std::size_t productCount) {
    ob::PipelineResult run;
    run.demand.str.resize(productCount);
    for (std::size_t index = 0; index < productCount; ++index) run.demand.str[index].matnr = "P" + std::to_string(index);
    for (const ob::STRRecord& line : run.demand.str) {
        ob::JoinedLine joined;
        joined.str = &line;
        run.join.lines.push_back(joined);
    }
    run.segregation.groups = std::move(groups);
    run.trailer = kTrailer;
    run.ranMilestone2 = true;
    return run;
}

// A 48 x 40, 50 in unit load: 1,920 sq in of floor, 15 rows of two to a truck.
inline ob::SelectionItem item(std::size_t groupIndex, std::size_t lineIndex, double weightLb = 1000.0, int priority = 0,
                   double longSide = 48.0, double shortSide = 40.0) {
    ob::SelectionItem built;
    built.groupIndex = groupIndex;
    built.lineIndices = {lineIndex};
    built.footprintLongIn = longSide;
    built.footprintShortIn = shortSide;
    built.heightIn = 50.0;
    built.weightLb = weightLb;
    built.unitLoads = 1.0;
    built.cubeCubicIn = longSide * shortSide * 50.0;
    built.shipDate = "2026-08-06";
    built.availDate = "2026-08-01";
    built.priority = priority;
    return built;
}

// `levels` unit loads of 35 in stacked, so three stand under the 108 in ceiling.
inline ob::SelectionItem stackOf(std::size_t lineIndex, double longSide, double shortSide, int levels, double weightLb,
                                 const std::string& productNumber = "") {
    ob::SelectionItem built = item(0, lineIndex, weightLb, 0, longSide, shortSide);
    built.lineIndices.assign(static_cast<std::size_t>(levels), lineIndex);
    built.unitLoads = levels;
    built.heightIn = 35.0 * levels;
    built.cubeCubicIn = longSide * shortSide * built.heightIn;
    built.productNumber = productNumber;
    return built;
}

inline ob::PlaceholderRecord reserved(const std::string& origin, int trucks, const std::string& shipDate = "2026-08-06") {
    ob::PlaceholderRecord record;
    record.locfrno = origin;
    record.loctono = "2528";
    record.ship_cond = "TL";
    record.datfr_ta = shipDate;
    record.datto_ta = "2026-08-12";
    record.zzna_equip_size = "53F";
    record.no_of_loads = trucks;
    return record;
}

struct Day {
    ob::PipelineResult run;
    ob::ReservationBook book;
    ob::SelectionItems items;
    ob::LaneClassification classification;
};

inline Day dayOf(std::vector<ob::SegregatedGroup> groups, std::size_t products, std::vector<ob::SelectionItem> items,
          const std::vector<ob::PlaceholderRecord>& placeholders) {
    Day day;
    day.run = runWith(std::move(groups), products);
    day.book = ob::buildReservationBook(placeholders, kCtl, kPlanDate, {"53F", {"53F"}});
    // Each line ships on the lane of the group whose items carry it, as a real extract's do.
    for (const ob::SelectionItem& item : items) {
        const ob::GroupKey& lane = day.run.segregation.groups[item.groupIndex].key;
        for (const std::size_t lineIndex : item.lineIndices) {
            day.run.demand.str[lineIndex].locfrno = lane.locationFrom;
            day.run.demand.str[lineIndex].loctono = lane.locationTo;
            day.run.demand.str[lineIndex].ship_cond = lane.shipCondition;
        }
    }
    std::sort(items.begin(), items.end(), ob::shipsBefore);
    // What buildSelectionItems would count: every unit load stacked and carried by an item.
    for (const ob::SelectionItem& item : items) {
        day.items.unitLoadsIn += item.unitLoads;
        day.items.weightInLb += item.weightLb;
    }
    day.items.unitLoadsOut = day.items.unitLoadsIn;
    day.items.weightOutLb = day.items.weightInLb;
    day.items.items = std::move(items);
    day.classification = ob::classifyLanes(day.run, day.book, day.items, kRouting);
    return day;
}

inline ob::SelectionPlan plan(const Day& day, const ob::SelectorSettings& settings = selectorSettings()) {
    return ob::selectLoads(day.run, day.book, day.items, day.classification, settings);
}

// Sixteen stacks for one truck, all due the same day at the same priority: 34 unit loads that stand
// in exactly 630 in grouped one way, though in sorted order their rows need 692 in. A search that
// dropped a stack whenever the rows refused the load with it proved 31; one that trusted the sorted
// rows above ten stacks proved 32.
inline Day sixteenStacksWhereAll34Fit() {
    const std::vector<std::tuple<double, double, int>> stacks{
        {56, 52, 1}, {96, 40, 3}, {58, 54, 3}, {60, 52, 3}, {80, 40, 2}, {80, 40, 1}, {120, 48, 2}, {120, 48, 2},
        {60, 52, 3}, {80, 40, 1}, {56, 52, 2}, {72, 48, 2}, {58, 54, 2}, {60, 60, 3}, {56, 52, 3}, {48, 40, 1}};
    std::vector<ob::SelectionItem> items;
    for (std::size_t index = 0; index < stacks.size(); ++index) {
        const auto& [longSide, shortSide, levels] = stacks[index];
        // Zero-padded, so the products sort in the order listed.
        const std::string product = (index < 10 ? "P0" : "P") + std::to_string(index);
        items.push_back(stackOf(index, longSide, shortSide, levels, 1000.0 * levels, product));
    }
    return dayOf({group("2027")}, stacks.size(), items, {reserved("2027", 1)});
}

// One truck for two groups that must not mix, every good due the same day at priority 0: twelve
// 60 x 60 stacks, of which ten stand in its rows, and eleven 48 x 40, which all do (Tom Moore,
// 10 Nov 2026). By floor area the twelve are the larger truckload.
inline Day twelveLargeBesideElevenSmall() {
    std::vector<ob::SelectionItem> items(12, item(0, 0, 1000.0, 0, 60.0, 60.0));
    items.insert(items.end(), 11, item(1, 1, 1000.0, 0, 48.0, 40.0));
    return dayOf({group("2028"), group("2028", ob::SplitReason::DoNotMix, "S01")}, 2, items, {reserved("2028", 1)});
}

// Tom Moore's eleven stacks for one truck (12 Nov 2026), a unit load each, all due the same day at
// priority 0: grouped in sorted order their rows need 640 in, but all eleven stand in 586 in.
inline Day elevenStacksTheSortedRowsRefuse() {
    const std::vector<std::pair<double, double>> footprints{{100, 50}, {100, 60}, {60, 60},  {96, 40},
                                                             {100, 50}, {120, 48}, {80, 48},  {120, 48},
                                                             {120, 48}, {100, 60}, {80, 48}};
    std::vector<ob::SelectionItem> items;
    for (std::size_t index = 0; index < footprints.size(); ++index) {
        ob::SelectionItem stack = item(0, index, 1000.0, 0, footprints[index].first, footprints[index].second);
        stack.productNumber = "P" + std::to_string(100 + index);
        stack.copyIndex = index;
        items.push_back(stack);
    }
    return dayOf({group("2027")}, footprints.size(), items, {reserved("2027", 1)});
}

// Tom Moore's fourteen stacks for one truck (4 Nov 2026), all due the same day at priority 0: a
// 100 x 100 beside thirteen stacks of 23 unit loads that stand in 617 in. A search cut on the rows
// proved 22.
inline Day fourteenStacksWhere23Fit() {
    const std::vector<std::tuple<double, double, int>> stacks{
        {72, 60, 1}, {80, 48, 2}, {120, 48, 2}, {120, 48, 2}, {48, 48, 2}, {80, 48, 2}, {96, 40, 1},
        {72, 60, 2}, {80, 48, 2}, {72, 60, 1},  {60, 60, 2},  {60, 60, 2}, {40, 33, 2}};
    std::vector<ob::SelectionItem> items{item(0, 0, 1000.0, 0, 100.0, 100.0)};
    items.back().productNumber = "DISTRACTOR";
    for (std::size_t index = 0; index < stacks.size(); ++index) {
        const auto& [longSide, shortSide, levels] = stacks[index];
        ob::SelectionItem stack = item(0, index + 1, 1000.0, 0, longSide, shortSide);
        stack.unitLoads = levels;
        stack.lineIndices.assign(static_cast<std::size_t>(levels), index + 1);
        stack.heightIn = 50.0 * levels;
        stack.cubeCubicIn = longSide * shortSide * stack.heightIn;
        stack.productNumber = "P" + std::to_string(100 + index);
        stack.copyIndex = index;
        items.push_back(stack);
    }
    return dayOf({group("2027")}, stacks.size() + 1, items, {reserved("2027", 1)});
}

inline std::vector<ob::SelectionItem> repeated(std::size_t count, std::size_t groupIndex, std::size_t lineIndex, double weightLb = 1000.0) {
    return std::vector<ob::SelectionItem>(count, item(groupIndex, lineIndex, weightLb));
}

// Distinct products, one unit load each: the classifier sends these to binPacker.
inline std::vector<ob::SelectionItem> distinctProducts(std::size_t count, std::size_t groupIndex, std::size_t firstLine, double weightLb = 1000.0) {
    std::vector<ob::SelectionItem> items;
    for (std::size_t index = 0; index < count; ++index) items.push_back(item(groupIndex, firstLine + index, weightLb));
    return items;
}

} // namespace m4SyntheticDay
