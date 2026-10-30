#include "patternPacker.hpp"
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

// Pivot and reduced-cost tolerance for the LP. Coarser than kQuantityEpsilon because the
// tableau accumulates rounding over many pivots.
constexpr double kLpEpsilon = 1e-9;

struct ItemType {
    long long areaGrains = 0;
    long long weightGrains = 0;
    vector<size_t> items;  // input indices, most urgent first
};

struct LpSolution {
    bool solved = false;
    double value = 0.0;
    vector<double> typeValues;   // the duals that price new patterns
    vector<double> patternUses;  // trucks of each pattern the relaxation uses
};

// The LP over the current patterns, solved through its dual: maximise sum(demand x value)
// with every pattern's value at most one truck. Its slack basis is feasible from the start,
// and the pattern uses come back as the duals of its rows. Dense tableau, Bland's rule so it
// cannot cycle; an iteration cap guards the run anyway.
LpSolution solvePatternLp(const vector<vector<int>>& patterns, const vector<long long>& demand) {
    const size_t rows = patterns.size(), types = demand.size();
    const size_t columns = types + rows + 1;
    vector<vector<double>> tableau(rows + 1, vector<double>(columns, 0.0));
    vector<size_t> basis(rows);
    for (size_t row = 0; row < rows; ++row) {
        for (size_t type = 0; type < types; ++type) tableau[row][type] = patterns[row][type];
        tableau[row][types + row] = 1.0;
        tableau[row][columns - 1] = 1.0;
        basis[row] = types + row;
    }
    vector<double>& objective = tableau[rows];
    for (size_t type = 0; type < types; ++type) objective[type] = -static_cast<double>(demand[type]);

    LpSolution solution;
    const size_t iterationCap = 50 * (rows + types) + 100;
    for (size_t iteration = 0;; ++iteration) {
        if (iteration > iterationCap) return solution;
        size_t entering = columns;
        for (size_t column = 0; column + 1 < columns; ++column) {
            if (objective[column] < -kLpEpsilon) {
                entering = column;
                break;
            }
        }
        if (entering == columns) break;
        size_t leaving = rows;
        double bestRatio = 0.0;
        for (size_t row = 0; row < rows; ++row) {
            if (tableau[row][entering] <= kLpEpsilon) continue;
            const double ratio = tableau[row][columns - 1] / tableau[row][entering];
            if (leaving == rows || ratio < bestRatio - kLpEpsilon
                || (ratio <= bestRatio + kLpEpsilon && basis[row] < basis[leaving])) {
                leaving = row;
                bestRatio = ratio;
            }
        }
        // Every type has a pattern carrying it, so no value can grow without bound.
        if (leaving == rows) return solution;
        const double pivot = tableau[leaving][entering];
        for (double& cell : tableau[leaving]) cell /= pivot;
        for (size_t row = 0; row <= rows; ++row) {
            if (row == leaving || fabs(tableau[row][entering]) <= 0.0) continue;
            const double factor = tableau[row][entering];
            for (size_t column = 0; column < columns; ++column) {
                tableau[row][column] -= factor * tableau[leaving][column];
            }
        }
        basis[leaving] = entering;
    }

    solution.solved = true;
    solution.value = objective[columns - 1];
    solution.typeValues.assign(types, 0.0);
    for (size_t row = 0; row < rows; ++row) {
        if (basis[row] < types) solution.typeValues[basis[row]] = tableau[row][columns - 1];
    }
    solution.patternUses.resize(rows);
    for (size_t row = 0; row < rows; ++row) solution.patternUses[row] = max(0.0, objective[types + row]);
    return solution;
}

// The truck load worth most at `values`: a bounded knapsack over the types, at most `available`
// of each, within the floor and payload. Depth-first over types by value per floor grain, the
// rest bounded by the smaller of the fractional fills of the floor and the payload left.
class PatternPricer {
public:
    PatternPricer(const vector<ItemType>& types, long long areaCapacity, long long weightCapacity,
                  long long nodeLimit)
        : types_(types), areaCapacity_(areaCapacity), weightCapacity_(weightCapacity),
          nodeLimit_(nodeLimit) {}

    struct Priced {
        vector<int> counts;
        double value = 0.0;
        bool exact = true;
    };

