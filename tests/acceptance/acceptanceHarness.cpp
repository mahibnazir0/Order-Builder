#include "acceptanceHarness.hpp"

#include "floorPlanner.hpp"
#include "isoDate.hpp"
#include "joiner.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

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

string laneDayKey(const string& locationFrom, const string& locationTo,
                  const string& shipCondition, const string& shipDate) {
    return laneKey(locationFrom, locationTo, shipCondition) + '\x1f' + shipDate;
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

    const string& requiredDate(const json& parent, const char* key, const string& field) const {
        const string& date = requiredString(parent, key, field);
        if (!isIsoDate(date)) fail(field, "is not a real YYYY-MM-DD date");
        return date;
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
                    const string& shipDate, const string& field) {
    const string& idpr = reader.requiredString(line, "IDPR", field + ".IDPR");
    const string& matnr = reader.requiredString(line, "MATNR", field + ".MATNR");
    const string& unitOfMeasure = reader.requiredString(line, "MEINS", field + ".MEINS");
    const double quantity = reader.requiredQuantity(line, "MENGE", field + ".MENGE");
    const auto inserted = achieved.shippedByIdpr.emplace(idpr, ShippedLine{matnr, unitOfMeasure, {}});
    ShippedLine& shipped = inserted.first->second;
    if (shipped.matnr != matnr || shipped.unitOfMeasure != unitOfMeasure) {
        reader.fail(field, "ships IDPR " + idpr + " as a different material or unit than an"
                    " earlier load");
    }
    shipped.quantityByShipDate[shipDate] += quantity;
}

void readSolution(AchievedRequest& achieved, unordered_map<string, size_t>& laneDayIndexByKey,
                  const SolutionReader& reader, const json& root, const string& onlyLoadId) {
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
            if (!onlyLoadId.empty()
                && reader.requiredString(load, "ID", loadField + ".ID") != onlyLoadId) {
                continue;
            }
            const bool scored = isScored(load);
            if (scored && !inSet) ++achieved.scoredLoadsOutsideSet;
            if (!scored && inSet) ++achieved.unscoredLoadsInSet;
            const json& lines = reader.requiredArray(load, "LINES", loadField + ".LINES");
            if (!inSet) continue;

            const string& shipDate =
                reader.requiredDate(load, "DATFR_TA", loadField + ".DATFR_TA");
            const auto inserted = laneDayIndexByKey.emplace(
                laneDayKey(locationFrom, locationTo, shipCondition, shipDate),
                achieved.laneDays.size());
            if (inserted.second) {
                achieved.laneDays.push_back({locationFrom, locationTo, shipCondition, shipDate, 0});
            }
            ++achieved.laneDays[inserted.first->second].loads;
            for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
                addShippedLine(achieved, reader, lines[lineIndex], shipDate,
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
    for (const AchievedLaneDay& laneDay : laneDays) loads += laneDay.loads;
    return loads;
}

size_t AchievedRequest::laneGroups() const {
    unordered_set<string> lanes;
    for (const AchievedLaneDay& laneDay : laneDays) {
        lanes.insert(laneKey(laneDay.locationFrom, laneDay.locationTo, laneDay.shipCondition));
    }
    return lanes.size();
}

AchievedRequest readAchievedLoads(const string& directory, const string& requestId,
                                  const string& onlyLoadId) {
    AchievedRequest achieved;
    achieved.requestId = requestId;
    unordered_map<string, size_t> laneDayIndexByKey;
    for (const filesystem::path& path : solutionFiles(directory)) {
        const SolutionReader reader(path.string());
        ifstream input(path, ios::binary);
        if (!input) reader.fail("file", "cannot be opened");
        const json root = json::parse(input, nullptr, false);
        if (root.is_discarded()) reader.fail("file", "is not valid JSON");
        if (!root.is_object()) reader.fail("file", "is not a JSON object");
        if (reader.requiredString(root, "REQUEST_ID", "REQUEST_ID") != requestId) continue;
        ++achieved.solutionFiles;
        readSolution(achieved, laneDayIndexByKey, reader, root, onlyLoadId);
    }
    if (achieved.solutionFiles == 0) {
        throw runtime_error("acceptanceHarness: " + directory + ": no solution for request "
                            + requestId);
    }
    if (!onlyLoadId.empty() && achieved.laneDays.empty()) {
        throw runtime_error("acceptanceHarness: " + directory + ": request " + requestId
                            + " has no TF or TL load " + onlyLoadId);
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

    // What each shipped demand line shipped on each date; map keeps the dates in order.
    map<string, vector<pair<size_t, double>>> shippedByShipDate;
    for (const auto& [idpr, shipped] : achieved.shippedByIdpr) {
        const auto found = lineIndexByIdpr.find(idpr);
        if (found == lineIndexByIdpr.end()) {
            ++result.shippedLinesNotInDemand;
            continue;
        }
        const STRRecord& line = demand[found->second];
        if (line.matnr != shipped.matnr || line.unitofmeas != shipped.unitOfMeasure) {
            ++result.shippedLinesDisagreeing;
            continue;
        }
        if (!grouped[found->second]) ++result.shippedLinesNotGrouped;
        for (const auto& [shipDate, quantity] : shipped.quantityByShipDate) {
            shippedByShipDate[shipDate].emplace_back(found->second, quantity);
        }
    }

    result.laneDays.reserve(achieved.laneDays.size());
    unordered_map<string, size_t> laneDayIndexByKey;
    laneDayIndexByKey.reserve(achieved.laneDays.size());
    for (const AchievedLaneDay& achievedLaneDay : achieved.laneDays) {
        laneDayIndexByKey.emplace(laneDayKey(achievedLaneDay.locationFrom, achievedLaneDay.locationTo,
                                             achievedLaneDay.shipCondition, achievedLaneDay.shipDate),
                                  result.laneDays.size());
        LaneDayValidity& laneDay = result.laneDays.emplace_back();
        laneDay.lane = achievedLaneDay.locationFrom + " -> " + achievedLaneDay.locationTo + " "
            + achievedLaneDay.shipCondition;
        laneDay.shipDate = achievedLaneDay.shipDate;
        laneDay.achievedLoads = achievedLaneDay.loads;
    }

    // The demand lines with a ship date's shipped quantities in place of demanded ones, each on
    // the pallet type its shipped quantity chooses. shippedLines points into shippedDemand; both
    // stay local so no pointer outlives them.
    vector<STRRecord> shippedDemand = demand;
    vector<JoinedLine> shippedLines = run.join.lines;
    for (size_t lineIndex = 0; lineIndex < shippedLines.size(); ++lineIndex) {
        shippedLines[lineIndex].str = &shippedDemand[lineIndex];
    }
    for (const auto& [shipDate, lineQuantities] : shippedByShipDate) {
        DemandSelection selection;
        selection.shipDate = shipDate;
        selection.selected.assign(demand.size(), false);
        for (const auto& [lineIndex, quantity] : lineQuantities) {
            shippedDemand[lineIndex].trans = quantity;
            if (shippedLines[lineIndex].palletTypes != nullptr) {
                shippedLines[lineIndex].product =
                    Joiner::choosePalletType(*shippedLines[lineIndex].palletTypes, quantity).product;
            }
            selection.selected[lineIndex] = true;
            ++selection.selectedLines;
        }
        const FloorPlan plan =
            planFloor(run.segregation, shippedLines, selection, run.params, trailer);
        result.shippedLinesLeftOutOfFloor += plan.excludedLines.size();
        for (const LaneFloor& lane : plan.lanes) {
            const auto found = laneDayIndexByKey.find(
                laneDayKey(lane.locationFrom, lane.locationTo, lane.shipCondition, shipDate));
            if (found == laneDayIndexByKey.end()) {
                ++result.floorLaneDaysWithoutLoads;
                continue;
            }
            LaneDayValidity& laneDay = result.laneDays[found->second];
            laneDay.floorTrucks = lane.floorTrucks;
            laneDay.boundTrucks = lane.boundTrucks;
            laneDay.noStackingBaselineTrucks = lane.noStackingBaselineTrucks;
            laneDay.unitLoads = lane.totals.unitLoads;
            laneDay.weightLb = lane.totals.totalWeightLb;
            for (const size_t groupIndex : lane.groupIndices) {
                laneDay.weightTrucks += plan.groups[groupIndex].bound.weightTrucks;
                laneDay.volumeTrucks += plan.groups[groupIndex].bound.volumeTrucks;
            }
        }
    }

    for (const LaneDayValidity& laneDay : result.laneDays) {
        const auto achievedLoads = static_cast<long long>(laneDay.achievedLoads);
        if (laneDay.floorTrucks > achievedLoads) ++result.floorExceedsAchieved;
        if (laneDay.noStackingBaselineTrucks > achievedLoads) ++result.baselineExceedsAchieved;
        result.achievedLoads += laneDay.achievedLoads;
        result.floorTrucks += laneDay.floorTrucks;
        result.noStackingBaselineTrucks += laneDay.noStackingBaselineTrucks;
    }
    return result;
}

} // namespace acceptanceTests
