#include "selectionReporter.hpp"
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

// Every column as wide as its widest entry, numbers to the right: nothing is ever cut short.
void printTable(ostream& out, const Row& header, const vector<Row>& rows, const vector<bool>& rightAligned) {
    vector<size_t> widths(header.size());
    for (size_t column = 0; column < header.size(); ++column) widths[column] = header[column].size();
    for (const Row& row : rows) {
        for (size_t column = 0; column < row.size(); ++column) widths[column] = max(widths[column], row[column].size());
    }
    auto printRow = [&](const Row& row) {
        out << " ";
        for (size_t column = 0; column < row.size(); ++column) {
            out << " ";
            if (rightAligned[column]) {
                pad_left(out, row[column], widths[column]);
            } else if (column + 1 == row.size()) {
                out << row[column];
            } else {
                pad_right(out, row[column], widths[column]);
            }
        }
        out << "\n";
    };
    printRow(header);
    Row dashes;
    for (const size_t width : widths) dashes.push_back(string(width, '-'));
    printRow(dashes);
    for (const Row& row : rows) printRow(row);
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
        case AttemptOutcome::OverBudget: text += " over budget (" + fixed(attempt.seconds, 1) + " s)"; break;
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

} // anonymous namespace

const vector<string>& selectionReportSections() {
    static const vector<string> sections{
        "1. Inputs",          "2. Parameters", "3. Fullness",       "4. Reservations not used",
        "5. Log of activities", "6. Trucks",   "7. Goods left over", "8. Item master"};
    return sections;
}

