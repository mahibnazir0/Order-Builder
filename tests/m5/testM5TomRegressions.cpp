// Tom's exact review cases (v12 Section 3: R-1a, R-1b, R-5a, R-8, R-9). Each runs through corrected M4
// (selectLoads, and planShipDates where the case says so) and through M5's direct seam (planLaneDate).
//
// The checks recount from the scenario's own numbers. They never call the floor-pattern or truck-check
// code that produced a plan: layouts go through m5LayoutChecker, and the best possible unit-load count
// comes from an exhaustive search of every grouping into rows (rowOracle below). Tom's input files are
// compared with their recorded SHA-256 before use.
#include "doctest.h"
#include "json.hpp"
#include "m5UnitLoads.hpp"
#include "shipDatePlanner.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5Fixtures.hpp"
#include "support/m5LayoutChecker.hpp"
#include "support/m5Oracles.hpp"
#include "support/m5Sha256.hpp"
#include "../m4SyntheticDay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using json = nlohmann::json;

namespace {

const string kFixtureRoot = "tests/fixtures/tomReview/";
constexpr double kInfinity = numeric_limits<double>::infinity();
constexpr double kTolerance = 1e-9;
constexpr double kMaxTrailerLengthIn = 630.0;

// One fixed prebuilt stack, as Tom's files describe it.
struct Stack {
    string product;
    double longIn = 0.0;
    double shortIn = 0.0;
    double heightIn = 0.0;
    double weightLb = 0.0;
    int unitLoads = 1;
    int priority = 0;
};

struct Scenario {
    string name;
    double lengthIn = 630.0;
    double widthIn = 100.0;
    double heightIn = 108.0;
    double payloadLb = 45000.0;
    double unitLoadCap = 64.0;
    vector<Stack> stacks;
};

// ---- Tom's files, checked against SHA256SUMS.txt before use -------------------------------------------------

json loadTomFile(const string& relativePath) {
    ifstream sums(kFixtureRoot + "SHA256SUMS.txt");
    REQUIRE_MESSAGE(sums.good(), "missing " << kFixtureRoot << "SHA256SUMS.txt");
    string expected;
    string line;
    while (getline(sums, line)) {
        if (line.size() > 66 && line.substr(66) == relativePath) expected = line.substr(0, 64);
    }
    REQUIRE_MESSAGE(!expected.empty(), relativePath << " is not in SHA256SUMS.txt");
    const string path = kFixtureRoot + relativePath;
    const optional<string> actual = m5Checksum::fileSha256Hex(path);
    REQUIRE_MESSAGE(actual.has_value(), "missing " << path);
    REQUIRE_MESSAGE(*actual == expected, path << " differs from the recorded SHA-256");
    m5Evidence::input(relativePath, path);
    ifstream file(path);
    return json::parse(file);
}

Scenario scenarioFromCaseFile(const json& file) {
    Scenario scenario;
    scenario.name = file.at("case").get<string>();
    const json& trailer = file.at("trailer");
    scenario.lengthIn = trailer.at("length_in").get<double>();
    scenario.widthIn = trailer.at("width_in").get<double>();
    scenario.heightIn = trailer.at("height_in").get<double>();
    scenario.payloadLb = trailer.at("payload_lb").get<double>();
    scenario.unitLoadCap = trailer.at("max_unit_loads").get<double>();
    for (const json& item : file.at("items")) {
        scenario.stacks.push_back({item.at("product").get<string>(), item.at("length_in").get<double>(),
                                   item.at("width_in").get<double>(), item.at("height_in").get<double>(),
                                   item.at("weight_lb").get<double>(), item.at("unit_loads_per_stack").get<int>(),
                                   item.at("priority").get<int>()});
    }
    return scenario;
}

// The eleven items of floor-proof-results.json, products P100..P110 in file order, on the standard trailer.
Scenario elevenItemScenario(const json& file) {
    Scenario scenario;
    scenario.name = "R-8";
    for (const json& item : file.at("items")) {
        scenario.stacks.push_back({"P" + to_string(100 + scenario.stacks.size()), item.at("long").get<double>(),
                                   item.at("short").get<double>(), item.at("height").get<double>(),
                                   item.at("weight").get<double>(), static_cast<int>(item.at("unit_loads").get<double>()), 0});
    }
    return scenario;
}

// capacity_probe.cpp: 34 two-high stacks of 48 x 36 in, 100 in high, 1,000 lb each.
Scenario capacityScenario(double cap) {
    Scenario scenario;
    scenario.name = "R-9";
    scenario.unitLoadCap = cap;
    for (int index = 0; index < 34; ++index) scenario.stacks.push_back({"P" + to_string(index), 48.0, 36.0, 100.0, 1000.0, 2, 0});
    return scenario;
}

double totalUnitLoads(const Scenario& scenario) {
    double total = 0.0;
    for (const Stack& stack : scenario.stacks) total += stack.unitLoads;
    return total;
}

// ---- The exact oracle: every grouping of stacks into rows ------------------------------------------------------

using Dims = pair<double, double>;  // short side, long side

// Whether a side-by-side or interlocked set of stacks fits widthIn across.
bool within(double acrossIn, double widthIn) { return acrossIn <= widthIn + kTolerance; }

struct Rect {
    double left, right, front, back;
};

bool overlaps(const Rect& first, const Rect& second) {
    return first.left < second.right - kTolerance && second.left < first.right - kTolerance
           && first.front < second.back - kTolerance && second.front < first.back - kTolerance;
}

// The depth of a pinwheel of four stacks in this order: front-left and back-right long side across,
// front-right and back-left turned, each pushed back against the one in front (floorPattern.hpp).
double pinwheelDepth(const array<Dims, 4>& order, double widthIn) {
    const double frontLeftShort = order[0].first, frontLeftLong = order[0].second;
    const double frontRightShort = order[1].first, frontRightLong = order[1].second;
    const double backLeftShort = order[2].first, backLeftLong = order[2].second;
    const double backRightShort = order[3].first, backRightLong = order[3].second;
    const array<Rect, 4> rects{{{0.0, frontLeftLong, 0.0, frontLeftShort},
                                {frontLeftLong, frontLeftLong + frontRightShort, 0.0, frontRightLong},
                                {0.0, backLeftShort, frontLeftShort, frontLeftShort + backLeftLong},
                                {backLeftShort, backLeftShort + backRightLong, frontRightLong, frontRightLong + backRightShort}}};
    double depth = 0.0;
    for (size_t first = 0; first < rects.size(); ++first) {
        if (!within(rects[first].right, widthIn)) return kInfinity;
        depth = max(depth, rects[first].back);
        for (size_t second = first + 1; second < rects.size(); ++second) {
            if (overlaps(rects[first], rects[second])) return kInfinity;
        }
    }
    return depth;
}

// The shallowest supported row for these one to four stacks (short side, long side), widthIn across: the
// patterns of floorPattern.hpp's header, restated here from that description, not from the code that builds them.
double shallowestRow(vector<Dims> dims, double widthIn) {
    sort(dims.begin(), dims.end());
    static map<pair<double, vector<Dims>>, double> cache;
    const auto key = make_pair(widthIn, dims);
    const auto cached = cache.find(key);
    if (cached != cache.end()) return cached->second;

    double best = kInfinity;
    if (dims.size() == 1) {
        const auto [shortIn, longIn] = dims[0];
        if (within(longIn, widthIn)) best = shortIn;
        else if (within(shortIn, widthIn)) best = longIn;
    } else if (dims.size() == 2) {
        for (size_t turnedFirst = 0; turnedFirst < 2; ++turnedFirst) {
            const Dims& first = dims[turnedFirst];
            const Dims& second = dims[1 - turnedFirst];
            if (within(first.second + second.second, widthIn)) best = min(best, max(first.first, second.first));          // wide-wide
            if (within(first.second + second.first, widthIn)) best = min(best, max(first.first, second.second));         // wide-narrow
        }
        if (within(dims[0].first + dims[1].first, widthIn)) best = min(best, max(dims[0].second, dims[1].second));       // narrow-narrow
    } else if (dims.size() == 3) {
        if (within(dims[0].first + dims[1].first + dims[2].first, widthIn)) {
            best = max({dims[0].second, dims[1].second, dims[2].second});                                                  // three across
        }
    } else if (dims.size() == 4) {
        array<size_t, 4> order{0, 1, 2, 3};
        do {
            best = min(best, pinwheelDepth({dims[order[0]], dims[order[1]], dims[order[2]], dims[order[3]]}, widthIn));
        } while (next_permutation(order.begin(), order.end()));
    }
    cache[key] = best;
    return best;
}

struct BestSelection {
    double unitLoads = 0.0;
    double usedLengthIn = kInfinity;  // shortest floor length among the best-unit-load selections
    double fullSetLengthIn = kInfinity;  // shortest floor length that holds every usable stack
};

// The most unit loads any subset of the stacks can ship on one truck, over every grouping into supported rows
// (the layout scope M4 and M5 both claim: wide-wide, wide-narrow, narrow-narrow, three-across, pinwheel, single),
// within the trailer length, under the ceiling, payload and unit-load cap.
BestSelection rowOracle(const Scenario& scenario) {
    const size_t count = scenario.stacks.size();
    REQUIRE(count <= 16);
    const size_t all = (size_t{1} << count) - 1;
    vector<double> rowDepth(all + 1, kInfinity);
    for (size_t mask = 1; mask <= all; ++mask) {
        vector<Dims> dims;
        bool usable = true;
        for (size_t index = 0; index < count && usable; ++index) {
            if ((mask & (size_t{1} << index)) == 0) continue;
            const Stack& stack = scenario.stacks[index];
            usable = stack.heightIn <= scenario.heightIn + kTolerance;
            dims.push_back({min(stack.longIn, stack.shortIn), max(stack.longIn, stack.shortIn)});
        }
        if (!usable || dims.size() > 4) continue;
        rowDepth[mask] = shallowestRow(dims, scenario.widthIn);
    }
    vector<double> packed(all + 1, kInfinity);
    packed[0] = 0.0;
    for (size_t mask = 1; mask <= all; ++mask) {
        size_t lowest = 0;
        while ((mask & (size_t{1} << lowest)) == 0) ++lowest;
        for (size_t part = mask; part != 0; part = (part - 1) & mask) {
            if ((part & (size_t{1} << lowest)) == 0 || !isfinite(rowDepth[part]) || !isfinite(packed[mask ^ part])) continue;
            packed[mask] = min(packed[mask], rowDepth[part] + packed[mask ^ part]);
        }
    }
    BestSelection best;
    for (size_t mask = 1; mask <= all; ++mask) {
        if (packed[mask] > scenario.lengthIn + kTolerance) continue;
        double units = 0.0;
        double weight = 0.0;
        for (size_t index = 0; index < count; ++index) {
            if ((mask & (size_t{1} << index)) == 0) continue;
            units += scenario.stacks[index].unitLoads;
            weight += scenario.stacks[index].weightLb;
        }
        if (weight > scenario.payloadLb + kTolerance || units > scenario.unitLoadCap + kTolerance) continue;
        if (units > best.unitLoads + kTolerance || (fabs(units - best.unitLoads) <= kTolerance && packed[mask] < best.usedLengthIn)) {
            best.unitLoads = units;
            best.usedLengthIn = packed[mask];
        }
    }
    best.fullSetLengthIn = packed[all];
    return best;
}

// ---- What a plan is checked against -----------------------------------------------------------------------------

struct Verdict {
    double unitLoads = 0.0;
    double weightLb = 0.0;
    double lengthIn = 0.0;
    size_t trucks = 0;
    vector<string> shippedProducts;  // sorted
    vector<string> problems;
    bool claimsOptimal = false;
    string proofText;
};

ob::FloorItem floorItemOf(const Stack& stack) {
    return {stack.longIn, stack.shortIn, stack.heightIn, stack.unitLoads, 0.0};
}

// A truck's rows, recounted: weight, unit loads, length and the independent layout check.
void checkTruck(const Scenario& scenario, const vector<size_t>& stackIndices, const ob::FloorLayout& layout,
                const ob::FloorPatternSettings& floor, Verdict& verdict) {
    vector<ob::FloorItem> items;
    double weight = 0.0;
    double units = 0.0;
    for (const size_t index : stackIndices) {
        const Stack& stack = scenario.stacks.at(index);
        items.push_back(floorItemOf(stack));
        weight += stack.weightLb;
        units += stack.unitLoads;
        verdict.shippedProducts.push_back(stack.product);
    }
    for (const string& problem : m5LayoutChecker::witnessProblems(layout, items, scenario.lengthIn, scenario.widthIn,
                                                                  scenario.heightIn, floor)) {
        verdict.problems.push_back(problem);
    }
    for (const ob::FloorRow& row : layout.rows) verdict.lengthIn += row.depthIn;
    if (weight > scenario.payloadLb + kTolerance) verdict.problems.push_back("truck over the payload");
    if (units > scenario.unitLoadCap + kTolerance) verdict.problems.push_back("truck over the unit-load cap");
    verdict.weightLb += weight;
    verdict.unitLoads += units;
    ++verdict.trucks;
}

Verdict checkM4Plan(const Scenario& scenario, const ob::SelectionPlan& plan, const ob::FloorPatternSettings& floor) {
    Verdict verdict;
    vector<int> uses(scenario.stacks.size(), 0);
    for (const ob::PlannedTruck& truck : plan.trucks) {
        for (const size_t index : truck.items) ++uses.at(index);
        checkTruck(scenario, truck.items, truck.floor, floor, verdict);
    }
    for (const ob::LeftOverItem& left : plan.leftOver) ++uses.at(left.itemIndex);
    for (size_t index = 0; index < uses.size(); ++index) {
        if (uses[index] != 1) verdict.problems.push_back("stack " + scenario.stacks[index].product + " counted " + to_string(uses[index]) + " times");
    }
    if (verdict.trucks > 1) verdict.problems.push_back("more trucks than the one reserved");
    verdict.claimsOptimal = plan.routes.at(0).proven;
    verdict.proofText = verdict.claimsOptimal ? "proven" : "unproven: " + plan.routes.at(0).unprovenBecause;
    sort(verdict.shippedProducts.begin(), verdict.shippedProducts.end());
    return verdict;
}

ob::m5::M5LaneDateInput m5InputOf(const Scenario& scenario) {
    vector<ob::m5::Atom> atoms;
    for (size_t index = 0; index < scenario.stacks.size(); ++index) {
        const Stack& stack = scenario.stacks[index];
        ob::m5::Atom atom;
        atom.id = "S" + to_string(index);
        atom.group = "A";
        atom.floor = floorItemOf(stack);
        atom.floor.cubeCubicIn = stack.longIn * stack.shortIn * stack.heightIn;
        atom.weightLb = stack.weightLb;
        atom.unitLoads = stack.unitLoads;
        ob::m5::Component component;
        component.source = "line-" + to_string(index);
        component.product = stack.product;
        component.unitLoads = stack.unitLoads;
        component.dueDate = "2026-08-06";
        component.availableDate = "2026-08-01";
        component.priority = stack.priority;
        atom.components.push_back(component);
        atoms.push_back(atom);
    }
    const ob::m5::TrailerCapability trailer{scenario.lengthIn, scenario.widthIn, scenario.heightIn, scenario.payloadLb,
                                            ob::m5::unitLoadCapFromNumber(scenario.unitLoadCap).value.value_or(0)};
    return m5Fixtures::laneWith(atoms, 1, trailer);
}

struct M5Run {
    ob::m5::M5LaneDateInput input;
    ob::m5::M5LaneOutcome outcome;
    Verdict verdict;
};

M5Run runM5(const Scenario& scenario) {
    M5Run run;
    run.input = m5InputOf(scenario);
    const ob::m5::PhysicalSettings physical = m5Fixtures::physicalSettings();
    m5Fixtures::FakeClock clock;
    const ob::m5::M5RunServices services(clock);
    run.outcome = ob::m5::planLaneDate(run.input, m5Fixtures::syntheticProfile(), physical, services);

    for (const string& violation : m5Oracles::invariantViolations(run.outcome, run.input, physical.floor)) run.verdict.problems.push_back(violation);
    if (run.outcome.kind != ob::m5::OutcomeKind::CompletedWithDispatch) run.verdict.problems.push_back("M5 dispatched nothing");
    if (run.outcome.selected) {
        for (const ob::m5::TruckPlan& truck : run.outcome.selected->trucks) {
            vector<size_t> stackIndices;
            for (const size_t atom : truck.atoms) stackIndices.push_back(static_cast<size_t>(stoul(run.input.atoms[atom].id.substr(1))));
            // The layout's own indices are positions in the truck's atoms, ascending, as validation checks them.
            checkTruck(scenario, stackIndices, truck.layout, physical.floor, run.verdict);
        }
    }
    if (run.verdict.trucks > 1) run.verdict.problems.push_back("more trucks than the one reserved");
    run.verdict.claimsOptimal = run.outcome.proof == ob::m5::ProofStatus::ProvenOptimal;
    run.verdict.proofText = run.outcome.proofScope.empty() ? "no proof claimed" : run.outcome.proofScope;
    sort(run.verdict.shippedProducts.begin(), run.verdict.shippedProducts.end());
    return run;
}

// ---- M4 ----------------------------------------------------------------------------------------------------------

m4SyntheticDay::Day m4DayOf(const Scenario& scenario) {
    vector<ob::SelectionItem> goods;
    for (size_t index = 0; index < scenario.stacks.size(); ++index) {
        const Stack& stack = scenario.stacks[index];
        ob::SelectionItem item = m4SyntheticDay::item(0, index, stack.weightLb, stack.priority, stack.longIn, stack.shortIn);
        item.productNumber = stack.product;
        item.unitLoads = stack.unitLoads;
        item.heightIn = stack.heightIn;
        item.lineIndices.assign(static_cast<size_t>(stack.unitLoads), index);
        item.copyIndex = index;
        goods.push_back(item);
    }
    m4SyntheticDay::Day day = m4SyntheticDay::dayOf({m4SyntheticDay::group("2027")}, goods.size(), goods,
                                                    {m4SyntheticDay::reserved("2027", 1)});
    day.run.trailer.interiorLengthIn = scenario.lengthIn;
    day.run.trailer.interiorWidthIn = scenario.widthIn;
    day.run.trailer.stackHeightCeilingIn = scenario.heightIn;
    day.run.trailer.weightLimitLb = scenario.payloadLb;
    day.classification = ob::classifyLanes(day.run, day.book, day.items, m4SyntheticDay::kRouting);
    return day;
}

ob::SelectorSettings m4SettingsOf(const Scenario& scenario) {
    ob::SelectorSettings settings = m4SyntheticDay::selectorSettings();
    settings.maxUnitLoadsPerTruck = scenario.unitLoadCap;
    return settings;
}

Verdict runM4Select(const Scenario& scenario) {
    const m4SyntheticDay::Day day = m4DayOf(scenario);
    const ob::SelectorSettings settings = m4SettingsOf(scenario);
    return checkM4Plan(scenario, ob::selectLoads(day.run, day.book, day.items, day.classification, settings), settings.floor);
}

// Date rules on, window 10, as Tom's probe runs them.
Verdict runM4Dated(const Scenario& scenario) {
    const m4SyntheticDay::Day day = m4DayOf(scenario);
    const ob::SelectorSettings settings = m4SettingsOf(scenario);
    const ob::ShipDatePlan dated = ob::planShipDates(day.run, day.book, day.items, m4SyntheticDay::kRouting, settings, {true, true}, 10);
    return checkM4Plan(scenario, dated.plan, settings.floor);
}

// ---- Shared assertions and evidence ----------------------------------------------------------------------------

// A plan that claims "best possible" must ship the oracle's best count.
void requireVerdict(const Verdict& verdict, double expectedUnitLoads, const BestSelection& oracle, const string& path) {
    INFO(path);
    for (const string& problem : verdict.problems) INFO(problem);
    CHECK(verdict.problems.empty());
    CHECK(verdict.trucks == 1);
    CHECK(verdict.unitLoads == expectedUnitLoads);
    CHECK(oracle.unitLoads == expectedUnitLoads);
    if (verdict.claimsOptimal) CHECK(verdict.unitLoads == oracle.unitLoads);
    CHECK(verdict.lengthIn <= kMaxTrailerLengthIn + kTolerance);
}

json verdictJson(const Verdict& verdict) {
    return {{"unitLoads", verdict.unitLoads}, {"weightLb", verdict.weightLb}, {"floorLengthIn", verdict.lengthIn},
            {"trucks", verdict.trucks}, {"claimsOptimal", verdict.claimsOptimal}, {"proof", verdict.proofText},
            {"problems", verdict.problems}};
}

json oracleJson(const BestSelection& oracle) {
    return {{"bestUnitLoads", oracle.unitLoads}, {"shortestFloorForBestIn", oracle.usedLengthIn}};
}

// R-1a: Tom's witness (five large stacks alone in a row, twenty small ones in ten pairs) checked independently.
void requireR1aWitnessValid(const Scenario& scenario, const ob::FloorPatternSettings& floor) {
    vector<ob::FloorItem> items;
    ob::FloorLayout witness;
    for (size_t index = 0; index < scenario.stacks.size() && items.size() < 25; ++index) {
        const bool large = scenario.stacks[index].product == "A-LARGE";
        if (large && items.size() >= 5) continue;
        if (!large && items.size() < 5) continue;
        items.push_back(floorItemOf(scenario.stacks[index]));
    }
    REQUIRE(items.size() == 25);
    for (size_t large = 0; large < 5; ++large) witness.rows.push_back({ob::FloorPatternKind::Single, {{large, false}}, 40.0, 98.75, false});
    for (size_t pair = 0; pair < 10; ++pair) {
        witness.rows.push_back({ob::FloorPatternKind::WideWide, {{5 + 2 * pair, false}, {6 + 2 * pair, false}}, 40.0, 96.0, false});
    }
    CHECK(m5LayoutChecker::witnessProblems(witness, items, scenario.lengthIn, scenario.widthIn, scenario.heightIn, floor).empty());
    double length = 0.0;
    double weight = 0.0;
    for (const ob::FloorRow& row : witness.rows) length += row.depthIn;
    for (const ob::FloorItem& item : items) weight += 1000.0 * item.levels;
    CHECK(length == 600.0);
    CHECK(weight == 25000.0);
}

// R-8: Tom's supplied 586 in layout as rectangles: each item once, in its own footprint, inside the floor, no overlaps.
void requireR8WitnessValid(const Scenario& scenario, const json& layout) {
    const json& trailer = layout.at("trailer");
    const double length = trailer.at("length").get<double>();
    const double width = trailer.at("width").get<double>();
    struct Box { double x, y, along, across; };
    vector<Box> boxes;
    set<size_t> seen;
    double extent = 0.0;
    double weight = 0.0;
    for (const json& placement : layout.at("placements")) {
        const size_t item = placement.at("item").get<size_t>();
        REQUIRE(item < scenario.stacks.size());
        CHECK(seen.insert(item).second);
        const Box box{placement.at("x").get<double>(), placement.at("y").get<double>(), placement.at("length").get<double>(),
                      placement.at("width").get<double>()};
        const Stack& stack = scenario.stacks[item];
        const bool sameFootprint = (box.along == stack.longIn && box.across == stack.shortIn)
                                   || (box.along == stack.shortIn && box.across == stack.longIn);
        CHECK(sameFootprint);
        CHECK(box.x >= 0.0);
        CHECK(box.y >= 0.0);
        CHECK(box.x + box.along <= length + kTolerance);
        CHECK(box.y + box.across <= width + kTolerance);
        CHECK(placement.at("height").get<double>() <= trailer.at("height").get<double>());
        for (const Box& other : boxes) {
            const bool apart = box.x + box.along <= other.x + kTolerance || other.x + other.along <= box.x + kTolerance
                               || box.y + box.across <= other.y + kTolerance || other.y + other.across <= box.y + kTolerance;
            CHECK(apart);
        }
        boxes.push_back(box);
        extent = max(extent, box.x + box.along);
        weight += stack.weightLb;
    }
    CHECK(seen.size() == scenario.stacks.size());
    CHECK(extent == 586.0);
    CHECK(weight == 11000.0);
}

vector<string> productsOf(const Scenario& scenario) {
    vector<string> products;
    for (const Stack& stack : scenario.stacks) products.push_back(stack.product);
    sort(products.begin(), products.end());
    return products;
}

// R-5a's fourteen stacks: the file's own items with the first (the distractor) replaced.
Scenario withDistractor(Scenario base, double longIn, double shortIn) {
    base.stacks.front().longIn = longIn;
    base.stacks.front().shortIn = shortIn;
    return base;
}

const vector<pair<double, double>> kDistractors{{100, 100}, {120, 60}, {96, 60}, {80, 80}, {100, 60},
                                                {120, 48},  {140, 60}, {90, 90}, {100, 80}};

}  // namespace

