#include "selector.hpp"
#include "pipeline.hpp"
#include "reportFormat.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <iterator>
#include <map>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <utility>

using namespace std;

namespace ob {

namespace {

using LaneDate = tuple<string, string, string, string>;  // origin, destination, condition, date

// A truck as positions in its route's item list. Positions follow selection order, so the
// highest position is the least urgent item.
using RouteTruck = vector<size_t>;

vector<PackingMethod> methodChain(SolveMethod method) {
    switch (method) {
    case SolveMethod::Knapsack: return {PackingMethod::Knapsack, PackingMethod::GreedyKnapsack};
    case SolveMethod::PatternPacker:
        return {PackingMethod::PatternPacker, PackingMethod::BinPacker, PackingMethod::FirstFitDecreasing};
    case SolveMethod::BinPacker: return {PackingMethod::BinPacker, PackingMethod::FirstFitDecreasing};
    default: return {};
    }
}

struct MethodResult {
    vector<RouteTruck> trucks;
    vector<char> unfittable;  // parallel to the route's items
    bool proven = false;
    bool lpIterationCapHit = false;
    string unprovenBecause;  // empty when proven
};

vector<char> unfittableFlags(const KnapsackProblem& problem, size_t count) {
    vector<char> flags(count, 0);
    for (const size_t index : problem.tooTall) flags[index] = 1;
    for (const size_t index : problem.tooLarge) flags[index] = 1;
    return flags;
}

MethodResult runMethod(PackingMethod method, const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                       const SelectorSettings& settings, int allowed, const TruckLimits& truck) {
    MethodResult result;
    switch (method) {
    case PackingMethod::Knapsack:
    case PackingMethod::GreedyKnapsack: {
        KnapsackSettings knapsack = settings.knapsack;
        // One node is the greedy warm start, kept as it is.
        if (method == PackingMethod::GreedyKnapsack) knapsack.nodeLimit = 1;
        const LaneSelection lane = selectForTrucks(items, capacity, knapsack, allowed, truck);
        for (const KnapsackSelection& truck : lane.trucks) {
            if (!truck.chosen.empty()) result.trucks.push_back(truck.chosen);
        }
        result.unfittable = unfittableFlags(prepareKnapsack(items, capacity, knapsack), items.size());
        result.proven = method == PackingMethod::Knapsack && lane.proven;
        if (method == PackingMethod::GreedyKnapsack) {
            result.unprovenBecause = "greedy knapsack proves nothing";
        } else if (!lane.proven) {
            result.unprovenBecause = "knapsack stopped at its node limit of " + grouped(knapsack.nodeLimit) + " on a truck";
        }
        break;
    }
    case PackingMethod::PatternPacker: {
        const PatternPackerSettings& patternSettings = settings.patternPacker;
        const PatternPacking packing = packByPatterns(items, capacity, patternSettings, allowed);
        for (const PackedTruck& truck : packing.trucks) result.trucks.push_back(truck.items);
        result.unfittable = unfittableFlags(
            prepareKnapsack(items, capacity, {patternSettings.weightGrainLb, patternSettings.areaGrainSqIn, 1}),
            items.size());
        result.proven = packing.provenOptimal;
        result.lpIterationCapHit = packing.lpIterationCapHit;
        if (packing.provenOptimal) break;
        if (packing.unstable) {
            result.unprovenBecause = "patternPacker's LP or pricing could not finish";
        } else if (!packing.converged) {
            result.unprovenBecause = "patternPacker stopped unconverged, at its pattern limit of "
                                     + grouped(patternSettings.maxPatterns)
                                     + " or a pricing node limit of " + grouped(patternSettings.pricingNodeLimit);
        } else {
            result.unprovenBecause = "patternPacker's rounding is " + grouped(packing.gapToLp)
                                     + " truck(s) over its LP bound";
        }
        break;
    }
    case PackingMethod::BinPacker:
    case PackingMethod::FirstFitDecreasing: {
        const BinPacking packing = method == PackingMethod::BinPacker
            ? packBins(items, capacity, settings.binPacker, allowed)
            : packFirstFitDecreasing(items, capacity, settings.binPacker, allowed);
        for (const BinTruck& truck : packing.trucks) result.trucks.push_back(truck.items);
        result.unfittable.assign(items.size(), 0);
        for (const size_t index : packing.unfittable) result.unfittable[index] = 1;
        result.proven = packing.proven;
        result.lpIterationCapHit = packing.lpIterationCapHit;
        if (packing.proven) break;
        const BinPackerSettings& binSettings = settings.binPacker;
        if (method == PackingMethod::FirstFitDecreasing) {
            result.unprovenBecause = "first-fit decreasing is over its lower bound";
        } else if (packing.fellBack) {
            result.unprovenBecause = "binPacker did not search, being over its search item limit of "
                                     + grouped(binSettings.maxSearchItems)
                                     + " or its root LP unsolved: first-fit decreasing stands";
        } else if (packing.branchNodes >= binSettings.branchNodeLimit) {
            result.unprovenBecause = "binPacker stopped at its branch node limit of " + grouped(binSettings.branchNodeLimit);
        } else {
            result.unprovenBecause = "binPacker's column generation stopped unconverged at a branch node, at its round limit of "
                                     + grouped(binSettings.columnRoundsPerNode)
                                     + " or a pricing node limit of " + grouped(binSettings.pricingNodeLimit);
        }
        break;
    }
    }
    return result;
}

struct BundleResult {
    vector<PlannedTruck> trucks;
    vector<RouteOutcome> outcomes;
    vector<LeftOverItem> leftOver;
    long long built = 0;
    bool escalated = false;
};

class BundleSolver {
public:
    BundleSolver(const PipelineResult& run, const SelectionItems& items,
                 const LaneClassification& classification, const SelectorSettings& settings,
                 const function<double()>& clock)
        : run_(run), items_(items), classification_(classification), settings_(settings), clock_(clock),
          capacity_(capacityOf(run.trailer)) {}

