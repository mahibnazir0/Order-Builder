#pragma once
// ============================================================================
// acceptanceHarness.hpp - Proves the thing the client disputed: on a dataset
// where Truck Builder's achieved truck count is known, the floor never
// exceeds it. Tests only.
//
// Achieved loads come from Truck Builder's pushed solutions (*_valid.json).
// The acceptance set is the loads on ship conditions TF and TL; suffixed
// conditions (TL_N102, SH_N101, ...) are not scored by Truck Builder and are
// left out. Every load carries its ship date (DATFR_TA), so the floor is set
// beside the loads built on the same lane and ship date, from the volume those
// loads shipped (client, 27 Oct). A lane group is one lane within one planning
// request, whatever its ship dates: the client's count of 454 for 4-6 August.
//
// Shipped volume is rebuilt per demand line (IDPR) and ship date from what the
// loads carry, then floored by the real pipeline: the run's own segregation,
// the Joiner's pallet-type choice for the shipped quantity, planFloor and
// floorBound. Nothing here reimplements the bound.
// ============================================================================

#include "pipeline.hpp"
#include "trailerSpec.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace acceptanceTests {

struct AchievedLaneDay {
    std::string locationFrom;
    std::string locationTo;
    std::string shipCondition;
    std::string shipDate;
    std::size_t loads = 0;
};

struct ShippedLine {
    std::string matnr;
    std::string unitOfMeasure;
    // Summed over every acceptance-set load that carries the line, by the load's ship date.
    std::map<std::string, double> quantityByShipDate;
};

struct AchievedRequest {
    std::string requestId;
    std::size_t solutionFiles = 0;
    std::vector<AchievedLaneDay> laneDays; // acceptance-set lanes and ship dates with a load
    std::unordered_map<std::string, ShippedLine> shippedByIdpr;
    // The acceptance set is chosen by ship condition; these count where a populated
    // LOAD_OPT_SCORE would have chosen differently.
    std::size_t scoredLoadsOutsideSet = 0;
    std::size_t unscoredLoadsInSet = 0;

    std::size_t totalLoads() const;
    std::size_t laneGroups() const; // distinct lanes, whatever their ship dates
};

// Reads every *_valid.json in directory and keeps the solutions for requestId: every load, or
// only the load whose ID is onlyLoadId when one is given. Every field the harness relies on is
// checked: a malformed file, a missing or wrong-type block or field, a load DATFR_TA that is
// not a real date, a negative or non-finite MENGE, or one IDPR shipped as two materials or
// units throws std::runtime_error naming the file and the field. Throws if no solution matches
// requestId, or no load matches onlyLoadId.
AchievedRequest readAchievedLoads(const std::string& directory, const std::string& requestId,
                                  const std::string& onlyLoadId = "");

struct LaneDayValidity {
    std::string lane; // "2027 -> 2500 TL"
    std::string shipDate;
    std::size_t achievedLoads = 0;
    long long floorTrucks = 0;
    double boundTrucks = 0.0;
    // Each term summed over the lane-day's groups, so a violation shows which term drove it.
    double weightTrucks = 0.0;
    double volumeTrucks = 0.0;
    double unitLoads = 0.0;
    double weightLb = 0.0; // cargo and pallets
    long long noStackingBaselineTrucks = 0;
};

struct ValidityResult {
    std::vector<LaneDayValidity> laneDays; // one per achieved lane and ship date
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
    // Floored volume on a lane and ship date no achieved load is on: a shipped line whose
    // demand lane differs from its load's. Its floor is set beside nothing.
    std::size_t floorLaneDaysWithoutLoads = 0;
};

// Floors the shipped volume of every acceptance-set lane and ship date with the run's own
// segregation, params and the given trailer, and sets each one's floor beside its achieved
// loads. run must be the pipeline run of the demand extract the request planned.
ValidityResult checkValidity(const ob::PipelineResult& run, const AchievedRequest& achieved,
                             const ob::TrailerSpec& trailer);

} // namespace acceptanceTests