TEST_CASE("R-1a/m4Path: Tom's 40-item case ships 25 unit loads through M4 selectLoads" * doctest::test_suite("m5")) {
    const json file = loadTomFile("m5ExactInputs/R-1a.json");
    const Scenario scenario = scenarioFromCaseFile(file);
    REQUIRE(scenario.stacks.size() == 40);
    const double expected = file.at("expected_unit_loads").get<double>();
    REQUIRE(expected == 25.0);
    requireR1aWitnessValid(scenario, m4SyntheticDay::selectorSettings().floor);

    const Verdict verdict = runM4Select(scenario);
    // No optimality oracle for 40 items (override): the plan must reach Tom's 25 in a valid layout.
    for (const string& problem : verdict.problems) INFO(problem);
    CHECK(verdict.problems.empty());
    CHECK(verdict.unitLoads == expected);
    CHECK(verdict.weightLb == 25000.0);
    CHECK(verdict.lengthIn <= kMaxTrailerLengthIn + kTolerance);
    m5Evidence::note("expected", {{"unitLoads", expected}, {"witnessLengthIn", 600}});
    m5Evidence::note("actual", verdictJson(verdict));
}

TEST_CASE("R-1a/m5Path: Tom's 40-item case ships 25 unit loads through the M5 direct seam" * doctest::test_suite("m5")) {
    const json file = loadTomFile("m5ExactInputs/R-1a.json");
    const Scenario scenario = scenarioFromCaseFile(file);
    const double expected = file.at("expected_unit_loads").get<double>();

    const M5Run run = runM5(scenario);
    for (const string& problem : run.verdict.problems) INFO(problem);
    CHECK(run.verdict.problems.empty());
    CHECK(run.verdict.unitLoads == expected);
    CHECK(run.verdict.weightLb == 25000.0);
    CHECK(run.verdict.lengthIn <= kMaxTrailerLengthIn + kTolerance);
    m5Evidence::note("expected", {{"unitLoads", expected}, {"witnessLengthIn", 600}});
    m5Evidence::note("actual", verdictJson(run.verdict));
}

