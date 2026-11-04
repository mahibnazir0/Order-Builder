#include "shipDatePlanner.hpp"
#include "isoDate.hpp"
#include "pipeline.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

using namespace std;

namespace ob {

namespace {

// Goods the next date's trucks could take. Not an unfittable good (no truck takes it), one left to
// M5 (it plans them), or one on a lane-date that failed (the run must report that, not hide it).
bool carriesOver(LeftOverReason reason) {
    switch (reason) {
    case LeftOverReason::NoReservation:
    case LeftOverReason::OverReservation:
    case LeftOverReason::FloorEviction:
    case LeftOverReason::UnitLoadCap: return true;
    default: return false;
    }
}

// The run's lanes, each with its reserved trucks by date and its items in selection order.
struct Lanes {
    vector<size_t> laneOfGroup;
    vector<map<string, long long>> reservedTrucks;  // ascending by date
    vector<vector<size_t>> items;
};

Lanes lanesOf(const PipelineResult& run, const ReservationBook& book, const SelectionItems& items) {
    const vector<SegregatedGroup>& groups = run.segregation.groups;
    map<tuple<string, string, string>, size_t> laneIndex;
    Lanes lanes;
    lanes.laneOfGroup.reserve(groups.size());
    for (const SegregatedGroup& group : groups) {
        const GroupKey& key = group.key;
        lanes.laneOfGroup.push_back(
            laneIndex.try_emplace({key.locationFrom, key.locationTo, key.shipCondition}, laneIndex.size()).first->second);
    }
    lanes.reservedTrucks.resize(laneIndex.size());
    lanes.items.resize(laneIndex.size());
    for (const Reservation& reservation : book.reservations) {
        const auto lane = laneIndex.find({reservation.origin, reservation.destination, reservation.shipCondition});
        if (lane != laneIndex.end()) lanes.reservedTrucks[lane->second][reservation.shipDate] = reservation.trucks;
    }
    for (size_t index = 0; index < items.items.size(); ++index) {
        lanes.items[lanes.laneOfGroup[items.items[index].groupIndex]].push_back(index);
    }
    return lanes;
}

class DatePlanner {
public:
    DatePlanner(const PipelineResult& run, const ReservationBook& book, const SelectionItems& items,
                const LaneClassifierSettings& routing, const SelectorSettings& selector, const ShipDateRules& rules,
                long long maxDaysPulledForward)
        : run_(run), book_(book), items_(items), routing_(routing), selector_(selector), rules_(rules),
          maxDaysPulledForward_(maxDaysPulledForward), lanes_(lanesOf(run, book, items)),
          own_(routeItems(run, book, items)), plannedDate_(own_.routeDates), settled_(items.items.size(), 0),
          routeInPass_(items.items.size(), 0), built_(book.reservations.size(), 0),
          escalated_(book.reservations.size(), 0) {}
    // It holds references to its inputs and plans once.
    DatePlanner(const DatePlanner&) = delete;
    DatePlanner& operator=(const DatePlanner&) = delete;

    ShipDatePlan plan() {
        if (rules_.carryUnshippedForward) {
            for (size_t index = 0; index < plannedDate_.size(); ++index) {
                if (plannedDate_[index].empty()) continue;
                const string reserved = reservedDateFrom(laneOf(index), plannedDate_[index], false);
                if (!reserved.empty()) plannedDate_[index] = reserved;
            }
        }
        // Carried goods move only to reserved dates, so every date a good can reach is here.
        set<string> dates;
        for (const string& date : plannedDate_) {
            if (!date.empty()) dates.insert(date);
        }
        for (const map<string, long long>& reserved : lanes_.reservedTrucks) {
            for (const auto& [date, trucks] : reserved) dates.insert(date);
        }
        for (const string& date : dates) planDate(date);
        return assemble();
    }

private:
    size_t laneOf(size_t itemIndex) const { return lanes_.laneOfGroup[items_.items[itemIndex].groupIndex]; }

    // The lane's first reserved date on or, when `after`, strictly after `date`; empty for none.
    string reservedDateFrom(size_t lane, const string& date, bool after) const {
        const map<string, long long>& reserved = lanes_.reservedTrucks[lane];
        const auto found = after ? reserved.upper_bound(date) : reserved.lower_bound(date);
        return found == reserved.end() ? string() : found->first;
    }

