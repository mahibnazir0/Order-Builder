#include "knapsack.hpp"
#include "demand_types.hpp"
#include "isoDate.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <functional>
#include <tuple>
#include <utility>

using namespace std;

namespace ob {

namespace {

bool isPositiveFinite(double value) { return isfinite(value) && value > 0.0; }
bool isNonNegativeFinite(double value) { return isfinite(value) && value >= 0.0; }

string shortNumber(double value) {
    ostringstream text;
    text << value;
    return text.str();
}

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
        if (item.unitLoads * 1000.0 > static_cast<double>(kMaxGrains)) {
            throw invalid_argument(name + ": " + shortNumber(item.unitLoads) + " unit loads are too many to count");
        }
        if (!isIsoDate(item.shipDate)) throw invalid_argument(name + ": ship date '" + item.shipDate + "' is not a date");
        if (!item.rankDate.empty() && !isIsoDate(item.rankDate)) {
            throw invalid_argument(name + ": rank date '" + item.rankDate + "' is not a date");
        }
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

// Whole grains, as a double: a figure past kMaxGrains is compared or rejected before any cast,
// since casting a double beyond long long's range is undefined.
double grainsUp(double value, double grain) { return ceil(value / grain - kQuantityEpsilon); }
double grainsDown(double value, double grain) { return floor(value / grain + kQuantityEpsilon); }

long long countedGrains(double grains, const string& what) {
    if (!(grains <= static_cast<double>(kMaxGrains))) {
        throw invalid_argument(what + " counts " + shortNumber(grains) + " grains, past the "
                               + shortNumber(static_cast<double>(kMaxGrains)) + " the solvers count; use a coarser grain");
    }
    return static_cast<long long>(grains);
}

void addCounted(long long& total, long long count, const string& what) {
    total += count;  // both at most kMaxGrains, so the sum cannot overflow
    if (total > kMaxGrains) {
        throw invalid_argument("Knapsack: the items' " + what + " together count past "
                               + shortNumber(static_cast<double>(kMaxGrains)) + " grains; use a coarser grain");
    }
}

const string& rankingDate(const KnapsackItem& item) { return item.rankDate.empty() ? item.shipDate : item.rankDate; }

// An item's urgency class, compared ascending: most urgent first.
tuple<const string&, int, const string&> urgencyClassOf(const KnapsackItem& item) {
    return {rankingDate(item), item.priority, item.shipDate};
}

// Whether factor * count is over limit, without forming a product that could overflow.
bool productOver(long long factor, long long count, long long limit) { return count > 0 && factor > limit / count; }

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

void validateLimits(const vector<ItemLimit>& limits, size_t itemCount) {
    for (size_t limit = 0; limit < limits.size(); ++limit) {
        const ItemLimit& itemLimit = limits[limit];
        const string name = "Knapsack limit " + to_string(limit);
        if (itemLimit.perItem.size() != itemCount) {
            throw invalid_argument(name + ": " + to_string(itemLimit.perItem.size()) + " shares for "
                                   + to_string(itemCount) + " items");
        }
        if (!isNonNegativeFinite(itemLimit.capacity)) {
            throw invalid_argument(name + ": capacity must be finite and not negative");
        }
        for (const double share : itemLimit.perItem) {
            if (isnan(share) || share < 0.0) throw invalid_argument(name + ": a share is negative or not a number");
        }
    }
}

// The caller's limits read per candidate, in solve order.
struct CandidateLimits {
    vector<vector<double>> share;  // [limit][position]
    vector<double> capacity;
};

CandidateLimits candidateLimits(const KnapsackProblem& problem, const vector<ItemLimit>& limits) {
    CandidateLimits byPosition;
    byPosition.share.reserve(limits.size());
    byPosition.capacity.reserve(limits.size());
    for (const ItemLimit& limit : limits) {
        vector<double> share;
        share.reserve(problem.candidates.size());
        for (const size_t index : problem.candidates) share.push_back(limit.perItem[index]);
        byPosition.share.push_back(std::move(share));
        byPosition.capacity.push_back(limit.capacity);
    }
    return byPosition;
}

bool withinLimits(const CandidateLimits& limits, const vector<double>& left, size_t position) {
    for (size_t limit = 0; limit < left.size(); ++limit) {
        if (limits.share[limit][position] > left[limit] + kQuantityEpsilon) return false;
    }
    return true;
}

vector<char> greedyTaken(const KnapsackProblem& problem, const CandidateLimits& limits, const TruckCheck& fits) {
    vector<char> taken(problem.candidates.size(), 0);
    vector<size_t> chosen;
    long long areaLeft = problem.areaCapacity, weightLeft = problem.weightCapacity;
    vector<double> limitLeft = limits.capacity;
    for (size_t position = 0; position < problem.candidates.size(); ++position) {
        if (problem.areaGrains[position] > areaLeft || problem.weightGrains[position] > weightLeft
            || !withinLimits(limits, limitLeft, position)) {
            continue;
        }
        chosen.push_back(problem.candidates[position]);
        if (fits && !fits(chosen)) {
            chosen.pop_back();
            continue;
        }
        taken[position] = 1;
        areaLeft -= problem.areaGrains[position];
        weightLeft -= problem.weightGrains[position];
        for (size_t limit = 0; limit < limitLeft.size(); ++limit) limitLeft[limit] -= limits.share[limit][position];
    }
    return taken;
}

// Upper bounds on what positions from `position` on can still add, per class. Candidates are
// sorted by class, so each class is one block; within the block a suffix, and past it whole
// blocks. A class can add no more than its items' unit loads, nor more than the floor, weight or
// any limit left times its densest item's unit loads per grain or share.
class SuffixBounds {
public:
    SuffixBounds(const KnapsackProblem& problem, const CandidateLimits& limits)
        : problem_(problem), count_(problem.candidates.size()), amount_(count_ + 1, 0),
          perArea_(count_ + 1, 0.0), perWeight_(count_ + 1, 0.0),
          perLimit_(limits.capacity.size(), vector<double>(count_ + 1, 0.0)), area_(count_ + 1, 0),
          blockStart_(problem.classCount, count_) {
        auto densityOf = [](double amount, double share) {
            return share == 0.0 ? numeric_limits<double>::infinity() : amount / share;
        };
        for (size_t position = count_; position-- > 0;) {
            const bool sameClassNext = position + 1 < count_
                                       && problem.classOf[position + 1] == problem.classOf[position];
            const double amount = static_cast<double>(problem.amount[position]);
            amount_[position] = problem.amount[position] + (sameClassNext ? amount_[position + 1] : 0);
            perArea_[position] = max(amount / static_cast<double>(problem.areaGrains[position]),
                                     sameClassNext ? perArea_[position + 1] : 0.0);
            perWeight_[position] = max(densityOf(amount, static_cast<double>(problem.weightGrains[position])),
                                       sameClassNext ? perWeight_[position + 1] : 0.0);
            for (size_t limit = 0; limit < perLimit_.size(); ++limit) {
                perLimit_[limit][position] = max(densityOf(amount, limits.share[limit][position]),
                                                 sameClassNext ? perLimit_[limit][position + 1] : 0.0);
            }
            area_[position] = problem.areaGrains[position] + area_[position + 1];
            blockStart_[problem.classOf[position]] = position;
        }
    }

