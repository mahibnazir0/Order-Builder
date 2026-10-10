#include "stackBuilder.hpp"

#include "stackRules.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

using namespace std;

namespace ob {
namespace {

struct Item {
    size_t lineIndex = 0;
    UnitLoad load;
    double quantity = 0.0;
};

using Order = vector<size_t>;

struct Context {
    const vector<Item>& items;
    const M2Params& params;
    double ceilingIn;
};

Order naturalOrder(size_t count) {
    Order order(count);
    iota(order.begin(), order.end(), size_t{0});
    return order;
}

template <typename Less>
Order sortedOrder(size_t count, Less less) {
    Order order = naturalOrder(count);
    stable_sort(order.begin(), order.end(), less);
    return order;
}

// Whole-pallet stacking commits whole stacks, so a line must have a pallet left for every
// level it would occupy; otherwise any remainder can be spread across the chain.
bool hasPalletForAnotherLevel(const Context& context, const Order& chain, size_t top,
                              const vector<double>& remaining) {
    if (!context.params.stackWholePallets) return remaining[top] > kQuantityEpsilon;
    const auto levelsHeld = count(chain.begin(), chain.end(), top);
    return remaining[top] >= static_cast<double>(levelsHeld + 1);
}

// `top` may join the chain only if the customer's stack-height cap allows it and every
// load already in it can still carry what would sit above it. Each level is checked with
// canStack against a copy of that load that already counts the height and weight of
// everything stacked over it.
bool canExtendChain(const Context& context, const Order& chain, const UnitLoad& top) {
    if (chain.size() >= static_cast<size_t>(context.params.maxStackHeight)) return false;
    double heightAboveIn = 0.0;
    double weightAboveLb = 0.0;
    for (size_t level = chain.size(); level-- > 0;) {
        const UnitLoad& own = context.items[chain[level]].load;
        UnitLoad carrier = own;
        carrier.heightIn += heightAboveIn;
        carrier.ownWeightAboveLb += weightAboveLb;
        if (!canStack(carrier, top, context.params, context.ceilingIn).isFeasible) return false;
        heightAboveIn += own.heightIn;
        weightAboveLb += own.weightLb;
    }
    return true;
}

// Greedy: take each base in baseOrder, keep adding the first feasible top in topOrder,
// then commit as many identical stacks as the scarcest line allows. Repeats until every
// line is used up. Each commit exhausts at least one line, so it always terminates.
StackSet buildWithOrders(const Context& context, StackMethod method,
                         const Order& baseOrder, const Order& topOrder) {
    const auto& items = context.items;
    vector<double> remaining(items.size());
    for (size_t i = 0; i < items.size(); ++i) remaining[i] = items[i].quantity;

    StackSet set;
    set.method = method;
    for (const size_t base : baseOrder) {
        while (remaining[base] > kQuantityEpsilon) {
            Order chain{base};
            for (bool extended = true; extended;) {
                extended = false;
                for (const size_t top : topOrder) {
                    if (hasPalletForAnotherLevel(context, chain, top, remaining)
                        && canExtendChain(context, chain, items[top].load)) {
                        chain.push_back(top);
                        extended = true;
                        break;
                    }
                }
            }

            vector<size_t> timesInChain(items.size(), 0);
            for (const size_t member : chain) ++timesInChain[member];
            double quantity = remaining[base] / static_cast<double>(timesInChain[base]);
            size_t limiting = base;
            for (const size_t member : chain) {
                const double fits = remaining[member] / static_cast<double>(timesInChain[member]);
                if (fits < quantity) { quantity = fits; limiting = member; }
            }
            if (context.params.stackWholePallets) quantity = floor(quantity);
            double stackWeightLb = 0.0;
            for (const size_t member : chain) {
                remaining[member] -= quantity;
                stackWeightLb += items[member].load.weightLb;
            }
            // Zeroing the limiting line stops fractional residue from looping forever. Whole
            // quantities subtract exactly, and flooring can leave the limiting line a pallet.
            for (const size_t member : chain) {
                const bool exhausted = remaining[member] <= kQuantityEpsilon
                    || (member == limiting && !context.params.stackWholePallets);
                if (exhausted) remaining[member] = 0.0;
            }
            set.floorPositions += quantity;
            set.heaviestStackLb = max(set.heaviestStackLb, stackWeightLb);
            set.stacks.push_back(BuiltStack{std::move(chain), quantity});
        }
    }
    return set;
}

StackSet runMethod(const Context& context, StackMethod method) {
    const auto& items = context.items;
    const size_t count = items.size();
    const auto weightOf = [&](size_t i) { return items[i].load.weightLb; };
    const auto heightOf = [&](size_t i) { return items[i].load.heightIn; };
    const auto criOf = [&](size_t i) { return items[i].load.cri; };

    switch (method) {
    case StackMethod::Natural:
        return buildWithOrders(context, method, naturalOrder(count), naturalOrder(count));
    case StackMethod::Target:
        // Fill toward the ceiling: the tallest load that still fits goes on top.
        return buildWithOrders(context, method, naturalOrder(count),
                               sortedOrder(count, [&](size_t a, size_t b) {
                                   return heightOf(a) > heightOf(b); }));
    case StackMethod::TallAndHeavy:
        // Heavy, tall loads carry; the shortest loads ride on top.
        return buildWithOrders(context, method,
                               sortedOrder(count, [&](size_t a, size_t b) {
                                   return weightOf(a) != weightOf(b) ? weightOf(a) > weightOf(b)
                                                                     : heightOf(a) > heightOf(b); }),
                               sortedOrder(count, [&](size_t a, size_t b) {
                                   return heightOf(a) < heightOf(b); }));
    case StackMethod::BaseAndTop:
        // Strongest crush rating carries; the lightest loads ride on top.
        return buildWithOrders(context, method,
                               sortedOrder(count, [&](size_t a, size_t b) {
                                   return criOf(a) > criOf(b); }),
                               sortedOrder(count, [&](size_t a, size_t b) {
                                   return weightOf(a) < weightOf(b); }));
    case StackMethod::TryHard:
        break;
    }
    return StackSet{};
}

bool isBetter(const StackSet& candidate, const StackSet& incumbent, bool weightBound) {
    if (abs(candidate.floorPositions - incumbent.floorPositions) > kQuantityEpsilon) {
        return candidate.floorPositions < incumbent.floorPositions;
    }
    return weightBound && candidate.heaviestStackLb < incumbent.heaviestStackLb;
}

// Try Hard re-runs Base & Top ordering from several rotated starting bases, up to the
// configured attempt cap, and keeps the best. Deterministic, so results are repeatable.
StackSet runTryHard(const Context& context, size_t attempts, bool weightBound) {
    const auto& items = context.items;
    const size_t count = items.size();
    const Order tops = sortedOrder(count, [&](size_t a, size_t b) {
        return items[a].load.weightLb < items[b].load.weightLb; });
    StackSet best;
    for (size_t attempt = 0; attempt < attempts; ++attempt) {
        Order bases = naturalOrder(count);
        rotate(bases.begin(), bases.begin() + static_cast<ptrdiff_t>(
                        (attempt + 1) * count / (attempts + 1)), bases.end());
        StackSet candidate = buildWithOrders(context, StackMethod::TryHard, bases, tops);
        if (attempt == 0 || isBetter(candidate, best, weightBound)) best = std::move(candidate);
    }
    return best;
}

void requireAlignedInputs(const SegregationResult& segregation, const vector<JoinedLine>& lines,
                          const vector<double>& palletsPerLine, const BindingResult& binding,
                          const TrailerSpec& trailer) {
    if (!isfinite(trailer.stackHeightCeilingIn) || trailer.stackHeightCeilingIn <= 0.0) {
        throw invalid_argument("stackBuilder: trailer stackHeightCeilingIn must be positive");
    }
    if (palletsPerLine.size() != lines.size()) {
        throw invalid_argument("stackBuilder: pallet vector does not match the joined lines");
    }
    if (binding.groups.size() != segregation.groups.size()) {
        throw invalid_argument("stackBuilder: binding results do not match the groups");
    }
}

} // namespace

double stackedPalletsForLine(double palletEquivalents, const M2Params& params) {
    return params.stackWholePallets ? ceil(palletEquivalents - kQuantityEpsilon) : palletEquivalents;
}

vector<bool> stackedLineFlags(const StackingResult& stacking, size_t lineCount) {
    vector<bool> stacked(lineCount, false);
    for (const auto& group : stacking.groups) {
        for (const auto& stack : group.best.stacks) {
            if (stack.quantity <= 0.0) continue;
            for (const size_t lineIndex : stack.lineIndices) {
                if (lineIndex < lineCount) stacked[lineIndex] = true;
            }
        }
    }
    return stacked;
}

StackingResult buildStacks(const SegregationResult& segregation,
                           const vector<JoinedLine>& lines,
                           const vector<double>& palletsPerLine,
                           const BindingResult& binding,
                           const M2Params& params,
                           const TrailerSpec& trailer) {
    requireAlignedInputs(segregation, lines, palletsPerLine, binding, trailer);

    StackingResult result;
    result.groups.reserve(segregation.groups.size());
    for (size_t groupIndex = 0; groupIndex < segregation.groups.size(); ++groupIndex) {
        vector<Item> items;
        items.reserve(segregation.groups[groupIndex].lineIndices.size());
        for (const size_t lineIndex : segregation.groups[groupIndex].lineIndices) {
            if (lineIndex >= lines.size()) {
                throw invalid_argument("stackBuilder: group line index out of range");
            }
            const double quantity = palletsPerLine[lineIndex];
            if (!isfinite(quantity) || quantity < 0.0) {
                result.invalidQuantityLines.push_back(lineIndex);
                continue;
            }
            if (quantity == 0.0) {
                result.zeroQuantityLines.push_back(lineIndex);
                continue;
            }
            const double stackedQuantity = stackedPalletsForLine(quantity, params);
            UnitLoad load = buildUnitLoad(lines[lineIndex], params);
            if (load.error != UnitLoadError::None) {
                result.excludedLines.push_back({lineIndex, load.error});
                continue;
            }
            // The floor's test (exceedsCeiling): a load on the ceiling, or a ULP above it
            // from inexact case heights, fits. Milestones 2 and 3 never disagree on a line.
            if (load.heightIn > trailer.stackHeightCeilingIn + kQuantityEpsilon) {
                result.overHeightLines.push_back(lineIndex);
                continue;
            }
            if (exceedsOwnCri(load, params)) result.ownCriExceededLines.push_back(lineIndex);
            items.push_back(Item{lineIndex, std::move(load), stackedQuantity});
        }

        const Context context{items, params, trailer.stackHeightCeilingIn};
        const bool weightBound = binding.groups[groupIndex].binding == BindingConstraint::Weight;
        vector<StackSet> candidates;
        for (const StackMethod method : {StackMethod::Natural, StackMethod::Target,
                                         StackMethod::TallAndHeavy, StackMethod::BaseAndTop}) {
            candidates.push_back(runMethod(context, method));
        }
        if (params.pass2AttemptCap > 0) {
            candidates.push_back(runTryHard(context, static_cast<size_t>(params.pass2AttemptCap),
                                            weightBound));
        }

        GroupStacking group;
        size_t bestIndex = 0;
        for (size_t i = 0; i < candidates.size(); ++i) {
            group.outcomes.push_back({candidates[i].method, candidates[i].floorPositions});
            if (isBetter(candidates[i], candidates[bestIndex], weightBound)) bestIndex = i;
        }
        group.best = std::move(candidates[bestIndex]);

        // Stacks hold line indices into `items`; translate them back to joined-line indices.
        for (auto& stack : group.best.stacks) {
            for (auto& member : stack.lineIndices) member = items[member].lineIndex;
        }
        result.groups.push_back(std::move(group));
    }
    return result;
}

} // namespace ob