TEST_CASE("R-1b/m4Path: Tom's four items on the 100 x 100 floor ship 2 unit loads through M4 selectLoads" * doctest::test_suite("m5")) {
    const json file = loadTomFile("m5ExactInputs/R-1b.json");
    const Scenario scenario = scenarioFromCaseFile(file);
    REQUIRE(scenario.stacks.size() == 4);
    REQUIRE(scenario.lengthIn == 100.0);
    REQUIRE(scenario.widthIn == 100.0);
    const double expected = file.at("expected_unit_loads").get<double>();

    const BestSelection oracle = rowOracle(scenario);
    const Verdict verdict = runM4Select(scenario);
    requireVerdict(verdict, expected, oracle, "M4 selectLoads");
    CHECK(verdict.lengthIn <= 100.0 + kTolerance);  // 3 or 4 would mean the default 630 in trailer was used
    CHECK(verdict.weightLb == 200.0);
    m5Evidence::note("expected", {{"unitLoads", expected}, {"floorIn", "100 x 100"}});
    m5Evidence::note("actual", verdictJson(verdict));
    m5Evidence::note("oracle", oracleJson(oracle));
}

TEST_CASE("R-1b/m5Path: Tom's four items on the 100 x 100 floor ship 2 unit loads through the M5 direct seam" * doctest::test_suite("m5")) {
    const json file = loadTomFile("m5ExactInputs/R-1b.json");
    const Scenario scenario = scenarioFromCaseFile(file);
    const double expected = file.at("expected_unit_loads").get<double>();

    const BestSelection oracle = rowOracle(scenario);
    const M5Run run = runM5(scenario);
    requireVerdict(run.verdict, expected, oracle, "M5 planLaneDate");
    CHECK(run.verdict.lengthIn <= 100.0 + kTolerance);
    CHECK(run.verdict.weightLb == 200.0);
    m5Evidence::note("expected", {{"unitLoads", expected}, {"floorIn", "100 x 100"}});
    m5Evidence::note("actual", verdictJson(run.verdict));
    m5Evidence::note("oracle", oracleJson(oracle));
}

