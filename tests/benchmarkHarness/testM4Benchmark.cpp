#include "doctest.h"
#include "../acceptance/acceptanceHarness.hpp"
#include "../m4AugustFixtures.hpp"
#include "../m4SyntheticDay.hpp"
#include "benchmarkHarness.hpp"
#include "converter.hpp"
#include "floorPattern.hpp"
#include "isoDate.hpp"
#include "stackRules.hpp"
#include "reportFormat.hpp"
#include "selectionParams.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;
using namespace ob;

namespace {

// The spec's measure of a weight-heavy lane: a truck over 40,000 lb (E210).
constexpr double kHeavyTruckLb = 40000.0;

// What the client has said of his own loads. Stated figures, not data: they are printed beside
// ours for the reader and never feed a decision.
constexpr long long kClientStatedUnusedAugust = 195;
constexpr double kClientStatedRearWasteIn = 14.5;
constexpr double kClientStatedCubicFeetLow = 3300.0;
constexpr double kClientStatedCubicFeetHigh = 3400.0;

SelectorSettings realDataSettings() {
    SelectorSettings settings = m4SyntheticDay::selectorSettings();
    settings.knapsack = {0.001, 0.001, 200'000};
    settings.patternPacker = {0.001, 0.001, 200, 200'000, 100'000};
    settings.binPacker = {0.001, 0.001, 50, 100'000, 2'000, 100, 100'000};
    settings.secondsPerLane = 10.0;
    return settings;
}

// One extract as M4 sees it: its run, reservation book, items, routes and plan.
struct HeldDay {
    string label;
    const PipelineResult* run = nullptr;
    ReservationBook book;
    SelectionItems items;
    LaneClassification classification;
    SelectionPlan plan;
};

HeldDay heldDay(const string& label, const PipelineResult& run) {
    HeldDay day;
    day.label = label;
    day.run = &run;
    day.book = buildReservationBook(run.placeholders.placeholders, run.demand.ctl,
                                    planDateFromRequestId(run.demand.request_id), {"53F", {"53F"}});
    day.items = buildSelectionItems(run, day.book.window, true, m4AugustTests::kShippedMaxDaysPulledForward);
    ShipDatePlan planned = planShipDates(run, day.book, day.items, m4AugustTests::kShippedRouting, realDataSettings(),
                                         m4AugustTests::kShippedShipDateRules,
                                         m4AugustTests::kShippedMaxDaysPulledForward);
    day.classification = std::move(planned.classification);
    day.plan = std::move(planned.plan);
    return day;
}

// Truck Builder's loads for one August run, read once.
const acceptanceTests::AchievedRequest& augustClientLoads(size_t run) {
    static const array<acceptanceTests::AchievedRequest, 3> loads = [] {
        array<acceptanceTests::AchievedRequest, 3> read;
        for (size_t index = 0; index < read.size(); ++index) {
            read[index] = acceptanceTests::readAchievedLoads(m4AugustTests::kAugustSolutionDirectories[index],
                                                             m4AugustTests::augustItems(index).run.demand.request_id);
        }
        return read;
    }();
    return loads[run];
}

// Every extract on this machine: the 4-6 August runs first, then the 17 Aug and early September
// days, then 29 Sep - 5 Oct. Built once. The client's August trucks go through the plausibility
// gate before anything is planned, so a conversion error stops the benchmark rather than being
// compared against.
const vector<HeldDay>& heldDays() {
    static const vector<HeldDay> days = [] {
        vector<HeldDay> held;
        if (m4AugustTests::augustPresent()) {
            const array<string, 3> labels{"Aug4", "Aug5", "Aug6"};
            for (size_t run = 0; run < labels.size(); ++run) {
                const PipelineResult& pipeline = m4AugustTests::augustItems(run).run;
                if (m4AugustTests::augustSolutionsPresent()) {
                    requirePlausibleTrucks(augustClientLoads(run).trucks, pipeline.index, realDataSettings().maxUnitLoadsPerTruck);
                }
                held.push_back(heldDay(labels[run], pipeline));
            }
        }
        if (crossDayTests::allExtractsPresent()) {
            for (size_t day = 0; day < crossDayTests::kDayCount; ++day) {
                held.push_back(heldDay(crossDayTests::dayFiles()[day].label, crossDayTests::pipelineRuns()[day]));
            }
        }
        for (size_t day = 0; day < crossDayTests::newExtracts::kDayCount; ++day) {
            if (crossDayTests::newExtracts::dayPresent(day)) {
                held.push_back(heldDay(crossDayTests::newExtracts::labels[day], *crossDayTests::newExtracts::run(day)));
            }
        }
        return held;
    }();
    return days;
}

// The August run at `run` (0 for 4 Aug). heldDays() holds only the extracts present, so without
// August its first days are September's: check the label rather than trust the position.
const HeldDay& augustDay(size_t run) {
    REQUIRE(m4AugustTests::augustPresent());
    const array<string, 3> labels{"Aug4", "Aug5", "Aug6"};
    REQUIRE(run < labels.size());
    REQUIRE(heldDays().size() > run);
    const HeldDay& day = heldDays()[run];
    REQUIRE(day.label == labels[run]);
    return day;
}

// For each August run, its routes sent to patternPacker or binPacker, each benchmarked with every
// method. Those are where the variety ratio decides; a knapsack route's demand runs far past its
// reservation, and packing all of it with every method says nothing about the crossover.
const array<vector<RouteBenchmark>, 3>& augustRouteBenchmarks() {
    static const array<vector<RouteBenchmark>, 3> benchmarks = [] {
        array<vector<RouteBenchmark>, 3> byRun;
        for (size_t run = 0; run < byRun.size(); ++run) {
            const HeldDay& day = augustDay(run);
            for (size_t route = 0; route < day.classification.routes.size(); ++route) {
                const SolveMethod method = day.classification.routes[route].method;
                if (method != SolveMethod::PatternPacker && method != SolveMethod::BinPacker) continue;
                byRun[run].push_back(benchmarkRoute(*day.run, day.items, day.classification, route, realDataSettings()));
            }
        }
        return byRun;
    }();
    return benchmarks;
}

vector<RouteBenchmark> allAugustRouteBenchmarks() {
    vector<RouteBenchmark> all;
    for (const vector<RouteBenchmark>& run : augustRouteBenchmarks()) all.insert(all.end(), run.begin(), run.end());
    return all;
}

// Truck Builder's loads for one August run, by lane and ship date.
struct ClientDay {
    acceptanceTests::AchievedRequest achieved;
    vector<ClientLaneDay> laneDays;
    size_t shippedLines = 0;
    size_t shippedLinesInDemand = 0;  // the share his loads drew from this run's demand
};

// The master row a line of the client's load was loaded in: its product in its unit.
const ProductRecord& clientLineProduct(const InputTruck& truck, const InputTruckLine& line, const PipelineResult& pipeline) {
    const vector<ProductRecord>& rows = pipeline.index.at(line.matnr);
    const auto product = find_if(rows.begin(), rows.end(),
                                 [&](const ProductRecord& row) { return row.uom == line.unitOfMeasure; });
    if (product == rows.end()) throw runtime_error("client load " + truck.id + ": no " + line.unitOfMeasure + " row for " + line.matnr);
    return *product;
}

double clientLineUnitLoads(const InputTruck& truck, const InputTruckLine& line, const PipelineResult& pipeline) {
    return Converter::to_pallets(line.quantity, line.unitOfMeasure, clientLineProduct(truck, line, pipeline));
}

// What one of the client's loads carries: each line's unit loads times its unit load's footprint
// and height, from the product master exactly as our items are measured.
double clientCargoCubicIn(const InputTruck& truck, const PipelineResult& pipeline) {
    double cubicIn = 0.0;
    for (const InputTruckLine& line : truck.lines) {
        const ProductRecord& product = clientLineProduct(truck, line, pipeline);
        const optional<PalletSpec> pallet = resolvePalletSpec(product, pipeline.params);
        if (!pallet) throw runtime_error("client load " + truck.id + ": no pallet for " + line.matnr);
        const UnitLoadFootprint footprint = unitLoadFootprint(product, *pallet, pipeline.params.footprintRule);
        cubicIn += Converter::to_pallets(line.quantity, line.unitOfMeasure, product) * footprint.longSideIn
                   * footprint.shortSideIn * unitLoadHeightIn(product, *pallet, pipeline.params);
    }
    return cubicIn;
}

const ClientDay& clientDay(size_t run) {
    REQUIRE(m4AugustTests::augustSolutionsPresent());
    static const array<ClientDay, 3> days = [] {
        array<ClientDay, 3> loaded;
        for (size_t index = 0; index < loaded.size(); ++index) {
            const PipelineResult& pipeline = *augustDay(index).run;
            ClientDay& day = loaded[index];
            day.achieved = augustClientLoads(index);
            for (const auto& laneDay : day.achieved.laneDays) {
                double cargoCubicIn = 0.0;
                for (const size_t truck : laneDay.truckIndices) {
                    cargoCubicIn += clientCargoCubicIn(day.achieved.trucks[truck], pipeline);
                }
                day.laneDays.push_back({laneDay.locationFrom, laneDay.locationTo, laneDay.shipCondition,
                                        laneDay.shipDate, static_cast<long long>(laneDay.loads), cargoCubicIn});
            }
            unordered_set<string> demandLines;
            for (const STRRecord& line : pipeline.demand.str) demandLines.insert(line.idpr);
            day.shippedLines = day.achieved.shippedByIdpr.size();
            for (const auto& shipped : day.achieved.shippedByIdpr) {
                if (demandLines.count(shipped.first)) ++day.shippedLinesInDemand;
            }
        }
        return loaded;
    }();
    return days[run];
}

// Each method's trucks and unit loads over every route on one of the lanes the client loaded
// most in a run: the spec's 38-truck lane on 5 Aug and 23- and 21-truck lanes on 6 Aug (E204,
// E205). Each route is solved alone, allowed its lane-date's whole reservation, so a lane split
// by segregation can total more trucks than the lane reserves; `reserved` counts each lane-date
// once.
struct LargestLane {
    string lane;
    long long clientLoads = 0;
    long long reserved = 0;
    long long allowed = 0;  // what the routes were each allowed, summed
    size_t routes = 0;
    vector<MethodScore> totals;  // parallel to benchmarkedMethods(), summed over the routes
};

// rank 0 is the lane the client loaded most, 1 the next.
LargestLane largestClientLane(size_t run, size_t rank) {
    REQUIRE(m4AugustTests::augustSolutionsPresent());
    const HeldDay& day = augustDay(run);
    map<tuple<string, string, string>, long long> loadsByLane;
    for (const ClientLaneDay& laneDay : clientDay(run).laneDays) {
        loadsByLane[make_tuple(laneDay.origin, laneDay.destination, laneDay.shipCondition)] += laneDay.loads;
    }
    vector<pair<tuple<string, string, string>, long long>> lanes(loadsByLane.begin(), loadsByLane.end());
    stable_sort(lanes.begin(), lanes.end(), [](const auto& left, const auto& right) { return left.second > right.second; });
    const auto& busiest = lanes.at(rank);
    const string origin = get<0>(busiest.first), destination = get<1>(busiest.first), condition = get<2>(busiest.first);
    LargestLane largest;
    largest.lane = origin + " -> " + destination + " " + condition;
    largest.clientLoads = busiest.second;
    for (const Reservation& reservation : day.book.reservations) {
        if (reservation.origin == origin && reservation.destination == destination
            && reservation.shipCondition == condition) {
            largest.reserved += reservation.trucks;
        }
    }
    for (const PackingMethod method : benchmarkedMethods()) {
        MethodScore total;
        total.method = method;
        total.ran = true;
        largest.totals.push_back(total);
    }
    for (size_t route = 0; route < day.classification.routes.size(); ++route) {
        const LaneRoute& laneRoute = day.classification.routes[route];
        const GroupKey& key = day.run->segregation.groups[laneRoute.groupIndex].key;
        if (key.locationFrom != origin || key.locationTo != destination || key.shipCondition != condition) continue;
        if (laneRoute.itemIndices.empty() || laneRoute.reservedTrucks == 0) continue;
        ++largest.routes;
        largest.allowed += laneRoute.reservedTrucks;
        const RouteBenchmark benchmark = benchmarkRoute(*day.run, day.items, day.classification, route, realDataSettings());
        for (size_t method = 0; method < benchmark.scores.size(); ++method) {
            const MethodScore& score = benchmark.scores[method];
            MethodScore& total = largest.totals[method];
            total.ran = total.ran && score.ran;
            total.trucks += score.trucks;
            total.unitLoadsShipped += score.unitLoadsShipped;
            total.seconds += score.seconds;
        }
    }
    return largest;
}

string percentOf(size_t part, size_t whole) {
    return fixed(whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole), 1) + "%";
}

string percentOf(double part, double whole) { return fixed(whole <= 0.0 ? 0.0 : 100.0 * part / whole, 1) + "%"; }

bool fitsTrailerWidth(const FloorRow& row, const TrailerSpec& trailer) {
    return row.widthIn <= trailer.interiorWidthIn + kQuantityEpsilon;
}

bool isOverflow(LeftOverReason reason) {
    return reason == LeftOverReason::OverReservation || reason == LeftOverReason::FloorEviction
           || reason == LeftOverReason::UnitLoadCap;
}

// Per route: whether it left goods over a full truck, there or moved on to another date. Its
// trucks stopped for want of room; the other routes' trucks ran out of goods first.
vector<char> routesLeavingGoods(const HeldDay& day) {
    const vector<optional<size_t>> routeOfItem = finalRouteOfItems(day.classification, day.plan, day.items.items.size());
    vector<char> leaving(day.classification.routes.size(), 0);
    for (const LeftOverItem& left : day.plan.leftOver) {
        if (isOverflow(left.reason) && routeOfItem[left.itemIndex]) leaving[*routeOfItem[left.itemIndex]] = 1;
    }
    for (const MovedItem& move : day.plan.moved) {
        if (isOverflow(move.reason)) leaving[move.routeIndex] = 1;
    }
    return leaving;
}

struct RowCounts {
    map<FloorPatternKind, size_t> rowsByPattern;
    size_t rows = 0;
    size_t turnedItems = 0;
    double usedLengthIn = 0.0;
    double footprintLengthIn = 0.0;  // the trucks' footprints packed edge to edge across the width
};

RowCounts rowCounts(const HeldDay& day) {
    RowCounts counts;
    for (const PlannedTruck& truck : day.plan.trucks) {
        for (const FloorRow& row : truck.floor.rows) ++counts.rowsByPattern[row.pattern];
        counts.rows += truck.floor.rows.size();
        counts.turnedItems += static_cast<size_t>(truck.floor.turnedItems);
        counts.usedLengthIn += truck.floor.usedLengthIn;
        for (const size_t index : truck.items) {
            const SelectionItem& item = day.items.items[index];
            counts.footprintLengthIn += item.footprintLongIn * item.footprintShortIn / day.run->trailer.interiorWidthIn;
        }
    }
    return counts;
}

size_t trucksCarryingStacks(const HeldDay& day, size_t levels) {
    return static_cast<size_t>(count_if(day.plan.trucks.begin(), day.plan.trucks.end(), [&](const PlannedTruck& truck) {
        return any_of(truck.items.begin(), truck.items.end(),
                      [&](size_t index) { return day.items.items[index].lineIndices.size() >= levels; });
    }));
}

// The client's loads set against our plan line by line: how far each load's date lies from the
// line's DATFR_TA, and where the line's goods are in our plan, judged by its first item.
struct ClientLinesAgainstOurs {
    map<long long, double> unitLoadsByDaysFromShipDate;
    map<string, double> unitLoadsByOurState;
    double unitLoads = 0.0;
    double unitLoadsAfterDeliveryDate = 0.0;  // loaded after the line's DATTO_TA
};

// Where a demand line's goods are in our plan, judged by its first item.
class LinesInOurPlan {
public:
    explicit LinesInOurPlan(const HeldDay& day)
        : day_(day), firstItemOfLine_(day.run->join.lines.size(), day.items.items.size()),
          truckOfItem_(day.items.items.size(), nullptr), leftOverReasonOf_(day.items.items.size()) {
        const vector<JoinedLine>& lines = day.run->join.lines;
        lineByIdpr_.reserve(lines.size());
        for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) lineByIdpr_.emplace(lines[lineIndex].str->idpr, lineIndex);
        for (size_t index = 0; index < day.items.items.size(); ++index) {
            for (const size_t lineIndex : day.items.items[index].lineIndices) {
                if (firstItemOfLine_[lineIndex] == day.items.items.size()) firstItemOfLine_[lineIndex] = index;
            }
        }
        for (const PlannedTruck& truck : day.plan.trucks) {
            for (const size_t index : truck.items) truckOfItem_[index] = &truck;
        }
        for (const LeftOverItem& left : day.plan.leftOver) leftOverReasonOf_[left.itemIndex] = left.reason;
    }

    // The demand line with this IDPR, or nullptr when it is not a line of this run.
    const STRRecord* line(const string& idpr) const {
        const auto found = lineByIdpr_.find(idpr);
        return found == lineByIdpr_.end() ? nullptr : day_.run->join.lines[found->second].str;
    }

    // "on our truck, his date", "left: no reservation", ...; loadDate is the client's.
    string state(const string& idpr, const string& loadDate) const {
        const auto found = lineByIdpr_.find(idpr);
        if (found == lineByIdpr_.end()) return "not a line of this run";
        const size_t item = firstItemOfLine_[found->second];
        if (item == day_.items.items.size()) return "no item: rejected or not stacked";
        if (truckOfItem_[item]) return truckOfItem_[item]->shipDate == loadDate ? "on our truck, his date" : "on our truck, another date";
        if (leftOverReasonOf_[item]) return string("left: ") + leftOverReasonName(*leftOverReasonOf_[item]);
        return "on no truck and in no list";
    }

private:
    const HeldDay& day_;
    unordered_map<string, size_t> lineByIdpr_;
    vector<size_t> firstItemOfLine_;
    vector<const PlannedTruck*> truckOfItem_;
    vector<optional<LeftOverReason>> leftOverReasonOf_;
};

ClientLinesAgainstOurs clientLinesAgainstOurs(const HeldDay& day, const ClientDay& client) {
    const LinesInOurPlan ourPlan(day);
    ClientLinesAgainstOurs result;
    for (const auto& laneDay : client.achieved.laneDays) {
        for (const size_t truckIndex : laneDay.truckIndices) {
            const InputTruck& load = client.achieved.trucks[truckIndex];
            for (const InputTruckLine& line : load.lines) {
                const double unitLoads = clientLineUnitLoads(load, line, *day.run);
                result.unitLoads += unitLoads;
                result.unitLoadsByOurState[ourPlan.state(line.idpr, load.shipDate)] += unitLoads;
                const STRRecord* demand = ourPlan.line(line.idpr);
                if (!demand) continue;
                result.unitLoadsByDaysFromShipDate[daysBetween(demand->datfr_ta, load.shipDate)] += unitLoads;
                if (load.shipDate > demand->datto_ta) result.unitLoadsAfterDeliveryDate += unitLoads;
            }
        }
    }
    return result;
}

// What would show our lane key failing his lanes: his loads on a lane and date that a placeholder
// row names inside our window, yet our book does not reserve; and his unit loads carried on a lane
// other than their demand line's, which would mean the two files spell a lane differently.
struct LaneKeyMisses {
    long long loads = 0;
    double unitLoadsOffTheirLane = 0.0;
};

// Why the client's unit loads that our plan leaves as "no reservation" or "after window" are left:
// whether his load's own lane and date match a reservation in our book (lane key and CTL window
// both), whether the line rides his load's lane, and how far his load's date is from the line's
// DATFR_TA.
LaneKeyMisses printClientMatching(ostream& out, const HeldDay& day, const ClientDay& client) {
    LaneKeyMisses misses;
    const LinesInOurPlan ourPlan(day);
    const PlanningWindow& window = day.book.window;
    const vector<PlaceholderRecord>& placeholders = day.run->placeholders.placeholders;
    map<string, long long> loadsByLaneDate;
    map<tuple<string, string, string>, double> byMatch;   // our state, his load's lane-date, the line's lane
    map<pair<string, string>, double> byDays;               // our state, his load's date from DATFR_TA
    map<string, double> totalByState;
    for (const auto& laneDay : client.achieved.laneDays) {
        const bool reserved = findReservation(day.book, laneDay.locationFrom, laneDay.locationTo, laneDay.shipCondition,
                                              laneDay.shipDate) != nullptr;
        const bool inWindow = laneDay.shipDate >= window.firstShipDate && laneDay.shipDate <= window.lastShipDate;
        const string loadLaneDate = reserved  ? "reserved in our book"
                                    : inWindow ? "in our window, not reserved"
                                               : "outside our window";
        string why = loadLaneDate;
        if (!reserved) {
            // A placeholder row naming exactly this lane and date, if any, and what the book did with it.
            const auto row = find_if(placeholders.begin(), placeholders.end(), [&](const PlaceholderRecord& placeholder) {
                return placeholder.locfrno == laneDay.locationFrom && placeholder.loctono == laneDay.locationTo
                       && placeholder.ship_cond == laneDay.shipCondition && placeholder.datfr_ta == laneDay.shipDate;
            });
            if (row == placeholders.end()) {
                why += ": no placeholder row for it";
            } else {
                const size_t rowIndex = static_cast<size_t>(row - placeholders.begin());
                const auto skipped = find_if(day.book.skipped.begin(), day.book.skipped.end(),
                                             [&](const SkippedPlaceholder& skip) { return skip.placeholderIndex == rowIndex; });
                if (skipped == day.book.skipped.end() && inWindow) misses.loads += static_cast<long long>(laneDay.loads);
                why += skipped == day.book.skipped.end() ? string(": its row is in no reservation")
                                                         : string(": its row skipped, ") + describeSkipReason(skipped->reason);
            }
        }
        loadsByLaneDate[why] += static_cast<long long>(laneDay.loads);
        for (const size_t truckIndex : laneDay.truckIndices) {
            const InputTruck& load = client.achieved.trucks[truckIndex];
            for (const InputTruckLine& line : load.lines) {
                const string state = ourPlan.state(line.idpr, load.shipDate);
                if (state != "left: no reservation" && state != "left: after window") continue;
                const STRRecord& demand = *ourPlan.line(line.idpr);
                const bool sameLane = demand.locfrno == laneDay.locationFrom && demand.loctono == laneDay.locationTo
                                      && demand.ship_cond == laneDay.shipCondition;
                const double unitLoads = clientLineUnitLoads(load, line, *day.run);
                if (!sameLane) misses.unitLoadsOffTheirLane += unitLoads;
                byMatch[{state, loadLaneDate, sameLane ? "yes" : "no"}] += unitLoads;
                const long long days = daysBetween(demand.datfr_ta, load.shipDate);
                byDays[{state, (days > 0 ? "+" : "") + to_string(days)}] += unitLoads;
                totalByState[state] += unitLoads;
            }
        }
    }
    long long loads = 0;
    for (const auto& [why, count] : loadsByLaneDate) loads += count;
    out << "  His " << grouped(loads) << " loads by their lane and date, against our book (window "
        << window.firstShipDate << " to " << window.lastShipDate << "):\n";
    for (const auto& [why, count] : loadsByLaneDate) {
        out << "    " << why << ": " << grouped(count) << " (" << percentOf(static_cast<double>(count), static_cast<double>(loads)) << ")\n";
    }
    out << "\n  His unit loads our plan leaves as no reservation or after window:\n";
    vector<vector<string>> matchRows;
    for (const auto& [key, unitLoads] : byMatch) {
        const auto& [state, loadLaneDate, sameLane] = key;
        matchRows.push_back({state, loadLaneDate, sameLane, grouped(unitLoads), percentOf(unitLoads, totalByState[state])});
    }
    printColumns(out, {"our plan", "his load's lane and date", "line on that lane", "unit loads", "share"}, matchRows,
                 {false, false, false, true, true});
    out << "\n  The same unit loads by days from the line's DATFR_TA to his load's date:\n";
    for (const auto& [state, total] : totalByState) {
        out << "    " << state << ":";
        for (const auto& [key, unitLoads] : byDays) {
            if (key.first == state) out << " " << key.second << ": " << grouped(unitLoads) << ";";
        }
        out << "\n";
    }
    return misses;
}

// One lane of one run: its reservations, our routes and what they built, our goods left on no
// truck, and the client's loads with the ship dates of the lines they carried.
void printLane(ostream& out, const HeldDay& day, const ClientDay& client, const string& origin,
               const string& destination, const string& condition) {
    const PipelineResult& run = *day.run;
    auto onLane = [&](size_t groupIndex) {
        const GroupKey& key = run.segregation.groups[groupIndex].key;
        return key.locationFrom == origin && key.locationTo == destination && key.shipCondition == condition;
    };
    out << "  Reserved:";
    for (const Reservation& reservation : day.book.reservations) {
        if (reservation.origin == origin && reservation.destination == destination && reservation.shipCondition == condition) {
            out << " " << reservation.trucks << " trucks on " << reservation.shipDate << ";";
        }
    }
    out << "\n\n";

    const vector<char> leavingGoods = routesLeavingGoods(day);
    vector<vector<string>> routeRows;
    for (size_t routeIndex = 0; routeIndex < day.classification.routes.size(); ++routeIndex) {
        const LaneRoute& route = day.classification.routes[routeIndex];
        if (!onLane(route.groupIndex)) continue;
        const RouteOutcome& outcome = day.plan.routes[routeIndex];
        routeRows.push_back({printable(run.segregation.groups[route.groupIndex].key.segregant), route.shipDate,
                             solveMethodName(route.method), grouped(route.reservedTrucks), grouped(route.trucksAllocated),
                             grouped(outcome.trucksAllowed), grouped(outcome.trucksBuilt), grouped(route.unitLoads),
                             leavingGoods[routeIndex] ? "yes" : "no"});
    }
    printColumns(out, {"group", "ship date", "method", "date reserved", "allocated", "allowed", "built", "unit loads",
                       "goods left over a full truck"},
                 routeRows, {false, false, false, true, true, true, true, true, false});

    map<pair<string, string>, double> leftByReasonAndDate;
    for (const LeftOverItem& left : day.plan.leftOver) {
        const SelectionItem& item = day.items.items[left.itemIndex];
        if (onLane(item.groupIndex)) leftByReasonAndDate[{leftOverReasonName(left.reason), item.shipDate}] += item.unitLoads;
    }

    vector<vector<string>> leftRows;
    for (const auto& [reasonAndDate, unitLoads] : leftByReasonAndDate) {
        leftRows.push_back({reasonAndDate.first, reasonAndDate.second, grouped(unitLoads)});
    }
    out << "\n  Our goods on the lane on no truck:\n";
    printColumns(out, {"why", "ship date", "unit loads"}, leftRows, {false, false, true});

    unordered_map<string, string> shipDateByIdpr;
    shipDateByIdpr.reserve(run.demand.str.size());
    for (const STRRecord& line : run.demand.str) shipDateByIdpr.emplace(line.idpr, line.datfr_ta);
    out << "\n  The client's loads on the lane, unit loads by the ship date (DATFR_TA) of the lines they carry:\n";
    for (const auto& laneDay : client.achieved.laneDays) {
        if (laneDay.locationFrom != origin || laneDay.locationTo != destination || laneDay.shipCondition != condition) continue;
        map<string, double> byShipDate;
        double unitLoads = 0.0;
        for (const size_t truckIndex : laneDay.truckIndices) {
            const InputTruck& load = client.achieved.trucks[truckIndex];
            for (const InputTruckLine& line : load.lines) {
                const double lineUnitLoads = clientLineUnitLoads(load, line, run);
                unitLoads += lineUnitLoads;
                const auto shipDate = shipDateByIdpr.find(line.idpr);
                byShipDate[shipDate == shipDateByIdpr.end() ? string("not in demand") : shipDate->second] += lineUnitLoads;
            }
        }
        out << "    " << laneDay.shipDate << ": " << laneDay.loads << " loads, " << grouped(unitLoads) << " unit loads;";
        for (const auto& [shipDate, lineUnitLoads] : byShipDate) out << " " << shipDate << " " << grouped(lineUnitLoads) << ";";
        out << "\n";
    }
}

// One truck's rows, each beside the rows its members could have formed instead: per pattern, its
// shortest that fits the width, else its first. floorPattern keeps the shortest that fits.
void printTruckRows(ostream& out, const HeldDay& day, const PlannedTruck& truck, const FloorPatternSettings& settings) {
    const TrailerSpec& trailer = day.run->trailer;
    vector<FloorItem> floorItems;
    floorItems.reserve(truck.items.size());
    for (const size_t index : truck.items) floorItems.push_back(floorItemFrom(day.items.items[index]));
    vector<vector<string>> rows;
    for (const FloorRow& row : truck.floor.rows) {
        string members;
        vector<size_t> memberIndices;
        for (const FloorPlacement& placement : row.placements) {
            const FloorItem& item = floorItems[placement.itemIndex];
            members += (members.empty() ? "" : ", ") + fixed(item.longSideIn, 1) + " x " + fixed(item.shortSideIn, 1)
                       + (item.levels > 1 ? " " + to_string(item.levels) + " high" : "") + (placement.turned ? " turned" : "");
            memberIndices.push_back(placement.itemIndex);
        }
        map<FloorPatternKind, FloorRow> shortestByPattern;
        for (FloorRow& candidate : candidateRows(floorItems, memberIndices, trailer, settings)) {
            const auto kept = shortestByPattern.find(candidate.pattern);
            if (kept == shortestByPattern.end()) {
                shortestByPattern.emplace(candidate.pattern, std::move(candidate));
            } else if (fitsTrailerWidth(candidate, trailer)
                       && (!fitsTrailerWidth(kept->second, trailer) || candidate.depthIn < kept->second.depthIn)) {
                kept->second = std::move(candidate);
            }
        }
        string weighed;
        for (const auto& [pattern, candidate] : shortestByPattern) {
            weighed += (weighed.empty() ? "" : "; ") + string(floorPatternName(pattern)) + " " + fixed(candidate.depthIn, 1)
                       + (fitsTrailerWidth(candidate, trailer) ? "" : " (too wide)");
        }
        rows.push_back({floorPatternName(row.pattern), fixed(row.depthIn, 1), fixed(row.widthIn, 1), members, weighed});
    }
    printColumns(out, {"row", "deep in", "wide in", "members, long x short in", "rows weighed, deep in"}, rows,
                 {false, true, true, false, false});
}

// The day planned again with other ship-date rules, to price them; the run plans only with the shipped ones.
SelectionPlan plannedUnder(const HeldDay& day, const ShipDateRules& rules, const SelectorSettings& settings) {
    return planShipDates(*day.run, day.book, day.items, m4AugustTests::kShippedRouting, settings, rules,
                         m4AugustTests::kShippedMaxDaysPulledForward)
        .plan;
}

// Why a good on no truck could not ride a truck of `date`, by what the ship-date rules allow there.
string whyNotOn(const SelectionItem& item, LeftOverReason reason, const string& date) {
    if (reason == LeftOverReason::Unfittable) return "fits no truck";
    if (reason == LeftOverReason::EscalatedToM5) return "left to M5";
    if (item.availDate > date) return "not available by then";
    if (item.shipDate > date
        && daysBetween(date, item.shipDate) + item.daysPulledForward > m4AugustTests::kShippedMaxDaysPulledForward) {
        return "due over " + to_string(m4AugustTests::kShippedMaxDaysPulledForward) + " days later";
    }
    return "allowed that date, not loaded";
}

using LaneKey = tuple<string, string, string>;

LaneKey laneKeyOf(const HeldDay& day, size_t groupIndex) {
    const GroupKey& key = day.run->segregation.groups[groupIndex].key;
    return {key.locationFrom, key.locationTo, key.shipCondition};
}

// Why a lane-date's trucks carry no more: the lane's goods on no truck, by why none could ride
// that date, and the cause that follows. A truck takes only its own group's goods, so a truck's
// cause is judged on its group's alone; a reservation's on every group of the lane.
class LaneDateCauses {
public:
    explicit LaneDateCauses(const HeldDay& day) : day_(day) {
        for (const LeftOverItem& left : day.plan.leftOver) {
            leftByLane_[laneKeyOf(day, day.items.items[left.itemIndex].groupIndex)].push_back(&left);
        }
    }

