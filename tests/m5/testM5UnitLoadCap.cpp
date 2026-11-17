// The per-slot unit-load cap (ED-20, tests v12 X-PAT-16, X-PAT-17, X-STATE-11, X-CG-21) on the shared
// fixture FX-CAP-ATOM: rigid 48 x 36 x 90 in, 600 lb atoms that carry 2 (T2) or 3 (T3) unit loads.
// Floor, height and payload are slack, so the cap alone binds. Oracles enumerate whole-atom subsets and
// assignments with their own arithmetic; they never call production code.
#include "doctest.h"
#include "m5Commit.hpp"
#include "m5Ledger.hpp"
#include "m5Methods.hpp"
#include "m5Repair.hpp"
#include "m5Solver.hpp"
#include "m5UnitLoads.hpp"
#include "m5Validation.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Fixtures.hpp"
#include "support/m5Oracles.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace std;
using namespace ob::m5;
using namespace m5Fixtures;

namespace {

constexpr int64_t kMaxCap = numeric_limits<int64_t>::max();

// One atom of FX-CAP-ATOM: `unitLoads` components of one unit each from their own source lines, or
// one coalesced component carrying them all.
Atom capAtom(const string& id, int unitLoads, bool coalesced = false) {
    Atom atom;
    atom.id = id;
    atom.group = "G";
    atom.floor.longSideIn = 48.0;
    atom.floor.shortSideIn = 36.0;
    atom.floor.heightIn = 90.0;
    atom.floor.levels = 1;
    atom.floor.cubeCubicIn = 48.0 * 36.0 * 90.0;
    atom.weightLb = 600.0;
    atom.unitLoads = unitLoads;
    const int components = coalesced ? 1 : unitLoads;
    for (int index = 0; index < components; ++index) {
        Component component;
        component.source = "line-" + id + (coalesced ? "" : "-" + to_string(index));
        component.product = unitLoads == 2 ? "T2" : unitLoads == 3 ? "T3" : "T" + to_string(unitLoads);
        component.unitLoads = coalesced ? unitLoads : 1.0;
        component.dueDate = kD;
        component.availableDate = kD;
        atom.components.push_back(component);
    }
    return atom;
}

vector<Atom> capAtoms(const string& prefix, size_t count, int unitLoads, bool coalesced = false) {
    vector<Atom> atoms;
    for (size_t index = 0; index < count; ++index) atoms.push_back(capAtom(prefix + to_string(index + 1), unitLoads, coalesced));
    return atoms;
}

TrailerCapability slotWithCap(int64_t cap) { return {630.0, 100.0, 108.0, 45000.0, cap}; }

M5LaneDateInput laneOf(vector<Atom> atoms, const vector<int64_t>& caps) {
    M5LaneDateInput input = laneWith(std::move(atoms), 0);
    for (const int64_t cap : caps) input.pool.slots.push_back(slotWithCap(cap));
    return input;
}

M5PolicyProfile profileWith(MethodPolicy method) {
    M5PolicyProfile profile = syntheticProfile();
    profile.method = method;
    return profile;
}

M5LaneOutcome planWith(const M5LaneDateInput& input, MethodPolicy method = MethodPolicy::SequentialAThenB) {
    return plan(input, profileWith(method));
}

// ---- Independent arithmetic ---------------------------------------------------------------------------

double componentUnits(const Atom& atom) {
    double units = 0.0;
    for (const Component& component : atom.components) units += component.unitLoads;
    return units;
}

double unitsOf(const M5LaneDateInput& input, const vector<size_t>& atoms) {
    double units = 0.0;
    for (const size_t atom : atoms) units += componentUnits(input.atoms[atom]);
    return units;
}

double shippedUnits(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    double units = 0.0;
    if (!outcome.selected) return 0.0;
    for (const TruckPlan& truck : outcome.selected->trucks) units += unitsOf(input, truck.atoms);
    return units;
}

double leftUnits(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    double units = 0.0;
    for (const LeftOver& left : outcome.leftOver) units += componentUnits(input.atoms[left.atom]);
    return units;
}

// Unit loads on each actual slot index, from the selected plan.
map<size_t, double> unitsBySlot(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    map<size_t, double> bySlot;
    if (!outcome.selected) return bySlot;
    for (const TruckPlan& truck : outcome.selected->trucks) bySlot[truck.slot] += unitsOf(input, truck.atoms);
    return bySlot;
}

// The most unit loads whole atoms can ship when each atom goes to one slot or is left: every assignment
// of the atoms to slots-or-leftover, each slot within its cap. Floor and payload are slack here.
double oracleBestService(const vector<int>& atomUnits, const vector<int64_t>& caps) {
    const size_t choices = caps.size() + 1;
    size_t assignments = 1;
    for (size_t index = 0; index < atomUnits.size(); ++index) assignments *= choices;
    double best = 0.0;
    for (size_t code = 0; code < assignments; ++code) {
        vector<int64_t> load(caps.size(), 0);
        size_t rest = code;
        double shipped = 0.0;
        bool valid = true;
        for (const int units : atomUnits) {
            const size_t choice = rest % choices;
            rest /= choices;
            if (choice == caps.size()) continue;
            load[choice] += units;
            shipped += units;
            if (load[choice] > caps[choice]) valid = false;
        }
        if (valid) best = max(best, shipped);
    }
    return best;
}

// The fewest slots that carry every atom, each within `cap`; 0 when `slots` cannot.
size_t oracleFewestTrucks(const vector<int>& atomUnits, int64_t cap, size_t slots) {
    size_t assignments = 1;
    for (size_t index = 0; index < atomUnits.size(); ++index) assignments *= slots;
    size_t fewest = 0;
    for (size_t code = 0; code < assignments; ++code) {
        vector<int64_t> load(slots, 0);
        size_t rest = code;
        bool valid = true;
        for (const int units : atomUnits) {
            load[rest % slots] += units;
            if (load[rest % slots] > cap) valid = false;
            rest /= slots;
        }
        if (!valid) continue;
        const size_t used = static_cast<size_t>(count_if(load.begin(), load.end(), [](int64_t units) { return units > 0; }));
        if (fewest == 0 || used < fewest) fewest = used;
    }
    return fewest;
}

vector<int> unitsList(const M5LaneDateInput& input) {
    vector<int> units;
    for (const Atom& atom : input.atoms) units.push_back(static_cast<int>(componentUnits(atom)));
    return units;
}

void checkWholeAndValid(const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    for (const string& violation : m5Oracles::invariantViolations(outcome, input, physicalSettings().floor)) {
        INFO(violation);
        CHECK(false);
    }
    CHECK(shippedUnits(outcome, input) + leftUnits(outcome, input) == unitsOf(input, [&] {
              vector<size_t> all(input.atoms.size());
              for (size_t index = 0; index < all.size(); ++index) all[index] = index;
              return all;
          }()));
    if (outcome.selected) CHECK(outcome.selected->validation->allPassed());
}

// A candidate with the named trucks (atoms per actual slot) and every other atom left over.
CandidatePlan candidateOf(const M5LaneDateInput& input, const vector<pair<size_t, vector<size_t>>>& trucks) {
    CandidatePlan plan;
    vector<char> used(input.atoms.size(), 0);
    for (const auto& [slot, atoms] : trucks) {
        TruckPlan truck;
        truck.slot = slot;
        truck.group = "G";
        truck.atoms = atoms;
        sort(truck.atoms.begin(), truck.atoms.end());
        for (const size_t atom : atoms) used[atom] = 1;
        plan.trucks.push_back(truck);
    }
    for (size_t atom = 0; atom < input.atoms.size(); ++atom) {
        if (!used[atom]) plan.leftOver.push_back({atom, LeftOverCause::OverReservation});
    }
    return plan;
}

bool hasCapFinding(const ValidationReport& report, size_t truck) {
    return any_of(report.findings.begin(), report.findings.end(), [&](const ValidationFinding& finding) {
        return finding.truck == truck && finding.section == "physical" && finding.reason.find("over the cap of") != string::npos;
    });
}

// Method A at the direct context seam with a caller-owned pattern cache, allocated as the orchestrator
// allocates: the unit loads the chosen trucks ship.
double patternServiceWithCache(const M5LaneDateInput& input, GeometryCache& cache) {
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const UrgencyDomain domain(snapshot);
    const M5PolicyProfile profile = profileWith(MethodPolicy::PatternOnly);
    const unique_ptr<AllocationComparator> comparator = makeComparator(profile.comparator);
    const PhysicalSettings physical = physicalSettings();
    const vector<char> unfittable(input.atoms.size(), 0);
    const MethodContext context{snapshot, domain, profile, physical, *comparator, cache, unfittable, input.pool.slots.size(), StopCheck(), {}};
    const PatternPlanner planner;
    const AllocationResult allocation =
        allocate(snapshot, domain, {planner.supplyForGroup(context, "G")}, input.pool.slots.size(), *comparator);
    double units = 0.0;
    for (const GroupTrucks& trucks : allocation.chosen) {
        for (const Truck& truck : trucks) {
            CHECK(unitsOf(input, truck) <= static_cast<double>(input.pool.slots.front().maxUnitLoads));
            units += unitsOf(input, truck);
        }
    }
    return units;
}

void noteService(double expectedShipped, double expectedLeft, const M5LaneOutcome& outcome, const M5LaneDateInput& input) {
    m5Evidence::note("expected", {{"shippedUnitLoads", expectedShipped}, {"leftUnitLoads", expectedLeft}});
    m5Evidence::note("actual", {{"shippedUnitLoads", shippedUnits(outcome, input)},
                                {"leftUnitLoads", leftUnits(outcome, input)},
                                {"trucks", outcome.selected ? outcome.selected->trucks.size() : 0},
                                {"outcome", outcomeKindName(outcome.kind)},
                                {"proof", proofStatusName(outcome.proof)}});
}

vector<Atom> mixedFixture(bool coalesced = false) {
    vector<Atom> atoms = capAtoms("A", 3, 2, coalesced);
    for (Atom& atom : capAtoms("B", 2, 3, coalesced)) atoms.push_back(atom);
    return atoms;
}

}  // namespace

