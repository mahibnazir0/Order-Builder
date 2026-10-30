#include "paramsLoader.hpp"
#include "inputFile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

using namespace std;

namespace ob {

using json = nlohmann::json;

namespace {

enum class ZeroAllowed { No, Yes };

constexpr size_t maximumMessageTextLength = 240;

string printableText(const string& text) {
    const size_t length = min(text.size(), maximumMessageTextLength);
    string result;
    result.reserve(length + 3);
    for (size_t index = 0; index < length; ++index) {
        const auto character = static_cast<unsigned char>(text[index]);
        result.push_back(character >= 0x20 && character <= 0x7e
            ? static_cast<char>(character) : '?');
    }
    if (text.size() > maximumMessageTextLength) {
        result += "...";
    }
    return result;
}

// A path is identified by its end, so a long one keeps the file name.
string printablePath(const string& path) {
    if (path.size() <= maximumMessageTextLength) {
        return printableText(path);
    }
    return "..." + printableText(path.substr(path.size() - maximumMessageTextLength));
}

string formatNumber(double value) {
    ostringstream output;
    output.imbue(locale::classic());
    output << setprecision(numeric_limits<double>::digits10) << value;
    return output.str();
}

bool containsPrintableAscii(const string& value) {
    return any_of(value.begin(), value.end(), [](char character) {
        const auto code = static_cast<unsigned char>(character);
        return code >= 0x21 && code <= 0x7e;
    });
}

const json& requiredBlock(const json& root, const char* key) {
    const auto block = root.find(key);
    if (block == root.end()) {
        throw runtime_error("params: required block '" + string(key) + "' is missing");
    }
    return *block;
}

const json& requiredField(const json& record, const char* key,
                          const string& recordName) {
    if (!record.is_object()) {
        throw runtime_error("params: " + recordName + " must be an object");
    }
    const auto field = record.find(key);
    if (field == record.end()) {
        throw runtime_error("params: " + recordName + "." + key + " is required");
    }
    return *field;
}

double readQuantity(const json& value, const string& key,
                    ZeroAllowed zeroAllowed = ZeroAllowed::No) {
    if (!value.is_number()) {
        throw runtime_error("params: " + key + " must be a number");
    }
    const double quantity = value.get<double>();
    const bool invalidRange = zeroAllowed == ZeroAllowed::Yes
        ? quantity < 0.0 : quantity <= 0.0;
    if (!isfinite(quantity) || invalidRange) {
        const string requirement = zeroAllowed == ZeroAllowed::Yes
            ? "non-negative" : "positive";
        throw runtime_error("params: " + key + " must be " + requirement
            + " and finite, got " + formatNumber(quantity));
    }
    return quantity;
}

int readInteger(const json& value, const string& key, int minimum) {
    if (!value.is_number()) {
        throw runtime_error("params: " + key + " must be an integer");
    }
    const double number = value.get<double>();
    if (!isfinite(number) || trunc(number) != number
        || number < minimum || number > numeric_limits<int>::max()) {
        throw runtime_error("params: " + key + " must be an integer between "
            + to_string(minimum) + " and "
            + to_string(numeric_limits<int>::max())
            + ", got " + formatNumber(number));
    }
    return static_cast<int>(number);
}

string readString(const json& value, const string& key) {
    if (!value.is_string()) {
        throw runtime_error("params: " + key + " must be a string");
    }
    return value.get<string>();
}

void requireNonEmptyArray(const json& value, const string& key) {
    if (!value.is_array()) {
        throw runtime_error("params: " + key + " must be an array");
    }
    if (value.empty()) {
        throw runtime_error("params: " + key + " must not be empty");
    }
}

TrailerSpec readTrailer(const json& record, size_t index,
                        const vector<TrailerSpec>& trailers) {
    const string recordName = "trailers[" + to_string(index) + "]";
    TrailerSpec trailer;
    trailer.trailerCode = readString(
        requiredField(record, "trailerCode", recordName),
        recordName + ".trailerCode");
    if (!containsPrintableAscii(trailer.trailerCode)) {
        throw runtime_error("params: " + recordName
            + ".trailerCode must contain a printable ASCII character");
    }
    for (const auto& existingTrailer : trailers) {
        if (existingTrailer.trailerCode == trailer.trailerCode) {
            throw runtime_error("params: " + recordName
                + ".trailerCode is duplicated");
        }
    }
    trailer.interiorLengthIn = readQuantity(
        requiredField(record, "interiorLengthIn", recordName),
        recordName + ".interiorLengthIn");
    trailer.interiorWidthIn = readQuantity(
        requiredField(record, "interiorWidthIn", recordName),
        recordName + ".interiorWidthIn");
    trailer.stackHeightCeilingIn = readQuantity(
        requiredField(record, "stackHeightCeilingIn", recordName),
        recordName + ".stackHeightCeilingIn");
    trailer.weightLimitLb = readQuantity(
        requiredField(record, "weightLimitLb", recordName),
        recordName + ".weightLimitLb");
    trailer.stackPositions = readInteger(
        requiredField(record, "stackPositions", recordName),
        recordName + ".stackPositions", 1);
    const json& maxStackDepth = requiredField(record, "maxStackDepth", recordName);
    if (!maxStackDepth.is_null()) {
        trailer.maxStackDepth = readInteger(maxStackDepth, recordName + ".maxStackDepth", 1);
    }
    return trailer;
}

} // anonymous namespace

M2Params parseParams(const json& root) {
    if (!root.is_object()) {
        throw runtime_error("params: root must be an object");
    }
    M2Params params;

    const auto& criLimits = requiredBlock(root, "criSafeLimitLb");
    if (!criLimits.is_array() || criLimits.size() != 10) {
        throw runtime_error("params: criSafeLimitLb must be an array of 10 values");
    }
    double previousLimit = 0.0;
    for (size_t index = 0; index < criLimits.size(); ++index) {
        const string key = "criSafeLimitLb[" + to_string(index) + "]";
        const double limit = readQuantity(criLimits[index], key);
        params.cri.safeLimitLb[index + 1] = limit;
        if (index > 0 && limit <= previousLimit) {
            params.warnings.push_back("params: " + key + " is not increasing with CRI");
        }
        previousLimit = limit;
    }

    // Pallet weight, height and footprint come from the product master and the pallet table
    // (client, 3 Oct). A params file still carrying them would be a second source of truth
    // that silently stops mattering, so it is refused rather than ignored.
    if (root.contains("pallets")) {
        throw runtime_error("params: 'pallets' is no longer read; pallet weight, height and"
                            " footprint come from the product master and the pallet table"
                            " (--pallets)");
    }

    const auto& trailers = requiredBlock(root, "trailers");
    requireNonEmptyArray(trailers, "trailers");
    params.trailers.reserve(trailers.size());
    for (size_t index = 0; index < trailers.size(); ++index) {
        params.trailers.push_back(readTrailer(trailers[index], index, params.trailers));
    }

    if (!root.contains("doNotMixReading")) {
        params.defaultedKeys.push_back("doNotMixReading");
    } else {
        const string reading = readString(
            root["doNotMixReading"], "doNotMixReading");
        if (reading == "FlaggedVsNormal") {
            params.doNotMixReading = SegregationReading::FlaggedVsNormal;
        } else if (reading != "Strict") {
            throw runtime_error(
                "params: doNotMixReading must be Strict or FlaggedVsNormal");
        }
    }

    // Absence uses the default; explicit zero means try only the first strategy.
    if (!root.contains("pass2AttemptCap")) {
        params.defaultedKeys.push_back("pass2AttemptCap");
    } else {
        params.pass2AttemptCap = readInteger(
            root["pass2AttemptCap"], "pass2AttemptCap", 0);
        if (params.pass2AttemptCap > kMaxPass2Attempts) {
            throw runtime_error("params: pass2AttemptCap must be an integer between 0 and "
                + to_string(kMaxPass2Attempts) + ", got "
                + to_string(params.pass2AttemptCap));
        }
    }
    // Stack depth has one home, the trailer's maxStackDepth, which the floor reads too. A
    // second cap here would let Milestone 2 and the floor stack differently.
    if (root.contains("maxStackHeight")) {
        throw runtime_error("params: 'maxStackHeight' is no longer read; stacks build to the"
                            " ceiling unless the trailer's maxStackDepth limits them");
    }
    if (!root.contains("blankCriIsStackable")) {
        params.defaultedKeys.push_back("blankCriIsStackable");
    } else {
        if (!root["blankCriIsStackable"].is_boolean()) {
            throw runtime_error("params: blankCriIsStackable must be a boolean");
        }
        params.blankCriIsStackable = root["blankCriIsStackable"].get<bool>();
    }
    const string floorDeckHeight = readString(
        requiredBlock(root, "floorDeckHeight"), "floorDeckHeight");
    if (floorDeckHeight == "Included") {
        params.floorDeckHeight = DeckHeightRule::Included;
    } else if (floorDeckHeight != "Excluded") {
        throw runtime_error("params: floorDeckHeight must be Excluded or Included");
    }
    const string floorRoundingPoint = readString(
        requiredBlock(root, "floorRoundingPoint"), "floorRoundingPoint");
    if (floorRoundingPoint == "Lane") {
        params.floorRoundingPoint = FloorRoundingPoint::Lane;
    } else if (floorRoundingPoint != "Group") {
        throw runtime_error("params: floorRoundingPoint must be Group or Lane");
    }
    if (!root.contains("stackWholePallets")) {
        params.defaultedKeys.push_back("stackWholePallets");
    } else {
        if (!root["stackWholePallets"].is_boolean()) {
            throw runtime_error("params: stackWholePallets must be a boolean");
        }
        params.stackWholePallets = root["stackWholePallets"].get<bool>();
    }
    return params;
}

M2Params loadParams(const string& path) {
    const string pathText = printablePath(path);
    ifstream input(path, ios::binary);
    if (!isRegularFile(path) || !input) {
        throw runtime_error("params: cannot open file: " + pathText);
    }
    // istream::read turns a streambuf read error into badbit; reading through
    // the streambuf directly (as istreambuf_iterator and json::parse do) does not.
    string contents;
    array<char, 4096> chunk;
    while (input.read(chunk.data(), static_cast<streamsize>(chunk.size()))
           || input.gcount() > 0) {
        contents.append(chunk.data(), static_cast<size_t>(input.gcount()));
    }
    if (input.bad()) {
        throw runtime_error("params: cannot read file: " + pathText);
    }
    json root;
    try {
        root = json::parse(contents);
    } catch (const json::exception& error) {
        throw runtime_error("params: invalid JSON in " + pathText
            + ": " + printableText(error.what()));
    }
    M2Params params = parseParams(root);
    params.sourcePath = pathText;
    return params;
}

const PalletSpec* palletSpecFor(const M2Params& params, const string& palletId) {
    return palletSpecFor(params.pallets, palletId);
}

} // namespace ob
