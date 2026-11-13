#include "m5Solver.hpp"
#include "columnGeneration.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>

using namespace std;

namespace ob {
namespace m5 {

namespace {

constexpr const char* kCapacityModel = "capacityOnly/allRequired/groupsSeparate";

struct GroupItems {
    long long capacity = 0;
    vector<size_t> items;       // model indices that fit
};

// Packable items by group, and the impossible ones.
vector<GroupItems> groupsOf(const ScalarModel& model, vector<size_t>* impossible) {
    map<size_t, GroupItems> byGroup;
    for (size_t item = 0; item < model.sizes.size(); ++item) {
        const size_t group = model.groupOf.empty() ? 0 : model.groupOf[item];
        GroupItems& entry = byGroup[group];
        entry.capacity = model.capacityOfGroup.empty() ? model.capacity : model.capacityOfGroup.at(group);
        if (model.sizes[item] > entry.capacity || model.sizes[item] <= 0) {
            if (impossible) impossible->push_back(item);
            continue;
        }
        entry.items.push_back(item);
    }
    vector<GroupItems> groups;
    for (auto& [group, entry] : byGroup) groups.push_back(std::move(entry));
    return groups;
}

long long groupBound(const ScalarModel& model, const GroupItems& group) {
    long long total = 0, overHalf = 0;
    for (const size_t item : group.items) {
        total += model.sizes[item];
        if (2 * model.sizes[item] > group.capacity) ++overHalf;
    }
    const long long volume = (total + group.capacity - 1) / group.capacity;
    return max(volume, overHalf);
}

vector<vector<size_t>> ffdOf(const ScalarModel& model, const GroupItems& group) {
    vector<size_t> order = group.items;
    stable_sort(order.begin(), order.end(), [&](size_t left, size_t right) { return model.sizes[left] > model.sizes[right]; });
    vector<vector<size_t>> bins;
    vector<long long> room;
    for (const size_t item : order) {
        size_t bin = 0;
        while (bin < bins.size() && room[bin] < model.sizes[item]) ++bin;
        if (bin == bins.size()) {
            bins.emplace_back();
            room.push_back(group.capacity);
        }
        bins[bin].push_back(item);
        room[bin] -= model.sizes[item];
    }
    return bins;
}

class ExactSearch {
public:
    ExactSearch(const ScalarModel& model, const GroupItems& group, long long nodeLimit, long long bound)
        : model_(model), capacity_(group.capacity), nodeLimit_(nodeLimit), bound_(bound), order_(group.items) {
        stable_sort(order_.begin(), order_.end(), [&](size_t left, size_t right) { return model.sizes[left] > model.sizes[right]; });
        suffix_.assign(order_.size() + 1, 0);
        for (size_t position = order_.size(); position-- > 0;) suffix_[position] = suffix_[position + 1] + model.sizes[order_[position]];
        best_ = ffdOf(model, group);
    }

    // The proven-best bins, or empty when the node limit stopped the search first.
    optional<vector<vector<size_t>>> run() {
        if (static_cast<long long>(best_.size()) > bound_) search(0);
        if (stopped_) return nullopt;
        return best_;
    }

private:
    void search(size_t position) {
        if (stopped_ || static_cast<long long>(best_.size()) == bound_) return;
        if (++nodes_ > nodeLimit_) {
            stopped_ = true;
            return;
        }
        if (position == order_.size()) {
            if (bins_.size() < best_.size()) best_ = bins_;
            return;
        }
        long long freeRoom = 0;
        for (const long long left : room_) freeRoom += left;
        const long long overflow = max(0LL, suffix_[position] - freeRoom);
        const long long needed = static_cast<long long>(bins_.size()) + (overflow + capacity_ - 1) / capacity_;
        if (needed >= static_cast<long long>(best_.size())) return;
        const size_t item = order_[position];
        const long long size = model_.sizes[item];
        vector<long long> tried;  // bins with the same room are interchangeable
        for (size_t bin = 0; bin < bins_.size(); ++bin) {
            if (room_[bin] < size || find(tried.begin(), tried.end(), room_[bin]) != tried.end()) continue;
            tried.push_back(room_[bin]);
            bins_[bin].push_back(item);
            room_[bin] -= size;
            search(position + 1);
            room_[bin] += size;
            bins_[bin].pop_back();
        }
        if (bins_.size() + 1 < best_.size()) {
            bins_.push_back({item});
            room_.push_back(capacity_ - size);
            search(position + 1);
            bins_.pop_back();
            room_.pop_back();
        }
    }

