#pragma once
// Evidence records (traceability Section 11): one JSON file per executed variant, written by every
// test run into <build>/m5Evidence/<commit>/ (<commit>-dirty when the build had changes not
// committed), named <testId>.<variant>.json. They stay in the build folder and travel with a
// delivery; they are never committed. Fields no run produced stay null.
//
// The listener (m5EvidenceListener.cpp) writes a record for every testId/variant a test case name
// declares. A test adds its own fields (inputs, expected, actual, proof, ...) with note() and input(),
// and reports a missing prerequisite with blocked().

#include "m5EvidenceGate.hpp"
#include "m5Manifest.hpp"
#include "m5Sha256.hpp"

#include "json.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace m5Evidence {

inline const std::string kProfilePath = "config/orderBuilderParams.json";

struct BuildIdentity {
    std::string commit = "unknown";
    bool dirty = true;
    std::string compiler;
    std::string config;
};

// The file the build writes (cmake/m5BuildIdentity.cmake): "commit=<sha>" and "dirty=true|false".
// A missing or unreadable file reads as an unknown, dirty build, which never counts for acceptance.
inline BuildIdentity readBuildIdentity(const std::string& path, std::string compiler, std::string config) {
    BuildIdentity identity;
    identity.compiler = std::move(compiler);
    identity.config = std::move(config);
    std::ifstream file(path);
    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("commit=", 0) == 0) identity.commit = line.substr(7);
        if (line.rfind("dirty=", 0) == 0) identity.dirty = line.substr(6) != "false";
    }
    return identity;
}

inline std::string folderName(const BuildIdentity& build) { return build.commit + (build.dirty ? "-dirty" : ""); }

// The record file of a key; a later case declaring the same key in one run gets a file of its own,
// so neither result overwrites the other and the gate sees both.
inline std::string recordFileName(const std::string& testId, const std::string& variant, int earlierInRun = 0) {
    return testId + "." + variant + (earlierInRun == 0 ? "" : ".duplicate-" + std::to_string(earlierInRun)) + ".json";
}

// PASS needs at least one assertion and none failed; a registered case that asserted nothing is
// NOT_RUN however the process exits; a reported missing prerequisite is BLOCKED.
inline std::string resultOf(int assertions, int failedAssertions, bool caseSucceeded, const std::string& blockedBecause) {
    if (!blockedBecause.empty()) return "BLOCKED";
    if (assertions == 0) return "NOT_RUN";
    return caseSucceeded && failedAssertions == 0 ? "PASS" : "FAIL";
}

// What the running test case adds to its records; the listener clears it at each case start.
inline nlohmann::json& currentNotes() {
    static nlohmann::json notes = nlohmann::json::object();
    return notes;
}

inline void note(const std::string& field, nlohmann::json value) { currentNotes()[field] = std::move(value); }

// The name of the running test case, set by the listener at each case start, so a test that skips itself can name itself.
inline std::string& currentTestCase() {
    static std::string name;
    return name;
}

// Records an input file by path and the SHA-256 of its exact bytes (null when it cannot be read).
inline void input(const std::string& name, const std::string& path) {
    const std::optional<std::string> sha256 = m5Checksum::fileSha256Hex(path);
    currentNotes()["inputs"][name] = {{"path", path}, {"sha256", sha256 ? nlohmann::json(*sha256) : nlohmann::json()}};
}

inline void blocked(const std::string& reason) { currentNotes()["blockedBecause"] = reason; }

struct CaseResult {
    std::string testCase;
    std::string file;
    int line = 0;
    int assertions = 0;
    int failedAssertions = 0;
    bool succeeded = false;
    double seconds = 0.0;
    std::string exception;
};