    BundleResult solve(const vector<size_t>& routeIndices) const {
        // Each group may build its allocation plus what the groups solved before it left unbuilt.
        // Most urgent goods first, as items are in selection order, so spare trucks pass down.
        vector<size_t> solveOrder = routeIndices;
        stable_sort(solveOrder.begin(), solveOrder.end(), [&](size_t left, size_t right) {
            const LaneRoute& leftRoute = classification_.routes[left];
            const vector<size_t>& leftItems = leftRoute.itemIndices;
            const vector<size_t>& rightItems = classification_.routes[right].itemIndices;
            if (leftItems.empty() || rightItems.empty()) return !leftItems.empty() && rightItems.empty();
            return shipsBeforeOn(items_.items[leftItems.front()], items_.items[rightItems.front()], leftRoute.shipDate,
                                 classification_.pastDueOrder);
        });
        const BindingLimit binding = bindingOf(routeIndices);
        const long long reservedTrucks = classification_.routes[routeIndices.front()].reservedTrucks;
        vector<long long> shareOf;  // parallel to solveOrder
        shareOf.reserve(solveOrder.size());
        for (const size_t routeIndex : solveOrder) shareOf.push_back(classification_.routes[routeIndex].trucksAllocated);
        dealByBestTruck(solveOrder, reservedTrucks, shareOf);
        long long spare = reservedTrucks;
        for (const long long share : shareOf) spare -= share;
        bool escalated = false;
        long long built = 0;
        vector<RouteSolution> solutions;  // parallel to solveOrder
        solutions.reserve(solveOrder.size());
        vector<SolveMethod> methodOf;     // parallel to solveOrder: the method each group was solved by
        methodOf.reserve(solveOrder.size());
        for (size_t position = 0; position < solveOrder.size(); ++position) {
            const size_t routeIndex = solveOrder[position];
            const LaneRoute& route = classification_.routes[routeIndex];
            methodOf.push_back(route.method);
            RouteSolution solution;
            RouteOutcome& outcome = solution.outcome;
            outcome.routeIndex = routeIndex;
            outcome.binding = binding;
            switch (route.method) {
            case SolveMethod::M5:
                outcome.status = RouteStatus::Escalated;
                escalated = true;
                leaveAll(route, LeftOverReason::EscalatedToM5, solution.leftOver);
                break;
            case SolveMethod::Unrouted:
                outcome.status = RouteStatus::Unrouted;
                leaveAll(route, LeftOverReason::NoReservation, solution.leftOver);
                break;
            case SolveMethod::NoDemand:
                outcome.status = RouteStatus::NoDemand;
                outcome.trucksAllowed = route.trucksAllocated + spare;
                spare = outcome.trucksAllowed;
                break;
            default:
                outcome.trucksAllowed = shareOf[position] + spare;
                // Dealt fewer trucks than the classifier estimated, a group may now have to choose.
                if (isSelectionProblem(outcome.trucksAllowed, route.demandTrucks)) methodOf.back() = SolveMethod::Knapsack;
                solveGroup(route, methodChain(methodOf.back()), solution);
                spare = outcome.trucksAllowed - outcome.trucksBuilt;
                break;
            }
            built += outcome.trucksBuilt;
            solutions.push_back(std::move(solution));
        }
        if (!escalated) giveUnbuiltToUrgent(solveOrder, methodOf, reservedTrucks - built, solutions);

        BundleResult bundle;
        bundle.escalated = escalated;
        for (RouteSolution& solution : solutions) {
            bundle.built += solution.outcome.trucksBuilt;
            bundle.trucks.insert(bundle.trucks.end(), make_move_iterator(solution.trucks.begin()),
                                 make_move_iterator(solution.trucks.end()));
            bundle.leftOver.insert(bundle.leftOver.end(), solution.leftOver.begin(), solution.leftOver.end());
            bundle.outcomes.push_back(std::move(solution.outcome));
        }
        return bundle;
    }

    // Every route of the bundle unsolved, its goods left over: what a bundle that threw
    // outside any one method comes back as.
    BundleResult unsolved(const vector<size_t>& routeIndices, const string& error) const {
        BundleResult bundle;
        for (const size_t routeIndex : routeIndices) {
            RouteOutcome outcome;
            outcome.routeIndex = routeIndex;
            outcome.status = RouteStatus::Unsolved;
            outcome.attempts.push_back({PackingMethod::FirstFitDecreasing, AttemptOutcome::Threw, 0.0, error});
            leaveAll(classification_.routes[routeIndex], LeftOverReason::LaneUnsolved, bundle.leftOver);
            bundle.outcomes.push_back(std::move(outcome));
        }
        return bundle;
    }

