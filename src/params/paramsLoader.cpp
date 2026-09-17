#include "paramsLoader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace ob {

using json = nlohmann::json;

namespace {

enum class ZeroAllowed { No, Yes };

constexpr std::size_t maximumMessageTextLength = 240;

std::string printableText(const std::string& text) {
    const std::size_t length = std::min(text.size(), maximumMessageTextLength);
    std::string result;
    result.reserve(length + 3);
    for (std::size_t index = 0; index < length; ++index) {
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
std::string printablePath(const std::string& path) {
    if (path.size() <= maximumMessageTextLength) {
        return printableText(path);
    }
    return "..." + printableText(path.substr(path.size() - maximumMessageTextLength));
}

std::string formatNumber(double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::digits10) << value;
    return output.str();
}

bool containsPrintableAscii(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](char character) {
        const auto code = static_cast<unsigned char>(character);
        return code >= 0x21 && code <= 0x7e;
    });
}

const json& requiredBlock(const json& root, const char* key) {
    const auto block = root.find(key);
    if (block == root.end()) {
        throw std::runtime_error("params: required block '" + std::string(key) + "' is missing");
    }
    return *block;
}

const json& requiredField(const json& record, const char* key,
                          const std::string& recordName) {
    if (!record.is_object()) {
        throw std::runtime_error("params: " + recordName + " must be an object");
    }
    const auto field = record.find(key);
    if (field == record.end()) {
        throw std::runtime_error("params: " + recordName + "." + key + " is required");
    }
    return *field;
}

double readQuantity(const json& value, const std::string& key,
                    ZeroAllowed zeroAllowed = ZeroAllowed::No) {
    if (!value.is_number()) {
        throw std::runtime_error("params: " + key + " must be a number");
    }
    const double quantity = value.get<double>();
    const bool invalidRange = zeroAllowed == ZeroAllowed::Yes
        ? quantity < 0.0 : quantity <= 0.0;
    if (!std::isfinite(quantity) || invalidRange) {
        const std::string requirement = zeroAllowed == ZeroAllowed::Yes
            ? "non-negative" : "positive";
        throw std::runtime_error("params: " + key + " must be " + requirement
            + " and finite, got " + formatNumber(quantity));
    }
    return quantity;
}

int readInteger(const json& value, const std::string& key, int minimum) {
    if (!value.is_number()) {
        throw std::runtime_error("params: " + key + " must be an integer");
    }
    const double number = value.get<double>();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < minimum || number > std::numeric_limits<int>::max()) {
        throw std::runtime_error("params: " + key + " must be an integer between "
            + std::to_string(minimum) + " and "
            + std::to_string(std::numeric_limits<int>::max())
            + ", got " + formatNumber(number));
    }
    return static_cast<int>(number);
}

std::string readString(const json& value, const std::string& key) {
    if (!value.is_string()) {
        throw std::runtime_error("params: " + key + " must be a string");
    }
    return value.get<std::string>();
}

void requireNonEmptyArray(const json& value, const std::string& key) {
    if (!value.is_array()) {
        throw std::runtime_error("params: " + key + " must be an array");
    }
    if (value.empty()) {
        throw std::runtime_error("params: " + key + " must not be empty");
    }
}

PalletSpec readPallet(const json& record, std::size_t index,
                      const M2Params& params) {
    const std::string recordName = "pallets[" + std::to_string(index) + "]";
    PalletSpec pallet;
    pallet.palletId = readString(
        requiredField(record, "palletId", recordName), recordName + ".palletId");
    if (!containsPrintableAscii(pallet.palletId)) {
        throw std::runtime_error("params: " + recordName
            + ".palletId must contain a printable ASCII character");
    }
    if (palletSpecFor(params, pallet.palletId) != nullptr) {
        throw std::runtime_error("params: " + recordName + ".palletId is duplicated");
    }
    pallet.addedWeightLb = readQuantity(
        requiredField(record, "addedWeightLb", recordName),
        recordName + ".addedWeightLb", ZeroAllowed::Yes);
    pallet.addedHeightIn = readQuantity(
        requiredField(record, "addedHeightIn", recordName),
        recordName + ".addedHeightIn", ZeroAllowed::Yes);
    pallet.footprintLengthIn = readQuantity(
        requiredField(record, "footprintLengthIn", recordName),
        recordName + ".footprintLengthIn");
    pallet.footprintWidthIn = readQuantity(
        requiredField(record, "footprintWidthIn", recordName),
        recordName + ".footprintWidthIn");
    return pallet;
}

