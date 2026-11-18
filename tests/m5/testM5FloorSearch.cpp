// Bounded floor search (ED-19, tests v12 U-PF-09 and X-PAT-18). The bounded-search adapter is driven by an
// instrumented synthetic backend that finishes after a fixed number of admitted states, so the accounting
// never depends on machine speed or the production search's state order. Oracles keep their own permit
// ledger, fake-clock trace and rectangle checks; they never call production code.
#include "doctest.h"
#include "m5FloorSearch.hpp"
#include "m5Physical.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Fixtures.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace ob::m5;
using namespace m5Fixtures;

namespace {

// The standard slot of both tests: 630 x 100 x 108 in, 45,000 lb, cap 64.
const TrailerCapability kSlot{630.0, 100.0, 108.0, 45000.0, 64};

// `count` rigid, nonstackable one-unit atoms `along` x 100 x 50 in, 1,000 lb, each from its own source line.
M5LaneDateInput atomsAlong(double along, size_t count) {
    vector<Atom> atoms;
    for (size_t index = 0; index < count; ++index) atoms.push_back(unitOf("U" + to_string(index), "G", 100.0, along, 50.0, 1000.0));
    return laneWith(std::move(atoms), 1, kSlot);
}

vector<size_t> firstAtoms(size_t count) {
    vector<size_t> atoms(count);
    for (size_t index = 0; index < count; ++index) atoms[index] = index;
    return atoms;
}

// The consecutive-row witness: atom i at (along * i, 0), `along` deep and 100 across.
LayoutWitness rowWitness(double along, size_t count) {
    LayoutWitness witness;
    for (size_t index = 0; index < count; ++index) witness.placements.push_back({index, along * static_cast<double>(index), 0.0, along, 100.0});
    witness.claimedExtentIn = along * static_cast<double>(count);
    return witness;
}

// Independent oracle: every rectangle on the 630 x 100 floor, none overlapping, each atom once, and the
// claimed extent the farthest edge.
struct OracleWitness {
    bool valid = true;
    double extentIn = 0.0;
    double weightLb = 0.0;
};

OracleWitness oracleCheck(const LayoutWitness& witness, double alongIn) {
    OracleWitness oracle;
    set<size_t> atoms;
    for (size_t index = 0; index < witness.placements.size(); ++index) {
        const WitnessPlacement& box = witness.placements[index];
        if (box.xIn < 0.0 || box.yIn < 0.0 || box.xIn + box.lengthIn > 630.0 || box.yIn + box.widthIn > 100.0) oracle.valid = false;
        if (box.lengthIn != alongIn || box.widthIn != 100.0) oracle.valid = false;
        if (!atoms.insert(box.atom).second) oracle.valid = false;
        for (size_t other = 0; other < index; ++other) {
            const WitnessPlacement& next = witness.placements[other];
            if (box.xIn < next.xIn + next.lengthIn && next.xIn < box.xIn + box.lengthIn && box.yIn < next.yIn + next.widthIn
                && next.yIn < box.yIn + box.widthIn) {
                oracle.valid = false;
            }
        }
        oracle.extentIn = max(oracle.extentIn, box.xIn + box.lengthIn);
        oracle.weightLb += 1000.0;
    }
    if (oracle.extentIn != witness.claimedExtentIn) oracle.valid = false;
    return oracle;
}

// The synthetic backend: returns `witness` on its `finishAfter`-th admitted state, Undecided before. Each
// call is one admitted state, recorded in the oracle's own ledger; `onState` runs first (a fake clock tick,
// a barrier).
SteppedSearch syntheticBackend(int finishAfter, LayoutWitness witness, atomic<int>& ledger, function<void()> onState = {}) {
    auto calls = make_shared<int>(0);
    return [=, &ledger]() {
        if (onState) onState();
        ++ledger;
        SearchStep step;
        if (++*calls == finishAfter) {
            step.verdict = FloorVerdict::Fits;
            step.witness = witness;
        }
        return step;
    };
}

constexpr int kNever = 1'000'000;

} // namespace

