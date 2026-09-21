#include "stackBuilder.hpp"

#include "stackRules.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace ob {
namespace {

// Below this a quantity is floating-point residue, not a real fraction of a pallet.
constexpr double kQuantityEpsilon = 1e-9;

struct Item {
    std::size_t lineIndex = 0;
    UnitLoad load;
    double quantity = 0.0;
};

using Order = std::vector<std::size_t>;

struct Context {
    const std::vector<Item>& items;
    const M2Params& params;
    double ceilingIn;
};

Order naturalOrder(std::size_t count) {
    Order order(count);
    std::iota(order.begin(), order.end(), std::size_t{0});
    return order;
}

template <typename Less>
Order sortedOrder(std::size_t count, Less less) {
    Order order = naturalOrder(count);
    std::stable_sort(order.begin(), order.end(), less);
    return order;
}

// `top` may join the chain only if every load already in it can still carry what would
// sit above it. Each level is checked with canStack against a copy of that load that
// already counts the height and weight of everything stacked over it.
bool canExtendChain(const Context& context, const Order& chain, const UnitLoad& top) {
    double heightAboveIn = 0.0;
    double weightAboveLb = 0.0;
    for (std::size_t level = chain.size(); level-- > 0;) {
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
    std::vector<double> remaining(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) remaining[i] = items[i].quantity;

    StackSet set;
    set.method = method;
    for (const std::size_t base : baseOrder) {
        while (remaining[base] > kQuantityEpsilon) {
            Order chain{base};
            for (bool extended = true; extended;) {
                extended = false;
                for (const std::size_t top : topOrder) {
                    if (remaining[top] > kQuantityEpsilon
                        && canExtendChain(context, chain, items[top].load)) {
                        chain.push_back(top);
                        extended = true;
                        break;
                    }
                }
            }

            std::vector<std::size_t> timesInChain(items.size(), 0);
            for (const std::size_t member : chain) ++timesInChain[member];
            double quantity = remaining[base] / static_cast<double>(timesInChain[base]);
            std::size_t limiting = base;
            for (const std::size_t member : chain) {
                const double fits = remaining[member] / static_cast<double>(timesInChain[member]);
                if (fits < quantity) { quantity = fits; limiting = member; }
            }
            double stackWeightLb = 0.0;
            for (const std::size_t member : chain) {
                remaining[member] -= quantity;
                stackWeightLb += items[member].load.weightLb;
            }
            for (const std::size_t member : chain) {
                if (remaining[member] <= kQuantityEpsilon || member == limiting) remaining[member] = 0.0;
            }
            set.floorPositions += quantity;
            set.heaviestStackLb = std::max(set.heaviestStackLb, stackWeightLb);
            set.stacks.push_back(BuiltStack{std::move(chain), quantity});
        }
    }
    return set;
}

StackSet runMethod(const Context& context, StackMethod method) {
    const auto& items = context.items;
    const std::size_t count = items.size();
    const auto weightOf = [&](std::size_t i) { return items[i].load.weightLb; };
    const auto heightOf = [&](std::size_t i) { return items[i].load.heightIn; };
    const auto criOf = [&](std::size_t i) { return items[i].load.cri; };

    switch (method) {
    case StackMethod::Natural:
        return buildWithOrders(context, method, naturalOrder(count), naturalOrder(count));
    case StackMethod::Target:
        // Fill toward the ceiling: the tallest load that still fits goes on top.
        return buildWithOrders(context, method, naturalOrder(count),
                               sortedOrder(count, [&](std::size_t a, std::size_t b) {
                                   return heightOf(a) > heightOf(b); }));
    case StackMethod::TallAndHeavy:
        // Heavy, tall loads carry; the shortest loads ride on top.
        return buildWithOrders(context, method,
                               sortedOrder(count, [&](std::size_t a, std::size_t b) {
                                   return weightOf(a) != weightOf(b) ? weightOf(a) > weightOf(b)
                                                                     : heightOf(a) > heightOf(b); }),
                               sortedOrder(count, [&](std::size_t a, std::size_t b) {
                                   return heightOf(a) < heightOf(b); }));
    case StackMethod::BaseAndTop:
        // Strongest crush rating carries; the lightest loads ride on top.
        return buildWithOrders(context, method,
                               sortedOrder(count, [&](std::size_t a, std::size_t b) {
                                   return criOf(a) > criOf(b); }),
                               sortedOrder(count, [&](std::size_t a, std::size_t b) {
                                   return weightOf(a) < weightOf(b); }));
    case StackMethod::TryHard:
        break;
    }
    return StackSet{};
}

bool isBetter(const StackSet& candidate, const StackSet& incumbent, bool weightBound) {
    if (std::abs(candidate.floorPositions - incumbent.floorPositions) > kQuantityEpsilon) {
        return candidate.floorPositions < incumbent.floorPositions;
    }
    return weightBound && candidate.heaviestStackLb < incumbent.heaviestStackLb;
}

// Try Hard re-runs Base & Top ordering from several rotated starting bases, up to the
// configured attempt cap, and keeps the best. Deterministic, so results are repeatable.
StackSet runTryHard(const Context& context, std::size_t attempts, bool weightBound) {
    const auto& items = context.items;
    const std::size_t count = items.size();
    const Order tops = sortedOrder(count, [&](std::size_t a, std::size_t b) {
        return items[a].load.weightLb < items[b].load.weightLb; });
    StackSet best;
    for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
        Order bases = naturalOrder(count);
        std::rotate(bases.begin(), bases.begin() + static_cast<std::ptrdiff_t>(
                        (attempt + 1) * count / (attempts + 1)), bases.end());
        StackSet candidate = buildWithOrders(context, StackMethod::TryHard, bases, tops);
        if (attempt == 0 || isBetter(candidate, best, weightBound)) best = std::move(candidate);
    }
    return best;
}

void requireAlignedInputs(const SegregationResult& segregation, const std::vector<JoinedLine>& lines,
                          const std::vector<double>& palletsPerLine, const BindingResult& binding,
                          const TrailerSpec& trailer) {
    if (!std::isfinite(trailer.stackHeightCeilingIn) || trailer.stackHeightCeilingIn <= 0.0) {
        throw std::invalid_argument("stackBuilder: trailer stackHeightCeilingIn must be positive");
    }
    if (palletsPerLine.size() != lines.size()) {
        throw std::invalid_argument("stackBuilder: pallet vector does not match the joined lines");
    }
    if (binding.groups.size() != segregation.groups.size()) {
        throw std::invalid_argument("stackBuilder: binding results do not match the groups");
    }
}

} // namespace

