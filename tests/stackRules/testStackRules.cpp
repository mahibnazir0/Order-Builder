#include "doctest.h"
#include "stackRules.hpp"
#include "paramsLoader.hpp"
#include "../importer/crossDayFixtures.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>

using namespace ob;

namespace {

const M2Params& stackParams() {
    static const auto params = loadParams("config/orderBuilderParams.json");
    return params;
}

double configuredCeilingIn() {
    return stackParams().trailers.at(0).stackHeightCeilingIn;
}

struct PairCounts {
    std::size_t passHeight = 0;
    std::size_t passAll = 0;
};

PairCounts countPairs(const std::vector<UnitLoad>& loads, double ceilingIn) {
    PairCounts counts;
    const auto& params = stackParams();
    for (const auto& base : loads) {
        for (const auto& top : loads) {
            const auto result = canStack(base, top, params, ceilingIn);
            if (result.reason != StackFeasibility::Reason::HeightCeiling) ++counts.passHeight;
            if (result.isFeasible) ++counts.passAll;
        }
    }
    return counts;
}

ProductRecord sampleProduct() {
    ProductRecord product;
    product.id = "synthetic";
    product.height_in = 5.0;
    product.length_in = 10.0;
    product.width_in = 8.0;
    product.weight_lb = 9.0;
    product.layers_unit_load = 6;
    product.cases_layer = 10;
    product.cases_unit_load = 60;
    product.strength = 10;
    product.pallet_id = "PTL";
    return product;
}

JoinedLine joinedProduct(const ProductRecord& product) {
    return {nullptr, &product, true, false};
}

} // namespace

TEST_CASE("stackRules: construction uses cases and configured pallet, never case footprint") {
    const auto product = sampleProduct();
    const auto load = buildUnitLoad(joinedProduct(product), stackParams());
    CHECK(load.error == UnitLoadError::None);
    CHECK(load.weightLb == 600.0);
    CHECK(load.heightIn == 35.5);
    CHECK(load.ownWeightAboveLb == 450.0);
    CHECK(load.footprintLengthIn == 48.0);
    CHECK(load.footprintWidthIn == 40.0);
    CHECK(load.weightAboveSource == WeightAboveSource::Derived);
    auto params = stackParams();
    params.pallets = {{"EPAL", 12.0, 3.0, 47.0, 31.0}};
    auto european = product;
    european.pallet_id = "EPAL";
    const auto custom = buildUnitLoad(joinedProduct(european), params);
    CHECK(custom.error == UnitLoadError::None);
    CHECK(custom.heightIn == 33.0);
    CHECK(custom.weightLb == 552.0);
    CHECK(custom.footprintLengthIn == 47.0);
    CHECK(custom.footprintWidthIn == 31.0);
}

TEST_CASE("stackRules: supplied weight above wins including zero; invalid supplied never falls back") {
    const auto product = sampleProduct();
    for (double supplied : {0.0, 125.0}) {
        const auto load = buildUnitLoad(joinedProduct(product), stackParams(), supplied);
        CHECK(load.error == UnitLoadError::None);
        CHECK(load.ownWeightAboveLb == supplied);
        CHECK(load.weightAboveSource == WeightAboveSource::Supplied);
    }
    for (double supplied : {-1.0, std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity()}) {
        CHECK(buildUnitLoad(joinedProduct(product), stackParams(), supplied).error
              == UnitLoadError::InvalidData);
    }
}