TEST_CASE("U-PF-09/sequentialLimits: route 5, per call 3: P stops at its call limit, Q fits on the last permit, R gets none"
          * doctest::test_suite("m5")) {
    m5Evidence::note("expected", {{"P", "Undecided callLimit, 3 states"}, {"Q", "Fits, 2 states"}, {"R", "Undecided routeLimit, 0 states"},
                                  {"charged", 5}});
    RouteSearchBudget route("U-PF-09 sequentialLimits", 3, 5);
    atomic<int> ledgerP{0}, ledgerQ{0}, ledgerR{0};
    const BoundedSearchResult p = runBoundedSearch(route, syntheticBackend(4, rowWitness(52.0, 11), ledgerP));
    CHECK(p.verdict == FloorVerdict::Undecided);
    CHECK(p.stop == SearchStop::CallLimit);
    CHECK(p.statesCharged == 3);
    CHECK(ledgerP == 3);
    CHECK(route.statesLeft() == 2);

    const BoundedSearchResult q = runBoundedSearch(route, syntheticBackend(2, rowWitness(53.0, 11), ledgerQ));
    CHECK(q.verdict == FloorVerdict::Fits);
    CHECK(q.stop == SearchStop::None);
    CHECK(q.statesCharged == 2);
    CHECK(ledgerQ == 2);
    CHECK(route.statesLeft() == 0);
    REQUIRE(q.witness);

    const BoundedSearchResult r = runBoundedSearch(route, syntheticBackend(1, rowWitness(54.0, 11), ledgerR));
    CHECK(r.verdict == FloorVerdict::Undecided);
    CHECK(r.stop == SearchStop::RouteLimit);
    CHECK(r.statesCharged == 0);
    CHECK(ledgerR == 0);

    CHECK(ledgerP + ledgerQ + ledgerR == 5);
    CHECK(route.statesCharged() == 5);
    CHECK(route.stopsOf(SearchStop::CallLimit) == 1);
    CHECK(route.stopsOf(SearchStop::RouteLimit) == 1);
    CHECK(p.routeIdentity == route.identity());
    CHECK(r.routeScope == "U-PF-09 sequentialLimits");

    // Q's witness is still checked after the route's states are gone: validation is not search.
    const M5LaneDateInput q11 = atomsAlong(53.0, 11);
    const WitnessMeasure checked = validateSuppliedWitness(q11, firstAtoms(11), *q.witness, kSlot, physicalSettings().floor);
    const OracleWitness oracle = oracleCheck(*q.witness, 53.0);
    CHECK(oracle.valid);
    CHECK(oracle.extentIn == 583.0);
    CHECK(oracle.weightLb == 11000.0);
    CHECK(checked.valid);
    CHECK(checked.occupiedExtentIn == 583.0);
    CHECK(route.statesCharged() == 5);
}