    void planDate(const string& date) {
        ItemRouting routing;
        routing.routeDates.assign(items_.items.size(), string());
        for (size_t index = 0; index < items_.items.size(); ++index) {
            if (!settled_[index] && plannedDate_[index] == date) routing.routeDates[index] = date;
        }
        if (rules_.pullForward) offerPulledForward(date, routing.routeDates);

        LaneClassification classification = classifyLanes(run_, book_, items_, routing_, routing);
        // The classifier keeps an empty route for every reservation of the run; this pass plans one date.
        classification.routes.erase(remove_if(classification.routes.begin(), classification.routes.end(),
                                               [&](const LaneRoute& route) { return route.shipDate != date; }),
                                     classification.routes.end());
        const SelectionPlan datePlan = selectLoads(run_, book_, items_, classification, selector_);

        const size_t offset = routes_.size();
        for (size_t route = 0; route < classification.routes.size(); ++route) {
            for (const size_t index : classification.routes[route].itemIndices) routeInPass_[index] = offset + route;
        }
        for (const PlannedTruck& truck : datePlan.trucks) {
            for (const size_t index : truck.items) settled_[index] = 1;
            trucks_.push_back(truck);
            trucks_.back().routeIndex += offset;
        }
        for (const LeftOverItem& left : datePlan.leftOver) {
            const size_t index = left.itemIndex;
            const size_t route = routeInPass_[index];
            if (plannedDate_[index] != date) {
                moved_.push_back({index, route, left.reason, true});
                continue;
            }
            const string next = rules_.carryUnshippedForward && carriesOver(left.reason)
                                    ? reservedDateFrom(laneOf(index), date, true) : string();
            if (!next.empty()) {
                plannedDate_[index] = next;
                moved_.push_back({index, route, left.reason, false});
                continue;
            }
            leftOver_.push_back(left);
            settled_[index] = 1;
        }
        for (const RouteOutcome& outcome : datePlan.routes) {
            outcomes_.push_back(outcome);
            outcomes_.back().routeIndex += offset;
        }
        routes_.insert(routes_.end(), make_move_iterator(classification.routes.begin()),
                       make_move_iterator(classification.routes.end()));
        for (const ReservationUse& use : datePlan.reservations) {
            built_[use.reservationIndex] += use.built;
            if (use.escalated) escalated_[use.reservationIndex] = 1;
        }
    }

    // Each lane with trucks reserved on `date` is offered goods due later, earliest due first. A
    // truck carries one group's goods, so each group's last, part-filled truck is first topped up
    // with the group's own, which costs no other group a truck; then, while the reservation has
    // room, the lane's goods fill the trucks left, until they are estimated to fill it.
    void offerPulledForward(const string& date, vector<string>& routeDates) const {
        for (size_t lane = 0; lane < lanes_.items.size(); ++lane) {
            const auto reserved = lanes_.reservedTrucks[lane].find(date);
            if (reserved == lanes_.reservedTrucks[lane].end() || reserved->second > routing_.m5Threshold) continue;
            const double trucks = static_cast<double>(reserved->second);
            TruckShares laneOffered;
            map<size_t, TruckShares> groupOffered;
            for (const size_t index : lanes_.items[lane]) {
                if (routeDates[index] != date) continue;
                const TruckShares shares = truckSharesOf(items_.items[index], run_.trailer);
                laneOffered += shares;
                groupOffered[items_.items[index].groupIndex] += shares;
            }
            for (auto& [group, offered] : groupOffered) {
                const double wholeTrucks = ceil(offered.largest() - kQuantityEpsilon);
                offerLater(lane, date, group, wholeTrucks, offered, &laneOffered, routeDates);
            }
            offerLater(lane, date, nullopt, trucks, laneOffered, nullptr, routeDates);
        }
    }

    // Offers the lane's goods due later on `date`, earliest due first and only `group`'s when one is
    // named, until `offered` reaches `trucks`. Each good offered counts in `offered`, and in
    // `alsoCounted` when given.
    void offerLater(size_t lane, const string& date, optional<size_t> group, double trucks, TruckShares& offered,
                    TruckShares* alsoCounted, vector<string>& routeDates) const {
        for (const size_t index : lanes_.items[lane]) {
            if (offered.largest() >= trucks) return;
            const SelectionItem& item = items_.items[index];
            if ((group && item.groupIndex != *group) || item.shipDate <= date || settled_[index]
                || routeDates[index] == date) {
                continue;
            }
            const long long daysAhead = daysBetween(date, item.shipDate);
            // Items run in ship-date order, so every one after this is due later still.
            if (daysAhead > maxDaysPulledForward_) return;
            if (item.availDate > date || daysAhead + item.daysPulledForward > maxDaysPulledForward_) continue;
            routeDates[index] = date;
            const TruckShares shares = truckSharesOf(item, run_.trailer);
            offered += shares;
            if (alsoCounted) *alsoCounted += shares;
        }
    }