// ---- X-PAT-16 --------------------------------------------------------------------------------------------

TEST_CASE("X-PAT-16/mixedClasses: three T2 and two T3 atoms at cap 5 ship 5 unit loads, one of each" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneOf(mixedFixture(), {5});
    const M5LaneOutcome outcome = planWith(input);
    CHECK(oracleBestService(unitsList(input), {5}) == 5.0);
    CHECK(shippedUnits(outcome, input) == 5.0);
    CHECK(leftUnits(outcome, input) == 7.0);
    REQUIRE(outcome.selected);
    REQUIRE(outcome.selected->trucks.size() == 1);
    const vector<size_t>& shipped = outcome.selected->trucks.front().atoms;
    CHECK(shipped.size() == 2);
    CHECK(count_if(shipped.begin(), shipped.end(), [&](size_t atom) { return componentUnits(input.atoms[atom]) == 2.0; }) == 1);
    CHECK(count_if(shipped.begin(), shipped.end(), [&](size_t atom) { return componentUnits(input.atoms[atom]) == 3.0; }) == 1);
    CHECK(outcome.leftOver.size() == 3);
    checkWholeAndValid(outcome, input);
    noteService(5, 7, outcome, input);
}

TEST_CASE("X-PAT-16/homogeneousTwo: three T2 atoms at cap 5 ship two atoms, 4 unit loads" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneOf(capAtoms("A", 3, 2), {5});
    const M5LaneOutcome outcome = planWith(input);
    CHECK(oracleBestService(unitsList(input), {5}) == 4.0);
    CHECK(shippedUnits(outcome, input) == 4.0);
    CHECK(leftUnits(outcome, input) == 2.0);
    CHECK(shippedCount(outcome) == 2);
    checkWholeAndValid(outcome, input);
    noteService(4, 2, outcome, input);
}

