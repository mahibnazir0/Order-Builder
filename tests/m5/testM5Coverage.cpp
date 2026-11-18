// The M5 evidence registry and acceptance gate (X-RUN-09): the committed v12 manifest, the
// testId/variant keys the tests declare, the gate on synthetic evidence bundles, and the gate on this
// build's own records.
#include "doctest.h"
#include "../runTempFolder.hpp"
#include "support/m5Evidence.hpp"
#include "support/m5EvidenceGate.hpp"
#include "support/m5Manifest.hpp"
#include "support/m5Sha256.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using m5Gate::GateResult;
using m5Gate::Identity;
using m5Gate::Record;
using m5Manifest::Manifest;
using m5Manifest::Variant;

namespace {

vector<string> linesOf(const string& path) {
    ifstream file(path);
    REQUIRE_MESSAGE(file.good(), "missing " << path);
    vector<string> lines;
    for (string line; getline(file, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] != '#') lines.push_back(line);
    }
    return lines;
}

// Every TEST_CASE name in the M5 test sources.
vector<string> registeredTestNames() {
    static const regex testCase(R"(TEST_CASE\("((?:[^"\\]|\\.)*)\")");
    vector<string> names;
    for (const filesystem::directory_entry& entry : filesystem::directory_iterator("tests/m5")) {
        if (entry.path().extension() != ".cpp") continue;
        ifstream file(entry.path());
        stringstream text;
        text << file.rdbuf();
        const string source = text.str();
        for (sregex_iterator match(source.begin(), source.end(), testCase), end; match != end; ++match) names.push_back((*match)[1]);
    }
    return names;
}

map<string, m5Gate::Exception> exceptionsOf(const string& path) {
    map<string, m5Gate::Exception> exceptions;
    for (const string& line : linesOf(path)) {
        const size_t first = line.find('|'), second = line.find('|', first + 1);
        REQUIRE(second != string::npos);
        exceptions[line.substr(0, first)] = {line.substr(first + 1, second - first - 1), line.substr(second + 1)};
    }
    return exceptions;
}

Manifest committedManifest() {
    string error;
    const optional<Manifest> manifest = m5Manifest::load(m5Manifest::kManifestPath, &error);
    REQUIRE_MESSAGE(manifest.has_value(), error);
    return *manifest;
}

Variant variantOf(const string& testId, const string& name, const string& requirement = "required",
                  const string& status = "NOT_RUN") {
    Variant variant;
    variant.testId = testId;
    variant.variant = name;
    variant.requirement = requirement;
    variant.executionStatus = status;
    return variant;
}

// A conditional variant deferred, or selected, by a recorded decision, as R-7's integration variants.
Variant conditionalVariant(const string& testId, const string& name, bool selected) {
    Variant variant = variantOf(testId, name, "conditional", selected ? "NOT_RUN" : "BLOCKED");
    variant.selected = selected;
    variant.decisionRecorded = true;
    variant.blocker = selected ? "" : "per-lane override not agreed";
    return variant;
}

Manifest syntheticManifest(const vector<Variant>& variants) {
    Manifest manifest;
    manifest.requirementFile = "spec.md";
    manifest.requirementSha256 = "spec-1";
    for (const Variant& variant : variants) {
        if (find(manifest.testIds.begin(), manifest.testIds.end(), variant.testId) == manifest.testIds.end()) {
            manifest.testIds.push_back(variant.testId);
        }
    }
    manifest.namedTestCount = manifest.testIds.size();
    manifest.variants = variants;
    return manifest;
}

Identity currentBuild() { return {"c1", false, "spec-1", "manifest-1", "profile-1"}; }

Record resultFor(const string& testId, const string& variant, const string& status = "PASS", int assertions = 3) {
    Record record;
    record.testId = testId;
    record.variant = variant;
    record.executionStatus = status;
    record.assertions = assertions;
    record.failedAssertions = status == "FAIL" ? 1 : 0;
    record.identity = currentBuild();
    record.testCase = testId + "/" + variant + " synthetic case";
    return record;
}

bool listed(const vector<string>& entries, const string& prefix) {
    return any_of(entries.begin(), entries.end(), [&](const string& entry) { return entry.rfind(prefix, 0) == 0; });
}

// The gate on this build's own records: the evidence folder of the current commit.
struct BuildStatus {
    Manifest manifest;
    m5Evidence::BuildIdentity build;
    filesystem::path folder;
    GateResult result;
};

BuildStatus statusOfThisBuild() {
    BuildStatus status;
    status.manifest = committedManifest();
#if defined(OB_M5_EVIDENCE_DIR) && defined(OB_M5_BUILD_IDENTITY) && defined(OB_M5_COMPILER) && defined(OB_M5_BUILD_CONFIG)
    status.build = m5Evidence::readBuildIdentity(OB_M5_BUILD_IDENTITY, OB_M5_COMPILER, OB_M5_BUILD_CONFIG);
    status.folder = filesystem::path(OB_M5_EVIDENCE_DIR) / m5Evidence::folderName(status.build);
#endif
    status.result = m5Gate::evaluate(status.manifest, m5Evidence::readRecords(status.folder),
                                     m5Evidence::currentIdentity(status.build, status.manifest));
    return status;
}

string statusText(const BuildStatus& status) {
    const GateResult& result = status.result;
    ostringstream text;
    text << "M5 evidence status: commit " << status.build.commit << (status.build.dirty ? " (changes not committed)" : "")
         << ", " << status.build.compiler << " " << status.build.config << "\n"
         << "Manifest variants " << status.manifest.variants.size() << ", selected " << result.selected << ": PASS "
         << result.passed.size() << ", FAIL " << result.failed.size() << ", NOT_RUN " << result.notRun.size()
         << ", BLOCKED " << result.blocked.size() << ", PARTIAL " << result.partial.size() << ", stale " << result.stale.size() << "; deferred "
         << result.deferred.size() << "; problems " << result.problems.size() << "\n"
         << "Accepted: " << (result.accepted() ? "yes" : "no") << "\n";
    const vector<pair<string, const vector<string>*>> lists{{"PROBLEM", &result.problems}, {"PASS", &result.passed},
                                                           {"FAIL", &result.failed},       {"PARTIAL", &result.partial}, {"STALE", &result.stale},
                                                           {"BLOCKED", &result.blocked},   {"DEFERRED", &result.deferred},
                                                           {"NOT_RUN", &result.notRun}};
    for (const auto& [label, entries] : lists) {
        for (const string& entry : *entries) text << label << " " << entry << "\n";
    }
    return text.str();
}

}  // namespace

TEST_CASE("m5Registry: the committed manifest is the v12 one: requirement hash, 304 tests, 557 unique keys" * doctest::test_suite("m5")) {
    const Manifest manifest = committedManifest();
    CHECK(manifest.requirementFile == "M5_Edge_Cases_and_Tests_v12.md");
    CHECK(m5Checksum::fileSha256Hex(m5Manifest::kRequirementFolder + manifest.requirementFile) == manifest.requirementSha256);
    CHECK(manifest.namedTestCount == 304);
    CHECK(set<string>(manifest.testIds.begin(), manifest.testIds.end()).size() == 304);
    CHECK(manifest.testIds.size() == 304);
    set<string> keys;
    map<string, size_t> byStatus, byRequirement;
    for (const Variant& variant : manifest.variants) {
        keys.insert(variant.key());
        ++byStatus[variant.executionStatus];
        ++byRequirement[variant.requirement];
    }
    CHECK(manifest.variants.size() == 557);
    CHECK(keys.size() == 557);
    // Decision D1 selected R-7's two conditional integration variants and decision 4 (both 17 Nov 2026)
    // X-ROUTE-05/synthetic is PARTIAL; the remaining manifest statuses are 553 NOT_RUN and 3 BLOCKED.
    CHECK(byStatus["NOT_RUN"] == 553);
    CHECK(byStatus["BLOCKED"] == 3);
    CHECK(byStatus["PARTIAL"] == 1);
    CHECK(byRequirement["required"] == 548);
    CHECK(byRequirement["conditional"] == 9);
    for (const Variant& variant : manifest.variants) {
        const bool selectedByDecision = (variant.testId == "R-7" && (variant.variant == "m4Path" || variant.variant == "m5Path"))
                                        || (variant.testId == "U-AC-04" && variant.variant == "zeroAndRange")
                                        || (variant.testId == "U-FL-03" && variant.variant == "zeroAndMaximumRange");
        if (!selectedByDecision) continue;
        CHECK(variant.selected);
        CHECK(variant.decisionRecorded);
        CHECK(variant.executionStatus == "NOT_RUN");
    }
}

TEST_CASE("m5Registry: every testId/variant a test name declares is a manifest key, and no key is declared twice" * doctest::test_suite("m5")) {
    const Manifest manifest = committedManifest();
    set<string> known;
    for (const Variant& variant : manifest.variants) known.insert(variant.key());
    map<string, int> declared;
    for (const string& name : registeredTestNames()) {
        for (const auto& [testId, variant] : m5Manifest::declaredKeys(name)) {
            const string key = testId + "/" + variant;
            INFO(name);
            CHECK_MESSAGE(known.count(key) == 1, key << " is not a manifest key");
            ++declared[key];
        }
    }
    for (const auto& [key, cases] : declared) CHECK_MESSAGE(cases == 1, key << " is declared by " << cases << " test cases");
}

TEST_CASE("m5Registry: every manifest test ID has an M5 test or a recorded exception" * doctest::test_suite("m5")) {
    const Manifest manifest = committedManifest();
    const vector<string> problems = m5Gate::idCoverageProblems(manifest.testIds, registeredTestNames(),
                                                               exceptionsOf("tests/m5/data/m5CoverageExceptions.txt"));
    for (const string& problem : problems) INFO(problem);
    CHECK(problems.empty());
}

TEST_CASE("m5Registry: a test name declares exactly the testId/variant tokens it carries" * doctest::test_suite("m5")) {
    using Keys = vector<pair<string, string>>;
    CHECK(m5Manifest::declaredKeys("X-RUN-09/duplicateID U-PB-08/payloadExactAndEpsilon: the gate rejects it")
          == Keys{{"X-RUN-09", "duplicateID"}, {"U-PB-08", "payloadExactAndEpsilon"}});
    CHECK(m5Manifest::declaredKeys("R-1a/m4Path, R-1b/m5Path and X-PAT-13/twoAcross100") ==
          Keys{{"R-1a", "m4Path"}, {"R-1b", "m5Path"}, {"X-PAT-13", "twoAcross100"}});
    CHECK(m5Manifest::declaredKeys("ACC-1/scalarSuite PUB-02/x RD-13/y") == Keys{{"ACC-1", "scalarSuite"}, {"PUB-02", "x"}, {"RD-13", "y"}});
    CHECK(m5Manifest::declaredKeys("R-1c X-REB-05 X-RES-09 a broken truck is repaired on a copy").empty());
    CHECK(m5Manifest::declaredKeys("the SHA-256/abc of TOM-1/x is not a key").empty());
}

TEST_CASE("m5Evidence: a record carries its key, result, assertions and build identity; fields no run produced stay null" * doctest::test_suite("m5")) {
    Variant variant = variantOf("X-RUN-09", "synthetic");
    variant.evidenceScope = "SYNTHETIC";
    variant.fixture = "FX-COUNT";
    m5Evidence::CaseResult passed;
    passed.testCase = "X-RUN-09/synthetic: case";
    passed.assertions = 4;
    passed.succeeded = true;
    const m5Evidence::BuildIdentity build{"c1", false, "MSVC 19.44", "Release"};
    const nlohmann::json notes = {{"expected", {{"trucks", 2}}}};
    const nlohmann::json record = m5Evidence::recordJson(variant, passed, build, currentBuild(), notes);
    CHECK(record.at("testId") == "X-RUN-09");
    CHECK(record.at("variant") == "synthetic");
    CHECK(record.at("executionStatus") == "PASS");
    CHECK(record.at("assertions").at("total") == 4);
    CHECK(record.at("build").at("commit") == "c1");
    CHECK(record.at("build").at("dirty") == false);
    CHECK(record.at("requirement").at("sha256") == "spec-1");
    CHECK(record.at("expected").at("trucks") == 2);
    for (const char* field : {"actual", "ledger", "proof", "stop", "seed", "machine"}) CHECK(record.at(field).is_null());

    const filesystem::path folder = testTemp::runTempFolder() / "evidenceRecord";
    filesystem::create_directories(folder);
    ofstream(folder / m5Evidence::recordFileName("X-RUN-09", "synthetic")) << record.dump(1);
    const vector<Record> read = m5Evidence::readRecords(folder);
    REQUIRE(read.size() == 1);
    CHECK(read.front().key() == "X-RUN-09/synthetic");
    CHECK(read.front().executionStatus == "PASS");
    CHECK(read.front().assertions == 4);
    CHECK(m5Gate::staleBecause(read.front().identity, currentBuild()).empty());

    CHECK(m5Evidence::resultOf(4, 1, false, "") == "FAIL");
    CHECK(m5Evidence::resultOf(4, 0, true, "fixture missing") == "BLOCKED");
    CHECK(m5Evidence::recordFileName("X-RUN-09", "synthetic", 1) != m5Evidence::recordFileName("X-RUN-09", "synthetic"));
}

TEST_CASE("X-RUN-09/synthetic: only a complete, current bundle is accepted, whatever the run's exit code" * doctest::test_suite("m5")) {
    const Manifest manifest =
        syntheticManifest({variantOf("X-SYN-01", "one"), variantOf("X-SYN-01", "two"), variantOf("X-SYN-02", "one")});
    const vector<Record> complete{resultFor("X-SYN-01", "one"), resultFor("X-SYN-01", "two"), resultFor("X-SYN-02", "one")};
    const GateResult accepted = m5Gate::evaluate(manifest, complete, currentBuild());
    CHECK(accepted.accepted());
    CHECK(accepted.passed.size() == 3);
    CHECK(accepted.selected == 3);

    const GateResult failedCase =
        m5Gate::evaluate(manifest, {complete[0], complete[1], resultFor("X-SYN-02", "one", "FAIL")}, currentBuild());
    CHECK_FALSE(failedCase.accepted());
    CHECK(failedCase.failed == vector<string>{"X-SYN-02/one"});

    Record missingFixture = resultFor("X-SYN-02", "one", "BLOCKED");
    missingFixture.reason = "fixture missing";
    const GateResult blocked = m5Gate::evaluate(manifest, {complete[0], complete[1], missingFixture}, currentBuild());
    CHECK_FALSE(blocked.accepted());
    CHECK(blocked.blocked == vector<string>{"X-SYN-02/one: fixture missing"});
    CHECK(blocked.passed.size() == 2);

    Record oldBuild = complete[2];
    oldBuild.identity.commit = "c0";
    const GateResult old = m5Gate::evaluate(manifest, {complete[0], complete[1], oldBuild}, currentBuild());
    CHECK_FALSE(old.accepted());
    CHECK(listed(old.stale, "X-SYN-02/one: commit c0"));

    Identity uncommitted = currentBuild();
    uncommitted.dirty = true;
    const GateResult dirty = m5Gate::evaluate(manifest, complete, uncommitted);
    CHECK_FALSE(dirty.accepted());
    CHECK(listed(dirty.problems, "the current build has changes not committed"));
}

TEST_CASE("X-RUN-09/duplicateID: a test ID listed twice in the manifest rejects the bundle" * doctest::test_suite("m5")) {
    Manifest manifest = syntheticManifest({variantOf("X-SYN-01", "one"), variantOf("X-SYN-02", "one")});
    manifest.testIds.push_back("X-SYN-01");
    const GateResult result =
        m5Gate::evaluate(manifest, {resultFor("X-SYN-01", "one"), resultFor("X-SYN-02", "one")}, currentBuild());
    CHECK_FALSE(result.accepted());
    CHECK(listed(result.problems, "test ID X-SYN-01 is listed twice"));
}

TEST_CASE("X-RUN-09/duplicateVariant: a key listed twice, or two results for one key, rejects the bundle and neither result counts" * doctest::test_suite("m5")) {
    const Manifest twiceListed = syntheticManifest({variantOf("X-SYN-01", "one"), variantOf("X-SYN-01", "one")});
    const GateResult listedTwice = m5Gate::evaluate(twiceListed, {resultFor("X-SYN-01", "one")}, currentBuild());
    CHECK_FALSE(listedTwice.accepted());
    CHECK(listed(listedTwice.problems, "variant X-SYN-01/one is listed twice"));

    const Manifest manifest = syntheticManifest({variantOf("X-SYN-01", "one")});
    Record second = resultFor("X-SYN-01", "one");
    second.testCase = "another case declaring X-SYN-01/one";
    const GateResult twoResults = m5Gate::evaluate(manifest, {resultFor("X-SYN-01", "one"), second}, currentBuild());
    CHECK_FALSE(twoResults.accepted());
    CHECK(listed(twoResults.problems, "X-SYN-01/one has 2 results"));
    CHECK(twoResults.passed.empty());
    // In one run a second declaration gets its own file, so the first result is never overwritten.
    CHECK(m5Evidence::recordFileName("X-SYN-01", "one", 1) == "X-SYN-01.one.duplicate-1.json");
}

TEST_CASE("X-RUN-09/noAssertion: a registered case that asserted nothing is NOT_RUN and blocks acceptance though the run exits zero" * doctest::test_suite("m5")) {
    CHECK(m5Evidence::resultOf(0, 0, true, "") == "NOT_RUN");
    const Manifest manifest = syntheticManifest({variantOf("X-SYN-01", "one")});
    const GateResult result = m5Gate::evaluate(manifest, {resultFor("X-SYN-01", "one", "PASS", 0)}, currentBuild());
    CHECK_FALSE(result.accepted());
    CHECK(result.passed.empty());
    CHECK(result.notRun == vector<string>{"X-SYN-01/one: no assertion ran"});

    // A PASS whose record names a clause it did not cover is partial: listed, not counted, blocks acceptance.
    Record partial = resultFor("X-SYN-01", "one");
    partial.notCovered = "invalid cap fails before planning";
    const GateResult partialResult = m5Gate::evaluate(manifest, {partial}, currentBuild());
    CHECK_FALSE(partialResult.accepted());
    CHECK(partialResult.passed.empty());
    CHECK(partialResult.partial == vector<string>{"X-SYN-01/one: invalid cap fails before planning"});
}

TEST_CASE("X-RUN-09/missingVariant: a PASS for one variant does not hide a missing variant of the same test" * doctest::test_suite("m5")) {
    const Manifest manifest = syntheticManifest({variantOf("X-SYN-01", "one"), variantOf("X-SYN-01", "two")});
    const GateResult result = m5Gate::evaluate(manifest, {resultFor("X-SYN-01", "one")}, currentBuild());
    CHECK_FALSE(result.accepted());
    CHECK(result.passed == vector<string>{"X-SYN-01/one"});
    CHECK(result.notRun == vector<string>{"X-SYN-01/two"});
}

TEST_CASE("X-RUN-09/staleSpecHash: results tied to another requirement, manifest or params version do not count" * doctest::test_suite("m5")) {
    const Manifest manifest = syntheticManifest({variantOf("X-SYN-01", "one")});
    Record oldRequirement = resultFor("X-SYN-01", "one");
    oldRequirement.identity.requirementSha256 = "spec-0";
    const GateResult requirement = m5Gate::evaluate(manifest, {oldRequirement}, currentBuild());
    CHECK_FALSE(requirement.accepted());
    CHECK(requirement.stale == vector<string>{"X-SYN-01/one: another requirement version"});

    Record oldManifest = resultFor("X-SYN-01", "one");
    oldManifest.identity.manifestSha256 = "manifest-0";
    CHECK(m5Gate::evaluate(manifest, {oldManifest}, currentBuild()).stale == vector<string>{"X-SYN-01/one: another manifest version"});
    Record oldParams = resultFor("X-SYN-01", "one");
    oldParams.identity.profileSha256 = "profile-0";
    CHECK(m5Gate::evaluate(manifest, {oldParams}, currentBuild()).stale == vector<string>{"X-SYN-01/one: another params file"});

    Identity newRequirement = currentBuild();
    newRequirement.requirementSha256 = "spec-2";
    const GateResult outdatedManifest = m5Gate::evaluate(manifest, {resultFor("X-SYN-01", "one")}, newRequirement);
    CHECK_FALSE(outdatedManifest.accepted());
    CHECK(listed(outdatedManifest.problems, "the manifest was derived from another requirement version"));
}

TEST_CASE("X-RUN-09/deferredConditionalExcluded: unselected conditional variants stay visible, neither pass nor block; deferring without a decision is rejected" * doctest::test_suite("m5")) {
    const Manifest manifest = syntheticManifest(
        {variantOf("X-SYN-01", "one"), conditionalVariant("R-7", "m4Path", false), conditionalVariant("R-7", "m5Path", false)});
    const GateResult result = m5Gate::evaluate(manifest, {resultFor("X-SYN-01", "one")}, currentBuild());
    CHECK(result.accepted());
    CHECK(result.selected == 1);
    CHECK(result.passed == vector<string>{"X-SYN-01/one"});
    CHECK(result.deferred == vector<string>{"R-7/m4Path: per-lane override not agreed", "R-7/m5Path: per-lane override not agreed"});

    const GateResult failedDeferred = m5Gate::evaluate(
        manifest, {resultFor("X-SYN-01", "one"), resultFor("R-7", "m4Path", "FAIL")}, currentBuild());
    CHECK(failedDeferred.accepted());
    CHECK(failedDeferred.passed.size() == 1);
    CHECK(failedDeferred.deferred.size() == 2);

    // Relabelling a missing required variant as deferred, with no recorded decision, does not hide it.
    Variant relabelled = variantOf("X-SYN-02", "one", "conditional");
    relabelled.selected = false;
    const Manifest hidden = syntheticManifest({variantOf("X-SYN-01", "one"), relabelled});
    const GateResult rejected = m5Gate::evaluate(hidden, {resultFor("X-SYN-01", "one")}, currentBuild());
    CHECK_FALSE(rejected.accepted());
    CHECK(listed(rejected.problems, "X-SYN-02/one is deferred without a recorded decision"));
}

TEST_CASE("X-RUN-09/deferredConditionalSelected: once a recorded decision selects a conditional variant, its missing or old evidence blocks the gate" * doctest::test_suite("m5")) {
    const Manifest manifest = syntheticManifest(
        {variantOf("X-SYN-01", "one"), conditionalVariant("R-7", "m4Path", true), conditionalVariant("R-7", "m5Path", true)});
    const GateResult missing = m5Gate::evaluate(manifest, {resultFor("X-SYN-01", "one")}, currentBuild());
    CHECK_FALSE(missing.accepted());
    CHECK(missing.selected == 3);
    CHECK(missing.notRun == vector<string>{"R-7/m4Path", "R-7/m5Path"});
    CHECK(missing.deferred.empty());

    // Results recorded under the previous manifest, before the selection, are stale.
    Record oldM4 = resultFor("R-7", "m4Path"), oldM5 = resultFor("R-7", "m5Path");
    oldM4.identity.manifestSha256 = oldM5.identity.manifestSha256 = "manifest-0";
    const GateResult beforeSelection = m5Gate::evaluate(manifest, {resultFor("X-SYN-01", "one"), oldM4, oldM5}, currentBuild());
    CHECK_FALSE(beforeSelection.accepted());
    CHECK(beforeSelection.stale.size() == 2);

    const GateResult current = m5Gate::evaluate(
        manifest, {resultFor("X-SYN-01", "one"), resultFor("R-7", "m4Path"), resultFor("R-7", "m5Path")}, currentBuild());
    CHECK(current.accepted());
    CHECK(current.passed.size() == 3);
}

TEST_CASE("m5Acceptance: every selected required variant has one current PASS record at this build" * doctest::skip()) {
    const BuildStatus status = statusOfThisBuild();
    const string text = statusText(status);
    MESSAGE(text);
    CHECK(status.result.accepted());
}

TEST_CASE("m5EvidenceStatus: writes the state of every manifest variant at this build" * doctest::skip()) {
    const BuildStatus status = statusOfThisBuild();
    REQUIRE_FALSE(status.folder.empty());
    filesystem::create_directories(status.folder);
    const filesystem::path report = status.folder / "m5EvidenceStatus.txt";
    ofstream(report) << statusText(status);
    CHECK(filesystem::exists(report));
    MESSAGE("PASS " << status.result.passed.size() << " of " << status.result.selected << " selected variants; written to "
                    << report.string());
}
