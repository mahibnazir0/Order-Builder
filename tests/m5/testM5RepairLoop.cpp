// Bounded repair (tests v12 X-REB-10, R-1c/invalidDonorRestore) on FX-30: 50 source units of one group
// and two reserved trucks, checked as 30 + 20. Injected proposals edit copies through CandidateEditor; the
// oracles keep their own per-source ledger and judge each published floor with the independent layout
// checker. They never call production code.
#include "doctest.h"
#include "m5Allocation.hpp"
#include "m5Ledger.hpp"
#include "m5Physical.hpp"
#include "m5Repair.hpp"
#include "m5Validation.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Fixtures.hpp"
#include "support/m5LayoutChecker.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using namespace ob::m5;
using namespace m5Fixtures;

namespace {

struct RepairFixture {
    M5LaneDateInput input = laneWith(fx30Units("u", 50), 2);
    DemandSnapshot snapshot;
    M5PolicyProfile profile = syntheticProfile();
    PhysicalSettings physical = physicalSettings();
    unique_ptr<AllocationComparator> comparator;
    unique_ptr<UrgencyDomain> domain;
    CandidatePlan checked;  // 30 + 20, validated on its own revision

    RepairFixture() {
        snapshot = *buildSnapshot(input).snapshot;
        comparator = makeComparator(ComparatorKind::M4Urgency, tieOrderFor(snapshot));
        domain = make_unique<UrgencyDomain>(snapshot);
        TruckPlan first, second;
        first.slot = 0;
        second.slot = 1;
        first.group = second.group = "A";
        for (size_t atom = 0; atom < 30; ++atom) first.atoms.push_back(atom);
        for (size_t atom = 30; atom < 50; ++atom) second.atoms.push_back(atom);
        for (TruckPlan* truck : {&first, &second}) {
            truck->layout = checkTruck(input, truck->atoms, input.pool.slots[truck->slot], physical).layout;
        }
        checked.trucks = {first, second};
        checked.validation = validateFinal(checked, snapshot, profile, physical);
        REQUIRE(checked.validation->allPassed());
    }

    RepairRun run(const CandidatePlan& start, const RepairProposal& propose, size_t cap, const StopCheck& stop = StopCheck()) const {
        return runRepair(start, snapshot, profile, physical, *comparator, *domain, propose, cap, stop);
    }
};

// Moves `atom` to whichever of the two trucks does not hold it.
bool toggleAtom(CandidatePlan& work, size_t atom) {
    const bool inFirst = find(work.trucks[0].atoms.begin(), work.trucks[0].atoms.end(), atom) != work.trucks[0].atoms.end();
    CandidateEditor editor(work);
    editor.moveAtom(inFirst ? 0 : 1, inFirst ? 1 : 0, atom);
    return true;
}

// Every source unit shipped exactly once over the plan's trucks, plus the left-over ones.
map<string, int> unitsPerSource(const M5LaneDateInput& input, const CandidatePlan& plan) {
    map<string, int> units;
    for (const TruckPlan& truck : plan.trucks) {
        for (const size_t atom : truck.atoms) units[input.atoms[atom].components.front().source] += 1;
    }
    for (const LeftOver& left : plan.leftOver) units[input.atoms[left.atom].components.front().source] += 1;
    return units;
}

void checkPublishable(const RepairFixture& fixture, const CandidatePlan& plan) {
    REQUIRE(plan.validation);
    CHECK(plan.validation->revision == plan.revision);
    CHECK(plan.validation->allPassed());
    CHECK(plan.trucks.size() == 2);
    size_t shipped = 0;
    for (const TruckPlan& truck : plan.trucks) {
        shipped += truck.atoms.size();
        vector<ob::FloorItem> items;
        for (const size_t atom : truck.atoms) items.push_back(fixture.input.atoms[atom].floor);
        const TrailerCapability& slot = fixture.input.pool.slots[truck.slot];
        CHECK(m5LayoutChecker::witnessProblems(truck.layout, items, slot.lengthIn, slot.widthIn, slot.heightIn,
                                               fixture.physical.floor).empty());
    }
    CHECK(shipped == 50);
    const map<string, int> units = unitsPerSource(fixture.input, plan);
    CHECK(units.size() == 50);
    CHECK(all_of(units.begin(), units.end(), [](const auto& entry) { return entry.second == 1; }));
}

}  // namespace

