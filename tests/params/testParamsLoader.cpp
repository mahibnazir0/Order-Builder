#include "doctest.h"
#include "paramsLoader.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace ob;
using json = nlohmann::json;

namespace {

std::string loadErrorMessage(const std::string& path) {
    try {
        loadParams(path);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return "";
}

bool isPrintableAscii(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](char character) {
        const auto code = static_cast<unsigned char>(character);
        return code >= 0x20 && code <= 0x7e;
    });
}

std::string writeParamsFile(const std::string& fileName, const std::string& content) {
    const std::string path = "tests/params/" + fileName;
    std::ofstream out(path, std::ios::binary);
    out << content;
    return path;
}

std::string replaceFirst(std::string text, const std::string& from, const std::string& to) {
    const auto position = text.find(from);
    REQUIRE(position != std::string::npos);
    return text.replace(position, from.size(), to);
}

std::string parseErrorMessage(const json& root) {
    try {
        parseParams(root);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return "";
}

json completeParams() {
    return json::parse(R"({
        "criSafeLimitLb": [5,299,549,799,1149,1499,1849,2199,3099,3599],
        "pallets": [
            {"palletId":"PTL","addedWeightLb":60,"addedHeightIn":5.5,"footprintLengthIn":48,"footprintWidthIn":40},
            {"palletId":"PGM","addedWeightLb":60,"addedHeightIn":5.5,"footprintLengthIn":48,"footprintWidthIn":40},
            {"palletId":"TLD","addedWeightLb":0,"addedHeightIn":0,"footprintLengthIn":48,"footprintWidthIn":40},
            {"palletId":"GMA","addedWeightLb":0,"addedHeightIn":0,"footprintLengthIn":48,"footprintWidthIn":40}
        ],
        "trailers": [{"trailerCode":"53FT_NA","interiorLengthIn":630,"interiorWidthIn":100,
                      "stackHeightCeilingIn":108,"weightLimitLb":45000,"stackPositions":30}],
        "doNotMixReading":"Strict","pass2AttemptCap":4,"blankCriIsStackable":false
    })");
}

void checkCompleteParams(const M2Params& params) {
    const std::array<double, 11> expectedLimits{0,5,299,549,799,1149,1499,1849,2199,3099,3599};
    CHECK(params.cri.safeLimitLb == expectedLimits);
    REQUIRE(params.pallets.size() == 4);
    const std::array<std::string, 4> expectedIds{"PTL", "PGM", "TLD", "GMA"};
    for (std::size_t index = 0; index < params.pallets.size(); ++index) {
        const auto& pallet = params.pallets[index];
        CHECK(pallet.palletId == expectedIds[index]);
        CHECK(pallet.addedWeightLb == (index < 2 ? 60.0 : 0.0));
        CHECK(pallet.addedHeightIn == (index < 2 ? 5.5 : 0.0));
        CHECK(pallet.footprintLengthIn == 48);
        CHECK(pallet.footprintWidthIn == 40);
    }
    REQUIRE(params.trailers.size() == 1);
    const auto& trailer = params.trailers[0];
    CHECK(trailer.trailerCode == "53FT_NA");
    CHECK(trailer.interiorLengthIn == 630);
    CHECK(trailer.interiorWidthIn == 100);
    CHECK(trailer.stackHeightCeilingIn == 108);
    CHECK(trailer.weightLimitLb == 45000);
    CHECK(trailer.stackPositions == 30);
    CHECK(params.doNotMixReading == SegregationReading::Strict);
    CHECK(params.pass2AttemptCap == 4);
    CHECK_FALSE(params.blankCriIsStackable);
    CHECK(params.warnings.empty());
}