    // One group alone, through `chain`, allowed `trucksAllowed` trucks.
    RouteSolution solveAlone(size_t routeIndex, const vector<PackingMethod>& chain, long long trucksAllowed) const {
        RouteSolution solution;
        solution.outcome.routeIndex = routeIndex;
        solution.outcome.trucksAllowed = trucksAllowed;
        solution.outcome.binding = bindingOf({routeIndex});
        solveGroup(classification_.routes[routeIndex], chain, solution);
        return solution;
    }

private:
    BindingLimit bindingOf(const vector<size_t>& routeIndices) const {
        double areaSqIn = 0.0, weightLb = 0.0;
        for (const size_t routeIndex : routeIndices) {
            for (const size_t index : classification_.routes[routeIndex].itemIndices) {
                const SelectionItem& item = items_.items[index];
                areaSqIn += item.footprintLongIn * item.footprintShortIn;
                weightLb += item.weightLb;
            }
        }
        if (areaSqIn <= 0.0 && weightLb <= 0.0) return BindingLimit::None;
        // A tie plans against floor: the customer's lanes mostly run out of space first.
        return weightLb / capacity_.weightLb > areaSqIn / capacity_.floorAreaSqIn ? BindingLimit::Weight
                                                                                : BindingLimit::FloorArea;
    }

    static void leaveAll(const LaneRoute& route, LeftOverReason reason, vector<LeftOverItem>& leftOver) {
        for (const size_t index : route.itemIndices) leftOver.push_back({index, reason});
    }

    // Goods a full truck could not take: over the reservation, or trimmed off a truck by its
    // rows or the unit-load cap.
    static bool isOverflow(const LeftOverItem& left) {
        return left.reason == LeftOverReason::OverReservation || left.reason == LeftOverReason::FloorEviction
               || left.reason == LeftOverReason::UnitLoadCap;
    }

    // The allocation is an estimate: a group it gave too few trucks leaves goods over the
    // reservation, while a less urgent group builds fewer than it was given, and spare trucks
    // pass only to groups solved later. The lane-date's trucks still unbuilt go to the most
    // urgent group with overflow, re-solved allowed its own trucks plus those,
    // so the lane-date never builds more than it reserved. A group that cannot use them builds
    // no more with fewer, so each is tried once.
    void giveUnbuiltToUrgent(const vector<size_t>& solveOrder, const vector<SolveMethod>& methodOf, long long unbuilt,
                             vector<RouteSolution>& solutions) const {
        for (size_t position = 0; position < solveOrder.size() && unbuilt > 0; ++position) {
            RouteSolution& current = solutions[position];
            if (current.outcome.status != RouteStatus::Solved || !current.outcome.methodUsed) continue;
            if (none_of(current.leftOver.begin(), current.leftOver.end(), isOverflow)) continue;
            const LaneRoute& route = classification_.routes[solveOrder[position]];
            RouteSolution resolved;
            resolved.outcome.routeIndex = solveOrder[position];
            resolved.outcome.binding = current.outcome.binding;
            resolved.outcome.trucksAllowed = current.outcome.trucksBuilt + unbuilt;
            solveGroup(route, methodChain(methodOf[position]), resolved);
            if (resolved.outcome.trucksBuilt <= current.outcome.trucksBuilt) continue;
            unbuilt -= resolved.outcome.trucksBuilt - current.outcome.trucksBuilt;
            current = std::move(resolved);
        }
    }

    // Rule 2 dealt on trucks that fit. Where some group's goods need more trucks than the
    // classifier's estimate gave it, that estimate decides whose goods stay behind, and floor area
    // is not rows: twelve 60 x 60 stacks estimated as one truckload took the one truck from eleven
    // 48 x 40, though only ten stand in its rows (Tom Moore, 10 Nov 2026). So the lane-date's
    // trucks go one at a time to the group whose best feasible next truck - the knapsack's, on its
    // rows and the unit-load cap - ships most of the lane-date's most urgent goods, class by class,
    // then covers most floor; equal trucks go to the group solved first. `shareOf` is left as the
    // classifier dealt it when no group is short, or when a truck cannot be searched.
    void dealByBestTruck(const vector<size_t>& solveOrder, long long reservedTrucks, vector<long long>& shareOf) const {
        vector<size_t> sharing;  // positions in solveOrder of the groups with goods to solve
        bool someGroupShort = false;
        for (size_t position = 0; position < solveOrder.size(); ++position) {
            const LaneRoute& route = classification_.routes[solveOrder[position]];
            const bool solvable = route.method == SolveMethod::Knapsack || route.method == SolveMethod::PatternPacker
                                  || route.method == SolveMethod::BinPacker;
            if (!solvable || route.itemIndices.empty()) continue;
            sharing.push_back(position);
            someGroupShort = someGroupShort || route.demandAboveReservation;
        }
        if (sharing.size() < 2 || !someGroupShort) return;

        using UrgencyClass = tuple<string, int, string>;  // rank date, priority, ship date
        auto classOf = [](const KnapsackItem& item) {
            return UrgencyClass{item.rankDate.empty() ? item.shipDate : item.rankDate, item.priority, item.shipDate};
        };
        const size_t groups = sharing.size();
        // Sized once and never resized: each group's check holds references into its own entries.
        vector<vector<KnapsackItem>> itemsOf(groups);
        vector<map<vector<size_t>, bool>> rowsFitOf(groups);
        vector<UrgencyClass> classes;
        for (size_t group = 0; group < groups; ++group) {
            itemsOf[group] = knapsackItemsOf(classification_.routes[solveOrder[sharing[group]]]);
            for (const KnapsackItem& item : itemsOf[group]) classes.push_back(classOf(item));
        }
        sort(classes.begin(), classes.end());
        classes.erase(unique(classes.begin(), classes.end()), classes.end());
        // Unit loads per class of the lane-date, most urgent first, then floor covered, in thousandths.
        auto valueOf = [&](size_t group, const KnapsackSelection& truck) {
            vector<long long> value(classes.size() + 1, 0);
            for (const size_t index : truck.chosen) {
                const KnapsackItem& item = itemsOf[group][index];
                const auto found = lower_bound(classes.begin(), classes.end(), classOf(item));
                value[static_cast<size_t>(found - classes.begin())] += llround(item.unitLoads * 1000.0);
                value.back() += llround(item.areaSqIn * 1000.0);
            }
            return value;
        };

        vector<long long> dealt(groups, 0);
        try {
            vector<TruckLimits> truckOf;
            truckOf.reserve(groups);
            vector<vector<size_t>> remainingOf(groups);
            vector<KnapsackSelection> nextOf(groups);
            auto searchNext = [&](size_t group) {
                const vector<size_t>& remaining = remainingOf[group];
                nextOf[group] = remaining.empty() ? KnapsackSelection()
                                                  : selectNextTruck(itemsOf[group], remaining, capacity_,
                                                                    settings_.knapsack, truckOf[group]);
            };
            for (size_t group = 0; group < groups; ++group) {
                truckOf.push_back(truckLimits(classification_.routes[solveOrder[sharing[group]]], itemsOf[group],
                                              rowsFitOf[group]));
                remainingOf[group].resize(itemsOf[group].size());
                iota(remainingOf[group].begin(), remainingOf[group].end(), size_t{0});
                searchNext(group);
            }
            for (long long truck = 0; truck < reservedTrucks; ++truck) {
                optional<size_t> winner;
                vector<long long> winnerValue;
                for (size_t group = 0; group < groups; ++group) {
                    if (nextOf[group].chosen.empty()) continue;
                    vector<long long> value = valueOf(group, nextOf[group]);
                    if (!winner || winnerValue < value) {
                        winner = group;
                        winnerValue = std::move(value);
                    }
                }
                if (!winner) break;
                ++dealt[*winner];
                remainingOf[*winner] = nextOf[*winner].leftOver;
                searchNext(*winner);
            }
        } catch (const exception&) {
            // The group's methods meet the same failure and record it; the estimate stands.
            return;
        }
        fill(shareOf.begin(), shareOf.end(), 0);
        for (size_t group = 0; group < groups; ++group) shareOf[sharing[group]] = dealt[group];
    }