    // The cause, and the goods on no truck in unit loads by why, for trucks of `date`.
    pair<string, map<string, double>> of(const LaneKey& lane, const string& date,
                                         optional<size_t> groupIndex = nullopt) const {
        map<string, double> unitLoadsByWhy;
        const auto found = leftByLane_.find(lane);
        if (found != leftByLane_.end()) {
            for (const LeftOverItem* left : found->second) {
                const SelectionItem& item = day_.items.items[left->itemIndex];
                if (groupIndex && item.groupIndex != *groupIndex) continue;
                unitLoadsByWhy[whyNotOn(item, left->reason, date)] += item.unitLoads;
            }
        }
        const string cause = unitLoadsByWhy.count("allowed that date, not loaded") ? "goods allowed that date were not loaded"
                             : !unitLoadsByWhy.empty() ? "goods left on the lane, none allowed that date"
                                                       : "no goods left on the lane";
        return {cause, unitLoadsByWhy};
    }

private:
    const HeldDay& day_;
    map<LaneKey, vector<const LeftOverItem*>> leftByLane_;
};

// Every reserved truck left unbuilt, by lane-date, with its cause; returns the trucks by cause.
map<string, long long> printUnbuiltTrucks(ostream& out, const HeldDay& day) {
    const LaneDateCauses causes(day);
    const vector<char> leavingGoods = routesLeavingGoods(day);
    set<tuple<string, string, string, string>> laneDatesLeavingGoods;
    for (size_t route = 0; route < day.classification.routes.size(); ++route) {
        if (!leavingGoods[route]) continue;
        const LaneRoute& laneRoute = day.classification.routes[route];
        const auto [origin, destination, condition] = laneKeyOf(day, laneRoute.groupIndex);
        laneDatesLeavingGoods.insert({origin, destination, condition, laneRoute.shipDate});
    }
    map<string, long long> trucksByCause;
    vector<vector<string>> rows;
    for (const ReservationUse& use : day.plan.reservations) {
        if (use.unused() <= 0) continue;
        const Reservation& reservation = day.book.reservations[use.reservationIndex];
        string cause, detail;
        if (use.escalated) {
            cause = "left to M5";
        } else if (laneDatesLeavingGoods.count({reservation.origin, reservation.destination, reservation.shipCondition,
                                                reservation.shipDate})) {
            cause = "a group left goods over a full truck";
        } else {
            map<string, double> unitLoadsByWhy;
            tie(cause, unitLoadsByWhy) =
                causes.of({reservation.origin, reservation.destination, reservation.shipCondition}, reservation.shipDate);
            for (const auto& [why, unitLoads] : unitLoadsByWhy) {
                detail += (detail.empty() ? "" : "; ") + why + " " + grouped(unitLoads);
            }
        }
        trucksByCause[cause] += use.unused();
        rows.push_back({reservation.origin + " -> " + reservation.destination + " " + reservation.shipCondition,
                        reservation.shipDate, grouped(use.reserved), grouped(use.built), grouped(use.unused()), cause,
                        detail.empty() ? string("-") : detail});
    }
    printColumns(out, {"lane", "ship date", "reserved", "built", "unbuilt", "cause", "the lane's goods on no truck, unit loads"},
                 rows, {false, false, true, true, true, false, false});
    return trucksByCause;
}

// Where a run's trucks lose cube, by what stopped each truck: in cu ft per truck, the trailer's
// cube less the cargo, split into the rear left empty, the gaps across the width beside the rows,
// and the air above the stacks (the ceiling, less what the stacks reach).
struct LostCube {
    size_t trucks = 0;
    double cargo = 0.0;
    double rear = 0.0;
    double acrossWidth = 0.0;
    double aboveStacks = 0.0;

