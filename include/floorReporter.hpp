#pragma once
// ============================================================================
// floorReporter.hpp - Prints the M3 truck floor so that no figure can be read
// without the decisions that produced it.
//
// Presentation only: it adds up and formats what floorPlanner computed and
// decides nothing. Every line is plain ASCII; text taken from the input files
// (lane codes, the extract label, paths) has any other byte replaced by '?'.
// ============================================================================

#include "demandSelector.hpp"
#include "floorPlanner.hpp"
#include "paramsTypes.hpp"
#include "trailerSpec.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>

namespace ob {

// Prints, in order:
//   the basis block  - extract, demand rule, trailer, weight limit, stack positions, interior
//                      height, depth limit, pallet weights and their source, deck height and
//                      rounding point, each marked with the open question behind it;
//   section B        - the floor: lines selected, lanes and groups, every bound term whether
//                      or not it binds, the floor, and the no-stacking baseline labelled as
//                      not being a floor, then every line left out and why;
//   section C        - floor by lane: groups, unit loads, stacked inches, binding term, floor.
// selection must be the one plan was built from; params and trailer the ones it was planned
// against. maxLanes limits section C to the lanes with the largest floors; 0 prints all.
// Throws std::invalid_argument when selection does not carry the plan's demand rule.
void printFloorReport(std::ostream& out, const FloorPlan& plan, const DemandSelection& selection,
                      const M2Params& params, const TrailerSpec& trailer,
                      const std::string& extractLabel, std::size_t maxLanes = 0);

} // namespace ob