TEST_CASE("R-5a/m4Path: Tom's base 14-stack case ships 23 unit loads through M4 selectLoads and planShipDates" * doctest::test_suite("m5")) {
    const json file = loadTomFile("m5ExactInputs/R-5a.json");
    const json counterexample = loadTomFile("m5ExactInputs/exactness-counterexample.json");
    const Scenario scenario = scenarioFromCaseFile(file);
    REQUIRE(scenario.stacks.size() == 14);
    const double expected = file.at("expected_unit_loads").get<double>();
    // The two Tom files describe the same fourteen stacks, in product order DISTRACTOR, P100..P112.
    const json& items = counterexample.at("all_items");
    REQUIRE(items.size() == scenario.stacks.size());
    for (size_t index = 0; index < items.size(); ++index) {
        CHECK(items[index].at("product").get<string>() == scenario.stacks[index].product);
        CHECK(items[index].at("long_in").get<double>() == scenario.stacks[index].longIn);
        CHECK(items[index].at("short_in").get<double>() == scenario.stacks[index].shortIn);
        CHECK(items[index].at("unit_loads").get<int>() == scenario.stacks[index].unitLoads);
    }

    const BestSelection oracle = rowOracle(scenario);
    CHECK(oracle.usedLengthIn <= file.at("feasible_witness_length_in").get<double>() + kTolerance);  // Tom's 617 in
    const Verdict selected = runM4Select(scenario);
    const Verdict dated = runM4Dated(scenario);
    requireVerdict(selected, expected, oracle, "M4 selectLoads");
    requireVerdict(dated, expected, oracle, "M4 planShipDates");
    m5Evidence::note("expected", {{"unitLoads", expected}});
    m5Evidence::note("actual", {{"selectLoads", verdictJson(selected)}, {"planShipDates", verdictJson(dated)}});
    m5Evidence::note("oracle", oracleJson(oracle));
}

