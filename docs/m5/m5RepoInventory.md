# M5 repository inventory (WP0)

Status: WP0 deliverable, 17 November 2026. Turns the REPORTED repository facts of
`m5ModularArchitecture.md` Section 2 into facts checked at the current head.

- Head checked: `11c498b` on `feature/m5` = Saif's M5 (`6493ee0`, built to the Revision 4 handover
  and tests v7) merged with M4 `2c59941` (Tom's 16 Nov review fixes). M4 `4b6311a` is not merged.
- Decision (Mahib, 17 Nov 2026): Saif's `feature/m5` is the M5 base. The handover's WP1-WP10 are
  carried out as gap closure from tests v7 to tests v12.
- Check labels: READ = source read at the line given; TEST = the named test passes at the head;
  MEASURED = command run at the head, output kept in the build folder (not committed).
- Status labels: VERIFIED = as the architecture states; CHANGED = exists but differs as noted;
  NEW = added on `feature/m5`; MISSING = not in the code.

## 1. Baseline at 11c498b

Measured on 17 Nov 2026 on mains power, each step run alone. Raw output stays in the build folder
(`build-msvc-m5pr01/verify/`), not in git.

| Item | At 11c498b | Before the merge (6493ee0, M4 = f9d9945) |
|---|---|---|
| MSVC 19.44 x64 Release, `/W4 /WX /utf-8`, clean | Built in 262.3 s, 0 warnings, 0 errors | 253.1 s, 0 warnings |
| `ctest -E m4Benchmark` | 5 of 5 pass, 465.4 s | 5 of 5 pass, 566.2 s |
| `ob_tests` | 1,370 cases (34 skipped: run by other entries), 1,896,875 assertions, 308.9 s | 1,359 cases, 1,869,999 assertions |
| `augustAcceptance` / `m5Core` / `m5RealData` / `m5Published` | 11 / 193 / 15 / 1 cases pass | same cases |
| GCC 16.1 (MSYS2), `-Wall -Wextra -Wpedantic -Werror` | Fails: `array-bounds` inside libstdc++ (regex and json) from `tests/testPipelineMilestone4.cpp`, `nonnull` from `tests/m5/testM5Scenarios.cpp`. With `-Wno-error=array-bounds -Wno-error=nonnull` on the command line it builds and `ob_tests` passes 1,370 cases | Same two, plus `array-bounds` in `src/floorPattern/floorPattern.cpp` (gone after 2c59941) |
| August plan fingerprints, M5 off (Aug4 / Aug5 / Aug6) | `65792cd4121a4016` / `85c78969d9bed948` / `fd99b699f3ae884e` | `d023d0dc844bc96a` / `9463a19ed51df529` / `f79518632646835c` |
| Trucks built, M5 off | 296 / 489 / 557 of 324 / 532 / 584 reserved | same |
| Unproven routes, M5 off | 21 / 50 / 51 | 29 / 75 / 67 |
| Knapsack routes allocated one truck, proven | 79/81, 135/137, 144/153 | 79/81, 125/137, 136/153 |
| Of them on a lane-date reserving one, proven | 67/69, 115/117, 133/141 (12 unproven: 10 at the knapsack node limit, 2 at the floor-state limit) | 67/69, 107/117, 125/141 (28 unproven) |
| 7 Aug hand-over, lane 2028 -> 2574 TL | 3 groups, 747 stacks, 763 unit loads, 21 reserved; CSV SHA-256 `fa9372985fe1816af7dd55d9075f78213f95c689c7c97289584403181aa7380f`, byte-identical to the 7cef94e baseline | same |
| M5 on (shipped params) | Trucks 296 / 510 / 557; 7 Aug lane 21 trucks, 697 unit loads (unchanged by the merge); both-dates benchmark 41 trucks; Aug6 2028 -> 2190 over two dates 23 trucks; reserved = built + unused on every day | same results on those lanes |
| `planShipDates`, M5 off, 4 threads (single runs) | 26.1 / 37.0 / 34.0 s | 19.3 / 38.4 / 45.5 s (same session) |

M4-only commits merged into `feature/m5`: `2c59941`. With M5 off, every later PR must reproduce the
11c498b fingerprints above unless it says why they change.

## 2. Architecture Section 2.1 facts

| Item | At 11c498b | Status | Check |
|---|---|---|---|
| Language | C++17 (`CMakeLists.txt:4-5`) | VERIFIED | READ |
| Targets | `ob_core`, `order_builder`, `ob_tests`; ctest entries `ob_tests`, `augustAcceptance`, `m4Benchmark`, `m5Core`, `m5RealData`, `m4BenchmarkThreadsSuccess`, `m5Published` | CHANGED (4 entries added on feature/m5; no `m5Benchmark`) | READ, `ctest -N` |
| Warning flags | MSVC `/W4 /WX /utf-8` (`CMakeLists.txt:21`); GCC/Clang `-Wall -Wextra -Wpedantic -Werror` (`:23`) | VERIFIED | READ |
| GCC 16.1 exceptions | Strict build fails in `tests/testPipelineMilestone4.cpp` (`-Werror=array-bounds` inside libstdc++ regex and json code) and in `tests/m5/testM5Scenarios.cpp` (`-Werror=nonnull`, new on feature/m5; fixed in Revision 9.2: the file now compiles alone under the strict flags). Before `2c59941` it also failed in `src/floorPattern/floorPattern.cpp` | VERIFIED (array-bounds, RK-6) + CHANGED (nonnull fixed) | MEASURED |
| Vendored | `third_party/doctest.h`, `third_party/json.hpp` | VERIFIED | READ |
| Config | `config/orderBuilderParams.json`: `maxUnitLoadsPerTruck` 64 (`:19`), `pastDueOrder` AgeFirst (`:24`), `m5Threshold` 20 (`:25`), knapsack `nodeLimit` 200000 (`:27`), floor `searchStateLimit` 200000 / `routeSearchStateLimit` 1000000 (`:35-36`), `secondsPerLane` 10, `threads` 4; `m5` block with `enabled: true` (`:43-58`) | CHANGED (m5 block added) | READ |
| Fixtures | Client extracts under `tests/importer/` (ignored by `.gitignore`); committed synthetic files under `tests/fixtures/tomReview/` and `tests/m5/data/orlib/` (Section 6) | CHANGED | READ |
| Temp files | `tests/params` tests (P4) and the M5 real-data test write into one per-run folder under the system temp folder (`tests/runTempFolder.hpp`). Five M4 tests still use fixed names in the temp folder (repository owner's call) | CHANGED | READ, TEST |
| CLI | Exit 7 SelectionIncomplete, 8 SelectionProvisional (`include/pipeline.hpp:151, :154`); M5 failure reasons count toward 7 (`src/pipeline.cpp:480-487`) | VERIFIED + extended | READ |

## 3. Architecture Section 2.2 reuse table

| Item | Architecture says | At 11c498b | Status | Check |
|---|---|---|---|---|
| Escalation seam | `selector.cpp:173-177` case M5, `leaveAll(EscalatedToM5)` | Planner registered: `BundleSolver::solve` sends the lane-date to `solveLargeLane` (`selector.cpp:176`, `:281`), which checks each offered item comes back once and trucks stay within the reservation. No planner: `case SolveMethod::M5` (`:213-217`) as before; `giveUnbuiltToUrgent` skipped (`:238`) | CHANGED (seam exists) | READ; pipelineM4/pipelineM5 tests |
| Routing rule | `laneClassifier.cpp:306` reserved > m5Threshold | `laneClassifier.cpp:312-315`; threshold >= 2 enforced (`:31`) | VERIFIED (line moved) | READ; U-RT-08 |
| Settings / route | `LaneClassifierSettings{m5Threshold, varietyRatio, pastDueOrder}`, `LaneRoute{...}` | `include/laneClassifier.hpp:75`, `:87`; adds `pastDueOrderByLane` and `LaneRoute::pastDueOrder` | CHANGED (per-lane past-due order, off by default) | READ |
| Reservation dealing | `allocateReservations` `laneClassifier.cpp:95-135` | `laneClassifier.cpp:103`; past-due order taken from the lane's route | CHANGED (signature) | READ |
| Urgency order | `shipsBefore` / `rankDateOf` / `shipsBeforeOn` | `selectionItem.cpp:282`, `:286`, `:290` | VERIFIED | READ |
| dealByBestTruck | `selector.cpp:325` | `selector.cpp:396` (called `:195`) | VERIFIED (line moved) | READ |
| Selector types | `selector.hpp:101-197` | `SelectorSettings :86` (+ `largeLanePlanner`), `RouteStatus :123`, `RouteOutcome :131`, `PlannedTruck :152`, `LeftOverReason :165` (+ `AcceptanceWithheld`, `M5NoResult`, `M5ConfigurationIncomplete`, `M5InvalidInput`), `SelectionPlan :223` (+ `largeLanes`), `PackingMethod :75` (+ `M5PatternRepeat`, `M5RollingBlocks`) | CHANGED (as proposed) | READ |
| Row checks | `setRowChecks :566`, `truckLimits :421`, `arrange :687`, `floorFits :694`, `keepsMethodLoad :550`, private to `BundleSolver` | moved to `include/truckCheck.hpp` in PR-4: `RouteRows` (`arrange`, `truckLimits`, private `setRowChecks`), `RowVerdicts`, `keepsMethodLoad`, beside `floorFits`, `floorVerdict` and `checkTruckLoad`; `BundleSolver` calls them. Since `2c59941` the row checks and the relaxed-row cuts each get their own route budget of `routeSearchStateLimit` states (`selector.cpp:507-510`) | CHANGED (P2 done in PR-4) | READ |
| Parallel planning | `selectLoads :669`, workers `:700` | `selector.cpp:918`, workers `:949` | VERIFIED (lines moved) | READ |
| Date planner | `planShipDates :287`, `carriesOver 21-32`, `offerPulledForward :164`, skip `:167` | `planShipDates :296`, `carriesOver :26` (+ `AcceptanceWithheld`), `planDate :110`, `offerPulledForward :170`; the skip at `:174` now applies only with no planner registered | CHANGED (P10, ED-15) | READ |
| Pull-forward days | `daysPulledForwardAllowed()` | `include/selectionParams.hpp` (unchanged) | VERIFIED | READ |
| Stack date limit | `withShipDateLimit`, `maxDaysBetweenShipDatesInStack` | `maxDaysBetweenShipDatesInStack` in `include/paramsTypes.hpp:56`; `withShipDateLimit` exists only in `tests/stackBuilder` | CHANGED (name) | READ |
| SelectionItem | `selectionItem.hpp:46` | `:46`; adds `lineUnitLoads` (`:56`) | CHANGED (as needed by M5) | READ |
| Reservations | `reservationBook.hpp:41`, `:94` | `Reservation :41`, `findReservation :94`; equipment blank -> `defaultEquipment`, unknown -> row skipped; ALL rows of a duplicated lane-date are skipped (`reservationBook.cpp:113-128`) | VERIFIED + fact added | READ |
| Knapsack | `knapsack.hpp:158/162/177/190/215/221` | `TruckFit :167`, `TruckCheck :171`, `TruckLimits :187`, `selectForTruck :200`, `selectNextTruck :225`, `selectForTrucks :231`, `nodeLimit :110`, `proven :144`, `betterLoadUndecided :146`. Since `2c59941`: exact pass first from the greedy fill, fast pass only at the node limit; goods alike but for weight searched as one run | CHANGED (2c59941) | READ |
| Column generation | `columnGeneration.hpp:48/:55` | `solveCoveringLp :48`, `PatternPricer :55`, `Priced :62` | VERIFIED | READ |
| Pattern packer | `patternPacker.hpp:48` | `PatternPacking :48`; integer step = LP floor then pricing fill (`patternPacker.cpp:140-175`) | VERIFIED | READ |
| Bin packer | `binPacker.hpp:74/:80` | `packBins :74`, `packFirstFitDecreasing :80`, `proven :61` | VERIFIED | READ |
| Floor patterns | `floorPattern.hpp:20-34`, `arrangeFloor :136`, `arrangeFloorSorted :144`, `kExactFloorItems :121`, `FloorSearchBudget :72` | `FloorPatternKind :42` (three-across "a footprint of 33.3 in or less", `:13`), `FloorSearchBudget :73`, `fitUnproven :116`, `kExactFloorItems :122` (10), `arrangeFloor :141`, `arrangeFloorSorted :149`. Since `2c59941`: `PairingBound` (`floorPattern.cpp:581`) refuses a load whose rows pair past the trailer without a search, where `pairingBoundHolds` (`:615`) allows it | CHANGED (2c59941) | READ |
| Row candidates | `candidateRows :158`, `rowLengthShares :152`, `loadGrowthCheck :169` | `rowLengthShares :157`, `candidateRows :163`, `loadGrowthCheck :175` | VERIFIED (lines moved) | READ |
| Unit-load metrics / converter | `unitLoadMetricsFor`, `Converter::to_pallets` | Present (`include/stackRules.hpp`, `include/converter.hpp:95`); M5 does not call the converter | VERIFIED | READ |
| Pipeline | trailer once per run `144-152`, selection `334/343/612` | Trailer: `--trailer` or the largest in the params file (`pipeline.cpp:147-153`); M5 planner registered from the params file (`:562-564`) | CHANGED (registration) | READ |
| Binding constraint | `trucksIfCube` | `include/bindingConstraint.hpp:20` | VERIFIED | READ |
| Priority domain | `kMaxPriority = 11` | `include/demand_types.hpp:24` | VERIFIED | READ |
| Reporter | `selectionReporter`; `SelectorSettings::clock`, `beforeMethod` | M5 sections appended by `printSelectionReport` | CHANGED (as proposed) | READ |
| Benchmark harness | `benchmarkHarness.cpp`, `testM4Benchmark.cpp` | Present; `2c59941` adds the proof-coverage test and `m4ProofCoverage.txt` | CHANGED | READ |
| Test fixtures | `m4SyntheticDay.hpp`, `m4AugustFixtures.hpp`, `crossDayFixtures.hpp` | Present | VERIFIED | READ |
| Tom R-1 reproductions | `testSelector.cpp` 593/610, 937-981 | Kept as M4 regressions; Tom's exact R-1a/R-1b/R-5a files are now committed (Section 6) | CHANGED | READ |

## 4. M4 prerequisites (architecture Section 2.4)

| ID | At 11c498b |
|---|---|
| P1 | DONE: baseline re-run at 11c498b (Section 1). |
| P2 | DONE (PR-4): `floorFits`, `floorVerdict`, `checkTruckLoad`, `RowVerdicts`, `keepsMethodLoad` and class `RouteRows` (`arrange`, `truckLimits`, `setRowChecks`) are in `truckCheck`; `BundleSolver` keeps none of them. No behaviour change: M5-off August fingerprints and the M5-on lane-date results are identical. |
| P3 | DONE (PR-4): `collectEscalatedLaneDates(run, classification)` in `include/largeLanePlanner.hpp` (`src/largeLanePlanner/largeLanePlanner.cpp`) lists every lane-date routed to M5 without a planner; the selector, `forcedRequest` and the listing share `largeLaneRequestOf` and `routesByLaneDate`. `m5::gatherLaneDate` turns a request into the M5 input. |
| P4 | DONE (95fe5f2): the params tests write into a unique per-run temp folder. |
| P5-P7 | DONE (REPORTED at 7cef94e; unchanged). |
| P8 | The "backwards asserts" do not exist at this head or in history; the threshold check is `CHECK(shipped.m5Threshold < *overBudget)` in `testM4Benchmark.cpp`; U-RT-08 is `testM5RealData.cpp`. |
| P9 | IMPLEMENTED on feature/m5 (`selection.pastDueOrderByLane`, off by default); awaiting Tom's confirmation (D1, architecture Section 22.1); R-7 m4Path/m5Path selected. |
| P10 | DONE: `shipDatePlanner.cpp:174`. |

## 5. Names in the design documents that differ in the code

- The manifest's 31 `testFile` paths (`tests/m5/<area>/testM5*.cpp`) are not used; tests live flat in
  `tests/m5/` (decision 4: the manifest follows the code, updated in PR-2).
- `m5PatternPlanner` and `m5BlockPlanner` are one module, `include/m5Methods.hpp` / `src/m5Methods/`.
- PROPOSED types not in the code (gap-closure work): `ExactValue`/`ExactLength`, `AtomClass`, `PackingClass`,
  `CargoUse`, `PatternUse`, `CandidateClaims`, `BoundRecord`, `CancellationToken`, `BudgetSlice`,
  `FloorSearchLimits`/`FloorRouteBudget`/`FloorStateLedger`, `TrailerCapability::maxUnitLoads`; stop reasons
  `FloorSearchStateLimit`/`FloorRouteStateLimit` and outcomes `SearchLimitNoIncumbent`/`WithIncumbent`.
- Different names for the same thing: `IClock` -> `Clock`; `ProfileKey`, `PayloadHash`, `IdempotencyKey` ->
  strings; `makeM4Comparator`/`makeFxPriorityComparator` -> `makeComparator(ComparatorKind)`; `ExactLimits` ->
  `kExactSlots = 4`.

## 6. Committed synthetic fixtures

All are checked by SHA-256 before use (`tests/m5/testM5Fixtures.cpp`; list in
`tests/fixtures/tomReview/SHA256SUMS.txt`) and kept byte-exact by `.gitattributes`.

| Folder | Source | Package SHA-256 |
|---|---|---|
| `tests/fixtures/tomReview/m5ExactInputs/` (10 files) | `Mahib-M5-exact-review-inputs.zip`, Tom's engineer, 16 Nov 2026 | `c38a9a6c8199141bc3d602eff4af0fd232b2dcda110c35fca7e311d521df2f01` (tests v12 Section 40) |
| `tests/fixtures/tomReview/e6dbb0f/` (10 files) | `M4-review-reproductions-e6dbb0f.zip`, Tom's review of e6dbb0f | `82654357c5bd732147c6569b1349c027a6be8950a32bea752257606e39b60500` |
| `tests/m5/data/orlib/binpack1.txt` | OR-Library bin-packing set 1, downloaded 13 Nov 2026 | file `891fe4b3be86371b120ccaf37ce4525b5b0a2fc249d38725cd3216fbaa42b172` |

Three files appear in both of Tom's packages with the same bytes: `exactness-counterexample.json`,
`monotonicity-standard-results.json`, and the fourteen-stack probe (`original_fourteen_stack_probe.cpp` =
`proof_standard_probe.cpp`).

## 7. Client data

The client's extracts (August 4-6, the September and October days) and their derived reports stay out of git:
`.gitignore` covers `tests/importer/`, build folders and extract file names wherever they land. Real-data
tests report the missing files by name. Tracked files that hold client figures from earlier milestones
(`M1_Output_Final.txt`, `M1_Output_Revised.txt`, `m1OutputRestated.txt`) are Saif's decision on his delivery
branches; M5 adds none.