    void add(const LostCube& other) {
        trucks += other.trucks;
        cargo += other.cargo;
        rear += other.rear;
        acrossWidth += other.acrossWidth;
        aboveStacks += other.aboveStacks;
    }
};

map<string, LostCube> lostCubeByStop(const HeldDay& day) {
    const TrailerSpec& trailer = day.run->trailer;
    const vector<char> leavingGoods = routesLeavingGoods(day);
    const LaneDateCauses causes(day);
    map<string, LostCube> byStop;
    for (const PlannedTruck& truck : day.plan.trucks) {
        double footprints = 0.0, cargo = 0.0;
        for (const size_t index : truck.items) {
            const SelectionItem& item = day.items.items[index];
            footprints += item.footprintLongIn * item.footprintShortIn;
            cargo += item.cubeCubicIn;
        }
        const double floorShare = truck.floor.usedLengthIn / trailer.interiorLengthIn;
        const double weightShare = truck.weightLb / trailer.weightLimitLb;
        const string stop = !leavingGoods[truck.routeIndex]
                                ? "ran out: " + causes.of(laneKeyOf(day, truck.groupIndex), truck.shipDate, truck.groupIndex).first
                            : weightShare >= floorShare ? "stopped by weight, goods left"
                                                        : "stopped by floor, goods left";
        LostCube& lost = byStop[stop];
        ++lost.trucks;
        lost.cargo += cargo / 1728.0;
        lost.rear += truck.floor.rearWasteIn * trailer.interiorWidthIn * trailer.stackHeightCeilingIn / 1728.0;
        lost.acrossWidth += (truck.floor.usedLengthIn * trailer.interiorWidthIn - footprints) * trailer.stackHeightCeilingIn / 1728.0;
        lost.aboveStacks += (footprints * trailer.stackHeightCeilingIn - cargo) / 1728.0;
    }
    return byStop;
}

vector<string> lostCubeRow(const string& run, const string& stop, const LostCube& lost, size_t runTrucks,
                           const TrailerSpec& trailer) {
    const double trucks = static_cast<double>(max<size_t>(lost.trucks, 1));
    const double rearCubicFeetPerInch = trailer.interiorWidthIn * trailer.stackHeightCeilingIn / 1728.0;
    return {run, stop, grouped(lost.trucks), percentOf(lost.trucks, runTrucks), grouped(lost.cargo / trucks),
            grouped((lost.rear + lost.acrossWidth + lost.aboveStacks) / trucks), grouped(lost.rear / trucks),
            fixed(lost.rear / trucks / rearCubicFeetPerInch, 1), grouped(lost.acrossWidth / trucks),
            grouped(lost.aboveStacks / trucks)};
}

// One plan's row of section 7.
vector<string> ruleCostRow(const string& label, const HeldDay& day, const SelectionItems& items,
                           const SelectionPlan& plan, double maxUnitLoadsPerTruck) {
    const FullnessSummary fullness = summariseFullness(*day.run, items, plan, kHeavyTruckLb);
    double unitLoadsShipped = 0.0;
    for (const PlannedTruck& truck : plan.trucks) unitLoadsShipped += truck.unitLoads;
    const size_t violations = checkInvariants(*day.run, day.book, items, plan, maxUnitLoadsPerTruck).size();
    return {day.label, label, grouped(plan.trucks.size()), grouped(plan.penaltyExposureTrucks),
            grouped(fullness.averageCubicFeet), fixed(fullness.averageRearWasteIn, 1), grouped(unitLoadsShipped),
            violations == 0 ? string("none") : grouped(violations)};
}

} // anonymous namespace

