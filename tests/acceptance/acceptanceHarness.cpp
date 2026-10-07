#include "acceptanceHarness.hpp"

#include "floorPlanner.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace std;
using namespace ob;
using json = nlohmann::json;

namespace acceptanceTests {

namespace {

const string kSolutionSuffix = "_valid.json";

bool isAcceptanceSetCondition(const string& shipCondition) {
    return shipCondition == "TF" || shipCondition == "TL";
}

string laneKey(const string& locationFrom, const string& locationTo,
               const string& shipCondition) {
    return locationFrom + '\x1f' + locationTo + '\x1f' + shipCondition;
}

class SolutionReader {
public:
    explicit SolutionReader(const string& path) : path_(path) {}

    [[noreturn]] void fail(const string& field, const string& problem) const {
        throw runtime_error("acceptanceHarness: " + path_ + ": " + field + " " + problem);
    }

    const json& requiredArray(const json& parent, const char* key, const string& field) const {
        const auto found = parent.find(key);
        if (found == parent.end()) fail(field, "is missing");
        if (!found->is_array()) fail(field, "is present but is not an array");
        return *found;
    }

    const string& requiredString(const json& parent, const char* key, const string& field) const {
        const auto found = parent.find(key);
        if (found == parent.end()) fail(field, "is missing");
        if (!found->is_string()) fail(field, "is not a string");
        return found->get_ref<const string&>();
    }

    double requiredQuantity(const json& parent, const char* key, const string& field) const {
        const auto found = parent.find(key);
        if (found == parent.end()) fail(field, "is missing");
        if (!found->is_number()) fail(field, "is not a number");
        const double quantity = found->get<double>();
        if (!isfinite(quantity) || quantity < 0.0) {
            fail(field, "must be a non-negative finite number");
        }
        return quantity;
    }

private:
    string path_;
};

// Truck Builder writes no LOAD_OPT_SCORE, null or "" on loads it did not score.
bool isScored(const json& load) {
    const auto score = load.find("LOAD_OPT_SCORE");
    if (score == load.end() || score->is_null()) return false;
    return !(score->is_string() && score->get_ref<const string&>().empty());
}

void addShippedLine(AchievedRequest& achieved, const SolutionReader& reader, const json& line,
                    const string& field) {
    const string& idpr = reader.requiredString(line, "IDPR", field + ".IDPR");
    const string& matnr = reader.requiredString(line, "MATNR", field + ".MATNR");
    const string& unitOfMeasure = reader.requiredString(line, "MEINS", field + ".MEINS");
    const double quantity = reader.requiredQuantity(line, "MENGE", field + ".MENGE");
    const auto inserted =
        achieved.shippedByIdpr.emplace(idpr, ShippedLine{matnr, unitOfMeasure, 0.0});
    ShippedLine& shipped = inserted.first->second;
    if (shipped.matnr != matnr || shipped.unitOfMeasure != unitOfMeasure) {
        reader.fail(field, "ships IDPR " + idpr + " as a different material or unit than an"
                    " earlier load");
    }
    shipped.quantity += quantity;
}

void readSolution(AchievedRequest& achieved, unordered_map<string, size_t>& laneIndexByKey,
                  const SolutionReader& reader, const json& root) {
    const json& lanes = reader.requiredArray(root, "LANES", "LANES");
    for (size_t laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
        const string laneField = "LANES[" + to_string(laneIndex) + "]";
        const json& lane = lanes[laneIndex];
        if (!lane.is_object()) reader.fail(laneField, "is not an object");
        const string& locationFrom = reader.requiredString(lane, "LOCFRNO", laneField + ".LOCFRNO");
        const string& locationTo = reader.requiredString(lane, "LOCTONO", laneField + ".LOCTONO");
        const string& shipCondition =
            reader.requiredString(lane, "SHIP_COND", laneField + ".SHIP_COND");
        const json& loads = reader.requiredArray(lane, "LOADS", laneField + ".LOADS");
        const bool inSet = isAcceptanceSetCondition(shipCondition);

        for (size_t loadIndex = 0; loadIndex < loads.size(); ++loadIndex) {
            const string loadField = laneField + ".LOADS[" + to_string(loadIndex) + "]";
            const json& load = loads[loadIndex];
            if (!load.is_object()) reader.fail(loadField, "is not an object");
            const bool scored = isScored(load);
            if (scored && !inSet) ++achieved.scoredLoadsOutsideSet;
            if (!scored && inSet) ++achieved.unscoredLoadsInSet;
            const json& lines = reader.requiredArray(load, "LINES", loadField + ".LINES");
            if (!inSet) continue;

            const auto inserted = laneIndexByKey.emplace(
                laneKey(locationFrom, locationTo, shipCondition), achieved.laneDays.size());
            if (inserted.second) {
                achieved.laneDays.push_back({locationFrom, locationTo, shipCondition, 0});
            }
            ++achieved.laneDays[inserted.first->second].loads;
            for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
                addShippedLine(achieved, reader, lines[lineIndex],
                               loadField + ".LINES[" + to_string(lineIndex) + "]");
            }
        }
    }
}

vector<filesystem::path> solutionFiles(const string& directory) {
    vector<filesystem::path> paths;
    for (const auto& entry : filesystem::directory_iterator(directory)) {
        const string name = entry.path().filename().string();
        if (entry.is_regular_file() && name.size() > kSolutionSuffix.size()
            && name.compare(name.size() - kSolutionSuffix.size(), kSolutionSuffix.size(),
                            kSolutionSuffix) == 0) {
            paths.push_back(entry.path());
        }
    }
    sort(paths.begin(), paths.end());
    return paths;
}

} // anonymous namespace

