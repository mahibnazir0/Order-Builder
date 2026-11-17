#pragma once
// The acceptance evidence gate (X-RUN-09). M5 is accepted only when every selected required
// (testId, variant) key of the manifest has exactly one current PASS record with real assertions,
// written by a clean build of the current commit against the current requirement, manifest and
// params. A missing, failed, NOT_RUN, BLOCKED, stale or duplicated result blocks acceptance whatever
// exit code the test run had. A conditional variant that no recorded decision selects stays visible
// as deferred and neither passes nor blocks; one deferred without a recorded decision is rejected.

#include "m5Manifest.hpp"

#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace m5Gate {

// What a record was produced from; a record counts only when all of it matches the current build.
struct Identity {
    std::string commit;
    bool dirty = true;
    std::string requirementSha256;
    std::string manifestSha256;
    std::string profileSha256;
};

struct Record {
    std::string testId;
    std::string variant;
    std::string executionStatus;  // PASS, FAIL, NOT_RUN or BLOCKED
    int assertions = 0;
    int failedAssertions = 0;
    Identity identity;
    std::string testCase;
    std::string reason;
    std::string key() const { return testId + "/" + variant; }
};

struct GateResult {
    std::vector<std::string> problems;  // the bundle itself is invalid: duplicates, unknown keys, ...
    std::vector<std::string> passed;
    std::vector<std::string> failed;
    std::vector<std::string> notRun;
    std::vector<std::string> blocked;
    std::vector<std::string> stale;
    std::vector<std::string> deferred;  // unselected conditional variants, with their reason
    std::size_t selected = 0;
    bool accepted() const {
        return problems.empty() && failed.empty() && notRun.empty() && blocked.empty() && stale.empty()
               && passed.size() == selected;
    }
};

inline std::string staleBecause(const Identity& record, const Identity& current) {
    if (record.commit != current.commit) return "commit " + record.commit + ", not " + current.commit;
    if (record.dirty) return "built from a tree with changes not committed";
    if (record.requirementSha256 != current.requirementSha256) return "another requirement version";
    if (record.manifestSha256 != current.manifestSha256) return "another manifest version";
    if (record.profileSha256 != current.profileSha256) return "another params file";
    return "";
}

inline GateResult evaluate(const m5Manifest::Manifest& manifest, const std::vector<Record>& records, const Identity& current) {
    GateResult result;
    if (manifest.requirementSha256 != current.requirementSha256) {
        result.problems.push_back("the manifest was derived from another requirement version");
    }
    if (current.dirty) result.problems.push_back("the current build has changes not committed");
    std::set<std::string> testIds;
    for (const std::string& testId : manifest.testIds) {
        if (!testIds.insert(testId).second) result.problems.push_back("test ID " + testId + " is listed twice");
    }
    std::map<std::string, const m5Manifest::Variant*> variantOf;
    for (const m5Manifest::Variant& variant : manifest.variants) {
        if (!variantOf.emplace(variant.key(), &variant).second) {
            result.problems.push_back("variant " + variant.key() + " is listed twice");
        }
    }
    std::map<std::string, std::vector<const Record*>> recordsOf;
    for (const Record& record : records) {
        if (!variantOf.count(record.key())) {
            result.problems.push_back("a result for " + record.key() + ", which the manifest does not list");
            continue;
        }
        recordsOf[record.key()].push_back(&record);
    }
    for (const auto& [key, variant] : variantOf) {
        const auto found = recordsOf.find(key);
        const std::vector<const Record*> none;
        const std::vector<const Record*>& ofKey = found == recordsOf.end() ? none : found->second;
        if (!variant->selected) {
            if (!variant->decisionRecorded) {
                result.problems.push_back(key + " is deferred without a recorded decision");
            } else {
                const std::string& why = variant->blocker.empty() ? variant->selectionReason : variant->blocker;
                result.deferred.push_back(key + ": " + (why.empty() ? "deferred" : why));
            }
            continue;
        }
        ++result.selected;
        if (ofKey.size() > 1) {
            std::string testCases;
            for (const Record* record : ofKey) testCases += (testCases.empty() ? "" : "; ") + record->testCase;
            result.problems.push_back(key + " has " + std::to_string(ofKey.size()) + " results (" + testCases + ")");
            continue;
        }
        if (ofKey.empty()) {
            if (variant->executionStatus == "BLOCKED") {
                result.blocked.push_back(key + ": " + variant->blocker);
            } else {
                result.notRun.push_back(key);
            }
            continue;
        }
        const Record& record = *ofKey.front();
        const std::string stale = staleBecause(record.identity, current);
        if (!stale.empty()) {
            result.stale.push_back(key + ": " + stale);
        } else if (record.executionStatus == "PASS" && record.assertions > 0 && record.failedAssertions == 0) {
            result.passed.push_back(key);
        } else if (record.executionStatus == "PASS" || record.executionStatus == "NOT_RUN") {
            result.notRun.push_back(key + (record.assertions == 0 ? ": no assertion ran" : ""));
        } else if (record.executionStatus == "BLOCKED") {
            result.blocked.push_back(key + ": " + record.reason);
        } else {
            result.failed.push_back(key);
        }
    }
    return result;
}

// Test IDs a test name carries (with or without a variant): the ID-level coverage check.
inline std::set<std::string> idsNamedIn(const std::string& testName) {
    static const std::regex id(R"((?:^|[^A-Za-z0-9-])((?:ACC|PUB|RD)-[0-9]+|R-[0-9]+[a-z]?|U-[A-Z]{2}-[0-9]+|X-[A-Z0-9]+-[0-9]+)(?=$|[^A-Za-z0-9-]))");
    std::set<std::string> ids;
    for (std::sregex_iterator match(testName.begin(), testName.end(), id), end; match != end; ++match) ids.insert((*match)[1].str());
    return ids;
}

// A manifest test ID with no test yet, and why: NOT_RUN (planned, with the pull request that adds
// it), BLOCKED (a named prerequisite is missing) or EXTERNAL (covered by another ctest entry).
struct Exception {
    std::string status;
    std::string reason;
};

// Every manifest test ID has a test naming it or an exception with a reason; no test name appears
// twice; an exception is not kept for an ID that now has a test.
inline std::vector<std::string> idCoverageProblems(const std::vector<std::string>& manifestIds, const std::vector<std::string>& testNames,
                                                   const std::map<std::string, Exception>& exceptions) {
    std::vector<std::string> problems;
    std::set<std::string> required(manifestIds.begin(), manifestIds.end());
    std::set<std::string> names, covered;
    for (const std::string& name : testNames) {
        if (!names.insert(name).second) problems.push_back("test name registered twice: " + name);
        const std::set<std::string> ids = idsNamedIn(name);
        covered.insert(ids.begin(), ids.end());
    }
    for (const auto& [id, exception] : exceptions) {
        if (!required.count(id)) problems.push_back("exception for " + id + ", which is not a manifest ID");
        if (covered.count(id)) problems.push_back("exception for " + id + ", which now has a test");
        if (exception.reason.empty()) problems.push_back("exception for " + id + " gives no reason");
        if (exception.status != "NOT_RUN" && exception.status != "BLOCKED" && exception.status != "EXTERNAL") {
            problems.push_back("exception for " + id + " has status '" + exception.status + "'");
        }
    }
    for (const std::string& id : required) {
        if (!covered.count(id) && !exceptions.count(id)) problems.push_back("manifest ID " + id + " has no test");
    }
    return problems;
}

} // namespace m5Gate
