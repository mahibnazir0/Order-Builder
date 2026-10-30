#include "selectionItem.hpp"
#include "pipeline.hpp"
#include "isoDate.hpp"
#include "stackRules.hpp"
#include "tolerance.hpp"
#include "unitLoadMetrics.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <tuple>

using namespace std;

namespace ob {

namespace {

// What one unit load of a line contributes to an item, worked out once per line.
struct LineFigures {
    UnitLoadMetrics metrics;
    double weightPerUnitLoadLb = 0.0;
    double longSideIn = 0.0;
    double shortSideIn = 0.0;
};

LineFigures lineFigures(const JoinedLine& line, const M2Params& params) {
    LineFigures figures;
    figures.metrics = unitLoadMetricsFor(line, params);
    if (figures.metrics.error != UnitLoadMetricsError::None) return figures;
    if (figures.metrics.unitLoads > 0.0) {
        figures.weightPerUnitLoadLb = figures.metrics.weightLb / figures.metrics.unitLoads;
    }
    // unitLoadMetricsFor succeeded, so the pallet resolves and the footprint is valid.
    const optional<PalletSpec> pallet = resolvePalletSpec(*line.product, params);
    const UnitLoadFootprint footprint = unitLoadFootprint(*line.product, *pallet);
    figures.longSideIn = footprint.longSideIn;
    figures.shortSideIn = footprint.shortSideIn;
    return figures;
}

optional<ItemRejection> lineRejection(const STRRecord& line, const LineFigures& figures) {
    if (figures.metrics.error != UnitLoadMetricsError::None) return ItemRejection::InvalidUnitLoad;
    if (!isIsoDate(line.datfr_ta)) return ItemRejection::InvalidShipDate;
    if (!isIsoDate(line.datto_ta) || line.datto_ta < line.datfr_ta) {
        return ItemRejection::InvalidDeliveryDate;
    }
    if (!isIsoDate(line.avail_date)) return ItemRejection::InvalidAvailDate;
    // The Validator only warns on this, so M1 totals keep the line; here priority decides
    // what ships, and the unreadable sentinel must never pass as priority 0.
    if (line.tprio < 0 || line.tprio > kMaxPriority) return ItemRejection::UnreadablePriority;
    return nullopt;
}

auto selectionOrder(const SelectionItem& item) {
    return tie(item.shipDate, item.priority, item.productNumber, item.groupIndex, item.stackIndex,
               item.copyIndex);
}

} // anonymous namespace

SelectionItems buildSelectionItems(const PipelineResult& run, const PlanningWindow& window) {
    if (!run.ranMilestone2) {
        throw invalid_argument("Selection items need Milestone 2's stacks, and it did not run");
    }
    const vector<JoinedLine>& lines = run.join.lines;
    const vector<GroupStacking>& groups = run.stacking.groups;
    if (groups.size() != run.segregation.groups.size()) {
        throw invalid_argument("Stacking has " + to_string(groups.size()) + " groups and segregation "
                               + to_string(run.segregation.groups.size()));
    }

    const vector<bool> stacked = stackedLineFlags(run.stacking, lines.size());
    vector<optional<LineFigures>> figures(lines.size());
    vector<double> unallocated(lines.size(), 0.0);
    SelectionItems result;
    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        if (!stacked[lineIndex]) continue;
        figures[lineIndex] = lineFigures(lines[lineIndex], run.params);
        const UnitLoadMetrics& metrics = figures[lineIndex]->metrics;
        if (metrics.error != UnitLoadMetricsError::None) continue;
        unallocated[lineIndex] = metrics.unitLoads;
        result.unitLoadsIn += metrics.unitLoads;
        result.weightInLb += metrics.weightLb;
    }

