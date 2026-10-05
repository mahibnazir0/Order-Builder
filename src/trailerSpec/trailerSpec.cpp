#include "trailerSpec.hpp"

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
    const string fileName = sourcePath.empty() ? "<params not read from a file>" : sourcePath;
    throw runtime_error("params: " + fileName + ": trailers[].trailerCode has no '"
        + printableCode(trailerCode) + "' (listed: "
        + (listedCodes.empty() ? "none" : listedCodes) + ")");
}

} // namespace ob