// Run with: ./build/ob_tests -ts=m4Benchmark --no-skip, or ctest (its m4Benchmark entry). Every
// test here is skipped in a plain ob_tests run: they take minutes over every extract, and a
// clean clone has none (E224). ctest runs them, and there a missing extract fails by name
// instead of the suite passing with nothing checked (E225).
TEST_SUITE("m4Benchmark") {

TEST_CASE("m4Benchmark: every extract the benchmark reads is present" * doctest::skip()) {
    CHECK(m4AugustTests::augustPresent());
    CHECK(m4AugustTests::augustSolutionsPresent());
    for (const string& missing : crossDayTests::missingExtracts()) FAIL_CHECK(missing);
    for (size_t day = 0; day < crossDayTests::newExtracts::kDayCount; ++day) {
        CHECK_MESSAGE(crossDayTests::newExtracts::dayPresent(day),
                      crossDayTests::newExtracts::directories[day] << " is not complete");
    }
}

TEST_CASE("m4Benchmark: every held extract keeps every invariant" * doctest::skip()) {
    for (const HeldDay& day : heldDays()) {
        CAPTURE(day.label);
        for (const InvariantViolation& violation :
             checkInvariants(*day.run, day.book, day.items, day.plan, realDataSettings().maxUnitLoadsPerTruck)) {
            FAIL_CHECK(violation.invariant << ": " << violation.detail);
        }
    }
}

// The lanes the client splits into underscored lane ids (1697_674, 1709_1710, ...) are the do-not-mix
// splits at origins 2027 and 2028. I3 is checked on every truck of those lanes from the lines it
// carries, not from the items' own group indices.
TEST_CASE("m4Benchmark: I3 holds on the segregation-split lanes from 2027 and 2028 in every held extract"
          * doctest::skip()) {
    size_t splitLaneTrucks = 0;
    for (const HeldDay& day : heldDays()) {
        CAPTURE(day.label);
        const vector<SegregatedGroup>& groups = day.run->segregation.groups;
        set<tuple<string, string, string>> splitLanes;
        for (const SegregatedGroup& group : groups) {
            const GroupKey& key = group.key;
            if (group.splitReason == SplitReason::DoNotMix && (key.locationFrom == "2027" || key.locationFrom == "2028")) {
                splitLanes.insert({key.locationFrom, key.locationTo, key.shipCondition});
            }
        }
        vector<size_t> groupOfLine(day.run->join.lines.size(), groups.size());
        for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            for (const size_t lineIndex : groups[groupIndex].lineIndices) groupOfLine[lineIndex] = groupIndex;
        }
        for (const PlannedTruck& truck : day.plan.trucks) {
            const GroupKey& key = groups[truck.groupIndex].key;
            if (!splitLanes.count({key.locationFrom, key.locationTo, key.shipCondition})) continue;
            ++splitLaneTrucks;
            for (const size_t index : truck.items) {
                for (const size_t lineIndex : day.items.items[index].lineIndices) {
                    CHECK(groupOfLine[lineIndex] == truck.groupIndex);
                }
            }
        }
        for (const InvariantViolation& violation :
             checkInvariants(*day.run, day.book, day.items, day.plan, realDataSettings().maxUnitLoadsPerTruck)) {
            CHECK_MESSAGE(violation.invariant != "I3", violation.detail);
        }
    }
    CHECK(splitLaneTrucks > 0);
}

