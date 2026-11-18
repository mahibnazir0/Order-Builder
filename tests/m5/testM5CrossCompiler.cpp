// The cross-compiler gate (X-RUN-09/crossCompilerGate). Each compiler's build writes a record of itself
// (its commit, the warning flags it was built with and where its evidence records are) into one shared
// folder; the gate then compares the compilers' builds of this commit and the results of the synthetic
// variants each one ran. A compiler with no record is reported BLOCKED with its name, never PASS.
#include "doctest.h"
#include "support/m5Evidence.hpp"
#include "support/m5Manifest.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

namespace {

#if defined(OB_M5_MATRIX_DIR) && defined(OB_M5_STRICT_FLAGS) && defined(OB_M5_EXTRA_FLAGS) && defined(OB_M5_EVIDENCE_DIR) \
    && defined(OB_M5_BUILD_IDENTITY) && defined(OB_M5_COMPILER) && defined(OB_M5_BUILD_CONFIG)
constexpr bool kMatrixConfigured = true;
const char* const kMatrixDir = OB_M5_MATRIX_DIR;
const char* const kStrictFlags = OB_M5_STRICT_FLAGS;
const char* const kExtraFlags = OB_M5_EXTRA_FLAGS;
const char* const kEvidenceDir = OB_M5_EVIDENCE_DIR;
const char* const kBuildIdentityPath = OB_M5_BUILD_IDENTITY;
const char* const kCompiler = OB_M5_COMPILER;
const char* const kBuildConfig = OB_M5_BUILD_CONFIG;
#else
constexpr bool kMatrixConfigured = false;
const char* const kMatrixDir = "";
const char* const kStrictFlags = "";
const char* const kExtraFlags = "";
const char* const kEvidenceDir = "";
const char* const kBuildIdentityPath = "";
const char* const kCompiler = "unknown";
const char* const kBuildConfig = "unknown";
#endif

// The compilers v12 names for the matrix, with the strict flags each must be built with.
struct RequiredCompiler {
    string key;
    string name;
    vector<string> strictFlags;
};

const vector<RequiredCompiler>& requiredCompilers() {
    static const vector<RequiredCompiler> compilers{
        {"msvc", "MSVC VS2022 x64 Release", {"/W4", "/WX", "/utf-8"}},
        {"gnu", "GCC", {"-Wall", "-Wextra", "-Wpedantic", "-Werror"}},
        {"clang", "Clang", {"-Wall", "-Wextra", "-Wpedantic", "-Werror"}},
    };
    return compilers;
}

// The only warning that may be left non-fatal: the recorded libstdc++/json false positive (P11-3).
const set<string>& allowedExtraFlags() {
    static const set<string> flags{"-Wno-error=array-bounds"};
    return flags;
}

// Variants that only the first compiler runs: the 32-minute M4 thread test under --success.
const set<string>& primaryCompilerOnly() {
    static const set<string> keys{"U-PF-04/existingTest"};
    return keys;
}

string compilerKeyOf(const string& compiler) {
    string key = compiler.substr(0, compiler.find(' '));
    transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return key;
}

vector<string> wordsOf(const string& text) {
    istringstream stream(text);
    vector<string> words;
    for (string word; stream >> word;) words.push_back(word);
    return words;
}

bool hasWord(const vector<string>& words, const string& word) { return find(words.begin(), words.end(), word) != words.end(); }

filesystem::path recordPathOf(const string& compilerKey) { return filesystem::path(kMatrixDir) / (compilerKey + ".json"); }

}  // namespace

TEST_CASE("m5CompilerRecord: this build writes its compiler record for the cross-compiler gate" * doctest::test_suite("m5")) {
    if (!kMatrixConfigured) {
        MESSAGE("not a CMake build with the matrix folder: no compiler record written");
        return;
    }
    const m5Evidence::BuildIdentity build = m5Evidence::readBuildIdentity(kBuildIdentityPath, kCompiler, kBuildConfig);
    const string key = compilerKeyOf(kCompiler);
    error_code failed;
    filesystem::create_directories(kMatrixDir, failed);
    REQUIRE_FALSE(failed);
    const nlohmann::json record = {{"compilerKey", key},
                                   {"compiler", kCompiler},
                                   {"config", kBuildConfig},
                                   {"commit", build.commit},
                                   {"dirty", build.dirty},
                                   {"strictFlags", kStrictFlags},
                                   {"extraFlags", kExtraFlags},
                                   {"evidenceFolder", (filesystem::path(kEvidenceDir) / m5Evidence::folderName(build)).generic_string()}};
    ofstream(recordPathOf(key)) << record.dump(1) << "\n";
    ifstream readBack(recordPathOf(key));
    const nlohmann::json written = nlohmann::json::parse(readBack);
    CHECK(written.at("compilerKey") == key);
    CHECK(written.at("commit") == build.commit);
    CHECK_FALSE(key.empty());
}