TEST_CASE("U-PF-09/exhaustedRetry: a retry in the spent context is charged nothing, admitted nothing and cached nowhere"
          * doctest::test_suite("m5")) {
    m5Evidence::note("expected", {{"retry", "Undecided routeLimit, 0 states"}, {"charged", 5}, {"cachedUndecided", false}});
    auto route = make_shared<RouteSearchBudget>("U-PF-09 exhaustedRetry", 3, 5);
    FloorVerdictCache cache;
    atomic<int> ledgerP{0}, ledgerQ{0}, ledgerRetry{0};
    runBoundedSearch(*route, syntheticBackend(4, rowWitness(52.0, 11), ledgerP), &cache, "P");
    runBoundedSearch(*route, syntheticBackend(2, rowWitness(53.0, 11), ledgerQ), &cache, "Q");
    REQUIRE(route->statesLeft() == 0);
    CHECK_FALSE(cache.find("P"));

    const BoundedSearchResult retry = runBoundedSearch(*route, syntheticBackend(4, rowWitness(52.0, 11), ledgerRetry), &cache, "P");
    CHECK(retry.verdict == FloorVerdict::Undecided);
    CHECK(retry.stop == SearchStop::RouteLimit);
    CHECK(retry.statesCharged == 0);
    CHECK(ledgerRetry == 0);
    CHECK(route->statesCharged() == 5);
    CHECK(route->statesLeft() == 0);
    CHECK_FALSE(cache.find("P"));
    REQUIRE(cache.find("Q"));
    CHECK(cache.find("Q")->verdict == FloorVerdict::Fits);

    // The production adapter in the same context: a wrapper keeps the route budget rather than making one,
    // and a load whose sorted rows fail is Undecided, never DoesNotFit, with no states to search.
    PhysicalSettings settings = physicalSettings();
    settings.routeBudget = route;
    const PhysicalSettings wrapped = withRouteBudget(settings, "another scope", {});
    CHECK(wrapped.routeBudget.get() == route.get());
    const M5LaneDateInput thirteen = atomsAlong(52.0, 13);
    const PhysicalCheck check = checkTruckInRoute(thirteen, firstAtoms(13), kSlot, wrapped, settings.floor.searchStateLimit);
    CHECK_FALSE(check.fits);
    CHECK(check.undecided);
    CHECK(check.stop == SearchStop::RouteLimit);
    CHECK(check.statesCharged == 0);
    CHECK(route->statesLeft() == 0);
}