void checkInvalidQuantities(const std::string& block, const std::string& field,
                            bool allowZero) {
    auto root = completeParams();
    const std::vector<json> invalidValues{
        -1, -5, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        "12", nullptr, true, json::object(), json::array()
    };
    const std::string key = block + "[0]." + field;
    for (const auto& value : invalidValues) {
        CAPTURE(key);
        CAPTURE(value);
        root[block][0][field] = value;
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains(key.c_str()), std::runtime_error);
    }
    root[block][0][field] = 0;
    if (allowZero) {
        CHECK_NOTHROW(parseParams(root));
    } else {
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains(key.c_str()), std::runtime_error);
    }
    root[block][0].erase(field);
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains(key.c_str()), std::runtime_error);
}

} // anonymous namespace

TEST_CASE("params complete input parses every field") {
    const auto params = parseParams(completeParams());
    checkCompleteParams(params);
    CHECK(params.defaultedKeys.empty());
}

TEST_CASE("params default configuration file loads every field") {
    const auto params = loadParams("config/orderBuilderParams.json");
    checkCompleteParams(params);
    CHECK(params.defaultedKeys.empty());
}

TEST_CASE("params missing policy keys use and record their defaults") {
    auto root = completeParams();
    root.erase("doNotMixReading");
    root.erase("pass2AttemptCap");
    root.erase("blankCriIsStackable");
    const auto params = parseParams(root);
    CHECK(params.doNotMixReading == SegregationReading::Strict);
    CHECK(params.pass2AttemptCap == 4);
    CHECK_FALSE(params.blankCriIsStackable);
    const std::vector<std::string> expectedKeys{
        "doNotMixReading", "pass2AttemptCap", "blankCriIsStackable"
    };
    CHECK(params.defaultedKeys == expectedKeys);
}

TEST_CASE("params missing CRI limits are rejected") {
    auto root = completeParams();
    root.erase("criSafeLimitLb");
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("criSafeLimitLb"), std::runtime_error);
}

TEST_CASE("params missing pallets block is rejected") {
    auto root = completeParams();
    root.erase("pallets");
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("pallets"), std::runtime_error);
}

TEST_CASE("params missing trailers block is rejected") {
    auto root = completeParams();
    root.erase("trailers");
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("trailers"), std::runtime_error);
}

TEST_CASE("params pallet lookup returns matching specs or nullptr") {
    const auto params = parseParams(completeParams());
    const auto* ptl = palletSpecFor(params, "PTL");
    const auto* tld = palletSpecFor(params, "TLD");
    REQUIRE(ptl != nullptr);
    REQUIRE(tld != nullptr);
    CHECK(ptl == &params.pallets[0]);
    CHECK(ptl->addedWeightLb == 60);
    CHECK(tld->addedWeightLb == 0);
    CHECK(palletSpecFor(params, "ZZZ") == nullptr);
}

TEST_CASE("params CRI requires exactly ten limits") {
    auto root = completeParams();
    root["criSafeLimitLb"].erase(9);
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("criSafeLimitLb"), std::runtime_error);
    root["criSafeLimitLb"].push_back(3599);
    root["criSafeLimitLb"].push_back(4000);
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("criSafeLimitLb"), std::runtime_error);
}

TEST_CASE("params every CRI limit must be positive finite and numeric") {
    auto root = completeParams();
    const std::vector<json> invalidValues{0, -1, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        nullptr, true, "5", json::array(), json::object()};
    for (std::size_t index = 0; index < 10; ++index) {
        for (const auto& value : invalidValues) {
            auto invalidRoot = root;
            invalidRoot["criSafeLimitLb"][index] = value;
            CHECK_THROWS_WITH_AS(parseParams(invalidRoot),
                doctest::Contains(("criSafeLimitLb[" + std::to_string(index) + "]").c_str()), std::runtime_error);
        }
    }
}