TEST_CASE("stackRules: invalid case measurements and capacities fail construction") {
    for (double badValue : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity()}) {
        for (auto member : {&ProductRecord::height_in, &ProductRecord::weight_lb}) {
            auto product = sampleProduct();
            product.*member = badValue;
            CHECK(buildUnitLoad(joinedProduct(product), stackParams()).error == UnitLoadError::InvalidData);
        }
    }
    for (int badValue : {0, -1}) {
        for (auto member : {&ProductRecord::layers_unit_load, &ProductRecord::cases_layer,
                            &ProductRecord::cases_unit_load}) {
            auto product = sampleProduct();
            product.*member = badValue;
            CHECK(buildUnitLoad(joinedProduct(product), stackParams()).error == UnitLoadError::InvalidData);
        }
    }
    auto product = sampleProduct();
    product.layers_unit_load = 1;
    CHECK(buildUnitLoad(joinedProduct(product), stackParams()).ownWeightAboveLb == 0.0);
    product.weight_lb = std::numeric_limits<double>::max();
    CHECK(buildUnitLoad(joinedProduct(product), stackParams()).error == UnitLoadError::InvalidData);
    product = sampleProduct();
    product.height_in = std::numeric_limits<double>::max();
    CHECK(buildUnitLoad(joinedProduct(product), stackParams()).error == UnitLoadError::InvalidData);
}

TEST_CASE("stackRules: nonfinite CSV values in all five arithmetic fields are rejected after M1 import") {
    // Capacity fields are ints in M1; test NaN/Inf at their actual CSV boundary.
    const auto path = std::filesystem::path("build/stackRulesInvalid.csv");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); }
    } cleanup{path};
    for (std::size_t column : {std::size_t{4}, std::size_t{7}, std::size_t{8}, std::size_t{9}, std::size_t{10}}) {
        for (const auto& invalid : {"0", "-1", "nan", "inf"}) {
            CAPTURE(column);
            CAPTURE(invalid);
            std::array<std::string, 12> cells{{"synthetic", "test", "10", "8", "5", "10",
                                              "CS", "9", "10", "6", "60", "PTL"}};
            cells[column] = invalid;
            {
                std::ofstream output(path);
                REQUIRE(output.is_open());
                output << "ID,Description,Length,Width,Height,Strength,UoM,Weight,Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n";
                for (std::size_t index = 0; index < cells.size(); ++index) {
                    if (index != 0) output << ',';
                    output << cells[index];
                }
                output << '\n';
            }
            const auto imported = ProductImporter::load(path.string());
            REQUIRE(imported.products.size() == 1);
            CHECK(buildUnitLoad(joinedProduct(imported.products.front()), stackParams()).error
                  == UnitLoadError::InvalidData);
        }
    }
}

TEST_CASE("stackRules: missing pallet IDs are exact distinct and collected together") {
    auto first = sampleProduct();
    auto second = sampleProduct();
    first.pallet_id = "PTL ";
    second.pallet_id = "UNKNOWN";
    const std::vector<JoinedLine> lines{joinedProduct(first), joinedProduct(first), joinedProduct(second), {}};
    CHECK(missingPalletIds(lines, stackParams()) == std::vector<std::string>{"PTL ", "UNKNOWN"});
    CHECK(palletSpecFor(stackParams(), first.pallet_id) == nullptr);
    CHECK(buildUnitLoad(lines.front(), stackParams()).error == UnitLoadError::MissingPalletSpec);
    CHECK(buildUnitLoad({}, stackParams()).error == UnitLoadError::MissingProduct);
    auto missingConfig = stackParams();
    missingConfig.pallets.erase(missingConfig.pallets.begin());
    const auto product = sampleProduct();
    CHECK(missingPalletIds({joinedProduct(product)}, missingConfig) == std::vector<std::string>{"PTL"});
}