TEST_CASE("m4Benchmark: every item and reservation of every held extract is routed exactly once"
          * doctest::skip()) {
    for (const HeldDay& day : heldDays()) {
        CAPTURE(day.label);
        const LaneClassification& classification = day.classification;
        vector<int> seen(day.items.items.size(), 0);
        set<tuple<string, string, string, string>> routedLaneDates;
        for (const LaneRoute& route : classification.routes) {
            for (const size_t index : route.itemIndices) ++seen[index];
            const GroupKey& key = day.run->segregation.groups[route.groupIndex].key;
            routedLaneDates.insert({key.locationFrom, key.locationTo, key.shipCondition, route.shipDate});
            const bool hasItems = !route.itemIndices.empty();
            CHECK((route.method == SolveMethod::NoDemand) == !hasItems);
            CHECK((route.method == SolveMethod::Unrouted) == (hasItems && route.reservedTrucks == 0));
            CHECK((route.method == SolveMethod::M5) == (hasItems && route.reservedTrucks > m4AugustTests::kShippedRouting.m5Threshold));
        }
        for (const UnroutedItem& left : classification.itemsLeftOut) ++seen[left.itemIndex];
        // A good offered to a date that did not load it is on that date's route too.
        for (const MovedItem& move : day.plan.moved) --seen[move.itemIndex];
        CHECK(all_of(seen.begin(), seen.end(), [](int count) { return count == 1; }));
        const ReservationCoverage coverage = matchDemand(day.book, day.run->demand.str);
        const set<size_t> withoutDemand(coverage.reservationsWithoutDemand.begin(),
                                        coverage.reservationsWithoutDemand.end());
        for (size_t index = 0; index < day.book.reservations.size(); ++index) {
            const Reservation& reservation = day.book.reservations[index];
            CAPTURE(index);
            CHECK((routedLaneDates.count({reservation.origin, reservation.destination, reservation.shipCondition,
                                          reservation.shipDate})
                   || withoutDemand.count(index)));
        }
    }
}

// Rev 8: higher priority ships first, and filling the truck wins. A group of a split lane-date
// must not take a truck it fills under half while a sibling group, with goods at least as urgent,
// leaves half a truck or more over the reservation.
TEST_CASE("m4Benchmark: no split lane-date sends a half-empty truck while a sibling group's goods stay"
          * doctest::skip()) {
    auto urgencyOf = [](const SelectionItem& item) {
        return make_tuple(item.timing != ShipTiming::Overdue, item.shipDate, item.priority);
    };
    size_t splitLaneDatesChecked = 0;
    for (const HeldDay& day : heldDays()) {
        CAPTURE(day.label);
        const double truckFloor = day.run->trailer.interiorLengthIn * day.run->trailer.interiorWidthIn;
        using LaneDate = tuple<string, string, string, string>;
        auto laneDateOf = [&](size_t groupIndex, const string& shipDate) {
            const GroupKey& key = day.run->segregation.groups[groupIndex].key;
            return LaneDate{key.locationFrom, key.locationTo, key.shipCondition, shipDate};
        };
        const vector<optional<size_t>> routeOfItem =
            finalRouteOfItems(day.classification, day.plan, day.items.items.size());
        // Per lane-date and group: the floor its goods left over the reservation take, and the most urgent of them.
        map<pair<LaneDate, size_t>, pair<double, const SelectionItem*>> leftOverByGroup;
        for (const LeftOverItem& left : day.plan.leftOver) {
            if (left.reason != LeftOverReason::OverReservation) continue;
            const SelectionItem& item = day.items.items[left.itemIndex];
            REQUIRE(routeOfItem[left.itemIndex]);
            const LaneRoute& route = day.classification.routes[*routeOfItem[left.itemIndex]];
            auto& [area, mostUrgent] = leftOverByGroup[{laneDateOf(route.groupIndex, route.shipDate), route.groupIndex}];
            area += item.footprintLongIn * item.footprintShortIn;
            if (!mostUrgent || urgencyOf(item) < urgencyOf(*mostUrgent)) mostUrgent = &item;
        }
        set<LaneDate> checked;
        for (const PlannedTruck& truck : day.plan.trucks) {
            if (truck.areaSqIn >= 0.5 * truckFloor) continue;
            const LaneDate laneDate = laneDateOf(truck.groupIndex, truck.shipDate);
            const SelectionItem* truckMostUrgent = nullptr;
            for (const size_t index : truck.items) {
                if (!truckMostUrgent || urgencyOf(day.items.items[index]) < urgencyOf(*truckMostUrgent)) {
                    truckMostUrgent = &day.items.items[index];
                }
            }
            for (const auto& [groupKey, left] : leftOverByGroup) {
                if (groupKey.first != laneDate || groupKey.second == truck.groupIndex) continue;
                checked.insert(laneDate);
                CAPTURE(get<0>(laneDate));
                CAPTURE(get<1>(laneDate));
                CAPTURE(get<3>(laneDate));
                CHECK_FALSE((left.first >= 0.5 * truckFloor && !(urgencyOf(*truckMostUrgent) < urgencyOf(*left.second))));
            }
        }
        splitLaneDatesChecked += checked.size();
    }
    MESSAGE("split lane-dates with a half-empty truck beside a sibling's left-over goods: " << splitLaneDatesChecked);
}

TEST_CASE("m4Benchmark: on 4 August the one truck of 2027 -> 2031 TF goes to the group that fills it"
          * doctest::skip()) {
    const HeldDay& day = augustDay(0);
    size_t routesSeen = 0;
    for (size_t routeIndex = 0; routeIndex < day.classification.routes.size(); ++routeIndex) {
        const LaneRoute& route = day.classification.routes[routeIndex];
        const SegregatedGroup& group = day.run->segregation.groups[route.groupIndex];
        if (group.key.locationFrom != "2027" || group.key.locationTo != "2031" || group.key.shipCondition != "TF"
            || route.shipDate != "2026-08-06") {
            continue;
        }
        ++routesSeen;
        CAPTURE(group.key.segregant);
        CHECK(day.plan.routes[routeIndex].trucksBuilt == (group.key.segregant == "S20" ? 1 : 0));
    }
    CHECK(routesSeen == 2);
}

// Rev 8 judges M4 on the cube each truck carries and the space left at its rear, not on the
// truck count. Pinned as measured so a change that empties trucks fails by name; a change that
// fills them better passes and should re-pin. Re-pinned on 2 Nov 2026 when the client's
// ship-date rules went in (2,887 / 2,774 / 2,724 cu ft before), and again the same day when each
// group's part-filled truck was topped up with its own later goods first.
TEST_CASE("m4Benchmark: August fill is no worse than measured on 2 Nov" * doctest::skip()) {
    struct Fill {
        double cubicFeetAtLeast;
        double rearWasteInAtMost;
    };
    const array<Fill, 3> measured{Fill{3143.7, 38.6}, Fill{3189.6, 28.6}, Fill{3165.2, 33.4}};
    for (size_t run = 0; run < measured.size(); ++run) {
        const HeldDay& day = augustDay(run);
        CAPTURE(day.label);
        const FullnessSummary fullness = summariseFullness(*day.run, day.items, day.plan, kHeavyTruckLb);
        CHECK(fullness.averageCubicFeet >= measured[run].cubicFeetAtLeast);
        CHECK(fullness.averageRearWasteIn <= measured[run].rearWasteInAtMost);
    }
}

TEST_CASE("m4Benchmark: the client's August loads are measured from their lines, near the cube he states"
          * doctest::skip()) {
    const double trailerCubicFeet = 630.0 * 100.0 * 108.0 / 1728.0;
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = augustDay(run);
        CAPTURE(day.label);
        double cargoCubicIn = 0.0;
        long long loads = 0;
        for (const ClientLaneDay& laneDay : clientDay(run).laneDays) {
            cargoCubicIn += laneDay.cargoCubicIn;
            loads += laneDay.loads;
        }
        REQUIRE(loads > 0);
        const double cubicFeetPerLoad = cargoCubicIn / 1728.0 / static_cast<double>(loads);
        // The client states 3,300 to 3,400 cu ft a truck; computed from the lines his loads carry it
        // is a little below, as the stated figure is his best, not his average.
        CHECK(cubicFeetPerLoad > 0.75 * trailerCubicFeet);
        CHECK(cubicFeetPerLoad < kClientStatedCubicFeetHigh);
    }
}

TEST_CASE("m4Benchmark: the August routes per method, with split lanes shared by urgency" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    using Counts = map<SolveMethod, size_t>;
    // Pulled-forward goods take most reserved lane-dates past their reservation, so most go to
    // knapsack.
    const array<Counts, 3> expected{
        Counts{{SolveMethod::Knapsack, 158}, {SolveMethod::BinPacker, 8}, {SolveMethod::Unrouted, 383},
               {SolveMethod::NoDemand, 5}},
        Counts{{SolveMethod::Knapsack, 265}, {SolveMethod::BinPacker, 7}, {SolveMethod::M5, 3},
               {SolveMethod::Unrouted, 194}, {SolveMethod::NoDemand, 16}},
        Counts{{SolveMethod::Knapsack, 293}, {SolveMethod::PatternPacker, 1}, {SolveMethod::BinPacker, 8},
               {SolveMethod::Unrouted, 274}, {SolveMethod::NoDemand, 8}}};
    for (size_t run = 0; run < expected.size(); ++run) {
        const HeldDay& day = augustDay(run);
        CAPTURE(day.label);
        Counts routesBy;
        for (const LaneRoute& route : day.classification.routes) ++routesBy[route.method];
        CHECK(routesBy == expected[run]);
    }
}

TEST_CASE("m4Benchmark: every held extract plans the same at one thread, again, and at four"
          * doctest::skip()) {
    SelectorSettings fourThreads = realDataSettings();
    fourThreads.threads = 4;
    for (const HeldDay& day : heldDays()) {
        CAPTURE(day.label);
        const SelectionPlan again = plannedUnder(day, m4AugustTests::kShippedShipDateRules, realDataSettings());
        CHECK(samePlan(day.plan, again));
        CHECK(planFingerprint(day.plan) == planFingerprint(again));
        CHECK(samePlan(day.plan, plannedUnder(day, m4AugustTests::kShippedShipDateRules, fourThreads)));
    }
}