    const TrailerSpec& trailer = run.trailer;
    for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        const vector<BuiltStack>& stacks = groups[groupIndex].best.stacks;
        for (size_t stackIndex = 0; stackIndex < stacks.size(); ++stackIndex) {
            const BuiltStack& stack = stacks[stackIndex];
            const size_t copies = stack.quantity > kQuantityEpsilon
                ? static_cast<size_t>(ceil(stack.quantity - kQuantityEpsilon)) : 0;
            for (size_t copyIndex = 0; copyIndex < copies; ++copyIndex) {
                SelectionItem item;
                item.groupIndex = groupIndex;
                item.stackIndex = stackIndex;
                item.copyIndex = copyIndex;
                optional<pair<size_t, ItemRejection>> rejection;
                for (const size_t lineIndex : stack.lineIndices) {
                    if (lineIndex >= lines.size() || !figures[lineIndex]) {
                        throw invalid_argument("Stack " + to_string(stackIndex) + " of group "
                                               + to_string(groupIndex)
                                               + " names a line that is not stacked: "
                                               + to_string(lineIndex));
                    }
                    const LineFigures& line = *figures[lineIndex];
                    const STRRecord& demand = *lines[lineIndex].str;
                    if (!rejection) {
                        if (const auto reason = lineRejection(demand, line)) {
                            rejection = make_pair(lineIndex, *reason);
                        }
                    }
                    if (line.metrics.error != UnitLoadMetricsError::None) {
                        item.lineIndices.push_back(lineIndex);
                        continue;
                    }
                    // Copies past the line's last unit load carry nothing of it.
                    const double share = min(1.0, unallocated[lineIndex]);
                    if (share <= kQuantityEpsilon) continue;
                    unallocated[lineIndex] -= share;

                    item.lineIndices.push_back(lineIndex);
                    item.unitLoads += share;
                    item.weightLb += share * line.weightPerUnitLoadLb;
                    item.heightIn += line.metrics.unitLoadHeightIn;
                    item.cubeCubicIn += share * line.longSideIn * line.shortSideIn
                                        * line.metrics.unitLoadHeightIn;
                    item.footprintLongIn = max(item.footprintLongIn, line.longSideIn);
                    item.footprintShortIn = max(item.footprintShortIn, line.shortSideIn);
                    if (item.lineIndices.size() == 1) {
                        item.shipDate = demand.datfr_ta;
                        item.availDate = demand.avail_date;
                        item.priority = demand.tprio;
                        item.productNumber = demand.matnr;
                    } else {
                        if (demand.datfr_ta != item.shipDate) item.mixesShipDates = true;
                        item.shipDate = min(item.shipDate, demand.datfr_ta);
                        item.availDate = max(item.availDate, demand.avail_date);
                        item.priority = min(item.priority, demand.tprio);
                    }
                }
                if (item.lineIndices.empty()) continue;

                result.unitLoadsOut += item.unitLoads;
                result.weightOutLb += item.weightLb;
                if (rejection) {
                    result.rejected.push_back({std::move(item), rejection->first, rejection->second});
                    continue;
                }

                if (item.shipDate < window.firstShipDate) {
                    item.timing = ShipTiming::Overdue;
                } else if (item.shipDate > window.lastShipDate) {
                    item.timing = ShipTiming::Future;
                }
                item.availableAfterShipDate = item.availDate > item.shipDate;
                item.unavailableInWindow = item.availDate > window.lastShipDate;
                if (item.mixesShipDates) ++result.itemsMixingShipDates;
                if (fabs(item.unitLoads - round(item.unitLoads)) > kQuantityEpsilon) {
                    ++result.partialItems;
                }
                result.items.push_back(std::move(item));
            }
        }
    }

    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        if (unallocated[lineIndex] > kQuantityEpsilon) result.linesNotFullyStacked.push_back(lineIndex);
    }

    sort(result.items.begin(), result.items.end(), shipsBefore);
    for (size_t itemIndex = 0; itemIndex < result.items.size(); ++itemIndex) {
        const SelectionItem& item = result.items[itemIndex];
        // Compared with a tolerance: an item exactly at a limit fits (E66, E71).
        if (item.weightLb > trailer.weightLimitLb + kQuantityEpsilon) {
            result.infeasible.push_back({itemIndex, ItemInfeasibility::OverPayload});
        } else if (item.heightIn > trailer.stackHeightCeilingIn + kQuantityEpsilon) {
            result.infeasible.push_back({itemIndex, ItemInfeasibility::OverCeiling});
        } else if (item.footprintLongIn > max(trailer.interiorLengthIn, trailer.interiorWidthIn)
                                              + kQuantityEpsilon
                   || item.footprintShortIn > min(trailer.interiorLengthIn, trailer.interiorWidthIn)
                                                  + kQuantityEpsilon) {
            result.infeasible.push_back({itemIndex, ItemInfeasibility::OverFloor});
        }
    }
    return result;
}

bool shipsBefore(const SelectionItem& left, const SelectionItem& right) {
    return selectionOrder(left) < selectionOrder(right);
}

const char* itemRejectionName(ItemRejection reason) {
    switch (reason) {
    case ItemRejection::InvalidShipDate: return "invalid ship date";
    case ItemRejection::InvalidDeliveryDate: return "invalid delivery date";
    case ItemRejection::InvalidAvailDate: return "invalid available date";
    case ItemRejection::UnreadablePriority: return "unreadable priority";
    case ItemRejection::InvalidUnitLoad: return "invalid unit load";
    }
    return "unknown";
}

const char* itemInfeasibilityName(ItemInfeasibility reason) {
    switch (reason) {
    case ItemInfeasibility::OverPayload: return "over payload";
    case ItemInfeasibility::OverCeiling: return "over ceiling";
    case ItemInfeasibility::OverFloor: return "over floor";
    }
    return "unknown";
}

} // namespace ob
