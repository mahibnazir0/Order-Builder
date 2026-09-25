#pragma once
// json_util.hpp — shared JSON field-access helper for the demand and
// placeholder readers (both read flat records off a top-level array with the
// same "missing/wrong-type field defaults, doesn't throw" discipline).

#include "json.hpp"
#include "logger.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
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

// Integer field access with the same fallback discipline as get_or<int>, but
// strict about the value: get<int> would static_cast 2.9 to 2, true to 1 and
// 4294967297 to 1, turning a malformed count into a plausible one the
// Validator can't catch. Only an integer, or a float with no fractional part,
// that fits in int is accepted; anything else returns `fallback`.
inline int getIntegerOr(const nlohmann::json& j, const char* key, int fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;

    constexpr int intMin = std::numeric_limits<int>::min();
    constexpr int intMax = std::numeric_limits<int>::max();
    if (it->is_number_unsigned()) {
        const auto value = it->get<std::uint64_t>();
        if (value <= static_cast<std::uint64_t>(intMax)) return static_cast<int>(value);
    } else if (it->is_number_integer()) {
        const auto value = it->get<std::int64_t>();
        if (value >= intMin && value <= intMax) return static_cast<int>(value);
    } else if (it->is_number_float()) {
        const double value = it->get<double>();
        if (std::isfinite(value) && std::trunc(value) == value
            && value >= intMin && value <= intMax) {
            return static_cast<int>(value);
        }
    }
    LOG_DEBUG(std::string("Field '") + key + "' is not an integer in int range, "
              "using fallback: " + it->dump());
    return fallback;
}

// A top-level block (STR, CTL, DNM, PHOLDER, ...) is optional: absent means
// "no records of this kind." Returns nullptr for that case. But if the key
// IS present, it must be an array — a present-but-wrong-shaped block (e.g.
// STR given as a JSON object) is malformed input, not an empty one, and
// `contains(key) && is_array()` would silently treat the two as the same
// thing. `context` names the file/block in the thrown message.
inline const nlohmann::json* get_optional_array(const nlohmann::json& j,
                                                 const char* key,
                                                 const char* context) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return nullptr;
    if (!it->is_array()) {
        throw std::runtime_error(std::string(context) + ": '" + key
                                  + "' is present but is not an array");
    }
    return &(*it);
}

} // namespace ob