TEST_CASE("X-PAT-16/homogeneousThree: two T3 atoms at cap 5 ship one atom, 3 unit loads" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneOf(capAtoms("B", 2, 3), {5});
    const M5LaneOutcome outcome = planWith(input);
    CHECK(oracleBestService(unitsList(input), {5}) == 3.0);
    CHECK(shippedUnits(outcome, input) == 3.0);
    CHECK(leftUnits(outcome, input) == 3.0);
    CHECK(shippedCount(outcome) == 1);
    checkWholeAndValid(outcome, input);
    noteService(3, 3, outcome, input);
}

TEST_CASE("X-PAT-16/sameGeometryABA: equal geometry, different coefficients, one warm pattern cache: 4/3/4" * doctest::test_suite("m5")) {
    const M5LaneDateInput two = laneOf(capAtoms("A", 3, 2), {5});
    const M5LaneDateInput three = laneOf(capAtoms("B", 2, 3), {5});
    GeometryCache warm;
    vector<double> warmRuns, coldRuns, plannedRuns;
    for (const M5LaneDateInput* input : {&two, &three, &two}) {
        warmRuns.push_back(patternServiceWithCache(*input, warm));
        GeometryCache cold;
        coldRuns.push_back(patternServiceWithCache(*input, cold));
        plannedRuns.push_back(shippedUnits(planWith(*input), *input));
    }
    CHECK(warmRuns == vector<double>{4.0, 3.0, 4.0});
    CHECK(coldRuns == warmRuns);
    CHECK(plannedRuns == warmRuns);
    m5Evidence::note("expected", {4, 3, 4});
    m5Evidence::note("actual", {{"warmCache", warmRuns}, {"coldCache", coldRuns}, {"planLaneDate", plannedRuns}});
}

TEST_CASE("X-PAT-16/coalescedSourceShares: one share of 2 or 3 per atom still ships 5 and leaves 7" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneOf(mixedFixture(true), {5});
    for (const Atom& atom : input.atoms) CHECK(atom.components.size() == 1);
    const M5LaneOutcome outcome = planWith(input);
    CHECK(shippedUnits(outcome, input) == 5.0);
    CHECK(leftUnits(outcome, input) == 7.0);
    REQUIRE(outcome.selected);
    size_t componentRecords = 0;
    for (const TruckPlan& truck : outcome.selected->trucks) {
        for (const size_t atom : truck.atoms) componentRecords += input.atoms[atom].components.size();
    }
    CHECK(componentRecords == 2);  // two records, five unit loads: the count is the quantity, not the records
    checkWholeAndValid(outcome, input);
    noteService(5, 7, outcome, input);
}

