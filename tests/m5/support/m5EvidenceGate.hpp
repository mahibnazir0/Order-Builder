#pragma once
// The acceptance evidence gate (X-RUN-09): every required v7 ID has a registered test, or a named
// exception with its reason. A missing ID, a duplicate ID or test name, a stale or reasonless
// exception is a problem; a BLOCKED ID is never counted as a pass.

#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace m5Gate {

struct Exception {
    std::string status;  // BLOCKED or EXTERNAL
    std::string reason;
};

struct GateResult {
    std::vector<std::string> problems;
    std::vector<std::string> blocked;
    bool complete() const { return problems.empty() && blocked.empty(); }
};

inline std::set<std::string> idsNamedIn(const std::string& testName) {
    static const std::regex id(R"(\b[A-Z]+(?:-[A-Za-z0-9]+)+\b)");
    std::set<std::string> ids;
    for (std::sregex_iterator match(testName.begin(), testName.end(), id), end; match != end; ++match) ids.insert(match->str());
    return ids;
}

inline GateResult evaluateGate(const std::vector<std::string>& requiredIds, const std::vector<std::string>& testNames,
                               const std::map<std::string, Exception>& exceptions) {
    GateResult result;
    std::set<std::string> required, names, covered;
    for (const std::string& id : requiredIds) {
        if (!required.insert(id).second) result.problems.push_back("required ID " + id + " is listed twice");
    }
    for (const std::string& name : testNames) {
        if (!names.insert(name).second) result.problems.push_back("test name registered twice: " + name);
        const std::set<std::string> ids = idsNamedIn(name);
        covered.insert(ids.begin(), ids.end());
    }
    for (const auto& [id, exception] : exceptions) {
        if (!required.count(id)) result.problems.push_back("exception for " + id + ", which is not a required ID");
        if (exception.reason.empty()) result.problems.push_back("exception for " + id + " gives no reason");
        if (exception.status != "BLOCKED" && exception.status != "EXTERNAL") {
            result.problems.push_back("exception for " + id + " has status '" + exception.status + "'");
        }
    }
    for (const std::string& id : required) {
        const auto exception = exceptions.find(id);
        if (exception != exceptions.end() && exception->second.status == "BLOCKED") {
            result.blocked.push_back(id);
        } else if (!covered.count(id) && exception == exceptions.end()) {
            result.problems.push_back("required ID " + id + " has no registered test");
        }
    }
    return result;
}

} // namespace m5Gate
