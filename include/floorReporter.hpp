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
//   the basis block  - extract, demand rule and the ship dates it covers (with the calendar row
//                      they came from), trailer and how it was chosen, weight limit, interior,
//                      depth limit, pallet weights and their source, deck height, rounding
//                      point and what a unit-load equivalent is;
//   section A        - where the extract falls against the run's ship dates: lines on them,
//                      before them, after them (future demand, in no floor) and undated;
//                      placeholders outside them; the daily floors and reservations added up;
//   then for each ship date (once for wholeExtract):
//   section B        - the floor: lines selected, lanes (and how many are not measurable or
//                      understated) and groups, every bound term whether or not it binds, the
//                      floor, the no-stacking baseline labelled as not being a floor, the
//                      trucks reserved that day, then every line left out and why;
//   section C        - floor by lane: groups, unit-load equivalents, cargo volume, binding
//                      term, floor ("not measurable" with no line counted, "*" if understated)
//                      and the trucks reserved on the lane that day.
// params and trailer must be the ones the run was planned against, and trailerChoice how that
// trailer was chosen. maxLanes limits each section C to the lanes with the largest floors; 0
// prints all. Throws std::invalid_argument when a floor's selection and plan are for different
// ship dates.
void printFloorReport(std::ostream& out, const FloorRun& run, const M2Params& params,
                      const TrailerSpec& trailer, TrailerChoice trailerChoice,
                      const std::string& extractLabel, std::size_t maxLanes = 0);

} // namespace ob