TEST_CASE("params non-increasing CRI limits produce warnings and retain values") {
    auto root = completeParams();
    for (double limit : {5.0, 4.0}) {
        root["criSafeLimitLb"][1] = limit;
        const auto params = parseParams(root);
        CHECK(params.cri.safeLimitLb[2] == limit);
        REQUIRE(params.warnings.size() == 1);
        CHECK(params.warnings[0].find("criSafeLimitLb[1]") != std::string::npos);
    }
}

TEST_CASE("params trailer weight limit must be positive finite and present") {
    checkInvalidQuantities("trailers", "weightLimitLb", false);
}

TEST_CASE("params trailer height ceiling must be positive finite and present") {
    checkInvalidQuantities("trailers", "stackHeightCeilingIn", false);
}

TEST_CASE("params trailer interior length must be positive finite and present") {
    checkInvalidQuantities("trailers", "interiorLengthIn", false);
}

TEST_CASE("params trailer interior width must be positive finite and present") {
    checkInvalidQuantities("trailers", "interiorWidthIn", false);
}

TEST_CASE("params pallet footprint length must be positive finite and present") {
    checkInvalidQuantities("pallets", "footprintLengthIn", false);
    auto root = completeParams();
    root["pallets"][0]["footprintLengthIn"] = -1e-300;
    CHECK(parseErrorMessage(root).find("-1e-300") != std::string::npos);
}

TEST_CASE("params pallet footprint width must be positive finite and present") {
    checkInvalidQuantities("pallets", "footprintWidthIn", false);
}

TEST_CASE("params pallet added weight permits zero but rejects invalid quantities") {
    checkInvalidQuantities("pallets", "addedWeightLb", true);
}

TEST_CASE("params pallet added height permits zero but rejects invalid quantities") {
    checkInvalidQuantities("pallets", "addedHeightIn", true);
}

TEST_CASE("params stack positions require a positive integer within int range") {
    auto root = completeParams();
    const std::vector<json> invalidValues{0, -1, 1.5, true, "30", nullptr,
        std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
        std::numeric_limits<unsigned long long>::max(), std::numeric_limits<long long>::min(),
        static_cast<long long>(std::numeric_limits<int>::max()) + 1};
    for (const auto& value : invalidValues) {
        root["trailers"][0]["stackPositions"] = value;
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("trailers[0].stackPositions"), std::runtime_error);
    }
    for (const json& value : std::vector<json>{1, 30.0, std::numeric_limits<int>::max()}) {
        root["trailers"][0]["stackPositions"] = value;
        CHECK(parseParams(root).trailers[0].stackPositions == value.get<int>());
    }
    root["trailers"][0].erase("stackPositions");
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("trailers[0].stackPositions"), std::runtime_error);
}

TEST_CASE("params explicit zero attempt cap is distinct from absence") {
    auto root = completeParams();
    root.erase("pass2AttemptCap");
    const auto defaulted = parseParams(root);
    CHECK(defaulted.pass2AttemptCap == 4);
    CHECK(defaulted.defaultedKeys == std::vector<std::string>{"pass2AttemptCap"});
    root["pass2AttemptCap"] = 0;
    const auto explicitZero = parseParams(root);
    CHECK(explicitZero.pass2AttemptCap == 0);
    CHECK(explicitZero.defaultedKeys.empty());
}

TEST_CASE("params attempt cap requires a non-negative integer within int range") {
    auto root = completeParams();
    const std::vector<json> invalidValues{-1, 0.5, true, "4", nullptr,
        std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
        std::numeric_limits<unsigned long long>::max(), std::numeric_limits<long long>::min(),
        static_cast<long long>(std::numeric_limits<int>::max()) + 1};
    for (const auto& value : invalidValues) {
        root["pass2AttemptCap"] = value;
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("pass2AttemptCap"), std::runtime_error);
    }
    for (const json& value : std::vector<json>{0.0, 1, 4.0, std::numeric_limits<int>::max()}) {
        root["pass2AttemptCap"] = value;
        CHECK(parseParams(root).pass2AttemptCap == value.get<int>());
    }
}