TrailerSpec readTrailer(const json& record, std::size_t index,
                        const std::vector<TrailerSpec>& trailers) {
    const std::string recordName = "trailers[" + std::to_string(index) + "]";
    TrailerSpec trailer;
    trailer.trailerCode = readString(
        requiredField(record, "trailerCode", recordName),
        recordName + ".trailerCode");
    if (!containsPrintableAscii(trailer.trailerCode)) {
        throw std::runtime_error("params: " + recordName
            + ".trailerCode must contain a printable ASCII character");
    }
    for (const auto& existingTrailer : trailers) {
        if (existingTrailer.trailerCode == trailer.trailerCode) {
            throw std::runtime_error("params: " + recordName
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
    return trailer;
}

} // anonymous namespace

M2Params parseParams(const json& root) {
    if (!root.is_object()) {
        throw std::runtime_error("params: root must be an object");
    }
    M2Params params;

    const auto& criLimits = requiredBlock(root, "criSafeLimitLb");
    if (!criLimits.is_array() || criLimits.size() != 10) {
        throw std::runtime_error("params: criSafeLimitLb must be an array of 10 values");
    }
    double previousLimit = 0.0;
    for (std::size_t index = 0; index < criLimits.size(); ++index) {
        const std::string key = "criSafeLimitLb[" + std::to_string(index) + "]";
        const double limit = readQuantity(criLimits[index], key);
        params.cri.safeLimitLb[index + 1] = limit;
        if (index > 0 && limit <= previousLimit) {
            params.warnings.push_back("params: " + key + " is not increasing with CRI");
        }
        previousLimit = limit;
    }

    const auto& pallets = requiredBlock(root, "pallets");
    requireNonEmptyArray(pallets, "pallets");
    params.pallets.reserve(pallets.size());
    for (std::size_t index = 0; index < pallets.size(); ++index) {
        params.pallets.push_back(readPallet(pallets[index], index, params));
    }

    const auto& trailers = requiredBlock(root, "trailers");
    requireNonEmptyArray(trailers, "trailers");
    params.trailers.reserve(trailers.size());
    for (std::size_t index = 0; index < trailers.size(); ++index) {
        params.trailers.push_back(readTrailer(trailers[index], index, params.trailers));
    }

    if (!root.contains("doNotMixReading")) {
        params.defaultedKeys.push_back("doNotMixReading");
    } else {
        const std::string reading = readString(
            root["doNotMixReading"], "doNotMixReading");
        if (reading == "FlaggedVsNormal") {
            params.doNotMixReading = SegregationReading::FlaggedVsNormal;
        } else if (reading != "Strict") {
            throw std::runtime_error(
                "params: doNotMixReading must be Strict or FlaggedVsNormal");
        }
    }

    // Absence uses the default; explicit zero means try only the first strategy.
    if (!root.contains("pass2AttemptCap")) {
        params.defaultedKeys.push_back("pass2AttemptCap");
    } else {
        params.pass2AttemptCap = readInteger(
            root["pass2AttemptCap"], "pass2AttemptCap", 0);
    }
    if (!root.contains("blankCriIsStackable")) {
        params.defaultedKeys.push_back("blankCriIsStackable");
    } else {
        if (!root["blankCriIsStackable"].is_boolean()) {
            throw std::runtime_error("params: blankCriIsStackable must be a boolean");
        }
        params.blankCriIsStackable = root["blankCriIsStackable"].get<bool>();
    }
    return params;
}

M2Params loadParams(const std::string& path) {
    const std::string pathText = printablePath(path);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("params: cannot open file: " + pathText);
    }
    // istream::read turns a streambuf read error into badbit; reading through
    // the streambuf directly (as istreambuf_iterator and json::parse do) does not.
    std::string contents;
    std::array<char, 4096> chunk;
    while (input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()))
           || input.gcount() > 0) {
        contents.append(chunk.data(), static_cast<std::size_t>(input.gcount()));
    }
    if (input.bad()) {
        throw std::runtime_error("params: cannot read file: " + pathText);
    }
    json root;
    try {
        root = json::parse(contents);
    } catch (const json::exception& error) {
        throw std::runtime_error("params: invalid JSON in " + pathText
            + ": " + printableText(error.what()));
    }
    return parseParams(root);
}

const PalletSpec* palletSpecFor(const M2Params& params, const std::string& palletId) {
    for (const auto& pallet : params.pallets) {
        if (pallet.palletId == palletId) {
            return &pallet;
        }
    }
    return nullptr;
}

} // namespace ob