TEST_CASE("R-5a/m5Path: Tom's base 14-stack case ships 23 unit loads through the M5 direct seam" * doctest::test_suite("m5")) {
    const json file = loadTomFile("m5ExactInputs/R-5a.json");
    const Scenario scenario = scenarioFromCaseFile(file);
    const double expected = file.at("expected_unit_loads").get<double>();

    const BestSelection oracle = rowOracle(scenario);
    const M5Run run = runM5(scenario);
    requireVerdict(run.verdict, expected, oracle, "M5 planLaneDate");
    m5Evidence::note("expected", {{"unitLoads", expected}});
    m5Evidence::note("actual", verdictJson(run.verdict));
    m5Evidence::note("oracle", oracleJson(oracle));
}

TEST_CASE("R-5a/distractorVariants: all nine distractor cases ship 23 unit loads on every path" * doctest::test_suite("m5")) {
    const json file = loadTomFile("m5ExactInputs/R-5a.json");
    const json seed = loadTomFile("m5ExactInputs/monotonicity-standard-results.json");
    const Scenario base = scenarioFromCaseFile(file);
    const double expected = file.at("expected_unit_loads").get<double>();
    // The thirteen real stacks are the seed file's, in order; only the distractor differs.
    REQUIRE(seed.at("items").size() == 13);
    for (size_t index = 0; index < 13; ++index) {
        const json& item = seed.at("items")[index];
        CHECK(item.at("long").get<double>() == base.stacks[index + 1].longIn);
        CHECK(item.at("short").get<double>() == base.stacks[index + 1].shortIn);
        CHECK(item.at("levels").get<int>() == base.stacks[index + 1].unitLoads);
    }
    CHECK(set<pair<double, double>>(kDistractors.begin(), kDistractors.end()).size() == 9);
    CHECK(withDistractor(base, 100, 100).stacks.front().longIn == base.stacks.front().longIn);  // variant 1 is the base case

    json instances = json::array();
    for (const auto& [longIn, shortIn] : kDistractors) {
        const Scenario scenario = withDistractor(base, longIn, shortIn);
        const string label = "distractor " + to_string(static_cast<int>(longIn)) + " x " + to_string(static_cast<int>(shortIn));
        const BestSelection oracle = rowOracle(scenario);
        const Verdict selected = runM4Select(scenario);
        const Verdict dated = runM4Dated(scenario);
        const M5Run run = runM5(scenario);
        requireVerdict(selected, expected, oracle, label + " M4 selectLoads");
        requireVerdict(dated, expected, oracle, label + " M4 planShipDates");
        requireVerdict(run.verdict, expected, oracle, label + " M5 planLaneDate");
        instances.push_back({{"distractor", {longIn, shortIn}}, {"oracle", oracleJson(oracle)}, {"selectLoads", verdictJson(selected)},
                             {"planShipDates", verdictJson(dated)}, {"m5", verdictJson(run.verdict)}});
    }
    CHECK(instances.size() == 9);
    m5Evidence::note("expected", {{"unitLoadsPerInstance", expected}, {"instances", 9}});
    m5Evidence::note("instances", instances);
}