TEST_CASE("stackRules: height precedes blank and CRI; boundaries and directional weight") {
    using Reason = StackFeasibility::Reason;
    const auto product = sampleProduct();
    auto base = buildUnitLoad(joinedProduct(product), stackParams());
    auto top = base;
    base.heightIn = 54.0;
    top.heightIn = 54.0;
    auto result = canStack(base, top, stackParams(), 108.0);
    CHECK(result.isFeasible);
    CHECK(result.marginInOrLb == 0.0);
    CHECK(canStack(base, top, stackParams(), 110.0).marginInOrLb == 2.0);
    top.heightIn += 0.001;
    base.cri = 0;
    CHECK(canStack(base, top, stackParams(), 108.0).reason == Reason::HeightCeiling);
    CHECK(canStack(base, top, stackParams(), 108.0).marginInOrLb == doctest::Approx(0.001));
    top.heightIn = 54.0;
    CHECK(canStack(base, top, stackParams(), 108.0).reason == Reason::BlankCri);
    base.cri = 10;
    base.ownWeightAboveLb = stackParams().cri.safeLimitLb[10] - top.weightLb;
    CHECK(canStack(base, top, stackParams(), 108.0).isFeasible);
    top.weightLb += 0.001;
    result = canStack(base, top, stackParams(), 108.0);
    CHECK(result.reason == Reason::CriExceeded);
    CHECK(result.marginInOrLb == doctest::Approx(0.001));
    base.heightIn = 109.0;
    CHECK(canStack(base, top, stackParams(), 108.0).reason == Reason::HeightCeiling);
    base = buildUnitLoad(joinedProduct(product), stackParams());
    top = base;
    top.cri = 0;
    CHECK(canStack(base, top, stackParams(), configuredCeilingIn()).isFeasible);
    CHECK(canStack(top, base, stackParams(), configuredCeilingIn()).reason == Reason::BlankCri);
    top.cri = 1;
    CHECK(canStack(base, top, stackParams(), configuredCeilingIn()).isFeasible);
    CHECK(canStack(top, base, stackParams(), configuredCeilingIn()).reason == Reason::CriExceeded);
    auto params = stackParams();
    params.cri.safeLimitLb[1] = 10000.0;
    CHECK(canStack(top, base, params, configuredCeilingIn()).isFeasible);
    params.blankCriIsStackable = true;
    top.cri = 0;
    CHECK(canStack(top, base, params, configuredCeilingIn()).reason == Reason::BlankCri);
}

TEST_CASE("stackRules: invalid CRI and malformed plain loads fail safely") {
    auto product = sampleProduct();
    for (int cri : {-1, 11}) {
        product.strength = cri;
        CHECK(buildUnitLoad(joinedProduct(product), stackParams()).error == UnitLoadError::InvalidData);
    }
    product = sampleProduct();
    const auto valid = buildUnitLoad(joinedProduct(product), stackParams());
    for (int cri : {-1, 11}) {
        auto load = valid;
        load.cri = cri;
        CHECK(canStack(load, valid, stackParams(), configuredCeilingIn()).reason == StackFeasibility::Reason::InvalidData);
    }
    for (double badValue : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity()}) {
        for (auto member : {&UnitLoad::heightIn, &UnitLoad::weightLb,
                            &UnitLoad::footprintLengthIn, &UnitLoad::footprintWidthIn}) {
            auto load = valid;
            load.*member = badValue;
            CHECK_FALSE(canStack(load, valid, stackParams(), configuredCeilingIn()).isFeasible);
            CHECK_FALSE(canStack(valid, load, stackParams(), configuredCeilingIn()).isFeasible);
        }
        CHECK_FALSE(canStack(valid, valid, stackParams(), badValue).isFeasible);
    }
    const auto& params = stackParams();
    const double ceilingIn = configuredCeilingIn();
    static_assert(noexcept(canStack(valid, valid, params, ceilingIn)), "predicate must not throw");
}

