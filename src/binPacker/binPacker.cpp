#include "binPacker.hpp"
#include "columnGeneration.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

using namespace std;

namespace ob {

namespace {

// A truck as the positions of the problem's candidates it carries.
using UnitTruck = vector<size_t>;

bool isPositiveFinite(double value) { return isfinite(value) && value > 0.0; }

void validate(const BinPackerSettings& settings, int reservedTrucks) {
    if (!isPositiveFinite(settings.weightGrainLb) || !isPositiveFinite(settings.areaGrainSqIn)) {
        throw invalid_argument("Bin packer settings: weight and area grains must be positive and finite");
    }
    if (settings.columnRoundsPerNode < 1 || settings.pricingNodeLimit < 1 || settings.branchNodeLimit < 1
        || settings.maxSearchItems < 1) {
        throw invalid_argument("Bin packer settings: round, node and item limits must be at least 1");
    }
    if (reservedTrucks < 0) {
        throw invalid_argument("Bin packer: reserved trucks " + to_string(reservedTrucks) + " is negative");
    }
}

// The larger of a unit's share of the floor and of the payload.
double shareOfTruck(const KnapsackProblem& problem, size_t unit) {
    const double area = static_cast<double>(problem.areaGrains[unit]) / static_cast<double>(problem.areaCapacity);
    const double weight = problem.weightCapacity > 0
        ? static_cast<double>(problem.weightGrains[unit]) / static_cast<double>(problem.weightCapacity) : 0.0;
    return max(area, weight);
}

// Largest share first, each into the first truck it fits.
vector<UnitTruck> firstFitDecreasing(const KnapsackProblem& problem, vector<size_t> units) {
    stable_sort(units.begin(), units.end(), [&](size_t left, size_t right) {
        return shareOfTruck(problem, left) > shareOfTruck(problem, right);
    });
    vector<UnitTruck> trucks;
    vector<pair<long long, long long>> left;  // floor and weight grains left per truck
    for (const size_t unit : units) {
        size_t truck = 0;
        while (truck < trucks.size()
               && (problem.areaGrains[unit] > left[truck].first || problem.weightGrains[unit] > left[truck].second)) {
            ++truck;
        }
        if (truck == trucks.size()) {
            trucks.emplace_back();
            left.emplace_back(problem.areaCapacity, problem.weightCapacity);
        }
        trucks[truck].push_back(unit);
        left[truck].first -= problem.areaGrains[unit];
        left[truck].second -= problem.weightGrains[unit];
    }
    return trucks;
}

// One branch: pairs of units that must share a truck, and pairs that must not.
struct BranchNode {
    vector<pair<size_t, size_t>> together;
    vector<pair<size_t, size_t>> apart;
};

struct NodeResult {
    bool feasible = true;
    bool solved = false;
    bool converged = false;
    double lpValue = 0.0;
    vector<UnitTruck> columns;  // the node's patterns as units
    vector<double> uses;        // the LP's use of each
    optional<pair<size_t, size_t>> branchPair;  // units the LP half-puts together
};

class BranchAndPrice {
public:
    BranchAndPrice(const KnapsackProblem& problem, const BinPackerSettings& settings)
        : problem_(problem), settings_(settings), unitCount_(problem.candidates.size()) {}

