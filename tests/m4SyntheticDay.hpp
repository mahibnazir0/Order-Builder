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
    settings.patternPacker = {1.0, 1.0, 200, 200'000};
    settings.binPacker = {1.0, 1.0, 50, 100'000, 2'000, 100};
    settings.floor.fullCubeFraction = 0.84;
    settings.secondsPerLane = 10.0;
    settings.threads = 1;
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
    std::sort(items.begin(), items.end(), ob::shipsBefore);
    day.items.items = std::move(items);
    day.classification = ob::classifyLanes(day.run, day.book, day.items, kRouting);
    return day;
}

inline ob::SelectionPlan plan(const Day& day, const ob::SelectorSettings& settings = selectorSettings()) {
    return ob::selectLoads(day.run, day.book, day.items, day.classification, settings);
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
