#include "laneClassifier.hpp"
#include "pipeline.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using namespace std;

namespace ob {

namespace {

// '\x1f' cannot occur in a location code or ship condition, so distinct lanes never share a key.
string laneKey(const string& origin, const string& destination, const string& shipCondition) {
    return origin + '\x1f' + destination + '\x1f' + shipCondition;
}

string laneKeyOf(const SegregatedGroup& group) {
    return laneKey(group.key.locationFrom, group.key.locationTo, group.key.shipCondition);
}

void validate(const LaneClassifierSettings& settings, const TrailerSpec& trailer) {
    if (settings.m5Threshold < 2) {
        throw invalid_argument("Lane classifier: m5Threshold must be at least 2, is "
                               + to_string(settings.m5Threshold));
    }
    if (!isfinite(settings.varietyRatio) || settings.varietyRatio <= 0.0) {
        throw invalid_argument("Lane classifier: varietyRatio must be positive and finite");
    }
    // These divide the demand into trucks.
    if (!isfinite(trailer.weightLimitLb) || trailer.weightLimitLb <= 0.0
        || !isfinite(trailer.interiorLengthIn) || trailer.interiorLengthIn <= 0.0
        || !isfinite(trailer.interiorWidthIn) || trailer.interiorWidthIn <= 0.0) {
        throw invalid_argument("Lane classifier: trailer " + trailer.trailerCode
                               + " has a payload or size that is not positive and finite");
    }
}

long long demandTrucks(const vector<size_t>& itemIndices, const SelectionItems& items, const TrailerSpec& trailer) {
    TruckShares total;
    for (const size_t index : itemIndices) total += truckSharesOf(items.items[index], trailer);
    return static_cast<long long>(ceil(total.largest() - kQuantityEpsilon));
}

// One truckload of a route's goods, as its lane-date's reservation is dealt out.
struct EstimatedLoad {
    size_t routeIndex = 0;
    size_t sequence = 0;    // its place among the route's loads
    bool overdue = false;   // of its most urgent item
    string shipDate;
    int priority = 0;
    double fill = 0.0;      // the largest share of a truck its goods due by the route's date take
};

auto urgencyOf(const EstimatedLoad& load) { return make_tuple(!load.overdue, cref(load.shipDate), load.priority); }

// The route's items in order, cut into a new load wherever the next item would take the open one
// past a truck on floor area or payload. Goods due after the route's date, pulled forward
// (shipDatePlanner), only fill room: they never make a load the fuller one.
void appendEstimatedLoads(size_t routeIndex, const LaneRoute& route, const SelectionItems& items,
                          const TrailerSpec& trailer, vector<EstimatedLoad>& loads) {
    optional<EstimatedLoad> open;
    TruckShares openShares, openDueShares;
    size_t sequence = 0;
    for (const size_t index : route.itemIndices) {
        const SelectionItem& item = items.items[index];
        const TruckShares shares = truckSharesOf(item, trailer);
        if (open && (openShares.floor + shares.floor > 1.0 + kQuantityEpsilon
                     || openShares.weight + shares.weight > 1.0 + kQuantityEpsilon)) {
            open->fill = openDueShares.largest();
            loads.push_back(std::move(*open));
            open.reset();
            openShares = {};
            openDueShares = {};
        }
        EstimatedLoad itemLoad{routeIndex, sequence, item.timing == ShipTiming::Overdue, item.shipDate, item.priority, 0.0};
        if (!open) {
            open = std::move(itemLoad);
            ++sequence;
        } else if (urgencyOf(itemLoad) < urgencyOf(*open)) {
            open->overdue = itemLoad.overdue;
            open->shipDate = itemLoad.shipDate;
            open->priority = itemLoad.priority;
        }
        openShares += shares;
        if (item.shipDate <= route.shipDate) openDueShares += shares;
    }
    if (open) {
        open->fill = openDueShares.largest();
        loads.push_back(std::move(*open));
    }
}

// Rule 2: each lane-date's reserved trucks go one estimated load at a time, most urgent first
// and, among equally urgent, fullest first, so a group with a thin load never takes a truck
// another group's urgent or fuller goods need (spec: higher priority ships first; filling the
// truck wins). Trucks beyond every load go to the route holding the most urgent load, or to the
// first route when none has demand, so the shares always add up to the reservation.
void allocateReservations(vector<LaneRoute>& routes, const vector<SegregatedGroup>& groups,
                          const SelectionItems& items, const TrailerSpec& trailer) {
    map<pair<string, string>, vector<size_t>> routesByLaneDate;
    for (size_t routeIndex = 0; routeIndex < routes.size(); ++routeIndex) {
        const LaneRoute& route = routes[routeIndex];
        if (route.reservedTrucks > 0) {
            routesByLaneDate[{laneKeyOf(groups[route.groupIndex]), route.shipDate}].push_back(routeIndex);
        }
    }
    for (const auto& [laneDate, routeIndices] : routesByLaneDate) {
        vector<EstimatedLoad> loads;
        for (const size_t routeIndex : routeIndices) {
            appendEstimatedLoads(routeIndex, routes[routeIndex], items, trailer, loads);
        }
        if (loads.empty()) {
            routes[routeIndices.front()].trucksAllocated = routes[routeIndices.front()].reservedTrucks;
            continue;
        }
        sort(loads.begin(), loads.end(), [](const EstimatedLoad& left, const EstimatedLoad& right) {
            return tuple_cat(urgencyOf(left), make_tuple(-left.fill, left.routeIndex, left.sequence))
                   < tuple_cat(urgencyOf(right), make_tuple(-right.fill, right.routeIndex, right.sequence));
        });
        const long long reserved = routes[routeIndices.front()].reservedTrucks;
        const size_t dealt = min(loads.size(), static_cast<size_t>(reserved));
        for (size_t load = 0; load < dealt; ++load) ++routes[loads[load].routeIndex].trucksAllocated;
        routes[loads.front().routeIndex].trucksAllocated += reserved - static_cast<long long>(dealt);
    }
}

SolveMethod byVariety(double unitLoads, size_t distinctProducts, const LaneClassifierSettings& settings) {
    return unitLoads / static_cast<double>(distinctProducts) > settings.varietyRatio
               ? SolveMethod::PatternPacker : SolveMethod::BinPacker;
}

// laneDates: the lane's reserved dates, ascending.
string routeDate(const SelectionItem& item, const PlanningWindow& window,
                 const vector<string>* laneDates) {
    const string earliest = max({item.shipDate, window.firstShipDate, item.availDate});
    if (item.timing == ShipTiming::Overdue && laneDates) {
        const auto firstReserved = lower_bound(laneDates->begin(), laneDates->end(), earliest);
        if (firstReserved != laneDates->end()) return *firstReserved;
    }
    return earliest;
}

// book.reservations is sorted by lane then date, so each lane's dates come out ascending.
unordered_map<string, vector<string>> reservedDatesByLane(const ReservationBook& book) {
    unordered_map<string, vector<string>> datesByLane;
    for (const Reservation& reservation : book.reservations) {
        datesByLane[laneKey(reservation.origin, reservation.destination, reservation.shipCondition)].push_back(
            reservation.shipDate);
    }
    return datesByLane;
}

void requireRunGroup(const SelectionItem& item, size_t itemIndex, size_t groupCount) {
    if (item.groupIndex >= groupCount) {
        throw invalid_argument("Item " + to_string(itemIndex) + " names group " + to_string(item.groupIndex) + " of "
                               + to_string(groupCount));
    }
}

} // anonymous namespace

TruckShares truckSharesOf(const SelectionItem& item, const TrailerSpec& trailer) {
    return {item.footprintLongIn * item.footprintShortIn / (trailer.interiorLengthIn * trailer.interiorWidthIn),
            item.weightLb / trailer.weightLimitLb};
}

ItemRouting routeItems(const PipelineResult& run, const ReservationBook& book, const SelectionItems& items) {
    const vector<SegregatedGroup>& groups = run.segregation.groups;
    const unordered_map<string, vector<string>> reservedDates = reservedDatesByLane(book);
    ItemRouting routing;
    routing.routeDates.resize(items.items.size());
    for (size_t itemIndex = 0; itemIndex < items.items.size(); ++itemIndex) {
        const SelectionItem& item = items.items[itemIndex];
        requireRunGroup(item, itemIndex, groups.size());
        if (item.timing == ShipTiming::Future) {
            routing.itemsLeftOut.push_back({itemIndex, UnroutedItemReason::AfterWindow});
        } else if (item.unavailableInWindow) {
            routing.itemsLeftOut.push_back({itemIndex, UnroutedItemReason::UnavailableInWindow});
        } else {
            const auto laneDates = reservedDates.find(laneKeyOf(groups[item.groupIndex]));
            const vector<string>* dates = laneDates == reservedDates.end() ? nullptr : &laneDates->second;
            routing.routeDates[itemIndex] = routeDate(item, book.window, dates);
        }
    }
    return routing;
}

LaneClassification classifyLanes(const PipelineResult& run, const ReservationBook& book,
                                 const SelectionItems& items,
                                 const LaneClassifierSettings& settings) {
    return classifyLanes(run, book, items, settings, routeItems(run, book, items));
}

LaneClassification classifyLanes(const PipelineResult& run, const ReservationBook& book,
                                 const SelectionItems& items, const LaneClassifierSettings& settings,
                                 const ItemRouting& routing) {
    validate(settings, run.trailer);
    if (routing.routeDates.size() != items.items.size()) {
        throw invalid_argument("Lane classifier: " + to_string(routing.routeDates.size()) + " route dates for "
                               + to_string(items.items.size()) + " items");
    }
    const vector<SegregatedGroup>& groups = run.segregation.groups;

    unordered_map<string, vector<size_t>> groupsByLane;
    groupsByLane.reserve(groups.size());
    for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        groupsByLane[laneKeyOf(groups[groupIndex])].push_back(groupIndex);
    }
    const unordered_map<string, vector<string>> reservedDates = reservedDatesByLane(book);

