#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ob {

// The only home for what a truck is. Every trailer figure (payload, interior dimensions,
// positions, depth) comes from the params file through this type; nothing else may carry
// these values as literals.
struct TrailerSpec {
    std::string trailerCode;
    double interiorLengthIn = 0.0;
    double interiorWidthIn = 0.0;
    double stackHeightCeilingIn = 0.0;
    double weightLimitLb = 0.0;
    int stackPositions = 0;
    // Empty means the params file states there is no depth limit (JSON null). A missing key
    // is a parse error, so "no limit" is always a written decision, never a silent default.
    std::optional<int> maxStackDepth;
};

// Exact match on trailerCode ("53FT_NA" and "53FT_NA " differ). An unknown code throws
// std::runtime_error naming the params file, the trailers[].trailerCode key and the codes
// that do exist. sourcePath is the params file the trailers were read from.
const TrailerSpec& selectTrailer(const std::vector<TrailerSpec>& trailers,
                                 const std::string& sourcePath,
                                 const std::string& trailerCode);
// Prevent returning a reference into a trailer list destroyed at the end of the call.
const TrailerSpec& selectTrailer(std::vector<TrailerSpec>&& trailers,
                                 const std::string& sourcePath,
                                 const std::string& trailerCode) = delete;

} // namespace ob