TEST_CASE("R-8/m4Path: Tom's eleven items all ship through M4 selectLoads and planShipDates" * doctest::test_suite("m5")) {
    const json items = loadTomFile("e6dbb0f/floor-proof-results.json");
    const json layout = loadTomFile("e6dbb0f/verified-layout.json");
    const Scenario scenario = elevenItemScenario(items);
    REQUIRE(scenario.stacks.size() == 11);
    requireR8WitnessValid(scenario, layout);

    const BestSelection oracle = rowOracle(scenario);
    const Verdict selected = runM4Select(scenario);
    const Verdict dated = runM4Dated(scenario);
    requireVerdict(selected, 11.0, oracle, "M4 selectLoads");
    requireVerdict(dated, 11.0, oracle, "M4 planShipDates");
    CHECK(selected.shippedProducts == productsOf(scenario));
    CHECK(dated.shippedProducts == productsOf(scenario));
    CHECK(selected.weightLb == 11000.0);
    m5Evidence::note("expected", {{"unitLoads", 11}, {"suppliedLayoutLengthIn", 586}});
    m5Evidence::note("actual", {{"selectLoads", verdictJson(selected)}, {"planShipDates", verdictJson(dated)}});
    m5Evidence::note("oracle", oracleJson(oracle));
}

TEST_CASE("R-8/m5Path: Tom's eleven items all ship through the M5 direct seam" * doctest::test_suite("m5")) {
    const json items = loadTomFile("e6dbb0f/floor-proof-results.json");
    const json layout = loadTomFile("e6dbb0f/verified-layout.json");
    const Scenario scenario = elevenItemScenario(items);
    requireR8WitnessValid(scenario, layout);

    const BestSelection oracle = rowOracle(scenario);
    const M5Run run = runM5(scenario);
    requireVerdict(run.verdict, 11.0, oracle, "M5 planLaneDate");
    CHECK(run.verdict.shippedProducts == productsOf(scenario));
    CHECK(run.verdict.weightLb == 11000.0);
    CHECK(run.outcome.leftOver.empty());
    // The sorted rows refuse these eleven, so the grouping search runs, and draws on the lane-date's one
    // route budget (ED-19): charged, within it, and never stopped on this load.
    const ob::m5::FloorSearchRecord& search = run.outcome.floorSearch;
    CHECK(search.statesCharged > 0);
    CHECK(search.statesCharged <= search.routeStates);
    CHECK(search.routeStates == 1'000'000);
    CHECK(search.callLimitStops + search.routeLimitStops + search.wallDeadlineStops == 0);
    m5Evidence::note("expected", {{"unitLoads", 11}, {"suppliedLayoutLengthIn", 586}});
    m5Evidence::note("actual", verdictJson(run.verdict));
    m5Evidence::note("oracle", oracleJson(oracle));
}

