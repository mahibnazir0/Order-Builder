#pragma once
// placeholder_importer.hpp — declares the placeholder JSON reader
#include "placeholder_types.hpp"
#include <string>

namespace ob {

class PlaceholderImporter {
public:
    // Read PlaceHolder-1.json (top-level key: PHOLDER).
    // Throws std::runtime_error if the file cannot be opened or is not valid JSON.
    // Missing fields inside a record default to empty/zero — the Validator,
    // not the Importer, decides whether that is acceptable. Same discipline
    // as the demand and product readers.
    static PlaceholderLoadResult load(const std::string& json_path);
};

} // namespace ob
