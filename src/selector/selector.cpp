#include "selector.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
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
};

vector<char> unfittableFlags(const KnapsackProblem& problem, size_t count) {
    vector<char> flags(count, 0);
    for (const size_t index : problem.tooTall) flags[index] = 1;
    for (const size_t index : problem.tooLarge) flags[index] = 1;
    return flags;
}

MethodResult runMethod(PackingMethod method, const vector<KnapsackItem>& items,
                       const KnapsackCapacity& capacity, const SelectorSettings& settings, int allowed) {
    MethodResult result;
    switch (method) {
    case PackingMethod::Knapsack:
    case PackingMethod::GreedyKnapsack: {
        KnapsackSettings knapsack = settings.knapsack;
        // One node is the greedy warm start, kept as it is.
        if (method == PackingMethod::GreedyKnapsack) knapsack.nodeLimit = 1;
        const LaneSelection lane = selectForTrucks(items, capacity, knapsack, allowed);
        for (const KnapsackSelection& truck : lane.trucks) {
            if (!truck.chosen.empty()) result.trucks.push_back(truck.chosen);
        }
        result.unfittable = unfittableFlags(prepareKnapsack(items, capacity, knapsack), items.size());
        result.proven = method == PackingMethod::Knapsack && lane.proven;
        break;
    }
    case PackingMethod::PatternPacker: {
        const PatternPacking packing = packByPatterns(items, capacity, settings.patternPacker, allowed);
        for (const PackedTruck& truck : packing.trucks) result.trucks.push_back(truck.items);
        result.unfittable = unfittableFlags(
            prepareKnapsack(items, capacity,
                            {settings.patternPacker.weightGrainLb, settings.patternPacker.areaGrainSqIn, 1}),
            items.size());
        result.proven = packing.provenOptimal;
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
        BundleResult bundle;
        long long remaining = classification_.routes[routeIndices.front()].reservedTrucks;
        for (const size_t routeIndex : routeIndices) {
            const LaneRoute& route = classification_.routes[routeIndex];
            RouteOutcome outcome;
            outcome.routeIndex = routeIndex;
            switch (route.method) {
            case SolveMethod::M5:
                outcome.status = RouteStatus::Escalated;
                bundle.escalated = true;
                leaveAll(route, LeftOverReason::EscalatedToM5, bundle);
                break;
            case SolveMethod::Unrouted:
                outcome.status = RouteStatus::Unrouted;
                leaveAll(route, LeftOverReason::NoReservation, bundle);
                break;
            case SolveMethod::NoDemand:
                outcome.status = RouteStatus::NoDemand;
                outcome.trucksAllowed = remaining;
                break;
            default:
                outcome.trucksAllowed = remaining;
                solveGroup(route, outcome, bundle);
                remaining -= outcome.trucksBuilt;
                break;
            }
            bundle.built += outcome.trucksBuilt;
            bundle.outcomes.push_back(std::move(outcome));
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
            leaveAll(classification_.routes[routeIndex], LeftOverReason::LaneUnsolved, bundle);
            bundle.outcomes.push_back(std::move(outcome));
        }
        return bundle;
    }

private:
    static void leaveAll(const LaneRoute& route, LeftOverReason reason, BundleResult& bundle) {
        for (const size_t index : route.itemIndices) bundle.leftOver.push_back({index, reason});
    }

    void solveGroup(const LaneRoute& route, RouteOutcome& outcome, BundleResult& bundle) const {
        vector<KnapsackItem> items;
        items.reserve(route.itemIndices.size());
        for (const size_t index : route.itemIndices) items.push_back(knapsackItemFrom(items_.items[index]));
        const int allowed = static_cast<int>(max(0LL, outcome.trucksAllowed));

        const vector<PackingMethod> chain = methodChain(route.method);
        optional<MethodResult> kept;
        for (size_t step = 0; step < chain.size() && !kept; ++step) {
            const double start = clock_();
            try {
                MethodResult result = runMethod(chain[step], items, capacity_, settings_, allowed);
                const double seconds = clock_() - start;
                // The last method in the chain is kept however long it took: there is nothing
                // further down to hand on to.
                if (seconds > settings_.secondsPerLane && step + 1 < chain.size()) {
                    outcome.attempts.push_back({chain[step], AttemptOutcome::OverBudget, seconds, ""});
                    continue;
                }
                outcome.attempts.push_back({chain[step], AttemptOutcome::Used, seconds, ""});
                outcome.methodUsed = chain[step];
                outcome.proven = result.proven;
                kept = std::move(result);
            } catch (const exception& error) {
                outcome.attempts.push_back({chain[step], AttemptOutcome::Threw, clock_() - start, error.what()});
            }
        }
        if (!kept) {
            outcome.status = RouteStatus::Unsolved;
            leaveAll(route, LeftOverReason::LaneUnsolved, bundle);
            return;
        }

        vector<RouteTruck> trucks = std::move(kept->trucks);
        vector<char> placed(items.size(), 0);
        for (const RouteTruck& truck : trucks) {
            for (const size_t position : truck) placed[position] = 1;
        }
        vector<LeftOverReason> reasonFor(items.size(), LeftOverReason::OverReservation);
        for (size_t position = 0; position < items.size(); ++position) {
            if (kept->unfittable[position]) reasonFor[position] = LeftOverReason::Unfittable;
        }
        for (RouteTruck& truck : trucks) fitFloor(route, truck, placed, reasonFor);
        fillRoom(route, items, trucks, placed, kept->unfittable);
        trucks.erase(remove_if(trucks.begin(), trucks.end(), [](const RouteTruck& truck) { return truck.empty(); }),
                     trucks.end());
        spreadWeight(route, items, trucks);

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
            bundle.trucks.push_back(std::move(planned));
        }
        outcome.trucksBuilt = static_cast<long long>(trucks.size());
        if (!trucks.empty()) outcome.binding = weightShare > areaShare ? BindingLimit::Weight : BindingLimit::FloorArea;
        for (size_t position = 0; position < items.size(); ++position) {
            if (!placed[position]) bundle.leftOver.push_back({route.itemIndices[position], reasonFor[position]});
        }
    }

    // Goods left over, by the method or the floor, go most urgent first into the first truck
    // with room on its floor, payload and rows. The methods judge the floor by area, which rows
    // waste some of, so this takes back what the floor check had to trim. A truck stops being
    // tried after kFillMisses refusals in a row, which bounds the floor arrangements tried.
    void fillRoom(const LaneRoute& route, const vector<KnapsackItem>& items, vector<RouteTruck>& trucks,
                  vector<char>& placed, const vector<char>& unfittable) const {
        constexpr int kFillMisses = 10;
        for (RouteTruck& truck : trucks) {
            double area = 0.0, weight = 0.0;
            for (const size_t position : truck) {
                area += items[position].areaSqIn;
                weight += items[position].weightLb;
            }
            int misses = 0;
            for (size_t position = 0; position < items.size() && misses < kFillMisses; ++position) {
                if (placed[position] || unfittable[position]) continue;
                if (area + items[position].areaSqIn > capacity_.floorAreaSqIn + kQuantityEpsilon
                    || weight + items[position].weightLb > capacity_.weightLb + kQuantityEpsilon) {
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
                placed[position] = 1;
                misses = 0;
            }
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
        for (size_t round = 0; round < 4 * trucks.size(); ++round) {
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

} // anonymous namespace

SelectionPlan selectLoads(const PipelineResult& run, const ReservationBook& book, const SelectionItems& items,
                          const LaneClassification& classification, const SelectorSettings& settings) {
    validate(run, items, classification, settings);
    const function<double()> clock = settings.clock ? settings.clock : [] {
        return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count();
    };

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
    atomic<size_t> next{0};
    auto work = [&] {
        for (size_t bundle = next++; bundle < bundles.size(); bundle = next++) {
            // One lane's failure, whatever it is, must not take the run down.
            try {
                results[bundle] = solver.solve(bundles[bundle].second);
            } catch (const exception& error) {
                results[bundle] = solver.unsolved(bundles[bundle].second, error.what());
            }
        }
    };
    const size_t workers = min(static_cast<size_t>(settings.threads), max<size_t>(bundles.size(), 1));
    vector<thread> pool;
    pool.reserve(workers - 1);
    for (size_t worker = 1; worker < workers; ++worker) pool.emplace_back(work);
    work();
    for (thread& worker : pool) worker.join();

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
        plan.leftOver.push_back({left.itemIndex, left.reason == UnroutedItemReason::AfterWindow
                                                     ? LeftOverReason::AfterWindow
                                                     : LeftOverReason::UnavailableInWindow});
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
    case LeftOverReason::LaneUnsolved: return "lane unsolved";
    }
    return "unknown";
}

} // namespace ob
