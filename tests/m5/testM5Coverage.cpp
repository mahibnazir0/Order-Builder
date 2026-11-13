// The acceptance evidence gate over this repository's own M5 tests (X-RUN-09).
#include "doctest.h"
#include "support/m5EvidenceGate.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace std;
using namespace m5Gate;

namespace {

vector<string> linesOf(const string& path) {
    ifstream file(path);
    REQUIRE_MESSAGE(file.good(), "missing " << path);
    vector<string> lines;
    for (string line; getline(file, line);) {
        if (!line.empty() && line[0] != '#') lines.push_back(line);
    }
    return lines;
}

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

map<string, Exception> exceptionsOf(const string& path) {
    map<string, Exception> exceptions;
    for (const string& line : linesOf(path)) {
        const size_t first = line.find('|'), second = line.find('|', first + 1);
        REQUIRE(second != string::npos);
        exceptions[line.substr(0, first)] = {line.substr(first + 1, second - first - 1), line.substr(second + 1)};
    }
    return exceptions;
}

}  // namespace

TEST_CASE("X-RUN-09 the gate rejects missing, duplicate, stale and reasonless evidence; BLOCKED never passes" * doctest::test_suite("m5")) {
    CHECK(evaluateGate({"A-1", "B-2"}, {"A-1 B-2 both"}, {}).complete());
    CHECK_FALSE(evaluateGate({"A-1", "B-2"}, {"A-1 only"}, {}).complete());
    CHECK_FALSE(evaluateGate({"A-1", "A-1"}, {"A-1"}, {}).complete());
    CHECK_FALSE(evaluateGate({"A-1"}, {"A-1 twice", "A-1 twice"}, {}).complete());
    CHECK_FALSE(evaluateGate({"A-1"}, {"A-1"}, {{"Z-9", {"BLOCKED", "gone"}}}).complete());
    CHECK_FALSE(evaluateGate({"A-1", "B-2"}, {"A-1"}, {{"B-2", {"BLOCKED", ""}}}).complete());
    const GateResult blocked = evaluateGate({"A-1", "B-2"}, {"A-1"}, {{"B-2", {"BLOCKED", "inputs missing"}}});
    CHECK(blocked.problems.empty());
    CHECK(blocked.blocked == vector<string>{"B-2"});
    CHECK_FALSE(blocked.complete());
}

TEST_CASE("X-RUN-09 every one of the 291 v7 IDs has a registered M5 test or a named exception" * doctest::test_suite("m5")) {
    const vector<string> required = linesOf("tests/m5/data/m5TestIds.txt");
    CHECK(required.size() == 291);
    const GateResult result = evaluateGate(required, registeredTestNames(), exceptionsOf("tests/m5/data/m5CoverageExceptions.txt"));
    for (const string& problem : result.problems) INFO(problem);
    CHECK(result.problems.empty());
    CHECK(result.blocked == vector<string>{"R-1a", "R-1b", "R-5a"});
}