    // Every unit packed, in as few trucks as the search finds within its limits.
    vector<UnitTruck> solve(vector<UnitTruck> warmStart, long long& lowerBound, bool& proven,
                            bool& fellBack, long long& nodes) {
        best_ = std::move(warmStart);
        vector<BranchNode> stack{BranchNode{}};
        bool everyCutProven = true;
        nodes = 0;
        while (!stack.empty() && nodes < settings_.branchNodeLimit
               && static_cast<long long>(best_.size()) > lowerBound) {
            const BranchNode node = std::move(stack.back());
            stack.pop_back();
            ++nodes;
            const NodeResult result = evaluate(node);
            if (!result.feasible) continue;
            if (!result.solved) {
                if (nodes == 1) {
                    fellBack = true;
                    return best_;
                }
                everyCutProven = false;
                continue;
            }
            const long long nodeBound = static_cast<long long>(ceil(result.lpValue - 1e-6));
            if (nodes == 1 && result.converged) lowerBound = max(lowerBound, nodeBound);
            keepIfBetter(roundToTrucks(result));
            if (result.converged && nodeBound >= static_cast<long long>(best_.size())) continue;
            if (!result.branchPair) {
                // Converged and integral, the rounding above already holds the node's optimum.
                if (!result.converged) everyCutProven = false;
                continue;
            }
            BranchNode apart = node, together = node;
            apart.apart.push_back(*result.branchPair);
            together.together.push_back(*result.branchPair);
            stack.push_back(std::move(apart));
            stack.push_back(std::move(together));
        }
        proven = static_cast<long long>(best_.size()) <= lowerBound || (stack.empty() && everyCutProven);
        return best_;
    }

private:
    NodeResult evaluate(const BranchNode& node) {
        NodeResult result;
        // Units forced together become one group; groups forced apart conflict.
        vector<size_t> parent(unitCount_);
        iota(parent.begin(), parent.end(), size_t{0});
        auto root = [&](size_t unit) {
            while (parent[unit] != unit) unit = parent[unit] = parent[parent[unit]];
            return unit;
        };
        for (const auto& [first, second] : node.together) {
            const size_t a = root(first), b = root(second);
            if (a != b) parent[max(a, b)] = min(a, b);
        }
        vector<size_t> groupOf(unitCount_);
        vector<vector<size_t>> members;
        map<size_t, size_t> groupOfRoot;
        for (size_t unit = 0; unit < unitCount_; ++unit) {
            const auto [found, isNew] = groupOfRoot.emplace(root(unit), members.size());
            if (isNew) members.emplace_back();
            groupOf[unit] = found->second;
            members[found->second].push_back(unit);
        }
        const size_t groups = members.size();
        vector<vector<size_t>> conflicts(groups);
        for (const auto& [first, second] : node.apart) {
            const size_t a = groupOf[first], b = groupOf[second];
            if (a == b) {
                result.feasible = false;
                return result;
            }
            conflicts[a].push_back(b);
            conflicts[b].push_back(a);
        }
        vector<PricingType> types(groups);
        for (size_t group = 0; group < groups; ++group) {
            for (const size_t unit : members[group]) {
                types[group].areaGrains += problem_.areaGrains[unit];
                types[group].weightGrains += problem_.weightGrains[unit];
            }
            if (types[group].areaGrains > problem_.areaCapacity || types[group].weightGrains > problem_.weightCapacity) {
                result.feasible = false;
                return result;
            }
        }

        // Starting columns: each group alone, then every pooled pattern this node allows.
        vector<vector<int>> patterns;
        set<vector<int>> seen;
        auto addPattern = [&](vector<int> pattern) {
            if (!seen.insert(pattern).second) return false;
            UnitTruck units;
            for (size_t group = 0; group < groups; ++group) {
                if (pattern[group] > 0) units.insert(units.end(), members[group].begin(), members[group].end());
            }
            sort(units.begin(), units.end());
            if (pool_.insert(units).second) poolOrder_.push_back(units);
            result.columns.push_back(std::move(units));
            patterns.push_back(std::move(pattern));
            return true;
        };
        for (size_t group = 0; group < groups; ++group) {
            vector<int> alone(groups, 0);
            alone[group] = 1;
            addPattern(std::move(alone));
        }
        for (const UnitTruck& pooled : poolOrder_) {
            if (auto pattern = allowedPattern(pooled, groupOf, members, conflicts)) addPattern(std::move(*pattern));
        }

        const vector<long long> demand(groups, 1);
        PatternPricer pricer(types, problem_.areaCapacity, problem_.weightCapacity,
                             settings_.pricingNodeLimit, &conflicts);
        CoveringLpSolution lp;
        for (int round = 0;; ++round) {
            lp = solveCoveringLp(patterns, demand);
            if (!lp.solved) return result;
            if (round >= settings_.columnRoundsPerNode) break;
            const PatternPricer::Priced priced = pricer.best(lp.typeValues, demand);
            if (priced.value <= 1.0 + kLpEpsilon) {
                result.converged = priced.exact;
                break;
            }
            if (!addPattern(priced.counts)) break;
        }
        result.solved = true;
        result.lpValue = lp.value;
        result.uses = lp.patternUses;

        // Ryan-Foster: the pair of groups whose shared use is nearest one half.
        map<pair<size_t, size_t>, double> together;
        for (size_t column = 0; column < patterns.size(); ++column) {
            if (result.uses[column] <= kLpEpsilon) continue;
            vector<size_t> inColumn;
            for (size_t group = 0; group < groups; ++group) {
                if (patterns[column][group] > 0) inColumn.push_back(group);
            }
            for (size_t first = 0; first < inColumn.size(); ++first) {
                for (size_t second = first + 1; second < inColumn.size(); ++second) {
                    together[{inColumn[first], inColumn[second]}] += result.uses[column];
                }
            }
        }
        double nearestHalf = 1.0;
        for (const auto& [groupPair, share] : together) {
            if (share <= 1e-6 || share >= 1.0 - 1e-6) continue;
            if (fabs(share - 0.5) < nearestHalf) {
                nearestHalf = fabs(share - 0.5);
                result.branchPair = make_pair(members[groupPair.first].front(), members[groupPair.second].front());
            }
        }
        return result;
    }

