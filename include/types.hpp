#pragma once

// Shared data structures for Order Builder.
//
// Milestone 1: placeholder fields only. Real field lists come once Tom's
// sample JSON / spec is available. Keep this compiling; do not add logic.

#include <cstdint>
#include <string>
#include <vector>

namespace ob {

// A single product master record.
// TODO(mahib): replace placeholder fields with real product schema.
struct ProductRecord {
    std::string product_id;  // TODO(mahib): confirm key field name
};

// STRRecord, CTLRecord, DNMRecord, and DemandFile (the demand file structs)
// now live in demand_types.hpp -- include that header where they're needed.

// A single record from the placeholder input file.
// TODO(mahib): replace with real placeholder-file schema once defined.
struct PlaceholderRecord {
    std::string id;  // TODO(mahib)
};

// A single validation failure surfaced by Validator.
// TODO(mahib): confirm error fields (severity, source record, message, etc.).
struct ValidationError {
    std::string message;  // TODO(mahib)
};

// Result of joining product/demand/placeholder data.
// TODO(dev2): confirm join key(s) and result shape.
struct JoinResult {
    std::string key;  // TODO(dev2)
};

// Summary of a single freight lane, produced by Reporter.
// TODO(mahib): confirm summary fields (lane id, weight, cube, stops, etc.).
struct LaneSummary {
    std::string lane_id;  // TODO(mahib)
};

}  // namespace ob