    vector<KnapsackItem> knapsackItemsOf(const LaneRoute& route) const {
        vector<KnapsackItem> items;
        items.reserve(route.itemIndices.size());
        for (const size_t index : route.itemIndices) {
            items.push_back(knapsackItemFrom(items_.items[index]));
            items.back().rankDate = rankDateOf(items.back().shipDate, route.shipDate, classification_.pastDueOrder);
        }
        return items;
    }

    // One truck of the route as the knapsack judges it: rows and the unit-load cap on a whole load,
    // and the limits it may cut on - the cap, each stack's least share of the trailer length, and
    // the positions cap when the customer has one.
    TruckLimits truckLimits(const LaneRoute& route, const vector<KnapsackItem>& items,
                            map<vector<size_t>, bool>& rowsFitByFloorTypes) const {
        TruckLimits truck;
        truck.fits = truckCheck(route, items, rowsFitByFloorTypes);
        ItemLimit unitLoads{{}, settings_.maxUnitLoadsPerTruck};
        unitLoads.perItem.reserve(items.size());
        vector<FloorItem> floorItems;
        floorItems.reserve(items.size());
        for (size_t position = 0; position < items.size(); ++position) {
            unitLoads.perItem.push_back(items[position].unitLoads);
            floorItems.push_back(floorItemFrom(items_.items[route.itemIndices[position]]));
        }
        truck.limits.push_back(std::move(unitLoads));
        truck.limits.push_back({rowLengthShares(floorItems, run_.trailer, settings_.floor), run_.trailer.interiorLengthIn});
        if (settings_.floor.maxPositions) {
            truck.limits.push_back({vector<double>(items.size(), 1.0), static_cast<double>(*settings_.floor.maxPositions)});
        }
        return truck;
    }

