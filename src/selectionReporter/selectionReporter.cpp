#include "selectionReporter.hpp"
#include "isoDate.hpp"
#include "pipeline.hpp"
#include "reportFormat.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

using namespace std;

namespace ob {

namespace {

constexpr double kCubicInchesPerCubicFoot = 1728.0;

const char* const kRule = "----------------------------------------------------------------\n";

using Row = vector<string>;
using LaneDate = tuple<string, string, string, string>;  // origin, destination, condition, date

string count(size_t value) { return grouped(static_cast<double>(value)); }

string percent(double share) { return fixed(share * 100.0, 1) + "%"; }

string measure(double value) { return grouped(value, value == floor(value) ? 0 : 2); }

// A setting exactly as given: a 0.001 lb grain must not print as 0.00.
string setting(double value) {
    ostringstream text;
    text << value;
    return text.str();
}

string laneText(const string& origin, const string& destination, const string& condition) {
    return printable(origin) + " -> " + printable(destination) + " " + printable(condition);
}

string groupText(const SegregatedGroup& group) {
    return group.key.isSegregated ? "group " + printable(group.key.segregant) : "-";
}

void printLine(ostream& out, const string& label, const string& value) {
    out << "  ";
    pad_right(out, label, 30);
    out << value << "\n";
}

void printSection(ostream& out, size_t number) {
    out << "\n" << kRule << selectionReportSections()[number] << "\n" << kRule;
}

const char* statusName(RouteStatus status) {
    switch (status) {
    case RouteStatus::Solved: return "solved";
    case RouteStatus::Escalated: return "escalated to M5";
    case RouteStatus::Unrouted: return "no reservation";
    case RouteStatus::NoDemand: return "nothing to load";
    case RouteStatus::Unsolved: return "UNSOLVED";
    }
    return "unknown";
}

string attemptsText(const RouteOutcome& outcome) {
    if (outcome.attempts.empty()) return "no method called";
    string text;
    for (const MethodAttempt& attempt : outcome.attempts) {
        if (!text.empty()) text += "; ";
        text += packingMethodName(attempt.method);
        switch (attempt.outcome) {
        case AttemptOutcome::Used: text += " used"; break;
        case AttemptOutcome::OverBudget: text += " used, OVER BUDGET (" + fixed(attempt.seconds, 1) + " s)"; break;
        case AttemptOutcome::Threw: text += " threw: " + printable(attempt.error); break;
        }
    }
    return text;
}

struct RouteShares {
    double floorShare = 0.0;   // of the trucks' floor, averaged over them
    double weightShare = 0.0;  // of their payload
};

string patternsText(const FloorLayout& floor) {
    map<FloorPatternKind, size_t> rows;
    for (const FloorRow& row : floor.rows) ++rows[row.pattern];
    string text;
    for (const auto& [pattern, rowCount] : rows) {
        if (!text.empty()) text += ", ";
        text += string(floorPatternName(pattern)) + " " + count(rowCount);
    }
    return text.empty() ? "-" : text;
}

// The goods planned on a date not their own (shipDatePlanner): carried over from a date that could
// not take them, and pulled forward onto a truck ahead of their date.
void printDateMoves(ostream& out, const SelectionItems& items, const LaneClassification& classification,
                    const SelectionPlan& plan) {
    vector<string> truckDateOf(items.items.size());
    for (const PlannedTruck& truck : plan.trucks) {
        for (const size_t index : truck.items) truckDateOf[index] = truck.shipDate;
    }
    vector<string> firstOffered(items.items.size());
    for (const MovedItem& move : plan.moved) {
        string& first = firstOffered[move.itemIndex];
        if (!move.pulledForward && (first.empty() || classification.routes[move.routeIndex].shipDate < first)) {
            first = classification.routes[move.routeIndex].shipDate;
        }
    }
    size_t carried = 0, carriedLoaded = 0, pulled = 0;
    double carriedUnitLoads = 0.0, pulledUnitLoads = 0.0;
    long long mostDaysCarried = 0, mostDaysPulled = 0;
    for (size_t index = 0; index < items.items.size(); ++index) {
        const SelectionItem& item = items.items[index];
        const string& truckDate = truckDateOf[index];
        if (!firstOffered[index].empty()) {
            ++carried;
            carriedUnitLoads += item.unitLoads;
            if (!truckDate.empty()) {
                ++carriedLoaded;
                mostDaysCarried = max(mostDaysCarried, daysBetween(firstOffered[index], truckDate));
            }
        }
        if (!truckDate.empty() && truckDate < item.shipDate) {
            ++pulled;
            pulledUnitLoads += item.unitLoads;
            mostDaysPulled = max(mostDaysPulled, daysBetween(truckDate, item.shipDate) + item.daysPulledForward);
        }
    }
    printLine(out, "carried over", count(carried) + " stacks, " + grouped(carriedUnitLoads, 1)
                                       + " unit loads, their date's trucks could not take; " + count(carriedLoaded)
                                       + " loaded on a later date, up to " + to_string(mostDaysCarried) + " days on");
    printLine(out, "pulled forward", count(pulled) + " stacks, " + grouped(pulledUnitLoads, 1)
                                         + " unit loads loaded ahead of their date, up to " + to_string(mostDaysPulled)
                                         + " days");
}

} // anonymous namespace

const vector<string>& selectionReportSections() {
    static const vector<string> sections{
        "1. Inputs",          "2. Parameters", "3. How each lane was routed", "4. Fullness",
        "5. Reservations not used", "6. Log of activities", "7. Trucks", "8. One truck in full",
        "9. Goods left over", "10. Nothing lost", "11. Item master"};
    return sections;
}

void printSelectionReport(ostream& out, const PipelineResult& run, const ReservationBook& book,
                          const SelectionItems& items, const LaneClassification& classification,
                          const SelectionPlan& plan, const SelectionReportContext& context) {
    if (plan.routes.size() != classification.routes.size()
        || plan.reservations.size() != book.reservations.size()
        || any_of(plan.moved.begin(), plan.moved.end(), [&](const MovedItem& move) {
               return move.itemIndex >= items.items.size() || move.routeIndex >= classification.routes.size();
           })) {
        throw invalid_argument("Selection report: the plan was not made from this classification and book");
    }
    for (const ReservationUse& use : plan.reservations) {
        if (use.built > use.reserved) {
            const Reservation& reservation = book.reservations[use.reservationIndex];
            throw invalid_argument("Selection report: " + to_string(use.built) + " trucks built against "
                                   + to_string(use.reserved) + " reserved on "
                                   + laneText(reservation.origin, reservation.destination, reservation.shipCondition)
                                   + " for " + printable(reservation.shipDate) + ". This cannot happen; nothing is printed");
        }
    }
    const vector<SegregatedGroup>& groups = run.segregation.groups;
    const TrailerSpec& trailer = run.trailer;
    const double floorArea = trailer.interiorLengthIn * trailer.interiorWidthIn;
    const bool detailed = context.detail == ReportDetail::Detailed;

    // Which route, and so which lane-date, each routed item ended on.
    const vector<optional<size_t>> routeOfItem = finalRouteOfItems(classification, plan, items.items.size());
    auto laneDateOf = [&](size_t routeIndex) {
        const LaneRoute& route = classification.routes[routeIndex];
        const GroupKey& key = groups[route.groupIndex].key;
        return LaneDate{key.locationFrom, key.locationTo, key.shipCondition, route.shipDate};
    };
    map<size_t, RouteShares> sharesOfRoute;
    for (const PlannedTruck& truck : plan.trucks) {
        RouteShares& shares = sharesOfRoute[truck.routeIndex];
        shares.floorShare += truck.areaSqIn / floorArea;
        shares.weightShare += truck.weightLb / trailer.weightLimitLb;
    }
    for (auto& [routeIndex, shares] : sharesOfRoute) {
        const double trucks = static_cast<double>(max<long long>(1, plan.routes[routeIndex].trucksBuilt));
        shares.floorShare /= trucks;
        shares.weightShare /= trucks;
    }

    out << "M4 LOAD SELECTION - " << printable(context.extractLabel) << "\n";
    if (!context.shortfalls.empty()) {
        out << "  Result: INCOMPLETE (exit 7)\n";
        for (const string& shortfall : context.shortfalls) out << "    " << printable(shortfall) << "\n";
    } else if (!context.provisional.empty()) {
        out << "  Result: PROVISIONAL (exit 8)\n";
    } else {
        out << "  Result: complete\n";
    }
    if (!context.provisional.empty()) {
        out << "  Routes whose plan is not proven: " << count(context.provisional.size()) << "\n";
        for (const string& route : context.provisional) out << "    " << printable(route) << "\n";
    }

    printSection(out, 0);
    for (const string& file : context.inputFiles) printLine(out, "file", printable(file));
    printLine(out, "plan date", printable(book.window.planDate));
    printLine(out, "ship dates", printable(book.window.firstShipDate) + " to " + printable(book.window.lastShipDate)
                                     + " (CTL row " + count(book.window.ctlRowIndex)
                                     + (book.window.fromDatedRow ? ", dated the plan date)" : ", the plan date's weekday)"));
    printLine(out, "items", count(items.items.size()) + " stacks, each one floor position");
    printLine(out, "mixed ship dates", count(items.itemsMixingShipDates) + " stacks, "
                                           + grouped(items.unitLoadsMixingShipDates, 1)
                                           + " unit loads; later lines pulled forward up to "
                                           + to_string(items.mostDaysPulledForward) + " days");
    printLine(out, "routes", count(classification.routes.size()) + " group and ship-date routes");
    printDateMoves(out, items, classification, plan);
    printLine(out, "reservations", count(book.reservations.size()) + " lane-dates, "
                                       + grouped(static_cast<double>(book.reservedTrucks)) + " trucks");
    printLine(out, "stacks rejected", count(items.rejected.size()) + " for bad line data or pull-forward");
    if (!items.rejected.empty()) {
        vector<Row> rejectedRows;
        rejectedRows.reserve(items.rejected.size());
        for (const RejectedItem& rejected : items.rejected) {
            const STRRecord& line = *run.join.lines[rejected.lineIndex].str;
            string reason = itemRejectionName(rejected.reason);
            if (rejected.reason == ItemRejection::PullForwardBeyondLimit) {
                reason += ": " + to_string(rejected.item.daysPulledForward) + " days, " + printable(line.datfr_ta)
                          + " to " + printable(rejected.item.shipDate) + ", over " + to_string(context.maxDaysPulledForward)
                          + "; lines";
                for (const size_t lineIndex : rejected.item.lineIndices) {
                    reason += " " + printable(run.join.lines[lineIndex].str->idpr);
                }
            }
            rejectedRows.push_back({count(rejected.lineIndex), printable(line.matnr),
                                    laneText(line.locfrno, line.loctono, line.ship_cond), printable(line.datfr_ta),
                                    grouped(rejected.item.unitLoads, 2), reason});
        }
        printColumns(out, {"line", "product", "lane", "ship date", "unit loads", "reason"}, rejectedRows,
                     {true, false, false, false, true, false});
    }
    printLine(out, "placeholders skipped", count(book.skipped.size()) + " rows, "
                                               + grouped(book.skippedTrucks) + " trucks");
    if (!book.skipped.empty()) {
        const vector<PlaceholderRecord>& placeholders = run.placeholders.placeholders;
        vector<Row> skippedRows;
        skippedRows.reserve(book.skipped.size());
        for (const SkippedPlaceholder& skipped : book.skipped) {
            const PlaceholderRecord& row = placeholders[skipped.placeholderIndex];
            const bool readable = row.no_of_loads >= 0 && row.no_of_loads <= kMaxLoadsPerPlaceholder;
            skippedRows.push_back({count(skipped.placeholderIndex + 1), laneText(row.locfrno, row.loctono, row.ship_cond),
                                   printable(row.datfr_ta), readable ? grouped(row.no_of_loads) : "unreadable",
                                   describeSkipReason(skipped.reason)});
        }
        printColumns(out, {"row", "lane", "ship date", "trucks", "reason"}, skippedRows,
                     {true, false, false, true, false});
    }

    printSection(out, 1);
    const SelectorSettings& settings = context.settings;
    printLine(out, "trailer", printable(trailer.trailerCode) + ", " + measure(trailer.interiorLengthIn) + " x "
                                  + measure(trailer.interiorWidthIn) + " x " + measure(trailer.stackHeightCeilingIn)
                                  + " in, " + grouped(trailer.weightLimitLb) + " lb");
    printLine(out, "full at", "100% of payload, or " + percent(settings.floor.fullCubeFraction) + " of cube");
    printLine(out, "units", "CS, DIS and PAL quantities divided by Cases_Unit_Load");
    printLine(out, "", "a part unit load takes a whole floor position");
    printLine(out, "unit-load cap", "no truck over " + setting(settings.maxUnitLoadsPerTruck) + " unit loads");
    printLine(out, "routing", "M5 above " + to_string(context.routing.m5Threshold) + " trucks reserved; patternPacker above "
                                  + setting(context.routing.varietyRatio) + " unit loads per product");
    printLine(out, "knapsack", "grains " + setting(settings.knapsack.weightGrainLb) + " lb, "
                                   + setting(settings.knapsack.areaGrainSqIn) + " sq in; "
                                   + grouped(static_cast<double>(settings.knapsack.nodeLimit)) + " nodes");
    printLine(out, "patternPacker", "grains " + setting(settings.patternPacker.weightGrainLb) + " lb, "
                                        + setting(settings.patternPacker.areaGrainSqIn) + " sq in; "
                                        + to_string(settings.patternPacker.maxPatterns) + " patterns, "
                                        + grouped(static_cast<double>(settings.patternPacker.pricingNodeLimit))
                                        + " pricing nodes, " + grouped(settings.patternPacker.simplexIterationLimit)
                                        + " simplex pivots");
    printLine(out, "binPacker", "grains " + setting(settings.binPacker.weightGrainLb) + " lb, "
                                    + setting(settings.binPacker.areaGrainSqIn) + " sq in; "
                                    + to_string(settings.binPacker.columnRoundsPerNode) + " rounds a node, "
                                    + grouped(static_cast<double>(settings.binPacker.branchNodeLimit))
                                    + " branch nodes, at most " + to_string(settings.binPacker.maxSearchItems)
                                    + " items searched, " + grouped(settings.binPacker.simplexIterationLimit)
                                    + " simplex pivots");
    printLine(out, "floor", string(settings.floor.allowRotation ? "pallets may be turned" : "pallets never turned")
                                + (settings.floor.maxPositions
                                       ? ", at most " + to_string(*settings.floor.maxPositions) + " positions"
                                       : "")
                                + "; rows searched to " + grouped(settings.floor.searchStateLimit) + " groupings, "
                                + grouped(settings.floor.routeSearchStateLimit) + " a route");
    printLine(out, "budget", setting(settings.secondsPerLane) + " s a lane (overrun flags the lane, never changes the plan), "
                                 + to_string(settings.threads) + " threads");
    printLine(out, "fill and spread", to_string(settings.fillMissesPerTruck) + " misses a truck, "
                                          + to_string(settings.weightSpreadRoundsPerTruck) + " weight-spread rounds a truck");
    printLine(out, "detail", detailed ? "detailed" : "standard");

    out << "\n  PROVISIONAL BUSINESS RULES (not yet confirmed by the customer)\n";
    printLine(out, "priority direction", context.priorityZeroIsMostUrgent
                                             ? "TPRIO 0 ships first (priorityZeroIsMostUrgent = true)"
                                             : "TPRIO 11 ships first (priorityZeroIsMostUrgent = false)");
    printLine(out, "blank equipment", "taken as " + printable(context.defaultEquipment) + " on "
                                          + count(book.entriesWithDefaultEquipment) + " placeholder rows");
    printLine(out, "M5 threshold", "lanes above " + to_string(context.routing.m5Threshold)
                                       + " trucks reserved are left to M5, unplanned");
    printLine(out, "variety ratio", "patternPacker above " + setting(context.routing.varietyRatio)
                                        + " unit loads per product, binPacker at or below");
    printLine(out, "PGM/PTL footprint", run.params.footprintRule == FootprintRule::WrittenRule
                                            ? "larger of pallet and layer (footprintRule = WrittenRule; the file's"
                                              " Effective_Layer columns use the layer alone; Rev 8 question 4)"
                                            : "layer alone, as the file's Effective_Layer columns (footprintRule ="
                                              " LayerOnly; the written rule takes the larger of pallet and layer;"
                                              " Rev 8 question 4)");
    printLine(out, "PAL quantities", "divided by Cases_Unit_Load, as CS and DIS; whether a PAL is always one"
                                     " pallet is Rev 8 question 3");
    printLine(out, "mixed ship dates", context.shipDates.pullForward
                                           ? "a stack ships on its earliest line's date; its later lines go with it,"
                                             " pulled forward (M2 stacks by lane, within the pull-forward limit)"
                                           : "none: M2 stacks only lines due the same day (pullForward off)");
    printLine(out, "pull-forward limit", context.shipDates.pullForward
                                             ? "up to " + to_string(context.maxDaysPulledForward)
                                                   + " days (maxDaysPulledForward; customer-confirmed, Rev 8 section"
                                                     " 2.10); M2 never stacks lines further apart"
                                             : string("0 days: no line ships before its own date (pullForward off)"));
    const bool priorityFirst = context.routing.pastDueOrder == PastDueOrder::PriorityFirst;
    printLine(out, "past-due goods", priorityFirst
                                         ? "rank with the truck's own date's goods by priority, then oldest first"
                                           " (pastDueOrder = PriorityFirst)"
                                         : "ship ahead of the truck's own date's goods, oldest first, whatever their"
                                           " priority (pastDueOrder = AgeFirst)");
    printLine(out, "unshipped goods", context.shipDates.carryUnshippedForward
                                          ? "carried to the lane's next reserved date, ranked there as past due"
                                            " (carryUnshippedForward; customer-confirmed 28 Oct 2026)"
                                          : "left where their date's trucks cannot take them (carryUnshippedForward"
                                            " off)");
    printLine(out, "reserved trucks with room", context.shipDates.pullForward
                                                    ? "filled with goods due later, within the pull-forward limit"
                                                      " (pullForward; customer-confirmed 16 Oct 2026)"
                                                    : "left with room: no line ships before its own date"
                                                      " (pullForward off)");
    printLine(out, "knapsack objective", string("most unit loads of the most urgent class (")
                                             + (priorityFirst ? "priority, then ship date, among goods due by the"
                                                                " truck's date" : "ship date, then priority")
                                             + "), then the next; then most floor covered, on rows that fit and"
                                               " within the unit-load cap");

    printSection(out, 2);
    // Rev 8 section A: each lane-date with demand and trucks reserved, judged as a whole.
    struct LaneDateRouting {
        long long reserved = 0;
        long long demandTrucks = 0;
        set<string> products;
    };
    map<LaneDate, LaneDateRouting> routingByLaneDate;
    map<SolveMethod, size_t> routesByMethod;
    for (size_t routeIndex = 0; routeIndex < classification.routes.size(); ++routeIndex) {
        const LaneRoute& route = classification.routes[routeIndex];
        ++routesByMethod[route.method];
        if (route.itemIndices.empty() || route.reservedTrucks == 0) continue;
        LaneDateRouting& laneDate = routingByLaneDate[laneDateOf(routeIndex)];
        laneDate.reserved = route.reservedTrucks;
        laneDate.demandTrucks += route.demandTrucks;
        for (const size_t index : route.itemIndices) {
            for (const size_t lineIndex : items.items[index].lineIndices) {
                laneDate.products.insert(run.join.lines[lineIndex].str->matnr);
            }
        }
    }
    size_t demandFills = 0, demandShort = 0, demandExceeds = 0;
    size_t oneTruck = 0, packed = 0, toM5 = 0, fewProducts = 0, someProducts = 0, manyProducts = 0;
    long long reservedWithDemand = 0;
    for (const auto& [laneDate, routing] : routingByLaneDate) {
        reservedWithDemand += routing.reserved;
        if (routing.demandTrucks == routing.reserved) {
            ++demandFills;
        } else if (routing.demandTrucks < routing.reserved) {
            ++demandShort;
        } else {
            ++demandExceeds;
        }
        if (routing.reserved == 1) {
            ++oneTruck;
        } else if (routing.reserved > context.routing.m5Threshold) {
            ++toM5;
        } else {
            ++packed;
            const size_t products = routing.products.size();
            if (products <= 3) {
                ++fewProducts;
            } else if (products <= 10) {
                ++someProducts;
            } else {
                ++manyProducts;
            }
        }
    }
    const string threshold = to_string(context.routing.m5Threshold);
    printLine(out, "lane-dates", count(routingByLaneDate.size()) + " with demand and trucks reserved, "
                                     + grouped(static_cast<double>(reservedWithDemand)) + " trucks");
    printLine(out, "demand fills them", count(demandFills));
    printLine(out, "demand falls short", count(demandShort));
    printLine(out, "demand exceeds them", count(demandExceeds));
    printLine(out, "one truck reserved", count(oneTruck) + ", solved exactly");
    printLine(out, "2 to " + threshold + " reserved", count(packed) + ", packed");
    printLine(out, "more than " + threshold + " reserved", count(toM5) + ", sent to M5");
    printLine(out, "packed, by products", count(fewProducts) + " with 1 to 3, " + count(someProducts) + " with 4 to 10, "
                                              + count(manyProducts) + " with 11 or more");
    string methods;
    for (const auto& [method, routes] : routesByMethod) {
        methods += string(methods.empty() ? "" : ", ") + solveMethodName(method) + " " + count(routes);
    }
    printLine(out, "group routes by method", methods);

    printSection(out, 3);
    long long reserved = 0, escalated = 0, escalatedUnused = 0;
    for (const ReservationUse& use : plan.reservations) {
        reserved += use.reserved;
        if (use.escalated) {
            escalated += use.reserved;
            escalatedUnused += use.unused();
        }
    }
    double unitLoads = 0.0, rearWaste = 0.0, cube = 0.0;
    size_t weightFull = 0, cubeFull = 0, itemsOnTrucks = 0;
    for (const PlannedTruck& truck : plan.trucks) {
        unitLoads += truck.unitLoads;
        rearWaste += truck.floor.rearWasteIn;
        cube += truck.floor.cubeFraction;
        weightFull += truck.weightFull ? 1 : 0;
        cubeFull += truck.floor.cubeFull ? 1 : 0;
        itemsOnTrucks += truck.items.size();
    }
    const double built = static_cast<double>(max<size_t>(1, plan.trucks.size()));
    printLine(out, "trucks built", count(plan.trucks.size()) + " of " + grouped(static_cast<double>(reserved)) + " reserved");
    printLine(out, "left to M5", grouped(static_cast<double>(escalated)) + " trucks reserved");
    printLine(out, "unused (penalty exposure)", grouped(static_cast<double>(plan.penaltyExposureTrucks))
                                                    + " trucks, " + grouped(static_cast<double>(escalatedUnused))
                                                    + " of them unfilled, escalated to M5");
    printLine(out, "unit loads shipped", grouped(unitLoads, 1));
    printLine(out, "average rear waste", fixed(plan.trucks.empty() ? 0.0 : rearWaste / built, 1) + " in");
    const double trailerCubicFeet = floorArea * trailer.stackHeightCeilingIn / kCubicInchesPerCubicFoot;
    const double averageCube = plan.trucks.empty() ? 0.0 : cube / built;
    printLine(out, "average cube", percent(averageCube) + ", " + grouped(averageCube * trailerCubicFeet) + " cu ft (full at "
                                       + percent(settings.floor.fullCubeFraction) + ")");
    printLine(out, "full trucks", count(weightFull) + " by weight, " + count(cubeFull) + " by cube");
    printLine(out, "items", count(itemsOnTrucks) + " on trucks, " + count(plan.leftOver.size()) + " left over");
    size_t filledEvery = 0, oneShort = 0, twoOrMoreShort = 0;
    for (const ReservationUse& use : plan.reservations) {
        const Reservation& reservation = book.reservations[use.reservationIndex];
        if (use.escalated || !routingByLaneDate.count({reservation.origin, reservation.destination,
                                                      reservation.shipCondition, reservation.shipDate})) {
            continue;
        }
        if (use.unused() == 0) {
            ++filledEvery;
        } else if (use.unused() == 1) {
            ++oneShort;
        } else {
            ++twoOrMoreShort;
        }
    }
    printLine(out, "lane-dates with demand", count(filledEvery) + " filled every reservation, " + count(oneShort)
                                                 + " one short, " + count(twoOrMoreShort) + " two or more short");
    printLine(out, "your current system", "not an input to this run; the M4 benchmark sets it beside ours");

    printSection(out, 4);
    map<LaneDate, map<LeftOverReason, size_t>> leftByLaneDate;
    for (const LeftOverItem& left : plan.leftOver) {
        if (routeOfItem[left.itemIndex]) ++leftByLaneDate[laneDateOf(*routeOfItem[left.itemIndex])][left.reason];
    }
    set<LaneDate> laneDatesWithDemand;
    for (size_t routeIndex = 0; routeIndex < classification.routes.size(); ++routeIndex) {
        if (!classification.routes[routeIndex].itemIndices.empty()) laneDatesWithDemand.insert(laneDateOf(routeIndex));
    }
    vector<Row> unusedRows;
    map<tuple<string, string, string>, long long> unusedByLane;
    long long unusedNothingToLoad = 0, unusedBlocked = 0;
    for (const ReservationUse& use : plan.reservations) {
        if (use.unused() <= 0) continue;
        const Reservation& reservation = book.reservations[use.reservationIndex];
        const LaneDate laneDate{reservation.origin, reservation.destination, reservation.shipCondition, reservation.shipDate};
        string reason;
        const auto left = leftByLaneDate.find(laneDate);
        if (use.escalated) {
            reason = "unfilled, escalated to M5";
        } else if (!laneDatesWithDemand.count(laneDate)) {
            reason = "unfilled, nothing to load";
            unusedNothingToLoad += use.unused();
        } else if (left == leftByLaneDate.end()) {
            reason = "nothing more to load";
            unusedNothingToLoad += use.unused();
        } else {
            unusedBlocked += use.unused();
            reason = "blocked: goods left over (";
            bool first = true;
            for (const auto& [why, stacks] : left->second) {
                reason += string(first ? "" : ", ") + leftOverReasonName(why) + " " + count(stacks);
                first = false;
            }
            reason += ")";
        }
        unusedByLane[{reservation.origin, reservation.destination, reservation.shipCondition}] += use.unused();
        unusedRows.push_back({laneText(reservation.origin, reservation.destination, reservation.shipCondition),
                              printable(reservation.shipDate), grouped(static_cast<double>(use.reserved)),
                              grouped(static_cast<double>(use.built)), grouped(static_cast<double>(use.unused())), reason});
    }
    if (unusedRows.empty()) {
        out << "  Every reservation was used.\n";
    } else {
        printColumns(out, {"lane", "ship date", "reserved", "built", "unused", "reason"}, unusedRows,
                   {false, false, true, true, true, false});
        out << "\n  Penalty exposure by lane:\n";
        vector<Row> laneRows;
        for (const auto& [lane, unused] : unusedByLane) {
            laneRows.push_back({laneText(get<0>(lane), get<1>(lane), get<2>(lane)), grouped(static_cast<double>(unused))});
        }
        printColumns(out, {"lane", "unused trucks"}, laneRows, {false, true});
    }
    out << "\n  Penalty exposure for the day: " << grouped(static_cast<double>(plan.penaltyExposureTrucks))
        << " reserved trucks unused, " << grouped(static_cast<double>(escalatedUnused))
        << " of them unfilled, escalated to M5\n";

    const ReservationCoverage& coverage = run.reservationCoverage;
    out << "\n  Lanes reserved with no demand: " << count(coverage.reservationsWithoutDemand.size()) << " lane-dates, "
        << grouped(coverage.penaltyExposureTrucks) << " trucks\n";
    if (!coverage.reservationsWithoutDemand.empty()) {
        vector<Row> rows;
        rows.reserve(coverage.reservationsWithoutDemand.size());
        for (const size_t index : coverage.reservationsWithoutDemand) {
            const Reservation& reservation = book.reservations[index];
            rows.push_back({laneText(reservation.origin, reservation.destination, reservation.shipCondition),
                            printable(reservation.shipDate), grouped(reservation.trucks)});
        }
        printColumns(out, {"lane", "ship date", "trucks"}, rows, {false, false, true});
    }
    out << "\n  Demand lanes with no reservation: " << count(coverage.demandLanesWithoutReservation.size()) << "\n";
    if (!coverage.demandLanesWithoutReservation.empty()) {
        vector<Row> rows;
        rows.reserve(coverage.demandLanesWithoutReservation.size());
        for (const DemandLane& lane : coverage.demandLanesWithoutReservation) {
            rows.push_back({laneText(lane.origin, lane.destination, lane.shipCondition)});
        }
        printColumns(out, {"lane"}, rows, {false});
    }

    printSection(out, 5);
    vector<Row> routeRows;
    for (size_t routeIndex = 0; routeIndex < classification.routes.size(); ++routeIndex) {
        const LaneRoute& route = classification.routes[routeIndex];
        const RouteOutcome& outcome = plan.routes[routeIndex];
        const GroupKey& key = groups[route.groupIndex].key;
        string binding = "-";
        if (outcome.trucksBuilt > 0) {
            const RouteShares& shares = sharesOfRoute[routeIndex];
            binding = string(outcome.binding == BindingLimit::Weight ? "weight" : "floor") + " ("
                      + percent(shares.weightShare) + " payload, " + percent(shares.floorShare) + " floor)";
            if (outcome.bindingChanged) binding += ", trucks bind on the other";
        }
        const string proof = !outcome.methodUsed           ? "-"
                             : outcome.lpIterationCapHit ? "LP CAP HIT"
                             : outcome.proven            ? "proven"
                                                         : "unproven";
        routeRows.push_back({laneText(key.locationFrom, key.locationTo, key.shipCondition),
                             groupText(groups[route.groupIndex]), printable(route.shipDate),
                             grouped(static_cast<double>(route.reservedTrucks)), statusName(outcome.status),
                             grouped(static_cast<double>(outcome.trucksBuilt)), proof, binding, attemptsText(outcome)});
    }
    printColumns(out, {"lane", "group", "ship date", "reserved", "status", "trucks", "result", "binds on", "methods"},
               routeRows, {false, false, false, true, false, true, false, false, false});

    printSection(out, 6);
    vector<Row> truckRows;
    for (size_t truckIndex = 0; truckIndex < plan.trucks.size(); ++truckIndex) {
        const PlannedTruck& truck = plan.trucks[truckIndex];
        const GroupKey& key = groups[truck.groupIndex].key;
        const string full = truck.weightFull && truck.floor.cubeFull ? "weight, cube"
                            : truck.weightFull                       ? "weight"
                            : truck.floor.cubeFull                   ? "cube"
                                                                     : "-";
        truckRows.push_back({count(truckIndex + 1), laneText(key.locationFrom, key.locationTo, key.shipCondition),
                             groupText(groups[truck.groupIndex]), printable(truck.shipDate), count(truck.items.size()),
                             grouped(truck.unitLoads, 1), grouped(truck.weightLb), percent(truck.weightLb / trailer.weightLimitLb),
                             percent(truck.floor.cubeFraction), full, fixed(truck.floor.rearWasteIn, 1),
                             count(truck.floor.rows.size()), patternsText(truck.floor), count(static_cast<size_t>(truck.floor.turnedItems)),
                             count(truck.floor.loneTopPalletRows)});
    }
    if (truckRows.empty()) {
        out << "  No truck was built.\n";
    } else {
        printColumns(out, {"truck", "lane", "group", "ship date", "stacks", "unit loads", "weight lb", "payload", "cube",
                         "full", "rear waste in", "rows", "floor patterns", "turned", "lone tops"},
                   truckRows,
                   {true, false, false, false, true, true, true, true, true, false, true, true, false, true, true});
    }

    printSection(out, 7);
    optional<size_t> shownRoute;
    for (size_t routeIndex = 0; routeIndex < plan.routes.size(); ++routeIndex) {
        if (plan.routes[routeIndex].trucksBuilt > 0
            && (!shownRoute || plan.routes[routeIndex].trucksBuilt > plan.routes[*shownRoute].trucksBuilt)) {
            shownRoute = routeIndex;
        }
    }
    if (!shownRoute) {
        out << "  No truck was built.\n";
    } else {
        const PlannedTruck& truck = *find_if(plan.trucks.begin(), plan.trucks.end(), [&](const PlannedTruck& planned) {
            return planned.routeIndex == *shownRoute;
        });
        const GroupKey& key = groups[truck.groupIndex].key;
        double tallestIn = 0.0;
        for (const size_t index : truck.items) tallestIn = max(tallestIn, items.items[index].heightIn);
        printLine(out, "truck", laneText(key.locationFrom, key.locationTo, key.shipCondition) + " "
                                    + groupText(groups[truck.groupIndex]) + ", " + printable(truck.shipDate) + ", 1 of "
                                    + grouped(static_cast<double>(plan.routes[*shownRoute].trucksBuilt))
                                    + " (the route with the most trucks)");
        printLine(out, "unit loads on board", grouped(truck.unitLoads, 1));
        printLine(out, "floor space used", grouped(truck.areaSqIn) + " of " + grouped(floorArea) + " sq in ("
                                               + percent(truck.areaSqIn / floorArea) + ")");
        printLine(out, "height used", measure(tallestIn) + " of " + measure(trailer.stackHeightCeilingIn) + " in ("
                                          + percent(tallestIn / trailer.stackHeightCeilingIn) + ")");
        printLine(out, "weight", grouped(truck.weightLb) + " of " + grouped(trailer.weightLimitLb) + " lb ("
                                     + percent(truck.weightLb / trailer.weightLimitLb) + ")");
        printLine(out, "unused at the rear", fixed(truck.floor.rearWasteIn, 1) + " in");
        printLine(out, "floor pattern", patternsText(truck.floor));
        printLine(out, "pallets turned", count(static_cast<size_t>(truck.floor.turnedItems)));
        out << "\n";
        vector<Row> stackRows;
        stackRows.reserve(truck.items.size());
        for (size_t position = 0; position < truck.items.size(); ++position) {
            const SelectionItem& item = items.items[truck.items[position]];
            const string& bottom = item.productNumber.empty() ? run.join.lines[item.lineIndices.front()].str->matnr
                                                               : item.productNumber;
            stackRows.push_back({count(position + 1), count(item.lineIndices.size()), measure(item.heightIn),
                                 printable(bottom), grouped(item.unitLoads, 2)});
        }
        printColumns(out, {"stack", "high", "height in", "bottom product", "unit loads"}, stackRows,
                     {true, true, true, false, true});
    }

    printSection(out, 8);
    map<LeftOverReason, pair<size_t, double>> leftByReason;
    for (const LeftOverItem& left : plan.leftOver) {
        auto& [stacks, loads] = leftByReason[left.reason];
        ++stacks;
        loads += items.items[left.itemIndex].unitLoads;
    }
    if (leftByReason.empty()) {
        out << "  Nothing was left over.\n";
    } else {
        vector<Row> reasonRows;
        for (const auto& [reason, totals] : leftByReason) {
            reasonRows.push_back({leftOverReasonName(reason), count(totals.first), grouped(totals.second, 1)});
        }
        printColumns(out, {"reason", "stacks", "unit loads"}, reasonRows, {false, true, true});
    }
    if (detailed && !plan.leftOver.empty()) {
        out << "\n";
        vector<Row> itemRows;
        for (const LeftOverItem& left : plan.leftOver) {
            const SelectionItem& item = items.items[left.itemIndex];
            const GroupKey& key = groups[item.groupIndex].key;
            itemRows.push_back({count(left.itemIndex), printable(item.productNumber.empty()
                                                                     ? run.join.lines[item.lineIndices.front()].str->matnr
                                                                     : item.productNumber),
                                laneText(key.locationFrom, key.locationTo, key.shipCondition), printable(item.shipDate),
                                to_string(item.tprio), grouped(item.unitLoads, 2), leftOverReasonName(left.reason)});
        }
        printColumns(out, {"stack", "product", "lane", "ship date", "priority", "unit loads", "reason"}, itemRows,
                   {true, false, false, false, true, true, false});
    }

    printSection(out, 9);
    // Rev 8 section E: every unit load in comes out on a truck, held back or not loadable.
    double onTrucks = 0.0, heldBack = 0.0, notLoadable = 0.0;
    for (const PlannedTruck& truck : plan.trucks) onTrucks += truck.unitLoads;
    for (const LeftOverItem& left : plan.leftOver) {
        const double unitLoadsLeft = items.items[left.itemIndex].unitLoads;
        switch (left.reason) {
        case LeftOverReason::AfterWindow:
        case LeftOverReason::UnavailableInWindow:
        case LeftOverReason::NoReservation:
        case LeftOverReason::OverReservation:
        case LeftOverReason::FloorEviction:
        case LeftOverReason::UnitLoadCap: heldBack += unitLoadsLeft; break;
        case LeftOverReason::EscalatedToM5:
        case LeftOverReason::Unfittable:
        case LeftOverReason::LaneUnsolved: notLoadable += unitLoadsLeft; break;
        }
    }
    for (const RejectedItem& rejected : items.rejected) notLoadable += rejected.item.unitLoads;
    for (const UnstackedLine& line : items.linesNotFullyStacked) notLoadable += line.unitLoads;
    const double accounted = onTrucks + heldBack + notLoadable;
    const bool balanced = fabs(accounted - items.unitLoadsIn) <= kQuantityEpsilon * max(1.0, items.unitLoadsIn);
    printLine(out, "unit loads in", grouped(items.unitLoadsIn, 1));
    printLine(out, "put on a truck", grouped(onTrucks, 1));
    printLine(out, "held back, needed later", grouped(heldBack, 1));
    printLine(out, "could not be loaded", grouped(notLoadable, 1) + " (escalated, unfittable, unsolved, rejected)");
    printLine(out, "accounted for", grouped(accounted, 1) + (balanced ? " OK" : " MISMATCH"));
    out << "\n";
    printLine(out, "reservations not filled", grouped(static_cast<double>(plan.penaltyExposureTrucks)));
    printLine(out, "  nothing left to load", grouped(static_cast<double>(unusedNothingToLoad)));
    printLine(out, "  blocked, needs a planner", grouped(static_cast<double>(unusedBlocked)));
    printLine(out, "  left to M5", grouped(static_cast<double>(escalatedUnused)));
    printLine(out, "penalty exposure", grouped(static_cast<double>(plan.penaltyExposureTrucks)) + " unused reservations");

    printSection(out, 10);
    map<string, const ProductRecord*> products;
    for (const PlannedTruck& truck : plan.trucks) {
        for (const size_t index : truck.items) {
            for (const size_t lineIndex : items.items[index].lineIndices) {
                const JoinedLine& line = run.join.lines[lineIndex];
                products.emplace(line.str->matnr, line.product);
            }
        }
    }
    printLine(out, "products on the trucks", count(products.size()));
    if (detailed && !products.empty()) {
        out << "\n";
        vector<Row> productRows;
        for (const auto& [matnr, product] : products) {
            if (!product) {
                productRows.push_back({printable(matnr), "-", "-", "-", "-"});
                continue;
            }
            productRows.push_back({printable(matnr), printable(product->pallet_id), to_string(product->cases_unit_load),
                                   measure(product->weight_lb),
                                   measure(product->length_in) + " x " + measure(product->width_in) + " x "
                                       + measure(product->height_in)});
        }
        printColumns(out, {"product", "pallet", "cases a unit load", "case lb", "case in"}, productRows,
                   {false, false, true, true, false});
    }
}

} // namespace ob
