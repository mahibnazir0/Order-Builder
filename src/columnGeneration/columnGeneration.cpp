#include "columnGeneration.hpp"

#include <algorithm>
#include <cmath>

using namespace std;

namespace ob {

CoveringLpSolution solveCoveringLp(const vector<vector<int>>& patterns, const vector<long long>& demand) {
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

    CoveringLpSolution solution;
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

PatternPricer::PatternPricer(const vector<PricingType>& types, long long areaCapacity,
                             long long weightCapacity, long long nodeLimit,
                             const vector<vector<size_t>>* conflicts)
    : types_(types), areaCapacity_(areaCapacity), weightCapacity_(weightCapacity),
      nodeLimit_(nodeLimit), conflicts_(conflicts) {}

PatternPricer::Priced PatternPricer::best(const vector<double>& values, const vector<long long>& available) {
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

// The most the types still open at `position` could add within `capacityLeft` grains of one
// resource: whole types in `byDensity` order (value per grain of that resource) while they
// fit, then a fraction of the next. A type that uses none of the resource adds all of what is
// available. Conflicts are ignored, so this stays an upper bound.
double PatternPricer::fractionalFill(const vector<size_t>& byDensity, size_t position, long long capacityLeft,
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

void PatternPricer::search(size_t position, long long areaLeft, long long weightLeft, double value) {
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
    const PricingType& itemType = types_[type];
    long long most = (*available_)[type];
    if (conflicts_) {
        for (const size_t other : (*conflicts_)[type]) {
            if (counts_[other] > 0) most = 0;
        }
    }
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

} // namespace ob
