#pragma once
// The M5 test manifest (docs/m5/m5TestManifest.json) as the tests use it: every named test and every
// (testId, variant) key with its requirement and status, and the requirement it was derived from.
//
// A test case declares the variants it executes in its name as testId/variant tokens, for example
// TEST_CASE("X-RUN-09/duplicateID: ..."). Only declared variants get evidence records; a name that
// carries a test ID alone declares no variant.

#include "json.hpp"

#include <cstddef>
#include <fstream>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace m5Manifest {

inline const std::string kManifestPath = "docs/m5/m5TestManifest.json";
inline const std::string kRequirementFolder = "docs/m5/";

struct Variant {
    std::string testId;
    std::string variant;
    std::string requirement;      // required, requiredForAcceptance or conditional
    std::string executionStatus;  // as the manifest records it: NOT_RUN, BLOCKED, ...
    std::string blocker;          // the named reason a BLOCKED variant cannot run
    std::string evidenceScope;
    std::string entryPoint;
    std::string fixture;
    // A conditional variant counts only when a recorded decision selects it.
    bool selected = true;
    // An unselected conditional variant says why: a recorded decision (reason, decision reference and
    // activation condition), or the named blocker of the decision it waits for.
    bool decisionRecorded = false;
    std::string selectionReason;
    std::string key() const { return testId + "/" + variant; }
};

struct Manifest {
    std::string requirementFile;
    std::string requirementSha256;
    std::size_t namedTestCount = 0;
    std::vector<std::string> testIds;  // in manifest order, duplicates kept so they can be found
    std::vector<Variant> variants;
};

inline std::string textOr(const nlohmann::json& object, const char* field, const std::string& fallback = "") {
    const auto found = object.find(field);
    return found != object.end() && found->is_string() ? found->get<std::string>() : fallback;
}

inline Manifest fromJson(const nlohmann::json& root) {
    Manifest manifest;
    const nlohmann::json& source = root.at("requirementSource");
    manifest.requirementFile = source.at("fileName").get<std::string>();
    manifest.requirementSha256 = source.at("sha256").get<std::string>();
    manifest.namedTestCount = source.at("namedTestCount").get<std::size_t>();
    for (const nlohmann::json& test : root.at("tests")) {
        const std::string testId = test.at("id").get<std::string>();
        manifest.testIds.push_back(testId);
        for (const nlohmann::json& entry : test.at("variants")) {
            Variant variant;
            variant.testId = testId;
            variant.variant = entry.at("variant").get<std::string>();
            variant.requirement = entry.at("requirement").get<std::string>();
            variant.executionStatus = entry.at("executionStatus").get<std::string>();
            variant.blocker = textOr(entry, "blocker", textOr(test, "blocker"));
            variant.evidenceScope = textOr(entry, "evidenceScope");
            variant.entryPoint = textOr(entry, "entryPointOverride", textOr(test, "entryPoint"));
            variant.fixture = textOr(test, "fixture");
            if (variant.requirement == "conditional") {
                const auto selected = entry.find("selectedForCurrentMilestone");
                variant.selected = selected != entry.end() && selected->is_boolean() && selected->get<bool>();
                variant.selectionReason = textOr(entry, "selectionReason");
                const bool decisionNamed = !variant.selectionReason.empty() && !textOr(entry, "selectionDecisionRef").empty()
                                           && !textOr(entry, "activationCondition").empty();
                variant.decisionRecorded = decisionNamed || !variant.blocker.empty();
            }
            manifest.variants.push_back(std::move(variant));
        }
    }
    return manifest;
}

// The manifest at `path`; empty, with the reason in `error`, when it is missing or malformed.
inline std::optional<Manifest> load(const std::string& path, std::string* error) {
    std::ifstream file(path);
    if (!file) {
        if (error) *error = "cannot open " + path;
        return std::nullopt;
    }
    try {
        return fromJson(nlohmann::json::parse(file));
    } catch (const std::exception& failure) {
        if (error) *error = path + ": " + failure.what();
        return std::nullopt;
    }
}

// The testId/variant keys a test case name declares, in the order they appear.
inline std::vector<std::pair<std::string, std::string>> declaredKeys(const std::string& testCaseName) {
    static const std::regex key(
        R"((?:^|[\s,;(])((?:ACC|PUB|RD)-[0-9]+|R-[0-9]+[a-z]?|U-[A-Z]{2}-[0-9]+|X-[A-Z0-9]+-[0-9]+)/([A-Za-z][A-Za-z0-9]*))");
    std::vector<std::pair<std::string, std::string>> keys;
    for (std::sregex_iterator match(testCaseName.begin(), testCaseName.end(), key), end; match != end; ++match) {
        keys.emplace_back((*match)[1].str(), (*match)[2].str());
    }
    return keys;
}

} // namespace m5Manifest
