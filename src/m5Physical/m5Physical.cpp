#include "m5Physical.hpp"
#include "m5UnitLoads.hpp"
#include "truckCheck.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

using namespace std;

namespace ob {
namespace m5 {

namespace {

struct LoadTotals {
    double weightLb = 0.0;
    std::int64_t unitLoadHundredths = 0;  // checked; see m5UnitLoads.hpp
    bool unitLoadsOverflowed = false;
    double areaSqIn = 0.0;
};

void addUnitLoads(LoadTotals& totals, const Atom& atom) {
    const optional<int64_t> atomLoads = hundredthsOfUnitLoads(atom.unitLoads);
    const optional<int64_t> grown = atomLoads ? addCounts(totals.unitLoadHundredths, *atomLoads) : nullopt;
    if (grown) {
        totals.unitLoadHundredths = *grown;
    } else {
        totals.unitLoadsOverflowed = true;
    }
}

LoadTotals totalsOf(const M5LaneDateInput& input, const vector<size_t>& atoms) {
    LoadTotals totals;
    for (const size_t index : atoms) {
        const Atom& atom = input.atoms[index];
        totals.weightLb += atom.weightLb;
        addUnitLoads(totals, atom);
        totals.areaSqIn += atom.floor.longSideIn * atom.floor.shortSideIn;
    }
    return totals;
}

// Necessary conditions only, so a refusal here is one checkTruck would make too.
bool mayFit(const Atom& atom, const LoadTotals& totals, const TrailerCapability& capability) {
    if (atom.floor.heightIn > capability.heightIn) return false;
    if (totals.weightLb + atom.weightLb > capability.maxWeightLb) return false;
    LoadTotals grown = totals;
    addUnitLoads(grown, atom);
    if (grown.unitLoadsOverflowed || grown.unitLoadHundredths > capInHundredths(capability.maxUnitLoads)) return false;
    return totals.areaSqIn + atom.floor.longSideIn * atom.floor.shortSideIn <= capability.lengthIn * capability.widthIn;
}

// The final legality of a load M4's rows accepted, judged again in whole hundredths of an inch and a
// pound and compared exactly: each stack's height under the ceiling, the load's weight within the
// payload, each row's width within the trailer's and the rows' extent within its length. A measure is
// rounded up when off the 0.01 step (never understated), a limit down. Doubles stay inside the search;
// this is the answer's check, so no search grain or report rounding can pass an excess (U-PB-08,
// X-PAT-13). Returns the findings; empty when the load is legal.
vector<string> exactLimitFindings(const M5LaneDateInput& input, const vector<size_t>& atoms, const FloorLayout& layout,
                                  const TrailerCapability& capability) {
    vector<string> findings;
    const optional<int64_t> length = hundredthsRoundedDown(capability.lengthIn), width = hundredthsRoundedDown(capability.widthIn),
                            ceiling = hundredthsRoundedDown(capability.heightIn), payload = hundredthsRoundedDown(capability.maxWeightLb);
    if (!length || !width || !ceiling || !payload) {
        findings.push_back("the slot's length, width, height or payload is not a finite figure in hundredths");
        return findings;
    }
    long double weight = 0.0L;
    for (const size_t index : atoms) {
        const Atom& atom = input.atoms[index];
        const optional<int64_t> height = hundredthsRoundedUp(atom.floor.heightIn);
        if (!height || *height > *ceiling) {
            findings.push_back("stack " + atom.id + " stands above the slot's ceiling in whole hundredths of an inch");
        }
        weight += atom.weightLb;
    }
    const optional<int64_t> load = hundredthsRoundedUp(static_cast<double>(weight));
    if (!load || *load > *payload) findings.push_back("load weight is over the slot's payload in whole hundredths of a pound");
    long double extent = 0.0L;
    for (const FloorRow& row : layout.rows) {
        const optional<int64_t> across = hundredthsRoundedUp(row.widthIn);
        if (!across || *across > *width) findings.push_back("a row is wider than the slot in whole hundredths of an inch");
        extent += row.depthIn;
    }
    const optional<int64_t> used = hundredthsRoundedUp(static_cast<double>(extent));
    if (!used || *used > *length) findings.push_back("rows are longer than the slot in whole hundredths of an inch");
    return findings;
}

// The cap judged on the components' quantities in whole hundredths, summed with checked arithmetic and
// compared exactly; the cached per-atom figure is never trusted for a final answer (ED-20, ED-ARITH).
// Empty when the load is within the slot's cap.
optional<string> unitLoadCapFinding(const M5LaneDateInput& input, const vector<size_t>& atoms, const TrailerCapability& capability) {
    if (capability.maxUnitLoads < 1) return string("the slot has no unit-load cap: a whole number from 1 is required");
    const optional<int64_t> unitLoads = unitLoadHundredthsOfAtoms(input, atoms);
    if (!unitLoads) return string("the load's unit loads are not multiples of 0.01, or their sum overflows a 64-bit count");
    if (*unitLoads > capInHundredths(capability.maxUnitLoads)) {
        return "load holds " + unitLoadsText(*unitLoads) + " unit loads, over the cap of " + to_string(capability.maxUnitLoads);
    }
    return nullopt;
}

vector<size_t> withAtom(const vector<size_t>& truck, size_t atom) {
    vector<size_t> grown = truck;
    grown.insert(upper_bound(grown.begin(), grown.end(), atom), atom);
    return grown;
}

// One floor search's states, drawn from the route budget when the settings carry one: never more than it
// has left or its per-call cap, none past the wall deadline. What the search leaves is given back.
class RoutePermit {
public:
    RoutePermit(const PhysicalSettings& settings, long long callStates) : route_(settings.routeBudget.get()) {
        if (!route_) {
            wanted_ = granted_ = callStates;
        } else {
            wanted_ = min(callStates, route_->perCallStates());
            pastDeadline_ = route_->pastWallDeadline();
            granted_ = pastDeadline_ ? 0 : route_->admit(wanted_);
        }
        states_.statesLeft = granted_;
    }
    RoutePermit(const RoutePermit&) = delete;
    RoutePermit& operator=(const RoutePermit&) = delete;