// Tom's floor_proof_probe.cpp generation, unchanged: seed 120112, 15,000 trials of eleven 1,000 lb items drawn from
// twelve footprints. A trial reaches selection when the sorted arrangement alone cannot place the items (the
// arrangeFloorSorted gate: arrangeFloor itself is exact now) yet an exact grouping fits all eleven in 630 in.
TEST_CASE("R-8/probeSearch: every trial the seeded probe reaches ships all eleven items" * doctest::test_suite("m5")) {
    const vector<pair<double, double>> footprints{{72, 60}, {80, 48}, {120, 48}, {48, 48}, {96, 40}, {60, 60},
                                                  {40, 33}, {100, 50}, {110, 40}, {90, 55}, {80, 60}, {100, 60}};
    constexpr int kTrials = 15000;
    constexpr size_t kItems = 11;
    mt19937 generator(120112);
    const ob::SelectorSettings settings = m4SyntheticDay::selectorSettings();
    int generated = 0;
    int areaPassed = 0;
    int sortedGated = 0;
    int reachedSelection = 0;
    int m4Shipped = 0;
    int m5Shipped = 0;
    double longestFloorIn = 0.0;
    for (int trial = 0; trial < kTrials; ++trial) {
        ++generated;
        Scenario scenario;
        scenario.name = "R-8 probe trial " + to_string(trial);
        double area = 0.0;
        for (size_t item = 0; item < kItems; ++item) {
            const auto [longIn, shortIn] = footprints[generator() % footprints.size()];
            scenario.stacks.push_back({"P" + to_string(100 + item), longIn, shortIn, 50.0, 1000.0, 1, 0});
            area += longIn * shortIn;
        }
        if (area > 63000.0) continue;
        ++areaPassed;
        vector<ob::FloorItem> floorItems;
        for (const Stack& stack : scenario.stacks) floorItems.push_back(floorItemOf(stack));
        if (ob::arrangeFloorSorted(floorItems, m4SyntheticDay::kTrailer, settings.floor).fitsLength) continue;
        ++sortedGated;
        const BestSelection oracle = rowOracle(scenario);
        if (oracle.unitLoads < static_cast<double>(kItems)) continue;
        ++reachedSelection;

        const Verdict selected = runM4Select(scenario);
        const M5Run run = runM5(scenario);
        const bool selectedAll = selected.problems.empty() && selected.unitLoads == static_cast<double>(kItems);
        const bool m5All = run.verdict.problems.empty() && run.verdict.unitLoads == static_cast<double>(kItems);
        m4Shipped += selectedAll ? 1 : 0;
        m5Shipped += m5All ? 1 : 0;
        longestFloorIn = max({longestFloorIn, selected.lengthIn, run.verdict.lengthIn});
        if (!selectedAll || !m5All) {
            INFO("trial " << trial << " M4 " << selected.unitLoads << " UL, M5 " << run.verdict.unitLoads << " UL");
            for (const string& problem : selected.problems) INFO("M4: " << problem);
            for (const string& problem : run.verdict.problems) INFO("M5: " << problem);
            CHECK(selectedAll);
            CHECK(m5All);
        }
    }
    CHECK(generated == kTrials);
    CHECK(reachedSelection > 0);  // zero would make the probe vacuous
    CHECK(m4Shipped == reachedSelection);
    CHECK(m5Shipped == reachedSelection);
    CHECK(longestFloorIn <= kMaxTrailerLengthIn + kTolerance);
    m5Evidence::note("expected", {{"seed", 120112}, {"trials", kTrials}, {"reachedSelection", "> 0, all ship eleven"},
                                  {"reportedCount", "503 under Tom's recorded build (m4Fix2Verification.txt A3a/A3b)"}});
    m5Evidence::note("actual", {{"generated", generated}, {"areaPassed", areaPassed}, {"sortedArrangementGated", sortedGated},
                                {"reachedSelection", reachedSelection}, {"m4ShippedEleven", m4Shipped}, {"m5ShippedEleven", m5Shipped},
                                {"longestFloorIn", longestFloorIn}});
}