TEST_CASE("params present blocks must be non-empty arrays") {
    for (const std::string block : {"pallets", "trailers", "criSafeLimitLb"}) {
        for (const auto& value : std::vector<json>{json::object(), "bad", nullptr, true, 5, json::array()}) {
            auto root = completeParams();
            root[block] = value;
            CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains(block.c_str()), std::runtime_error);
        }
    }
}

TEST_CASE("params empty object fails because CRI limits are required") {
    CHECK_THROWS_WITH_AS(parseParams(json::parse("{}")),
        doctest::Contains("criSafeLimitLb"), std::runtime_error);
}

TEST_CASE("params root must be an object") {
    for (const auto& root : std::vector<json>{nullptr, json::array(), "bad", 1, true}) {
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("params:"), std::runtime_error);
    }
}

TEST_CASE("params pallet IDs must be non-empty strings") {
    for (const auto& value : std::vector<json>{"", " ", "\t", nullptr, 1, true}) {
        auto root = completeParams();
        root["pallets"][0]["palletId"] = value;
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("pallets[0].palletId"), std::runtime_error);
    }
    auto root = completeParams();
    root["pallets"][0].erase("palletId");
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("pallets[0].palletId"), std::runtime_error);
    root = completeParams();
    root["pallets"][0]["palletId"] = "EPAL";
    CHECK(parseParams(root).pallets[0].palletId == "EPAL");
}

TEST_CASE("params duplicate pallet IDs are rejected") {
    auto root = completeParams();
    root["pallets"][1]["palletId"] = "PTL";
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("pallets[1].palletId"), std::runtime_error);
}

TEST_CASE("params array records must be objects") {
    for (const std::string block : {"pallets", "trailers"}) {
        for (const auto& value : std::vector<json>{nullptr, "bad", 1, json::array()}) {
            auto root = completeParams();
            root[block][0] = value;
            CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains((block + "[0]").c_str()), std::runtime_error);
        }
    }
}

TEST_CASE("params trailer code must be a present non-empty string") {
    auto root = completeParams();
    for (const auto& value : std::vector<json>{nullptr, "", 1, true}) {
        root["trailers"][0]["trailerCode"] = value;
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("trailers[0].trailerCode"), std::runtime_error);
    }
    root["trailers"][0].erase("trailerCode");
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("trailers[0].trailerCode"), std::runtime_error);
}

TEST_CASE("params segregation reading rejects unknown or malformed values") {
    auto root = completeParams();
    for (const auto& value : std::vector<json>{"unknown", "strict", "", nullptr, 1, true}) {
        root["doNotMixReading"] = value;
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("doNotMixReading"), std::runtime_error);
    }
}

TEST_CASE("params alternate segregation reading is accepted") {
    auto root = completeParams();
    root["doNotMixReading"] = "FlaggedVsNormal";
    CHECK(parseParams(root).doNotMixReading == SegregationReading::FlaggedVsNormal);
}

TEST_CASE("params blank CRI policy accepts explicit true") {
    auto root = completeParams();
    root["blankCriIsStackable"] = true;
    CHECK(parseParams(root).blankCriIsStackable);
}

TEST_CASE("params blank CRI policy requires a boolean") {
    auto root = completeParams();
    for (const auto& value : std::vector<json>{nullptr, "false", 0, json::array(), json::object()}) {
        root["blankCriIsStackable"] = value;
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("blankCriIsStackable"), std::runtime_error);
    }
}

TEST_CASE("params duplicate trailer codes are rejected") {
    auto root = completeParams();
    root["trailers"].push_back(root["trailers"][0]);
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("trailers[1].trailerCode"), std::runtime_error);
}

TEST_CASE("params whitespace-only trailer code is rejected like an empty one") {
    for (const std::string value : {" ", "   ", "\t"}) {
        auto root = completeParams();
        root["trailers"][0]["trailerCode"] = value;
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("trailers[0].trailerCode"), std::runtime_error);
    }
}

