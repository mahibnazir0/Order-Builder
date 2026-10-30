#include "doctest.h"
#include "../acceptance/acceptanceHarness.hpp"
#include "../m4AugustFixtures.hpp"
#include "../m4SyntheticDay.hpp"
#include "benchmarkHarness.hpp"
#include "reportFormat.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

using namespace std;
using namespace ob;

namespace {

// The figure the client's own front-end check stops a run at (I6).
constexpr double kMaxUnitLoadsPerTruck = 64.0;
// The spec's measure of a weight-heavy lane: a truck over 40,000 lb (E210).
constexpr double kHeavyTruckLb = 40000.0;

// What the client has said of his own loads. Stated figures, not data: they are printed beside
// ours for the reader and never feed a decision.
constexpr long long kClientStatedUnusedAugust = 195;
constexpr double kClientStatedRearWasteIn = 14.5;
constexpr double kClientStatedCubicFeetLow = 3300.0;
constexpr double kClientStatedCubicFeetHigh = 3400.0;

SelectorSettings realDataSettings() {
    SelectorSettings settings = m4SyntheticDay::selectorSettings();
    settings.knapsack = {0.001, 0.001, 200'000};
    settings.patternPacker = {0.001, 0.001, 200, 200'000};
    settings.binPacker = {0.001, 0.001, 50, 100'000, 2'000, 100};
    settings.secondsPerLane = 10.0;
    return settings;
}

// One extract as M4 sees it: its run, reservation book, items, routes and plan.
struct HeldDay {
    string label;
    const PipelineResult* run = nullptr;
    ReservationBook book;
    SelectionItems items;
    LaneClassification classification;
    SelectionPlan plan;
};

HeldDay heldDay(const string& label, const PipelineResult& run) {
    HeldDay day;
    day.label = label;
    day.run = &run;
    day.book = buildReservationBook(run.placeholders.placeholders, run.demand.ctl,
                                    planDateFromRequestId(run.demand.request_id), {"53F", {"53F"}});
    day.items = buildSelectionItems(run, day.book.window);
    day.classification = classifyLanes(run, day.book, day.items, m4SyntheticDay::kRouting);
    day.plan = selectLoads(run, day.book, day.items, day.classification, realDataSettings());
    return day;
}

// Every extract on this machine: the 4-6 August runs first, then the 17 Aug and early September
// days, then 29 Sep - 5 Oct. Built once.
const vector<HeldDay>& heldDays() {
    static const vector<HeldDay> days = [] {
        vector<HeldDay> held;
        if (m4AugustTests::augustPresent()) {
            const array<string, 3> labels{"Aug4", "Aug5", "Aug6"};
            for (size_t run = 0; run < labels.size(); ++run) {
                held.push_back(heldDay(labels[run], m4AugustTests::augustItems(run).run));
            }
        }
        if (crossDayTests::allExtractsPresent()) {
            for (size_t day = 0; day < crossDayTests::kDayCount; ++day) {
                held.push_back(heldDay(crossDayTests::dayFiles()[day].label, crossDayTests::pipelineRuns()[day]));
            }
        }
        for (size_t day = 0; day < crossDayTests::newExtracts::kDayCount; ++day) {
            if (crossDayTests::newExtracts::dayPresent(day)) {
                held.push_back(heldDay(crossDayTests::newExtracts::labels[day], *crossDayTests::newExtracts::run(day)));
            }
        }
        return held;
    }();
    return days;
}

// For each August run, its routes sent to patternPacker or binPacker, each benchmarked with every
// method. Those are where the variety ratio decides; a knapsack route's demand runs far past its
// reservation, and packing all of it with every method says nothing about the crossover.
const array<vector<RouteBenchmark>, 3>& augustRouteBenchmarks() {
    static const array<vector<RouteBenchmark>, 3> benchmarks = [] {
        array<vector<RouteBenchmark>, 3> byRun;
        for (size_t run = 0; run < byRun.size(); ++run) {
            const HeldDay& day = heldDays()[run];
            for (size_t route = 0; route < day.classification.routes.size(); ++route) {
                const SolveMethod method = day.classification.routes[route].method;
                if (method != SolveMethod::PatternPacker && method != SolveMethod::BinPacker) continue;
                byRun[run].push_back(benchmarkRoute(*day.run, day.items, day.classification, route, realDataSettings()));
            }
        }
        return byRun;
    }();
    return benchmarks;
}

vector<RouteBenchmark> allAugustRouteBenchmarks() {
    vector<RouteBenchmark> all;
    for (const vector<RouteBenchmark>& run : augustRouteBenchmarks()) all.insert(all.end(), run.begin(), run.end());
    return all;
}

// Truck Builder's loads for one August run, by lane and ship date.
struct ClientDay {
    acceptanceTests::AchievedRequest achieved;
    vector<ClientLaneDay> laneDays;
    size_t shippedLines = 0;
    size_t shippedLinesInDemand = 0;  // the share his loads drew from this run's demand
};

const ClientDay& clientDay(size_t run) {
    static const array<ClientDay, 3> days = [] {
        array<ClientDay, 3> loaded;
        for (size_t index = 0; index < loaded.size(); ++index) {
            const PipelineResult& pipeline = *heldDays()[index].run;
            ClientDay& day = loaded[index];
            day.achieved = acceptanceTests::readAchievedLoads(m4AugustTests::kAugustSolutionDirectories[index],
                                                              pipeline.demand.request_id);
            for (const auto& laneDay : day.achieved.laneDays) {
                day.laneDays.push_back({laneDay.locationFrom, laneDay.locationTo, laneDay.shipCondition,
                                        laneDay.shipDate, static_cast<long long>(laneDay.loads)});
            }
            unordered_set<string> demandLines;
            for (const STRRecord& line : pipeline.demand.str) demandLines.insert(line.idpr);
            day.shippedLines = day.achieved.shippedByIdpr.size();
            for (const auto& shipped : day.achieved.shippedByIdpr) {
                if (demandLines.count(shipped.first)) ++day.shippedLinesInDemand;
            }
        }
        return loaded;
    }();
    return days[run];
}

// Each method's trucks and unit loads over every route on one of the lanes the client loaded
// most in a run: the spec's 38-truck lane on 5 Aug and 23- and 21-truck lanes on 6 Aug (E204,
// E205). Each route is solved alone, allowed its lane-date's whole reservation, so a lane split
// by segregation can total more trucks than the lane reserves; `reserved` counts each lane-date
// once.
struct LargestLane {
    string lane;
    long long clientLoads = 0;
    long long reserved = 0;
    long long allowed = 0;  // what the routes were each allowed, summed
    size_t routes = 0;
    vector<MethodScore> totals;  // parallel to benchmarkedMethods(), summed over the routes
};

// rank 0 is the lane the client loaded most, 1 the next.
LargestLane largestClientLane(size_t run, size_t rank) {
    const HeldDay& day = heldDays()[run];
    map<tuple<string, string, string>, long long> loadsByLane;
    for (const ClientLaneDay& laneDay : clientDay(run).laneDays) {
        loadsByLane[make_tuple(laneDay.origin, laneDay.destination, laneDay.shipCondition)] += laneDay.loads;
    }
    vector<pair<tuple<string, string, string>, long long>> lanes(loadsByLane.begin(), loadsByLane.end());
    stable_sort(lanes.begin(), lanes.end(), [](const auto& left, const auto& right) { return left.second > right.second; });
    const auto& busiest = lanes.at(rank);
    const string origin = get<0>(busiest.first), destination = get<1>(busiest.first), condition = get<2>(busiest.first);
    LargestLane largest;
    largest.lane = origin + " -> " + destination + " " + condition;
    largest.clientLoads = busiest.second;
    for (const Reservation& reservation : day.book.reservations) {
        if (reservation.origin == origin && reservation.destination == destination
            && reservation.shipCondition == condition) {
            largest.reserved += reservation.trucks;
        }
    }
    for (const PackingMethod method : benchmarkedMethods()) {
        MethodScore total;
        total.method = method;
        total.ran = true;
        largest.totals.push_back(total);
    }
    for (size_t route = 0; route < day.classification.routes.size(); ++route) {
        const LaneRoute& laneRoute = day.classification.routes[route];
        const GroupKey& key = day.run->segregation.groups[laneRoute.groupIndex].key;
        if (key.locationFrom != origin || key.locationTo != destination || key.shipCondition != condition) continue;
        if (laneRoute.itemIndices.empty() || laneRoute.reservedTrucks == 0) continue;
        ++largest.routes;
        largest.allowed += laneRoute.reservedTrucks;
        const RouteBenchmark benchmark = benchmarkRoute(*day.run, day.items, day.classification, route, realDataSettings());
        for (size_t method = 0; method < benchmark.scores.size(); ++method) {
            const MethodScore& score = benchmark.scores[method];
            MethodScore& total = largest.totals[method];
            total.ran = total.ran && score.ran;
            total.trucks += score.trucks;
            total.unitLoadsShipped += score.unitLoadsShipped;
            total.seconds += score.seconds;
        }
    }
    return largest;
}

string percentOf(size_t part, size_t whole) {
    return fixed(whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole), 1) + "%";
}

} // anonymous namespace

// Run with: ./build/ob_tests -ts=m4Benchmark --no-skip, or ctest (its m4Benchmark entry). Every
// test here is skipped in a plain ob_tests run: they take minutes over every extract, and a
// clean clone has none (E224). ctest runs them, and there a missing extract fails by name
// instead of the suite passing with nothing checked (E225).
TEST_SUITE("m4Benchmark") {

TEST_CASE("m4Benchmark: every extract the benchmark reads is present" * doctest::skip()) {
    CHECK(m4AugustTests::augustPresent());
    CHECK(m4AugustTests::augustSolutionsPresent());
    for (const string& missing : crossDayTests::missingExtracts()) FAIL_CHECK(missing);
    for (size_t day = 0; day < crossDayTests::newExtracts::kDayCount; ++day) {
        CHECK_MESSAGE(crossDayTests::newExtracts::dayPresent(day),
                      crossDayTests::newExtracts::directories[day] << " is not complete");
    }
}

TEST_CASE("m4Benchmark: every held extract keeps every invariant" * doctest::skip()) {
    for (const HeldDay& day : heldDays()) {
        CAPTURE(day.label);
        for (const InvariantViolation& violation :
             checkInvariants(*day.run, day.book, day.items, day.plan, kMaxUnitLoadsPerTruck)) {
            FAIL_CHECK(violation.invariant << ": " << violation.detail);
        }
    }
}

TEST_CASE("m4Benchmark: every held extract plans the same at one thread, again, and at four"
          * doctest::skip()) {
    SelectorSettings fourThreads = realDataSettings();
    fourThreads.threads = 4;
    for (const HeldDay& day : heldDays()) {
        CAPTURE(day.label);
        const SelectionPlan again = selectLoads(*day.run, day.book, day.items, day.classification, realDataSettings());
        CHECK(samePlan(day.plan, again));
        CHECK(planFingerprint(day.plan) == planFingerprint(again));
        CHECK(samePlan(day.plan, selectLoads(*day.run, day.book, day.items, day.classification, fourThreads)));
    }
}

TEST_CASE("m4Benchmark: the client's largest August lanes are run with every method" * doctest::skip()) {
    // --no-skip runs this without the files; the first three held days are August only with them.
    REQUIRE(m4AugustTests::augustPresent());
    REQUIRE(m4AugustTests::augustSolutionsPresent());
    // The spec's three: 38 loads in the 5 Aug run, 23 and 21 in the 6 Aug run.
    const vector<tuple<size_t, size_t, long long>> lanes{{1, 0, 38}, {2, 0, 23}, {2, 1, 21}};
    for (const auto& lane : lanes) {
        const size_t run = get<0>(lane), rank = get<1>(lane);
        const long long loads = get<2>(lane);
        CAPTURE(heldDays()[run].label);
        CAPTURE(rank);
        const LargestLane largest = largestClientLane(run, rank);
        CHECK(largest.clientLoads == loads);
        REQUIRE(largest.routes > 0);
        for (const MethodScore& total : largest.totals) {
            CHECK(total.ran);
            CHECK(total.trucks <= largest.allowed);
        }
    }
}

TEST_CASE("m4Benchmark: on the August routes the methods are ranked and the routing rules derived"
          * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    const vector<RouteBenchmark> benchmarks = allAugustRouteBenchmarks();
    REQUIRE_FALSE(benchmarks.empty());
    for (const RouteBenchmark& benchmark : benchmarks) {
        REQUIRE(benchmark.best);
        // The best ships at least as much as the method the route was sent to.
        const PackingMethod routed =
            benchmark.routedTo == SolveMethod::PatternPacker ? PackingMethod::PatternPacker : PackingMethod::BinPacker;
        const auto routedScore = find_if(benchmark.scores.begin(), benchmark.scores.end(),
                                         [&](const MethodScore& score) { return score.method == routed; });
        const auto bestScore = find_if(benchmark.scores.begin(), benchmark.scores.end(),
                                       [&](const MethodScore& score) { return score.method == *benchmark.best; });
        CHECK(bestScore->unitLoadsShipped >= routedScore->unitLoadsShipped - 1e-9);
    }
    // Measured 31 Oct 2026: the variety ratio barely separates patternPacker from binPacker. The
    // best split still sends more than a third of the routes it decides to the method that
    // ranked lower, so the ratio is not yet a routing rule (E206 - E208).
    const optional<VarietyCrossover> crossover = varietyCrossover(benchmarks);
    REQUIRE(crossover);
    CHECK(crossover->lanesMisrouted * 3 > crossover->lanesCompared);
    // No routed method came near the 10 s budget, so the data sets no M5 threshold yet (E209).
    CHECK_FALSE(smallestReservationOverBudget(benchmarks));
}

TEST_CASE("m4Benchmark: August is set beside the client's own loads, lane-day by lane-day" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    REQUIRE(m4AugustTests::augustSolutionsPresent());
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = heldDays()[run];
        CAPTURE(day.label);
        const ClientDay& client = clientDay(run);
        const ClientComparison comparison = compareWithClient(*day.run, day.book, day.plan, client.laneDays);
        CHECK(comparison.clientLoads == static_cast<long long>(client.achieved.totalLoads()));
        CHECK(comparison.ourTrucks == static_cast<long long>(day.plan.trucks.size()));
        CHECK(comparison.reserved == day.book.reservedTrucks);
        // Lanes left to M5 build nothing here, so the comparison counts their trucks unused;
        // the penalty leaves them to M5.
        long long leftToM5 = 0;
        for (const ReservationUse& use : day.plan.reservations) {
            if (use.escalated) leftToM5 += use.reserved - use.built;
        }
        CHECK(comparison.ourUnused == day.plan.penaltyExposureTrucks + leftToM5);
        // Measured 31 Oct 2026: every line his loads shipped is a line of this run's demand.
        // The spec expected about 41%, the rest autofill; these files show none.
        CHECK(client.shippedLinesInDemand == client.shippedLines);
    }
}

TEST_CASE("m4Benchmark: the benchmark report is written for the client" * doctest::skip()) {
    REQUIRE(m4AugustTests::augustPresent());
    REQUIRE(m4AugustTests::augustSolutionsPresent());
    ostringstream report;
    report << "M4 BENCHMARK REPORT\n\n"
           << "Every figure is ours, measured on the extracts held, unless it says it is the client's.\n";

    report << "\n1. Every extract: invariants, fullness and the plan's fingerprint\n\n";
    vector<vector<string>> dayRows;
    for (const HeldDay& day : heldDays()) {
        const FullnessSummary fullness = summariseFullness(*day.run, day.items, day.plan, kHeavyTruckLb);
        const vector<InvariantViolation> violations =
            checkInvariants(*day.run, day.book, day.items, day.plan, kMaxUnitLoadsPerTruck);
        ostringstream fingerprint;
        fingerprint << hex << planFingerprint(day.plan);
        dayRows.push_back({printable(day.label), grouped(static_cast<double>(day.plan.trucks.size())),
                           grouped(static_cast<double>(day.book.reservedTrucks)),
                           grouped(static_cast<double>(day.plan.penaltyExposureTrucks)),
                           fixed(fullness.averageRearWasteIn, 1), fixed(fullness.averageCube * 100.0, 1) + "%",
                           grouped(fullness.averageCubicFeet), grouped(fullness.averageWeightLb),
                           grouped(static_cast<double>(fullness.heavyLanes)),
                           violations.empty() ? string("none")
                                              : to_string(violations.size()) + " (" + violations.front().invariant + ")",
                           fingerprint.str()});
    }
    printColumns(report,
                 {"extract", "trucks", "reserved", "unused", "rear waste in", "cube", "cu ft", "lb",
                  "lanes over 40,000 lb", "invariants broken", "fingerprint"},
                 dayRows, {false, true, true, true, true, true, true, true, true, false, false});
    report << "\n  The client's stated figures: " << fixed(kClientStatedRearWasteIn, 1) << " in rear waste, "
           << grouped(kClientStatedCubicFeetLow) << " to " << grouped(kClientStatedCubicFeetHigh)
           << " cu ft a truck (E220, E221).\n"
           << "  The fingerprint is the same on every platform for the same plan: compare it across builds (E223).\n"
           << "  Invariants broken: I6 counts trucks over " << fixed(kMaxUnitLoadsPerTruck, 0)
           << " unit loads; on these extracts they are three-high PAL stacks, a rule still open.\n";

    report << "\n2. August against the client's own loads (E218, E219)\n";
    long long ourUnused = 0, clientUnused = 0;
    for (size_t run = 0; run < 3; ++run) {
        const HeldDay& day = heldDays()[run];
        const ClientDay& client = clientDay(run);
        const ClientComparison comparison = compareWithClient(*day.run, day.book, day.plan, client.laneDays);
        ourUnused += comparison.ourUnused;
        clientUnused += comparison.clientUnused;
        long long leftToM5 = 0;
        for (const ReservationUse& use : day.plan.reservations) {
            if (use.escalated) leftToM5 += use.reserved - use.built;
        }
        report << "\n" << day.label << ": " << percentOf(client.shippedLinesInDemand, client.shippedLines)
               << " of the lines his loads shipped are lines of this run's demand. " << leftToM5
               << " of our unused trucks are on lanes left to M5.\n";
        printClientComparison(report, comparison);
    }
    report << "\n  Reserved trucks left unused over the three runs, inside each run's window: client "
           << clientUnused << ", ours " << ourUnused << ". The client's stated figure over all his reservations: "
           << kClientStatedUnusedAugust << ".\n";

    report << "\n3. The client's largest August lanes, every method (E204, E205)\n\n";
    vector<vector<string>> laneRows;
    for (const auto& [run, rank] : vector<pair<size_t, size_t>>{{0, 0}, {1, 0}, {2, 0}, {2, 1}}) {
        const LargestLane largest = largestClientLane(run, rank);
        vector<string> row{heldDays()[run].label, printable(largest.lane), to_string(largest.clientLoads),
                           to_string(largest.reserved), to_string(largest.routes)};
        for (const MethodScore& total : largest.totals) {
            row.push_back(to_string(total.trucks) + " / " + grouped(total.unitLoadsShipped, 1));
        }
        laneRows.push_back(row);
    }
    vector<string> laneHeader{"run", "lane", "client loads", "we reserve", "routes"};
    for (const PackingMethod method : benchmarkedMethods()) laneHeader.push_back(packingMethodName(method));
    printColumns(report, laneHeader, laneRows, {false, false, true, true, true, false, false, false, false});
    report << "  Each method: trucks / unit loads shipped, over every ship date of the lane. Each group and\n"
           << "  ship date is solved alone with its lane-date's whole reservation, so a lane split by\n"
           << "  segregation can total more trucks than it reserves; the selector shares one reservation.\n";

    report << "\n4. The methods on every August route sent to patternPacker or binPacker (E206 - E209)\n\n";
    const vector<RouteBenchmark> benchmarks = allAugustRouteBenchmarks();
    map<string, size_t> wins;
    for (const RouteBenchmark& benchmark : benchmarks) {
        ++wins[string(solveMethodName(benchmark.routedTo)) + " routes won by " + packingMethodName(*benchmark.best)];
    }
    for (const auto& [label, routes] : wins) report << "  " << label << ": " << routes << "\n";
    const optional<VarietyCrossover> crossover = varietyCrossover(benchmarks);
    if (crossover) {
        report << "  Best variety-ratio split: " << fixed(crossover->ratio, 1) << " unit loads per product; it misroutes "
               << crossover->lanesMisrouted << " of the " << crossover->lanesCompared
               << " routes where patternPacker and binPacker differ.\n";
    }
    const optional<long long> overBudget = smallestReservationOverBudget(benchmarks);
    report << "  Smallest reservation whose routed method missed the " << fixed(realDataSettings().secondsPerLane, 0)
           << " s budget: " << (overBudget ? to_string(*overBudget) : string("none did")) << ".\n";
    for (size_t run = 0; run < 3; ++run) {
        report << "\n" << heldDays()[run].label << "\n";
        printBenchmark(report, *heldDays()[run].run, heldDays()[run].classification, augustRouteBenchmarks()[run]);
    }

    const string text = report.str();
    CHECK(all_of(text.begin(), text.end(), [](char character) {
        const auto code = static_cast<unsigned char>(character);
        return character == '\n' || (code >= 0x20 && code <= 0x7e);
    }));
    for (const char* section : {"1. Every extract", "2. August against", "3. The client's largest", "4. The methods"}) {
        CHECK(text.find(section) != string::npos);
    }
    ofstream(OB_M4_BENCHMARK_REPORT) << text;
    MESSAGE("benchmark report written to " << OB_M4_BENCHMARK_REPORT);
}

} // TEST_SUITE