void printSelectionReport(ostream& out, const PipelineResult& run, const ReservationBook& book,
                          const SelectionItems& items, const LaneClassification& classification,
                          const SelectionPlan& plan, const SelectionReportContext& context) {
    if (plan.routes.size() != classification.routes.size()
        || plan.reservations.size() != book.reservations.size()) {
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

    // Which route, and so which lane-date, each routed item belongs to.
    vector<optional<size_t>> routeOfItem(items.items.size());
    for (size_t routeIndex = 0; routeIndex < classification.routes.size(); ++routeIndex) {
        for (const size_t index : classification.routes[routeIndex].itemIndices) routeOfItem[index] = routeIndex;
    }
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

    printSection(out, 0);
    for (const string& file : context.inputFiles) printLine(out, "file", printable(file));
    printLine(out, "plan date", printable(book.window.planDate));
    printLine(out, "ship dates", printable(book.window.firstShipDate) + " to " + printable(book.window.lastShipDate)
                                     + " (CTL row " + count(book.window.ctlRowIndex)
                                     + (book.window.fromDatedRow ? ", dated the plan date)" : ", the plan date's weekday)"));
    printLine(out, "items", count(items.items.size()) + " stacks, each one floor position");
    printLine(out, "routes", count(classification.routes.size()) + " group and ship-date routes");
    printLine(out, "reservations", count(book.reservations.size()) + " lane-dates, "
                                       + grouped(static_cast<double>(book.reservedTrucks)) + " trucks");

    printSection(out, 1);
    const SelectorSettings& settings = context.settings;
    printLine(out, "trailer", printable(trailer.trailerCode) + ", " + measure(trailer.interiorLengthIn) + " x "
                                  + measure(trailer.interiorWidthIn) + " x " + measure(trailer.stackHeightCeilingIn)
                                  + " in, " + grouped(trailer.weightLimitLb) + " lb");
    printLine(out, "full at", "100% of payload, or " + percent(settings.floor.fullCubeFraction) + " of cube");
    printLine(out, "units", "CS, DIS and PAL quantities divided by Cases_Unit_Load");
    printLine(out, "", "a part unit load takes a whole floor position");
    printLine(out, "routing", "M5 above " + to_string(context.routing.m5Threshold) + " trucks reserved; patternPacker above "
                                  + setting(context.routing.varietyRatio) + " unit loads per product");
    printLine(out, "knapsack", "grains " + setting(settings.knapsack.weightGrainLb) + " lb, "
                                   + setting(settings.knapsack.areaGrainSqIn) + " sq in; "
                                   + grouped(static_cast<double>(settings.knapsack.nodeLimit)) + " nodes");
    printLine(out, "patternPacker", "grains " + setting(settings.patternPacker.weightGrainLb) + " lb, "
                                        + setting(settings.patternPacker.areaGrainSqIn) + " sq in; "
                                        + to_string(settings.patternPacker.maxPatterns) + " patterns, "
                                        + grouped(static_cast<double>(settings.patternPacker.pricingNodeLimit))
                                        + " pricing nodes");
    printLine(out, "binPacker", "grains " + setting(settings.binPacker.weightGrainLb) + " lb, "
                                    + setting(settings.binPacker.areaGrainSqIn) + " sq in; "
                                    + to_string(settings.binPacker.columnRoundsPerNode) + " rounds a node, "
                                    + grouped(static_cast<double>(settings.binPacker.branchNodeLimit))
                                    + " branch nodes, at most " + to_string(settings.binPacker.maxSearchItems)
                                    + " items searched");
    printLine(out, "floor", string(settings.floor.allowRotation ? "pallets may be turned" : "pallets never turned")
                                + (settings.floor.maxPositions
                                       ? ", at most " + to_string(*settings.floor.maxPositions) + " positions"
                                       : ""));
    printLine(out, "budget", setting(settings.secondsPerLane) + " s a lane, " + to_string(settings.threads) + " threads");
    printLine(out, "detail", detailed ? "detailed" : "standard");

    printSection(out, 2);
    long long reserved = 0, escalated = 0;
    for (const ReservationUse& use : plan.reservations) {
        reserved += use.reserved;
        if (use.escalated) escalated += use.reserved;
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
    printLine(out, "unused (penalty exposure)", grouped(static_cast<double>(plan.penaltyExposureTrucks)) + " trucks");
    printLine(out, "unit loads shipped", grouped(unitLoads, 1));
    printLine(out, "average rear waste", fixed(plan.trucks.empty() ? 0.0 : rearWaste / built, 1) + " in");
    printLine(out, "average cube", percent(plan.trucks.empty() ? 0.0 : cube / built) + " (full at "
                                       + percent(settings.floor.fullCubeFraction) + ")");
    printLine(out, "full trucks", count(weightFull) + " by weight, " + count(cubeFull) + " by cube");
    printLine(out, "items", count(itemsOnTrucks) + " on trucks, " + count(plan.leftOver.size()) + " left over");

    printSection(out, 3);
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
    for (const ReservationUse& use : plan.reservations) {
        if (use.unused() <= 0) continue;
        const Reservation& reservation = book.reservations[use.reservationIndex];
        const LaneDate laneDate{reservation.origin, reservation.destination, reservation.shipCondition, reservation.shipDate};
        string reason;
        const auto left = leftByLaneDate.find(laneDate);
        if (!laneDatesWithDemand.count(laneDate)) {
            reason = "nothing to load";
        } else if (left == leftByLaneDate.end()) {
            reason = "nothing more to load";
        } else {
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
        printTable(out, {"lane", "ship date", "reserved", "built", "unused", "reason"}, unusedRows,
                   {false, false, true, true, true, false});
        out << "\n  Penalty exposure by lane:\n";
        vector<Row> laneRows;
        for (const auto& [lane, unused] : unusedByLane) {
            laneRows.push_back({laneText(get<0>(lane), get<1>(lane), get<2>(lane)), grouped(static_cast<double>(unused))});
        }
        printTable(out, {"lane", "unused trucks"}, laneRows, {false, true});
    }
    out << "\n  Penalty exposure for the day: " << grouped(static_cast<double>(plan.penaltyExposureTrucks))
        << " reserved trucks unused\n";

    printSection(out, 4);
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
        }
        const string proof = !outcome.methodUsed ? "-" : outcome.proven ? "proven" : "unproven";
        routeRows.push_back({laneText(key.locationFrom, key.locationTo, key.shipCondition),
                             groupText(groups[route.groupIndex]), printable(route.shipDate),
                             grouped(static_cast<double>(route.reservedTrucks)), statusName(outcome.status),
                             grouped(static_cast<double>(outcome.trucksBuilt)), proof, binding, attemptsText(outcome)});
    }
    printTable(out, {"lane", "group", "ship date", "reserved", "status", "trucks", "result", "binds on", "methods"},
               routeRows, {false, false, false, true, false, true, false, false, false});

    printSection(out, 5);
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
        printTable(out, {"truck", "lane", "group", "ship date", "stacks", "unit loads", "weight lb", "payload", "cube",
                         "full", "rear waste in", "rows", "floor patterns", "turned", "lone tops"},
                   truckRows,
                   {true, false, false, false, true, true, true, true, true, false, true, true, false, true, true});
    }

    printSection(out, 6);
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
        printTable(out, {"reason", "stacks", "unit loads"}, reasonRows, {false, true, true});
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
                                to_string(item.priority), grouped(item.unitLoads, 2), leftOverReasonName(left.reason)});
        }
        printTable(out, {"stack", "product", "lane", "ship date", "priority", "unit loads", "reason"}, itemRows,
                   {true, false, false, false, true, true, false});
    }

    printSection(out, 7);
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
        printTable(out, {"product", "pallet", "cases a unit load", "case lb", "case in"}, productRows,
                   {false, false, true, true, false});
    }
}

} // namespace ob
