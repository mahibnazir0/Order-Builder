#include "benchmarkHarness.hpp"
#include "pipeline.hpp"
#include "reportFormat.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <map>
#include <ostream>
#include <stdexcept>
#include <cmath>
#include <tuple>

using namespace std;

namespace ob {

namespace {

using LaneDate = tuple<string, string, string, string>;  // origin, destination, condition, date

LaneDate laneDateOf(const PipelineResult& run, const string& shipDate, size_t groupIndex) {
    const GroupKey& key = run.segregation.groups[groupIndex].key;
    return {key.locationFrom, key.locationTo, key.shipCondition, shipDate};
}

string laneDateText(const LaneDate& laneDate) {
    return printable(get<0>(laneDate)) + " -> " + printable(get<1>(laneDate)) + " " + printable(get<2>(laneDate))
           + " on " + printable(get<3>(laneDate));
}

// The method the classifier sent a route to, among the benchmarked ones.
optional<PackingMethod> routedMethod(SolveMethod method) {
    switch (method) {
    case SolveMethod::Knapsack: return PackingMethod::Knapsack;
    case SolveMethod::PatternPacker: return PackingMethod::PatternPacker;
    case SolveMethod::BinPacker: return PackingMethod::BinPacker;
    default: return nullopt;
    }
}

const MethodScore* scoreOf(const RouteBenchmark& benchmark, PackingMethod method) {
    for (const MethodScore& score : benchmark.scores) {
        if (score.method == method) return &score;
    }
    return nullptr;
}

} // anonymous namespace

const vector<PackingMethod>& benchmarkedMethods() {
    static const vector<PackingMethod> methods{PackingMethod::Knapsack, PackingMethod::PatternPacker,
                                               PackingMethod::BinPacker, PackingMethod::FirstFitDecreasing};
    return methods;
}

bool scoresBetter(const MethodScore& left, const MethodScore& right) {
    const bool leftRanked = left.ran && left.withinBudget;
    const bool rightRanked = right.ran && right.withinBudget;
    if (leftRanked != rightRanked) return leftRanked;
    if (!leftRanked) return false;
    if (fabs(left.unitLoadsShipped - right.unitLoadsShipped) > kQuantityEpsilon) {
        return left.unitLoadsShipped > right.unitLoadsShipped;
    }
    if (left.trucks != right.trucks) return left.trucks < right.trucks;
    return left.averageRearWasteIn < right.averageRearWasteIn - kQuantityEpsilon;
}

RouteBenchmark benchmarkRoute(const PipelineResult& run, const SelectionItems& items,
                              const LaneClassification& classification, size_t routeIndex,
                              const SelectorSettings& settings) {
    if (routeIndex >= classification.routes.size()) {
        throw invalid_argument("Benchmark: route " + to_string(routeIndex) + " of "
                               + to_string(classification.routes.size()));
    }
    const LaneRoute& route = classification.routes[routeIndex];
    RouteBenchmark benchmark;
    benchmark.routeIndex = routeIndex;
    benchmark.routedTo = route.method;
    benchmark.reserved = route.reservedTrucks;
    benchmark.products = route.distinctProducts;
    benchmark.unitLoads = route.unitLoads;
    benchmark.unitLoadsPerProduct =
        route.distinctProducts == 0 ? 0.0 : route.unitLoads / static_cast<double>(route.distinctProducts);

    for (const PackingMethod method : benchmarkedMethods()) {
        const RouteSolution solution =
            solveRoute(run, items, classification, routeIndex, {method}, route.reservedTrucks, settings);
        MethodScore score;
        score.method = method;
        const MethodAttempt& attempt = solution.outcome.attempts.front();
        score.ran = attempt.outcome != AttemptOutcome::Threw;
        score.error = attempt.error;
        score.seconds = attempt.seconds;
        score.withinBudget = score.ran && attempt.seconds <= settings.secondsPerLane;
        score.proven = solution.outcome.proven;
        score.trucks = solution.outcome.trucksBuilt;
        score.stacksLeft = solution.leftOver.size();
        for (const PlannedTruck& truck : solution.trucks) {
            score.unitLoadsShipped += truck.unitLoads;
            score.averageCube += truck.floor.cubeFraction;
            score.averageRearWasteIn += truck.floor.rearWasteIn;
        }
        if (score.trucks > 0) {
            score.averageCube /= static_cast<double>(score.trucks);
            score.averageRearWasteIn /= static_cast<double>(score.trucks);
        }
        benchmark.scores.push_back(std::move(score));
    }
    // Strictly better only, so ties keep the earlier method.
    const MethodScore* best = nullptr;
    for (const MethodScore& score : benchmark.scores) {
        if (score.ran && score.withinBudget && (!best || scoresBetter(score, *best))) best = &score;
    }
    if (best) benchmark.best = best->method;
    return benchmark;
}

optional<VarietyCrossover> varietyCrossover(const vector<RouteBenchmark>& benchmarks) {
    // Each separating route: its unit loads per product, and whether patternPacker ranked higher.
    vector<pair<double, bool>> lanes;
    for (const RouteBenchmark& benchmark : benchmarks) {
        const MethodScore* pattern = scoreOf(benchmark, PackingMethod::PatternPacker);
        const MethodScore* bin = scoreOf(benchmark, PackingMethod::BinPacker);
        if (!pattern || !bin || !pattern->ran || !pattern->withinBudget || !bin->ran || !bin->withinBudget) continue;
        if (scoresBetter(*pattern, *bin)) {
            lanes.emplace_back(benchmark.unitLoadsPerProduct, true);
        } else if (scoresBetter(*bin, *pattern)) {
            lanes.emplace_back(benchmark.unitLoadsPerProduct, false);
        }
    }
    if (lanes.empty()) return nullopt;
    // Candidate ratios: zero, and every lane's own ratio (patternPacker strictly above).
    vector<double> candidates{0.0};
    for (const auto& lane : lanes) candidates.push_back(lane.first);
    sort(candidates.begin(), candidates.end());
    candidates.erase(unique(candidates.begin(), candidates.end()), candidates.end());
    VarietyCrossover best;
    best.lanesCompared = lanes.size();
    best.lanesMisrouted = lanes.size() + 1;
    for (const double ratio : candidates) {
        size_t misrouted = 0;
        for (const auto& [lanesRatio, patternWins] : lanes) {
            if ((lanesRatio > ratio) != patternWins) ++misrouted;
        }
        if (misrouted < best.lanesMisrouted) {
            best.ratio = ratio;
            best.lanesMisrouted = misrouted;
        }
    }
    return best;
}

optional<long long> smallestReservationOverBudget(const vector<RouteBenchmark>& benchmarks) {
    optional<long long> smallest;
    for (const RouteBenchmark& benchmark : benchmarks) {
        const optional<PackingMethod> routed = routedMethod(benchmark.routedTo);
        if (!routed) continue;
        const MethodScore* score = scoreOf(benchmark, *routed);
        if (score && score->ran && !score->withinBudget && (!smallest || benchmark.reserved < *smallest)) {
            smallest = benchmark.reserved;
        }
    }
    return smallest;
}

vector<InvariantViolation> checkInvariants(const PipelineResult& run, const ReservationBook& book,
                                           const SelectionItems& items, const SelectionPlan& plan,
                                           double maxUnitLoadsPerTruck) {
    vector<InvariantViolation> violations;
    const TrailerSpec& trailer = run.trailer;
    const double floorArea = trailer.interiorLengthIn * trailer.interiorWidthIn;

    vector<int> seen(items.items.size(), 0);
    map<LaneDate, long long> trucksByLaneDate;
    for (size_t truckIndex = 0; truckIndex < plan.trucks.size(); ++truckIndex) {
        const PlannedTruck& truck = plan.trucks[truckIndex];
        const string name = "truck " + to_string(truckIndex + 1);
        double area = 0.0, weight = 0.0, unitLoads = 0.0;
        for (const size_t index : truck.items) {
            if (index >= items.items.size()) {
                violations.push_back({"I1", name + " carries item " + to_string(index) + ", which does not exist"});
                continue;
            }
            const SelectionItem& item = items.items[index];
            ++seen[index];
            area += item.footprintLongIn * item.footprintShortIn;
            weight += item.weightLb;
            unitLoads += item.unitLoads;
            if (item.heightIn > trailer.stackHeightCeilingIn + kQuantityEpsilon) {
                violations.push_back({"I2", name + " carries a stack taller than the ceiling"});
            }
            if (item.groupIndex != truck.groupIndex) {
                violations.push_back({"I3", name + " carries goods of group " + to_string(item.groupIndex)
                                                + " on a truck of group " + to_string(truck.groupIndex)});
            }
        }
        if (area > floorArea + kQuantityEpsilon) violations.push_back({"I2", name + " is over its floor area"});
        if (weight > trailer.weightLimitLb + kQuantityEpsilon) violations.push_back({"I2", name + " is over its payload"});
        if (!truck.floor.fitsLength || !truck.floor.unplaceableItems.empty()) {
            violations.push_back({"I2", name + "'s rows do not fit the trailer"});
        }
        if (unitLoads > maxUnitLoadsPerTruck + kQuantityEpsilon) {
            violations.push_back({"I6", name + " carries " + fixed(unitLoads, 1) + " unit loads: a conversion error, not a load"});
        }
        ++trucksByLaneDate[laneDateOf(run, truck.shipDate, truck.groupIndex)];
    }
    for (const LeftOverItem& left : plan.leftOver) {
        if (left.itemIndex < seen.size()) ++seen[left.itemIndex];
    }
    for (size_t index = 0; index < seen.size(); ++index) {
        if (seen[index] != 1) {
            violations.push_back({"I1", "item " + to_string(index) + " is on " + to_string(seen[index])
                                            + " trucks or left-over lists, not exactly one"});
        }
    }

    if (plan.reservations.size() != book.reservations.size()) {
        violations.push_back({"I12", "the plan reports " + to_string(plan.reservations.size()) + " of "
                                         + to_string(book.reservations.size()) + " reservations"});
    }
    map<LaneDate, long long> reservedByLaneDate;
    for (const Reservation& reservation : book.reservations) {
        reservedByLaneDate[{reservation.origin, reservation.destination, reservation.shipCondition,
                            reservation.shipDate}] += reservation.trucks;
    }
    for (const auto& [laneDate, built] : trucksByLaneDate) {
        const auto reserved = reservedByLaneDate.find(laneDate);
        const long long allowed = reserved == reservedByLaneDate.end() ? 0 : reserved->second;
        if (built > allowed) {
            violations.push_back({"I4", laneDateText(laneDate) + ": " + to_string(built) + " trucks built, "
                                            + to_string(allowed) + " reserved"});
        }
    }
    for (const ReservationUse& use : plan.reservations) {
        if (use.reservationIndex >= book.reservations.size()) continue;
        const Reservation& reservation = book.reservations[use.reservationIndex];
        const LaneDate laneDate{reservation.origin, reservation.destination, reservation.shipCondition,
                                reservation.shipDate};
        const auto built = trucksByLaneDate.find(laneDate);
        const long long recounted = built == trucksByLaneDate.end() ? 0 : built->second;
        if (use.built != recounted || use.reserved != reservation.trucks) {
            violations.push_back({"I12", laneDateText(laneDate) + ": the plan reports " + to_string(use.built)
                                             + " of " + to_string(use.reserved) + " used, the trucks show "
                                             + to_string(recounted) + " of " + to_string(reservation.trucks)});
        }
    }
    return violations;
}

bool samePlan(const SelectionPlan& left, const SelectionPlan& right) {
    if (left.trucks.size() != right.trucks.size() || left.leftOver.size() != right.leftOver.size()
        || left.reservations.size() != right.reservations.size()
        || left.penaltyExposureTrucks != right.penaltyExposureTrucks) {
        return false;
    }
    for (size_t index = 0; index < left.trucks.size(); ++index) {
        if (left.trucks[index].routeIndex != right.trucks[index].routeIndex
            || left.trucks[index].items != right.trucks[index].items) {
            return false;
        }
    }
    for (size_t index = 0; index < left.leftOver.size(); ++index) {
        if (left.leftOver[index].itemIndex != right.leftOver[index].itemIndex
            || left.leftOver[index].reason != right.leftOver[index].reason) {
            return false;
        }
    }
    for (size_t index = 0; index < left.reservations.size(); ++index) {
        if (left.reservations[index].built != right.reservations[index].built) return false;
    }
    return true;
}

void printBenchmark(ostream& out, const PipelineResult& run, const LaneClassification& classification,
                    const vector<RouteBenchmark>& benchmarks) {
    out << "M4 METHOD BENCHMARK\n";
    out << "  Each method: trucks / unit loads shipped / average cube / seconds; '-' threw, '!' over budget.\n\n";
    vector<vector<string>> rows;
    vector<string> header{"lane", "ship date", "reserved", "products", "per product", "routed"};
    for (const PackingMethod method : benchmarkedMethods()) header.push_back(packingMethodName(method));
    header.push_back("best");
    for (const RouteBenchmark& benchmark : benchmarks) {
        const LaneRoute& route = classification.routes[benchmark.routeIndex];
        const GroupKey& key = run.segregation.groups[route.groupIndex].key;
        vector<string> row{printable(key.locationFrom) + " -> " + printable(key.locationTo) + " " + printable(key.shipCondition)
                               + (key.isSegregated ? " (" + printable(key.segregant) + ")" : ""),
                           printable(route.shipDate), grouped(static_cast<double>(benchmark.reserved)),
                           grouped(static_cast<double>(benchmark.products)), fixed(benchmark.unitLoadsPerProduct, 1),
                           solveMethodName(benchmark.routedTo)};
        for (const MethodScore& score : benchmark.scores) {
            if (!score.ran) {
                row.push_back("-");
                continue;
            }
            row.push_back(grouped(static_cast<double>(score.trucks)) + " / " + grouped(score.unitLoadsShipped, 1) + " / "
                          + fixed(score.averageCube * 100.0, 1) + "% / " + fixed(score.seconds, 2)
                          + (score.withinBudget ? "" : " !"));
        }
        row.push_back(benchmark.best ? packingMethodName(*benchmark.best) : "none");
        rows.push_back(std::move(row));
    }
    vector<bool> rightAligned(header.size(), false);
    for (const size_t column : {2, 3, 4}) rightAligned[column] = true;
    printColumns(out, header, rows, rightAligned);
}

} // namespace ob
