#include "laneClassifier.hpp"
#include "pipeline.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
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
    if (trailer.stackPositions <= 0 || !isfinite(trailer.weightLimitLb) || trailer.weightLimitLb <= 0.0
        || !isfinite(trailer.interiorLengthIn) || trailer.interiorLengthIn <= 0.0
        || !isfinite(trailer.interiorWidthIn) || trailer.interiorWidthIn <= 0.0
        || !isfinite(trailer.stackHeightCeilingIn) || trailer.stackHeightCeilingIn <= 0.0) {
        throw invalid_argument("Lane classifier: trailer " + trailer.trailerCode
                               + " has a positions, payload or size that is not positive and finite");
    }
}

long long demandTrucks(const vector<size_t>& itemIndices, const SelectionItems& items,
                       const TrailerSpec& trailer) {
    double positions = 0.0, weightLb = 0.0, cubeCubicIn = 0.0;
    for (const size_t index : itemIndices) {
        const SelectionItem& item = items.items[index];
        positions += item.positions;
        weightLb += item.weightLb;
        cubeCubicIn += item.cubeCubicIn;
    }
    const double trailerCube = trailer.interiorLengthIn * trailer.interiorWidthIn
                               * trailer.stackHeightCeilingIn;
    const double trucks = max({positions / trailer.stackPositions, weightLb / trailer.weightLimitLb,
                               cubeCubicIn / trailerCube});
    return static_cast<long long>(ceil(trucks - kQuantityEpsilon));
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

} // anonymous namespace

LaneClassification classifyLanes(const PipelineResult& run, const ReservationBook& book,
                                 const SelectionItems& items,
                                 const LaneClassifierSettings& settings) {
    validate(settings, run.trailer);
    const vector<SegregatedGroup>& groups = run.segregation.groups;

    unordered_map<string, vector<size_t>> groupsByLane;
    groupsByLane.reserve(groups.size());
    for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        groupsByLane[laneKeyOf(groups[groupIndex])].push_back(groupIndex);
    }

    // book.reservations is sorted by lane then date, so each lane's dates come out ascending.
    unordered_map<string, vector<string>> reservedDatesByLane;
    for (const Reservation& reservation : book.reservations) {
        reservedDatesByLane[laneKey(reservation.origin, reservation.destination,
                                    reservation.shipCondition)]
            .push_back(reservation.shipDate);
    }

    LaneClassification classification;
    // Ordered, so routes come out by group and then date whatever the item order.
    map<pair<size_t, string>, vector<size_t>> itemsByRoute;
    for (size_t itemIndex = 0; itemIndex < items.items.size(); ++itemIndex) {
        const SelectionItem& item = items.items[itemIndex];
        if (item.groupIndex >= groups.size()) {
            throw invalid_argument("Item " + to_string(itemIndex) + " names group "
                                   + to_string(item.groupIndex) + " of "
                                   + to_string(groups.size()));
        }
        if (item.timing == ShipTiming::Future) {
            classification.itemsLeftOut.push_back({itemIndex, UnroutedItemReason::AfterWindow});
        } else if (item.unavailableInWindow) {
            classification.itemsLeftOut.push_back({itemIndex, UnroutedItemReason::UnavailableInWindow});
        } else {
            const auto laneDates = reservedDatesByLane.find(laneKeyOf(groups[item.groupIndex]));
            const vector<string>* dates =
                laneDates == reservedDatesByLane.end() ? nullptr : &laneDates->second;
            itemsByRoute[{item.groupIndex, routeDate(item, book.window, dates)}].push_back(itemIndex);
        }
    }
    // A reservation with nothing to load still needs a route, to be reported as unused.
    for (const auto& [lane, dates] : reservedDatesByLane) {
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
        route.demandAboveReservation = route.demandTrucks > route.reservedTrucks;
        route.shortLane = route.demandTrucks < route.reservedTrucks;

        if (itemIndices.empty()) {
            route.method = SolveMethod::NoDemand;
        } else if (route.reservedTrucks == 0) {
            route.method = SolveMethod::Unrouted;
        } else {
            route.method = route.reservedTrucks == 1 || route.demandAboveReservation
                ? SolveMethod::Knapsack
                : byVariety(route.unitLoads, route.distinctProducts, settings);
            if (route.reservedTrucks > settings.m5Threshold) {
                route.methodBeforeEscalation = route.method;
                route.method = SolveMethod::M5;
            }
        }
        route.itemIndices = std::move(itemIndices);
        classification.routes.push_back(std::move(route));
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