TEST_CASE("X-PAT-16/invalidCountsAndCaps: the cap domain is 1..2^63-1 and counts never wrap" * doctest::test_suite("m5")) {
    for (const string& text : {string("1"), string("5"), string("68"), string("9223372036854775807")}) {
        const UnitLoadCap cap = unitLoadCapFromText(text);
        CHECK(cap.value == stoll(text));
        CHECK(cap.error.empty());
    }
    CHECK(unitLoadCapFromText("9223372036854775807").value == kMaxCap);
    for (const string& text : {string(""), string("abc"), string("5.5"), string("0"), string("-3"), string("+5"), string(" 5"),
                               string("1e3"), string("NaN"), string("inf"), string("9223372036854775808")}) {
        INFO(text);
        const UnitLoadCap cap = unitLoadCapFromText(text);
        CHECK_FALSE(cap.value);
        CHECK_FALSE(cap.error.empty());
    }
    CHECK(unitLoadCapFromNumber(68.0).value == 68);
    for (const double bad : {0.0, -1.0, 5.5, numeric_limits<double>::quiet_NaN(), numeric_limits<double>::infinity(),
                             -numeric_limits<double>::infinity(), 9223372036854775808.0}) {
        CHECK_FALSE(unitLoadCapFromNumber(bad).value);
    }
    const nlohmann::json block = nlohmann::json::parse(
        R"({"max": 9223372036854775807, "past": 9223372036854775808, "half": 5.5, "zero": 0, "negative": -5,
            "text": "5", "empty": null, "five": 5})");
    CHECK(unitLoadCapFromJson(block, "max").value == kMaxCap);
    CHECK(unitLoadCapFromJson(block, "five").value == 5);
    for (const char* field : {"past", "half", "zero", "negative", "text", "empty", "missing"}) {
        INFO(field);
        CHECK_FALSE(unitLoadCapFromJson(block, field).value);
    }
    // Count-only seam controls: checked, never wrapped.
    CHECK_FALSE(addCounts(kMaxCap, 1));
    CHECK_FALSE(multiplyCounts(2, int64_t{1} << 62));
    CHECK(multiplyCounts(1, int64_t{1} << 62) == (int64_t{1} << 62));
    CHECK(addCounts(kMaxCap - 1, 1) == kMaxCap);
    CHECK(capInHundredths(kMaxCap) == kMaxCap);  // saturates: no total can pass it
    // Quantities count in hundredths, rounded up off the step, so a load is never understated.
    CHECK(hundredthsOfUnitLoads(0.5) == 50);
    CHECK(hundredthsOfUnitLoads(2.0) == 200);
    CHECK(hundredthsOfUnitLoads(0.8109) == 82);
    CHECK(hundredthsOfUnitLoads(1.0 + 1e-13) == 100);
    CHECK_FALSE(hundredthsOfUnitLoads(-1.0));
    CHECK_FALSE(hundredthsOfUnitLoads(numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE(hundredthsOfUnitLoads(numeric_limits<double>::infinity()));
    // An invalid cap reaches no planning and changes nothing.
    for (const int64_t bad : {int64_t{0}, int64_t{-5}}) {
        const M5LaneDateInput input = laneOf(mixedFixture(), {bad});
        const M5LaneOutcome outcome = planWith(input);
        CHECK(outcome.kind == OutcomeKind::ConfigurationIncomplete);
        CHECK_FALSE(outcome.selected);
        CHECK_FALSE(outcome.delta);
        CHECK(outcome.leftOver.size() == input.atoms.size());
        CHECK(all_of(outcome.leftOver.begin(), outcome.leftOver.end(),
                     [](const LeftOver& left) { return left.cause == LeftOverCause::ConfigurationIncomplete; }));
    }
    m5Evidence::note("expected", "1/5/68/2^63-1 accepted; empty, text, 5.5, 0, negative, NaN, infinity, 2^63 refused; overflow refused");
    m5Evidence::note("actual", "as expected (assertions above)");
}

TEST_CASE("X-PAT-16/singleAtomOverCap: a six-unit atom at cap 5 is left whole; at cap 6 it ships" * doctest::test_suite("m5")) {
    const M5LaneDateInput atCap5 = laneOf({capAtom("S", 6)}, {5});
    const M5LaneOutcome refused = planWith(atCap5);
    CHECK(shippedUnits(refused, atCap5) == 0.0);
    CHECK(leftUnits(refused, atCap5) == 6.0);
    REQUIRE(refused.leftOver.size() == 1);
    CHECK(refused.leftOver.front().cause == LeftOverCause::Unfittable);
    checkWholeAndValid(refused, atCap5);
    const M5LaneDateInput atCap6 = laneOf({capAtom("S", 6)}, {6});
    const M5LaneOutcome shipped = planWith(atCap6);
    CHECK(shippedUnits(shipped, atCap6) == 6.0);
    CHECK(shipped.leftOver.empty());
    checkWholeAndValid(shipped, atCap6);
    m5Evidence::note("expected", {{"cap5", {{"shipped", 0}, {"left", 6}}}, {"cap6", {{"shipped", 6}, {"left", 0}}}});
    m5Evidence::note("actual", {{"cap5", {{"shipped", shippedUnits(refused, atCap5)}, {"left", leftUnits(refused, atCap5)}}},
                                {"cap6", {{"shipped", shippedUnits(shipped, atCap6)}, {"left", leftUnits(shipped, atCap6)}}}});
}

// ---- X-PAT-17 --------------------------------------------------------------------------------------------

namespace {

void checkBigSmall(const M5LaneOutcome& outcome, const M5LaneDateInput& input, size_t bigSlot, size_t smallSlot) {
    const vector<int64_t> caps{input.pool.slots[0].maxUnitLoads, input.pool.slots[1].maxUnitLoads};
    CHECK(oracleBestService(unitsList(input), caps) == 6.0);
    CHECK(shippedUnits(outcome, input) == 6.0);
    CHECK(leftUnits(outcome, input) == 10.0);
    const map<size_t, double> bySlot = unitsBySlot(outcome, input);
    CHECK(bySlot.size() == 2);
    CHECK(bySlot.count(bigSlot) == 1);
    CHECK(bySlot.count(smallSlot) == 1);
    if (bySlot.count(bigSlot)) CHECK(bySlot.at(bigSlot) == 4.0);
    if (bySlot.count(smallSlot)) CHECK(bySlot.at(smallSlot) == 2.0);
    checkWholeAndValid(outcome, input);
}

}  // namespace

TEST_CASE("X-PAT-17/methodA: eight T2 atoms on S-big cap 5 and S-small cap 3 ship 6 through method A" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneOf(capAtoms("A", 8, 2), {5, 3});
    const M5LaneDateInput untouched = input;
    const M5LaneOutcome outcome = planWith(input, MethodPolicy::PatternOnly);
    checkBigSmall(outcome, input, 0, 1);
    CHECK(buildSnapshot(input).snapshot->id == buildSnapshot(untouched).snapshot->id);
    const M5LaneOutcome both = planWith(input);
    CHECK(shippedUnits(both, input) == 6.0);  // the selected total, never A's plus B's
    noteService(6, 10, outcome, input);
}

TEST_CASE("X-PAT-17/methodB: the same fixture ships 6 through method B and through final selection" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneOf(capAtoms("A", 8, 2), {5, 3});
    const M5LaneDateInput untouched = input;
    const M5LaneOutcome outcome = planWith(input, MethodPolicy::BlocksOnly);
    checkBigSmall(outcome, input, 0, 1);
    CHECK(buildSnapshot(input).snapshot->id == buildSnapshot(untouched).snapshot->id);
    const M5LaneOutcome both = planWith(input);
    checkBigSmall(both, input, 0, 1);
    noteService(6, 10, outcome, input);
}

TEST_CASE("X-PAT-17/copiedPatternRepair: a two-atom pattern copied onto S-small is rejected and repaired by one whole atom" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneOf(capAtoms("A", 8, 2), {5, 3});
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const M5PolicyProfile profile = syntheticProfile();
    CandidatePlan copied = candidateOf(input, {{0, {0, 1}}, {1, {2, 3}}});
    copied.evidence.proof = ProofStatus::ProvenOptimal;
    copied.validation = validateFinal(copied, snapshot, profile, physicalSettings());
    CHECK_FALSE(copied.validation->allPassed());
    CHECK_FALSE(hasCapFinding(*copied.validation, 0));
    CHECK(hasCapFinding(*copied.validation, 1));
    string error;
    CHECK_FALSE(buildDelta(copied, snapshot, profile, &error));

    const uint64_t before = copied.revision;
    {
        CandidateEditor editor(copied);
        editor.removeToLeftOver(1, 3, LeftOverCause::OverReservation);
    }
    CHECK(copied.revision == before + 1);
    CHECK_FALSE(copied.validation);                         // stale validation dropped
    CHECK(copied.evidence.proof != ProofStatus::ProvenOptimal);  // stale proof dropped
    copied.validation = validateFinal(copied, snapshot, profile, physicalSettings());
    CHECK(copied.validation->allPassed());
    CHECK(snapshot.input.pool.slots[1].maxUnitLoads == 3);  // the slot's cap never changed
    CHECK(count_if(copied.leftOver.begin(), copied.leftOver.end(), [](const LeftOver& left) { return left.atom == 3; }) == 1);
    CHECK(unitsOf(input, copied.trucks[0].atoms) + unitsOf(input, copied.trucks[1].atoms) == 6.0);
    CHECK(reconcile(snapshot, copied).balanced);
    CHECK(buildDelta(copied, snapshot, profile, &error));
    m5Evidence::note("expected", {{"rejected", "S-small at 4 of cap 3"}, {"repaired", 6}, {"restored", "atom A4, 2 unit loads, once"}});
    m5Evidence::note("actual", {{"repairedShipped", unitsOf(input, copied.trucks[0].atoms) + unitsOf(input, copied.trucks[1].atoms)},
                                {"revision", copied.revision}});
}

TEST_CASE("X-PAT-17/stoppedRepair: a repair stopped before revalidation never commits; the checked incumbent stands" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneOf(capAtoms("A", 8, 2), {5, 3});
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    const M5PolicyProfile profile = syntheticProfile();
    CandidatePlan incumbent = candidateOf(input, {{0, {0, 1}}});
    incumbent.validation = validateFinal(incumbent, snapshot, profile, physicalSettings());
    REQUIRE(incumbent.validation->allPassed());

    CandidatePlan copied = candidateOf(input, {{0, {4, 5}}, {1, {6, 7}}});
    {
        CandidateEditor editor(copied);
        editor.removeToLeftOver(1, 7, LeftOverCause::OverReservation);
    }
    copied.validation = validateFinal(copied, snapshot, profile, physicalSettings(), [] { return true; });
    CHECK_FALSE(copied.validation->mandatoryChecksComplete);
    CHECK_FALSE(copied.validation->allPassed());
    string error;
    CHECK_FALSE(buildDelta(copied, snapshot, profile, &error));

    OperationalState state;
    state.snapshot = snapshot.id;
    state.reservationVersion = snapshot.input.pool.version;
    state.slotsReserved = snapshot.input.pool.slots.size();
    const OperationalState untouched = state;
    CHECK(state.consumedAtoms == untouched.consumedAtoms);
    CHECK(state.slotsUsed == 0);
    const optional<CommitDelta> delta = buildDelta(incumbent, snapshot, profile, &error);
    REQUIRE(delta);
    CommitLedger ledger;
    CHECK(ledger.applyIfCurrent(*delta, snapshot, state) == ApplyStatus::Applied);
    CHECK(state.consumedAtoms == set<string>{"A1", "A2"});
    CHECK(state.slotsUsed == 1);
    m5Evidence::note("expected", "no commit from the stopped repair; the earlier checked 4-unit incumbent commits");
    m5Evidence::note("actual", {{"stoppedComplete", copied.validation->mandatoryChecksComplete}, {"committed", state.consumedAtoms}});
}

TEST_CASE("X-PAT-17/smallCapFour: S-small at cap 4 takes a second whole atom: 8 ship, 8 left" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneOf(capAtoms("A", 8, 2), {5, 4});
    const M5LaneOutcome outcome = planWith(input);
    CHECK(oracleBestService(unitsList(input), {5, 4}) == 8.0);
    CHECK(shippedUnits(outcome, input) == 8.0);
    CHECK(leftUnits(outcome, input) == 8.0);
    const map<size_t, double> bySlot = unitsBySlot(outcome, input);
    CHECK(bySlot == map<size_t, double>{{0, 4.0}, {1, 4.0}});
    checkWholeAndValid(outcome, input);
    noteService(8, 8, outcome, input);
}

TEST_CASE("X-PAT-17/slotOrder: reversed slots and sources still bind caps 5/3 to their own slots" * doctest::test_suite("m5")) {
    vector<Atom> atoms = capAtoms("A", 8, 2);
    reverse(atoms.begin(), atoms.end());
    const M5LaneDateInput input = laneOf(atoms, {3, 5});
    const M5LaneOutcome outcome = planWith(input);
    checkBigSmall(outcome, input, 1, 0);
    for (const MethodPolicy method : {MethodPolicy::PatternOnly, MethodPolicy::BlocksOnly}) checkBigSmall(planWith(input, method), input, 1, 0);
    noteService(6, 10, outcome, input);
}

// ---- X-STATE-11 ------------------------------------------------------------------------------------------

TEST_CASE("X-STATE-11/capABA: caps 7, 5, 7 on six T2 atoms give 6/4/6, warm cache equal to cold" * doctest::test_suite("m5")) {
    GeometryCache warm;
    vector<double> planned, warmRuns, coldRuns;
    for (const int64_t cap : {7, 5, 7}) {
        const M5LaneDateInput input = laneOf(capAtoms("A", 6, 2), {cap});
        const M5LaneOutcome outcome = planWith(input);
        planned.push_back(shippedUnits(outcome, input));
        CHECK(leftUnits(outcome, input) == 12.0 - planned.back());
        CHECK(planned.back() == 2.0 * floor(static_cast<double>(cap) / 2.0));
        checkWholeAndValid(outcome, input);
        warmRuns.push_back(patternServiceWithCache(input, warm));
        GeometryCache cold;
        coldRuns.push_back(patternServiceWithCache(input, cold));
    }
    CHECK(planned == vector<double>{6.0, 4.0, 6.0});
    CHECK(warmRuns == planned);
    CHECK(coldRuns == planned);
    // The cap-7 six-unit load still fits its coordinates, mass and height, but not cap 5.
    const M5LaneDateInput cap5 = laneOf(capAtoms("A", 6, 2), {5});
    const ValidationReport report =
        validateFinal(candidateOf(cap5, {{0, {0, 1, 2}}}), *buildSnapshot(cap5).snapshot, syntheticProfile(), physicalSettings());
    CHECK_FALSE(report.allPassed());
    CHECK(hasCapFinding(report, 0));
    m5Evidence::note("expected", {6, 4, 6});
    m5Evidence::note("actual", {{"planLaneDate", planned}, {"warmCache", warmRuns}, {"coldCache", coldRuns}});
}

TEST_CASE("X-STATE-11/sameIdentityRevision: the same named profile and trailer with a revised cap is a new context" * doctest::test_suite("m5")) {
    const M5LaneDateInput cap7 = laneOf(capAtoms("A", 6, 2), {7});
    const M5LaneDateInput cap5 = laneOf(capAtoms("A", 6, 2), {5});
    CHECK(cap7.sourceVersion == cap5.sourceVersion);
    CHECK(cap7.pool.version == cap5.pool.version);
    const DemandSnapshot snapshot7 = *buildSnapshot(cap7).snapshot;
    const DemandSnapshot snapshot5 = *buildSnapshot(cap5).snapshot;
    CHECK(snapshot7.id != snapshot5.id);
    CHECK(cap7.pool.slots[0].signature() != cap5.pool.slots[0].signature());
    const M5PolicyProfile profile = syntheticProfile();
    CandidatePlan six = candidateOf(cap7, {{0, {0, 1, 2}}});
    six.validation = validateFinal(six, snapshot7, profile, physicalSettings());
    CHECK(six.validation->allPassed());
    CHECK(validationCurrent(six, snapshot7, profile));
    CHECK_FALSE(validationCurrent(six, snapshot5, profile));
    string error;
    CHECK_FALSE(buildDelta(six, snapshot5, profile, &error));
    for (const int64_t cap : {7, 5, 7}) {
        const M5LaneDateInput input = laneOf(capAtoms("A", 6, 2), {cap});
        CHECK(shippedUnits(planWith(input), input) == 2.0 * floor(static_cast<double>(cap) / 2.0));
    }
    m5Evidence::note("expected", "stable names and dimensions do not hide the revised cap: the cap-7 validation is not current under cap 5");
    m5Evidence::note("actual", {{"snapshotCap7", snapshot7.id}, {"snapshotCap5", snapshot5.id}, {"refusal", error}});
}

TEST_CASE("X-STATE-11/stalePrecommit: a six-unit delta prepared under cap 7 is refused under cap 5 without consuming anything" * doctest::test_suite("m5")) {
    const M5LaneDateInput cap7 = laneOf(capAtoms("A", 6, 2), {7});
    const M5LaneDateInput cap5 = laneOf(capAtoms("A", 6, 2), {5});
    const M5PolicyProfile profile = syntheticProfile();
    const M5LaneOutcome prepared = planWith(cap7);
    REQUIRE(prepared.delta);
    CHECK(shippedUnits(prepared, cap7) == 6.0);

    const DemandSnapshot current = *buildSnapshot(cap5).snapshot;
    OperationalState state;
    state.snapshot = current.id;
    state.reservationVersion = current.input.pool.version;
    state.slotsReserved = current.input.pool.slots.size();
    CommitLedger ledger;
    CHECK(ledger.applyIfCurrent(*prepared.delta, current, state) == ApplyStatus::StaleSnapshot);
    CHECK(state.consumedAtoms.empty());
    CHECK(state.slotsUsed == 0);
    CHECK(state.snapshot == current.id);

    const M5LaneOutcome replanned = planWith(cap5);
    REQUIRE(replanned.delta);
    CHECK(shippedUnits(replanned, cap5) == 4.0);
    CHECK(ledger.applyIfCurrent(*replanned.delta, current, state) == ApplyStatus::Applied);
    CHECK(state.consumedAtoms.size() == 2);
    CHECK(state.slotsUsed == 1);
    m5Evidence::note("expected", {{"staleApply", "StaleSnapshot, nothing consumed"}, {"replanned", 4}});
    m5Evidence::note("actual", {{"replannedShipped", shippedUnits(replanned, cap5)}, {"consumed", state.consumedAtoms}});
}

// ---- X-CG-21 ---------------------------------------------------------------------------------------------

TEST_CASE("X-CG-21/exactCapacity: seven T2 atoms on four cap-5 slots ship all 14 on four trucks, 4/4/4/2" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneOf(capAtoms("A", 7, 2), {5, 5, 5, 5});
    const M5LaneOutcome outcome = planWith(input);
    CHECK(oracleFewestTrucks(unitsList(input), 5, 4) == 4);
    CHECK(shippedUnits(outcome, input) == 14.0);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 4);
    vector<double> loads;
    for (const TruckPlan& truck : outcome.selected->trucks) loads.push_back(unitsOf(input, truck.atoms));
    sort(loads.begin(), loads.end());
    CHECK(loads == vector<double>{2.0, 4.0, 4.0, 4.0});
    vector<size_t> all(input.atoms.size());
    for (size_t index = 0; index < all.size(); ++index) all[index] = index;
    CHECK(trucksAtLeast(input, all, input.pool.slots.front()) == 4);  // ceil(7 / floor(5/2))
    checkWholeAndValid(outcome, input);
    m5Evidence::note("expected", {{"trucks", 4}, {"loads", {4, 4, 4, 2}}, {"shipped", 14}});
    m5Evidence::note("actual", {{"trucks", loads.size()}, {"loads", loads}, {"shipped", shippedUnits(outcome, input)}});
}

