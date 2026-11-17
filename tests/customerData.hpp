#pragma once
// The confidential client fixtures are gitignored. Each suite that reads them has a guard test
// that fails when they are missing, so a clone without them never passes a suite that ran none
// of them. OB_CUSTOMER_DATA=optional lets a build that can never hold them (the PR checks) report
// each as skipped instead; unset, or "required", the guards fail, as they must on our machines.
#include "doctest.h"

#include <cstdlib>
#include <string>

namespace customerData {

enum class Requirement { Required, Optional, Unreadable };

inline Requirement requirement() {
    const char* setting = std::getenv("OB_CUSTOMER_DATA");
    if (setting == nullptr || std::string(setting) == "required") return Requirement::Required;
    if (std::string(setting) == "optional") return Requirement::Optional;
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
        FAIL_CHECK("OB_CUSTOMER_DATA is \"" << std::getenv("OB_CUSTOMER_DATA") << "\", not \"required\" or \"optional\"; " << what);
        return;
    case Requirement::Required:
        FAIL_CHECK(what);
        return;
    }
}

} // namespace customerData
