#include "m5FloorSearch.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

using namespace std;

namespace ob {
namespace m5 {

namespace {

atomic<uint64_t> nextRouteIdentity{1};

} // namespace

const char* floorVerdictName(FloorVerdict verdict) {
    switch (verdict) {
    case FloorVerdict::Fits: return "Fits";
    case FloorVerdict::DoesNotFit: return "DoesNotFit";
    case FloorVerdict::Undecided: return "Undecided";
    }
    return "Undecided";
}

const char* searchStopName(SearchStop stop) {
    switch (stop) {
    case SearchStop::None: return "none";
    case SearchStop::CallLimit: return "callLimit";
    case SearchStop::RouteLimit: return "routeLimit";
    case SearchStop::WallDeadline: return "wallDeadline";
    }
    return "none";
}

uint64_t identityOfScope(const string& scope) {
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char character : scope) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return hash;
}

RouteSearchBudget::RouteSearchBudget(string scope, long long perCallStates, long long routeStates, function<bool()> wallStop,
                                     optional<uint64_t> identity)
    : scope_(std::move(scope)), identity_(identity ? *identity : nextRouteIdentity.fetch_add(1)), perCallStates_(perCallStates),
      routeStates_(routeStates), statesLeft_(routeStates), wallStop_(std::move(wallStop)) {
    if (perCallStates < 1 || routeStates < 1) {
        throw invalid_argument("floor-search state limits must be at least 1 (per call " + to_string(perCallStates) + ", route "
                               + to_string(routeStates) + ")");
    }
}

long long RouteSearchBudget::admit(long long wanted) {
    wanted = min(wanted, perCallStates_);
    if (wanted <= 0) return 0;
    long long left = statesLeft_.load();
    while (left > 0) {
        const long long granted = min(wanted, left);
        if (statesLeft_.compare_exchange_weak(left, left - granted)) return granted;
    }
    return 0;
}

void RouteSearchBudget::giveBack(long long unused) {
    if (unused > 0) statesLeft_.fetch_add(unused);
}

void RouteSearchBudget::recordStop(SearchStop stop) {
    switch (stop) {
    case SearchStop::CallLimit: ++callLimitStops_; break;
    case SearchStop::RouteLimit: ++routeLimitStops_; break;
    case SearchStop::WallDeadline: ++wallDeadlineStops_; break;
    case SearchStop::None: break;
    }
}

long long RouteSearchBudget::stopsOf(SearchStop stop) const {
    switch (stop) {
    case SearchStop::CallLimit: return callLimitStops_.load();
    case SearchStop::RouteLimit: return routeLimitStops_.load();
    case SearchStop::WallDeadline: return wallDeadlineStops_.load();
    case SearchStop::None: break;
    }
    return 0;
}

FloorSearchRecord floorSearchRecordOf(const RouteSearchBudget& budget) {
    FloorSearchRecord record;
    record.scope = budget.scope();
    record.identity = budget.identity();
    record.perCallStates = budget.perCallStates();
    record.routeStates = budget.routeStates();
    record.statesCharged = budget.statesCharged();
    record.callLimitStops = budget.stopsOf(SearchStop::CallLimit);
    record.routeLimitStops = budget.stopsOf(SearchStop::RouteLimit);
    record.wallDeadlineStops = budget.stopsOf(SearchStop::WallDeadline);
    return record;
}

optional<BoundedSearchResult> FloorVerdictCache::find(const string& key) const {
    lock_guard<mutex> lock(mutex_);
    const auto found = byKey_.find(key);
    if (found == byKey_.end()) return nullopt;
    return found->second;
}

void FloorVerdictCache::put(const string& key, const BoundedSearchResult& result) {
    if (result.verdict == FloorVerdict::Undecided) return;
    lock_guard<mutex> lock(mutex_);
    byKey_[key] = result;
}

BoundedSearchResult runBoundedSearch(RouteSearchBudget& budget, const SteppedSearch& search, FloorVerdictCache* cache,
                                     const string& cacheKey) {
    BoundedSearchResult result;
    result.routeScope = budget.scope();
    result.routeIdentity = budget.identity();
    if (cache) {
        if (optional<BoundedSearchResult> known = cache->find(cacheKey)) {
            result.verdict = known->verdict;
            result.witness = std::move(known->witness);
            result.certificate = std::move(known->certificate);
            result.cacheHit = true;
            return result;
        }
    }
    for (;;) {
        if (budget.pastWallDeadline()) {
            result.stop = SearchStop::WallDeadline;
            break;
        }
        if (result.statesCharged >= budget.perCallStates()) {
            result.stop = SearchStop::CallLimit;
            break;
        }
        if (budget.admit(1) == 0) {
            result.stop = SearchStop::RouteLimit;
            break;
        }
        ++result.statesCharged;
        SearchStep step = search();
        if (step.verdict && *step.verdict != FloorVerdict::Undecided) {
            result.verdict = *step.verdict;
            result.witness = std::move(step.witness);
            result.certificate = std::move(step.certificate);
            if (cache) cache->put(cacheKey, result);
            return result;
        }
    }
    budget.recordStop(result.stop);
    return result;
}

ServiceClaim claimMaximumService(long long available, optional<long long> incumbent, long long improvingCount,
                                 const BoundedSearchResult& improving, optional<long long> upperBound) {
    ServiceClaim claim;
    claim.shipped = incumbent.value_or(0);
    if (improving.verdict == FloorVerdict::Fits && improving.witness) claim.shipped = max(claim.shipped, improvingCount);
    claim.leftOver = available - claim.shipped;
    const string load = to_string(improvingCount) + "-unit-load search ";
    if (upperBound && claim.shipped >= *upperBound) {
        claim.provenBest = true;
        claim.because = "a checked load of " + to_string(claim.shipped) + " meets the independent bound of " + to_string(*upperBound);
    } else if (improving.verdict == FloorVerdict::DoesNotFit && improvingCount == claim.shipped + 1) {
        claim.provenBest = true;
        claim.because = "the " + load + "is refused by a complete certificate (" + improving.certificate + ")";
    } else if (improving.verdict == FloorVerdict::Undecided) {
        claim.because = "Unproven: the " + load + "stopped Undecided at its " + searchStopName(improving.stop)
                        + "; that is not a proof that it does not fit";
    } else {
        claim.because = "Unproven: no certificate closes the gap above " + to_string(claim.shipped);
    }
    return claim;
}

} // namespace m5
} // namespace ob