TEST_CASE("X-CG-21/weakBoundTrigger: the relaxation B=3 below a checked F=4 triggers careful work and proves nothing" * doctest::test_suite("m5")) {
    const long long relaxation = static_cast<long long>(ceil(14.0 / 5.0));
    CHECK(relaxation == 3);
    ScalarModel model;
    model.sizes.assign(7, 2);
    model.capacity = 5;
    const long long ffd = static_cast<long long>(scalarFirstFitDecreasing(model).bins.size());
    CHECK(ffd == 4);
    CHECK(shouldEscalate(ffd, relaxation, 1, true));
    CHECK(oracleFewestTrucks(vector<int>(7, 2), 5, 3) == 0);  // no three-truck plan exists: B is a bound, not a plan
    CHECK(oracleFewestTrucks(vector<int>(7, 2), 5, 4) == 4);  // so F = 4 is not shown suboptimal by B
    m5Evidence::note("expected", {{"B", 3}, {"F", 4}, {"escalate", true}, {"threeTruckPlans", 0}});
    m5Evidence::note("actual", {{"B", relaxation}, {"F", ffd}, {"escalate", shouldEscalate(ffd, relaxation, 1, true)}});
}

TEST_CASE("X-CG-21/matchingBoundStop: F=4 against the same-model bound 4 stops count escalation" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneOf(capAtoms("A", 7, 2), {5, 5, 5, 5});
    vector<size_t> all(input.atoms.size());
    for (size_t index = 0; index < all.size(); ++index) all[index] = index;
    const long long bound = trucksAtLeast(input, all, input.pool.slots.front());
    CHECK(bound == static_cast<long long>(oracleFewestTrucks(unitsList(input), 5, 4)));
    CHECK_FALSE(shouldEscalate(4, bound, 1, true));
    const M5LaneOutcome outcome = planWith(input);
    REQUIRE(outcome.escalation.bound);
    CHECK(*outcome.escalation.bound == 4);
    REQUIRE(outcome.escalation.quickTrucks);
    CHECK(*outcome.escalation.quickTrucks == 4);
    CHECK_FALSE(outcome.escalation.escalated);
    REQUIRE(outcome.methods.size() == 2);
    CHECK_FALSE(outcome.methods[1].ran);
    CHECK(outcome.proof == ProofStatus::ProvenOptimal);  // all-required count only, as its scope says
    m5Evidence::note("expected", {{"bound", 4}, {"quick", 4}, {"escalated", false}});
    m5Evidence::note("actual", {{"bound", *outcome.escalation.bound}, {"quick", *outcome.escalation.quickTrucks},
                                {"escalated", outcome.escalation.escalated}, {"proofScope", outcome.proofScope}});
}

