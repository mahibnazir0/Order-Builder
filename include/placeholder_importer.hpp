#pragma once
// placeholder_importer.hpp — declares the placeholder JSON reader
#include "placeholder_types.hpp"
#include <string>

namespace ob {

class PlaceholderImporter {
public:
    // Read a placeholder file (top-level key: PHOLDER). The name varies by extract
    // (PlaceHolder-1.json, Placeholder-N.json, UUID-named from 29 Sep).
    // Throws std::runtime_error if the file cannot be opened or is not valid JSON.
    // Missing fields inside a record default to empty/zero — the Validator,
    // not the Importer, decides whether that is acceptable. Same discipline
    // as the demand and product readers.
    static PlaceholderLoadResult load(const std::string& json_path);
};

} // namespace ob
