#pragma once
// ============================================================================
// selectionParams.hpp - M4 module C0. Every M4 setting, read once from the
// params file's "selection" block, checked at startup and never changed after.
//
// Every key is required: a missing one stops the run naming it, never a silent
// default. A provisional figure is one the file states, so it is printed with
// the run (selectionReporter section 2) and has one home to be corrected in.
//
// The block holds:
//   defaultEquipment, knownEquipment       blank ZZNA_EQUIP_SIZE takes the default
//   fullCubeFraction                       a truck is full at this share of its cube
//   maxUnitLoadsPerTruck                   anything above is a conversion error (I6)
//   m5Threshold, varietyRatio              the classifier's routing
//   knapsack, patternPacker, binPacker     each method's grains and limits
//   floor                                  rotation, orientation when fixed, patterns,
//                                          maxPositions (null for none)
//   secondsPerLane, threads                the selector's budget and workers
//   reportDetail                           Standard or Detailed
// ============================================================================

#include "json.hpp"
#include "laneClassifier.hpp"
#include "reservationBook.hpp"
#include "selectionReporter.hpp"
#include "selector.hpp"

#include <string>
#include <vector>

namespace ob {

struct SelectionParams {
    ReservationBookSettings reservations;
    LaneClassifierSettings routing;
    SelectorSettings selector;  // its clock is left empty: the run uses the steady clock
    double maxUnitLoadsPerTruck = 0.0;
    ReportDetail reportDetail = ReportDetail::Standard;
    std::string sourcePath;
};

// Reads root["selection"]. Throws std::runtime_error naming the key for a missing block or
// setting, a wrong type, or a value out of range: a grain, budget or limit that is zero,
// negative, NaN or infinite; a full-cube share outside (0, 1]; an M5 threshold below 2; a
// variety ratio outside (0, 1000]; fewer than 1 or more than 256 threads; a default equipment
// that is blank or not listed; an equipment type listed twice; an unknown, repeated or empty
// floor pattern list, or one without single rows.
SelectionParams parseSelectionParams(const nlohmann::json& root);
SelectionParams loadSelectionParams(const std::string& path);

} // namespace ob
