#pragma once
// Field readers for the params file, shared by the Milestone 2 params and the M4 selection
// settings. Each throws std::runtime_error naming the key: a setting is never silently
// defaulted, cut to range or read as a different type.

#include "json.hpp"

#include <string>

namespace ob {

enum class ZeroAllowed { No, Yes };

// record[key], or a throw naming recordName.key when it is absent or record is not an object.
const nlohmann::json& requiredField(const nlohmann::json& record, const char* key,
                                    const std::string& recordName);

// A finite number above zero (or at least zero with ZeroAllowed::Yes).
double readQuantity(const nlohmann::json& value, const std::string& key,
                    ZeroAllowed zeroAllowed = ZeroAllowed::No);

// A whole number from minimum to INT_MAX; 2.5 or true is refused, never truncated.
int readInteger(const nlohmann::json& value, const std::string& key, int minimum);

std::string readString(const nlohmann::json& value, const std::string& key);

// The params file parsed. Throws naming the file when it cannot be opened or read, or is not
// valid JSON.
nlohmann::json readParamsFile(const std::string& path);

// An array with at least one entry.
void requireNonEmptyArray(const nlohmann::json& value, const std::string& key);

} // namespace ob