TEST_CASE("stackRules: four real days whole rules regression") {
    const std::array<std::size_t, 4> expectedProducts{{1420,1398,1395,1385}};
    const std::array<std::size_t, 4> expectedPairs{{2016400,1954404,1946025,1918225}};
    const std::array<std::size_t, 4> expectedHeight{{65295,64754,62746,63247}};
    const std::array<std::size_t, 4> expectedPass{{43021,42461,41326,41712}};
    const std::array<std::size_t, 4> expectedWood{{487,482,482,483}};
    const std::array<double, 4> expectedShare{{3.24,3.31,3.22,3.30}};
    const std::array<double, 4> expectedMedian{{101.1,101.0,101.0,101.0}};
    for (std::size_t dayIndex = 0; dayIndex < 4; ++dayIndex) {
        CAPTURE(crossDayTests::days()[dayIndex].label);
        const auto& fixture = crossDayTests::fixtures()[dayIndex];
        REQUIRE(missingPalletIds(fixture.join.lines, stackParams()).empty());
        std::set<std::string> seen;
        std::vector<UnitLoad> loads;
        std::vector<double> heights;
        std::size_t wood = 0, blank = 0, selfFail = 0, derived = 0, supplied = 0, invalid = 0;
        for (const auto& joined : fixture.join.lines) {
            auto load = buildUnitLoad(joined, stackParams());
            if (load.error != UnitLoadError::None) { ++invalid; continue; }
            if (load.weightAboveSource == WeightAboveSource::Derived) ++derived;
            else ++supplied;
            if (!seen.insert(load.id).second) continue;
            const auto* pallet = palletSpecFor(stackParams(), joined.product->pallet_id);
            if (pallet->addedWeightLb > 0.0) ++wood;
            if (load.cri == 0) ++blank;
            else if (load.ownWeightAboveLb > stackParams().cri.safeLimitLb[static_cast<std::size_t>(load.cri)]) {
                ++selfFail;
                CHECK(load.id == "106005500");
            }
            if (dayIndex == 0 && load.id == "106005500") {
                CHECK(load.cri == 10);
                CHECK(stackParams().cri.safeLimitLb[10] == 3599.0);
                CHECK(load.ownWeightAboveLb == doctest::Approx(6426.0));
            }
            heights.push_back(load.heightIn);
            loads.push_back(std::move(load));
        }
        const auto [passHeight, passAll] = countPairs(loads, configuredCeilingIn());
        std::sort(heights.begin(), heights.end());
        REQUIRE_FALSE(heights.empty());
        const double median = (heights[(heights.size() - 1) / 2] + heights[heights.size() / 2]) / 2.0;
        const double share = 100.0 * static_cast<double>(passHeight) / static_cast<double>(loads.size() * loads.size());
        std::cout << std::setprecision(12) << "Stack day " << crossDayTests::days()[dayIndex].label
                  << ": products=" << loads.size() << " pairs=" << loads.size() * loads.size()
                  << " height=" << passHeight << " share=" << share << " pass=" << passAll
                  << " median=" << median << " selfFail=" << selfFail << " wood=" << wood
                  << " blank=" << blank << " derivedLines=" << derived << " suppliedLines=" << supplied
                  << " invalidLines=" << invalid << '\n';
        CHECK(loads.size() == expectedProducts[dayIndex]);
        CHECK(loads.size() * loads.size() == expectedPairs[dayIndex]);
        CHECK(passHeight == expectedHeight[dayIndex]);
        CHECK(passAll == expectedPass[dayIndex]);
        CHECK(std::round(share * 100.0) / 100.0 == doctest::Approx(expectedShare[dayIndex]));
        CHECK(std::round(median * 10.0) / 10.0 == doctest::Approx(expectedMedian[dayIndex]));
        CHECK(selfFail == (dayIndex == 0 ? 1 : 0));
        CHECK(wood == expectedWood[dayIndex]);
        CHECK(blank == 0);
        CHECK(supplied == 0);
        CHECK(derived + invalid == fixture.join.lines.size());
        CHECK(invalid == 0);
        if (dayIndex == 0) {
            CHECK(fixture.summary.total_weight_lb == doctest::Approx(103005833.0).epsilon(0.000001));
        }
        auto missingConfig = stackParams();
        missingConfig.pallets.erase(missingConfig.pallets.begin());
        CHECK(missingPalletIds(fixture.join.lines, missingConfig) == std::vector<std::string>{"PTL"});
    }
}

