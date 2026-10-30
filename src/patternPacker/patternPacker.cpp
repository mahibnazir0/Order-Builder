#include "patternPacker.hpp"
#include "columnGeneration.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <utility>

using namespace std;

namespace ob {

namespace {

struct ItemType {
    long long areaGrains = 0;
    long long weightGrains = 0;
    vector<size_t> items;  // input indices, most urgent first
};

bool isPositiveFinite(double value) { return isfinite(value) && value > 0.0; }

} // anonymous namespace

PatternPacking packByPatterns(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                              const PatternPackerSettings& settings, int reservedTrucks) {
    if (!isPositiveFinite(settings.weightGrainLb) || !isPositiveFinite(settings.areaGrainSqIn)) {
        throw invalid_argument("Pattern packer settings: weight and area grains must be positive and finite");
    }
    if (settings.maxPatterns < 1 || settings.pricingNodeLimit < 1) {
        throw invalid_argument("Pattern packer settings: pattern and node limits must be at least 1");
    }
    if (reservedTrucks < 0) {
        throw invalid_argument("Pattern packer: reserved trucks " + to_string(reservedTrucks) + " is negative");
    }
    const KnapsackProblem problem = prepareKnapsack(
        items, capacity, {settings.weightGrainLb, settings.areaGrainSqIn, 1});

    // Types by size and weight in grains, largest first; within a type the candidates keep
    // the problem's order, most urgent first.
    map<pair<long long, long long>, size_t, greater<pair<long long, long long>>> typeOf;
    for (size_t position = 0; position < problem.candidates.size(); ++position) {
        typeOf.emplace(make_pair(problem.areaGrains[position], problem.weightGrains[position]), 0);
    }
    vector<ItemType> types;
    types.reserve(typeOf.size());
    for (auto& [key, index] : typeOf) {
        index = types.size();
        types.push_back({key.first, key.second, {}});
    }
    for (size_t position = 0; position < problem.candidates.size(); ++position) {
        types[typeOf.at({problem.areaGrains[position], problem.weightGrains[position]})].items.push_back(
            problem.candidates[position]);
    }
    vector<long long> demand(types.size());
    for (size_t type = 0; type < types.size(); ++type) demand[type] = static_cast<long long>(types[type].items.size());

    PatternPacking packing;
    packing.typeCount = types.size();
    long long totalArea = 0, totalWeight = 0;
    for (const ItemType& type : types) {
        totalArea += type.areaGrains * static_cast<long long>(type.items.size());
        totalWeight += type.weightGrains * static_cast<long long>(type.items.size());
    }
    if (!types.empty()) {
        const double byArea = static_cast<double>(totalArea) / static_cast<double>(problem.areaCapacity);
        const double byWeight = problem.weightCapacity > 0
            ? static_cast<double>(totalWeight) / static_cast<double>(problem.weightCapacity) : 0.0;
        packing.volumeBoundTrucks = static_cast<long long>(ceil(max(byArea, byWeight) - kLpEpsilon));
    }

    vector<PricingType> pricingTypes;
    pricingTypes.reserve(types.size());
    for (const ItemType& type : types) pricingTypes.push_back({type.areaGrains, type.weightGrains});
    PatternPricer pricer(pricingTypes, problem.areaCapacity, problem.weightCapacity, settings.pricingNodeLimit);

    // Starting columns: each type alone, as many as one truck holds. Every candidate fits an
    // empty truck, so each holds at least one and the LP is bounded.
    vector<vector<int>> columns;
    set<vector<int>> seen;
    for (size_t type = 0; type < types.size(); ++type) {
        vector<int> pure(types.size(), 0);
        long long most = min(demand[type], problem.areaCapacity / types[type].areaGrains);
        if (types[type].weightGrains > 0) most = min(most, problem.weightCapacity / types[type].weightGrains);
        pure[type] = static_cast<int>(most);
        if (seen.insert(pure).second) columns.push_back(std::move(pure));
    }

    CoveringLpSolution lp;
    if (!types.empty()) {
        for (int round = 0;; ++round) {
            lp = solveCoveringLp(columns, demand);
            if (!lp.solved) {
                packing.unstable = true;
                break;
            }
            if (round >= settings.maxPatterns) break;
            const PatternPricer::Priced priced = pricer.best(lp.typeValues, demand);
            // A pattern worth more than one truck improves the LP whether or not the search
            // finished; only "none is worth more" needs a finished search to be proven.
            if (priced.value <= 1.0 + kLpEpsilon) {
                packing.converged = priced.exact;
                break;
            }
            // A column already in the LP cannot price above one truck unless the duals are off:
            // stop rather than loop on it.
            if (!seen.insert(priced.counts).second) {
                packing.unstable = true;
                break;
            }
            columns.push_back(priced.counts);
        }
    }
    packing.patternsGenerated = columns.size();
    if (packing.converged) {
        packing.lpValue = lp.value;
        packing.lpTrucks = static_cast<long long>(ceil(lp.value - 1e-6));
    }

    // Whole copies of the LP's patterns, never more of a type than is left.
    vector<long long> residual = demand;
    vector<vector<int>> truckLoads;
    auto takeLoad = [&](vector<int> load) {
        for (size_t type = 0; type < types.size(); ++type) {
            load[type] = static_cast<int>(min<long long>(load[type], residual[type]));
            residual[type] -= load[type];
        }
        if (any_of(load.begin(), load.end(), [](int count) { return count > 0; })) {
            truckLoads.push_back(std::move(load));
        }
    };
    if (lp.solved) {
        vector<size_t> byUse(columns.size());
        iota(byUse.begin(), byUse.end(), size_t{0});
        stable_sort(byUse.begin(), byUse.end(),
                    [&](size_t left, size_t right) { return lp.patternUses[left] > lp.patternUses[right]; });
        for (const size_t column : byUse) {
            const long long copies = static_cast<long long>(floor(lp.patternUses[column] + 1e-6));
            for (long long copy = 0; copy < copies; ++copy) takeLoad(columns[column]);
        }
    }
    // The rest truck by truck, each covering as much of the floor and payload as it can.
    vector<double> fillValues(types.size());
    for (size_t type = 0; type < types.size(); ++type) {
        fillValues[type] = static_cast<double>(types[type].areaGrains) / static_cast<double>(problem.areaCapacity)
            + (problem.weightCapacity > 0
                   ? static_cast<double>(types[type].weightGrains) / static_cast<double>(problem.weightCapacity)
                   : 0.0)
            + 1e-6;
    }
    while (any_of(residual.begin(), residual.end(), [](long long count) { return count > 0; })) {
        PatternPricer::Priced fill = pricer.best(fillValues, residual);
        if (none_of(fill.counts.begin(), fill.counts.end(), [](int count) { return count > 0; })) {
            // Cannot happen: every type fits an empty truck. Take one item so the loop ends.
            fill.counts.assign(types.size(), 0);
            for (size_t type = 0; type < types.size(); ++type) {
                if (residual[type] > 0) {
                    fill.counts[type] = 1;
                    break;
                }
            }
        }
        takeLoad(fill.counts);
    }
    packing.integerTrucks = static_cast<long long>(truckLoads.size());
    if (packing.converged) packing.gapToLp = packing.integerTrucks - packing.lpTrucks;
    packing.provenOptimal =
        packing.integerTrucks <= max(packing.volumeBoundTrucks, packing.converged ? packing.lpTrucks : 0);

    // Fullest trucks first, so the reservation keeps them and drops the emptiest.
    auto loadArea = [&](const vector<int>& load) {
        long long area = 0, weight = 0;
        for (size_t type = 0; type < types.size(); ++type) {
            area += load[type] * types[type].areaGrains;
            weight += load[type] * types[type].weightGrains;
        }
        return make_pair(area, weight);
    };
    stable_sort(truckLoads.begin(), truckLoads.end(),
                [&](const vector<int>& left, const vector<int>& right) { return loadArea(left) > loadArea(right); });

    vector<char> placed(items.size(), 0);
    vector<size_t> nextOfType(types.size(), 0);
    map<vector<int>, size_t> patternIndex;
    const size_t kept = min(truckLoads.size(), static_cast<size_t>(reservedTrucks));
    for (size_t truckIndex = 0; truckIndex < kept; ++truckIndex) {
        const vector<int>& load = truckLoads[truckIndex];
        PackedTruck truck;
        const auto [found, isNew] = patternIndex.emplace(load, packing.patterns.size());
        if (isNew) packing.patterns.push_back(load);
        truck.pattern = found->second;
        for (size_t type = 0; type < types.size(); ++type) {
            for (int count = 0; count < load[type]; ++count) {
                const size_t index = types[type].items[nextOfType[type]++];
                truck.items.push_back(index);
                placed[index] = 1;
                truck.areaUsedSqIn += items[index].areaSqIn;
                truck.weightUsedLb += items[index].weightLb;
            }
        }
        sort(truck.items.begin(), truck.items.end());
        packing.trucks.push_back(std::move(truck));
    }
    for (size_t index = 0; index < items.size(); ++index) {
        if (!placed[index]) packing.leftOver.push_back(index);
    }
    return packing;
}

} // namespace ob