TEST_CASE("X-CG-21/invalidQuickCandidate: a 2/2/3-atom plan that ignores the cap is never a feasible incumbent" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneOf(capAtoms("A", 7, 2), {5, 5, 5, 5});
    const DemandSnapshot snapshot = *buildSnapshot(input).snapshot;
    CandidatePlan three = candidateOf(input, {{0, {0, 1}}, {1, {2, 3}}, {2, {4, 5, 6}}});
    three.evidence.incumbent = 3;
    three.evidence.lowerBound = 3;
    three.evidence.boundModel = three.evidence.proofModel = "capacityOnly";
    three.evidence.proof = ProofStatus::ProvenOptimal;
    const ValidationReport report = validateFinal(three, snapshot, syntheticProfile(), physicalSettings());
    CHECK_FALSE(report.allPassed());
    CHECK(hasCapFinding(report, 2));
    CHECK(reconcile(snapshot, three).balanced);  // every good reconciles, and it is still invalid
    three.validation = report;
    string error;
    CHECK_FALSE(buildDelta(three, snapshot, syntheticProfile(), &error));
    const ValidationReport one =
        validateFinal(candidateOf(input, {{0, {0, 1, 2, 3, 4, 5, 6}}}), snapshot, syntheticProfile(), physicalSettings());
    CHECK_FALSE(one.allPassed());
    CHECK(hasCapFinding(one, 0));
    m5Evidence::note("expected", "both cap-ignorant candidates rejected; no dispatch, no zero-gap proof");
    m5Evidence::note("actual", {{"threeTruckPassed", report.allPassed()}, {"oneTruckPassed", one.allPassed()}});
}

