#include "knapsack.hpp"
#include "demand_types.hpp"
#include "isoDate.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

using namespace std;

namespace ob {

namespace {

bool isPositiveFinite(double value) { return isfinite(value) && value > 0.0; }
bool isNonNegativeFinite(double value) { return isfinite(value) && value >= 0.0; }

void validate(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
              const KnapsackSettings& settings) {
    if (!isNonNegativeFinite(capacity.floorAreaSqIn) || !isNonNegativeFinite(capacity.weightLb)
        || !isPositiveFinite(capacity.ceilingIn)) {
        throw invalid_argument("Knapsack capacity: floor area and weight must be finite and not "
                               "negative, and the ceiling positive and finite");
    }
    if (!isPositiveFinite(settings.weightGrainLb) || !isPositiveFinite(settings.areaGrainSqIn)) {
        throw invalid_argument("Knapsack settings: weight and area grains must be positive and finite");
    }
    if (settings.nodeLimit < 1) {
        throw invalid_argument("Knapsack settings: node limit must be at least 1");
    }
    for (size_t index = 0; index < items.size(); ++index) {
        const KnapsackItem& item = items[index];
        const string name = "Knapsack item " + to_string(index);
        if (!isPositiveFinite(item.areaSqIn)) throw invalid_argument(name + ": area must be positive and finite");
        if (!isNonNegativeFinite(item.weightLb)) throw invalid_argument(name + ": weight must be finite and not negative");
        if (!isNonNegativeFinite(item.heightIn)) throw invalid_argument(name + ": height must be finite and not negative");
        if (!isPositiveFinite(item.unitLoads)) throw invalid_argument(name + ": unit loads must be positive and finite");
        if (!isIsoDate(item.shipDate)) throw invalid_argument(name + ": ship date '" + item.shipDate + "' is not a date");
        if (item.priority < 0 || item.priority > kMaxPriority) {
            throw invalid_argument(name + ": priority " + to_string(item.priority) + " is outside 0.."
                                   + to_string(kMaxPriority));
        }
        // A grain coarser than an item would round it into grains it does not fill, so the grain,
        // not the load, would decide what fits (E4).
        if (item.weightLb > 0.0 && settings.weightGrainLb > item.weightLb + kQuantityEpsilon) {
            throw invalid_argument(name + " weighs less than the weight grain");
        }
        if (settings.areaGrainSqIn > item.areaSqIn + kQuantityEpsilon) {
            throw invalid_argument(name + " covers less floor than the area grain");
        }
    }
}

long long grainsUp(double value, double grain) {
    return static_cast<long long>(ceil(value / grain - kQuantityEpsilon));
}

long long grainsDown(double value, double grain) {
    return static_cast<long long>(floor(value / grain + kQuantityEpsilon));
}

KnapsackValue valueOf(const KnapsackProblem& problem, const vector<char>& taken) {
    KnapsackValue value(problem.classCount + 1, 0);
    for (size_t position = 0; position < problem.candidates.size(); ++position) {
        if (!taken[position]) continue;
        value[problem.classOf[position]] += problem.amount[position];
        value.back() += problem.areaGrains[position];
    }
    return value;
}

KnapsackSelection assemble(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                           const KnapsackProblem& problem, const vector<char>& taken) {
    KnapsackSelection selection;
    vector<char> chosen(items.size(), 0);
    long long areaLeft = problem.areaCapacity, weightLeft = problem.weightCapacity;
    for (size_t position = 0; position < problem.candidates.size(); ++position) {
        if (!taken[position]) continue;
        const size_t index = problem.candidates[position];
        chosen[index] = 1;
        selection.areaUsedSqIn += items[index].areaSqIn;
        selection.weightUsedLb += items[index].weightLb;
        areaLeft -= problem.areaGrains[position];
        weightLeft -= problem.weightGrains[position];
    }
    for (size_t index = 0; index < items.size(); ++index) {
        (chosen[index] ? selection.chosen : selection.leftOver).push_back(index);
    }
    selection.value = valueOf(problem, taken);
    selection.everythingFit = selection.leftOver.empty();
    selection.weightFull = capacity.weightLb > 0.0
                           && selection.weightUsedLb >= capacity.weightLb - kQuantityEpsilon;

    // The smallest candidate left, by floor then weight, shows which limit stopped the truck.
    optional<size_t> smallestLeft;
    for (size_t position = 0; position < problem.candidates.size(); ++position) {
        if (taken[position]) continue;
        if (!smallestLeft
            || make_pair(problem.areaGrains[position], problem.weightGrains[position])
                   < make_pair(problem.areaGrains[*smallestLeft], problem.weightGrains[*smallestLeft])) {
            smallestLeft = position;
        }
    }
    if (smallestLeft) {
        const bool areaBlocks = problem.areaGrains[*smallestLeft] > areaLeft;
        const bool weightBlocks = problem.weightGrains[*smallestLeft] > weightLeft;
        selection.binding = areaBlocks && weightBlocks ? BindingLimit::Both
                            : areaBlocks               ? BindingLimit::FloorArea
                            : weightBlocks             ? BindingLimit::Weight
                                                       : BindingLimit::None;
    }
    return selection;
}

vector<char> greedyTaken(const KnapsackProblem& problem) {
    vector<char> taken(problem.candidates.size(), 0);
    long long areaLeft = problem.areaCapacity, weightLeft = problem.weightCapacity;
    for (size_t position = 0; position < problem.candidates.size(); ++position) {
        if (problem.areaGrains[position] <= areaLeft && problem.weightGrains[position] <= weightLeft) {
            taken[position] = 1;
            areaLeft -= problem.areaGrains[position];
            weightLeft -= problem.weightGrains[position];
        }
    }
    return taken;
}

// Upper bounds on what positions from `position` on can still add, per class. Candidates are
// sorted by class, so each class is one block; within the block a suffix, and past it whole
// blocks. A class can add no more than its items' unit loads, nor more than the floor (or
// weight) left times its densest item's unit loads per grain.
class SuffixBounds {
public:
    explicit SuffixBounds(const KnapsackProblem& problem)
        : problem_(problem), count_(problem.candidates.size()), amount_(count_ + 1, 0),
          perArea_(count_ + 1, 0.0), perWeight_(count_ + 1, 0.0), area_(count_ + 1, 0),
          blockStart_(problem.classCount, count_) {
        for (size_t position = count_; position-- > 0;) {
            const bool sameClassNext = position + 1 < count_
                                       && problem.classOf[position + 1] == problem.classOf[position];
            const double amount = static_cast<double>(problem.amount[position]);
            const double perWeight = problem.weightGrains[position] == 0
                ? numeric_limits<double>::infinity()
                : amount / static_cast<double>(problem.weightGrains[position]);
            amount_[position] = problem.amount[position] + (sameClassNext ? amount_[position + 1] : 0);
            perArea_[position] = max(amount / static_cast<double>(problem.areaGrains[position]),
                                     sameClassNext ? perArea_[position + 1] : 0.0);
            perWeight_[position] = max(perWeight, sameClassNext ? perWeight_[position + 1] : 0.0);
            area_[position] = problem.areaGrains[position] + area_[position + 1];
            blockStart_[problem.classOf[position]] = position;
        }
    }