TEST_CASE("m4Benchmark: the client's largest August lanes are run with every method" * doctest::skip()) {
    // --no-skip runs this without the files; the first three held days are August only with them.
    REQUIRE(m4AugustTests::augustPresent());
    REQUIRE(m4AugustTests::augustSolutionsPresent());
    // The spec's three: 38 loads in the 5 Aug run, 23 and 21 in the 6 Aug run.
    const vector<tuple<size_t, size_t, long long>> lanes{{1, 0, 38}, {2, 0, 23}, {2, 1, 21}};
    for (const auto& lane : lanes) {
        const size_t run = get<0>(lane), rank = get<1>(lane);
        const long long loads = get<2>(lane);
        // Not CAPTURE(augustDay(run).label): CAPTURE re-evaluates its expression each time an
        // assertion is logged, and augustDay's REQUIREs then re-enter doctest's reporter, which
        // hangs a --success run.
        const string& label = augustDay(run).label;
        CAPTURE(label);
        CAPTURE(rank);
        const LargestLane largest = largestClientLane(run, rank);
        CHECK(largest.clientLoads == loads);
        REQUIRE(largest.routes > 0);
        for (const MethodScore& total : largest.totals) {
            CHECK(total.ran);
            CHECK(total.trucks <= largest.allowed);
        }
    }
}

TEST_CASE("m4Benchmark: on the August routes the methods are ranked and the routing rules derived"
          * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    const vector<RouteBenchmark> benchmarks = allAugustRouteBenchmarks();
    REQUIRE_FALSE(benchmarks.empty());
    for (const RouteBenchmark& benchmark : benchmarks) {
        REQUIRE(benchmark.best);
        // The best ships at least as much as the method the route was sent to.
        const PackingMethod routed =
            benchmark.routedTo == SolveMethod::PatternPacker ? PackingMethod::PatternPacker : PackingMethod::BinPacker;
        const auto routedScore = find_if(benchmark.scores.begin(), benchmark.scores.end(),
                                         [&](const MethodScore& score) { return score.method == routed; });
        const auto bestScore = find_if(benchmark.scores.begin(), benchmark.scores.end(),
                                       [&](const MethodScore& score) { return score.method == *benchmark.best; });
        CHECK(bestScore->unitLoadsShipped >= routedScore->unitLoadsShipped - 1e-9);
    }
    // The data sets the routing rules, never the reverse. A variety split is a rule once it sends
    // at most a third of the routes it decides to the method that ranked lower (E206 - E208), and
    // the shipped ratio must then decide them as well. A routed method over the budget puts the M5
    // threshold below its reservation (E209). On 31 Oct 2026 the data set neither. On 2 Nov 2026,
    // under the ship-date rules, the packers differ on 7 August routes and a split at 101 unit
    // loads per product misroutes 2 of them: the shipped varietyRatio is that split.
    const LaneClassifierSettings shipped = loadSelectionParams(crossDayTests::kStrictParamsPath).routing;
    const optional<VarietyCrossover> crossover = varietyCrossover(benchmarks);
    REQUIRE(crossover);
    MESSAGE("best variety split " << crossover->ratio << " misroutes " << crossover->lanesMisrouted << " of "
                                  << crossover->lanesCompared << "; the shipped " << shipped.varietyRatio << " misroutes "
                                  << varietySplitAt(benchmarks, shipped.varietyRatio).lanesMisrouted);
    if (crossover->lanesMisrouted * 3 <= crossover->lanesCompared) {
        CHECK(varietySplitAt(benchmarks, shipped.varietyRatio).lanesMisrouted == crossover->lanesMisrouted);
    }
    if (const optional<long long> overBudget = smallestReservationOverBudget(benchmarks)) {
        CHECK(shipped.m5Threshold < *overBudget);
    }
}

TEST_CASE("m4Benchmark: August is set beside the client's own loads, lane-day by lane-day" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    REQUIRE(m4AugustTests::augustSolutionsPresent());
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = augustDay(run);
        CAPTURE(day.label);
        const ClientDay& client = clientDay(run);
        const ClientComparison comparison = compareWithClient(*day.run, day.book, day.plan, client.laneDays);
        CHECK(comparison.clientLoads == static_cast<long long>(client.achieved.totalLoads()));
        CHECK(comparison.ourTrucks == static_cast<long long>(day.plan.trucks.size()));
        CHECK(comparison.reserved == day.book.reservedTrucks);
        // Lanes left to M5 build nothing here, and penalty exposure counts their trucks unused
        // as the comparison does.
        CHECK(comparison.ourUnused == day.plan.penaltyExposureTrucks);
        // Measured 31 Oct 2026: every line his loads shipped is a line of this run's demand.
        // The spec expected about 41%, the rest autofill; these files show none.
        CHECK(client.shippedLinesInDemand == client.shippedLines);
    }
}

TEST_CASE("m4Benchmark: the benchmark report is written for the client" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    REQUIRE(m4AugustTests::augustSolutionsPresent());
    ostringstream report;
    report << "M4 BENCHMARK REPORT\n\n"
           << "Every figure is ours, measured on the extracts held, unless it says it is the client's.\n";

    report << "\n1. Every extract: invariants, fullness and the plan's fingerprint\n\n";
    vector<vector<string>> dayRows;
    for (const HeldDay& day : heldDays()) {
        const FullnessSummary fullness = summariseFullness(*day.run, day.items, day.plan, kHeavyTruckLb);
        const vector<InvariantViolation> violations =
            checkInvariants(*day.run, day.book, day.items, day.plan, realDataSettings().maxUnitLoadsPerTruck);
        ostringstream fingerprint;
        fingerprint << hex << planFingerprint(day.plan);
        dayRows.push_back({printable(day.label), grouped(static_cast<double>(day.plan.trucks.size())),
                           grouped(static_cast<double>(day.book.reservedTrucks)),
                           grouped(static_cast<double>(day.plan.penaltyExposureTrucks)),
                           fixed(fullness.averageRearWasteIn, 1), fixed(fullness.averageCube * 100.0, 1) + "%",
                           grouped(fullness.averageCubicFeet), grouped(fullness.averageWeightLb),
                           grouped(static_cast<double>(fullness.heavyLanes)),
                           violations.empty() ? string("none")
                                              : to_string(violations.size()) + " (" + violations.front().invariant + ")",
                           fingerprint.str()});
    }
    printColumns(report,
                 {"extract", "trucks", "reserved", "unused", "rear waste in", "cube", "cu ft", "lb",
                  "lanes over 40,000 lb", "invariants broken", "fingerprint"},
                 dayRows, {false, true, true, true, true, true, true, true, true, false, false});
    report << "\n  The client's stated figures: " << fixed(kClientStatedRearWasteIn, 1) << " in rear waste, "
           << grouped(kClientStatedCubicFeetLow) << " to " << grouped(kClientStatedCubicFeetHigh)
           << " cu ft a truck (E220, E221).\n"
           << "  The fingerprint is the same on every platform for the same plan: compare it across builds (E223).\n"
           << "  Invariants broken: I6 counts trucks over " << fixed(realDataSettings().maxUnitLoadsPerTruck, 0)
           << " unit loads; on these extracts they are three-high PAL stacks, a rule still open.\n";

    report << "\n2. August against the client's own loads (E218, E219)\n";
    long long ourUnused = 0, clientUnused = 0;
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = augustDay(run);
        const ClientDay& client = clientDay(run);
        const ClientComparison comparison = compareWithClient(*day.run, day.book, day.plan, client.laneDays);
        ourUnused += comparison.ourUnused;
        clientUnused += comparison.clientUnused;
        long long leftToM5 = 0;
        for (const ReservationUse& use : day.plan.reservations) {
            if (use.escalated) leftToM5 += use.reserved - use.built;
        }
        report << "\n" << day.label << ": " << percentOf(client.shippedLinesInDemand, client.shippedLines)
               << " of the lines his loads shipped are lines of this run's demand. " << leftToM5
               << " of our unused trucks are on lanes left to M5.\n";
        printClientComparison(report, comparison);
    }
    report << "\n  Reserved trucks left unused over the three runs, inside each run's window: client "
           << clientUnused << ", ours " << ourUnused << ". The client's stated figure over all his reservations: "
           << kClientStatedUnusedAugust << ".\n";

    report << "\n3. The client's largest August lanes, every method (E204, E205)\n\n";
    vector<vector<string>> laneRows;
    for (const auto& [run, rank] : vector<pair<size_t, size_t>>{{0, 0}, {1, 0}, {2, 0}, {2, 1}}) {
        const LargestLane largest = largestClientLane(run, rank);
        vector<string> row{augustDay(run).label, printable(largest.lane), to_string(largest.clientLoads),
                           to_string(largest.reserved), to_string(largest.routes)};
        for (const MethodScore& total : largest.totals) {
            row.push_back(to_string(total.trucks) + " / " + grouped(total.unitLoadsShipped, 1));
        }
        laneRows.push_back(row);
    }
    vector<string> laneHeader{"run", "lane", "client loads", "we reserve", "routes"};
    for (const PackingMethod method : benchmarkedMethods()) laneHeader.push_back(packingMethodName(method));
    printColumns(report, laneHeader, laneRows, {false, false, true, true, true, false, false, false, false});
    report << "  Each method: trucks / unit loads shipped, over every ship date of the lane. Each group and\n"
           << "  ship date is solved alone with its lane-date's whole reservation, so a lane split by\n"
           << "  segregation can total more trucks than it reserves; the selector shares one reservation.\n";

    report << "\n4. The methods on every August route sent to patternPacker or binPacker (E206 - E209)\n\n";
    const vector<RouteBenchmark> benchmarks = allAugustRouteBenchmarks();
    map<string, size_t> wins;
    for (const RouteBenchmark& benchmark : benchmarks) {
        ++wins[string(solveMethodName(benchmark.routedTo)) + " routes won by " + packingMethodName(*benchmark.best)];
    }
    for (const auto& [label, routes] : wins) report << "  " << label << ": " << routes << "\n";
    const optional<VarietyCrossover> crossover = varietyCrossover(benchmarks);
    if (crossover) {
        report << "  Best variety-ratio split: " << fixed(crossover->ratio, 1) << " unit loads per product; it misroutes "
               << crossover->lanesMisrouted << " of the " << crossover->lanesCompared
               << " routes where patternPacker and binPacker differ.\n";
    }
    const optional<long long> overBudget = smallestReservationOverBudget(benchmarks);
    report << "  Smallest reservation whose routed method missed the " << fixed(realDataSettings().secondsPerLane, 0)
           << " s budget: " << (overBudget ? to_string(*overBudget) : string("none did")) << ".\n";
    for (size_t run = 0; run < 3; ++run) {
        report << "\n" << augustDay(run).label << "\n";
        printBenchmark(report, *augustDay(run).run, augustDay(run).classification, augustRouteBenchmarks()[run]);
    }

    const string text = report.str();
    CHECK(all_of(text.begin(), text.end(), [](char character) {
        const auto code = static_cast<unsigned char>(character);
        return character == '\n' || (code >= 0x20 && code <= 0x7e);
    }));
    for (const char* section : {"1. Every extract", "2. August against", "3. The client's largest", "4. The methods"}) {
        CHECK(text.find(section) != string::npos);
    }
    ofstream(OB_M4_BENCHMARK_REPORT) << text;
    MESSAGE("benchmark report written to " << OB_M4_BENCHMARK_REPORT);
}