TEST_CASE("params invalid JSON error message is plain ASCII") {
    const std::string path = writeParamsFile("_tmp_non_ascii.json", "{\"criSafeLimitLb\": \xE9}");
    std::string message;
    try {
        loadParams(path);
    } catch (const std::runtime_error& error) {
        message = error.what();
    }
    std::remove(path.c_str());
    REQUIRE_FALSE(message.empty());
    CHECK(std::all_of(message.begin(), message.end(),
        [](char character) { return static_cast<unsigned char>(character) < 0x80; }));
}

TEST_CASE("params missing file is rejected with the path in the message") {
    CHECK_THROWS_WITH_AS(loadParams("does/not/exist.json"),
        doctest::Contains("does/not/exist.json"), std::runtime_error);
}

TEST_CASE("params directory path is rejected with the path in the message") {
    CHECK_THROWS_WITH_AS(loadParams("tests/params"), doctest::Contains("tests/params"), std::runtime_error);
}

TEST_CASE("params empty file is rejected as invalid JSON") {
    const std::string path = writeParamsFile("_tmp_empty.json", "");
    CHECK_THROWS_WITH_AS(loadParams(path), doctest::Contains("invalid JSON"), std::runtime_error);
    std::remove(path.c_str());
}

TEST_CASE("params whitespace-only file is rejected as invalid JSON") {
    const std::string path = writeParamsFile("_tmp_whitespace.json", " \n\t\r\n ");
    CHECK_THROWS_WITH_AS(loadParams(path), doctest::Contains("invalid JSON"), std::runtime_error);
    std::remove(path.c_str());
}

TEST_CASE("params truncated file is rejected as invalid JSON") {
    const std::string text = completeParams().dump();
    const std::string path = writeParamsFile("_tmp_truncated.json", text.substr(0, text.size() / 2));
    CHECK_THROWS_WITH_AS(loadParams(path), doctest::Contains("invalid JSON"), std::runtime_error);
    std::remove(path.c_str());
}

TEST_CASE("params file with UTF-8 byte order mark loads normally") {
    const std::string path = writeParamsFile("_tmp_bom.json", "\xEF\xBB\xBF" + completeParams().dump());
    const auto params = loadParams(path);
    std::remove(path.c_str());
    checkCompleteParams(params);
}

TEST_CASE("params file with a repeated key keeps the last value") {
    const std::string text = replaceFirst(completeParams().dump(),
        "\"weightLimitLb\":45000", "\"weightLimitLb\":45000,\"weightLimitLb\":1");
    const std::string path = writeParamsFile("_tmp_repeated_key.json", text);
    const auto params = loadParams(path);
    std::remove(path.c_str());
    CHECK(params.trailers[0].weightLimitLb == 1);
}

TEST_CASE("params file number that overflows to infinity is rejected") {
    const std::string text = replaceFirst(completeParams().dump(), "45000", "1e999");
    const std::string path = writeParamsFile("_tmp_overflow.json", text);
    CHECK_THROWS_WITH_AS(loadParams(path), doctest::Contains("1e999"), std::runtime_error);
    std::remove(path.c_str());
}

TEST_CASE("params deeply nested file is rejected without exhausting the stack") {
    const std::size_t depth = 100000;
    const std::string path = writeParamsFile("_tmp_deep.json", std::string(depth, '[') + std::string(depth, ']'));
    CHECK_THROWS_WITH_AS(loadParams(path), doctest::Contains("root must be an object"), std::runtime_error);
    std::remove(path.c_str());
}

TEST_CASE("params present block with wrong type reports differently from an absent block") {
    for (const std::string block : {"criSafeLimitLb", "pallets", "trailers"}) {
        auto root = completeParams();
        root[block] = json::object();
        const std::string wrongTypeMessage = parseErrorMessage(root);
        root.erase(block);
        const std::string absentMessage = parseErrorMessage(root);
        CAPTURE(block);
        CHECK_FALSE(wrongTypeMessage.empty());
        CHECK_FALSE(absentMessage.empty());
        CHECK(wrongTypeMessage != absentMessage);
    }
}