TEST_CASE("stackRules: 17 August 110-inch ceiling sensitivity check") {
    const auto& fixture = crossDayTests::fixtures()[0];
    std::set<std::string> seen;
    std::vector<UnitLoad> loads;
    std::size_t invalid = 0;
    for (const auto& joined : fixture.join.lines) {
        auto load = buildUnitLoad(joined, stackParams());
        if (load.error != UnitLoadError::None) { ++invalid; continue; }
        if (seen.insert(load.id).second) loads.push_back(std::move(load));
    }
    REQUIRE(invalid == 0);
    const auto counts = countPairs(loads, 110.0);
    const auto pairs = loads.size() * loads.size();
    std::cout << "Stack sensitivity 17 Aug at 110 inches: pairs=" << pairs
              << " height=" << counts.passHeight << " pass=" << counts.passAll << '\n';
    CHECK(pairs == 2016400);
    CHECK(counts.passHeight == 65399);
    CHECK(counts.passAll == 43075);
}

TEST_CASE("stackRules: master ceiling distribution and real blank CRI") {
    const auto& fixture = crossDayTests::fixtures()[0];
    const double ceilingIn = configuredCeilingIn();
    std::size_t exactCeiling = 0, woodBand = 0, blank = 0;
    std::vector<double> taller;
    std::set<std::string> tallIds;
    const auto synthetic = sampleProduct();
    const auto top = buildUnitLoad(joinedProduct(synthetic), stackParams());
    for (const auto& product : fixture.products.products) {
        const double rawHeight = product.height_in * product.layers_unit_load;
        const auto* pallet = palletSpecFor(stackParams(), product.pallet_id);
        REQUIRE(pallet != nullptr);
        if (std::abs(rawHeight - ceilingIn) < 1e-9) ++exactCeiling;
        if (pallet->addedHeightIn > 0.0 && rawHeight > ceilingIn - pallet->addedHeightIn
            && rawHeight <= ceilingIn) ++woodBand;
        if (rawHeight > ceilingIn) {
            taller.push_back(rawHeight);
            tallIds.insert(product.id);
            CHECK(product.pallet_id == "TLD");
            const auto load = buildUnitLoad(joinedProduct(product), stackParams());
            REQUIRE(load.error == UnitLoadError::None);
            CHECK(canStack(load, top, stackParams(), ceilingIn).reason == StackFeasibility::Reason::HeightCeiling);
        }
        if (product.strength == 0) {
            ++blank;
            const auto load = buildUnitLoad(joinedProduct(product), stackParams());
            REQUIRE(load.error == UnitLoadError::None);
            CHECK(canStack(load, top, stackParams(), load.heightIn + top.heightIn).reason
                  == StackFeasibility::Reason::BlankCri);
        }
    }
    std::sort(taller.begin(), taller.end());
    std::cout << "Stack master: ceiling=" << ceilingIn << " exactCeiling=" << exactCeiling
              << " woodBand=" << woodBand << " blank=" << blank << " taller=";
    for (double height : taller) std::cout << height << ' ';
    std::cout << '\n';
    CHECK(exactCeiling == 153);
    CHECK(woodBand == 1);
    CHECK(blank == 1);
    REQUIRE(taller.size() == 4);
    const std::array<double, 4> expected{{108.75,113.39,113.39,141.05}};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CHECK(std::round(taller[index] * 100.0) / 100.0 == doctest::Approx(expected[index]));
    }
    for (const auto& day : crossDayTests::fixtures()) {
        std::size_t demandedTall = 0;
        for (const auto& line : day.join.lines) {
            if (line.product != nullptr && tallIds.count(line.product->id) != 0) ++demandedTall;
        }
        CHECK(demandedTall == 0);
    }
}
