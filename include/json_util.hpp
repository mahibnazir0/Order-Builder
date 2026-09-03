#pragma once
// json_util.hpp — shared JSON field-access helper for the demand and
// placeholder readers (both read flat records off a top-level array with the
// same "missing/wrong-type field defaults, doesn't throw" discipline).

#include "json.hpp"
#include "logger.hpp"

#include <string>

namespace ob {

// Safe field access: return j[key] as type T, or `fallback` if the key is
// missing, null, or the wrong type. Keeps one bad record from killing the
// whole load — the Validator, not the reader, decides whether the result is
// acceptable. Logged at debug level so a field silently defaulting (e.g.
// "AVAIL_QTY": "N/A") is distinguishable from a legitimately absent one.
template <typename T>
T get_or(const nlohmann::json& j, const char* key, T fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    try {
        return it->get<T>();
    } catch (const nlohmann::json::exception& e) {
        LOG_DEBUG(std::string("Field '") + key + "' has an unexpected type, "
                  "using fallback: " + e.what());
        return fallback;
    }
}

} // namespace ob