TEST_CASE("params stack positions reject fractional, out-of-range and zero forms") {
    for (const char* text : {"30.7", "1e20", "-0"}) {
        auto root = completeParams();
        root["trailers"][0]["stackPositions"] = json::parse(text);
        CAPTURE(text);
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("trailers[0].stackPositions"), std::runtime_error);
    }
}

TEST_CASE("params attempt cap written as -0 is an explicit zero") {
    auto root = completeParams();
    root["pass2AttemptCap"] = json::parse("-0");
    const auto params = parseParams(root);
    CHECK(params.pass2AttemptCap == 0);
    CHECK(params.defaultedKeys.empty());
}

TEST_CASE("params negative zero quantity is treated the same as zero") {
    auto root = completeParams();
    root["pallets"][2]["addedWeightLb"] = -0.0;
    CHECK(parseParams(root).pallets[2].addedWeightLb == 0.0);
    root["pallets"][2]["footprintLengthIn"] = -0.0;
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("pallets[2].footprintLengthIn"), std::runtime_error);
}

TEST_CASE("params pallet ids with surrounding whitespace are preserved") {
    for (const std::string value : {"PTL ", " PTL", "PTL\n"}) {
        auto root = completeParams();
        root["pallets"][0]["palletId"] = value;
        CHECK(parseParams(root).pallets[0].palletId == value);
    }
}

TEST_CASE("params very long trailer code is kept intact") {
    auto root = completeParams();
    const std::string longCode(1000000, 'T');
    root["trailers"][0]["trailerCode"] = longCode;
    CHECK(parseParams(root).trailers[0].trailerCode == longCode);
}

TEST_CASE("params misspelled policy key falls back to the default and is recorded") {
    auto root = completeParams();
    root.erase("blankCriIsStackable");
    root["blankCRIIsStackable"] = true;
    const auto params = parseParams(root);
    CHECK_FALSE(params.blankCriIsStackable);
    CHECK(params.defaultedKeys == std::vector<std::string>{"blankCriIsStackable"});
}

TEST_CASE("params pallet lookup on a copy points into the copy's own storage") {
    const auto original = parseParams(completeParams());
    const PalletSpec* originalSpec = palletSpecFor(original, "PTL");
    {
        const M2Params copy = original;
        CHECK(palletSpecFor(copy, "PTL") == &copy.pallets[0]);
    }
    CHECK(originalSpec == &original.pallets[0]);
    CHECK(originalSpec->addedWeightLb == 60);
}

TEST_CASE("params number in an error message is shown as written") {
    auto root = completeParams();
    root["trailers"][0]["weightLimitLb"] = -0.1;
    CHECK(parseErrorMessage(root) == "params: trailers[0].weightLimitLb must be positive and finite, got -0.1");
    root = completeParams();
    root["trailers"][0]["stackPositions"] = 30.0000000001;
    CHECK(parseErrorMessage(root)
        == "params: trailers[0].stackPositions must be an integer between 1 and 2147483647, got 30.0000000001");
}

TEST_CASE("params long path keeps the file name in the error message") {
    const std::string path = std::string(300, 'd') + "/orderBuilderParams.json";
    CHECK(loadErrorMessage(path).find("orderBuilderParams.json") != std::string::npos);
}

TEST_CASE("params pallet lookup cannot be called on a temporary params object") {
    // palletSpecFor is an overload set, so is_invocable needs a callable wrapping it.
    const auto lookup = [](auto&& params)
        -> decltype(palletSpecFor(std::forward<decltype(params)>(params), "PTL")) {
        return palletSpecFor(std::forward<decltype(params)>(params), "PTL");
    };
    CHECK(std::is_invocable_v<decltype(lookup), const M2Params&>);
    CHECK_FALSE(std::is_invocable_v<decltype(lookup), M2Params>);
}

