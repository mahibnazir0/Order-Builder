#pragma once
// ============================================================================
// m5Integration.hpp - M5 as the selector's large-lane planner.
//
// gatherLaneDate turns the selector's request into M5's input (M5 handover
// P3): each offered SelectionItem is an atom, each of its lines a component
// with that line's own dates and priority; goods due after the departure are
// can-go offers (P10). The declared source scope adds the lane-date's groups'
// goods that were not offered because they are due after the pull-forward
// window or not yet available, so fullness buckets can tell "no goods
// remain" from "only rule-excluded goods remain".
//
// The planner runs planLaneDate, applies the selected delta through its
// CommitLedger, and maps the outcome back: trucks, one RouteOutcome per route
// and every item not loaded with its reason (ED-15). Nothing thrown inside M5
// crosses the selector's thread: it becomes M5NoResult on every item
// (architecture 7.1, INV-15).
// ============================================================================

#include "json.hpp"
#include "largeLanePlanner.hpp"
#include "m5Budget.hpp"
#include "m5Commit.hpp"
#include "m5Orchestrator.hpp"
#include "m5Policy.hpp"
#include "m5Physical.hpp"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ob {

struct SelectionParams;

namespace m5 {

// Every reserved slot gets the run's trailer and the unit-load cap `maxUnitLoadsPerTruck` (the validated
// M4 setting, replicated onto the slots until the reservation feed carries per-slot caps, C9). A value
// that is not a whole number from 1 leaves the slots with no cap, which planLaneDate refuses before planning.
// With a trailer-type catalogue, each slot is its reservation's equipment type from it (an unknown code
// leaves a slot with no facts, which planning refuses by name); without one, every slot is the run's
// trailer with maxUnitLoadsPerTruck.
M5LaneDateInput gatherLaneDate(const LargeLaneRequest& request, const PipelineResult& run, const SelectionItems& items,
                               const DateRules& dates, const std::optional<double>& penaltyPerUnusedTruck,
                               double maxUnitLoadsPerTruck, const std::map<std::string, TrailerCapability>& trailerTypes = {},
                               const std::map<std::string, std::set<std::string>>& separateBuildings = {});

PhysicalSettings physicalSettingsOf(const SelectorSettings& settings);

// A lane-date gathered as the selector would gather it, whatever its routing: the benchmark's
// forced route (v7 0.4). It never changes a reservation. Empty when the lane has no route that day.
std::optional<LargeLaneRequest> forcedRequest(const PipelineResult& run, const LaneClassification& classification,
                                              const LaneKey& lane, const std::string& shipDate);

// The labelled linked-date benchmark (ACC-2b, RD-06): each date planned by M5 in date order with
// its own pool, goods it leaves over the reservation carried to the next date, goods it ships
// removed from later dates. Benchmark only: it commits nothing and its rows never enter
// production totals (X-REPORT-02).
struct LinkedDateBenchmark {
    std::vector<M5LaneDateInput> inputs;   // as each date was planned, after carry
    std::vector<M5LaneOutcome> outcomes;   // parallel
    std::string text;                      // plain ASCII, labelled BENCHMARK
};
LinkedDateBenchmark benchmarkLinkedDates(std::vector<M5LaneDateInput> datesInOrder, const M5PolicyProfile& profile,
                                         const PhysicalSettings& physical, const Clock& clock);

class M5LargeLanePlanner final : public LargeLanePlanner {
public:
    // `profile` empty with `configurationError` set plans nothing: every M5 lane-date is reported
    // configuration incomplete, its goods left over with that reason.
    M5LargeLanePlanner(std::optional<M5PolicyProfile> profile, std::string configurationError, DateRules dates,
                       int routingThreshold);

    LargeLaneResult plan(const LargeLaneRequest& request, const PipelineResult& run, const SelectionItems& items,
                         const SelectorSettings& settings) const override;

private:
    std::optional<M5PolicyProfile> profile_;
    std::string configurationError_;
    DateRules dates_;
    int routingThreshold_ = 0;
    // Shared by the worker threads; it locks itself.
    mutable CommitLedger ledger_;
};

// The planner a run registers, from the params file's root: empty when there is no "m5" block or
// it is not enabled. Throws std::runtime_error for an "m5" block whose "enabled" is malformed; any
// other malformed M5 setting yields a planner that reports it on every M5 lane-date.
std::shared_ptr<const LargeLanePlanner> makeLargeLanePlanner(const nlohmann::json& root, const SelectionParams& selection);

} // namespace m5
} // namespace ob
