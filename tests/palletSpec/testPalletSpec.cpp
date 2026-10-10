#include "doctest.h"
#include "joiner.hpp"
#include "palletSpec.hpp"
#include "paramsLoader.hpp"

#include <cmath>
#include <string>
#include <unordered_set>

using namespace std;
using namespace ob;

namespace {

const PalletSpec& confirmedSpec(const string& palletId) {
    const PalletSpec* pallet = palletSpecFor(confirmedPalletSpecs(), palletId);
    REQUIRE(pallet != nullptr);
    return *pallet;
}

} // namespace

TEST_CASE("palletSpec: wood pallets weigh Tom's confirmed 60 lb and the others add nothing") {
    CHECK(confirmedSpec("PTL").addedWeightLb == 60.0);
    CHECK(confirmedSpec("PGM").addedWeightLb == 60.0);
    CHECK(confirmedSpec("TLD").addedWeightLb == 0.0);
    CHECK(confirmedSpec("GMA").addedWeightLb == 0.0);
}

TEST_CASE("palletSpec: every confirmed spec is a physical pallet with a unique id") {
    unordered_set<string> seenIds;
    for (const auto& pallet : confirmedPalletSpecs()) {
        CAPTURE(pallet.palletId);
        CHECK(seenIds.insert(pallet.palletId).second);
        CHECK(isfinite(pallet.addedWeightLb));
        CHECK(pallet.addedWeightLb >= 0.0);
        CHECK(isfinite(pallet.addedHeightIn));
        CHECK(pallet.addedHeightIn >= 0.0);
        CHECK(isfinite(pallet.footprintLengthIn));
        CHECK(pallet.footprintLengthIn > 0.0);
        CHECK(isfinite(pallet.footprintWidthIn));
        CHECK(pallet.footprintWidthIn > 0.0);
    }
}

TEST_CASE("palletSpec: every pallet type the Joiner can choose has a confirmed spec") {
    for (const auto& palletId : Joiner::default_pallet_preference()) {
        CAPTURE(palletId);
        CHECK(palletSpecFor(confirmedPalletSpecs(), palletId) != nullptr);
    }
}

TEST_CASE("palletSpec: lookup is an exact match and an unknown type has no spec") {
    CHECK(palletSpecFor(confirmedPalletSpecs(), "PTL") != nullptr);
    for (const string nearMiss : {"", "PTL ", "ptl", " PTL", "WOOD", "XYZ"}) {
        CAPTURE(nearMiss);
        CHECK(palletSpecFor(confirmedPalletSpecs(), nearMiss) == nullptr);
    }
}

TEST_CASE("palletSpec: params lookup reads the run's pallet table, not the confirmed table") {
    M2Params params = loadParams("config/orderBuilderParams.json");
    params.pallets = {{"PTL", 65.0, 6.0, 48.0, 40.0}};
    const PalletSpec* pallet = palletSpecFor(params, "PTL");
    REQUIRE(pallet != nullptr);
    CHECK(pallet->addedWeightLb == 65.0);
    CHECK(pallet == &params.pallets[0]);
}
