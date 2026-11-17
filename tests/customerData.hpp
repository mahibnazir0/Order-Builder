#pragma once
// The confidential client fixtures are gitignored. Each suite that reads them has a guard test
// that fails when they are missing, so a clone without them never passes a suite that ran none
// of them. OB_CUSTOMER_DATA=optional lets a build that can never hold them (the PR checks) report
// each as skipped instead; unset, or "required", the guards fail, as they must on our machines.
#include "doctest.h"

#include <cstdlib>
#include <optional>
#include <string>

namespace customerData {

// OB_CUSTOMER_DATA as set, or nothing when unset. MSVC rejects std::getenv under /W4 /WX (C4996),
// so Windows reads it with _dupenv_s.
inline std::optional<std::string> setting() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "OB_CUSTOMER_DATA") != 0 || value == nullptr) return std::nullopt;
    std::string read(value);
    std::free(value);
    value = nullptr;
    return read;
#else
    const char* value = std::getenv("OB_CUSTOMER_DATA");
    if (value == nullptr) return std::nullopt;
    return std::string(value);
#endif
}

enum class Requirement { Required, Optional, Unreadable };

inline Requirement requirement() {
    const std::optional<std::string> value = setting();
    if (!value || *value == "required") return Requirement::Required;
    if (*value == "optional") return Requirement::Optional;
    return Requirement::Unreadable;
}

// Fails the calling test for missing customer data, unless OB_CUSTOMER_DATA=optional, where it is
// reported as skipped. A value other than "required" or "optional" fails, naming it, so a typo
// cannot turn the guards off.
inline void reportMissing(const std::string& what) {
    switch (requirement()) {
    case Requirement::Optional:
        MESSAGE("skipped (data not present): " << what);
        return;
    case Requirement::Unreadable:
        FAIL_CHECK("OB_CUSTOMER_DATA is \"" << setting().value_or("") << "\", not \"required\" or \"optional\"; " << what);
        return;
    case Requirement::Required:
        FAIL_CHECK(what);
        return;
    }
}

} // namespace customerData