TEST_CASE("params pallet id made only of non-breaking spaces is rejected") {
    auto root = completeParams();
    root["pallets"][0]["palletId"] = "\xC2\xA0\xC2\xA0";
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("pallets[0].palletId"), std::runtime_error);
}

TEST_CASE("params trailer code made only of non-breaking spaces is rejected") {
    auto root = completeParams();
    root["trailers"][0]["trailerCode"] = "\xC2\xA0";
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("trailers[0].trailerCode"), std::runtime_error);
}

TEST_CASE("params pallet id mixing non-ASCII and printable ASCII loads") {
    auto root = completeParams();
    root["pallets"][0]["palletId"] = "EP\xC3\x84L";
    CHECK(parseParams(root).pallets[0].palletId == "EP\xC3\x84L");
}

TEST_CASE("params numeric errors name the failing record index") {
    auto root = completeParams();
    root["pallets"][3]["footprintWidthIn"] = 0;
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("pallets[3].footprintWidthIn"), std::runtime_error);
    root = completeParams();
    root["trailers"].push_back(root["trailers"][0]);
    root["trailers"][1]["trailerCode"] = "48FT";
    root["trailers"][1]["weightLimitLb"] = 0;
    CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("trailers[1].weightLimitLb"), std::runtime_error);
}

TEST_CASE("params stack positions reject near-integral and oversized doubles") {
    for (double value : {30.0000000001, 2147483647.5, 2147483648.0, 9007199254740992.0, 9007199254740993.0, 1e-300}) {
        auto root = completeParams();
        root["trailers"][0]["stackPositions"] = value;
        CAPTURE(value);
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("trailers[0].stackPositions"), std::runtime_error);
    }
}

TEST_CASE("params stack positions accept INT_MAX written as a double") {
    auto root = completeParams();
    root["trailers"][0]["stackPositions"] = 2147483647.0;
    CHECK(parseParams(root).trailers[0].stackPositions == std::numeric_limits<int>::max());
}

TEST_CASE("params attempt cap written as -0.0 is an explicit zero") {
    auto root = completeParams();
    root["pass2AttemptCap"] = -0.0;
    const auto params = parseParams(root);
    CHECK(params.pass2AttemptCap == 0);
    CHECK(params.defaultedKeys.empty());
}

TEST_CASE("params pallet ids that differ only by whitespace load as distinct entries") {
    auto root = completeParams();
    root["pallets"][1]["palletId"] = "PTL ";
    const auto params = parseParams(root);
    CHECK(palletSpecFor(params, "PTL") == &params.pallets[0]);
    CHECK(palletSpecFor(params, "PTL ") == &params.pallets[1]);
}

TEST_CASE("params parse error message is capped and marks the truncation") {
    const std::string path = writeParamsFile("_tmp_long_token.json", "{\"a\": \"" + std::string(1000000, 'A'));
    const std::string message = loadErrorMessage(path);
    std::remove(path.c_str());
    CHECK(message.size() < 600);
    CHECK(message.substr(message.size() - 3) == "...");
}

TEST_CASE("params parse error message contains only printable ASCII") {
    const std::string path = writeParamsFile("_tmp_unprintable.json", "{\"a\": tru\x7f\t\xE9}");
    const std::string message = loadErrorMessage(path);
    std::remove(path.c_str());
    REQUIRE_FALSE(message.empty());
    CHECK(isPrintableAscii(message));
}

TEST_CASE("params unprintable path characters are replaced in the message") {
    const std::string message = loadErrorMessage("does/not/exist\t\xC3\xA9.json");
    CHECK(message.find("does/not/exist") != std::string::npos);
    CHECK(isPrintableAscii(message));
}