TEST_CASE("X-REB-10/oscillatingRepairCycle: a repair alternating 30+20 and 29+21 stops by repeat or cap 3 with one checked incumbent"
          * doctest::test_suite("m5")) {
    const RepairFixture fixture;
    const CandidatePlan before = fixture.checked;
    vector<size_t> proposals;
    const RepairRun run = fixture.run(fixture.checked, [&](CandidatePlan& work, size_t iteration) {
        proposals.push_back(iteration);
        return toggleAtom(work, 29);
    }, 3);
    CHECK(run.iterations <= 3);
    CHECK(proposals.size() == run.iterations);
    CHECK((run.stopReason == "repeated arrangement" || run.stopReason == "iteration cap"));
    CHECK(run.stopReason == "repeated arrangement");
    REQUIRE(run.incumbent);
    checkPublishable(fixture, *run.incumbent);
    // The arrangements alternate without raising the service: 50 units ship either way, on both slots.
    multiset<size_t> loads;
    for (const TruckPlan& truck : run.incumbent->trucks) loads.insert(truck.atoms.size());
    CHECK((loads == multiset<size_t>{20, 30} || loads == multiset<size_t>{21, 29}));
    CHECK(run.incumbent->evidence.proof != ProofStatus::ProvenOptimal);
    // Every revision judged is newer than the start; the start itself is untouched.
    CHECK(is_sorted(run.checkedRevisions.begin(), run.checkedRevisions.end()));
    for (const uint64_t revision : run.checkedRevisions) CHECK(revision > before.revision);
    CHECK(fixture.checked.revision == before.revision);
    CHECK(fixture.checked.trucks[0].atoms == before.trucks[0].atoms);
    m5Evidence::note("expected", {{"stop", "repeated arrangement or cap 3"}, {"shipped", 50}, {"slots", 2}});
    m5Evidence::note("actual", {{"stop", run.stopReason}, {"iterations", run.iterations}, {"shipped", 50}, {"loads", vector<size_t>(loads.begin(), loads.end())}});
}

TEST_CASE("R-1c/invalidDonorRestore: an edit overloading the recipient, stopped before revalidation, is discarded; 30+20 stays"
          * doctest::test_suite("m5")) {
    const RepairFixture fixture;
    // Moving a unit from the 20-truck to the 30-truck makes 31 on one floor: the donor's witness and the
    // recipient's capacity no longer hold.
    bool edited = false;
    const RepairProposal overload = [&](CandidatePlan& work, size_t) {
        CandidateEditor editor(work);
        editor.moveAtom(1, 0, 30);
        edited = true;
        return true;
    };
    const RepairRun stopped = fixture.run(fixture.checked, overload, 3, [&] { return edited; });
    CHECK(stopped.stopReason == "stopped");
    CHECK(stopped.checkedRevisions.empty());
    REQUIRE(stopped.incumbent);
    CHECK(stopped.incumbent->revision == fixture.checked.revision);
    CHECK(stopped.incumbent->trucks[0].atoms.size() == 30);
    CHECK(stopped.incumbent->trucks[1].atoms.size() == 20);
    checkPublishable(fixture, *stopped.incumbent);

    // Not stopped: the edit is validated, fails, and the checked plan still stands.
    edited = false;
    const RepairRun validated = fixture.run(fixture.checked, overload, 1);
    CHECK(validated.checkedRevisions.size() == 1);
    REQUIRE(validated.incumbent);
    CHECK(validated.incumbent->revision == fixture.checked.revision);
    checkPublishable(fixture, *validated.incumbent);

    // With no checked candidate to fall back on there is no plan, never the unchecked load.
    CandidatePlan unchecked = fixture.checked;
    unchecked.validation.reset();
    edited = false;
    const RepairRun none = fixture.run(unchecked, overload, 3, [&] { return edited; });
    CHECK_FALSE(none.incumbent.has_value());
    m5Evidence::note("expected", {{"stoppedBeforeCheck", "30+20 kept"}, {"failedCheck", "30+20 kept"}, {"noIncumbent", "no plan"}});
    m5Evidence::note("actual", {{"stoppedBeforeCheck", stopped.stopReason}, {"failedCheck", validated.checkedRevisions.size()},
                                {"noIncumbent", none.incumbent.has_value() ? "plan" : "no plan"}});
}
