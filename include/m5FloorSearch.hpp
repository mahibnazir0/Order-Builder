#pragma once
// ============================================================================
// m5FloorSearch.hpp - the bounded floor search every M5 floor check goes
// through (ED-19, R-PROOF, R-GROUP; tests v12 U-PF-09, X-PAT-18).
//
// A search ends Fits (with a witness), DoesNotFit (with a complete
// certificate) or Undecided, and an Undecided search names its stop: its own
// per-call limit, the route's shared state limit, or the wall deadline. One
// RouteSearchBudget is owned per route context (one M5 lane-date run) and every
// floor check of that context - methods A and B, their blocks, repair and the
// fullness probes - draws its states from it, so no check or retry receives a
// fresh allowance. Only decided verdicts are cached: an Undecided search is
// never remembered as "does not fit".
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ob {
namespace m5 {

enum class FloorVerdict { Fits, DoesNotFit, Undecided };

// Why an Undecided search stopped. None for a decided one.
enum class SearchStop { None, CallLimit, RouteLimit, WallDeadline };

const char* floorVerdictName(FloorVerdict verdict);
const char* searchStopName(SearchStop stop);

// One rectangle of a layout witness: which atom (an index into the checked atoms' input), where its
// front-left corner stands (x along the trailer from the front, y across it) and its size on the floor.
struct WitnessPlacement {
    std::size_t atom = 0;
    double xIn = 0.0;
    double yIn = 0.0;
    double lengthIn = 0.0;  // along
    double widthIn = 0.0;   // across
};

struct LayoutWitness {
    std::vector<WitnessPlacement> placements;
    double claimedExtentIn = 0.0;  // the farthest x + length the witness claims
};

// The states one route context may spend on floor searches, shared by every check in it, and the per-call
// cap no single search may pass. Thread-safe: a permit is admitted only while states are left, so the
// count never underflows however checks interleave. Not copyable: a copy would be a second budget.
class RouteSearchBudget {
public:
    // wallStop, when set, is the context's wall deadline: once it says true no further state is admitted.
    RouteSearchBudget(std::string scope, long long perCallStates, long long routeStates,
                      std::function<bool()> wallStop = {});
    RouteSearchBudget(const RouteSearchBudget&) = delete;
    RouteSearchBudget& operator=(const RouteSearchBudget&) = delete;

    const std::string& scope() const { return scope_; }
    std::uint64_t identity() const { return identity_; }  // unique per budget in the process
    long long perCallStates() const { return perCallStates_; }
    long long routeStates() const { return routeStates_; }
    long long statesLeft() const { return statesLeft_.load(); }
    long long statesCharged() const { return routeStates_ - statesLeft_.load(); }
    bool pastWallDeadline() const { return wallStop_ && wallStop_(); }

    // Admits up to `wanted` states (never more than the per-call cap or what is left) and returns how many.
    long long admit(long long wanted);
    // Returns admitted states a search did not use.
    void giveBack(long long unused);
    void recordStop(SearchStop stop);
    long long stopsOf(SearchStop stop) const;

private:
    std::string scope_;
    std::uint64_t identity_ = 0;
    long long perCallStates_ = 0;
    long long routeStates_ = 0;
    std::atomic<long long> statesLeft_{0};
    std::atomic<long long> callLimitStops_{0};
    std::atomic<long long> routeLimitStops_{0};
    std::atomic<long long> wallDeadlineStops_{0};
    std::function<bool()> wallStop_;
};

// What a route context spent on floor searches, for its report: the scope and identity, both limits, the
// states charged and how many searches each stop ended.
struct FloorSearchRecord {
    std::string scope;
    std::uint64_t identity = 0;
    long long perCallStates = 0;
    long long routeStates = 0;
    long long statesCharged = 0;
    long long callLimitStops = 0;
    long long routeLimitStops = 0;
    long long wallDeadlineStops = 0;
};
FloorSearchRecord floorSearchRecordOf(const RouteSearchBudget& budget);

struct BoundedSearchResult {
    FloorVerdict verdict = FloorVerdict::Undecided;
    SearchStop stop = SearchStop::None;
    long long statesCharged = 0;
    bool cacheHit = false;
    std::optional<LayoutWitness> witness;  // with Fits
    std::string certificate;               // with DoesNotFit: what proves it
    std::string routeScope;
    std::uint64_t routeIdentity = 0;
};

// One state of a search that is run state by state: Fits with its witness, DoesNotFit with its complete
// certificate, or nothing yet.
struct SearchStep {
    std::optional<FloorVerdict> verdict;
    std::optional<LayoutWitness> witness;
    std::string certificate;
};
using SteppedSearch = std::function<SearchStep()>;

// Decided verdicts by the key of the geometry checked (class, trailer and cap). Holds geometry only, never
// stock or acceptance, and never an Undecided result. Thread-safe.
class FloorVerdictCache {
public:
    std::optional<BoundedSearchResult> find(const std::string& key) const;
    void put(const std::string& key, const BoundedSearchResult& result);  // ignores Undecided

private:
    mutable std::mutex mutex_;
    std::map<std::string, BoundedSearchResult> byKey_;
};

// Runs `search` one admitted state at a time under the budget: before each state the wall deadline, then
// the per-call cap, then the route's states are checked, and the first that refuses stops the search
// Undecided with that stop. A cache hit charges nothing; a decided result is cached.
BoundedSearchResult runBoundedSearch(RouteSearchBudget& budget, const SteppedSearch& search,
                                     FloorVerdictCache* cache = nullptr, const std::string& cacheKey = {});

// What a stop may claim for a maximum-service objective (R-PROOF, X-PAT-18). `incumbent` is the checked
// load's count, `improving` the search for every load of `improvingCount` (alike atoms, so one check stands
// for them all), `upperBound` an independent bound on the count (all available demand, say). Proven only
// when a certificate closes the gap: the best count reaching the bound, or a complete DoesNotFit for one
// more than it. An Undecided search never proves anything, and never turns into a refusal of the lane.
struct ServiceClaim {
    long long shipped = 0;
    long long leftOver = 0;
    bool provenBest = false;
    std::string because;
};
ServiceClaim claimMaximumService(long long available, std::optional<long long> incumbent, long long improvingCount,
                                 const BoundedSearchResult& improving, std::optional<long long> upperBound);

} // namespace m5
} // namespace ob