size_t AchievedRequest::totalLoads() const {
    size_t loads = 0;
    for (const AchievedLane& lane : laneDays) loads += lane.loads;
    return loads;
}

AchievedRequest readAchievedLoads(const string& directory, const string& requestId) {
    AchievedRequest achieved;
    achieved.requestId = requestId;
    unordered_map<string, size_t> laneIndexByKey;
    for (const filesystem::path& path : solutionFiles(directory)) {
        const SolutionReader reader(path.string());
        ifstream input(path, ios::binary);
        if (!input) reader.fail("file", "cannot be opened");
        const json root = json::parse(input, nullptr, false);
        if (root.is_discarded()) reader.fail("file", "is not valid JSON");
        if (!root.is_object()) reader.fail("file", "is not a JSON object");
        if (reader.requiredString(root, "REQUEST_ID", "REQUEST_ID") != requestId) continue;
        ++achieved.solutionFiles;
        readSolution(achieved, laneIndexByKey, reader, root);
    }
    if (achieved.solutionFiles == 0) {
        throw runtime_error("acceptanceHarness: " + directory + ": no solution for request "
                            + requestId);
    }
    return achieved;
}

ValidityResult checkValidity(const PipelineResult& run, const AchievedRequest& achieved,
                             const TrailerSpec& trailer) {
    const vector<STRRecord>& demand = run.demand.str;
    if (run.join.lines.size() != demand.size()) {
        throw invalid_argument("acceptanceHarness: the run's join is not parallel to its demand");
    }
    ValidityResult result;

    vector<bool> grouped(demand.size(), false);
    for (const SegregatedGroup& group : run.segregation.groups) {
        for (const size_t lineIndex : group.lineIndices) grouped[lineIndex] = true;
    }
    unordered_map<string, size_t> lineIndexByIdpr;
    lineIndexByIdpr.reserve(demand.size());
    for (size_t lineIndex = 0; lineIndex < demand.size(); ++lineIndex) {
        lineIndexByIdpr.emplace(demand[lineIndex].idpr, lineIndex);
    }

    // The demand lines with shipped quantities in place of demanded ones. shippedLines points
    // into shippedDemand; both stay local so no pointer outlives them.
    vector<STRRecord> shippedDemand = demand;
    vector<JoinedLine> shippedLines = run.join.lines;
    DemandSelection shippedSelection;
    shippedSelection.selected.assign(demand.size(), false);
    for (const auto& [idpr, shipped] : achieved.shippedByIdpr) {
        const auto found = lineIndexByIdpr.find(idpr);
        if (found == lineIndexByIdpr.end()) {
            ++result.shippedLinesNotInDemand;
            continue;
        }
        STRRecord& line = shippedDemand[found->second];
        if (line.matnr != shipped.matnr || line.unitofmeas != shipped.unitOfMeasure) {
            ++result.shippedLinesDisagreeing;
            continue;
        }
        if (!grouped[found->second]) ++result.shippedLinesNotGrouped;
        line.trans = shipped.quantity;
        shippedSelection.selected[found->second] = true;
        ++shippedSelection.selectedLines;
    }
    for (size_t lineIndex = 0; lineIndex < shippedLines.size(); ++lineIndex) {
        shippedLines[lineIndex].str = &shippedDemand[lineIndex];
    }

    const FloorPlan plan =
        planFloor(run.segregation, shippedLines, shippedSelection, run.params, trailer);
    result.shippedLinesLeftOutOfFloor = plan.excludedLines.size();
    unordered_map<string, const LaneFloor*> floorByLane;
    floorByLane.reserve(plan.lanes.size());
    for (const LaneFloor& lane : plan.lanes) {
        floorByLane.emplace(laneKey(lane.locationFrom, lane.locationTo, lane.shipCondition), &lane);
    }

    result.laneDays.reserve(achieved.laneDays.size());
    for (const AchievedLane& achievedLane : achieved.laneDays) {
        LaneDayValidity laneDay;
        laneDay.lane = achievedLane.locationFrom + " -> " + achievedLane.locationTo + " "
            + achievedLane.shipCondition;
        laneDay.achievedLoads = achievedLane.loads;
        const auto found = floorByLane.find(laneKey(
            achievedLane.locationFrom, achievedLane.locationTo, achievedLane.shipCondition));
        if (found != floorByLane.end()) {
            laneDay.floorTrucks = found->second->floorTrucks;
            laneDay.boundTrucks = found->second->boundTrucks;
            laneDay.noStackingBaselineTrucks = found->second->noStackingBaselineTrucks;
            laneDay.unitLoads = found->second->totals.unitLoads;
            for (const size_t groupIndex : found->second->groupIndices) {
                laneDay.weightTrucks += plan.groups[groupIndex].bound.weightTrucks;
                laneDay.stackedHeightTrucks += plan.groups[groupIndex].bound.stackedHeightTrucks;
            }
        }
        const auto achievedLoads = static_cast<long long>(laneDay.achievedLoads);
        if (laneDay.floorTrucks > achievedLoads) ++result.floorExceedsAchieved;
        if (laneDay.noStackingBaselineTrucks > achievedLoads) ++result.baselineExceedsAchieved;
        result.achievedLoads += laneDay.achievedLoads;
        result.floorTrucks += laneDay.floorTrucks;
        result.noStackingBaselineTrucks += laneDay.noStackingBaselineTrucks;
        result.laneDays.push_back(laneDay);
    }
    return result;
}

} // namespace acceptanceTests