    const ScalarModel& model_;
    long long capacity_;
    long long nodeLimit_;
    long long bound_;
    vector<size_t> order_;
    vector<long long> suffix_;
    vector<vector<size_t>> best_;
    vector<vector<size_t>> bins_;
    vector<long long> room_;
    long long nodes_ = 0;
    bool stopped_ = false;
};

// The fewest columns, in whole copies, covering the demand: the restricted-master integer optimum.
// Empty when the search stops at its node limit.
optional<long long> restrictedIntegerOptimum(const vector<vector<int>>& columns, const vector<long long>& demand,
                                             long long nodeLimit) {
    long long best = 0;
    for (const long long count : demand) best += count;  // one item per bin always covers
    long long nodes = 0;
    bool stopped = false;
    vector<long long> left = demand;
    function<void(size_t, long long)> search = [&](size_t column, long long used) {
        if (stopped || used >= best) return;
        if (++nodes > nodeLimit) {
            stopped = true;
            return;
        }
        if (all_of(left.begin(), left.end(), [](long long count) { return count <= 0; })) {
            best = used;
            return;
        }
        if (column == columns.size()) return;
        long long most = 0;
        for (size_t type = 0; type < left.size(); ++type) {
            if (left[type] > 0 && columns[column][type] > 0) most = max(most, (left[type] + columns[column][type] - 1) / columns[column][type]);
        }
        for (long long copies = most; copies >= 0; --copies) {
            for (size_t type = 0; type < left.size(); ++type) left[type] -= copies * columns[column][type];
            search(column + 1, used + copies);
            for (size_t type = 0; type < left.size(); ++type) left[type] += copies * columns[column][type];
        }
    };
    search(0, 0);
    if (stopped) return nullopt;
    return best;
}

struct GroupSolve {
    vector<vector<size_t>> bins;
    long long bound = 0;
    optional<double> lpValue;
    string lpStatus = "notRun";
    optional<long long> restrictedInteger;
    bool proven = false;
    Termination termination = Termination::Completed;
    size_t columns = 0;
};

// Column generation over one group's distinct sizes: the LP value, a Farley bound, and residual
// rounding that re-prices what the whole copies of the LP's columns leave (Section 8.4).
GroupSolve solveGroup(const ScalarModel& model, const GroupItems& group, const ScalarLimits& limits, const SolverStop& stop) {
    GroupSolve solve;
    solve.bins = ffdOf(model, group);
    solve.bound = groupBound(model, group);
    if (group.items.empty()) {
        solve.proven = true;
        return solve;
    }

    map<long long, vector<size_t>, greater<long long>> itemsOfSize;
    for (const size_t item : group.items) itemsOfSize[model.sizes[item]].push_back(item);
    vector<long long> sizes, demand;
    for (const auto& [size, items] : itemsOfSize) {
        sizes.push_back(size);
        demand.push_back(static_cast<long long>(items.size()));
    }
    vector<vector<int>> columns;
    for (size_t type = 0; type < sizes.size(); ++type) {
        vector<int> column(sizes.size(), 0);
        column[type] = static_cast<int>(min(demand[type], group.capacity / sizes[type]));
        columns.push_back(std::move(column));
    }

    // One LP to convergence over `residual` demand; returns the solution and whether pricing was exact.
    auto converge = [&](const vector<long long>& residual, bool& exact, double& farley) {
        CoveringLpSolution lp;
        exact = true;
        farley = 0.0;
        for (int round = 0; round < limits.maxColumnRounds; ++round) {
            if (stop && stop()) {
                solve.termination = Termination::Budget;
                break;
            }
            lp = solveCoveringLp(columns, residual, limits.simplexIterationLimit);
            if (!lp.solved) {
                if (lp.iterationCapHit) solve.termination = Termination::IterationCap;
                break;
            }
            const PricedPattern priced = pricePattern(sizes, group.capacity, lp.typeValues, residual, limits.pricingNodeLimit);
            exact = priced.exact;
            double demandValue = 0.0;
            for (size_t type = 0; type < residual.size(); ++type) demandValue += static_cast<double>(residual[type]) * lp.typeValues[type];
            if (priced.exact) farley = max(farley, demandValue / max(1.0, priced.value));
            const bool duplicate = find(columns.begin(), columns.end(), priced.counts) != columns.end();
            if (priced.value <= 1.0 + kLpEpsilon || duplicate || !validColumn(priced.counts, sizes, group.capacity)) break;
            columns.push_back(priced.counts);
        }
        return lp;
    };

    bool exact = true;
    double farley = 0.0;
    const CoveringLpSolution root = converge(demand, exact, farley);
    if (root.solved) {
        solve.lpValue = root.value;
        solve.lpStatus = exact ? "converged" : "pricingIncomplete";
        if (exact) solve.bound = max(solve.bound, lpCeil(farley));
        solve.restrictedInteger = restrictedIntegerOptimum(columns, demand, limits.restrictedIntegerNodeLimit);
    }
    solve.columns = columns.size();

    // Residual rounding: whole copies of each LP column, then the LP again on what is left, while
    // each round fixes at least one bin and so strictly shrinks the residual.
    vector<vector<int>> fixedColumns;
    vector<long long> residual = demand;
    CoveringLpSolution lp = root;
    while (lp.solved && !(stop && stop())) {
        bool progress = false;
        for (size_t column = 0; column < columns.size() && column < lp.patternUses.size(); ++column) {
            for (long long copy = 0; copy < lpFloor(lp.patternUses[column]); ++copy) {
                bool fits = true;
                for (size_t type = 0; type < residual.size(); ++type) fits = fits && columns[column][type] <= residual[type];
                if (!fits) break;
                for (size_t type = 0; type < residual.size(); ++type) residual[type] -= columns[column][type];
                fixedColumns.push_back(columns[column]);
                progress = true;
            }
        }
        if (!progress || all_of(residual.begin(), residual.end(), [](long long count) { return count == 0; })) break;
        bool residualExact = true;
        double residualFarley = 0.0;
        lp = converge(residual, residualExact, residualFarley);
    }
    // The fixed columns as bins, then the residual packed exactly when small, else first-fit.
    map<long long, size_t> nextOfSize;
    vector<vector<size_t>> completed;
    for (const vector<int>& column : fixedColumns) {
        vector<size_t> bin;
        for (size_t type = 0; type < sizes.size(); ++type) {
            for (int copy = 0; copy < column[type]; ++copy) bin.push_back(itemsOfSize[sizes[type]][nextOfSize[sizes[type]]++]);
        }
        completed.push_back(std::move(bin));
    }
    GroupItems rest;
    rest.capacity = group.capacity;
    for (size_t type = 0; type < sizes.size(); ++type) {
        const vector<size_t>& items = itemsOfSize[sizes[type]];
        for (size_t position = nextOfSize[sizes[type]]; position < items.size(); ++position) rest.items.push_back(items[position]);
    }
    optional<vector<vector<size_t>>> restBins;
    if (rest.items.size() <= limits.exactMaxItems) restBins = ExactSearch(model, rest, limits.exactNodeLimit, groupBound(model, rest)).run();
    if (!restBins) restBins = ffdOf(model, rest);
    completed.insert(completed.end(), restBins->begin(), restBins->end());
    if (completed.size() < solve.bins.size()) solve.bins = std::move(completed);

    if (static_cast<long long>(solve.bins.size()) == solve.bound) {
        solve.proven = true;
    } else if (group.items.size() <= limits.exactMaxItems && !(stop && stop())) {
        if (optional<vector<vector<size_t>>> exactBins = ExactSearch(model, group, limits.exactNodeLimit, solve.bound).run()) {
            solve.bins = std::move(*exactBins);
            solve.bound = static_cast<long long>(solve.bins.size());
            solve.proven = true;
        }
    }
    return solve;
}

}  // namespace

long long scalarLowerBound(const ScalarModel& model) {
    long long bound = 0;
    for (const GroupItems& group : groupsOf(model, nullptr)) bound += groupBound(model, group);
    return bound;
}

ScalarPacking scalarFirstFitDecreasing(const ScalarModel& model) {
    ScalarPacking packing;
    for (const GroupItems& group : groupsOf(model, nullptr)) {
        for (vector<size_t>& bin : ffdOf(model, group)) packing.bins.push_back(std::move(bin));
    }
    return packing;
}

optional<ScalarPacking> scalarExact(const ScalarModel& model, long long nodeLimit) {
    ScalarPacking packing;
    for (const GroupItems& group : groupsOf(model, nullptr)) {
        optional<vector<vector<size_t>>> bins = ExactSearch(model, group, nodeLimit, groupBound(model, group)).run();
        if (!bins) return nullopt;
        for (vector<size_t>& bin : *bins) packing.bins.push_back(std::move(bin));
    }
    return packing;
}

ScalarResult solveCuttingStock(const ScalarModel& model, const ScalarLimits& limits, const SolverStop& stop) {
    ScalarResult result;
    const vector<GroupItems> groups = groupsOf(model, &result.impossible);
    result.ffdBins = static_cast<long long>(scalarFirstFitDecreasing(model).bins.size());
    if (stop && stop()) {
        result.evidence.termination = Termination::Budget;
        result.evidence.proof = ProofStatus::NotAttempted;
        return result;
    }
    ScalarPacking packing;
    bool proven = true;
    long long bound = 0;
    double lpValue = 0.0;
    long long restricted = 0;
    bool lpConverged = true, lpRun = true, restrictedKnown = true;
    for (const GroupItems& group : groups) {
        GroupSolve solve = solveGroup(model, group, limits, stop);
        for (vector<size_t>& bin : solve.bins) packing.bins.push_back(std::move(bin));
        proven = proven && solve.proven;
        bound += solve.bound;
        if (solve.lpValue) {
            lpValue += *solve.lpValue;
        } else if (!group.items.empty()) {
            lpRun = false;
        }
        lpConverged = lpConverged && (solve.lpStatus == "converged" || group.items.empty());
        if (solve.restrictedInteger) {
            restricted += *solve.restrictedInteger;
        } else if (!group.items.empty()) {
            restrictedKnown = false;
        }
        if (solve.termination != Termination::Completed) result.evidence.termination = solve.termination;
        result.evidence.columnsGenerated += solve.columns;
    }
    SolverEvidence& evidence = result.evidence;
    if (lpRun) evidence.lpValue = lpValue;
    evidence.lpStatus = !lpRun ? "notRun" : lpConverged ? "converged" : "pricingIncomplete";
    if (restrictedKnown && lpRun) evidence.restrictedMasterInteger = restricted;
    evidence.incumbent = static_cast<long long>(packing.bins.size());
    evidence.lowerBound = bound;
    evidence.boundModel = kCapacityModel;
    evidence.proofModel = kCapacityModel;
    evidence.proof = proven ? ProofStatus::ProvenOptimal : ProofStatus::Unproven;
    result.packing = std::move(packing);
    return result;
}

bool shouldEscalate(long long quick, long long bound, long long gapTrucks, bool allowanceLeft) {
    return allowanceLeft && quick - bound >= gapTrucks;
}

string gapText(long long result, optional<long long> bound) {
    if (!bound || *bound <= 0) return "N/A (no valid bound)";
    const long long gap = result - *bound;
    ostringstream out;
    out.setf(ios::fixed);
    out.precision(1);
    out << gap << " truck" << (gap == 1 ? "" : "s") << ", " << 100.0 * static_cast<double>(gap) / static_cast<double>(*bound)
        << "% above the bound of " << *bound;
    return out.str();
}

PricedPattern pricePattern(const vector<long long>& sizes, long long capacity, const vector<double>& duals,
                           const vector<long long>& available, long long nodeLimit) {
    vector<PricingType> types;
    types.reserve(sizes.size());
    for (const long long size : sizes) types.push_back({size, 0});
    PatternPricer pricer(types, capacity, 0, nodeLimit);
    const PatternPricer::Priced priced = pricer.best(duals, available);
    return {priced.counts, priced.value, priced.exact};
}

bool validColumn(const vector<int>& column, const vector<long long>& sizes, long long capacity) {
    if (column.size() != sizes.size()) return false;
    long long used = 0;
    bool any = false;
    for (size_t type = 0; type < column.size(); ++type) {
        if (column[type] < 0) return false;
        used += column[type] * sizes[type];
        any = any || column[type] > 0;
    }
    return any && used <= capacity;
}

AcceptedResult solveAccepted(const AcceptedModel& model) {
    const size_t count = model.cube.size();
    if (count > kAcceptedModelMaxItems || model.weight.size() != count) {
        throw invalid_argument("acceptance model: at most " + to_string(kAcceptedModelMaxItems) + " items, one weight per cube");
    }
    AcceptedResult result;
    double totalCube = 0.0;
    for (const double cube : model.cube) totalCube += cube;
    result.capacityBound = static_cast<long long>(ceil(totalCube / model.cubeCapacity - 1e-12));

    // Each item goes unshipped (-1) or into a bin no later than one past the highest used, so bin
    // order never repeats a partition.
    vector<int> binOf(count, -1);
    optional<ScalarPacking> bestAll;
    double bestPartialCube = -1.0;
    size_t bestPartialBins = 0;
    auto evaluate = [&](int binsUsed) {
        vector<double> cube(static_cast<size_t>(binsUsed), 0.0), weight(static_cast<size_t>(binsUsed), 0.0);
        bool everything = true;
        double shipped = 0.0;
        for (size_t item = 0; item < count; ++item) {
            if (binOf[item] < 0) {
                everything = false;
                continue;
            }
            cube[static_cast<size_t>(binOf[item])] += model.cube[item];
            weight[static_cast<size_t>(binOf[item])] += model.weight[item];
            shipped += model.cube[item];
        }
        for (int bin = 0; bin < binsUsed; ++bin) {
            const size_t index = static_cast<size_t>(bin);
            if (cube[index] > model.cubeCapacity || weight[index] > model.weightCapacity) return;
            if (!(weight[index] > model.xWeight || cube[index] > model.yCube)) return;
        }
        ScalarPacking packing;
        packing.bins.resize(static_cast<size_t>(binsUsed));
        for (size_t item = 0; item < count; ++item) {
            if (binOf[item] >= 0) packing.bins[static_cast<size_t>(binOf[item])].push_back(item);
        }
        if (everything && (!bestAll || packing.bins.size() < bestAll->bins.size())) bestAll = packing;
        if (shipped > bestPartialCube || (shipped == bestPartialCube && packing.bins.size() < bestPartialBins)) {
            bestPartialCube = shipped;
            bestPartialBins = packing.bins.size();
            result.partial = std::move(packing);
        }
    };
    function<void(size_t, int)> assign = [&](size_t item, int binsUsed) {
        if (item == count) {
            evaluate(binsUsed);
            return;
        }
        for (int bin = -1; bin <= binsUsed && bin < static_cast<int>(model.bins); ++bin) {
            binOf[item] = bin;
            assign(item + 1, bin == binsUsed ? binsUsed + 1 : binsUsed);
        }
        binOf[item] = -1;
    };
    assign(0, 0);
    result.partialCube = max(0.0, bestPartialCube);
    result.allGoodsPacking = bestAll;
    result.allGoods = bestAll ? ProofStatus::ProvenOptimal : ProofStatus::ProvenInfeasible;
    return result;
}

} // namespace m5
} // namespace ob
