#pragma once
// ============================================================================
// acceptanceHarness.hpp - Proves the thing the client disputed: on a dataset
// where Truck Builder's achieved truck count is known, the floor never
// exceeds it. Tests only.
//
// Achieved loads come from Truck Builder's pushed solutions (*_valid.json).
// The acceptance set is the lane-days on ship conditions TF and TL; suffixed
// conditions (TL_N102, SH_N101, ...) are not scored by Truck Builder and are
// left out. A lane-day is one lane within one planning request.
//
// Shipped volume is rebuilt per demand line (IDPR) from what the loads carry,
// then floored by the real pipeline: the run's own join and segregation,
// planFloor and floorBound. Nothing here reimplements the bound.
// ============================================================================

#include "pipeline.hpp"
#include "trailerSpec.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace acceptanceTests {

struct AchievedLane {
    std::string locationFrom;
    std::string locationTo;
    std::string shipCondition;
    std::size_t loads = 0;
};

struct ShippedLine {
    std::string matnr;
    std::string unitOfMeasure;
    double quantity = 0.0; // summed over every acceptance-set load that carries the line
};

struct AchievedRequest {
    std::string requestId;
    std::size_t solutionFiles = 0;
    std::vector<AchievedLane> laneDays; // acceptance-set lanes with at least one load
    std::unordered_map<std::string, ShippedLine> shippedByIdpr;
    // The acceptance set is chosen by ship condition; these count where a populated
    // LOAD_OPT_SCORE would have chosen differently.
    std::size_t scoredLoadsOutsideSet = 0;
    std::size_t unscoredLoadsInSet = 0;

    std::size_t totalLoads() const;
};

// Reads every *_valid.json in directory and keeps the solutions for requestId. Every field the
// harness relies on is checked: a malformed file, a missing or wrong-type block or field, a
// negative or non-finite MENGE, or one IDPR shipped as two materials or units throws
// std::runtime_error naming the file and the field. Throws if no solution matches requestId.
AchievedRequest readAchievedLoads(const std::string& directory, const std::string& requestId);

struct LaneDayValidity {
    std::string lane; // "2027 -> 2500 TL"
    std::size_t achievedLoads = 0;
    long long floorTrucks = 0;
    double boundTrucks = 0.0;
    // Each term summed over the lane-day's groups, so a violation shows which term drove it.
    double weightTrucks = 0.0;
    double stackedHeightTrucks = 0.0;
    double unitLoads = 0.0;
    long long noStackingBaselineTrucks = 0;
};

struct ValidityResult {
    std::vector<LaneDayValidity> laneDays;
    std::size_t achievedLoads = 0;
    long long floorTrucks = 0;
    long long noStackingBaselineTrucks = 0;
    std::size_t floorExceedsAchieved = 0;    // must be zero for a valid bound
    std::size_t baselineExceedsAchieved = 0; // why the baseline is not a floor
    // Shipped lines the floor could not count; each lowers the floor, never raises it.
    std::size_t shippedLinesNotInDemand = 0;
    std::size_t shippedLinesDisagreeing = 0; // MATNR or unit differs from the demand line
    std::size_t shippedLinesNotGrouped = 0;  // excluded by the validator before segregation
    std::size_t shippedLinesLeftOutOfFloor = 0;
};

// Floors the shipped volume of every acceptance-set lane-day with the run's own segregation,
// params and the given trailer, and sets each lane-day's floor beside its achieved loads.
// run must be the pipeline run of the demand extract the request planned.
ValidityResult checkValidity(const ob::PipelineResult& run, const AchievedRequest& achieved,
                             const ob::TrailerSpec& trailer);

} // namespace acceptanceTests
