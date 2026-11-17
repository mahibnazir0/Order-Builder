// The equal-service tie rule ED-21 (tests v12 X-RES-13, X-META-06) on fixture FX-TIE: groups of three
// FX-CAP-ATOM T2 atoms (48 x 36 x 90 in, 600 lb, two unit loads) on one source line each, one reserved
// slot of cap 6 per lane-date, so every complete allocation ships one group. The oracle enumerates every
// whole-atom plan of one truck, ranks them by urgency, then by its own FNV-1a and day-number rotation, then
// by canonical records; it never calls production code.
#include "doctest.h"
#include "m5Allocation.hpp"
#include "m5Commit.hpp"
#include "m5Ledger.hpp"
#include "m5Methods.hpp"
#include "m5Orchestrator.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Fixtures.hpp"
#include "support/m5StubMethods.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std;
using namespace ob::m5;
using namespace m5Fixtures;

namespace {

const string kAug6 = "2028-08-06", kAug7 = "2028-08-07", kAug8 = "2028-08-08";
const LaneKey kLaneA{"9001", "9004", "TL"}, kLaneB{"9001", "9003", "TL"}, kLaneC{"9001", "9002", "TL"};
constexpr int kAtomUnits = 2;
constexpr int kSlotCap = 6;

struct GroupSpec {
    string group;
    string source;
    int priority = 0;
    string available;  // empty: the ship date
};

Atom tieAtom(const string& id, const GroupSpec& spec, const string& shipDate) {
    Atom atom;
    atom.id = id;
    atom.group = spec.group;
    atom.floor.longSideIn = 48.0;
    atom.floor.shortSideIn = 36.0;
    atom.floor.heightIn = 90.0;
    atom.floor.levels = 1;
    atom.floor.cubeCubicIn = 48.0 * 36.0 * 90.0;
    atom.weightLb = 600.0;
    atom.unitLoads = kAtomUnits;
    for (int unit = 0; unit < kAtomUnits; ++unit) {
        Component component;
        component.source = spec.source;
        component.product = "T2";
        component.unitLoads = 1.0;
        component.priority = spec.priority;
        component.dueDate = shipDate;
        component.availableDate = spec.available.empty() ? shipDate : spec.available;
        atom.components.push_back(component);
    }
    return atom;
}

// Three atoms per group, listed in the order given (reversed for shuffledInput).
M5LaneDateInput tieLane(const LaneKey& lane, const string& shipDate, const vector<GroupSpec>& groups) {
    vector<Atom> atoms;
    for (const GroupSpec& spec : groups) {
        for (int index = 1; index <= 3; ++index) atoms.push_back(tieAtom(spec.source + "-a" + to_string(index), spec, shipDate));
    }
    M5LaneDateInput input = laneWith(std::move(atoms), 0);
    input.lane = lane;
    input.departure = shipDate;
    input.pool.slots.push_back({630.0, 100.0, 108.0, 45000.0, kSlotCap});
    input.sourceVersion = "fx-tie";
    return input;
}

vector<GroupSpec> groupsOf(const vector<string>& names) {
    vector<GroupSpec> specs;
    for (const string& name : names) specs.push_back({name, "S-" + name, 0, ""});
    return specs;
}

M5LaneDateInput reversed(M5LaneDateInput input) {
    reverse(input.atoms.begin(), input.atoms.end());
    return input;
}

// ---- Independent oracle ---------------------------------------------------------------------------

uint32_t referenceFnv1a(const string& text) {
    uint64_t hash = 2166136261ull;
    for (const unsigned char byte : text) hash = ((hash ^ byte) * 16777619ull) % 4294967296ull;
    return static_cast<uint32_t>(hash);
}

// Days from 1970-01-01 by the proleptic Gregorian civil calendar.
long long referenceDayNumber(const string& isoDate) {
    long long year = stoll(isoDate.substr(0, 4));
    const long long month = stoll(isoDate.substr(5, 2)), day = stoll(isoDate.substr(8, 2));
    year -= month <= 2 ? 1 : 0;
    const long long era = year / 400;
    const long long yearOfEra = year - era * 400;
    const long long dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const long long dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

bool oracleEligible(const Atom& atom, const string& shipDate) {
    return all_of(atom.components.begin(), atom.components.end(),
                  [&](const Component& component) { return component.availableDate <= shipDate && component.dueDate <= shipDate; });
}

vector<string> oracleRotatedGroups(const M5LaneDateInput& input) {
    set<string> eligible;
    for (const Atom& atom : input.atoms) {
        if (oracleEligible(atom, input.departure)) eligible.insert(atom.group);
    }
    vector<string> groups(eligible.begin(), eligible.end());
    if (groups.empty()) return groups;
    const uint64_t count = groups.size();
    const uint64_t laneKey = referenceFnv1a(input.lane.origin + "|" + input.lane.destination + "|" + input.lane.shipCondition);
    const uint64_t rotation = (laneKey % count + static_cast<uint64_t>(referenceDayNumber(input.departure)) % count) % count;
    rotate(groups.begin(), groups.begin() + static_cast<ptrdiff_t>(rotation), groups.end());
    return groups;
}

// The best one-truck plan: never mixing groups, within the slot cap; then more unit loads of the most
// urgent priority first; then ED-21 by the oracle's own rotation; then the lower (source, atom) records.
set<string> oracleWinner(const M5LaneDateInput& input) {
    const vector<string> order = oracleRotatedGroups(input);
    const size_t atomCount = input.atoms.size();
    REQUIRE(atomCount <= 12);
    struct Scored {
        vector<int> byPriority;
        vector<int> byGroupOrder;
        vector<pair<string, string>> records;
        set<string> atoms;
    };
    optional<Scored> best;
    for (uint32_t mask = 0; mask < (1u << atomCount); ++mask) {
        set<string> groups;
        int units = 0;
        Scored scored;
        scored.byPriority.assign(12, 0);
        scored.byGroupOrder.assign(order.size(), 0);
        bool allowed = true;
        for (size_t atom = 0; atom < atomCount; ++atom) {
            if ((mask >> atom & 1u) == 0) continue;
            const Atom& item = input.atoms[atom];
            if (!oracleEligible(item, input.departure)) {
                allowed = false;
                break;
            }
            groups.insert(item.group);
            scored.atoms.insert(item.id);
            for (const Component& component : item.components) {
                units += static_cast<int>(component.unitLoads);
                scored.byPriority[static_cast<size_t>(component.priority)] += static_cast<int>(component.unitLoads);
                const auto position = find(order.begin(), order.end(), item.group) - order.begin();
                scored.byGroupOrder[static_cast<size_t>(position)] += static_cast<int>(component.unitLoads);
                scored.records.emplace_back(component.source, item.id);
            }
        }
        if (!allowed || groups.size() > 1 || units > kSlotCap) continue;
        sort(scored.records.begin(), scored.records.end());
        bool better = true;
        if (best && scored.byPriority != best->byPriority) better = scored.byPriority > best->byPriority;
        else if (best && scored.byGroupOrder != best->byGroupOrder) better = scored.byGroupOrder > best->byGroupOrder;
        else if (best) better = scored.records < best->records;
        if (better) best = std::move(scored);
    }
    return best ? best->atoms : set<string>{};
}

// ---- Checks on a production outcome ---------------------------------------------------------------

set<string> shippedGroups(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    set<string> groups;
    if (!outcome.selected) return groups;
    for (const TruckPlan& truck : outcome.selected->trucks) {
        for (const size_t atom : truck.atoms) groups.insert(input.atoms[atom].group);
    }
    return groups;
}

// Exactly the expected group's three atoms ship on one truck, nothing mixes, every other atom is left
// over, and every source line's units balance (shipped plus left equals offered).
void checkTieOutcome(const M5LaneOutcome& outcome, const M5LaneDateInput& input, const string& expectedGroup) {
    REQUIRE(outcome.selected);
    CHECK(outcome.finalValidationPassed);
    CHECK(outcome.selected->trucks.size() == 1);
    CHECK(shippedGroups(outcome, input) == set<string>{expectedGroup});
    CHECK(shippedOfGroup(outcome, input, expectedGroup) == 3);
    CHECK(shippedIds(outcome, input) == oracleWinner(input));
    map<string, double> offered, accounted;
    for (const Atom& atom : input.atoms) {
        for (const Component& component : atom.components) offered[component.source] += component.unitLoads;
    }
    double shipped = 0.0;
    for (const TruckPlan& truck : outcome.selected->trucks) {
        CHECK(truck.group == expectedGroup);
        for (const size_t atom : truck.atoms) {
            for (const Component& component : input.atoms[atom].components) {
                accounted[component.source] += component.unitLoads;
                shipped += component.unitLoads;
            }
        }
    }
    CHECK(shipped == kSlotCap);
    CHECK(outcome.leftOver.size() + shippedCount(outcome) == input.atoms.size());
    for (const LeftOver& left : outcome.leftOver) {
        CHECK(input.atoms[left.atom].group != expectedGroup);
        for (const Component& component : input.atoms[left.atom].components) accounted[component.source] += component.unitLoads;
    }
    CHECK(accounted == offered);
}

string winnerOf(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    const set<string> groups = shippedGroups(outcome, input);
    return groups.size() == 1 ? *groups.begin() : "(" + to_string(groups.size()) + " groups)";
}

// A method that offers each group its three atoms as one truck, `copies[group]` times over.
class DuplicatingMethod final : public MethodPlanner {
public:
    explicit DuplicatingMethod(map<GroupId, size_t> copies) : copies_(std::move(copies)) {}
    MethodId id() const override { return MethodId::PatternRepeat; }
    GroupSupply supplyForGroup(const MethodContext& context, const GroupId& group) const override {
        GroupSupply supply;
        supply.group = group;
        supply.plans.push_back({});
        Truck truck;
        for (size_t atom = 0; atom < context.snapshot.input.atoms.size(); ++atom) {
            if (context.snapshot.input.atoms[atom].group == group) truck.push_back(atom);
        }
        const auto found = copies_.find(group);
        for (size_t copy = 0; found != copies_.end() && copy < found->second; ++copy) supply.plans.push_back({truck});
        return supply;
    }

private:
    map<GroupId, size_t> copies_;
};

}  // namespace

TEST_CASE("X-RES-13/twoGroupsAlternate: lane LA, G1 ships on 6 Aug 2028 (r=0) and G2 on 7 Aug (r=1)" * doctest::test_suite("m5")) {
    vector<string> winners;
    for (const string& date : {kAug6, kAug7}) {
        const M5LaneDateInput input = tieLane(kLaneA, date, groupsOf({"G1", "G2"}));
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        winners.push_back(winnerOf(outcome, input));
        checkTieOutcome(outcome, input, date == kAug6 ? "G1" : "G2");
    }
    m5Evidence::note("expected", {"G1", "G2"});
    m5Evidence::note("actual", winners);
}

TEST_CASE("X-RES-13/threeGroupsCycle: lane LA, G1, G2 and G3 each win once over 6, 7 and 8 Aug 2028" * doctest::test_suite("m5")) {
    const vector<pair<string, string>> expected{{kAug6, "G1"}, {kAug7, "G2"}, {kAug8, "G3"}};
    vector<string> winners;
    for (const auto& [date, group] : expected) {
        const M5LaneDateInput input = tieLane(kLaneA, date, groupsOf({"G1", "G2", "G3"}));
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        winners.push_back(winnerOf(outcome, input));
        checkTieOutcome(outcome, input, group);
    }
    CHECK(set<string>(winners.begin(), winners.end()) == set<string>{"G1", "G2", "G3"});
    m5Evidence::note("expected", {"G1", "G2", "G3"});
    m5Evidence::note("actual", winners);
}

TEST_CASE("X-RES-13/shuffledInput: reversed source rows and group listing give the same winners as twoGroupsAlternate"
          * doctest::test_suite("m5")) {
    vector<string> winners;
    for (const string& date : {kAug6, kAug7}) {
        const M5LaneDateInput input = reversed(tieLane(kLaneA, date, groupsOf({"G2", "G1"})));
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        winners.push_back(winnerOf(outcome, input));
        checkTieOutcome(outcome, input, date == kAug6 ? "G1" : "G2");
    }
    m5Evidence::note("expected", {"G1", "G2"});
    m5Evidence::note("actual", winners);
}

TEST_CASE("X-RES-13/copiedLane X-META-06/synthetic: a lane copied to LB is planned on its own; LA ships G1, LB ships G2, totals double"
          * doctest::test_suite("m5")) {
    const M5LaneDateInput laneA = tieLane(kLaneA, kAug6, groupsOf({"G1", "G2"}));
    M5LaneDateInput laneB = laneA;
    laneB.lane = kLaneB;
    const M5LaneOutcome outcomeA = plan(laneA, syntheticProfile());
    const M5LaneOutcome outcomeB = plan(laneB, syntheticProfile());
    checkTieOutcome(outcomeA, laneA, "G1");
    checkTieOutcome(outcomeB, laneB, "G2");
    // Shipped unit loads per urgency class are equal on both lanes; the total is twice one lane's.
    CHECK(shippedCount(outcomeA) == shippedCount(outcomeB));
    CHECK((shippedCount(outcomeA) + shippedCount(outcomeB)) * kAtomUnits == 2 * kSlotCap);
    // The FX-30 copy without a tie still ships the same goods on both lanes.
    M5LaneDateInput fx = laneWith(fx30Units("u", 50), 2), fxCopy = fx;
    fxCopy.lane = {"9999", "8888", "TL"};
    CHECK(shippedIds(plan(fx, syntheticProfile()), fx) == shippedIds(plan(fxCopy, syntheticProfile()), fxCopy));
    m5Evidence::note("expected", {{"LA", "G1"}, {"LB", "G2"}, {"totalUnitLoads", 2 * kSlotCap}});
    m5Evidence::note("actual", {{"LA", winnerOf(outcomeA, laneA)}, {"LB", winnerOf(outcomeB, laneB)},
                                {"totalUnitLoads", (shippedCount(outcomeA) + shippedCount(outcomeB)) * kAtomUnits}});
}

TEST_CASE("X-RES-13/acrossLanes: the 6 Aug 2028 tie on lanes LA, LB and LC ships G1, G2 and G1" * doctest::test_suite("m5")) {
    vector<string> winners;
    const vector<pair<LaneKey, string>> expected{{kLaneA, "G1"}, {kLaneB, "G2"}, {kLaneC, "G1"}};
    for (const auto& [lane, group] : expected) {
        const M5LaneDateInput input = tieLane(lane, kAug6, groupsOf({"G1", "G2"}));
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        winners.push_back(winnerOf(outcome, input));
        checkTieOutcome(outcome, input, group);
    }
    CHECK(set<string>(winners.begin(), winners.end()).size() == 2);
    m5Evidence::note("expected", {"G1", "G2", "G1"});
    m5Evidence::note("actual", winners);
}

TEST_CASE("X-RES-13/repeatRun: the 6 Aug two-group case on LA ships G1 twice in one process and again cold" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = tieLane(kLaneA, kAug6, groupsOf({"G1", "G2"}));
    vector<string> winners;
    for (int run = 0; run < 2; ++run) {
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        winners.push_back(winnerOf(outcome, input));
        checkTieOutcome(outcome, input, "G1");
    }
    const M5LaneDateInput cold = tieLane(kLaneA, kAug6, groupsOf({"G1", "G2"}));
    const M5LaneOutcome coldOutcome = plan(cold, syntheticProfile());
    winners.push_back(winnerOf(coldOutcome, cold));
    checkTieOutcome(coldOutcome, cold, "G1");
    m5Evidence::note("expected", {"G1", "G1", "G1"});
    m5Evidence::note("actual", winners);
}

TEST_CASE("X-RES-13/nonTieControl: G1 at P11 and G2 at P0 on LA 6 Aug: urgency ships G2 before ED-21 applies"
          * doctest::test_suite("m5")) {
    const M5LaneDateInput input = tieLane(kLaneA, kAug6, {{"G1", "S-G1", 11, ""}, {"G2", "S-G2", 0, ""}});
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    checkTieOutcome(outcome, input, "G2");
    CHECK(oracleRotatedGroups(input).front() == "G1");  // the rotation alone would have chosen G1
    m5Evidence::note("expected", "G2");
    m5Evidence::note("actual", winnerOf(outcome, input));
}

TEST_CASE("X-RES-13/canonicalWithinGroup: G1 alone with lines L1 and L2: L1's three atoms ship, in either input order"
          * doctest::test_suite("m5")) {
    const M5LaneDateInput forward = tieLane(kLaneA, kAug6, {{"G1", "L1", 0, ""}, {"G1", "L2", 0, ""}});
    vector<set<string>> shipped;
    for (const M5LaneDateInput& input : {forward, reversed(forward)}) {
        const M5LaneOutcome outcome = plan(input, syntheticProfile());
        REQUIRE(outcome.selected);
        CHECK(outcome.finalValidationPassed);
        CHECK(outcome.selected->trucks.size() == 1);
        CHECK(shippedIds(outcome, input) == set<string>{"L1-a1", "L1-a2", "L1-a3"});
        CHECK(shippedIds(outcome, input) == oracleWinner(input));
        CHECK(outcome.leftOver.size() == 3);
        shipped.push_back(shippedIds(outcome, input));
    }
    CHECK(shipped[0] == shipped[1]);
    m5Evidence::note("expected", {"L1-a1", "L1-a2", "L1-a3"});
    m5Evidence::note("actual", vector<string>(shipped[1].begin(), shipped[1].end()));
}

TEST_CASE("X-RES-13/renamedGroups: G1 renamed G9 sorts after G2, so on LA 6 Aug (r=0) G2 ships" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = tieLane(kLaneA, kAug6, groupsOf({"G9", "G2"}));
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    checkTieOutcome(outcome, input, "G2");
    m5Evidence::note("expected", "G2");
    m5Evidence::note("actual", winnerOf(outcome, input));
}

TEST_CASE("X-RES-13/threadCountReplay: LA 6 Aug planned on 1, 2 and 8 threads at once always ships G1" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = tieLane(kLaneA, kAug6, groupsOf({"G1", "G2"}));
    vector<string> winners;
    for (const size_t threadCount : {size_t{1}, size_t{2}, size_t{8}}) {
        vector<M5LaneOutcome> outcomes(threadCount);
        vector<thread> workers;
        for (size_t worker = 0; worker < threadCount; ++worker) {
            workers.emplace_back([&outcomes, &input, worker] { outcomes[worker] = plan(input, syntheticProfile()); });
        }
        for (thread& worker : workers) worker.join();
        for (const M5LaneOutcome& outcome : outcomes) {
            CHECK(outcome.stop == StopReason::Completed);
            checkTieOutcome(outcome, input, "G1");
            CHECK(outcome.selected->trucks.front().slot == 0);
            winners.push_back(winnerOf(outcome, input));
        }
    }
    CHECK(winners == vector<string>(11, "G1"));
    m5Evidence::note("expected", "G1 in all 11 runs");
    m5Evidence::note("actual", winners);
}

TEST_CASE("X-RES-13/duplicateCandidates: G1's plan offered three times and G2's once, then the reverse: G1 wins, nothing multiplies"
          * doctest::test_suite("m5")) {
    vector<string> winners;
    for (const map<GroupId, size_t>& copies : {map<GroupId, size_t>{{"G1", 3}, {"G2", 1}}, map<GroupId, size_t>{{"G1", 1}, {"G2", 3}}}) {
        const M5LaneDateInput input = tieLane(kLaneA, kAug6, groupsOf({"G1", "G2"}));
        FakeClock clock;
        M5RunServices services(clock);
        services.methods = {make_shared<DuplicatingMethod>(copies)};
        const M5LaneOutcome outcome = planLaneDate(input, syntheticProfile(), physicalSettings(), services);
        checkTieOutcome(outcome, input, "G1");
        REQUIRE(outcome.delta);
        winners.push_back(winnerOf(outcome, input));
    }
    m5Evidence::note("expected", {"G1", "G1"});
    m5Evidence::note("actual", winners);
}

TEST_CASE("X-RES-13/specialGroupIds: IDs A/B and A|B sort bytewise, A/B first; with r=0 A/B ships and neither ID is split"
          * doctest::test_suite("m5")) {
    const M5LaneDateInput input = tieLane(kLaneA, kAug6, {{"A/B", "S-slash", 0, ""}, {"A|B", "S-bar", 0, ""}});
    CHECK(oracleRotatedGroups(input) == vector<string>{"A/B", "A|B"});
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    checkTieOutcome(outcome, input, "A/B");
    m5Evidence::note("expected", "A/B");
    m5Evidence::note("actual", winnerOf(outcome, input));
}

TEST_CASE("X-RES-13/fnvUnsignedOverflow: laneKey 3274617072 stays unsigned, mod 3 = 0, day 21402 mod 3 = 0, r=0: G1 wins"
          * doctest::test_suite("m5")) {
    const uint32_t reference = referenceFnv1a("9001|9004|TL");
    CHECK(reference == 3274617072u);
    CHECK(reference > 2147483647u);
    CHECK(referenceDayNumber(kAug6) == 21402);
    // A signed 32-bit cast would give a negative remainder here, an invalid rotation index.
    const auto signedKey = static_cast<int64_t>(reference) - 4294967296LL;
    CHECK(signedKey % 3 == -1);
    CHECK(fnv1a32("9001|9004|TL") == reference);
    const M5LaneDateInput input = tieLane(kLaneA, kAug6, groupsOf({"G1", "G2", "G3"}));
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    checkTieOutcome(outcome, input, "G1");
    m5Evidence::note("expected", {{"laneKey", 3274617072u}, {"dayNumber", 21402}, {"rotation", 0}, {"winner", "G1"}});
    m5Evidence::note("actual", {{"laneKey", fnv1a32("9001|9004|TL")}, {"winner", winnerOf(outcome, input)}});
}

TEST_CASE("X-RES-13/ineligibleGroupIgnored: G0 available only from 8 Aug has no eligible goods on 7 Aug; g=2, r=1, G2 ships, G0 untouched"
          * doctest::test_suite("m5")) {
    const M5LaneDateInput input =
        tieLane(kLaneA, kAug7, {{"G0", "S-G0", 0, kAug8}, {"G1", "S-G1", 0, ""}, {"G2", "S-G2", 0, ""}});
    CHECK(oracleRotatedGroups(input) == vector<string>{"G2", "G1"});
    const M5LaneOutcome outcome = plan(input, syntheticProfile());
    checkTieOutcome(outcome, input, "G2");
    size_t notEligible = 0;
    for (const LeftOver& left : outcome.leftOver) {
        if (input.atoms[left.atom].group == "G0") {
            CHECK(left.cause == LeftOverCause::NotEligible);
            ++notEligible;
        }
    }
    CHECK(notEligible == 3);
    m5Evidence::note("expected", {{"winner", "G2"}, {"g", 2}, {"G0LeftNotEligible", 3}});
    m5Evidence::note("actual", {{"winner", winnerOf(outcome, input)}, {"G0LeftNotEligible", notEligible}});
}

TEST_CASE("X-STATE-10/sameSlotTieConcurrent: G1's and a stale G2 candidate race for LA 6 Aug's one slot; only G1 commits, once" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = tieLane(kLaneA, kAug6, groupsOf({"G1", "G2"}));
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const M5LaneOutcome selected = plan(input, syntheticProfile());
    REQUIRE(winnerOf(selected, input) == "G1");
    REQUIRE(selected.delta);
    set<string> g1Atoms;
    for (const TruckPlan& truck : selected.delta->trucks) {
        for (const size_t atom : truck.atoms) g1Atoms.insert(input.atoms[atom].id);
    }
    REQUIRE(g1Atoms.size() == 3);

    // The competing candidate: G2's three atoms, planned from a second snapshot of the same lane-date.
    M5LaneDateInput otherInput = input;
    otherInput.sourceVersion = "fx-tie-second-snapshot";
    FakeClock clock;
    M5RunServices services(clock);
    services.methods = {make_shared<m5Stubs::FixedMethod>(MethodId::PatternRepeat, GroupTrucks{Truck{3, 4, 5}})};
    const M5LaneOutcome competing = planLaneDate(otherInput, syntheticProfile(), physicalSettings(), services);
    REQUIRE(winnerOf(competing, otherInput) == "G2");
    REQUIRE(competing.delta);
    const DemandSnapshot otherSnapshot = *buildSnapshot(otherInput).snapshot;
    REQUIRE(otherSnapshot.id != snapshot.id);

    for (int round = 0; round < 50; ++round) {
        CommitLedger ledger;
        OperationalState state{snapshot.id, snapshot.input.pool.version, {}, 0, 1};
        atomic<bool> go{false};
        ApplyStatus g1Status = ApplyStatus::StaleSnapshot, g2Status = ApplyStatus::Applied;
        thread g1([&] {
            while (!go.load()) this_thread::yield();
            g1Status = ledger.applyIfCurrent(*selected.delta, snapshot, state);
        });
        thread g2([&] {
            while (!go.load()) this_thread::yield();
            g2Status = ledger.applyIfCurrent(*competing.delta, otherSnapshot, state);
        });
        go = true;
        g1.join();
        g2.join();
        CHECK(g1Status == ApplyStatus::Applied);
        CHECK(g2Status == ApplyStatus::StaleSnapshot);
        CHECK(state.consumedAtoms == g1Atoms);
        CHECK(state.slotsUsed == 1);
        CHECK(state.reservationVersion == snapshot.input.pool.version + 1);
        CHECK(ledger.applyIfCurrent(*selected.delta, snapshot, state) == ApplyStatus::AlreadyApplied);
        CHECK(state.slotsUsed == 1);
    }

    // Rebased on what G1 left, G2 has no slot: it ships nothing and debits nothing.
    const DemandSnapshot residual = residualAfter(snapshot, *selected.delta);
    CHECK(residual.input.pool.slots.empty());
    CHECK(residual.input.atoms.size() == 3);
    const M5LaneOutcome rebased = plan(residual.input, syntheticProfile());
    CHECK(shippedGroups(rebased, residual.input).empty());
    size_t sourceUnits = 0;
    for (const TruckPlan& truck : selected.delta->trucks) {
        for (const size_t atom : truck.atoms) sourceUnits += input.atoms[atom].components.size();
    }
    CHECK(sourceUnits == 6);
    m5Evidence::note("expected", {"G1 Applied once", "G2 StaleSnapshot", "1 slot", "6 source unit loads"});
}