TEST_CASE("X-RUN-09/crossCompilerGate: MSVC and GCC build this commit with the strict flags and agree on every synthetic variant; a compiler with no record is BLOCKED"
          * doctest::skip()) {
    REQUIRE(kMatrixConfigured);
    const m5Evidence::BuildIdentity build = m5Evidence::readBuildIdentity(kBuildIdentityPath, kCompiler, kBuildConfig);
    string error;
    const optional<m5Manifest::Manifest> manifest = m5Manifest::load(m5Manifest::kManifestPath, &error);
    REQUIRE_MESSAGE(manifest.has_value(), error);
    map<string, const m5Manifest::Variant*> syntheticVariants;
    for (const m5Manifest::Variant& variant : manifest->variants) {
        if (variant.selected && variant.evidenceScope == "SYNTHETIC" && variant.key() != "X-RUN-09/crossCompilerGate") {
            syntheticVariants[variant.key()] = &variant;
        }
    }
    REQUIRE_FALSE(syntheticVariants.empty());

    vector<string> problems, missing;
    map<string, map<string, m5Gate::Record>> recordsOf;  // compiler key -> variant key -> record
    nlohmann::json compilers = nlohmann::json::array();
    for (const RequiredCompiler& required : requiredCompilers()) {
        ifstream file(recordPathOf(required.key));
        if (!file.good()) {
            missing.push_back(required.name + " (no build record in " + kMatrixDir + ")");
            continue;
        }
        const nlohmann::json record = nlohmann::json::parse(file);
        const string commit = record.value("commit", "");
        if (commit != build.commit || record.value("dirty", true)) {
            problems.push_back(required.name + " was built from " + commit + (record.value("dirty", true) ? " (changes not committed)" : "") +
                               ", not this clean commit " + build.commit);
        }
        const vector<string> strict = wordsOf(record.value("strictFlags", ""));
        for (const string& flag : required.strictFlags) {
            if (!hasWord(strict, flag)) problems.push_back(required.name + " was not built with " + flag);
        }
        for (const string& flag : wordsOf(record.value("extraFlags", ""))) {
            const bool warningFlag = flag.rfind("-W", 0) == 0 || flag.rfind("/W", 0) == 0;
            if (warningFlag && !allowedExtraFlags().count(flag)) problems.push_back(required.name + " weakens the warning flags with " + flag);
        }
        const filesystem::path folder = record.value("evidenceFolder", "");
        if (!filesystem::exists(folder)) {
            problems.push_back(required.name + " has no evidence folder " + folder.generic_string());
            continue;
        }
        for (const m5Gate::Record& result : m5Evidence::readRecords(folder)) recordsOf[required.key][result.key()] = result;
        compilers.push_back({{"compiler", record.value("compiler", "")}, {"commit", commit}, {"flags", record.value("strictFlags", "") + " " + record.value("extraFlags", "")},
                             {"evidenceFolder", folder.generic_string()}, {"records", recordsOf[required.key].size()}});
    }

    // This build is one of the compilers, so at least one record must be there to compare.
    const string thisKey = compilerKeyOf(kCompiler);
    CHECK_MESSAGE(recordsOf.count(thisKey) == 1, "this build's own compiler record (" << thisKey << ") is missing: run the M5 core suite first");

    // Every synthetic variant that two compilers both ran has the same result; a variant one ran and
    // another did not is a skipped variant.
    size_t compared = 0;
    vector<string> differing, skipped;
    for (const auto& [key, variant] : syntheticVariants) {
        string firstCompiler, firstStatus;
        for (const auto& [compilerKey, results] : recordsOf) {
            const auto found = results.find(key);
            if (found == results.end()) {
                if (!primaryCompilerOnly().count(key) && any_of(recordsOf.begin(), recordsOf.end(), [&](const auto& other) { return other.second.count(key) == 1; })) {
                    skipped.push_back(key + " has no result on " + compilerKey);
                }
                continue;
            }
            const string status = found->second.executionStatus + (found->second.notCovered.empty() ? "" : " (partial)");
            if (firstCompiler.empty()) {
                firstCompiler = compilerKey;
                firstStatus = status;
            } else if (status != firstStatus) {
                differing.push_back(key + ": " + firstCompiler + " " + firstStatus + ", " + compilerKey + " " + status);
            } else {
                ++compared;
            }
        }
    }
    problems.insert(problems.end(), differing.begin(), differing.end());
    problems.insert(problems.end(), skipped.begin(), skipped.end());
    for (const string& problem : problems) INFO(problem);
    CHECK(problems.empty());
    if (recordsOf.size() >= 2) CHECK(compared > 0);

    m5Evidence::note("actual", {{"compilers", compilers}, {"synthetic variants compared across compilers", compared},
                                {"missing compilers", missing}, {"problems", problems}});
    m5Evidence::note("expected", {{"strictFlags", {{"msvc", "/W4 /WX /utf-8"}, {"gnu", "-Wall -Wextra -Wpedantic -Werror"}}},
                                  {"allowedNonFatalWarning", "-Wno-error=array-bounds (libstdc++/json false positive, recorded)"}});
    // A compiler that is not installed is blocked, never a pass; only when nothing else is wrong, so a failure is not hidden.
    if (problems.empty() && !missing.empty()) {
        string reason = "no build record for";
        for (const string& name : missing) reason += " " + name + ";";
        reason += " the other compilers pass and agree";
        m5Evidence::blocked(reason);
    }
}