// One record, in the traceability Section 11 shape, for one variant of an executed test case.
inline nlohmann::json recordJson(const m5Manifest::Variant& variant, const CaseResult& result, const BuildIdentity& build,
                                 const m5Gate::Identity& identity, const nlohmann::json& notes) {
    const std::string blockedBecause = notes.contains("blockedBecause") ? notes.at("blockedBecause").get<std::string>() : "";
    nlohmann::json record = {
        {"testId", variant.testId},
        {"variant", variant.variant},
        {"testCase", result.testCase},
        {"source", {{"file", result.file}, {"line", result.line}}},
        {"fixtureProfile", variant.fixture},
        {"entryPoint", variant.entryPoint},
        {"evidenceScope", variant.evidenceScope},
        {"executionStatus", resultOf(result.assertions, result.failedAssertions, result.succeeded, blockedBecause)},
        {"customerAcceptance", "NOT_EVALUATED"},
        {"pendingReferences", nlohmann::json::array()},
        {"assertions", {{"total", result.assertions}, {"failed", result.failedAssertions}}},
        {"seconds", result.seconds},
        {"build", {{"commit", build.commit}, {"dirty", build.dirty}, {"compiler", build.compiler}, {"config", build.config}}},
        {"requirement", {{"sha256", identity.requirementSha256}}},
        {"manifestSha256", identity.manifestSha256},
        {"profileSha256", identity.profileSha256},
        {"inputs", nlohmann::json::object()},
        {"model", nullptr},
        {"expected", nullptr},
        {"actual", nullptr},
        {"ledger", nullptr},
        {"reservations", nullptr},
        {"validation", nullptr},
        {"proof", nullptr},
        {"stop", nullptr},
        {"seed", nullptr},
        {"threads", nullptr},
        {"machine", nullptr},
        {"notes", ""},
    };
    if (!blockedBecause.empty()) record["reason"] = blockedBecause;
    if (!result.exception.empty()) record["exception"] = result.exception;
    for (const auto& [field, value] : notes.items()) {
        if (field != "blockedBecause") record[field] = value;
    }
    return record;
}

// The gate's view of a record file's content.
inline m5Gate::Record gateRecordOf(const nlohmann::json& record) {
    m5Gate::Record gate;
    gate.testId = record.value("testId", "");
    gate.variant = record.value("variant", "");
    gate.executionStatus = record.value("executionStatus", "");
    gate.testCase = record.value("testCase", "");
    gate.reason = record.value("reason", "");
    if (record.contains("notCovered") && record.at("notCovered").is_string()) gate.notCovered = record.at("notCovered").get<std::string>();
    if (record.contains("assertions")) {
        gate.assertions = record.at("assertions").value("total", 0);
        gate.failedAssertions = record.at("assertions").value("failed", 0);
    }
    if (record.contains("build")) {
        gate.identity.commit = record.at("build").value("commit", "");
        gate.identity.dirty = record.at("build").value("dirty", true);
    }
    if (record.contains("requirement")) gate.identity.requirementSha256 = record.at("requirement").value("sha256", "");
    gate.identity.manifestSha256 = record.value("manifestSha256", "");
    gate.identity.profileSha256 = record.value("profileSha256", "");
    return gate;
}

// Every record file in `folder` (one per key; a duplicate within a run has its own file name).
inline std::vector<m5Gate::Record> readRecords(const std::filesystem::path& folder) {
    std::vector<m5Gate::Record> records;
    std::error_code missing;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, missing)) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream file(entry.path());
        try {
            records.push_back(gateRecordOf(nlohmann::json::parse(file)));
        } catch (const std::exception&) {
            m5Gate::Record unreadable;
            unreadable.testId = entry.path().filename().string();
            unreadable.variant = "unreadable";
            records.push_back(unreadable);
        }
    }
    return records;
}

// The identity of the current build and inputs, as the gate compares records against it.
inline m5Gate::Identity currentIdentity(const BuildIdentity& build, const m5Manifest::Manifest& manifest) {
    m5Gate::Identity identity;
    identity.commit = build.commit;
    identity.dirty = build.dirty;
    identity.requirementSha256 =
        m5Checksum::fileSha256Hex(m5Manifest::kRequirementFolder + manifest.requirementFile).value_or("missing");
    identity.manifestSha256 = m5Checksum::fileSha256Hex(m5Manifest::kManifestPath).value_or("missing");
    identity.profileSha256 = m5Checksum::fileSha256Hex(kProfilePath).value_or("missing");
    return identity;
}

} // namespace m5Evidence