    void solveGroup(const LaneRoute& route, const vector<PackingMethod>& chain, RouteSolution& solution) const {
        RouteOutcome& outcome = solution.outcome;
        const vector<KnapsackItem> items = knapsackItemsOf(route);
        const int allowed = static_cast<int>(max(0LL, outcome.trucksAllowed));

        map<vector<size_t>, bool> rowsFitByFloorTypes;
        optional<TruckLimits> truck;
        optional<MethodResult> kept;
        for (size_t step = 0; step < chain.size() && !kept; ++step) {
            const double start = clock_();
            try {
                if (settings_.beforeMethod) settings_.beforeMethod(outcome.routeIndex, chain[step]);
                const bool usesRows = chain[step] == PackingMethod::Knapsack || chain[step] == PackingMethod::GreedyKnapsack;
                if (usesRows && !truck) truck = truckLimits(route, items, rowsFitByFloorTypes);
                MethodResult result =
                    runMethod(chain[step], items, capacity_, settings_, allowed, usesRows ? *truck : TruckLimits());
                const double seconds = clock_() - start;
                // Elapsed time never picks the method: a slower machine would then plan the same
                // input differently. An overrun keeps the result and flags the route instead.
                const bool overBudget = seconds > settings_.secondsPerLane;
                outcome.attempts.push_back(
                    {chain[step], overBudget ? AttemptOutcome::OverBudget : AttemptOutcome::Used, seconds, ""});
                outcome.overBudget = overBudget;
                outcome.lpIterationCapHit = result.lpIterationCapHit;
                outcome.methodUsed = chain[step];
                outcome.proven = result.proven;
                outcome.unprovenBecause = std::move(result.unprovenBecause);
                kept = std::move(result);
            } catch (const exception& error) {
                outcome.attempts.push_back({chain[step], AttemptOutcome::Threw, clock_() - start, error.what()});
            }
        }
        if (!kept) {
            outcome.status = RouteStatus::Unsolved;
            leaveAll(route, LeftOverReason::LaneUnsolved, solution.leftOver);
            return;
        }

        vector<RouteTruck> trucks = std::move(kept->trucks);
        const vector<RouteTruck> methodTrucks = trucks;
        vector<char> placed(items.size(), 0);
        for (const RouteTruck& truck : trucks) {
            for (const size_t position : truck) placed[position] = 1;
        }
        vector<LeftOverReason> reasonFor(items.size(), LeftOverReason::OverReservation);
        for (size_t position = 0; position < items.size(); ++position) {
            if (kept->unfittable[position]) reasonFor[position] = LeftOverReason::Unfittable;
        }
        for (RouteTruck& truck : trucks) {
            fitFloor(route, truck, placed, reasonFor);
            fitUnitLoadCap(items, truck, placed, reasonFor);
        }
        for (RouteTruck& truck : trucks) fillTruck(route, items, truck, placed, kept->unfittable);
        openTrucks(route, items, trucks, placed, kept->unfittable, static_cast<size_t>(allowed));
        trucks.erase(remove_if(trucks.begin(), trucks.end(), [](const RouteTruck& truck) { return truck.empty(); }),
                     trucks.end());
        // The packers prove their trucks on floor area and payload alone. The proof still covers
        // the trucks while they carry every good the method loaded, on no more trucks.
        if (outcome.proven && !keepsMethodLoad(methodTrucks, trucks, placed)) {
            outcome.proven = false;
            outcome.unprovenBecause = string(packingMethodName(*outcome.methodUsed))
                                      + " proved its trucks on floor area and payload, and their rows or the "
                                        "unit-load cap then left goods off them";
        }
        // Rev 8 section 4: a weight-bound lane packs to payload and watches the space, so its
        // weight is left where the method put it.
        if (outcome.binding == BindingLimit::FloorArea) spreadWeight(route, items, trucks);

        auto fill = [&](const RouteTruck& truck) {
            double share = 0.0;
            for (const size_t position : truck) {
                share += items[position].areaSqIn / capacity_.floorAreaSqIn + items[position].weightLb / capacity_.weightLb;
            }
            return share;
        };
        stable_sort(trucks.begin(), trucks.end(),
                    [&](const RouteTruck& left, const RouteTruck& right) { return fill(left) > fill(right); });

        double areaShare = 0.0, weightShare = 0.0;
        for (const RouteTruck& truck : trucks) {
            PlannedTruck planned;
            planned.routeIndex = outcome.routeIndex;
            planned.groupIndex = route.groupIndex;
            planned.shipDate = route.shipDate;
            for (const size_t position : truck) {
                planned.items.push_back(route.itemIndices[position]);
                planned.areaSqIn += items[position].areaSqIn;
                planned.weightLb += items[position].weightLb;
                planned.unitLoads += items[position].unitLoads;
            }
            planned.weightFull = planned.weightLb >= capacity_.weightLb - kQuantityEpsilon;
            planned.floor = arrange(route, truck);
            areaShare += planned.areaSqIn / capacity_.floorAreaSqIn;
            weightShare += planned.weightLb / capacity_.weightLb;
            solution.trucks.push_back(std::move(planned));
        }
        outcome.trucksBuilt = static_cast<long long>(trucks.size());
        if (!trucks.empty()) {
            const BindingLimit built = weightShare > areaShare ? BindingLimit::Weight : BindingLimit::FloorArea;
            outcome.bindingChanged = built != outcome.binding;
        }
        for (size_t position = 0; position < items.size(); ++position) {
            if (!placed[position]) solution.leftOver.push_back({route.itemIndices[position], reasonFor[position]});
        }
    }

    static bool keepsMethodLoad(const vector<RouteTruck>& methodTrucks, const vector<RouteTruck>& trucks,
                                const vector<char>& placed) {
        if (trucks.size() > methodTrucks.size()) return false;
        for (const RouteTruck& truck : methodTrucks) {
            for (const size_t position : truck) {
                if (!placed[position]) return false;
            }
        }
        return true;
    }

    // Whether one truck can carry the goods at these positions on its rows and within the unit-load
    // cap. Rows depend only on each item's footprint and levels, so loads alike in those share one
    // arrangement, remembered in `rowsFitByFloorTypes` by their sorted floor types.
    TruckCheck truckCheck(const LaneRoute& route, const vector<KnapsackItem>& items,
                          map<vector<size_t>, bool>& rowsFitByFloorTypes) const {
        map<tuple<double, double, int>, size_t> floorTypes;
        vector<size_t> floorTypeOf;
        floorTypeOf.reserve(items.size());
        for (const KnapsackItem& item : items) {
            floorTypeOf.push_back(
                floorTypes.try_emplace({item.longSideIn, item.shortSideIn, item.levels}, floorTypes.size()).first->second);
        }
        return [this, &route, &items, &rowsFitByFloorTypes, floorTypeOf = std::move(floorTypeOf),
                key = vector<size_t>()](const vector<size_t>& chosen) mutable {
            double unitLoads = 0.0;
            key.clear();
            for (const size_t position : chosen) {
                unitLoads += items[position].unitLoads;
                key.push_back(floorTypeOf[position]);
            }
            if (unitLoads > settings_.maxUnitLoadsPerTruck + kQuantityEpsilon) return false;
            sort(key.begin(), key.end());
            const auto known = rowsFitByFloorTypes.find(key);
            if (known != rowsFitByFloorTypes.end()) return known->second;
            RouteTruck truck = chosen;
            sort(truck.begin(), truck.end());
            const bool fits = floorFits(arrange(route, truck));
            rowsFitByFloorTypes.emplace(key, fits);
            return fits;
        };
    }

