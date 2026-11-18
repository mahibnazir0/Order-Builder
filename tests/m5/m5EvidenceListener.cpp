// Writes an evidence record for every testId/variant a test case name declares, whatever suite or
// filter the run uses (support/m5Evidence.hpp). It reads the manifest when the run starts; without the
// manifest, or outside a CMake build that names the evidence folder, it writes nothing.
//
// An existing M4 test case that is the test of an M5 variant cannot declare it in its name (M4 test files
// are not edited). tests/m5/data/m5ExistingTestKeys.txt names it: "test case name|testId/variant|options",
// where the option "success" means the record is written only when the run uses --success.
#include "doctest.h"
#include "support/m5Evidence.hpp"

#include <cstddef>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace std;

namespace {

#if defined(OB_M5_EVIDENCE_DIR) && defined(OB_M5_BUILD_IDENTITY)
constexpr bool kEvidenceConfigured = true;
const char* const kEvidenceDir = OB_M5_EVIDENCE_DIR;
const char* const kBuildIdentityPath = OB_M5_BUILD_IDENTITY;
#else
constexpr bool kEvidenceConfigured = false;
const char* const kEvidenceDir = "";
const char* const kBuildIdentityPath = "";
#endif
#if defined(OB_M5_COMPILER) && defined(OB_M5_BUILD_CONFIG)
const char* const kCompiler = OB_M5_COMPILER;
const char* const kBuildConfig = OB_M5_BUILD_CONFIG;
#else
const char* const kCompiler = "unknown";
const char* const kBuildConfig = "unknown";
#endif

class EvidenceListener final : public doctest::IReporter {
public:
    explicit EvidenceListener(const doctest::ContextOptions& options) : underSuccess_(options.success) {}

    void report_query(const doctest::QueryData&) override {}

    void test_run_start() override {
        if (!kEvidenceConfigured) return;
        string error;
        manifest_ = m5Manifest::load(m5Manifest::kManifestPath, &error);
        if (!manifest_) return;
        for (const m5Manifest::Variant& variant : manifest_->variants) variantOf_.emplace(variant.key(), variant);
        build_ = m5Evidence::readBuildIdentity(kBuildIdentityPath, kCompiler, kBuildConfig);
        identity_ = m5Evidence::currentIdentity(build_, *manifest_);
        folder_ = filesystem::path(kEvidenceDir) / m5Evidence::folderName(build_);
        error_code failed;
        filesystem::create_directories(folder_, failed);
        enabled_ = !failed;
        loadExistingTestKeys();
        runStarted_ = static_cast<long long>(time(nullptr));
    }

    void test_run_end(const doctest::TestRunStats&) override {}

    void test_case_start(const doctest::TestCaseData& data) override {
        current_ = m5Evidence::CaseResult();
        current_.testCase = data.m_name;
        current_.file = data.m_file.c_str();
        current_.line = static_cast<int>(data.m_line);
        keys_ = m5Manifest::declaredKeys(current_.testCase);
        m5Evidence::currentNotes() = nlohmann::json::object();
        for (const ExistingTest& existing : existingTests_) {
            if (existing.name != current_.testCase || (existing.needsSuccess && !underSuccess_)) continue;
            keys_.emplace_back(existing.testId, existing.variant);
            m5Evidence::note("existingTest", {{"name", existing.name}, {"runWithSuccess", underSuccess_}});
        }
    }

    void test_case_reenter(const doctest::TestCaseData&) override {}

    void test_case_end(const doctest::CurrentTestCaseStats& stats) override {
        if (!enabled_ || keys_.empty()) return;
        current_.assertions = stats.numAssertsCurrentTest;
        current_.failedAssertions = stats.numAssertsFailedCurrentTest;
        current_.succeeded = stats.testCaseSuccess;
        current_.seconds = stats.seconds;
        for (const auto& [testId, variantName] : keys_) {
            const string key = testId + "/" + variantName;
            const auto known = variantOf_.find(key);
            m5Manifest::Variant variant;
            if (known != variantOf_.end()) {
                variant = known->second;
            } else {
                variant.testId = testId;
                variant.variant = variantName;
            }
            const string fileName = m5Evidence::recordFileName(testId, variantName, writtenThisRun_[key]++);
            const nlohmann::json record =
                m5Evidence::recordJson(variant, current_, build_, identity_, m5Evidence::currentNotes());
            ofstream(folder_ / fileName) << record.dump(1) << "\n";
            ofstream(folder_ / "history.jsonl", ios::app)
                << nlohmann::json{{"key", key}, {"file", fileName}, {"executionStatus", record.at("executionStatus")},
                                  {"testCase", current_.testCase}, {"runStarted", runStarted_}}
                       .dump()
                << "\n";
        }
    }

    void test_case_exception(const doctest::TestCaseException& exception) override {
        current_.exception = exception.error_string.c_str();
    }

    void subcase_start(const doctest::SubcaseSignature&) override {}
    void subcase_end() override {}
    void log_assert(const doctest::AssertData&) override {}
    void log_message(const doctest::MessageData&) override {}
    void test_case_skipped(const doctest::TestCaseData&) override {}

private:
    struct ExistingTest {
        string name, testId, variant;
        bool needsSuccess = false;
    };

    void loadExistingTestKeys() {
        ifstream file("tests/m5/data/m5ExistingTestKeys.txt");
        for (string line; getline(file, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            const size_t first = line.find('|'), second = line.find('|', first + 1);
            const size_t slash = line.find('/', first + 1);
            if (first == string::npos || second == string::npos || slash == string::npos || slash > second) continue;
            ExistingTest existing;
            existing.name = line.substr(0, first);
            existing.testId = line.substr(first + 1, slash - first - 1);
            existing.variant = line.substr(slash + 1, second - slash - 1);
            existing.needsSuccess = line.substr(second + 1).find("success") != string::npos;
            existingTests_.push_back(existing);
        }
    }

    bool underSuccess_ = false;
    vector<ExistingTest> existingTests_;
    bool enabled_ = false;
    optional<m5Manifest::Manifest> manifest_;
    map<string, m5Manifest::Variant> variantOf_;
    m5Evidence::BuildIdentity build_;
    m5Gate::Identity identity_;
    filesystem::path folder_;
    long long runStarted_ = 0;
    m5Evidence::CaseResult current_;
    vector<pair<string, string>> keys_;
    map<string, int> writtenThisRun_;
};

}  // namespace

REGISTER_LISTENER("m5Evidence", 1, EvidenceListener);
