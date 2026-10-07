#pragma once
// Hand-built lines and groups shared by the floorPlanner and floorReporter tests. They feed
// the real planner; nothing here reimplements it.

#include "../importer/crossDayFixtures.hpp"
#include "demandSelector.hpp"
#include "floorPlanner.hpp"
#include "paramsLoader.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace floorTests {

inline const ob::M2Params& shippedParams() {
    static const ob::M2Params params = ob::loadParams(crossDayTests::kStrictParamsPath);
    return params;
}

inline const ob::TrailerSpec& shippedTrailer() {
    return ob::selectTrailer(shippedParams().trailers, shippedParams().sourcePath, "53FT_NA");
}

inline ob::M2Params paramsRoundingAt(ob::FloorRoundingPoint roundingPoint) {
    ob::M2Params params = shippedParams();
    params.floorRoundingPoint = roundingPoint;
    return params;
}

// One case per layer and one layer per unit load, so the case height is the unit-load height.
inline ob::ProductRecord unitLoadProduct(const std::string& palletId, double unitLoadHeightIn) {
    ob::ProductRecord record;
    record.id = "P";
    record.pallet_id = palletId;
    record.height_in = unitLoadHeightIn;
    record.weight_lb = 1.0;
    record.cases_layer = 1;
    record.layers_unit_load = 1;
    record.cases_unit_load = 1;
    return record;
}

inline ob::STRRecord palletDemand(double unitLoads) {
    ob::STRRecord record;
    record.matnr = "P";
    record.trans = unitLoads;
    record.unitofmeas = "PAL";
    return record;
}

// Pairs products[i] with demand[i]; the result points into both, so they must outlive it.
inline std::vector<ob::JoinedLine> joinedLines(const std::vector<ob::ProductRecord>& products,
                                               const std::vector<ob::STRRecord>& demand) {
    std::vector<ob::JoinedLine> lines(demand.size());
    for (std::size_t lineIndex = 0; lineIndex < demand.size(); ++lineIndex) {
        lines[lineIndex].str = &demand[lineIndex];
        lines[lineIndex].product = &products[lineIndex];
        lines[lineIndex].matched = true;
    }
    return lines;
}

inline ob::SegregatedGroup group(const std::string& locationTo, const std::string& segregant,
                                 const std::vector<std::size_t>& lineIndices) {
    ob::SegregatedGroup segregatedGroup;
    segregatedGroup.key = {"2027", locationTo, "TL", !segregant.empty(), segregant};
    segregatedGroup.lineIndices = lineIndices;
    return segregatedGroup;
}

// Half a trailer of stacked height per line.
struct HalfTrailerLines {
    std::vector<ob::ProductRecord> products;
    std::vector<ob::STRRecord> demand;
    std::vector<ob::JoinedLine> lines;

    explicit HalfTrailerLines(std::size_t lineCount) {
        const ob::TrailerSpec& trailer = shippedTrailer();
        products.assign(lineCount, unitLoadProduct("TLD", trailer.stackHeightCeilingIn));
        demand.assign(lineCount, palletDemand(trailer.stackPositions / 2.0));
        lines = joinedLines(products, demand);
    }
    // lines points into this object's own vectors; a copy would point into the original.
    HalfTrailerLines(const HalfTrailerLines&) = delete;
    HalfTrailerLines& operator=(const HalfTrailerLines&) = delete;
};

inline ob::DemandSelection wholeExtract(const std::vector<ob::JoinedLine>& lines) {
    return ob::selectDemand(std::vector<ob::STRRecord>(lines.size()), ob::DemandSelector{});
}

inline ob::SegregationResult segregation(const std::vector<ob::SegregatedGroup>& groups) {
    ob::SegregationResult result;
    result.groups = groups;
    return result;
}

} // namespace floorTests
