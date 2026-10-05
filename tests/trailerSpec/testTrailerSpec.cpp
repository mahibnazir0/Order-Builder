#include "doctest.h"
#include "paramsLoader.hpp"
#include "trailerSpec.hpp"

#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace std;
using namespace ob;
using json = nlohmann::json;

namespace {

const string kShippedParamsPath = "config/orderBuilderParams.json";

json shippedParamsJson() {
    ifstream input(kShippedParamsPath);
    REQUIRE(input);
    return json::parse(input);
}

string selectionErrorMessage(const M2Params& params, const string& trailerCode) {
    try {
        selectTrailer(params.trailers, params.sourcePath, trailerCode);
    } catch (const runtime_error& error) {
        return error.what();
    }
    return "";
}

} // namespace

TEST_CASE("trailerSpec: the shipped trailer is selected by code with every confirmed figure") {
    const M2Params params = loadParams(kShippedParamsPath);
    const TrailerSpec& trailer = selectTrailer(params.trailers, params.sourcePath, "53FT_NA");
    CHECK(trailer.trailerCode == "53FT_NA");
    CHECK(trailer.weightLimitLb == 45000);
    CHECK(trailer.stackHeightCeilingIn == 108);
    CHECK(trailer.interiorLengthIn == 630);
    CHECK(trailer.interiorWidthIn == 100);
    CHECK(trailer.stackPositions == 32);
    CHECK_FALSE(trailer.maxStackDepth.has_value());
}

TEST_CASE("trailerSpec: an unknown code names the params file, the key and the listed codes") {
    const M2Params params = loadParams(kShippedParamsPath);
    const string message = selectionErrorMessage(params, "NO_SUCH_TRAILER");
    CHECK(message.find(kShippedParamsPath) != string::npos);
    CHECK(message.find("trailers[].trailerCode") != string::npos);
    CHECK(message.find("'NO_SUCH_TRAILER'") != string::npos);
    CHECK(message.find("listed: 53FT_NA") != string::npos);
}

TEST_CASE("trailerSpec: selection is an exact match on the code") {
    const M2Params params = loadParams(kShippedParamsPath);
    for (const string nearMiss : {"53FT_NA ", "53ft_na", " 53FT_NA", ""}) {
        CAPTURE(nearMiss);
        CHECK_THROWS_AS(selectTrailer(params.trailers, params.sourcePath, nearMiss), runtime_error);
    }
}

TEST_CASE("trailerSpec: an unknown code with control characters is reported as plain ASCII") {
    const M2Params params = loadParams(kShippedParamsPath);
    const string message = selectionErrorMessage(params, "BAD\n\xC2\xA0");
    CHECK(message.find("'BAD" + string(3, '?') + "'") != string::npos);
}

TEST_CASE("trailerSpec: params parsed from memory say so in the unknown-code error") {
    const M2Params params = parseParams(shippedParamsJson());
    CHECK(selectionErrorMessage(params, "NO_SUCH_TRAILER").find("<params not read from a file>")
          != string::npos);
}

TEST_CASE("trailerSpec: with several trailers the named one is returned, not the first") {
    json root = shippedParamsJson();
    json secondTrailer = root["trailers"][0];
    secondTrailer["trailerCode"] = "48FT";
    secondTrailer["stackPositions"] = 28;
    secondTrailer["maxStackDepth"] = 3;
    root["trailers"].push_back(secondTrailer);
    const M2Params params = parseParams(root);
    const TrailerSpec& trailer = selectTrailer(params.trailers, params.sourcePath, "48FT");
    CHECK(trailer.trailerCode == "48FT");
    CHECK(trailer.stackPositions == 28);
    CHECK(trailer.maxStackDepth == 3);
}

TEST_CASE("trailerSpec: a trailer missing any field is rejected naming that field") {
    for (const string field : {"trailerCode", "interiorLengthIn", "interiorWidthIn",
                               "stackHeightCeilingIn", "weightLimitLb", "stackPositions",
                               "maxStackDepth"}) {
        CAPTURE(field);
        json root = shippedParamsJson();
        root["trailers"][0].erase(field);
        const string expectedKey = "trailers[0]." + field;
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains(expectedKey.c_str()),
                             runtime_error);
    }
}

TEST_CASE("trailerSpec: a configured maxStackDepth is read") {
    json root = shippedParamsJson();
    root["trailers"][0]["maxStackDepth"] = 4;
    CHECK(parseParams(root).trailers[0].maxStackDepth == 4);
}

TEST_CASE("trailerSpec: a maxStackDepth that is not a positive integer is rejected") {
    const double infinity = numeric_limits<double>::infinity();
    for (const json& badDepth : {json(0), json(-1), json(2.5), json(NAN), json(infinity),
                                 json(-infinity), json("3"), json::object(), json::array(),
                                 json(true)}) {
        CAPTURE(badDepth.dump());
        json root = shippedParamsJson();
        root["trailers"][0]["maxStackDepth"] = badDepth;
        CHECK_THROWS_WITH_AS(parseParams(root), doctest::Contains("trailers[0].maxStackDepth"),
                             runtime_error);
    }
}