    // `current` plus the most the positions from `position` on could add.
    KnapsackValue bound(const KnapsackValue& current, size_t position, long long areaLeft, long long weightLeft,
                        const vector<double>& limitLeft) const {
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
            for (size_t other = 0; other < perLimit_.size(); ++other) {
                if (isfinite(perLimit_[other][from])) {
                    most = min(most, (max(limitLeft[other], 0.0) + kQuantityEpsilon) * perLimit_[other][from]);
                }
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
    vector<vector<double>> perLimit_;  // and per share of each limit, [limit][position]
    vector<long long> area_;     // floor grains from here to the end, all classes
    vector<size_t> blockStart_;
};

} // anonymous namespace

KnapsackItem knapsackItemFrom(const SelectionItem& item) {
    return {item.footprintLongIn * item.footprintShortIn, item.weightLb, item.heightIn,
            item.unitLoads, item.shipDate, item.priority, item.productNumber, item.footprintLongIn,
            item.footprintShortIn, static_cast<int>(item.lineIndices.size())};
}

bool moreUrgent(const KnapsackItem& left, const KnapsackItem& right) {
    return tuple_cat(urgencyClassOf(left), tie(left.productNumber)) < tuple_cat(urgencyClassOf(right), tie(right.productNumber));
}

vector<size_t> trucksByUrgency(const vector<KnapsackItem>& items, const vector<vector<size_t>>& trucks) {
    vector<vector<size_t>> mostUrgentFirst = trucks;
    auto itemMoreUrgent = [&](size_t left, size_t right) { return moreUrgent(items[left], items[right]); };
    for (vector<size_t>& truck : mostUrgentFirst) sort(truck.begin(), truck.end(), itemMoreUrgent);
    vector<size_t> order(trucks.size());
    iota(order.begin(), order.end(), size_t{0});
    // Item by item; where one truck's goods run out first and every earlier pair was equally
    // urgent, the truck carrying more goes first.
    stable_sort(order.begin(), order.end(), [&](size_t left, size_t right) {
        const vector<size_t>& leftItems = mostUrgentFirst[left];
        const vector<size_t>& rightItems = mostUrgentFirst[right];
        for (size_t rank = 0; rank < min(leftItems.size(), rightItems.size()); ++rank) {
            if (itemMoreUrgent(leftItems[rank], rightItems[rank])) return true;
            if (itemMoreUrgent(rightItems[rank], leftItems[rank])) return false;
        }
        return leftItems.size() > rightItems.size();
    });
    return order;
}

KnapsackCapacity capacityOf(const TrailerSpec& trailer) {
    return {trailer.interiorLengthIn * trailer.interiorWidthIn, trailer.weightLimitLb,
            trailer.stackHeightCeilingIn};
}

KnapsackProblem prepareKnapsack(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                                const KnapsackSettings& settings) {
    validate(items, capacity, settings);
    KnapsackProblem problem;
    problem.areaCapacity = countedGrains(grainsDown(capacity.floorAreaSqIn, settings.areaGrainSqIn),
                                         "Knapsack: the floor of " + shortNumber(capacity.floorAreaSqIn)
                                             + " sq in at a grain of " + shortNumber(settings.areaGrainSqIn));
    problem.weightCapacity = countedGrains(grainsDown(capacity.weightLb, settings.weightGrainLb),
                                           "Knapsack: the payload of " + shortNumber(capacity.weightLb)
                                               + " lb at a grain of " + shortNumber(settings.weightGrainLb));

    vector<tuple<string, int, string>> classKeys;
    for (size_t index = 0; index < items.size(); ++index) {
        const KnapsackItem& item = items[index];
        // A unit load exactly at the ceiling fits; only taller is excluded.
        if (item.heightIn > capacity.ceilingIn + kQuantityEpsilon) {
            problem.tooTall.push_back(index);
        } else if (grainsUp(item.areaSqIn, settings.areaGrainSqIn) > static_cast<double>(problem.areaCapacity)
                   || grainsUp(item.weightLb, settings.weightGrainLb) > static_cast<double>(problem.weightCapacity)) {
            problem.tooLarge.push_back(index);
        } else {
            problem.candidates.push_back(index);
            classKeys.emplace_back(rankingDate(item), item.priority, item.shipDate);
        }
    }
    sort(classKeys.begin(), classKeys.end());
    classKeys.erase(unique(classKeys.begin(), classKeys.end()), classKeys.end());
    problem.classCount = classKeys.size();

    auto classIndexOf = [&](size_t index) {
        const tuple<string, int, string> key{rankingDate(items[index]), items[index].priority, items[index].shipDate};
        return static_cast<size_t>(lower_bound(classKeys.begin(), classKeys.end(), key) - classKeys.begin());
    };
    stable_sort(problem.candidates.begin(), problem.candidates.end(),
                [&](size_t left, size_t right) { return classIndexOf(left) < classIndexOf(right); });

    const size_t count = problem.candidates.size();
    problem.classOf.reserve(count);
    problem.amount.reserve(count);
    problem.areaGrains.reserve(count);
    problem.weightGrains.reserve(count);
    // Each candidate is within the capacity, so within kMaxGrains, and each cast is defined.
    long long totalAmount = 0, totalArea = 0, totalWeight = 0;
    for (const size_t index : problem.candidates) {
        problem.classOf.push_back(classIndexOf(index));
        problem.amount.push_back(llround(items[index].unitLoads * 1000.0));
        problem.areaGrains.push_back(static_cast<long long>(grainsUp(items[index].areaSqIn, settings.areaGrainSqIn)));
        problem.weightGrains.push_back(static_cast<long long>(grainsUp(items[index].weightLb, settings.weightGrainLb)));
        addCounted(totalAmount, problem.amount.back(), "unit loads");
        addCounted(totalArea, problem.areaGrains.back(), "floor area");
        addCounted(totalWeight, problem.weightGrains.back(), "weight");
    }
    return problem;
}

KnapsackSelection selectGreedy(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                               const KnapsackSettings& settings) {
    const KnapsackProblem problem = prepareKnapsack(items, capacity, settings);
    return assemble(items, capacity, problem, greedyTaken(problem, CandidateLimits(), TruckCheck()));
}

KnapsackSelection selectForTruck(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                                 const KnapsackSettings& settings, const TruckLimits& truck) {
    const KnapsackProblem problem = prepareKnapsack(items, capacity, settings);
    validateLimits(truck.limits, items.size());
    const CandidateLimits limits = candidateLimits(problem, truck.limits);
    const size_t count = problem.candidates.size();
    const SuffixBounds bounds(problem, limits);
    const TruckCheck& fits = truck.fits;

    // The greedy fill is the first leaf the search reaches, so starting from it keeps the same
    // tie-break: only a strictly better selection replaces it.
    vector<char> bestTaken = greedyTaken(problem, limits, fits);
    KnapsackValue best = valueOf(problem, bestTaken);
    // The input indices taken on the current branch, in the order taken: what `fits` judges.
    vector<size_t> chosen;
    chosen.reserve(count);
    auto fitsWith = [&](size_t position) {
        if (!fits) return true;
        chosen.push_back(problem.candidates[position]);
        const bool accepted = fits(chosen);
        chosen.pop_back();
        return accepted;
    };

    // Iterative depth-first search, so a 10,000-item lane cannot overflow the stack. Each depth
    // tries taking its item and leaving it out: taking first, unless the check refused the load
    // with it, when the load without it is searched first and the one with it after, unverified.
    vector<char> branchesTried(count, 0);  // 0, 1 or 2
    vector<char> takeFirst(count, 0);
    vector<char> canTake(count, 0);
    vector<char> taken(count, 0);
    // The depth whose item went on although the check refused the load. Below it the load is
    // unverified, and a leaf counts only once `fits` accepts it. `count` while verified.
    size_t unverifiedFrom = count;
    KnapsackValue current(problem.classCount + 1, 0);
    long long areaLeft = problem.areaCapacity, weightLeft = problem.weightCapacity;
    vector<double> limitLeft = limits.capacity;
    long long nodes = 0;
    bool proven = true;
    size_t depth = 0;
    bool descending = true;
    auto take = [&](size_t position, bool include) {
        const long long sign = include ? 1 : -1;
        taken[position] = include ? 1 : 0;
        if (include) {
            chosen.push_back(problem.candidates[position]);
        } else {
            chosen.pop_back();
        }
        current[problem.classOf[position]] += sign * problem.amount[position];
        current.back() += sign * problem.areaGrains[position];
        areaLeft -= sign * problem.areaGrains[position];
        weightLeft -= sign * problem.weightGrains[position];
        for (size_t limit = 0; limit < limitLeft.size(); ++limit) {
            limitLeft[limit] -= static_cast<double>(sign) * limits.share[limit][position];
        }
    };
    auto sameItem = [&](size_t left, size_t right) {
        const KnapsackItem& leftItem = items[problem.candidates[left]];
        const KnapsackItem& rightItem = items[problem.candidates[right]];
        if (problem.classOf[left] != problem.classOf[right] || problem.amount[left] != problem.amount[right]
            || problem.areaGrains[left] != problem.areaGrains[right]
            || problem.weightGrains[left] != problem.weightGrains[right]
            || leftItem.longSideIn != rightItem.longSideIn || leftItem.shortSideIn != rightItem.shortSideIn
            || leftItem.levels != rightItem.levels) {
            return false;
        }
        for (const vector<double>& share : limits.share) {
            if (share[left] != share[right]) return false;
        }
        return true;
    };
    while (true) {
        if (descending) {
            if (depth == count) {
                if (best < current && (unverifiedFrom == count || !fits || fits(chosen))) {
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
            if (!(best < bounds.bound(current, depth, areaLeft, weightLeft, limitLeft))) {
                descending = false;
                continue;
            }
            // Identical items are interchangeable, so only a leading run of them is ever taken:
            // once one is left out, the copies after it are too. This stops the search re-trying
            // the same load in every order.
            const bool copyOfExcluded = depth > 0 && !taken[depth - 1] && sameItem(depth - 1, depth);
            canTake[depth] = !copyOfExcluded && problem.areaGrains[depth] <= areaLeft
                             && problem.weightGrains[depth] <= weightLeft && withinLimits(limits, limitLeft, depth);
            // An unverified load is only judged whole, at its leaf.
            takeFirst[depth] = canTake[depth] && (unverifiedFrom < count || fitsWith(depth));
            branchesTried[depth] = 1;
            if (takeFirst[depth]) take(depth, true);
            ++depth;
            continue;
        }
        if (depth == 0) break;
        --depth;
        if (taken[depth]) {
            take(depth, false);
            if (unverifiedFrom == depth) unverifiedFrom = count;
        }
        if (branchesTried[depth] == 1 && canTake[depth]) {
            branchesTried[depth] = 2;
            if (!takeFirst[depth]) {
                take(depth, true);
                if (unverifiedFrom == count) unverifiedFrom = depth;
            }
            ++depth;
            descending = true;
        } else {
            branchesTried[depth] = 0;
        }
    }

    KnapsackSelection selection = assemble(items, capacity, problem, bestTaken);
    selection.proven = proven;
    selection.nodes = nodes;
    return selection;
}

KnapsackSelection selectByTable(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                                const KnapsackSettings& settings, long long maxTableCells) {
    if (maxTableCells < 1) throw invalid_argument("Knapsack table: maxTableCells must be at least 1");
    const KnapsackProblem problem = prepareKnapsack(items, capacity, settings);
    const size_t count = problem.candidates.size();
    const long long areaCells = problem.areaCapacity + 1, weightCells = problem.weightCapacity + 1;
    const long long width = static_cast<long long>(problem.classCount) + 1;
    // The value table holds `width` entries a cell and the taken flags one bit a cell per item,
    // so each is bounded on its own: 8 values a cell and 64 flags a cell on average.
    const long long most = numeric_limits<long long>::max();
    const long long valueLimit = maxTableCells > most / 8 ? most : maxTableCells * 8;
    const long long flagLimit = maxTableCells > most / 64 ? most : maxTableCells * 64;
    if (productOver(areaCells, weightCells, maxTableCells)
        || productOver(areaCells * weightCells, width, valueLimit)
        || productOver(areaCells * weightCells, static_cast<long long>(count), flagLimit)) {
        throw invalid_argument("Knapsack table of " + shortNumber(static_cast<double>(areaCells) * static_cast<double>(weightCells))
                               + " cells for " + to_string(count) + " items is too large; use selectForTruck");
    }
    const long long cells = areaCells * weightCells;

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

KnapsackSelection selectNextTruck(const vector<KnapsackItem>& items, const vector<size_t>& remaining,
                                  const KnapsackCapacity& capacity, const KnapsackSettings& settings,
                                  const TruckLimits& truck) {
    validateLimits(truck.limits, items.size());
    // The truck is searched over what is left, so its choices are mapped back to the caller's items.
    vector<KnapsackItem> pool;
    pool.reserve(remaining.size());
    for (const size_t index : remaining) {
        if (index >= items.size()) {
            throw invalid_argument("Knapsack: remaining item " + to_string(index) + " is not one of "
                                   + to_string(items.size()));
        }
        pool.push_back(items[index]);
    }
    TruckLimits pooled;
    vector<size_t> chosenItems;
    if (truck.fits) {
        pooled.fits = [&](const vector<size_t>& chosenInPool) {
            chosenItems.clear();
            for (const size_t index : chosenInPool) chosenItems.push_back(remaining[index]);
            return truck.fits(chosenItems);
        };
    }
    pooled.limits.reserve(truck.limits.size());
    for (const ItemLimit& limit : truck.limits) {
        ItemLimit& shares = pooled.limits.emplace_back();
        shares.capacity = limit.capacity;
        shares.perItem.reserve(remaining.size());
        for (const size_t index : remaining) shares.perItem.push_back(limit.perItem[index]);
    }
    KnapsackSelection selection = selectForTruck(pool, capacity, settings, pooled);
    for (size_t& index : selection.chosen) index = remaining[index];
    for (size_t& index : selection.leftOver) index = remaining[index];
    return selection;
}

LaneSelection selectForTrucks(const vector<KnapsackItem>& items, const KnapsackCapacity& capacity,
                              const KnapsackSettings& settings, int trucks, const TruckLimits& truck) {
    if (trucks < 0) throw invalid_argument("Knapsack: truck count " + to_string(trucks) + " is negative");
    validate(items, capacity, settings);
    validateLimits(truck.limits, items.size());
    LaneSelection lane;
    vector<size_t> remaining(items.size());
    iota(remaining.begin(), remaining.end(), size_t{0});
    lane.trucks.reserve(static_cast<size_t>(trucks));
    for (int built = 0; built < trucks; ++built) {
        KnapsackSelection selection = selectNextTruck(items, remaining, capacity, settings, truck);
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