    ShipDatePlan assemble() {
        ShipDatePlan result;
        for (const UnroutedItem& left : own_.itemsLeftOut) {
            if (settled_[left.itemIndex]) continue;
            result.classification.itemsLeftOut.push_back(left);
            leftOver_.push_back({left.itemIndex, leftOverReasonOf(left.reason)});
            settled_[left.itemIndex] = 1;
        }
        const auto unsettled = find(settled_.begin(), settled_.end(), 0);
        if (unsettled != settled_.end()) {
            throw logic_error("Ship-date planner: item " + to_string(unsettled - settled_.begin())
                              + " ended on no truck and in no left-over list");
        }

        // Passes ran date by date, each by group: by group alone, stably, gives group then date.
        vector<size_t> order(routes_.size());
        iota(order.begin(), order.end(), size_t{0});
        stable_sort(order.begin(), order.end(),
                    [&](size_t left, size_t right) { return routes_[left].groupIndex < routes_[right].groupIndex; });
        vector<size_t> renumbered(routes_.size());
        for (size_t position = 0; position < order.size(); ++position) renumbered[order[position]] = position;

        result.classification.routes.reserve(routes_.size());
        SelectionPlan& merged = result.plan;
        merged.routes.reserve(routes_.size());
        for (const size_t route : order) {
            result.classification.routes.push_back(std::move(routes_[route]));
            merged.routes.push_back(std::move(outcomes_[route]));
            merged.routes.back().routeIndex = renumbered[route];
        }
        for (PlannedTruck& truck : trucks_) truck.routeIndex = renumbered[truck.routeIndex];
        stable_sort(trucks_.begin(), trucks_.end(),
                    [](const PlannedTruck& left, const PlannedTruck& right) { return left.routeIndex < right.routeIndex; });
        merged.trucks = std::move(trucks_);
        sort(leftOver_.begin(), leftOver_.end(),
             [](const LeftOverItem& left, const LeftOverItem& right) { return left.itemIndex < right.itemIndex; });
        merged.leftOver = std::move(leftOver_);
        for (MovedItem& move : moved_) move.routeIndex = renumbered[move.routeIndex];
        sort(moved_.begin(), moved_.end(), [](const MovedItem& left, const MovedItem& right) {
            return tie(left.itemIndex, left.routeIndex) < tie(right.itemIndex, right.routeIndex);
        });
        merged.moved = std::move(moved_);

        merged.reservations.reserve(book_.reservations.size());
        for (size_t index = 0; index < book_.reservations.size(); ++index) {
            ReservationUse use;
            use.reservationIndex = index;
            use.reserved = book_.reservations[index].trucks;
            use.built = built_[index];
            use.escalated = escalated_[index] != 0;
            merged.penaltyExposureTrucks += use.unused();
            merged.reservations.push_back(use);
        }
        return result;
    }

    const PipelineResult& run_;
    const ReservationBook& book_;
    const SelectionItems& items_;
    const LaneClassifierSettings& routing_;
    const SelectorSettings& selector_;
    const ShipDateRules& rules_;
    long long maxDaysPulledForward_;
    Lanes lanes_;
    ItemRouting own_;
    vector<string> plannedDate_;  // the date a good is next offered on; empty when left out
    vector<char> settled_;        // on a truck, or left over for good
    vector<size_t> routeInPass_;  // the route an offered item is on, in the date being planned
    vector<LaneRoute> routes_;
    vector<RouteOutcome> outcomes_;
    vector<PlannedTruck> trucks_;
    vector<LeftOverItem> leftOver_;
    vector<MovedItem> moved_;
    vector<long long> built_;     // parallel to the book's reservations
    vector<char> escalated_;
};

} // anonymous namespace

ShipDatePlan planShipDates(const PipelineResult& run, const ReservationBook& book, const SelectionItems& items,
                           const LaneClassifierSettings& routing, const SelectorSettings& selector,
                           const ShipDateRules& rules, long long maxDaysPulledForward) {
    if (maxDaysPulledForward < 0) {
        throw invalid_argument("Ship-date planner: maxDaysPulledForward must be at least 0, is "
                               + to_string(maxDaysPulledForward));
    }
    if (!rules.carryUnshippedForward && !rules.pullForward) {
        ShipDatePlan result;
        result.classification = classifyLanes(run, book, items, routing);
        result.plan = selectLoads(run, book, items, result.classification, selector);
        return result;
    }
    return DatePlanner(run, book, items, routing, selector, rules, maxDaysPulledForward).plan();
}

vector<optional<size_t>> finalRouteOfItems(const LaneClassification& classification, const SelectionPlan& plan,
                                           size_t itemCount) {
    vector<optional<size_t>> finalRoute(itemCount);
    // An item's routes are all its group's, which run by date: the last is where it ended.
    for (size_t route = 0; route < classification.routes.size(); ++route) {
        for (const size_t index : classification.routes[route].itemIndices) {
            if (index < itemCount) finalRoute[index] = route;
        }
    }
    for (const MovedItem& move : plan.moved) {
        if (move.itemIndex < itemCount && finalRoute[move.itemIndex] == move.routeIndex) finalRoute[move.itemIndex].reset();
    }
    return finalRoute;
}

} // namespace ob
