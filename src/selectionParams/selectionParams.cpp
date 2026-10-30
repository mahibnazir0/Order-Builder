#include "selectionParams.hpp"
#include "paramsFields.hpp"

#include <algorithm>
#include <stdexcept>

using namespace std;

namespace ob {

using json = nlohmann::json;

namespace {

constexpr double kMaxVarietyRatio = 1000.0;
constexpr int kMaxThreads = 256;

bool readBoolean(const json& value, const string& key) {
    if (!value.is_boolean()) throw runtime_error("params: " + key + " must be true or false");
    return value.get<bool>();
}

long long readLimit(const json& block, const char* key, const string& blockName) {
    return readInteger(requiredField(block, key, blockName), blockName + "." + key, 1);
}

double readGrain(const json& block, const char* key, const string& blockName) {
    return readQuantity(requiredField(block, key, blockName), blockName + "." + key);
}

FloorPatternKind readPattern(const json& value, const string& key) {
    const string name = readString(value, key);
    static const vector<pair<string, FloorPatternKind>> kPatterns{
        {"WideWide", FloorPatternKind::WideWide},       {"WideNarrow", FloorPatternKind::WideNarrow},
        {"NarrowNarrow", FloorPatternKind::NarrowNarrow}, {"ThreeAcross", FloorPatternKind::ThreeAcross},
        {"Pinwheel", FloorPatternKind::Pinwheel},       {"Single", FloorPatternKind::Single}};
    for (const auto& [patternName, pattern] : kPatterns) {
        if (name == patternName) return pattern;
    }
    throw runtime_error("params: " + key + " '" + name
                        + "' is not WideWide, WideNarrow, NarrowNarrow, ThreeAcross, Pinwheel or Single");
}

FloorPatternSettings readFloor(const json& block, double fullCubeFraction) {
    const string name = "selection.floor";
    FloorPatternSettings floor;
    floor.allowRotation = readBoolean(requiredField(block, "allowRotation", name), name + ".allowRotation");
    const string orientation = readString(requiredField(block, "fixedOrientation", name), name + ".fixedOrientation");
    if (orientation == "LongSideAcross") {
        floor.fixedOrientation = FixedOrientation::LongSideAcross;
    } else if (orientation == "ShortSideAcross") {
        floor.fixedOrientation = FixedOrientation::ShortSideAcross;
    } else {
        throw runtime_error("params: " + name + ".fixedOrientation must be LongSideAcross or ShortSideAcross");
    }
    const json& patterns = requiredField(block, "allowedPatterns", name);
    requireNonEmptyArray(patterns, name + ".allowedPatterns");
    floor.allowedPatterns.clear();
    for (size_t index = 0; index < patterns.size(); ++index) {
        const FloorPatternKind pattern =
            readPattern(patterns[index], name + ".allowedPatterns[" + to_string(index) + "]");
        if (find(floor.allowedPatterns.begin(), floor.allowedPatterns.end(), pattern) != floor.allowedPatterns.end()) {
            throw runtime_error("params: " + name + ".allowedPatterns lists " + floorPatternName(pattern) + " twice");
        }
        floor.allowedPatterns.push_back(pattern);
    }
    if (find(floor.allowedPatterns.begin(), floor.allowedPatterns.end(), FloorPatternKind::Single)
        == floor.allowedPatterns.end()) {
        throw runtime_error("params: " + name + ".allowedPatterns must include Single: an odd item has to stand somewhere");
    }
    const json& maxPositions = requiredField(block, "maxPositions", name);
    if (!maxPositions.is_null()) floor.maxPositions = readInteger(maxPositions, name + ".maxPositions", 1);
    floor.fullCubeFraction = fullCubeFraction;
    return floor;
}

} // anonymous namespace

SelectionParams parseSelectionParams(const json& root) {
    if (!root.is_object()) throw runtime_error("params: root must be an object");
    const auto found = root.find("selection");
    if (found == root.end()) {
        throw runtime_error("params: required block 'selection' is missing; M4 has no default settings");
    }
    const json& block = *found;
    const string name = "selection";
    if (!block.is_object()) throw runtime_error("params: selection must be an object");

    SelectionParams params;
    params.reservations.defaultEquipment =
        readString(requiredField(block, "defaultEquipment", name), name + ".defaultEquipment");
    const json& known = requiredField(block, "knownEquipment", name);
    requireNonEmptyArray(known, name + ".knownEquipment");
    for (size_t index = 0; index < known.size(); ++index) {
        const string equipment = readString(known[index], name + ".knownEquipment[" + to_string(index) + "]");
        if (find(params.reservations.knownEquipment.begin(), params.reservations.knownEquipment.end(), equipment)
            != params.reservations.knownEquipment.end()) {
            throw runtime_error("params: " + name + ".knownEquipment lists '" + equipment + "' twice");
        }
        params.reservations.knownEquipment.push_back(equipment);
    }
    const auto& listed = params.reservations.knownEquipment;
    if (params.reservations.defaultEquipment.empty()
        || find(listed.begin(), listed.end(), params.reservations.defaultEquipment) == listed.end()) {
        throw runtime_error("params: " + name + ".defaultEquipment must be one of knownEquipment");
    }

    const double fullCubeFraction =
        readQuantity(requiredField(block, "fullCubeFraction", name), name + ".fullCubeFraction");
    if (fullCubeFraction > 1.0) throw runtime_error("params: " + name + ".fullCubeFraction must be at most 1");
    params.maxUnitLoadsPerTruck =
        readQuantity(requiredField(block, "maxUnitLoadsPerTruck", name), name + ".maxUnitLoadsPerTruck");

    params.routing.m5Threshold = readInteger(requiredField(block, "m5Threshold", name), name + ".m5Threshold", 2);
    params.routing.varietyRatio = readQuantity(requiredField(block, "varietyRatio", name), name + ".varietyRatio");
    if (params.routing.varietyRatio > kMaxVarietyRatio) {
        throw runtime_error("params: " + name + ".varietyRatio must be at most " + to_string(static_cast<int>(kMaxVarietyRatio)));
    }

    SelectorSettings& selector = params.selector;
    const json& knapsack = requiredField(block, "knapsack", name);
    const string knapsackName = name + ".knapsack";
    selector.knapsack = {readGrain(knapsack, "weightGrainLb", knapsackName),
                         readGrain(knapsack, "areaGrainSqIn", knapsackName),
                         readLimit(knapsack, "nodeLimit", knapsackName)};
    const json& pattern = requiredField(block, "patternPacker", name);
    const string patternName = name + ".patternPacker";
    selector.patternPacker = {readGrain(pattern, "weightGrainLb", patternName),
                              readGrain(pattern, "areaGrainSqIn", patternName),
                              static_cast<int>(readLimit(pattern, "maxPatterns", patternName)),
                              readLimit(pattern, "pricingNodeLimit", patternName)};
    const json& bin = requiredField(block, "binPacker", name);
    const string binName = name + ".binPacker";
    selector.binPacker = {readGrain(bin, "weightGrainLb", binName),
                          readGrain(bin, "areaGrainSqIn", binName),
                          static_cast<int>(readLimit(bin, "columnRoundsPerNode", binName)),
                          readLimit(bin, "pricingNodeLimit", binName),
                          readLimit(bin, "branchNodeLimit", binName),
                          static_cast<int>(readLimit(bin, "maxSearchItems", binName))};
    selector.floor = readFloor(requiredField(block, "floor", name), fullCubeFraction);
    selector.secondsPerLane = readQuantity(requiredField(block, "secondsPerLane", name), name + ".secondsPerLane");
    selector.threads = readInteger(requiredField(block, "threads", name), name + ".threads", 1);
    if (selector.threads > kMaxThreads) {
        throw runtime_error("params: " + name + ".threads must be at most " + to_string(kMaxThreads));
    }

    const string detail = readString(requiredField(block, "reportDetail", name), name + ".reportDetail");
    if (detail == "Standard") {
        params.reportDetail = ReportDetail::Standard;
    } else if (detail == "Detailed") {
        params.reportDetail = ReportDetail::Detailed;
    } else {
        throw runtime_error("params: " + name + ".reportDetail must be Standard or Detailed");
    }
    return params;
}

SelectionParams loadSelectionParams(const string& path) {
    SelectionParams params = parseSelectionParams(readParamsFile(path));
    params.sourcePath = path;
    return params;
}

} // namespace ob