    // Goods left over, by the method, the floor or the unit-load cap, go most urgent first into
    // the truck while it has room on its floor, payload, rows and unit-load cap. The methods judge
    // the floor by area, which rows waste some of, so this takes back what the floor check had to
    // trim. The truck stops being tried after fillMissesPerTruck refusals in a row, which bounds
    // the floor arrangements tried.
    void fillTruck(const LaneRoute& route, const vector<KnapsackItem>& items, RouteTruck& truck,
                   vector<char>& placed, const vector<char>& unfittable) const {
        double area = 0.0, weight = 0.0, unitLoads = 0.0;
        for (const size_t position : truck) {
            area += items[position].areaSqIn;
            weight += items[position].weightLb;
            unitLoads += items[position].unitLoads;
        }
        int misses = 0;
        for (size_t position = 0; position < items.size() && misses < settings_.fillMissesPerTruck; ++position) {
            if (placed[position] || unfittable[position]) continue;
            if (area + items[position].areaSqIn > capacity_.floorAreaSqIn + kQuantityEpsilon
                || weight + items[position].weightLb > capacity_.weightLb + kQuantityEpsilon
                || unitLoads + items[position].unitLoads > settings_.maxUnitLoadsPerTruck + kQuantityEpsilon) {
                ++misses;
                continue;
            }
            RouteTruck withItem = truck;
            withItem.insert(upper_bound(withItem.begin(), withItem.end(), position), position);
            if (!floorFits(arrange(route, withItem))) {
                ++misses;
                continue;
            }
            truck = std::move(withItem);
            area += items[position].areaSqIn;
            weight += items[position].weightLb;
            unitLoads += items[position].unitLoads;
            placed[position] = 1;
            misses = 0;
        }
    }

    // Goods still left over open the reserved trucks the method did not build, one at a time,
    // each filled as fillTruck fills. Without this a truck trimmed to the unit-load cap or the
    // floor would leave goods behind while the reservation still had trucks.
    void openTrucks(const LaneRoute& route, const vector<KnapsackItem>& items, vector<RouteTruck>& trucks,
                    vector<char>& placed, const vector<char>& unfittable, size_t allowed) const {
        while (trucks.size() < allowed) {
            RouteTruck opened;
            fillTruck(route, items, opened, placed, unfittable);
            if (opened.empty()) return;
            trucks.push_back(std::move(opened));
        }
    }

    // Drops the least urgent goods until the truck is within maxUnitLoadsPerTruck.
    void fitUnitLoadCap(const vector<KnapsackItem>& items, RouteTruck& truck, vector<char>& placed,
                        vector<LeftOverReason>& reasonFor) const {
        double unitLoads = 0.0;
        for (const size_t position : truck) unitLoads += items[position].unitLoads;
        while (!truck.empty() && unitLoads > settings_.maxUnitLoadsPerTruck + kQuantityEpsilon) {
            const size_t drop = truck.back();
            unitLoads -= items[drop].unitLoads;
            placed[drop] = 0;
            reasonFor[drop] = LeftOverReason::UnitLoadCap;
            truck.pop_back();
        }
    }

    FloorLayout arrange(const LaneRoute& route, const RouteTruck& truck) const {
        vector<FloorItem> floorItems;
        floorItems.reserve(truck.size());
        for (const size_t position : truck) floorItems.push_back(floorItemFrom(items_.items[route.itemIndices[position]]));
        return arrangeFloor(floorItems, run_.trailer, settings_.floor);
    }

    static bool floorFits(const FloorLayout& layout) {
        return layout.fitsLength && layout.withinMaxPositions && layout.unplaceableItems.empty();
    }

    // Drops goods until the truck's rows fit: an item that fits no orientation first, then the
    // least urgent.
    void fitFloor(const LaneRoute& route, RouteTruck& truck, vector<char>& placed,
                  vector<LeftOverReason>& reasonFor) const {
        for (FloorLayout layout = arrange(route, truck); !truck.empty() && !floorFits(layout);
             layout = arrange(route, truck)) {
            const size_t drop = layout.unplaceableItems.empty() ? truck.size() - 1 : layout.unplaceableItems.front();
            placed[truck[drop]] = 0;
            reasonFor[truck[drop]] = LeftOverReason::FloorEviction;
            truck.erase(truck.begin() + static_cast<ptrdiff_t>(drop));
        }
    }