    // `current` plus the most the positions from `position` on could add.
    KnapsackValue bound(const KnapsackValue& current, size_t position, long long areaLeft,
                        long long weightLeft) const {
        KnapsackValue limit = current;
        if (position >= count_) return limit;
        for (size_t classIndex = problem_.classOf[position]; classIndex < problem_.classCount; ++classIndex) {
            const size_t from = classIndex == problem_.classOf[position] ? position : blockStart_[classIndex];
            if (from >= count_) continue;
            double most = static_cast<double>(amount_[from]);
            most = min(most, static_cast<double>(areaLeft) * perArea_[from]);
            if (isfinite(perWeight_[from])) {
                most = min(most, static_cast<double>(weightLeft) * perWeight_[from]);
            }
            limit[classIndex] += static_cast<long long>(floor(most + 1e-6));
        }
        limit.back() += min(areaLeft, area_[position]);
        return limit;
    }

private:
    const KnapsackProblem& problem_;
    size_t count_;
    vector<long long> amount_;   // the class's unit loads from here to the end of its block
    vector<double> perArea_;     // its densest unit loads per floor grain over the same span
    vector<double> perWeight_;   // and per weight grain; infinite when an item weighs nothing
    vector<long long> area_;     // floor grains from here to the end, all classes
    vector<size_t> blockStart_;
};

} // anonymous namespace

KnapsackItem knapsackItemFrom(const SelectionItem& item) {
    return {item.footprintLongIn * item.footprintShortIn, item.weightLb, item.heightIn,
            item.unitLoads, item.shipDate, item.priority};
}

KnapsackCapacity capacityOf(const TrailerSpec& trailer) {
    return {trailer.interiorLengthIn * trailer.interiorWidthIn, trailer.weightLimitLb,
            trailer.stackHeightCeilingIn};
}

KnapsackProblem prepareKnapsack(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                                const KnapsackSettings& settings) {
    validate(items, capacity, settings);
    KnapsackProblem problem;
    problem.areaCapacity = grainsDown(capacity.floorAreaSqIn, settings.areaGrainSqIn);
    problem.weightCapacity = grainsDown(capacity.weightLb, settings.weightGrainLb);

    vector<pair<string, int>> classKeys;
    for (size_t index = 0; index < items.size(); ++index) {
        const KnapsackItem& item = items[index];
        // A unit load exactly at the ceiling fits; only taller is excluded.
        if (item.heightIn > capacity.ceilingIn + kQuantityEpsilon) {
            problem.tooTall.push_back(index);
        } else if (grainsUp(item.areaSqIn, settings.areaGrainSqIn) > problem.areaCapacity
                   || grainsUp(item.weightLb, settings.weightGrainLb) > problem.weightCapacity) {
            problem.tooLarge.push_back(index);
        } else {
            problem.candidates.push_back(index);
            classKeys.emplace_back(item.shipDate, item.priority);
        }
    }
    sort(classKeys.begin(), classKeys.end());
    classKeys.erase(unique(classKeys.begin(), classKeys.end()), classKeys.end());
    problem.classCount = classKeys.size();

    auto classIndexOf = [&](size_t index) {
        const pair<string, int> key{items[index].shipDate, items[index].priority};
        return static_cast<size_t>(lower_bound(classKeys.begin(), classKeys.end(), key) - classKeys.begin());
    };
    stable_sort(problem.candidates.begin(), problem.candidates.end(),
                [&](size_t left, size_t right) { return classIndexOf(left) < classIndexOf(right); });

    const size_t count = problem.candidates.size();
    problem.classOf.reserve(count);
    problem.amount.reserve(count);
    problem.areaGrains.reserve(count);
    problem.weightGrains.reserve(count);
    for (const size_t index : problem.candidates) {
        problem.classOf.push_back(classIndexOf(index));
        problem.amount.push_back(llround(items[index].unitLoads * 1000.0));
        problem.areaGrains.push_back(grainsUp(items[index].areaSqIn, settings.areaGrainSqIn));
        problem.weightGrains.push_back(grainsUp(items[index].weightLb, settings.weightGrainLb));
    }
    return problem;
}

KnapsackSelection selectGreedy(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                               const KnapsackSettings& settings) {
    const KnapsackProblem problem = prepareKnapsack(items, capacity, settings);
    return assemble(items, capacity, problem, greedyTaken(problem));
}

KnapsackSelection selectForTruck(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                                 const KnapsackSettings& settings) {
    const KnapsackProblem problem = prepareKnapsack(items, capacity, settings);
    const size_t count = problem.candidates.size();
    const SuffixBounds bounds(problem);

    // The greedy fill is the first leaf the include-first search reaches, so starting from it
    // keeps the same tie-break: only a strictly better selection replaces it.
    vector<char> bestTaken = greedyTaken(problem);
    KnapsackValue best = valueOf(problem, bestTaken);

    // Iterative depth-first search, so a 10,000-item lane cannot overflow the stack.
    // decision: 0 untried, 1 included, 2 excluded.
    vector<char> decision(count, 0);
    vector<char> taken(count, 0);
    KnapsackValue current(problem.classCount + 1, 0);
    long long areaLeft = problem.areaCapacity, weightLeft = problem.weightCapacity;
    long long nodes = 0;
    bool proven = true;
    size_t depth = 0;
    bool descending = true;
    auto take = [&](size_t position, bool include) {
        const long long sign = include ? 1 : -1;
        taken[position] = include ? 1 : 0;
        current[problem.classOf[position]] += sign * problem.amount[position];
        current.back() += sign * problem.areaGrains[position];
        areaLeft -= sign * problem.areaGrains[position];
        weightLeft -= sign * problem.weightGrains[position];
    };
    auto sameItem = [&](size_t left, size_t right) {
        return problem.classOf[left] == problem.classOf[right] && problem.amount[left] == problem.amount[right]
               && problem.areaGrains[left] == problem.areaGrains[right]
               && problem.weightGrains[left] == problem.weightGrains[right];
    };
    while (true) {
        if (descending) {
            if (depth == count) {
                if (best < current) {
                    best = current;
                    bestTaken = taken;
                }
                descending = false;
                continue;
            }
            if (++nodes > settings.nodeLimit) {
                proven = false;
                break;
            }
            if (!(best < bounds.bound(current, depth, areaLeft, weightLeft))) {
                descending = false;
                continue;
            }
            // Identical items are interchangeable, so only a leading run of them is ever taken:
            // once one is left out, the copies after it are too. This keeps the earliest-first
            // tie-break and stops the search re-trying the same load in every order.
            const bool copyOfExcluded = depth > 0 && decision[depth - 1] == 2 && sameItem(depth - 1, depth);
            if (!copyOfExcluded && problem.areaGrains[depth] <= areaLeft
                && problem.weightGrains[depth] <= weightLeft) {
                take(depth, true);
                decision[depth] = 1;
            } else {
                decision[depth] = 2;
            }
            ++depth;
            continue;
        }
        if (depth == 0) break;
        --depth;
        if (decision[depth] == 1) {
            take(depth, false);
            decision[depth] = 2;
            ++depth;
            descending = true;
        } else {
            decision[depth] = 0;
        }
    }

    KnapsackSelection selection = assemble(items, capacity, problem, bestTaken);
    selection.proven = proven;
    selection.nodes = nodes;
    return selection;
}

KnapsackSelection selectByTable(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                                const KnapsackSettings& settings) {
    const KnapsackProblem problem = prepareKnapsack(items, capacity, settings);
    const size_t count = problem.candidates.size();
    const long long areaCells = problem.areaCapacity + 1, weightCells = problem.weightCapacity + 1;
    const long long cells = areaCells * weightCells;
    const long long width = static_cast<long long>(problem.classCount) + 1;
    if (cells > kMaxKnapsackTableCells || cells * width > kMaxKnapsackTableCells * 8
        || cells * static_cast<long long>(count) > kMaxKnapsackTableCells * 64) {
        throw invalid_argument("Knapsack table of " + to_string(cells) + " cells for "
                               + to_string(count) + " items is too large; use selectForTruck");
    }

    // table[cell] is the best value within that much floor and weight, over the items so far.
    vector<long long> table(static_cast<size_t>(cells * width), 0);
    vector<vector<bool>> takenAt(count, vector<bool>(static_cast<size_t>(cells), false));
    KnapsackValue candidate(static_cast<size_t>(width));
    for (size_t position = 0; position < count; ++position) {
        const long long itemArea = problem.areaGrains[position];
        const long long itemWeight = problem.weightGrains[position];
        for (long long area = problem.areaCapacity; area >= itemArea; --area) {
            for (long long weight = problem.weightCapacity; weight >= itemWeight; --weight) {
                const long long cell = area * weightCells + weight;
                const long long from = (area - itemArea) * weightCells + (weight - itemWeight);
                copy_n(table.begin() + from * width, width, candidate.begin());
                candidate[problem.classOf[position]] += problem.amount[position];
                candidate.back() += itemArea;
                const auto stored = table.begin() + cell * width;
                if (lexicographical_compare(stored, stored + width, candidate.begin(), candidate.end())) {
                    copy(candidate.begin(), candidate.end(), stored);
                    takenAt[position][static_cast<size_t>(cell)] = true;
                }
            }
        }
    }

    vector<char> taken(count, 0);
    long long area = problem.areaCapacity, weight = problem.weightCapacity;
    for (size_t position = count; position-- > 0;) {
        if (takenAt[position][static_cast<size_t>(area * weightCells + weight)]) {
            taken[position] = 1;
            area -= problem.areaGrains[position];
            weight -= problem.weightGrains[position];
        }
    }
    return assemble(items, capacity, problem, taken);
}

LaneSelection selectForTrucks(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                              const KnapsackSettings& settings, int trucks) {
    if (trucks < 0) throw invalid_argument("Knapsack: truck count " + to_string(trucks) + " is negative");
    validate(items, capacity, settings);
    LaneSelection lane;
    vector<size_t> remaining(items.size());
    iota(remaining.begin(), remaining.end(), size_t{0});
    lane.trucks.reserve(static_cast<size_t>(trucks));
    for (int truck = 0; truck < trucks; ++truck) {
        vector<KnapsackItem> pool;
        pool.reserve(remaining.size());
        for (const size_t index : remaining) pool.push_back(items[index]);
        KnapsackSelection selection = selectForTruck(pool, capacity, settings);
        for (size_t& index : selection.chosen) index = remaining[index];
        for (size_t& index : selection.leftOver) index = remaining[index];
        remaining = selection.leftOver;
        lane.proven = lane.proven && selection.proven;
        lane.trucks.push_back(std::move(selection));
    }
    lane.leftOver = std::move(remaining);
    return lane;
}

const char* bindingLimitName(BindingLimit limit) {
    switch (limit) {
    case BindingLimit::None: return "none";
    case BindingLimit::FloorArea: return "floor area";
    case BindingLimit::Weight: return "weight";
    case BindingLimit::Both: return "floor area and weight";
    }
    return "unknown";
}

} // namespace ob