    // The pattern a pooled truck is at this node, or nullopt when it splits a group or joins
    // two that conflict.
    static optional<vector<int>> allowedPattern(const UnitTruck& units, const vector<size_t>& groupOf,
                                                const vector<vector<size_t>>& members,
                                                const vector<vector<size_t>>& conflicts) {
        vector<int> pattern(members.size(), 0);
        for (const size_t unit : units) ++pattern[groupOf[unit]];
        for (size_t group = 0; group < members.size(); ++group) {
            if (pattern[group] == 0) continue;
            if (static_cast<size_t>(pattern[group]) != members[group].size()) return nullopt;
            for (const size_t other : conflicts[group]) {
                if (pattern[other] > 0) return nullopt;
            }
            pattern[group] = 1;
        }
        return pattern;
    }

    // The LP's patterns used at least half a truck, most used first and each cut to what is
    // still unpacked, then first-fit decreasing for the rest. Any packing of every unit is a
    // valid incumbent, whatever this node's branching says.
    vector<UnitTruck> roundToTrucks(const NodeResult& result) const {
        vector<size_t> byUse(result.columns.size());
        iota(byUse.begin(), byUse.end(), size_t{0});
        stable_sort(byUse.begin(), byUse.end(),
                    [&](size_t left, size_t right) { return result.uses[left] > result.uses[right]; });
        vector<char> packed(unitCount_, 0);
        vector<UnitTruck> trucks;
        for (const size_t column : byUse) {
            if (result.uses[column] < 0.5) break;
            UnitTruck truck;
            for (const size_t unit : result.columns[column]) {
                if (!packed[unit]) {
                    packed[unit] = 1;
                    truck.push_back(unit);
                }
            }
            if (!truck.empty()) trucks.push_back(std::move(truck));
        }
        vector<size_t> rest;
        for (size_t unit = 0; unit < unitCount_; ++unit) {
            if (!packed[unit]) rest.push_back(unit);
        }
        for (UnitTruck& truck : firstFitDecreasing(problem_, std::move(rest))) trucks.push_back(std::move(truck));
        return trucks;
    }

    void keepIfBetter(vector<UnitTruck> trucks) {
        if (trucks.size() < best_.size()) best_ = std::move(trucks);
    }

