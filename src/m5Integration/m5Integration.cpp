#include "m5Integration.hpp"
#include "m5Orchestrator.hpp"
#include "m5Report.hpp"
#include "reportFormat.hpp"
#include "pipeline.hpp"
#include "selectionParams.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <stdexcept>

using namespace std;

namespace ob {
namespace m5 {

namespace {

string groupLabel(const PipelineResult& run, size_t groupIndex) {
    const GroupKey& key = run.segregation.groups[groupIndex].key;
    return "group " + to_string(groupIndex) + (key.isSegregated ? " (" + key.segregant + ")" : "");
}

// Each level's share of its line: the item's own record, or for an item built by hand, a whole unit
// load per level until the item's unit loads run out, as buildSelectionItems deals them.
vector<double> levelShares(const SelectionItem& item) {
    if (item.lineUnitLoads.size() == item.lineIndices.size()) return item.lineUnitLoads;
    vector<double> shares;
    double left = item.unitLoads;
    for (size_t level = 0; level < item.lineIndices.size(); ++level) {
        const double share = level + 1 == item.lineIndices.size() ? max(0.0, left) : min(1.0, max(0.0, left));
        shares.push_back(share);
        left -= share;
    }
    return shares;
}

Atom atomOf(const SelectionItem& item, size_t itemIndex, const PipelineResult& run, const string& departure) {
    Atom atom;
    atom.id = to_string(itemIndex);
    atom.group = groupLabel(run, item.groupIndex);
    atom.floor = floorItemFrom(item);
    atom.weightLb = item.weightLb;
    atom.unitLoads = item.unitLoads;
    const vector<double> shares = levelShares(item);
    // Levels of one line in one stack are one component: the line is the identity, not the level.
    map<size_t, size_t> componentOfLine;
    for (size_t level = 0; level < item.lineIndices.size(); ++level) {
        if (shares[level] <= 0.0) continue;
        const size_t lineIndex = item.lineIndices[level];
        const auto known = componentOfLine.find(lineIndex);
        if (known != componentOfLine.end()) {
            atom.components[known->second].unitLoads += shares[level];
            continue;
        }
        Component component;
        component.source = "line " + to_string(lineIndex);
        component.unitLoads = shares[level];
        // A line read from the demand file carries its own dates and priority; one built by hand
        // in a test carries none, and takes the item's.
        const STRRecord* line = lineIndex < run.join.lines.size() ? run.join.lines[lineIndex].str : nullptr;
        if (line && !line->datfr_ta.empty()) {
            component.product = line->matnr;
            component.dueDate = line->datfr_ta;
            component.availableDate = line->avail_date;
            component.priority = urgencyRank(line->tprio, run.selectionParams.priorityZeroIsMostUrgent);
        } else {
            component.product = line ? line->matnr : item.productNumber;
            component.dueDate = item.shipDate;
            component.availableDate = item.availDate;
            component.priority = item.priority;
        }
        component.offerKind = component.dueDate > departure ? OfferKind::CanGo : OfferKind::Required;
        componentOfLine.emplace(lineIndex, atom.components.size());
        atom.components.push_back(std::move(component));
    }
    return atom;
}

PackingMethod packingMethodOf(MethodId method) {
    return method == MethodId::RollingBlocks ? PackingMethod::M5RollingBlocks : PackingMethod::M5PatternRepeat;
}

LeftOverReason reasonOf(LeftOverCause cause) {
    switch (cause) {
    case LeftOverCause::NotEligible: return LeftOverReason::UnavailableInWindow;
    case LeftOverCause::Unfittable: return LeftOverReason::Unfittable;
    case LeftOverCause::OverReservation: return LeftOverReason::OverReservation;
    case LeftOverCause::AcceptanceWithheld: return LeftOverReason::AcceptanceWithheld;
    case LeftOverCause::NoResult: return LeftOverReason::M5NoResult;
    case LeftOverCause::ConfigurationIncomplete: return LeftOverReason::M5ConfigurationIncomplete;
    case LeftOverCause::InvalidInput: return LeftOverReason::M5InvalidInput;
    }
    return LeftOverReason::M5NoResult;
}

// A lane-date M5 could not plan at all: every offered item left over with `reason`.
LargeLaneResult unplanned(const LargeLaneRequest& request, LeftOverReason reason, LargeLaneReport report) {
    LargeLaneResult result;
    for (size_t route = 0; route < request.routeIndices.size(); ++route) {
        RouteOutcome outcome;
        outcome.routeIndex = request.routeIndices[route];
        outcome.status = RouteStatus::Escalated;
        outcome.trucksAllowed = request.reservedTrucks;
        result.routes.push_back(std::move(outcome));
        for (const size_t index : request.itemIndicesByRoute[route]) result.leftOver.push_back({index, reason});
    }
    report.planned = false;
    result.report = std::move(report);
    return result;
}

LargeLaneReport reportShell(const LargeLaneRequest& request) {
    LargeLaneReport report;
    report.origin = request.origin;
    report.destination = request.destination;
    report.shipCondition = request.shipCondition;
    report.shipDate = request.shipDate;
    return report;
}

}  // namespace

M5LaneDateInput gatherLaneDate(const LargeLaneRequest& request, const PipelineResult& run, const SelectionItems& items,
                               const DateRules& dates, const optional<double>& penaltyPerUnusedTruck) {
    M5LaneDateInput input;
    input.lane = {request.origin, request.destination, request.shipCondition};
    input.departure = request.shipDate;
    input.dates = dates;
    input.pastDueOrder = request.pastDueOrder;
    set<size_t> offered;
    set<size_t> groups(request.groupIndices.begin(), request.groupIndices.end());
    for (const vector<size_t>& itemIndices : request.itemIndicesByRoute) {
        for (const size_t index : itemIndices) {
            input.atoms.push_back(atomOf(items.items[index], index, run, request.shipDate));
            offered.insert(index);
        }
    }
    // The declared scope: the groups' goods not offered here because their dates rule them out.
    for (size_t index = 0; index < items.items.size(); ++index) {
        const SelectionItem& item = items.items[index];
        if (offered.count(index) || !groups.count(item.groupIndex)) continue;
        string reason;
        if (item.availDate > request.shipDate) {
            reason = "not available by the departure";
        } else if (item.shipDate > request.shipDate) {
            reason = "due after the departure and not offered for it";
        } else {
            continue;  // due by the departure: planned on an earlier date, or left there
        }
        input.excluded.push_back({"item " + to_string(index), groupLabel(run, item.groupIndex), item.unitLoads, reason});
    }
    input.sourceScopeComplete = true;
    const TrailerSpec& trailer = run.trailer;
    const TrailerCapability capability{trailer.interiorLengthIn, trailer.interiorWidthIn, trailer.stackHeightCeilingIn,
                                       trailer.weightLimitLb};
    input.pool.slots.assign(static_cast<size_t>(max(0LL, request.reservedTrucks)), capability);
    input.pool.penaltyPerUnusedTruck = penaltyPerUnusedTruck;
    input.sourceVersion = run.demand.request_id + "#" + request.origin + "-" + request.destination + "-" + request.shipCondition + "@"
                          + request.shipDate;
    return input;
}

PhysicalSettings physicalSettingsOf(const SelectorSettings& settings) {
    PhysicalSettings physical;
    physical.floor = settings.floor;
    if (settings.maxUnitLoadsPerTruck > 0.0) physical.maxUnitLoadsPerTruck = settings.maxUnitLoadsPerTruck;
    physical.searchStatesPerPackingCheck = min(physical.searchStatesPerPackingCheck, settings.floor.searchStateLimit);
    physical.binPacker = settings.binPacker;
    return physical;
}

M5LargeLanePlanner::M5LargeLanePlanner(optional<M5PolicyProfile> profile, string configurationError, DateRules dates,
                                       int routingThreshold)
    : profile_(std::move(profile)), configurationError_(std::move(configurationError)), dates_(dates),
      routingThreshold_(routingThreshold) {}

LargeLaneResult M5LargeLanePlanner::plan(const LargeLaneRequest& request, const PipelineResult& run, const SelectionItems& items,
                                         const SelectorSettings& settings) const {
    LargeLaneReport report = reportShell(request);
    try {
        const M5LaneDateInput input =
            gatherLaneDate(request, run, items, dates_, profile_ ? profile_->penaltyPerUnusedTruck : nullopt);
        const PhysicalSettings physical = physicalSettingsOf(settings);
        const FunctionClock clock(settings.clock);
        const M5RunServices services{clock, nullopt, nullopt, {}, {}};
        M5LaneOutcome outcome = profile_ ? planLaneDate(input, *profile_, physical, services)
                                         : configurationIncomplete(input, configurationError_);

        // The selected delta is applied once, atomically; anything else is no plan at all.
        if (outcome.selected && outcome.delta) {
            const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
            OperationalState state;
            state.snapshot = snapshot.id;
            state.reservationVersion = snapshot.input.pool.version;
            state.slotsReserved = snapshot.input.pool.slots.size();
            const ApplyStatus status = ledger_.applyIfCurrent(*outcome.delta, snapshot, state);
            if (status != ApplyStatus::Applied && status != ApplyStatus::AlreadyApplied) {
                throw logic_error(string("M5 could not commit its selected plan: ") + applyStatusName(status));
            }
        }

        M5ReportContext context;
        context.routingThreshold = routingThreshold_;
        context.runIdentity = run.demand.request_id;
        report.outcome = outcomeKindName(outcome.kind);
        report.text = renderAscii(buildReportData(outcome, input, profile_ ? &*profile_ : nullptr, context));
        report.planned = outcome.kind != OutcomeKind::InvalidInput && outcome.kind != OutcomeKind::ConfigurationIncomplete
                         && outcome.kind != OutcomeKind::TimeoutNoIncumbent;

        // Atoms were gathered route by route, so atom positions map back to items and routes.
        vector<size_t> itemOfAtom, routeOfAtom;
        for (size_t route = 0; route < request.itemIndicesByRoute.size(); ++route) {
            for (const size_t index : request.itemIndicesByRoute[route]) {
                itemOfAtom.push_back(index);
                routeOfAtom.push_back(route);
            }
        }
        LargeLaneResult result;
        vector<long long> builtOfRoute(request.routeIndices.size(), 0);
        if (outcome.selected) {
            const TrailerSpec& trailer = run.trailer;
            for (const TruckPlan& truck : outcome.selected->trucks) {
                PlannedTruck planned;
                const size_t route = routeOfAtom[truck.atoms.front()];
                planned.routeIndex = request.routeIndices[route];
                planned.groupIndex = request.groupIndices[route];
                planned.shipDate = request.shipDate;
                for (const size_t atom : truck.atoms) planned.items.push_back(itemOfAtom[atom]);
                sort(planned.items.begin(), planned.items.end());
                vector<FloorItem> floorItems;
                for (const size_t index : planned.items) {
                    const SelectionItem& item = items.items[index];
                    planned.areaSqIn += item.footprintLongIn * item.footprintShortIn;
                    planned.weightLb += item.weightLb;
                    planned.unitLoads += item.unitLoads;
                    floorItems.push_back(floorItemFrom(item));
                }
                planned.weightFull = planned.weightLb >= trailer.weightLimitLb - kQuantityEpsilon;
                planned.floor = arrangeFloor(floorItems, trailer, settings.floor);
                ++builtOfRoute[route];
                result.trucks.push_back(std::move(planned));
            }
        }
        for (const LeftOver& left : outcome.leftOver) result.leftOver.push_back({itemOfAtom[left.atom], reasonOf(left.cause)});
        sort(result.leftOver.begin(), result.leftOver.end(),
             [](const LeftOverItem& left, const LeftOverItem& right) { return left.itemIndex < right.itemIndex; });

        const MethodRecord* selectedMethod = nullptr;
        for (const MethodRecord& method : outcome.methods) {
            if (method.selected) selectedMethod = &method;
        }
        for (size_t route = 0; route < request.routeIndices.size(); ++route) {
            RouteOutcome routeOutcome;
            routeOutcome.routeIndex = request.routeIndices[route];
            routeOutcome.status = report.planned ? RouteStatus::Solved : RouteStatus::Escalated;
            routeOutcome.trucksAllowed = request.reservedTrucks;
            routeOutcome.trucksBuilt = builtOfRoute[route];
            for (const MethodRecord& method : outcome.methods) {
                if (method.ran) routeOutcome.attempts.push_back({packingMethodOf(method.method), AttemptOutcome::Used, method.seconds, ""});
            }
            if (selectedMethod) routeOutcome.methodUsed = packingMethodOf(selectedMethod->method);
            routeOutcome.proven = report.planned && outcome.proof == ProofStatus::ProvenOptimal;
            if (report.planned && !routeOutcome.proven) {
                routeOutcome.unprovenBecause = "M5 " + string(outcomeKindName(outcome.kind)) + ": " + outcome.proofScope;
            }
            result.routes.push_back(std::move(routeOutcome));
        }
        result.reservationsUsed = static_cast<long long>(result.trucks.size());
        result.report = std::move(report);
        return result;
    } catch (const exception& error) {
        report.outcome = "no result: " + string(error.what());
        report.text = "M5 large lane " + request.origin + " -> " + request.destination + " " + request.shipCondition + ", "
                      + request.shipDate + "\n  no result: " + printable(error.what()) + "\n";
        return unplanned(request, LeftOverReason::M5NoResult, std::move(report));
    }
}

shared_ptr<const LargeLanePlanner> makeLargeLanePlanner(const nlohmann::json& root, const SelectionParams& selection) {
    if (!m5Enabled(root)) return nullptr;
    optional<M5PolicyProfile> profile;
    string error;
    try {
        profile = loadProfile(root.at("m5"));
        profile->order = selection.routing.pastDueOrder;
    } catch (const runtime_error& failure) {
        error = failure.what();
    }
    DateRules dates;
    dates.pullForward = selection.shipDates.pullForward;
    dates.maxDaysPulledForward = selection.maxDaysPulledForward;
    return make_shared<M5LargeLanePlanner>(std::move(profile), std::move(error), dates, selection.routing.m5Threshold);
}

} // namespace m5
} // namespace ob