    LaneClassification classification;
    classification.itemsLeftOut = routing.itemsLeftOut;
    // Ordered, so routes come out by group and then date whatever the item order.
    map<pair<size_t, string>, vector<size_t>> itemsByRoute;
    for (size_t itemIndex = 0; itemIndex < items.items.size(); ++itemIndex) {
        requireRunGroup(items.items[itemIndex], itemIndex, groups.size());
        const string& date = routing.routeDates[itemIndex];
        if (!date.empty()) itemsByRoute[{items.items[itemIndex].groupIndex, date}].push_back(itemIndex);
    }
    // A reservation with nothing to load still needs a route, to be reported as unused.
    for (const auto& [lane, dates] : reservedDates) {
        const auto laneGroups = groupsByLane.find(lane);
        if (laneGroups == groupsByLane.end()) continue;
        for (const size_t groupIndex : laneGroups->second) {
            for (const string& date : dates) itemsByRoute[{groupIndex, date}];
        }
    }

    classification.routes.reserve(itemsByRoute.size());
    for (auto& [key, itemIndices] : itemsByRoute) {
        const SegregatedGroup& group = groups[key.first];
        const vector<size_t>& laneGroups = groupsByLane.at(laneKeyOf(group));

        LaneRoute route;
        route.groupIndex = key.first;
        route.shipDate = key.second;
        const Reservation* reservation = findReservation(
            book, group.key.locationFrom, group.key.locationTo, group.key.shipCondition, route.shipDate);
        route.reservedTrucks = reservation ? reservation->trucks : 0;
        route.groupsSharingReservation = laneGroups.size();
        if (group.splitReason == SplitReason::DoNotMix) {
            route.conflict = ConflictReason::DoNotMix;
        } else if (laneGroups.size() > 1) {
            route.conflict = ConflictReason::SegregationSplit;
        }

        unordered_set<string> products;
        for (const size_t index : itemIndices) {
            const SelectionItem& item = items.items[index];
            route.unitLoads += item.unitLoads;
            for (const size_t lineIndex : item.lineIndices) {
                products.insert(run.join.lines[lineIndex].str->matnr);
            }
        }
        route.distinctProducts = products.size();
        route.demandTrucks = demandTrucks(itemIndices, items, run.trailer);
        route.itemIndices = std::move(itemIndices);
        classification.routes.push_back(std::move(route));
    }