TEST_CASE("R-9/m4Path: the configured unit-load cap 64 gives 64 and 68 gives 68 through M4" * doctest::test_suite("m5")) {
    const json reference = loadTomFile("e6dbb0f/capacity-results.json");
    REQUIRE(reference.size() == 2);
    json actual = json::array();
    for (size_t row = 0; row < 2; ++row) {
        const double cap = reference[row].at("configured_cap").get<double>();
        const Scenario scenario = capacityScenario(cap);
        // The cap and the 68 offered unit loads bound the count; a valid layout that reaches the bound is the optimum.
        const double bound = min(cap, totalUnitLoads(scenario));
        const Verdict verdict = runM4Select(scenario);
        for (const string& problem : verdict.problems) INFO(problem);
        CHECK(verdict.problems.empty());
        CHECK(verdict.unitLoads == bound);
        CHECK(verdict.unitLoads == reference[row].at("unit_loads").get<double>());
        CHECK(verdict.lengthIn <= kMaxTrailerLengthIn + kTolerance);
        CHECK(verdict.weightLb == 500.0 * bound);  // 1,000 lb per two-high stack, counted from the components
        actual.push_back({{"cap", cap}, {"bound", bound}, {"referenceLengthIn", reference[row].at("length")}, {"verdict", verdictJson(verdict)}});
    }
    m5Evidence::note("expected", {{"cap64", 64}, {"cap68", 68}});
    m5Evidence::note("actual", actual);
}

TEST_CASE("R-9/m5Path: the unit-load cap 64 gives 64 and 68 gives 68 through the M5 direct seam" * doctest::test_suite("m5")) {
    const json reference = loadTomFile("e6dbb0f/capacity-results.json");
    REQUIRE(reference.size() == 2);
    json actual = json::array();
    for (size_t row = 0; row < 2; ++row) {
        const double cap = reference[row].at("configured_cap").get<double>();
        const Scenario scenario = capacityScenario(cap);
        const double bound = min(cap, totalUnitLoads(scenario));
        const M5Run run = runM5(scenario);
        for (const string& problem : run.verdict.problems) INFO(problem);
        CHECK(run.verdict.problems.empty());
        CHECK(run.verdict.unitLoads == bound);
        CHECK(run.verdict.unitLoads == reference[row].at("unit_loads").get<double>());
        CHECK(run.verdict.lengthIn <= kMaxTrailerLengthIn + kTolerance);
        CHECK(run.verdict.weightLb == 500.0 * bound);
        actual.push_back({{"cap", cap}, {"bound", bound}, {"referenceLengthIn", reference[row].at("length")}, {"verdict", verdictJson(run.verdict)}});
    }
    // A missing, non-positive or invalid cap fails before planning, with nothing planned or committed: the
    // configured value is converted as gatherLaneDate converts it, and an unusable one leaves the slot uncapped.
    json refused = json::array();
    for (const double badCap : {0.0, -64.0, 64.5, numeric_limits<double>::quiet_NaN(), numeric_limits<double>::infinity()}) {
        Scenario scenario = capacityScenario(64.0);
        scenario.unitLoadCap = badCap;
        const ob::m5::M5LaneDateInput input = m5InputOf(scenario);
        CHECK(input.pool.slots.front().maxUnitLoads == 0);
        m5Fixtures::FakeClock clock;
        const ob::m5::M5RunServices services(clock);
        const ob::m5::M5LaneOutcome outcome =
            ob::m5::planLaneDate(input, m5Fixtures::syntheticProfile(), m5Fixtures::physicalSettings(), services);
        CHECK(outcome.kind == ob::m5::OutcomeKind::ConfigurationIncomplete);
        CHECK_FALSE(outcome.selected);
        CHECK_FALSE(outcome.delta);
        CHECK(outcome.leftOver.size() == input.atoms.size());
        CHECK_FALSE(outcome.findings.empty());
        refused.push_back({{"cap", std::isfinite(badCap) ? json(badCap) : json(std::isnan(badCap) ? "NaN" : "infinity")},
                           {"outcome", ob::m5::outcomeKindName(outcome.kind)}});
    }
    m5Evidence::note("expected", {{"cap64", 64}, {"cap68", 68}, {"invalidCaps", "configuration incomplete before planning"}});
    m5Evidence::note("actual", {{"caps", actual}, {"invalidCaps", refused}});
}
