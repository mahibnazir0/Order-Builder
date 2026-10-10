#include "trailerSpec.hpp"

#include <limits>
#include <stdexcept>

using namespace std;

namespace ob {

namespace {

// The code comes from the command line, so keep the error message plain ASCII.
string printableCode(const string& trailerCode) {
    string printable = trailerCode;
    for (char& character : printable) {
        const auto code = static_cast<unsigned char>(character);
        if (code < 0x20 || code > 0x7e) character = '?';
    }
    return printable;
}

double unitLoadCapacity(const TrailerSpec& trailer) {
    if (!trailer.maxStackDepth) return numeric_limits<double>::infinity();
    return static_cast<double>(trailer.stackPositions) * *trailer.maxStackDepth;
}

bool isAtLeastAsLarge(const TrailerSpec& candidate, const TrailerSpec& other) {
    return candidate.weightLimitLb >= other.weightLimitLb
        && candidate.stackHeightCeilingIn >= other.stackHeightCeilingIn
        && candidate.stackHeightCeilingIn * candidate.stackPositions
               >= other.stackHeightCeilingIn * other.stackPositions
        && unitLoadCapacity(candidate) >= unitLoadCapacity(other);
}

string fileNameFor(const string& sourcePath) {
    return sourcePath.empty() ? "<params not read from a file>" : sourcePath;
}

} // anonymous namespace

const TrailerSpec& selectTrailer(const vector<TrailerSpec>& trailers,
                                 const string& sourcePath,
                                 const string& trailerCode) {
    for (const auto& trailer : trailers) {
        if (trailer.trailerCode == trailerCode) return trailer;
    }
    string listedCodes;
    for (const auto& trailer : trailers) {
        if (!listedCodes.empty()) listedCodes += ", ";
        listedCodes += trailer.trailerCode;
    }
    throw runtime_error("params: " + fileNameFor(sourcePath) + ": trailers[].trailerCode has no '"
        + printableCode(trailerCode) + "' (listed: "
        + (listedCodes.empty() ? "none" : listedCodes) + ")");
}

const TrailerSpec& largestTrailer(const vector<TrailerSpec>& trailers, const string& sourcePath) {
    if (trailers.empty()) {
        throw runtime_error("params: " + fileNameFor(sourcePath) + ": trailers lists none");
    }
    const TrailerSpec* largest = nullptr;
    for (const auto& candidate : trailers) {
        bool largestOnEveryFigure = true;
        for (const auto& other : trailers) {
            if (!isAtLeastAsLarge(candidate, other)) {
                largestOnEveryFigure = false;
                break;
            }
        }
        if (largestOnEveryFigure
            && (largest == nullptr || candidate.trailerCode < largest->trailerCode)) {
            largest = &candidate;
        }
    }
    if (largest == nullptr) {
        throw runtime_error("params: " + fileNameFor(sourcePath)
            + ": no trailer is largest on payload, interior height, stack positions and depth"
              " together; name one with --trailer");
    }
    return *largest;
}

} // namespace ob