TEST_CASE("X-CG-21/threeSlots: three cap-5 slots cannot carry all 14; maximum service ships 12 and leaves 2" * doctest::test_suite("m5")) {
    const M5LaneDateInput input = laneOf(capAtoms("A", 7, 2), {5, 5, 5});
    vector<size_t> all(input.atoms.size());
    for (size_t index = 0; index < all.size(); ++index) all[index] = index;
    CHECK(trucksAtLeast(input, all, input.pool.slots.front()) == 4);  // all-required model infeasible on three
    CHECK(oracleFewestTrucks(unitsList(input), 5, 3) == 0);
    const M5LaneOutcome outcome = planWith(input);
    CHECK(oracleBestService(unitsList(input), {5, 5, 5}) == 12.0);
    CHECK(shippedUnits(outcome, input) == 12.0);
    CHECK(leftUnits(outcome, input) == 2.0);
    REQUIRE(outcome.selected);
    CHECK(outcome.selected->trucks.size() == 3);
    CHECK(outcome.offeredService == ServiceLevel::Partial);
    CHECK(outcome.proof != ProofStatus::ProvenOptimal);
    REQUIRE(outcome.leftOver.size() == 1);
    CHECK(outcome.leftOver.front().cause == LeftOverCause::OverReservation);
    checkWholeAndValid(outcome, input);
    noteService(12, 2, outcome, input);
}
