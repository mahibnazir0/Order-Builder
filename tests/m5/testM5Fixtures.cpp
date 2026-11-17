// Committed synthetic fixtures are checked byte for byte before any test relies on them: Tom's review
// files and the OR-Library set keep the SHA-256 recorded when they arrived (Tom's 16 Nov files: tests
// v12 Section 40). The expected values are also in tests/fixtures/tomReview/SHA256SUMS.txt.
#include "doctest.h"
#include "support/m5Sha256.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace std;

namespace {

struct ExpectedFile {
    string path;  // from the repository root, where ctest runs
    string sha256;
};

void checkUnchanged(const vector<ExpectedFile>& files) {
    for (const ExpectedFile& file : files) {
        INFO(file.path);
        const optional<string> actual = m5Checksum::fileSha256Hex(file.path);
        REQUIRE_MESSAGE(actual.has_value(), "missing " << file.path);
        CHECK(*actual == file.sha256);
    }
}

const string kExactInputs = "tests/fixtures/tomReview/m5ExactInputs/";
const string kReviewE6dbb0f = "tests/fixtures/tomReview/e6dbb0f/";

}  // namespace

TEST_CASE("m5Fixtures: the checksum gives the FIPS 180-4 example answers" * doctest::test_suite("m5")) {
    CHECK(m5Checksum::sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(m5Checksum::sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(m5Checksum::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
          == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(m5Checksum::sha256Hex(string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("m5Fixtures: Tom's 16 Nov review inputs are committed unchanged" * doctest::test_suite("m5")) {
    checkUnchanged({
        {kExactInputs + "R-1a.json", "fc58a9a4c1912b2d88b9a7cf28ee1bdaf5f0a1b1bcea3be2b72e65b47effd052"},
        {kExactInputs + "R-1b.json", "40958f86d06dc40ca6d2e3b792b20c12b8bad2ac00ef551d70cc2f01470c0b8e"},
        {kExactInputs + "R-5a.json", "2534cf00edaa5980e71f93806f0971ce1d3d4aebed8ac1ceab53579bbe6a2d32"},
        {kExactInputs + "exactness-counterexample.json", "f6dd74e61f6ae42bd041cf3527a89820898ed79dc0489d6b16f32cba4ebcf917"},
        {kExactInputs + "monotonicity-standard-results.json", "95a55f69289e1569d0be403faaf33cd9eee494df91bf0cbe1d4e57d1e254770a"},
        {kExactInputs + "original_fourteen_stack_probe.cpp", "da803f590d7ad9138f1cb1279ff582d2d5809541a53f7917baa6c047126388fc"},
        {kExactInputs + "original_review_probe.cpp", "7ac2c4fa01dafe14af56df8ab37e7a0eb19614df796b37170b3ad462a0551fa8"},
        {kExactInputs + "run_requested_cases.cpp", "6111672b84b1d32d8ae47b6aacc56242cecc7bea4d482c2acbe4bf3aca91e201"},
        {kExactInputs + "results.json", "9f1f0fd3ca90621ff3f004de383f8f21b542c1c56f65072fd280d86e5bb9c5f6"},
        {kExactInputs + "README.txt", "c410cdd594fe366d33275fd8461501dd0d429eda239e509845b2274d2b0d8c2b"},
    });
}

TEST_CASE("m5Fixtures: Tom's e6dbb0f review files are committed unchanged" * doctest::test_suite("m5")) {
    checkUnchanged({
        {kReviewE6dbb0f + "REPRODUCE.txt", "466695a2f1596ab1098a1d0a8f30c741588d3242f2b55d4bc1ef116985bcd0fd"},
        {kReviewE6dbb0f + "floor_proof_probe.cpp", "2f34d8fcbf30bfecc89335fa809815748115f5bb79060f8f0fb077453bc5d0f5"},
        {kReviewE6dbb0f + "floor-proof-results.json", "55e67abc9b41615d6c7dd83206960f00a0db8d475740cc882258a71a5ef3e323"},
        {kReviewE6dbb0f + "verified-layout.json", "77febdb024364e7b1a92b17173733f7869312ebd651624dbd0a1ee2cd5cc7bc1"},
        {kReviewE6dbb0f + "capacity_probe.cpp", "8020ed273797578e3b1183b6c30782236816dc40adab5a0869b153257b2cb083"},
        {kReviewE6dbb0f + "capacity-results.json", "3516e2e90f4480e2898860648ad63fc1c2ecd8e81e14b4fcac9c5a5eee9e0522"},
        {kReviewE6dbb0f + "exactness-counterexample.json", "f6dd74e61f6ae42bd041cf3527a89820898ed79dc0489d6b16f32cba4ebcf917"},
        {kReviewE6dbb0f + "proof_standard_probe.cpp", "da803f590d7ad9138f1cb1279ff582d2d5809541a53f7917baa6c047126388fc"},
        {kReviewE6dbb0f + "monotonicity-standard-results.json", "95a55f69289e1569d0be403faaf33cd9eee494df91bf0cbe1d4e57d1e254770a"},
        {kReviewE6dbb0f + "proof-standard-results.jsonl", "bd4cd0b5b0baa4d38058f4a08cba159ab283b3349ddafb855a5a2f6bad0505a4"},
    });
}

TEST_CASE("m5Fixtures: binpack1.txt keeps the checksum recorded at download" * doctest::test_suite("m5")) {
    checkUnchanged({{"tests/m5/data/orlib/binpack1.txt", "891fe4b3be86371b120ccaf37ce4525b5b0a2fc249d38725cd3216fbaa42b172"}});
}