TEST_CASE("m4Benchmark: August trucks mix wide-wide with pinwheel and single rows and turn pallets where that is shorter"
          * doctest::skip()) {
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = augustDay(run);
        CAPTURE(day.label);
        RowCounts counts = rowCounts(day);
        CHECK(counts.rowsByPattern[FloorPatternKind::WideWide] > 0);
        CHECK(counts.rowsByPattern[FloorPatternKind::Pinwheel] > 0);
        CHECK(counts.rowsByPattern[FloorPatternKind::Single] > 0);
        CHECK(counts.turnedItems > 0);
    }
}

// Measured 1 Nov 2026: 24% to 29% of August trucks carry a stack two high or more.
TEST_CASE("m4Benchmark: at least a fifth of August trucks carry a stack two high" * doctest::skip()) {
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = augustDay(run);
        CAPTURE(day.label);
        CHECK(trucksCarryingStacks(day, 2) * 5 >= day.plan.trucks.size());
    }
}


TEST_CASE("m4Benchmark: the fill diagnosis is written" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    REQUIRE(m4AugustTests::augustSolutionsPresent());
    const SelectorSettings settings = realDataSettings();
    const TrailerSpec& trailer = augustDay(0).run->trailer;
    const double floorArea = trailer.interiorLengthIn * trailer.interiorWidthIn;
    const double trailerCubicIn = floorArea * trailer.stackHeightCeilingIn;
    ostringstream report;
    report << "M4 FILL DIAGNOSIS\n\n"
           << "What stops our 4-6 August trucks filling as the client's loads do. Every figure is measured on\n"
           << "the extracts held.\n";

    report << "\n1. Floor rows\n\n  Pallets " << (settings.floor.allowRotation ? "may be turned" : "are never turned")
           << ", and a row stands wide-wide, long sides across, unless another is shorter.\n  Allowed:";
    for (const FloorPatternKind pattern : settings.floor.allowedPatterns) report << " " << floorPatternName(pattern);
    report << ".\n\n";
    const array<FloorPatternKind, 6> patterns{FloorPatternKind::WideWide,    FloorPatternKind::WideNarrow,
                                              FloorPatternKind::NarrowNarrow, FloorPatternKind::ThreeAcross,
                                              FloorPatternKind::Pinwheel,     FloorPatternKind::Single};
    vector<string> patternHeader{"run", "trucks", "rows"};
    for (const FloorPatternKind pattern : patterns) patternHeader.push_back(floorPatternName(pattern));
    patternHeader.insert(patternHeader.end(), {"items turned", "footprints / length used"});
    vector<vector<string>> patternRows;
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = augustDay(run);
        RowCounts counts = rowCounts(day);
        vector<string> row{day.label, grouped(day.plan.trucks.size()), grouped(counts.rows)};
        for (const FloorPatternKind pattern : patterns) row.push_back(grouped(counts.rowsByPattern[pattern]));
        row.push_back(grouped(counts.turnedItems));
        row.push_back(percentOf(counts.footprintLengthIn, counts.usedLengthIn));
        patternRows.push_back(row);
    }
    vector<bool> rightAligned(patternHeader.size(), true);
    rightAligned.front() = false;
    printColumns(report, patternHeader, patternRows, rightAligned);
    report << "  Footprints / length used: the length the trucks' footprints would take packed edge to edge\n"
           << "  across the width, over the length their rows take. Section 6 shows one truck's rows.\n";

    report << "\n2. Stacks two high or more\n\n";
    vector<vector<string>> stackRows;
    map<string, pair<set<string>, set<string>>> productsByPallet;  // products, and those that carry themselves
    map<string, pair<double, double>> unitLoadsByPallet;            // stacked two high or more, all
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = augustDay(run);
        double onTrucks = 0.0, stackedOnTrucks = 0.0;
        for (const PlannedTruck& truck : day.plan.trucks) {
            for (const size_t index : truck.items) {
                const SelectionItem& item = day.items.items[index];
                onTrucks += item.unitLoads;
                if (item.lineIndices.size() >= 2) stackedOnTrucks += item.unitLoads;
            }
        }
        const size_t stacked = trucksCarryingStacks(day, 2);
        stackRows.push_back({day.label, grouped(day.plan.trucks.size()), grouped(stacked),
                             percentOf(stacked, day.plan.trucks.size()), grouped(stackedOnTrucks), percentOf(stackedOnTrucks, onTrucks)});
        for (const JoinedLine& line : day.run->join.lines) {
            if (!line.product) continue;
            const string product = line.product->id + " " + line.product->uom;
            auto& [products, selfStacking] = productsByPallet[line.product->pallet_id];
            if (!products.insert(product).second) continue;
            const UnitLoad load = buildUnitLoad(line, day.run->params);
            if (load.error == UnitLoadError::None && canStack(load, load, day.run->params, trailer.stackHeightCeilingIn).isFeasible) {
                selfStacking.insert(product);
            }
        }
        for (const SelectionItem& item : day.items.items) {
            auto& [stackedUnitLoads, allUnitLoads] = unitLoadsByPallet[day.run->join.lines[item.lineIndices.front()].product->pallet_id];
            allUnitLoads += item.unitLoads;
            if (item.lineIndices.size() >= 2) stackedUnitLoads += item.unitLoads;
        }
    }
    printColumns(report, {"run", "trucks", "with a stack 2+ high", "share", "unit loads on trucks in such stacks", "share"},
                 stackRows, {false, true, true, true, true, true});
    report << "\n";
    vector<vector<string>> palletRows;
    for (const auto& [pallet, products] : productsByPallet) {
        const auto& [stackedUnitLoads, allUnitLoads] = unitLoadsByPallet[pallet];
        palletRows.push_back({printable(pallet), grouped(products.first.size()), grouped(products.second.size()),
                              grouped(stackedUnitLoads), percentOf(stackedUnitLoads, allUnitLoads)});
    }
    printColumns(report, {"pallet", "products", "can carry themselves", "unit loads M2 stacked 2+ high", "of all its unit loads"},
                 palletRows, {false, true, true, true, true});
    report << "  Over the three runs. A product can carry itself when canStack allows one of its unit loads on another.\n";

    report << "\n3. What stops our trucks filling\n\n";
    vector<vector<string>> limitRows;
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = augustDay(run);
        const vector<char> leavingGoods = routesLeavingGoods(day);
        array<size_t, 2> trucks{};
        array<double, 2> cube{}, floor{}, rearWaste{};
        for (const PlannedTruck& truck : day.plan.trucks) {
            const size_t side = leavingGoods[truck.routeIndex] ? 0 : 1;
            ++trucks[side];
            cube[side] += truck.floor.cubeFraction;
            floor[side] += truck.areaSqIn / floorArea;
            rearWaste[side] += truck.floor.rearWasteIn;
        }
        double clientCargo = 0.0;
        long long clientLoads = 0;
        for (const ClientLaneDay& laneDay : clientDay(run).laneDays) {
            clientCargo += laneDay.cargoCubicIn;
            clientLoads += laneDay.loads;
        }
        vector<string> row{day.label};
        for (size_t side = 0; side < 2; ++side) {
            const double count = static_cast<double>(max<size_t>(trucks[side], 1));
            row.insert(row.end(), {grouped(trucks[side]), percentOf(cube[side], count), percentOf(floor[side], count),
                                   fixed(rearWaste[side] / count, 1)});
        }
        row.push_back(percentOf(clientCargo / trailerCubicIn, static_cast<double>(max<long long>(clientLoads, 1))));
        limitRows.push_back(row);
    }
    printColumns(report,
                 {"run", "route left goods: trucks", "cube", "floor", "rear waste in", "route ran out: trucks", "cube",
                  "floor", "rear waste in", "client cube"},
                 limitRows, {false, true, true, true, true, true, true, true, true, true});
    report << "  A route left goods when some of its goods stayed over a full truck: over the reservation, off\n"
           << "  the rows, or over the unit-load cap. Its trucks stopped for want of room. A route that ran out\n"
           << "  shipped every good the ship-date rules let reach its date: what limits its trucks is those goods.\n";
    size_t benchmarkedRoutes = 0, sameEveryMethod = 0, sameAndShippingAll = 0;
    for (const RouteBenchmark& benchmark : allAugustRouteBenchmarks()) {
        ++benchmarkedRoutes;
        const MethodScore& first = benchmark.scores.front();
        const bool same = all_of(benchmark.scores.begin(), benchmark.scores.end(), [&](const MethodScore& score) {
            return score.ran && score.trucks == first.trucks
                   && fabs(score.unitLoadsShipped - first.unitLoadsShipped) <= kQuantityEpsilon;
        });
        if (!same) continue;
        ++sameEveryMethod;
        if (first.unitLoadsShipped >= benchmark.unitLoads - kQuantityEpsilon) ++sameAndShippingAll;
    }
    report << "  Of the " << benchmarkedRoutes << " August routes sent to patternPacker or binPacker, every method builds the\n"
           << "  same trucks and ships the same unit loads on " << sameEveryMethod << ", and " << sameAndShippingAll
           << " of those ship every unit load they have.\n";

    report << "\n4. The client's loads against each line's ship date (DATFR_TA)\n\n";
    array<ClientLinesAgainstOurs, 3> against;
    set<string> states;
    for (size_t run = 0; run < 3; ++run) {
        against[run] = clientLinesAgainstOurs(augustDay(run), clientDay(run));
        for (const auto& [state, unitLoads] : against[run].unitLoadsByOurState) states.insert(state);
        double early = 0.0, onDate = 0.0, late = 0.0;
        report << "  " << augustDay(run).label << ", unit loads by days from DATFR_TA to the load's date:";
        for (const auto& [days, unitLoads] : against[run].unitLoadsByDaysFromShipDate) {
            report << " " << (days > 0 ? "+" : "") << days << ": " << grouped(unitLoads) << ";";
            if (days < 0) {
                early += unitLoads;
            } else if (days == 0) {
                onDate += unitLoads;
            } else {
                late += unitLoads;
            }
        }
        report << "\n    before DATFR_TA " << percentOf(early, against[run].unitLoads) << ", on it "
               << percentOf(onDate, against[run].unitLoads) << ", after it " << percentOf(late, against[run].unitLoads)
               << "; after DATTO_TA " << percentOf(against[run].unitLoadsAfterDeliveryDate, against[run].unitLoads) << "\n";
    }
    report << "\n  Where the unit loads the client shipped are in our plan:\n\n";
    vector<vector<string>> stateRows;
    for (const string& state : states) {
        vector<string> row{state};
        for (size_t run = 0; run < 3; ++run) {
            const auto found = against[run].unitLoadsByOurState.find(state);
            const double unitLoads = found == against[run].unitLoadsByOurState.end() ? 0.0 : found->second;
            row.insert(row.end(), {grouped(unitLoads), percentOf(unitLoads, against[run].unitLoads)});
        }
        stateRows.push_back(row);
    }
    printColumns(report, {"our plan", "Aug4", "share", "Aug5", "share", "Aug6", "share"}, stateRows,
                 {false, true, true, true, true, true, true});
    report << "  A good reaches a reserved lane-date on its own ship date, carried on from an earlier date that\n"
           << "  could not take it (carryUnshippedForward), or pulled forward to fill room, due at most "
           << m4AugustTests::kShippedMaxDaysPulledForward << " days later\n"
           << "  (pullForwardToFill). Section 8 names what keeps the rest off.\n";
    report << "\n  Matching: is a lane key or the CTL window keeping his goods off our trucks?\n";
    for (size_t run = 0; run < 3; ++run) {
        CAPTURE(run);
        report << "\n  " << augustDay(run).label << "\n";
        const LaneKeyMisses misses = printClientMatching(report, augustDay(run), clientDay(run));
        CHECK(misses.loads == 0);
        CHECK(misses.unitLoadsOffTheirLane == 0.0);
    }
    report << "\n  Every unit load above rides a load on its own line's lane, and every placeholder row inside\n"
           << "  the window that names his load's lane and date is a reservation in our book (both checked):\n"
           << "  the lane key matches his lanes. The CTL window's share is his loads outside our window, listed\n"
           << "  per run. The rest reach other dates because his loads carry lines due days earlier or later\n"
           << "  (the days table); section 7 prices each ship-date rule.\n";

    const HeldDay& aug6 = augustDay(2);
    long long aug6LaneBuilt = 0, aug6LaneReserved = 0;
    for (const ReservationUse& use : aug6.plan.reservations) {
        const Reservation& reservation = aug6.book.reservations[use.reservationIndex];
        if (reservation.origin != "2028" || reservation.destination != "2574" || reservation.shipCondition != "TL") continue;
        aug6LaneBuilt += use.built;
        aug6LaneReserved += use.reserved;
    }
    report << "\n5. Aug6, 2028 -> 2574 TL: " << aug6LaneBuilt << " trucks of the " << aug6LaneReserved
           << " reserved\n\n";
    printLane(report, aug6, clientDay(2), "2028", "2574", "TL");
    report << "  Section 8 names the cause of each reserved truck left unbuilt, on this lane and every other.\n";

    report << "\n6. Rows of the first Aug6 2028 -> 2574 TL truck on 10 Aug\n\n";
    const auto shown = find_if(aug6.plan.trucks.begin(), aug6.plan.trucks.end(), [&](const PlannedTruck& truck) {
        const GroupKey& key = aug6.run->segregation.groups[truck.groupIndex].key;
        return key.locationFrom == "2028" && key.locationTo == "2574" && key.shipCondition == "TL" && truck.shipDate == "2026-08-10";
    });
    REQUIRE(shown != aug6.plan.trucks.end());
    report << "  " << shown->items.size() << " positions in " << shown->floor.rows.size() << " rows, "
           << fixed(shown->floor.usedLengthIn, 1) << " in used, " << fixed(shown->floor.rearWasteIn, 1) << " in rear waste.\n\n";
    printTruckRows(report, aug6, *shown, settings.floor);

    report << "\n7. What each ship-date rule is worth: the same goods planned under each\n\n";
    SelectorSettings fourThreads = settings;
    fourThreads.threads = 4;
    SelectorSettings uncapped = fourThreads;
    uncapped.maxUnitLoadsPerTruck = 1e9;
    vector<vector<string>> costRows;
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = augustDay(run);
        const pair<const char*, ShipDateRules> looser[] = {{"own ship date only (both rules off)", {false, false}},
                                                           {"carryUnshippedForward only", {true, false}},
                                                           {"pullForwardToFill only", {false, true}}};
        for (const auto& [label, rules] : looser) {
            costRows.push_back(ruleCostRow(label, day, day.items, plannedUnder(day, rules, fourThreads),
                                           settings.maxUnitLoadsPerTruck));
        }
        costRows.push_back(ruleCostRow("as planned: both rules on", day, day.items, day.plan, settings.maxUnitLoadsPerTruck));
        costRows.push_back(ruleCostRow("both rules, no unit-load cap", day, day.items,
                                       plannedUnder(day, m4AugustTests::kShippedShipDateRules, uncapped),
                                       settings.maxUnitLoadsPerTruck));
        const ClientDay& client = clientDay(run);
        double clientCargoCubicIn = 0.0;
        long long clientLoads = 0;
        for (const ClientLaneDay& laneDay : client.laneDays) {
            clientCargoCubicIn += laneDay.cargoCubicIn;
            clientLoads += laneDay.loads;
        }
        const ClientComparison comparison = compareWithClient(*day.run, day.book, day.plan, client.laneDays);
        costRows.push_back({day.label, "the client's loads", grouped(clientLoads), grouped(comparison.clientUnused),
                            grouped(clientCargoCubicIn / 1728.0 / static_cast<double>(max<long long>(clientLoads, 1))),
                            fixed(kClientStatedRearWasteIn, 1) + " stated", grouped(against[run].unitLoads), "-"});
    }
    printColumns(report, {"run", "plan", "trucks", "unused", "cu ft a truck", "rear waste in", "unit loads shipped",
                          "invariants broken"},
                 costRows, {false, false, true, true, true, true, true, false});
    report << "  Each row plans the same goods with the real planner, classifier and selector:\n"
           << "  - carryUnshippedForward (Tom, 28 Oct): a good its date's trucks do not take - none reserved,\n"
           << "    or the reserved ones full - rides the lane's next reserved date, ahead of that date's goods.\n"
           << "  - pullForwardToFill (Tom, 16 Oct): a reserved truck with room takes goods due up to "
           << m4AugustTests::kShippedMaxDaysPulledForward << " days\n"
           << "    later (maxDaysPulledForward), once available, after its own date's goods.\n"
           << "  - no unit-load cap: both rules, but no truck stopped at " << fixed(settings.maxUnitLoadsPerTruck, 0)
           << " unit loads (I6). Its broken invariants\n"
           << "    are the trucks over that cap.\n"
           << "  The client's row: Truck Builder's loads, the reserved trucks it left empty, the cube of the\n"
           << "  lines it shipped, and the rear waste it states, which no file holds to measure.\n";

    report << "\n8. What is left: every reserved truck left unbuilt, and the cube each truck lacks\n";
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = augustDay(run);
        report << "\n  " << day.label << ", reserved trucks left unbuilt:\n\n";
        const map<string, long long> trucksByCause = printUnbuiltTrucks(report, day);
        long long unbuilt = 0;
        for (const auto& [cause, trucks] : trucksByCause) unbuilt += trucks;
        CHECK(unbuilt == day.plan.penaltyExposureTrucks);
        report << "\n  " << day.label << ": " << unbuilt << " unbuilt;";
        for (const auto& [cause, trucks] : trucksByCause) report << " " << cause << " " << trucks << ";";
        report << "\n";
    }
    report << "\n  Causes: \"no goods left on the lane\" - every good of the lane is on a truck, so nothing could\n"
           << "  fill it; \"goods left on the lane, none allowed that date\" - the lane still has goods, but each is\n"
           << "  due over the pull-forward limit after the truck, not available by its date, fits no truck, or\n"
           << "  is left to M5; \"left to M5\" - the lane-date is over m5Threshold and M4 plans none of it.\n"
           << "  \"Goods allowed that date were not loaded\" or \"a group left goods over a full truck\" would be\n"
           << "  the planner's own shortfall.\n";

    report << "\n  The cube each truck lacks, in cu ft a truck, by what stopped it. The trailer holds "
           << grouped(trailerCubicIn / 1728.0) << " cu ft;\n"
           << "  what it does not carry is the rear left empty, the gaps across the width beside its rows, and\n"
           << "  the air above its stacks, under the " << fixed(trailer.stackHeightCeilingIn, 0) << " in ceiling.\n\n";
    vector<vector<string>> lostRows;
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = augustDay(run);
        LostCube all, forRoom;
        for (const auto& [stop, lost] : lostCubeByStop(day)) {
            lostRows.push_back(lostCubeRow(day.label, stop, lost, day.plan.trucks.size(), trailer));
            all.add(lost);
            if (stop.rfind("stopped by", 0) == 0) forRoom.add(lost);
        }
        lostRows.push_back(lostCubeRow(day.label, "stopped for room, floor or weight", forRoom, day.plan.trucks.size(), trailer));
        lostRows.push_back(lostCubeRow(day.label, "every truck", all, day.plan.trucks.size(), trailer));
        CHECK(all.trucks == day.plan.trucks.size());
    }
    const double statedLostCubicFeet = trailerCubicIn / 1728.0 - kClientStatedCubicFeetHigh;
    const double statedRearCubicFeet = kClientStatedRearWasteIn * trailer.interiorWidthIn * trailer.stackHeightCeilingIn / 1728.0;
    lostRows.push_back({"client", "stated: 3,400 cu ft, 14.5 in at the rear", "-", "-", grouped(kClientStatedCubicFeetHigh),
                        grouped(statedLostCubicFeet), grouped(statedRearCubicFeet), fixed(kClientStatedRearWasteIn, 1), "-",
                        grouped(statedLostCubicFeet - statedRearCubicFeet) + " both"});
    printColumns(report, {"run", "what stopped the truck", "trucks", "share", "cargo", "lacks", "rear", "rear in",
                          "across width", "above stacks"},
                 lostRows, {false, false, true, true, true, true, true, true, true, true});
    report << "  A truck ran out when its route shipped every good the ship-date rules let reach its date, for\n"
           << "  the cause named as for an unbuilt truck. One\n"
           << "  that left goods stopped on the limit nearer full: weight when its payload share is at least the\n"
           << "  share of the length its rows take, otherwise floor. The client's split is his two stated\n"
           << "  figures (Rev 8 section 1): no file holds his rows or stacks to split them further.\n";

    const string text = report.str();
    CHECK(all_of(text.begin(), text.end(), [](char character) {
        const auto code = static_cast<unsigned char>(character);
        return character == '\n' || (code >= 0x20 && code <= 0x7e);
    }));
    for (const char* section : {"1. Floor rows", "2. Stacks", "3. What stops", "4. The client's loads", "5. Aug6", "6. Rows",
                                "7. What each ship-date rule is worth", "8. What is left"}) {
        CHECK(text.find(section) != string::npos);
    }
    ofstream(OB_M4_FILL_DIAGNOSIS) << text;
    MESSAGE("fill diagnosis written to " << OB_M4_FILL_DIAGNOSIS);
}

} // TEST_SUITE