    // Moves or swaps goods from the heaviest truck to the lightest while that lowers the
    // heavier of the two and both stay within floor, payload and their rows.
    void spreadWeight(const LaneRoute& route, const vector<KnapsackItem>& items, vector<RouteTruck>& trucks) const {
        if (trucks.size() < 2) return;
        auto totals = [&](const RouteTruck& truck) {
            double area = 0.0, weight = 0.0;
            for (const size_t position : truck) {
                area += items[position].areaSqIn;
                weight += items[position].weightLb;
            }
            return make_pair(area, weight);
        };
        auto unitLoadsOf = [&](const RouteTruck& truck) {
            double unitLoads = 0.0;
            for (const size_t position : truck) unitLoads += items[position].unitLoads;
            return unitLoads;
        };
        const size_t rounds = static_cast<size_t>(settings_.weightSpreadRoundsPerTruck) * trucks.size();
        for (size_t round = 0; round < rounds; ++round) {
            size_t heavy = 0, light = 0;
            vector<pair<double, double>> load(trucks.size());
            for (size_t truck = 0; truck < trucks.size(); ++truck) {
                load[truck] = totals(trucks[truck]);
                if (load[truck].second > load[heavy].second) heavy = truck;
                if (load[truck].second < load[light].second) light = truck;
            }
            if (heavy == light) return;
            // A move is a swap with nothing (no position) coming back.
            double bestPeak = load[heavy].second - kQuantityEpsilon;
            optional<pair<size_t, optional<size_t>>> best;
            for (size_t out = 0; out < trucks[heavy].size(); ++out) {
                const KnapsackItem& outgoing = items[trucks[heavy][out]];
                for (size_t back = 0; back <= trucks[light].size(); ++back) {
                    const bool isSwap = back < trucks[light].size();
                    const double backArea = isSwap ? items[trucks[light][back]].areaSqIn : 0.0;
                    const double backWeight = isSwap ? items[trucks[light][back]].weightLb : 0.0;
                    const double heavyWeight = load[heavy].second - outgoing.weightLb + backWeight;
                    const double lightWeight = load[light].second - backWeight + outgoing.weightLb;
                    if (load[light].first - backArea + outgoing.areaSqIn > capacity_.floorAreaSqIn + kQuantityEpsilon
                        || load[heavy].first - outgoing.areaSqIn + backArea > capacity_.floorAreaSqIn + kQuantityEpsilon
                        || lightWeight > capacity_.weightLb + kQuantityEpsilon) {
                        continue;
                    }
                    const double peak = max(heavyWeight, lightWeight);
                    if (peak < bestPeak) {
                        bestPeak = peak;
                        best = make_pair(out, isSwap ? optional<size_t>(back) : nullopt);
                    }
                }
            }
            if (!best) return;
            RouteTruck heavyAfter = trucks[heavy], lightAfter = trucks[light];
            const size_t outgoing = heavyAfter[best->first];
            heavyAfter.erase(heavyAfter.begin() + static_cast<ptrdiff_t>(best->first));
            lightAfter.push_back(outgoing);
            if (best->second) {
                const size_t incoming = lightAfter[*best->second];
                lightAfter.erase(lightAfter.begin() + static_cast<ptrdiff_t>(*best->second));
                heavyAfter.push_back(incoming);
            }
            sort(heavyAfter.begin(), heavyAfter.end());
            sort(lightAfter.begin(), lightAfter.end());
            if (unitLoadsOf(lightAfter) > settings_.maxUnitLoadsPerTruck + kQuantityEpsilon
                || unitLoadsOf(heavyAfter) > settings_.maxUnitLoadsPerTruck + kQuantityEpsilon) {
                return;
            }
            if (!floorFits(arrange(route, heavyAfter)) || !floorFits(arrange(route, lightAfter))) return;
            trucks[heavy] = std::move(heavyAfter);
            trucks[light] = std::move(lightAfter);
        }
    }