    Priced best(const vector<double>& values, const vector<long long>& available) {
        values_ = &values;
        available_ = &available;
        order_.clear();
        for (size_t type = 0; type < types_.size(); ++type) {
            if (values[type] > kLpEpsilon && available[type] > 0) order_.push_back(type);
        }
        sort(order_.begin(), order_.end(), [&](size_t left, size_t right) {
            const double leftDensity = values[left] / static_cast<double>(types_[left].areaGrains);
            const double rightDensity = values[right] / static_cast<double>(types_[right].areaGrains);
            return leftDensity != rightDensity ? leftDensity > rightDensity : left < right;
        });
        rank_.assign(types_.size(), order_.size());
        for (size_t position = 0; position < order_.size(); ++position) rank_[order_[position]] = position;
        weightOrder_ = order_;
        sort(weightOrder_.begin(), weightOrder_.end(), [&](size_t left, size_t right) {
            // Weightless types first: they cost no payload at all.
            const double leftGrains = static_cast<double>(types_[left].weightGrains);
            const double rightGrains = static_cast<double>(types_[right].weightGrains);
            const double leftDensity = leftGrains > 0.0 ? values[left] / leftGrains : INFINITY;
            const double rightDensity = rightGrains > 0.0 ? values[right] / rightGrains : INFINITY;
            return leftDensity != rightDensity ? leftDensity > rightDensity : left < right;
        });
        counts_.assign(types_.size(), 0);
        bestCounts_ = counts_;
        bestValue_ = 0.0;
        nodes_ = 0;
        exact_ = true;
        search(0, areaCapacity_, weightCapacity_, 0.0);
        return {bestCounts_, bestValue_, exact_};
    }

private:
    // The most the types still open at `position` could add within `capacityLeft` grains of
    // one resource: whole types in `byDensity` order (value per grain of that resource) while
    // they fit, then a fraction of the next. A type that uses none of the resource adds all of
    // what is available.
    double fractionalFill(const vector<size_t>& byDensity, size_t position, long long capacityLeft,
                          bool floor) const {
        double bound = 0.0;
        double left = static_cast<double>(capacityLeft);
        for (const size_t type : byDensity) {
            if (rank_[type] < position) continue;
            const double grains = static_cast<double>(floor ? types_[type].areaGrains : types_[type].weightGrains);
            const double available = static_cast<double>((*available_)[type]);
            const double count = grains > 0.0 ? min(available, left / grains) : available;
            bound += count * (*values_)[type];
            left -= count * grains;
            if (left <= 0.0) break;
        }
        return bound;
    }

    void search(size_t position, long long areaLeft, long long weightLeft, double value) {
        if (value > bestValue_ + kLpEpsilon) {
            bestValue_ = value;
            bestCounts_ = counts_;
        }
        if (position == order_.size()) return;
        if (++nodes_ > nodeLimit_) {
            exact_ = false;
            return;
        }
        const double bound = min(fractionalFill(order_, position, areaLeft, true),
                                 fractionalFill(weightOrder_, position, weightLeft, false));
        if (value + bound <= bestValue_ + kLpEpsilon) return;

        const size_t type = order_[position];
        const ItemType& itemType = types_[type];
        long long most = (*available_)[type];
        most = min(most, areaLeft / itemType.areaGrains);
        if (itemType.weightGrains > 0) most = min(most, weightLeft / itemType.weightGrains);
        for (long long count = most; count >= 0; --count) {
            counts_[type] = static_cast<int>(count);
            search(position + 1, areaLeft - count * itemType.areaGrains,
                   weightLeft - count * itemType.weightGrains,
                   value + static_cast<double>(count) * (*values_)[type]);
            if (!exact_) break;
        }
        counts_[type] = 0;
    }

    const vector<ItemType>& types_;
    long long areaCapacity_;
    long long weightCapacity_;
    long long nodeLimit_;
    const vector<double>* values_ = nullptr;
    const vector<long long>* available_ = nullptr;
    vector<size_t> order_;
    vector<size_t> weightOrder_;  // the same types by value per weight grain
    vector<size_t> rank_;         // each type's place in order_
    vector<int> counts_;
    vector<int> bestCounts_;
    double bestValue_ = 0.0;
    long long nodes_ = 0;
    bool exact_ = true;
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

    PatternPricer pricer(types, problem.areaCapacity, problem.weightCapacity, settings.pricingNodeLimit);

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

    LpSolution lp;
    if (!types.empty()) {
        for (int round = 0;; ++round) {
            lp = solvePatternLp(columns, demand);
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