    const KnapsackProblem& problem_;
    const BinPackerSettings& settings_;
    size_t unitCount_;
    vector<UnitTruck> best_;
    // Every pattern priced anywhere in the tree, offered again at each node that allows it.
    set<UnitTruck> pool_;
    vector<UnitTruck> poolOrder_;
};

// The trucks as input indices, fullest first, at most `reservedTrucks` of them.
BinPacking finish(const vector<KnapsackItem>& items, const KnapsackProblem& problem,
                  vector<UnitTruck> trucks, int reservedTrucks, BinPacking packing) {
    auto fill = [&](const UnitTruck& truck) {
        double share = 0.0;
        for (const size_t unit : truck) {
            share += static_cast<double>(problem.areaGrains[unit]) / static_cast<double>(problem.areaCapacity);
            if (problem.weightCapacity > 0) {
                share += static_cast<double>(problem.weightGrains[unit]) / static_cast<double>(problem.weightCapacity);
            }
        }
        return share;
    };
    stable_sort(trucks.begin(), trucks.end(),
                [&](const UnitTruck& left, const UnitTruck& right) { return fill(left) > fill(right); });
    packing.bestTrucks = static_cast<long long>(trucks.size());
    packing.overReservation = trucks.size() > static_cast<size_t>(reservedTrucks);
    vector<char> placed(items.size(), 0);
    for (size_t truckIndex = 0; truckIndex < trucks.size() && truckIndex < static_cast<size_t>(reservedTrucks);
         ++truckIndex) {
        BinTruck truck;
        for (const size_t unit : trucks[truckIndex]) {
            const size_t index = problem.candidates[unit];
            truck.items.push_back(index);
            placed[index] = 1;
            truck.areaUsedSqIn += items[index].areaSqIn;
            truck.weightUsedLb += items[index].weightLb;
        }
        sort(truck.items.begin(), truck.items.end());
        packing.trucks.push_back(std::move(truck));
    }
    for (size_t index = 0; index < items.size(); ++index) {
        if (!placed[index]) packing.leftOver.push_back(index);
    }
    packing.unfittable = problem.tooTall;
    packing.unfittable.insert(packing.unfittable.end(), problem.tooLarge.begin(), problem.tooLarge.end());
    sort(packing.unfittable.begin(), packing.unfittable.end());
    return packing;
}

long long volumeBound(const KnapsackProblem& problem) {
    long long area = 0, weight = 0;
    for (size_t unit = 0; unit < problem.candidates.size(); ++unit) {
        area += problem.areaGrains[unit];
        weight += problem.weightGrains[unit];
    }
    if (problem.candidates.empty()) return 0;
    const double byArea = static_cast<double>(area) / static_cast<double>(problem.areaCapacity);
    const double byWeight = problem.weightCapacity > 0
        ? static_cast<double>(weight) / static_cast<double>(problem.weightCapacity) : 0.0;
    return static_cast<long long>(ceil(max(byArea, byWeight) - kLpEpsilon));
}

vector<size_t> allUnits(const KnapsackProblem& problem) {
    vector<size_t> units(problem.candidates.size());
    iota(units.begin(), units.end(), size_t{0});
    return units;
}

} // anonymous namespace

BinPacking packFirstFitDecreasing(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                                  const BinPackerSettings& settings, int reservedTrucks) {
    validate(settings, reservedTrucks);
    const KnapsackProblem problem =
        prepareKnapsack(items, capacity, {settings.weightGrainLb, settings.areaGrainSqIn, 1});
    vector<UnitTruck> trucks = firstFitDecreasing(problem, allUnits(problem));
    BinPacking packing;
    packing.ffdTrucks = static_cast<long long>(trucks.size());
    packing.lowerBound = volumeBound(problem);
    packing.proven = packing.ffdTrucks <= packing.lowerBound;
    return finish(items, problem, std::move(trucks), reservedTrucks, std::move(packing));
}

BinPacking packBins(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                    const BinPackerSettings& settings, int reservedTrucks,
                    const vector<size_t>& groupIndices) {
    validate(settings, reservedTrucks);
    if (!groupIndices.empty()) {
        if (groupIndices.size() != items.size()) {
            throw invalid_argument("Bin packer: " + to_string(groupIndices.size()) + " group indices for "
                                   + to_string(items.size()) + " items");
        }
        if (any_of(groupIndices.begin(), groupIndices.end(),
                   [&](size_t group) { return group != groupIndices.front(); })) {
            throw invalid_argument("Bin packer: items of more than one segregated group; their goods may never share a truck");
        }
    }
    const KnapsackProblem problem =
        prepareKnapsack(items, capacity, {settings.weightGrainLb, settings.areaGrainSqIn, 1});

    vector<UnitTruck> warmStart = firstFitDecreasing(problem, allUnits(problem));
    BinPacking packing;
    packing.ffdTrucks = static_cast<long long>(warmStart.size());
    packing.lowerBound = volumeBound(problem);
    vector<UnitTruck> trucks;
    if (problem.candidates.empty()) {
        packing.proven = true;
    } else if (problem.candidates.size() > static_cast<size_t>(settings.maxSearchItems)) {
        packing.fellBack = true;
        packing.proven = packing.ffdTrucks <= packing.lowerBound;
        trucks = std::move(warmStart);
    } else {
        BranchAndPrice search(problem, settings);
        trucks = search.solve(std::move(warmStart), packing.lowerBound, packing.proven, packing.fellBack,
                              packing.branchNodes);
    }
    packing.improvedOnWarmStart = static_cast<long long>(trucks.size()) < packing.ffdTrucks;
    return finish(items, problem, std::move(trucks), reservedTrucks, std::move(packing));
}

} // namespace ob
