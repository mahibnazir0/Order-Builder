#pragma once

// Shared data structures for Order Builder.
//
// Milestone 1: placeholder fields only. Real field lists come once Tom's
// sample JSON / spec is available. Keep this compiling; do not add logic.

#include <cstdint>
#include <string>
#include <vector>

namespace ob {

// ProductRecord (the product master struct) now lives in product_types.hpp --
// include that header where it's needed.
//
// STRRecord, CTLRecord, DNMRecord, and DemandFile (the demand file structs)
// now live in demand_types.hpp -- include that header where they're needed.
//
// PlaceholderRecord (and PlaceholderLoadResult) now live in
// placeholder_types.hpp -- include that header where they're needed.
//
// JoinResult (and JoinedLine, ProductIndex) now live in joiner.hpp --
// include that header where they're needed.
//
// ValidationIssue, ValidationReport and ValidationConfig (replacing the old
// ValidationError) now live in validator.hpp -- include that header where
// they're needed.
//
// LaneSummary (and DaySummary) now live in reporter.hpp -- include that
// header where they're needed.
//
// Every scaffold struct has now moved to its own module header, so this file
// declares nothing. It is kept as a signpost to where each type went.

}  // namespace ob