    const PipelineResult& run_;
    const SelectionItems& items_;
    const LaneClassification& classification_;
    const SelectorSettings& settings_;
    const function<double()>& clock_;
    KnapsackCapacity capacity_;
};

void validate(const PipelineResult& run, const SelectionItems& items, const LaneClassification& classification,
              const SelectorSettings& settings) {
    if (!isfinite(settings.secondsPerLane) || settings.secondsPerLane <= 0.0) {
        throw invalid_argument("Selector settings: seconds per lane must be positive and finite");
    }
    if (settings.threads < 1) throw invalid_argument("Selector settings: threads must be at least 1");
    if (settings.fillMissesPerTruck < 1 || settings.weightSpreadRoundsPerTruck < 1) {
        throw invalid_argument("Selector settings: fill misses and weight-spread rounds must be at least 1");
    }
    if (!isfinite(settings.maxUnitLoadsPerTruck) || settings.maxUnitLoadsPerTruck <= 0.0) {
        throw invalid_argument("Selector settings: unit loads per truck must be positive and finite");
    }
    for (const LaneRoute& route : classification.routes) {
        if (route.groupIndex >= run.segregation.groups.size()) {
            throw invalid_argument("Selector: a route names group " + to_string(route.groupIndex)
                                   + " of " + to_string(run.segregation.groups.size()));
        }
        for (const size_t index : route.itemIndices) {
            if (index >= items.items.size()) {
                throw invalid_argument("Selector: a route names item " + to_string(index) + " of "
                                       + to_string(items.items.size()));
            }
        }
    }
    for (const UnroutedItem& left : classification.itemsLeftOut) {
        if (left.itemIndex >= items.items.size()) {
            throw invalid_argument("Selector: a left-out item " + to_string(left.itemIndex) + " is not one of "
                                   + to_string(items.items.size()));
        }
    }
}

function<double()> clockOf(const SelectorSettings& settings) {
    if (settings.clock) return settings.clock;
    return [] { return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count(); };
}

string thrownMessage(const exception_ptr& thrown) {
    try {
        rethrow_exception(thrown);
    } catch (const exception& error) {
        return error.what();
    } catch (...) {
        return "an exception that is not a std::exception";
    }
}

} // anonymous namespace

RouteSolution solveRoute(const PipelineResult& run, const SelectionItems& items,
                         const LaneClassification& classification, size_t routeIndex,
                         const vector<PackingMethod>& chain, long long trucksAllowed,
                         const SelectorSettings& settings) {
    validate(run, items, classification, settings);
    if (routeIndex >= classification.routes.size()) {
        throw invalid_argument("Selector: route " + to_string(routeIndex) + " of "
                               + to_string(classification.routes.size()));
    }
    if (chain.empty()) throw invalid_argument("Selector: no method to solve the route with");
    const function<double()> clock = clockOf(settings);
    const BundleSolver solver(run, items, classification, settings, clock);
    return solver.solveAlone(routeIndex, chain, trucksAllowed);
}

SelectionPlan selectLoads(const PipelineResult& run, const ReservationBook& book, const SelectionItems& items,
                          const LaneClassification& classification, const SelectorSettings& settings) {
    validate(run, items, classification, settings);
    const function<double()> clock = clockOf(settings);

    // Routes by lane and ship date: groups sharing a reservation are solved together, in order.
    map<LaneDate, vector<size_t>> routesByLaneDate;
    for (size_t routeIndex = 0; routeIndex < classification.routes.size(); ++routeIndex) {
        const LaneRoute& route = classification.routes[routeIndex];
        const GroupKey& key = run.segregation.groups[route.groupIndex].key;
        routesByLaneDate[{key.locationFrom, key.locationTo, key.shipCondition, route.shipDate}].push_back(routeIndex);
    }
    vector<pair<LaneDate, vector<size_t>>> bundles(routesByLaneDate.begin(), routesByLaneDate.end());

    const BundleSolver solver(run, items, classification, settings, clock);
    vector<BundleResult> results(bundles.size());
    vector<exception_ptr> failures(bundles.size());
    atomic<size_t> next{0};
    auto work = [&] {
        for (size_t bundle = next++; bundle < bundles.size(); bundle = next++) {
            // One lane's failure, whatever it is, must not take the run down. The handler only
            // keeps the exception, which cannot throw: the unsolved result allocates, so it is
            // built on the main thread, where a failure reaches the caller instead of terminating
            // the process.
            try {
                results[bundle] = solver.solve(bundles[bundle].second);
            } catch (...) {
                failures[bundle] = current_exception();
            }
        }
    };
    const size_t workers = min(static_cast<size_t>(settings.threads), max<size_t>(bundles.size(), 1));
    vector<thread> pool;
    pool.reserve(workers - 1);
    for (size_t worker = 1; worker < workers; ++worker) pool.emplace_back(work);
    work();
    for (thread& worker : pool) worker.join();
    for (size_t bundle = 0; bundle < bundles.size(); ++bundle) {
        if (failures[bundle]) results[bundle] = solver.unsolved(bundles[bundle].second, thrownMessage(failures[bundle]));
    }

    SelectionPlan plan;
    plan.routes.resize(classification.routes.size());
    map<LaneDate, pair<long long, bool>> builtByLaneDate;
    for (size_t bundle = 0; bundle < bundles.size(); ++bundle) {
        BundleResult& result = results[bundle];
        for (RouteOutcome& outcome : result.outcomes) plan.routes[outcome.routeIndex] = std::move(outcome);
        for (PlannedTruck& truck : result.trucks) plan.trucks.push_back(std::move(truck));
        plan.leftOver.insert(plan.leftOver.end(), result.leftOver.begin(), result.leftOver.end());
        builtByLaneDate[bundles[bundle].first] = {result.built, result.escalated};
    }
    stable_sort(plan.trucks.begin(), plan.trucks.end(),
                [](const PlannedTruck& left, const PlannedTruck& right) { return left.routeIndex < right.routeIndex; });
    for (const UnroutedItem& left : classification.itemsLeftOut) {
        plan.leftOver.push_back({left.itemIndex, leftOverReasonOf(left.reason)});
    }
    sort(plan.leftOver.begin(), plan.leftOver.end(),
         [](const LeftOverItem& left, const LeftOverItem& right) { return left.itemIndex < right.itemIndex; });

    for (size_t index = 0; index < book.reservations.size(); ++index) {
        const Reservation& reservation = book.reservations[index];
        ReservationUse use;
        use.reservationIndex = index;
        use.reserved = reservation.trucks;
        const auto found = builtByLaneDate.find(
            {reservation.origin, reservation.destination, reservation.shipCondition, reservation.shipDate});
        if (found != builtByLaneDate.end()) {
            use.built = found->second.first;
            use.escalated = found->second.second;
        }
        plan.penaltyExposureTrucks += use.unused();
        plan.reservations.push_back(use);
    }
    return plan;
}

LeftOverReason leftOverReasonOf(UnroutedItemReason reason) {
    return reason == UnroutedItemReason::AfterWindow ? LeftOverReason::AfterWindow
                                                     : LeftOverReason::UnavailableInWindow;
}

const char* packingMethodName(PackingMethod method) {
    switch (method) {
    case PackingMethod::Knapsack: return "knapsack";
    case PackingMethod::GreedyKnapsack: return "greedyKnapsack";
    case PackingMethod::PatternPacker: return "patternPacker";
    case PackingMethod::BinPacker: return "binPacker";
    case PackingMethod::FirstFitDecreasing: return "firstFitDecreasing";
    }
    return "unknown";
}

const char* leftOverReasonName(LeftOverReason reason) {
    switch (reason) {
    case LeftOverReason::AfterWindow: return "after window";
    case LeftOverReason::UnavailableInWindow: return "unavailable in window";
    case LeftOverReason::NoReservation: return "no reservation";
    case LeftOverReason::EscalatedToM5: return "escalated to M5";
    case LeftOverReason::Unfittable: return "unfittable";
    case LeftOverReason::OverReservation: return "over reservation";
    case LeftOverReason::FloorEviction: return "floor eviction";
    case LeftOverReason::UnitLoadCap: return "unit-load cap";
    case LeftOverReason::LaneUnsolved: return "lane unsolved";
    }
    return "unknown";
}

} // namespace ob
