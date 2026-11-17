#include "m5Report.hpp"
#include "reportFormat.hpp"

#include <sstream>

using namespace std;

namespace ob {
namespace m5 {

namespace {

string orNotAvailable(const optional<double>& value, int places) { return value ? fixed(*value, places) : "N/A"; }

string reasonsOf(const FullnessLabel& label) {
    string reasons;
    auto add = [&](bool on, const char* reason) {
        if (on) reasons += (reasons.empty() ? "" : ", ") + string(reason);
    };
    add(label.byWeight, "weight");
    add(label.byRearInches, "rear inches");
    add(label.byFloorLimit, "floor-limited");
    return reasons.empty() ? "-" : reasons;
}

}  // namespace

map<string, BucketAverage> rearInchesByBucket(const vector<FullnessLabel>& labels) {
    map<string, BucketAverage> buckets;
    map<string, double> totals;
    for (const FullnessBucket bucket : {FullnessBucket::GoodsRemain, FullnessBucket::NoGoodsRemain, FullnessBucket::RuleLimited}) {
        buckets[fullnessBucketName(bucket)];
    }
    for (const FullnessLabel& label : labels) {
        if (!label.bucket) continue;
        const string name = fullnessBucketName(*label.bucket);
        ++buckets[name].trucks;
        totals[name] += label.rearInches;
    }
    for (auto& [name, bucket] : buckets) {
        if (bucket.trucks > 0) bucket.averageRearInches = totals[name] / static_cast<double>(bucket.trucks);
    }
    return buckets;
}

M5ReportData buildReportData(const M5LaneOutcome& outcome, const M5LaneDateInput& input, const M5PolicyProfile* profile,
                             const M5ReportContext& context) {
    M5ReportData data;
    data.lane = input.lane.text();
    data.departure = input.departure;
    data.scope = context.scope;
    data.runIdentity = context.runIdentity;
    data.sourceVersion = input.sourceVersion;
    data.reservedTrucks = outcome.reservedTrucks;
    data.allowanceSeconds = outcome.allowanceSeconds;
    data.routing = "reserved trucks per lane and ship date > " + to_string(context.routingThreshold);
    data.pastDueOrder = pastDueOrderName(input.pastDueOrder);
    data.canGoWindow = input.dates.pullForward ? "goods due up to " + to_string(input.dates.maxDaysPulledForward)
                                                     + " days after the date, available by it, after every good due by it"
                                               : "off (pull-forward off)";
    if (profile) {
        data.profileKey = profile->key();
        data.secondsPerLoad = profile->secondsPerLoad;
        data.minimumLoad = profile->acceptance
                               ? "optional policy ON: weight > " + fixed(profile->acceptance->weightLb, 1) + " lb OR cube > "
                                     + fixed(profile->acceptance->cubeCuFt, 1) + " cu ft (strict)"
                               : "at export, after M5";
        data.fullnessRule = fixed(profile->fullness.weightFullPercent, 1) + "% of the trailer's weight, or rear inches <= "
                            + fixed(profile->fullness.rearInchLimit, 1)
                            + (profile->fullness.rearInchLimitDefaulted ? " (default, stated to Tom)" : " (user setting)")
                            + ", or floor-limited on evidence";
        data.methodPolicy = methodPolicyName(profile->method);
        data.penalty = profile->penaltyPerUnusedTruck ? fixed(*profile->penaltyPerUnusedTruck, 2) + " per unused truck" : "unknown";
    } else {
        data.minimumLoad = data.fullnessRule = data.methodPolicy = data.penalty = "not read: M5 settings incomplete";
    }
    for (const Atom& atom : input.atoms) {
        if (find(data.groups.begin(), data.groups.end(), atom.group) == data.groups.end()) data.groups.push_back(atom.group);
        data.unitLoadsIn += atom.unitLoads;
    }

    map<size_t, const FullnessLabel*> labelOf;
    for (const FullnessLabel& label : outcome.fullness) labelOf[label.truck] = &label;
    if (outcome.selected) {
        const vector<TruckPlan>& trucks = outcome.selected->trucks;
        for (size_t truck = 0; truck < trucks.size(); ++truck) {
            TruckRow row;
            row.truck = truck + 1;
            row.group = trucks[truck].group;
            row.stacks = trucks[truck].atoms.size();
            for (const size_t index : trucks[truck].atoms) {
                const Atom& atom = input.atoms[index];
                row.unitLoads += atom.unitLoads;
                row.weightLb += atom.weightLb;
                for (const Component& component : atom.components) {
                    if (component.offerKind == OfferKind::CanGo) row.canGoUnitLoads += component.unitLoads;
                }
            }
            data.unitLoadsShipped += row.unitLoads;
            data.canGoUnitLoadsShipped += row.canGoUnitLoads;
            const auto found = labelOf.find(truck);
            if (found != labelOf.end()) {
                const FullnessLabel& label = *found->second;
                row.weightPercent = label.weightPercent;
                row.rearInches = label.rearInches;
                row.fullness = fullnessStateName(label.state);
                row.reasons = reasonsOf(label);
                row.fit = remainingFitName(label.fitStatus);
                row.bucket = label.bucket ? fullnessBucketName(*label.bucket) : "not established (source scope incomplete)";
                if (label.state == FullnessState::Full) ++data.fullTrucks;
                data.fullByWeight += label.byWeight ? 1 : 0;
                data.fullByRear += label.byRearInches ? 1 : 0;
                data.fullByFloor += label.byFloorLimit ? 1 : 0;
            }
            data.trucks.push_back(std::move(row));
        }
        const SolverEvidence& evidence = outcome.selected->evidence;
        data.countEvidence = "incumbent " + (evidence.incumbent ? to_string(*evidence.incumbent) : string("N/A")) + ", bound "
                             + (evidence.lowerBound ? to_string(*evidence.lowerBound) : string("N/A")) + " ("
                             + (evidence.boundModel.empty() ? string("no model") : evidence.boundModel) + "), "
                             + proofStatusName(evidence.proof);
    }
    for (const LeftOver& left : outcome.leftOver) {
        if (left.atom < input.atoms.size()) data.unitLoadsLeftByCause[leftOverCauseName(left.cause)] += input.atoms[left.atom].unitLoads;
    }
    data.loadedTrucks = data.trucks.size();
    data.unusedReservations = outcome.reservedTrucks - static_cast<long long>(data.loadedTrucks);
    if (profile && profile->penaltyPerUnusedTruck) {
        data.penaltyAmount = static_cast<double>(data.unusedReservations) * *profile->penaltyPerUnusedTruck;
    }
    data.methods = outcome.methods;
    data.escalation = outcome.escalation;
    data.proof = proofStatusName(outcome.proof);
    data.proofScope = outcome.proofScope;
    data.comparator = outcome.comparator;
    data.floorSearch = outcome.floorSearch;
    data.outcome = outcomeKindName(outcome.kind);
    data.stop = stopReasonName(outcome.stop);
    data.elapsedSeconds = outcome.timing.elapsedSeconds;
    data.secondsByPhase = outcome.timing.secondsByPhase;
    if (data.loadedTrucks > 0) data.secondsPerLoadedTruck = data.elapsedSeconds / static_cast<double>(data.loadedTrucks);
    data.rearInchesByBucket = rearInchesByBucket(outcome.fullness);
    data.findings = outcome.findings;
    data.openItems = {"OPEN-COMP: equal-service fill and truck-count tie operator not frozen; ties broken by canonical membership",
                      "C9: per-truck trailer figures absent from the reservation feed; every slot uses the run's trailer"};
    return data;
}

optional<string> unusedComparisonMismatch(long long reserved, long long built, long long statedUnused) {
    if (reserved - built == statedUnused) return nullopt;
    return to_string(reserved) + " reserved - " + to_string(built) + " built = " + to_string(reserved - built) + ", not "
           + to_string(statedUnused) + ": the figures are not on one basis";
}

string renderAscii(const M5ReportData& data) {
    ostringstream out;
    auto line = [&](const string& label, const string& value) { out << "  " << label << ": " << printable(value) << '\n'; };
    out << "M5 large lane " << printable(data.lane) << ", " << printable(data.departure) << '\n';

    out << "3A Run\n";
    line("scope", data.scope);
    line("run", data.runIdentity.empty() ? "-" : data.runIdentity);
    line("hand-over", data.sourceVersion.empty() ? "-" : data.sourceVersion);
    line("profile", data.profileKey.empty() ? "-" : data.profileKey);
    line("minimum-load check", data.minimumLoad);
    line("fullness rule", data.fullnessRule);
    line("routing", data.routing);
    line("past-due order", data.pastDueOrder);
    line("can-go fill", data.canGoWindow);
    line("method policy", data.methodPolicy);
    line("seconds per load", fixed(data.secondsPerLoad, 2));
    line("lane-date allowance", fixed(data.allowanceSeconds, 2) + " s (" + to_string(data.reservedTrucks) + " reserved)");

    out << "3B Lane\n";
    string groups;
    for (const string& group : data.groups) groups += (groups.empty() ? "" : ", ") + group;
    line("groups sharing the reservation", groups.empty() ? "-" : groups);
    line("reserved", to_string(data.reservedTrucks));
    line("trucks loaded", to_string(data.loadedTrucks));
    line("unused reservations", to_string(data.unusedReservations) + ", penalty " + data.penalty
                                    + (data.penaltyAmount ? " = " + fixed(*data.penaltyAmount, 2) : string()));
    vector<vector<string>> rows;
    for (const TruckRow& truck : data.trucks) {
        rows.push_back({to_string(truck.truck), printable(truck.group), to_string(truck.stacks), fixed(truck.unitLoads, 2),
                        fixed(truck.canGoUnitLoads, 2), fixed(truck.weightPercent, 1), fixed(truck.rearInches, 1), truck.fullness,
                        truck.reasons, truck.fit, truck.bucket});
    }
    if (!rows.empty()) {
        printColumns(out, {"truck", "group", "stacks", "unit loads", "can-go", "weight %", "rear in", "fullness", "reasons", "fit check", "bucket"},
                     rows, {true, false, true, true, true, true, true, false, false, false, false});
    }
    for (const auto& [cause, unitLoads] : data.unitLoadsLeftByCause) line("left: " + cause, fixed(unitLoads, 2) + " unit loads");

    out << "3C Method evidence\n";
    for (const MethodRecord& method : data.methods) {
        string status = method.ran ? (method.validated ? "ran, validated" : "ran, failed validation") : "not run: " + method.skippedBecause;
        if (method.selected) status += ", selected";
        line(string("method ") + methodName(method.method),
             status + "; " + to_string(method.trucks) + " trucks, " + fixed(method.unitLoads, 2) + " unit loads, "
                 + fixed(method.seconds, 2) + " s");
        for (const string& finding : method.findings) line("  finding", finding);
    }
    line("quick count vs bound", data.escalation.gap.empty() ? "N/A" : data.escalation.gap);
    line("careful method", (data.escalation.escalated ? "run: " : "not escalated: ") + data.escalation.reason);
    line("count evidence", data.countEvidence.empty() ? "N/A" : data.countEvidence);
    line("service proof", data.proof + (data.proofScope.empty() ? "" : " - " + data.proofScope));
    line("comparator", data.comparator);
    const FloorSearchRecord& search = data.floorSearch;
    line("floor search states", to_string(search.statesCharged) + " of the route's " + to_string(search.routeStates) + " (at most "
                                    + to_string(search.perCallStates) + " a search); Undecided stops: call limit "
                                    + to_string(search.callLimitStops) + ", route limit " + to_string(search.routeLimitStops)
                                    + ", wall deadline " + to_string(search.wallDeadlineStops));

    out << "3D Threshold benchmark\n";
    line("rows", "none in a production run: benchmark runs are labelled and kept apart");

    out << "3E Summary\n";
    line("outcome", data.outcome + " (" + data.stop + ")");
    line("unit loads in / shipped / of them can-go", fixed(data.unitLoadsIn, 2) + " / " + fixed(data.unitLoadsShipped, 2) + " / "
                                                         + fixed(data.canGoUnitLoadsShipped, 2));
    line("FULL trucks", to_string(data.fullTrucks) + " of " + to_string(data.loadedTrucks) + " (by weight " + to_string(data.fullByWeight)
                            + ", by rear inches " + to_string(data.fullByRear) + ", floor-limited " + to_string(data.fullByFloor) + ")");
    for (const auto& [bucket, average] : data.rearInchesByBucket) {
        line("average rear inches, " + bucket, orNotAvailable(average.averageRearInches, 1) + " over " + to_string(average.trucks) + " trucks");
    }
    line("time", fixed(data.elapsedSeconds, 3) + " s of " + fixed(data.allowanceSeconds, 2) + " s allowed");
    for (const auto& [phase, seconds] : data.secondsByPhase) line("  " + phase, fixed(seconds, 3) + " s");
    line("seconds per loaded truck", orNotAvailable(data.secondsPerLoadedTruck, 3));
    for (const string& finding : data.findings) line("finding", finding);
    for (const string& item : data.openItems) line("open", item);
    return out.str();
}

} // namespace m5
} // namespace ob