StackingResult buildStacks(const SegregationResult& segregation,
                           const std::vector<JoinedLine>& lines,
                           const std::vector<double>& palletsPerLine,
                           const BindingResult& binding,
                           const M2Params& params,
                           const TrailerSpec& trailer) {
    requireAlignedInputs(segregation, lines, palletsPerLine, binding, trailer);

    StackingResult result;
    result.groups.reserve(segregation.groups.size());
    for (std::size_t groupIndex = 0; groupIndex < segregation.groups.size(); ++groupIndex) {
        std::vector<Item> items;
        items.reserve(segregation.groups[groupIndex].lineIndices.size());
        for (const std::size_t lineIndex : segregation.groups[groupIndex].lineIndices) {
            if (lineIndex >= lines.size()) {
                throw std::invalid_argument("stackBuilder: group line index out of range");
            }
            const double quantity = palletsPerLine[lineIndex];
            if (!std::isfinite(quantity) || quantity < 0.0) {
                ++result.excludedInvalidQuantityLines;
                continue;
            }
            if (quantity == 0.0) continue;
            UnitLoad load = buildUnitLoad(lines[lineIndex], params);
            if (load.error != UnitLoadError::None) {
                result.excludedLines.push_back({lineIndex, load.error});
                continue;
            }
            items.push_back(Item{lineIndex, std::move(load), quantity});
        }

        const Context context{items, params, trailer.stackHeightCeilingIn};
        const bool weightBound = binding.groups[groupIndex].binding == BindingConstraint::Weight;
        std::vector<StackSet> candidates;
        for (const StackMethod method : {StackMethod::Natural, StackMethod::Target,
                                         StackMethod::TallAndHeavy, StackMethod::BaseAndTop}) {
            candidates.push_back(runMethod(context, method));
        }
        if (params.pass2AttemptCap > 0) {
            candidates.push_back(runTryHard(context, static_cast<std::size_t>(params.pass2AttemptCap),
                                            weightBound));
        }

        GroupStacking group;
        std::size_t bestIndex = 0;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
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