    FloorSearchBudget* states() { return &states_; }

    // Settles the permit once the search is over; returns the states it spent.
    long long settle(bool undecided, SearchStop& stop) {
        const long long spent = clamp(granted_ - states_.statesLeft, 0LL, granted_);
        stop = SearchStop::None;
        if (!route_) {
            if (undecided) stop = SearchStop::CallLimit;
            return spent;
        }
        route_->giveBack(granted_ - spent);
        if (undecided) {
            stop = pastDeadline_ ? SearchStop::WallDeadline : granted_ < wanted_ ? SearchStop::RouteLimit : SearchStop::CallLimit;
            route_->recordStop(stop);
        }
        return spent;
    }

private:
    RouteSearchBudget* route_ = nullptr;
    long long wanted_ = 0;
    long long granted_ = 0;
    bool pastDeadline_ = false;
    FloorSearchBudget states_;
};

}  // namespace

string classSignatureOf(const Atom& atom) {
    ostringstream out;
    out << hexfloat << atom.floor.longSideIn << ',' << atom.floor.shortSideIn << ',' << atom.floor.heightIn << ','
        << atom.floor.levels << ',' << atom.weightLb << ',' << atom.unitLoads;
    return out.str();
}

TrailerSpec trailerOf(const TrailerCapability& capability) {
    TrailerSpec trailer;
    trailer.trailerCode = "M5 slot " + capability.signature();
    trailer.interiorLengthIn = capability.lengthIn;
    trailer.interiorWidthIn = capability.widthIn;
    trailer.stackHeightCeilingIn = capability.heightIn;
    trailer.weightLimitLb = capability.maxWeightLb;
    return trailer;
}

vector<size_t> slotsBestFirst(const ReservationPool& pool) {
    vector<size_t> order(pool.slots.size());
    for (size_t slot = 0; slot < order.size(); ++slot) order[slot] = slot;
    stable_sort(order.begin(), order.end(),
                [&](size_t left, size_t right) { return pool.slots[left].maxUnitLoads > pool.slots[right].maxUnitLoads; });
    return order;
}

bool slotsAreAlike(const ReservationPool& pool) {
    return all_of(pool.slots.begin(), pool.slots.end(),
                  [&](const TrailerCapability& slot) { return slot.signature() == pool.slots.front().signature(); });
}

bool slotsDifferOnlyInCap(const ReservationPool& pool) {
    for (const TrailerCapability& slot : pool.slots) {
        const TrailerCapability& first = pool.slots.front();
        if (slot.lengthIn != first.lengthIn || slot.widthIn != first.widthIn || slot.heightIn != first.heightIn
            || slot.maxWeightLb != first.maxWeightLb) {
            return false;
        }
    }
    return true;
}

PhysicalCheck checkTruck(const M5LaneDateInput& input, const vector<size_t>& atoms, const TrailerCapability& capability,
                         const PhysicalSettings& settings, FloorSearchBudget* budget) {
    PhysicalCheck check;
    if (optional<string> capFinding = unitLoadCapFinding(input, atoms, capability)) {
        check.findings.push_back(std::move(*capFinding));
        return check;
    }
    vector<FloorItem> floorItems;
    floorItems.reserve(atoms.size());
    for (const size_t index : atoms) floorItems.push_back(input.atoms[index].floor);
    const LoadTotals totals = totalsOf(input, atoms);
    TruckLoadCheck load = checkTruckLoad(floorItems, totals.weightLb, 0.0, trailerOf(capability), settings.floor, nullopt, budget);
    check.fits = load.verdict == TruckFit::Fits;
    check.undecided = load.verdict == TruckFit::Undecided;
    check.findings = std::move(load.findings);
    check.layout = std::move(load.layout);
    if (check.fits) {
        vector<string> exact = exactLimitFindings(input, atoms, check.layout, capability);
        if (!exact.empty()) {
            check.fits = false;
            check.findings = std::move(exact);
        }
    }
    return check;
}

PhysicalCheck checkTruckInRoute(const M5LaneDateInput& input, const vector<size_t>& atoms, const TrailerCapability& capability,
                                const PhysicalSettings& settings, long long callStates) {
    RoutePermit permit(settings, callStates);
    PhysicalCheck check = checkTruck(input, atoms, capability, settings, permit.states());
    check.statesCharged = permit.settle(check.undecided, check.stop);
    return check;
}

PhysicalSettings withRouteBudget(const PhysicalSettings& settings, const string& scope, function<bool()> wallStop) {
    PhysicalSettings routed = settings;
    if (!routed.routeBudget) {
        routed.routeBudget = make_shared<RouteSearchBudget>(scope, settings.floor.searchStateLimit, settings.floor.routeSearchStateLimit,
                                                            std::move(wallStop));
    }
    return routed;
}

double rearInchesOf(const FloorLayout& layout, const TrailerCapability& capability) {
    double extent = 0.0;
    for (const FloorRow& row : layout.rows) extent += row.depthIn;
    return capability.lengthIn - extent;
}

WitnessMeasure measureSuppliedWitness(const vector<PlacedRectangle>& placed, const TrailerCapability& capability) {
    struct Edges {
        int64_t front, back, left, right;
    };
    WitnessMeasure measure;
    const optional<int64_t> floorLength = hundredthsRoundedDown(capability.lengthIn), floorWidth = hundredthsRoundedDown(capability.widthIn);
    if (!floorLength || !floorWidth) {
        measure.findings.push_back("the slot's length or width is not a finite figure in hundredths");
        return measure;
    }
    vector<pair<size_t, Edges>> judged;
    judged.reserve(placed.size());
    for (size_t index = 0; index < placed.size(); ++index) {
        const PlacedRectangle& box = placed[index];
        const string name = "rectangle " + to_string(index);
        if (!isfinite(box.xIn) || !isfinite(box.yIn) || !isfinite(box.lengthIn) || !isfinite(box.widthIn) || box.lengthIn <= 0.0
            || box.widthIn <= 0.0) {
            measure.findings.push_back(name + " has a coordinate or size that is not finite and positive");
            continue;
        }
        const optional<int64_t> front = hundredthsRoundedDown(box.xIn), left = hundredthsRoundedDown(box.yIn),
                                back = hundredthsRoundedUp(box.xIn + box.lengthIn), right = hundredthsRoundedUp(box.yIn + box.widthIn);
        if (!front || !left || !back || !right || *back > *floorLength || *right > *floorWidth) {
            measure.findings.push_back(name + " lies outside the trailer floor in whole hundredths of an inch");
            continue;
        }
        const Edges edges{*front, *back, *left, *right};
        for (const auto& [other, next] : judged) {
            const bool apart = edges.back <= next.front || next.back <= edges.front || edges.right <= next.left || next.right <= edges.left;
            if (!apart) measure.findings.push_back(name + " overlaps rectangle " + to_string(other));
        }
        judged.emplace_back(index, edges);
        measure.occupiedExtentIn = max(measure.occupiedExtentIn, box.xIn + box.lengthIn);
    }
    measure.valid = measure.findings.empty();
    measure.rearInches = capability.lengthIn - measure.occupiedExtentIn;
    return measure;
}

WitnessMeasure validateSuppliedWitness(const M5LaneDateInput& input, const vector<size_t>& atoms, const LayoutWitness& witness,
                                       const TrailerCapability& capability, const FloorPatternSettings& floor) {
    vector<PlacedRectangle> placed;
    placed.reserve(witness.placements.size());
    for (const WitnessPlacement& placement : witness.placements) {
        placed.push_back({placement.xIn, placement.yIn, placement.lengthIn, placement.widthIn});
    }
    WitnessMeasure measure = measureSuppliedWitness(placed, capability);
    const set<size_t> expected(atoms.begin(), atoms.end());
    set<size_t> seen;
    auto sameHundredths = [](double left, double right) {
        const optional<int64_t> leftHundredths = hundredthsRoundedUp(left), rightHundredths = hundredthsRoundedUp(right);
        return leftHundredths && rightHundredths && *leftHundredths == *rightHundredths;
    };
    for (const WitnessPlacement& placement : witness.placements) {
        if (!expected.count(placement.atom)) {
            measure.findings.push_back("the witness places atom " + to_string(placement.atom) + ", which is not in the load");
            continue;
        }
        const Atom& atom = input.atoms[placement.atom];
        if (!seen.insert(placement.atom).second) measure.findings.push_back("the witness places atom " + atom.id + " twice");
        const bool lengthwise = sameHundredths(placement.lengthIn, atom.floor.longSideIn) && sameHundredths(placement.widthIn, atom.floor.shortSideIn);
        const bool crosswise = sameHundredths(placement.lengthIn, atom.floor.shortSideIn) && sameHundredths(placement.widthIn, atom.floor.longSideIn);
        if (!lengthwise && !crosswise) {
            measure.findings.push_back("atom " + atom.id + " is placed with a footprint it does not have");
        } else if (!floor.allowRotation) {
            // Without turning, every footprint goes in with the fixed side across: the long side across is
            // the crosswise placement, the short side across the lengthwise one. A square satisfies both.
            const bool fixedWay = floor.fixedOrientation == FixedOrientation::LongSideAcross ? crosswise : lengthwise;
            if (!fixedWay) measure.findings.push_back("atom " + atom.id + " is turned, and this site's forklifts cannot turn pallets");
        }
    }
    for (const size_t atom : expected) {
        if (!seen.count(atom)) measure.findings.push_back("the witness leaves atom " + input.atoms[atom].id + " off the floor");
    }
    const optional<int64_t> claimed = hundredthsRoundedUp(witness.claimedExtentIn);
    const optional<int64_t> measured = hundredthsRoundedUp(measure.occupiedExtentIn);
    if (!claimed || !measured || *claimed != *measured) {
        measure.findings.push_back("the claimed extent does not equal the measured extent of the rectangles in whole hundredths of an inch");
    }
    // Height, payload and cap are judged as checkTruck judges them, on the atoms' own figures.
    FloorLayout extentOnly;
    vector<string> limits = exactLimitFindings(input, atoms, extentOnly, capability);
    measure.findings.insert(measure.findings.end(), limits.begin(), limits.end());
    if (optional<string> capFinding = unitLoadCapFinding(input, atoms, capability)) measure.findings.push_back(std::move(*capFinding));
    measure.valid = measure.findings.empty();
    return measure;
}

namespace {

// A placed footprint in whole hundredths of an inch: across the trailer and along it.
struct Footprint {
    int64_t across = 0;
    int64_t along = 0;
};

optional<Footprint> footprintOf(const Atom& atom, bool turned) {
    const optional<int64_t> longSide = hundredthsRoundedUp(atom.floor.longSideIn), shortSide = hundredthsRoundedUp(atom.floor.shortSideIn);
    if (!longSide || !shortSide) return nullopt;
    return turned ? Footprint{*shortSide, *longSide} : Footprint{*longSide, *shortSide};
}

// Whether a stored row's placements form its pattern: the member count, and which members are turned
// (short side across), as floorPattern's row builder makes them.
bool formsPattern(const FloorRow& row) {
    const vector<FloorPlacement>& placed = row.placements;
    const auto turnedCount = count_if(placed.begin(), placed.end(), [](const FloorPlacement& placement) { return placement.turned; });
    switch (row.pattern) {
    case FloorPatternKind::Single: return placed.size() == 1;
    case FloorPatternKind::WideWide: return placed.size() == 2 && turnedCount == 0;
    case FloorPatternKind::WideNarrow: return placed.size() == 2 && turnedCount == 1;
    case FloorPatternKind::NarrowNarrow: return placed.size() == 2 && turnedCount == 2;
    case FloorPatternKind::ThreeAcross: return placed.size() == 3 && turnedCount == 3;
    case FloorPatternKind::Pinwheel:
        return placed.size() == 4 && !placed[0].turned && placed[1].turned && placed[2].turned && !placed[3].turned;
    }
    return false;
}

double inchesOf(int64_t hundredths) { return static_cast<double>(hundredths) / 100.0; }

}  // namespace

LayoutWitness witnessOfStoredRows(const M5LaneDateInput& input, const vector<size_t>& atoms, const FloorLayout& layout,
                                  vector<string>& findings) {
    LayoutWitness witness;
    int64_t rowStart = 0;
    for (size_t rowIndex = 0; rowIndex < layout.rows.size(); ++rowIndex) {
        const FloorRow& row = layout.rows[rowIndex];
        const string name = "stored row " + to_string(rowIndex) + " (" + floorPatternName(row.pattern) + ")";
        const optional<int64_t> storedDepth = hundredthsRoundedUp(row.depthIn);
        if (!storedDepth) {
            findings.push_back(name + " has a depth that is not a finite figure in hundredths");
            continue;
        }
        if (!formsPattern(row)) {
            findings.push_back(name + "'s placements do not form that pattern");
            continue;
        }
        vector<Footprint> sizes;
        vector<size_t> carried;
        for (const FloorPlacement& placement : row.placements) {
            if (placement.itemIndex >= atoms.size()) {
                findings.push_back(name + " places position " + to_string(placement.itemIndex) + ", which the truck does not carry");
                break;
            }
            const size_t atom = atoms[placement.itemIndex];
            const optional<Footprint> size = footprintOf(input.atoms[atom], placement.turned);
            if (!size) {
                findings.push_back(name + " places atom " + input.atoms[atom].id + ", whose footprint is not a finite figure in hundredths");
                break;
            }
            sizes.push_back(*size);
            carried.push_back(atom);
        }
        if (sizes.size() != row.placements.size()) continue;

        // Positions inside the row, relative to its front-left corner: (along, across).
        vector<pair<int64_t, int64_t>> corners;
        int64_t depth = 0;
        if (row.pattern == FloorPatternKind::Pinwheel) {
            const Footprint &frontLeft = sizes[0], &frontRight = sizes[1], &backLeft = sizes[2];
            corners = {{0, 0}, {0, frontLeft.across}, {frontLeft.along, 0}, {frontRight.along, backLeft.across}};
        } else {
            int64_t across = 0;
            for (const Footprint& size : sizes) {
                corners.emplace_back(0, across);
                across += size.across;
            }
        }
        for (size_t member = 0; member < sizes.size(); ++member) {
            depth = max(depth, corners[member].first + sizes[member].along);
            witness.placements.push_back({carried[member], inchesOf(rowStart + corners[member].first), inchesOf(corners[member].second),
                                          inchesOf(sizes[member].along), inchesOf(sizes[member].across)});
        }
        // The stored depth must be its members' own. A pinwheel's depth sums two sides, so rounding each up
        // may put it one hundredth past the stored depth rounded up; nothing else is allowed.
        const int64_t roundingSlack = row.pattern == FloorPatternKind::Pinwheel ? 1 : 0;
        if (*storedDepth > depth || depth - *storedDepth > roundingSlack) {
            findings.push_back(name + " is stored " + to_string(*storedDepth) + " hundredths deep, but its members take "
                               + to_string(depth));
        }
        const optional<int64_t> nextStart = addCounts(rowStart, depth);
        if (!nextStart) {
            findings.push_back(name + " ends past what a 64-bit count of hundredths holds");
            break;
        }
        rowStart = *nextStart;
    }
    witness.claimedExtentIn = inchesOf(rowStart);
    return witness;
}

PhysicalCheck checkStoredRows(const M5LaneDateInput& input, const vector<size_t>& atoms, const FloorLayout& layout,
                              const TrailerCapability& capability, const FloorPatternSettings& floor) {
    PhysicalCheck check;
    check.layout = layout;
    for (const size_t position : layout.unplaceableItems) {
        check.findings.push_back("the stored rows leave position " + to_string(position) + " off the floor as fitting no orientation");
    }
    const auto& allowed = floor.allowedPatterns;
    for (size_t rowIndex = 0; rowIndex < layout.rows.size(); ++rowIndex) {
        const FloorPatternKind pattern = layout.rows[rowIndex].pattern;
        if (find(allowed.begin(), allowed.end(), pattern) == allowed.end()) {
            check.findings.push_back("stored row " + to_string(rowIndex) + " is a " + floorPatternName(pattern)
                                     + " row, which this site's patterns do not allow");
        }
    }
    if (floor.maxPositions && static_cast<long long>(atoms.size()) > *floor.maxPositions) {
        check.findings.push_back("the truck holds " + to_string(atoms.size()) + " positions, over the site's cap of "
                                 + to_string(*floor.maxPositions));
    }
    vector<string> unreadable;
    const LayoutWitness witness = witnessOfStoredRows(input, atoms, layout, unreadable);
    check.findings.insert(check.findings.end(), unreadable.begin(), unreadable.end());
    const WitnessMeasure measure = validateSuppliedWitness(input, atoms, witness, capability, floor);
    check.findings.insert(check.findings.end(), measure.findings.begin(), measure.findings.end());
    check.fits = check.findings.empty();
    return check;
}

vector<size_t> physicalClassOf(const M5LaneDateInput& input) {
    map<string, size_t> classes;
    vector<size_t> classOf;
    classOf.reserve(input.atoms.size());
    for (const Atom& atom : input.atoms) {
        classOf.push_back(classes.try_emplace(classSignatureOf(atom), classes.size()).first->second);
    }
    return classOf;
}

int bestSingleClassLoad(const M5LaneDateInput& input, size_t prototype, const TrailerCapability& capability,
                        const PhysicalSettings& settings, int available, bool* decided) {
    if (decided) *decided = true;
    const Atom& atom = input.atoms[prototype];
    if (available <= 0 || atom.floor.heightIn > capability.heightIn) return 0;
    // Bounds no load can pass, so the search below starts at the most that could fit.
    double most = available;
    if (atom.weightLb > 0.0) most = min(most, floor(capability.maxWeightLb / atom.weightLb));
    const optional<int64_t> copyLoads = unitLoadHundredthsOfAtom(atom);
    if (!copyLoads || capability.maxUnitLoads < 1 || *copyLoads > capInHundredths(capability.maxUnitLoads)) return 0;
    if (*copyLoads > 0) most = min(most, static_cast<double>(capInHundredths(capability.maxUnitLoads) / *copyLoads));
    most = min(most, floor(capability.lengthIn * capability.widthIn / (atom.floor.longSideIn * atom.floor.shortSideIn)));
    if (settings.floor.maxPositions) most = min(most, static_cast<double>(*settings.floor.maxPositions));
    const TrailerSpec trailer = trailerOf(capability);
    for (int copies = static_cast<int>(most); copies > 0; --copies) {
        const vector<FloorItem> items(static_cast<size_t>(copies), atom.floor);
        RoutePermit permit(settings, settings.floor.searchStateLimit);
        const TruckFit verdict = floorVerdict(arrangeFloor(items, trailer, settings.floor, permit.states()));
        SearchStop stop = SearchStop::None;
        permit.settle(verdict == TruckFit::Undecided, stop);
        if (verdict == TruckFit::Fits) return copies;
        if (verdict == TruckFit::Undecided && decided) *decided = false;
    }
    return 0;
}

optional<int> GeometryCache::find(const string& key) const {
    lock_guard<mutex> lock(mutex_);
    const auto found = copiesByKey_.find(key);
    if (found == copiesByKey_.end()) return nullopt;
    return found->second;
}

void GeometryCache::put(const string& key, int copies) {
    lock_guard<mutex> lock(mutex_);
    copiesByKey_[key] = copies;
}

void fillTruck(const M5LaneDateInput& input, const vector<size_t>& classOf, vector<size_t>& truck, const vector<size_t>& ranked,
               vector<char>& placed, const TrailerCapability& capability, const PhysicalSettings& settings, const StopCheck& stop) {
    LoadTotals totals = totalsOf(input, truck);
    set<size_t> refusedClasses;
    for (const size_t candidate : ranked) {
        if (stop && stop()) return;
        if (placed[candidate] || refusedClasses.count(classOf[candidate])) continue;
        const Atom& atom = input.atoms[candidate];
        if (!mayFit(atom, totals, capability)) {
            refusedClasses.insert(classOf[candidate]);
            continue;
        }
        vector<size_t> grown = withAtom(truck, candidate);
        if (!checkTruckInRoute(input, grown, capability, settings, settings.searchStatesPerPackingCheck).fits) {
            refusedClasses.insert(classOf[candidate]);
            continue;
        }
        truck = std::move(grown);
        placed[candidate] = 1;
        totals.weightLb += atom.weightLb;
        addUnitLoads(totals, atom);
        totals.areaSqIn += atom.floor.longSideIn * atom.floor.shortSideIn;
    }
}

vector<vector<size_t>> packByUrgency(const M5LaneDateInput& input, const vector<size_t>& ranked,
                                     const vector<TrailerCapability>& slots, const PhysicalSettings& settings, const StopCheck& stop) {
    const vector<size_t> classOf = physicalClassOf(input);
    vector<char> placed(input.atoms.size(), 0);
    vector<vector<size_t>> trucks;
    while (trucks.size() < slots.size() && !(stop && stop())) {
        vector<size_t> truck;
        fillTruck(input, classOf, truck, ranked, placed, slots[trucks.size()], settings, stop);
        if (truck.empty()) break;
        trucks.push_back(std::move(truck));
    }
    return trucks;
}

KnapsackItem knapsackItemOf(const M5LaneDateInput& input, size_t index) {
    const Atom& atom = input.atoms[index];
    KnapsackItem item;
    item.areaSqIn = atom.floor.longSideIn * atom.floor.shortSideIn;
    item.weightLb = atom.weightLb;
    item.heightIn = atom.floor.heightIn;
    item.unitLoads = atom.unitLoads;
    item.longSideIn = atom.floor.longSideIn;
    item.shortSideIn = atom.floor.shortSideIn;
    item.levels = atom.floor.levels;
    auto rankOf = [&](const Component& component) {
        return make_tuple(rankDateOf(component.dueDate, input.departure, input.pastDueOrder), component.priority, component.dueDate);
    };
    const auto urgent = min_element(atom.components.begin(), atom.components.end(),
                                    [&](const Component& a, const Component& b) { return rankOf(a) < rankOf(b); });
    item.shipDate = urgent->dueDate;
    item.rankDate = rankDateOf(urgent->dueDate, input.departure, input.pastDueOrder);
    item.priority = urgent->priority;
    item.productNumber = urgent->product;
    return item;
}

vector<vector<size_t>> packByKnapsack(const M5LaneDateInput& input, const vector<size_t>& ranked,
                                      const vector<TrailerCapability>& slots, const PhysicalSettings& settings) {
    if (ranked.empty() || slots.empty()) return {};
    vector<KnapsackItem> items;
    items.reserve(ranked.size());
    for (const size_t index : ranked) items.push_back(knapsackItemOf(input, index));
    auto atomsOf = [&](const vector<size_t>& chosen) {
        vector<size_t> atoms;
        atoms.reserve(chosen.size());
        for (const size_t position : chosen) atoms.push_back(ranked[position]);
        sort(atoms.begin(), atoms.end());
        return atoms;
    };
    // The search prunes on the unit-load sum as a double; the final answer is checkTruck's exact count.
    auto limitsOf = [&](const TrailerCapability& capability) {
        TruckLimits limits;
        limits.fits = [&, capability](const vector<size_t>& chosen) {
            const PhysicalCheck check = checkTruckInRoute(input, atomsOf(chosen), capability, settings, settings.floor.searchStateLimit);
            return check.fits ? TruckFit::Fits : check.undecided ? TruckFit::Undecided : TruckFit::DoesNotFit;
        };
        ItemLimit unitLoads{{}, static_cast<double>(capability.maxUnitLoads)};
        for (const KnapsackItem& item : items) unitLoads.perItem.push_back(item.unitLoads);
        limits.limits.push_back(std::move(unitLoads));
        return limits;
    };
    vector<vector<size_t>> trucks;
    try {
        vector<size_t> remaining(items.size());
        for (size_t position = 0; position < remaining.size(); ++position) remaining[position] = position;
        while (trucks.size() < slots.size() && !remaining.empty()) {
            const TrailerCapability& capability = slots[trucks.size()];
            const KnapsackCapacity capacity{capability.lengthIn * capability.widthIn, capability.maxWeightLb, capability.heightIn};
            const KnapsackSelection truck = selectNextTruck(items, remaining, capacity, settings.knapsack, limitsOf(capability));
            if (truck.chosen.empty()) break;
            trucks.push_back(atomsOf(truck.chosen));
            remaining = truck.leftOver;
        }
    } catch (const invalid_argument&) {
        return {};
    }
    return trucks;
}

vector<vector<size_t>> packCarefully(const M5LaneDateInput& input, const vector<size_t>& ranked,
                                     const vector<TrailerCapability>& slots, const PhysicalSettings& settings, const StopCheck& stop) {
    if (ranked.empty() || slots.empty()) return {};
    const TrailerCapability& capability = slots.front();
    const size_t maxTrucks = slots.size();
    const bool oneCap = all_of(slots.begin(), slots.end(), [&](const TrailerCapability& slot) { return slot.signature() == capability.signature(); });
    if (!oneCap) return packByUrgency(input, ranked, slots, settings, stop);
    vector<KnapsackItem> items;
    items.reserve(ranked.size());
    for (const size_t index : ranked) items.push_back(knapsackItemOf(input, index));
    BinPacking packing;
    try {
        const KnapsackCapacity capacity{capability.lengthIn * capability.widthIn, capability.maxWeightLb, capability.heightIn};
        packing = packBins(items, capacity, settings.binPacker, static_cast<int>(maxTrucks));
    } catch (const invalid_argument&) {
        // Goods M4's packer cannot count on its grains (a weightless stack, say) are packed in
        // order instead; the trucks are checked the same way.
        return packByUrgency(input, ranked, slots, settings, stop);
    }

    // Positions in `ranked` order the trucks' atoms most urgent first, so a trim drops the least.
    vector<size_t> rankOf(input.atoms.size(), 0);
    for (size_t position = 0; position < ranked.size(); ++position) rankOf[ranked[position]] = position;
    vector<char> placed(input.atoms.size(), 1);
    for (const size_t index : ranked) placed[index] = 0;
    vector<vector<size_t>> trucks;
    for (const BinTruck& bin : packing.trucks) {
        vector<size_t> truck;
        for (const size_t position : bin.items) truck.push_back(ranked[position]);
        sort(truck.begin(), truck.end(), [&](size_t left, size_t right) { return rankOf[left] < rankOf[right]; });
        while (!truck.empty()) {
            vector<size_t> sorted = truck;
            sort(sorted.begin(), sorted.end());
            if (checkTruckInRoute(input, sorted, capability, settings, settings.searchStatesPerPackingCheck).fits) break;
            truck.pop_back();
        }
        sort(truck.begin(), truck.end());
        for (const size_t index : truck) placed[index] = 1;
        if (!truck.empty()) trucks.push_back(std::move(truck));
    }
    const vector<size_t> classOf = physicalClassOf(input);
    for (vector<size_t>& truck : trucks) fillTruck(input, classOf, truck, ranked, placed, capability, settings, stop);
    while (trucks.size() < maxTrucks && !(stop && stop())) {
        vector<size_t> truck;
        fillTruck(input, classOf, truck, ranked, placed, capability, settings, stop);
        if (truck.empty()) break;
        trucks.push_back(std::move(truck));
    }
    return trucks;
}

} // namespace m5
} // namespace ob