TEST_CASE("U-PF-09/freshContextRetry: a new context of 6/6 completes P in 4 states; the cache keeps no Unknown"
          * doctest::test_suite("m5")) {
    m5Evidence::note("expected", {{"P", "Fits, 4 states in a fresh context"}, {"cacheHit", "0 states"}});
    FloorVerdictCache cache;
    RouteSearchBudget spent("U-PF-09 freshContextRetry spent", 3, 5);
    atomic<int> ledgerFirst{0}, ledgerFresh{0}, ledgerAgain{0};
    const BoundedSearchResult first = runBoundedSearch(spent, syntheticBackend(4, rowWitness(52.0, 11), ledgerFirst), &cache, "P");
    REQUIRE(first.verdict == FloorVerdict::Undecided);
    CHECK_FALSE(cache.find("P"));

    RouteSearchBudget fresh("U-PF-09 freshContextRetry fresh", 6, 6);
    CHECK(fresh.identity() != spent.identity());
    const BoundedSearchResult retried = runBoundedSearch(fresh, syntheticBackend(4, rowWitness(52.0, 11), ledgerFresh), &cache, "P");
    CHECK(retried.verdict == FloorVerdict::Fits);
    CHECK(retried.statesCharged == 4);
    CHECK_FALSE(retried.cacheHit);
    CHECK(ledgerFresh == 4);
    CHECK(fresh.statesLeft() == 2);

    // The verified geometry is reused: a cache hit reports the work it actually did, none.
    const BoundedSearchResult again = runBoundedSearch(fresh, syntheticBackend(4, rowWitness(52.0, 11), ledgerAgain), &cache, "P");
    CHECK(again.verdict == FloorVerdict::Fits);
    CHECK(again.cacheHit);
    CHECK(again.statesCharged == 0);
    CHECK(ledgerAgain == 0);
    CHECK(fresh.statesLeft() == 2);
    REQUIRE(again.witness);
    CHECK(oracleCheck(*again.witness, 52.0).valid);

    // Each lane-date run owns a fresh context with the params' limits and reports it.
    const M5LaneDateInput lane = atomsAlong(52.0, 11);
    const M5LaneOutcome one = plan(lane, syntheticProfile());
    const M5LaneOutcome two = plan(lane, syntheticProfile());
    CHECK(one.floorSearch.perCallStates == 200'000);
    CHECK(one.floorSearch.routeStates == 1'000'000);
    // PR-10e: a context is named by its own lane-date, not by how many ran before it in the process, so the same
    // lane-date always reports the same identity and a different lane-date another.
    CHECK(one.floorSearch.identity == two.floorSearch.identity);
    CHECK(one.floorSearch.identity == identityOfScope(one.floorSearch.scope));
    M5LaneDateInput otherLane = lane;
    otherLane.lane = {"3000", "3100", "TL"};
    CHECK(plan(otherLane, syntheticProfile()).floorSearch.identity != one.floorSearch.identity);
    CHECK(one.floorSearch.scope.find(lane.lane.text()) != string::npos);
    CHECK(one.floorSearch.statesCharged >= 0);
    CHECK(one.floorSearch.statesCharged <= one.floorSearch.routeStates);
}

TEST_CASE("U-PF-09/parallelAccounting: two four-state jobs on a shared route of 5, per call 3, never pass 5 or 3"
          * doctest::test_suite("m5")) {
    m5Evidence::note("expected", {{"totalAdmitted", 5}, {"perCallMax", 3}, {"completed", 0}});
    // Barrier-controlled: each admitted state waits for its turn. Alternating runs A, B, A, B, ...; serial
    // runs all of A first. A job that has stopped no longer holds a turn.
    enum class Order { Alternating, Serial };
    auto runJobs = [](Order order) {
        RouteSearchBudget route("U-PF-09 parallelAccounting", 3, 5);
        mutex turnMutex;
        condition_variable turnChanged;
        int turn = 0;
        bool done[2] = {false, false};
        atomic<int> ledger[2] = {{0}, {0}};
        BoundedSearchResult results[2];
        auto waitTurn = [&](int job) {
            unique_lock<mutex> lock(turnMutex);
            turnChanged.wait(lock, [&] { return turn == job || done[1 - job]; });
            if (order == Order::Alternating) turn = 1 - job;
            turnChanged.notify_all();
        };
        auto run = [&](int job) {
            results[job] = runBoundedSearch(route, syntheticBackend(4, rowWitness(52.0, 11), ledger[job], [&, job] { waitTurn(job); }));
            lock_guard<mutex> lock(turnMutex);
            done[job] = true;
            if (order == Order::Serial) turn = 1 - job;
            turnChanged.notify_all();
        };
        thread first(run, 0), second(run, 1);
        first.join();
        second.join();
        CHECK(ledger[0] + ledger[1] == 5);
        CHECK(ledger[0] <= 3);
        CHECK(ledger[1] <= 3);
        CHECK(results[0].statesCharged + results[1].statesCharged == 5);
        CHECK(results[0].verdict == FloorVerdict::Undecided);
        CHECK(results[1].verdict == FloorVerdict::Undecided);
        CHECK(route.statesLeft() == 0);
        CHECK(route.statesCharged() == 5);
    };
    runJobs(Order::Alternating);
    runJobs(Order::Serial);

    // Free-running: no particular division of the five permits is required, only the totals.
    for (int trial = 0; trial < 50; ++trial) {
        RouteSearchBudget route("U-PF-09 parallelAccounting free", 3, 5);
        atomic<int> ledger[2] = {{0}, {0}};
        BoundedSearchResult results[2];
        thread first([&] { results[0] = runBoundedSearch(route, syntheticBackend(4, rowWitness(52.0, 11), ledger[0])); });
        thread second([&] { results[1] = runBoundedSearch(route, syntheticBackend(4, rowWitness(52.0, 11), ledger[1])); });
        first.join();
        second.join();
        CHECK(ledger[0] + ledger[1] == 5);
        CHECK(max(ledger[0].load(), ledger[1].load()) <= 3);
        CHECK(results[0].verdict == FloorVerdict::Undecided);
        CHECK(results[1].verdict == FloorVerdict::Undecided);
        CHECK(route.statesLeft() == 0);
    }
}

TEST_CASE("U-PF-09/wallDeadlineWins: a 3-second deadline stops a four-state job at 3 states with 3 permits left"
          * doctest::test_suite("m5")) {
    m5Evidence::note("expected", {{"stop", "wallDeadline"}, {"states", 3}, {"routeLeft", 3}});
    double now = 0.0;
    vector<double> trace;  // the fake clock at each admitted state
    RouteSearchBudget route("U-PF-09 wallDeadlineWins", 6, 6, [&now] { return now >= 3.0; });
    atomic<int> ledger{0}, ledgerLater{0};
    const BoundedSearchResult result = runBoundedSearch(route, syntheticBackend(4, rowWitness(52.0, 11), ledger, [&] {
                                                            trace.push_back(now);
                                                            now += 1.0;
                                                        }));
    CHECK(result.verdict == FloorVerdict::Undecided);
    CHECK(result.stop == SearchStop::WallDeadline);
    CHECK(result.statesCharged == 3);
    CHECK(ledger == 3);
    CHECK(trace == vector<double>{0.0, 1.0, 2.0});
    CHECK(now == 3.0);
    CHECK(route.statesLeft() == 3);

    // A later method, repair or final search in the same context does not get the time back.
    const BoundedSearchResult later = runBoundedSearch(route, syntheticBackend(1, rowWitness(52.0, 11), ledgerLater));
    CHECK(later.stop == SearchStop::WallDeadline);
    CHECK(later.statesCharged == 0);
    CHECK(ledgerLater == 0);
    CHECK(route.stopsOf(SearchStop::WallDeadline) == 2);
}

TEST_CASE("X-PAT-18/undecidedWithIncumbent: checked W10 is kept, ten shipped and one left, Unproven at the named limit"
          * doctest::test_suite("m5")) {
    m5Evidence::note("expected", {{"shipped", 10}, {"left", 1}, {"proof", "Unproven"}, {"stop", "callLimit"}});
    const M5LaneDateInput input = atomsAlong(52.0, 11);
    const LayoutWitness w10 = rowWitness(52.0, 10);
    const OracleWitness oracle = oracleCheck(w10, 52.0);
    CHECK(oracle.valid);
    CHECK(oracle.extentIn == 520.0);
    CHECK(oracle.weightLb == 10000.0);
    CHECK(validateSuppliedWitness(input, firstAtoms(10), w10, kSlot, physicalSettings().floor).valid);

    RouteSearchBudget route("X-PAT-18 undecidedWithIncumbent", 3, 1'000);
    atomic<int> ledger{0};
    const BoundedSearchResult eleven = runBoundedSearch(route, syntheticBackend(kNever, rowWitness(52.0, 11), ledger));
    CHECK(eleven.verdict == FloorVerdict::Undecided);
    CHECK(eleven.verdict != FloorVerdict::DoesNotFit);
    CHECK(eleven.stop == SearchStop::CallLimit);
    CHECK_FALSE(eleven.witness);

    const ServiceClaim claim = claimMaximumService(11, 10, 11, eleven, nullopt);
    CHECK(claim.shipped == 10);
    CHECK(claim.leftOver == 1);
    CHECK_FALSE(claim.provenBest);
    CHECK(claim.because.find("callLimit") != string::npos);
    CHECK(claim.because.find("not a proof") != string::npos);
}

TEST_CASE("X-PAT-18/undecidedNoIncumbent: exhaustion before any checked load ships nothing and proves nothing"
          * doctest::test_suite("m5")) {
    m5Evidence::note("expected", {{"shipped", 0}, {"left", 11}, {"proof", "Unproven"}, {"verdict", "Undecided"}});
    RouteSearchBudget route("X-PAT-18 undecidedNoIncumbent", 3, 1'000);
    atomic<int> ledger{0};
    const BoundedSearchResult eleven = runBoundedSearch(route, syntheticBackend(kNever, rowWitness(52.0, 11), ledger));
    CHECK(eleven.verdict == FloorVerdict::Undecided);
    CHECK(eleven.certificate.empty());
    const ServiceClaim claim = claimMaximumService(11, nullopt, 11, eleven, nullopt);
    CHECK(claim.shipped == 0);
    CHECK(claim.leftOver == 11);
    CHECK_FALSE(claim.provenBest);
}

TEST_CASE("X-PAT-18/witnessCertificate: W11 is legal without enumeration; the demand bound of 11 certifies service"
          * doctest::test_suite("m5")) {
    m5Evidence::note("expected", {{"extent", 572}, {"weight", 11000}, {"shipped", 11}, {"proof", "ProvenBest (service bound 11)"}});
    const M5LaneDateInput input = atomsAlong(52.0, 11);
    const LayoutWitness w11 = rowWitness(52.0, 11);
    const OracleWitness oracle = oracleCheck(w11, 52.0);
    CHECK(oracle.valid);
    CHECK(oracle.extentIn == 572.0);
    CHECK(oracle.weightLb == 11000.0);
    const WitnessMeasure measure = validateSuppliedWitness(input, firstAtoms(11), w11, kSlot, physicalSettings().floor);
    CHECK(measure.valid);
    CHECK(measure.occupiedExtentIn == 572.0);
    CHECK(measure.rearInches == 58.0);

    // The production check needs no states for it: a route of one state is enough and none is spent.
    PhysicalSettings settings = physicalSettings();
    settings.routeBudget = make_shared<RouteSearchBudget>("X-PAT-18 witnessCertificate", 1, 1);
    const PhysicalCheck check = checkTruckInRoute(input, firstAtoms(11), kSlot, settings, 1);
    CHECK(check.fits);
    CHECK(check.statesCharged == 0);

    // An unrelated incomplete search does not take the bound's certificate away.
    RouteSearchBudget other("X-PAT-18 unrelated", 1, 1);
    atomic<int> ledger{0};
    const BoundedSearchResult unrelated = runBoundedSearch(other, syntheticBackend(kNever, rowWitness(52.0, 11), ledger));
    REQUIRE(unrelated.verdict == FloorVerdict::Undecided);
    const ServiceClaim claim = claimMaximumService(11, 11, 11, unrelated, 11);
    CHECK(claim.shipped == 11);
    CHECK(claim.leftOver == 0);
    CHECK(claim.provenBest);
}

TEST_CASE("X-PAT-18/provedNoFitControl: thirteen atoms are refused by a complete certificate; twelve fit and are best"
          * doctest::test_suite("m5")) {
    m5Evidence::note("expected", {{"thirteen", "DoesNotFit (67,600 > 63,000 sq in)"}, {"twelve", "624 in, 12,000 lb"}, {"best", 12}, {"left", 1}});
    const M5LaneDateInput input = atomsAlong(52.0, 13);
    CHECK(13 * 52 * 100 > 630 * 100);  // the area certificate, independently

    PhysicalSettings settings = physicalSettings();
    settings.routeBudget = make_shared<RouteSearchBudget>("X-PAT-18 provedNoFitControl", 200'000, 1'000'000);
    const PhysicalCheck thirteen = checkTruckInRoute(input, firstAtoms(13), kSlot, settings, 200'000);
    CHECK_FALSE(thirteen.fits);
    CHECK_FALSE(thirteen.undecided);
    CHECK(thirteen.stop == SearchStop::None);

    const LayoutWitness w12 = rowWitness(52.0, 12);
    const OracleWitness oracle = oracleCheck(w12, 52.0);
    CHECK(oracle.valid);
    CHECK(oracle.extentIn == 624.0);
    CHECK(oracle.weightLb == 12000.0);
    CHECK(validateSuppliedWitness(input, firstAtoms(12), w12, kSlot, physicalSettings().floor).valid);

    BoundedSearchResult refused;
    refused.verdict = FloorVerdict::DoesNotFit;
    refused.certificate = "area 67,600 sq in over the 63,000 sq in floor";
    const ServiceClaim claim = claimMaximumService(13, 12, 13, refused, nullopt);
    CHECK(claim.shipped == 12);
    CHECK(claim.leftOver == 1);
    CHECK(claim.provenBest);

    // Solved end to end on one slot: twelve ship and one is left; the refusal is not a no-service claim.
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    CHECK(shippedCount(outcome) == 12);
    CHECK(outcome.leftOver.size() == 1);
}

TEST_CASE("X-PAT-18/invalidSuppliedWitness: U10 moved onto U0 with the stale extent 572 is rejected as supplied"
          * doctest::test_suite("m5")) {
    m5Evidence::note("expected", {{"valid", false}, {"findings", "overlap, stale extent"}});
    const M5LaneDateInput input = atomsAlong(52.0, 11);
    LayoutWitness moved = rowWitness(52.0, 11);
    moved.placements[10].xIn = 0.0;
    moved.placements[10].yIn = 0.0;
    REQUIRE(moved.claimedExtentIn == 572.0);
    CHECK_FALSE(oracleCheck(moved, 52.0).valid);

    const WitnessMeasure measure = validateSuppliedWitness(input, firstAtoms(11), moved, kSlot, physicalSettings().floor);
    CHECK_FALSE(measure.valid);
    CHECK(measure.occupiedExtentIn == 520.0);
    bool overlap = false, staleExtent = false;
    for (const string& finding : measure.findings) {
        overlap = overlap || finding.find("overlaps") != string::npos;
        staleExtent = staleExtent || finding.find("claimed extent") != string::npos;
    }
    CHECK(overlap);
    CHECK(staleExtent);
}

TEST_CASE("X-PAT-18 a supplied witness is judged by the site's turning rule: a turned footprint fails when pallets cannot turn"
          * doctest::test_suite("m5")) {
    const M5LaneDateInput input = atomsAlong(52.0, 2);
    const LayoutWitness longSideAcross = rowWitness(52.0, 2);
    LayoutWitness shortSideAcross;
    shortSideAcross.placements = {{0, 0.0, 0.0, 100.0, 52.0}, {1, 100.0, 0.0, 100.0, 52.0}};
    shortSideAcross.claimedExtentIn = 200.0;
    const auto turnedFinding = [](const WitnessMeasure& measure) {
        return any_of(measure.findings.begin(), measure.findings.end(),
                      [](const string& finding) { return finding.find("is turned") != string::npos; });
    };
    ob::FloorPatternSettings turning = physicalSettings().floor;
    CHECK(validateSuppliedWitness(input, firstAtoms(2), longSideAcross, kSlot, turning).valid);
    CHECK(validateSuppliedWitness(input, firstAtoms(2), shortSideAcross, kSlot, turning).valid);

    ob::FloorPatternSettings fixedLong = turning;
    fixedLong.allowRotation = false;
    fixedLong.fixedOrientation = ob::FixedOrientation::LongSideAcross;
    CHECK(validateSuppliedWitness(input, firstAtoms(2), longSideAcross, kSlot, fixedLong).valid);
    const WitnessMeasure refused = validateSuppliedWitness(input, firstAtoms(2), shortSideAcross, kSlot, fixedLong);
    CHECK_FALSE(refused.valid);
    CHECK(turnedFinding(refused));

    ob::FloorPatternSettings fixedShort = fixedLong;
    fixedShort.fixedOrientation = ob::FixedOrientation::ShortSideAcross;
    CHECK(validateSuppliedWitness(input, firstAtoms(2), shortSideAcross, kSlot, fixedShort).valid);
    CHECK(turnedFinding(validateSuppliedWitness(input, firstAtoms(2), longSideAcross, kSlot, fixedShort)));
}