    allocateReservations(classification.routes, groups, items, run.trailer);
    for (LaneRoute& route : classification.routes) {
        route.demandAboveReservation = route.demandTrucks > route.trucksAllocated;
        route.shortLane = route.demandTrucks < route.trucksAllocated;
        if (route.itemIndices.empty()) {
            route.method = SolveMethod::NoDemand;
        } else if (route.reservedTrucks == 0) {
            route.method = SolveMethod::Unrouted;
        } else {
            route.method = route.trucksAllocated <= 1 || route.demandAboveReservation
                ? SolveMethod::Knapsack
                : byVariety(route.unitLoads, route.distinctProducts, settings);
            if (route.reservedTrucks > settings.m5Threshold) {
                route.methodBeforeEscalation = route.method;
                route.method = SolveMethod::M5;
            }
        }
    }
    return classification;
}

const char* solveMethodName(SolveMethod method) {
    switch (method) {
    case SolveMethod::Knapsack: return "knapsack";
    case SolveMethod::PatternPacker: return "patternPacker";
    case SolveMethod::BinPacker: return "binPacker";
    case SolveMethod::M5: return "M5";
    case SolveMethod::Unrouted: return "unrouted";
    case SolveMethod::NoDemand: return "noDemand";
    }
    return "unknown";
}

} // namespace ob
