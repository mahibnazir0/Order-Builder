# M5 Modular Architecture - Order Builder, Milestone 5 (large lanes)

Status: architecture and developer-handover phase, Revision 9. No production code is written in this phase.
Date: 17 November 2026. Revision 3 applied Tom's follow-up answers (minimum-load check moved to export after
M5; fullness by rear inches; Q6 escalation; can-go fill; goal order; per-lane past-due order; Q5) and moved
the M4 baseline to commit e51d9c4 (Tom's 3da08ab review fixes). Revision 5 moves the baseline to commit f9d9945 (Saif's fixes for Tom's
12 Nov review of e6dbb0f: row grouping and proof labelling), records the unit-load cap rule, and records that the per-lane past-due
override is deferred (AgeFirst run-wide stays current until a change is agreed). Revision 6 makes the bounded floor-search, unit-load accounting and deferred-feature contracts explicit. Revision 7 applies Tom's 14 Nov answers: the equal-service tie rule ED-21 (OPEN-COMP resolved) and the C9 decision. Revision 8 applies Tom's 16 Nov email: R-1a/R-1b inputs received, ED-21 rotating by lane and date, and a single alternate trailer type (40 ft high-cube container). The current test specification is `M5_Edge_Cases_and_Tests_v12.md` (304 named tests, 557 variants, 24 invariants, 48 EC checklist items and 10 CL rules).
Section 2.5 lists the reported baseline findings B1-B30; Section 15 preserves the Revision 3 history, Section 16 records the Revision 4 review, Section 17 the Revision 5 changes, Section 18 the Revision 6 review, Section 19 the Revision 7 changes, Section 20 the Revision 8 changes, and Section 21 the Revision 9 update. Owner: lead architect. Audience: the M5 implementer and reviewer (Saif).
Companion documents: `m5DeveloperHandover.md`, `m5TestTraceability.md`, `m5TestManifest.json`.

## 0. Evidence levels used in this document

This phase had **no direct access to the repository**. Every statement about existing code carries one of
these labels. Nothing here claims an inspection that did not happen.

| Label | Meaning |
|---|---|
| REPORTED | Path, symbol or measurement attributed by the supplied handover to reports at `7cef94e` (5-8 Nov 2026), `e51d9c4` (`m4FixVerification.txt`, 11 Nov) or `f9d9945` (`m4Fix2Verification.txt`, 14 Nov; Tom's e6dbb0f itself was not available there and e51d9c4 stood in for it). The source repository and those original report files are not included in the review bundle; neither this review nor the architecture independently verifies the attributed claims |
| HANDOVER | Named in `M5_Handover.md` or project notes; location or responsibility not independently reported |
| PROPOSED | New file, type or function introduced by this design |
| UNVERIFIED | A capability the design needs whose existence is unknown; WP0 must confirm or create it |

WP0 (developer handover) converts REPORTED/HANDOVER/UNVERIFIED items to VERIFIED at the current head before any
M5 code is written, and records every difference in `docs/m5/m5RepoInventory.md` (PROPOSED path).

## 1. Source authority

| Source | Authority | Use |
|---|---|---|
| `M5_Edge_Cases_and_Tests_v11.md` (17 Nov 2026) | Current working test specification | Test IDs, fixtures (Sections 0.5-0.6), report contract (0.7), decision register (0.3, 10), continuation map (30). Not modified here |
| Tom's follow-up replies after Revision 3 (Q6; four follow-up questions with his weekly rear-inch chart; forwarded review with his notes; M4 re-reviews at 3da08ab and e6dbb0f with his synthetic reproduction files) | Customer statements | Decide CL-01/03/04/05, goal order, can-go, per-lane order, Q5 (v7 Section 10) |
| `M5_Output_Preview_Rev3_Dated_11_6_26.docx`, internally **Revision 3, dated 6 November 2026** | Tom's answers to Q1-Q9 | Customer statements only. Its pre-answer body text is not a requirement where v9 0.7 clarifies it. Preview 3A-3E remains the report layout |
| `M5_Handover.md` (end of M4) | Context, carried client rules, lessons | Rules table (units, trailer, reservations, dates, priority, footprint, deck, floor, segregation, scope) |
| `4_5-Month-Delivery-Plan.md`, M5 row | Milestone acceptance wording | ACC-1..3 |
| `1_Order_Builder_Algorithms_Rev4_8-17-26.md` | Algorithm intent | Method family per lane type; cutting stock and column generation for large repetitive lanes; floor as common bound |
| Tool reports attributed to `7cef94e`, `e51d9c4` and `f9d9945` | Existing-code and measurement claims (REPORTED; original reports absent here) | Reuse inventory, seams, build facts; verify against actual files in WP0 |

Conflicts and resolutions (no test is weakened; nothing is presented as customer approval):

| # | Where | Conflict | Resolution |
|---|---|---|---|
| C1 | Preview Rev 3 body vs current report contract | Body still shows 84% / 3,307.5 line, "pending Q7" and per-lane ten-second wording | v9 0.7 governs: customer minimum-load check at export after M5, fullness labels, current run-wide past-due order with P9 deferred, and named timing contract; x/y appear only for an explicitly enabled optional policy. 3A-3E layout kept |
| C2 | v7 U-RM-01 "M4's normal small-lane path" vs no-recursion requirement | A full M4 route would re-escalate a large residual into M5 | Residuals use the **M4 packer layer** (knapsack, binPacker, patternPacker, truck check) through `m5Physical`/`m5Solver`, never `selector` routing. Recorded as PC-1 in the traceability file |
| C3 | v7 U-CG-08 (single columnGeneration) vs X-CG-18 (integer completion) | LP convergence alone fails X-CG-18 | Extend the shared `columnGeneration` in place; integer completion is a layer that calls it (PC-2) |
| C4 | Handover "M5 reuses selectionItem ... and the three packers" vs reported packer behaviour after 7cef94e | Packers clear `proven` when rows/cap trim goods instead of re-solving | M5 validates proof scope and unresolved improving alternatives instead of inheriting a packer flag. A valid witness proves fit; a heuristic or exhausted negative search cannot prove no fit or exclude a better load (B25). Section 8 governs optimization proof. M4 behaviour unchanged |
| C5 | Delivery plan "five largest lanes" vs three lanes | CLOSED: three real lanes plus the 38-truck reference, known-optimum benchmarks and M4 regressions (Q5) | Acceptance lane set kept as a manifest file |
| C6 | v7 RD-02 current hand-over 747 stacks / 763 unit loads vs historical 635 | 635 counts client lines, not M4 hand-over (REPORTED) | Baseline must be reproduced in WP0 before assertion |
| C7 | Delivery plan ACC-3 "agreed gap" vs Tom "I don't understand this question" | CLOSED by Tom's follow-up: careful method whenever the quick one is above a valid minimum | Gap policy isolated behind `EscalationPolicy`; F > B escalates with allowance left and F = B stops that same-objective truck-count phase. This is not a proof of the complete service/fill objective |
| C8 | Q4 "applies to all shipments" vs M4 scope | CLOSED: Tom says the minimum-load test runs at export to the host system, after the algorithm | No minimum-load rule in M4 or M5; M5 keeps the predicate as an optional, OFF policy seam |
| C9 | Handover client rule "per-truck values from the input data override these (16 Oct)" vs input format | The placeholder/reservation input has no per-truck size/weight field (B4) or per-slot unit-load cap (B26) | M5 keeps resolved per-slot capabilities in its types; current real runs use the run trailer and validated global `selection.maxUnitLoadsPerTruck` for every slot. Direct-seam heterogeneous-cap tests run now. A future feed must specify override resolution before production use (U-DA-05 input capability); it does not block the current global-cap adapter. Tom, 14 Nov: refusing a lane-date with mixed trailers is "okay" for now; he normally expects a truck-type availability list ("2 of A, 5 of B, and 1 of C") to give the mix. That list is not in the current inputs: out of scope for M5, needs a client format and its own milestone. Tom's engineer, 16 Nov: a future mixed fleet needs reservations keyed by lane, date and type, a duplicate-booking rule and per-type sizes, payload and unit-load limit; the client's files so far use only 53F or blank. A single alternate type per run (40 ft high-cube container) is tested by U-DA-07 |

## 2. Repository inventory and reuse map

### 2.1 Build and test facts (REPORTED unless stated)

| Item | Fact |
|---|---|
| Language | C++17 (project notes; HANDOVER). No C++20 features: no `std::span`, concepts, ranges, designated initializers, `<=>` |
| Build | CMake; targets `ob_core` (static library), `order_builder` (CLI), `ob_tests`; ctest entries `ob_tests`, `augustAcceptance`, `m4Benchmark` |
| Compilers | MSVC x64 VS2022 Release `/W4 /WX /utf-8` (`/O2 /Ob2 /DNDEBUG /MD`, REPORTED from Release projects); GCC/Clang `-Wall -Wextra -Wpedantic -Werror` (HANDOVER). GCC 16.1 fails `-Werror=array-bounds` inside libstdc++ regex in `tests/testPipelineMilestone4.cpp` (REPORTED; still so at f9d9945: the reported run used command-line-only `-Wno-error=array-bounds`; this remains an unresolved strict-warning-build exception, not a clean GCC `-Werror` result) |
| Tests | doctest single binary; 1,131 cases at 7cef94e, 1,149 at e51d9c4, 1,165 cases and 1,861,160 assertions at f9d9945; 18 benchmark cases `doctest::skip()` in `ob_tests`, run by `m4Benchmark` with `--no-skip` |
| Vendored | `third_party/doctest.h`, `third_party/json.hpp` (nlohmann) |
| Config | `config/orderBuilderParams.json`, all under `selection`: `m5Threshold` 20 (route iff reserved > 20), `varietyRatio` 66 (69 at 7cef94e), `pastDueOrder` "AgeFirst" (one run-wide value, no per-lane key), `carryUnshippedForward`, `pullForward`, `maxDaysPulledForward` 10, `maxUnitLoadsPerTruck` 64 (required, one global value, `config:19`), `floor.searchStateLimit` 200000 and `floor.routeSearchStateLimit` 1000000 (new at f9d9945, required, `config:35-36`), `secondsPerLane` 10 (flags only), `threads` 4 |
| Naming | camelCase for new files and identifiers; namespace `ob` (REPORTED symbols use `ob::`) |
| Fixtures | Confidential, gitignored, copied by hand (`tests/importer/august/...`, `tests/importer/crossDay/<yyyymmdd>/...`) |
| Hazard | `tests/params` tests write fixed-name temp files `tests/params/_tmp_*.json`; two concurrent runs collided (REPORTED). New M5 tests must use unique temp paths |
| CLI | `order_builder --product --demand --placeholder --params --pallets --layer-sizes --demand-rule --day --select`; exit 7 "INCOMPLETE" when stacks are left escalated to M5, exit 8 "provisional" when routes are unproven; Aug5 middle wall time 28.32 s on mains at 7cef94e, 20.08 s at e51d9c4 and 38.09 s at f9d9945 (e51d9c4 21.15 s in the same session) (REPORTED) |

### 2.2 Reuse table - existing code (REPORTED / HANDOVER)

| Existing path / symbol | Reported responsibility | M5 use | Adapter or change required | Relevant tests |
|---|---|---|---|---|
| `src/selector/selector.cpp:173-177` in `BundleSolver::solve(const vector<size_t>& routeIndices) const` (line 147): `case SolveMethod::M5: outcome.status = RouteStatus::Escalated; leaveAll(route, LeftOverReason::EscalatedToM5, ...)`; `giveUnbuiltToUrgent` skipped on escalated lane-dates (~197) | Escalation seam: every route (group) of the lane-date is left over | Replace by a call through the injected `LargeLanePlanner` interface; fall back to current behaviour when none is injected | Change in M4 file (WP8); keep M4 tests unchanged when planner absent | U-RT-01..03, X-ROUTE-*, RD-01/05 |
| `src/laneClassifier/laneClassifier.cpp:306` `if (route.reservedTrucks > settings.m5Threshold)`; `include/laneClassifier.hpp:65` `LaneClassifierSettings{int m5Threshold; double varietyRatio; PastDueOrder pastDueOrder}`; `:71` `LaneRoute{..., reservedTrucks, groupsSharingReservation, trucksAllocated, itemIndices, unitLoads, distinctProducts, demandTrucks, ..., method, methodBeforeEscalation}`; config keys live under `selection` | Routing: reserved trucks above `m5Threshold` -> M5, all groups of the lane-date | Routing stays here (CL-08). M5 reads `methodBeforeEscalation` for reporting | Add config validation of routing mode/scope (reject unsupported) | U-RT-03..05, X-ROUTE-06/07 |
| `laneClassifier.cpp:95-135` `allocateReservations`; `selectionItem.cpp` `shipsBefore` (57-60, 280-282), `shipsBeforeOn`/`rankDateOf` (284-294); knapsack objective (`knapsack.cpp:97-100, 239-241`) | Corrected M4 urgency ordering and reservation dealing (fix 2): urgency classes (rank date, priority, ship date), lexicographic, then fill, then fixed index order | Basis of CL-02 urgency precedence (ED-02; equal-service ties ED-21) | Reuse `shipsBeforeOn`/`rankDateOf`; M5 builds its key from the same classes | R-2, X-RES-06, X-E2E-07 |
| `src/selector/selector.cpp:325` `dealByBestTruck` (e51d9c4 :305; unchanged in intent at f9d9945): reservations dealt to groups by their best feasible truck, not area estimates (B18) | Group competition for short reservations | Reference behaviour for M5 allocation on small cases; R-6 checks both | None | R-6 |
| `include/selector.hpp:101-197`: `RouteStatus{Solved, Escalated, Unrouted, NoDemand, Unsolved}`; `RouteOutcome{routeIndex, status, attempts, methodUsed, proven, unprovenBecause, overBudget, lpIterationCapHit, trucksAllowed, trucksBuilt, binding, bindingChanged}`; `PlannedTruck{routeIndex, groupIndex, shipDate, items, areaSqIn, weightLb, unitLoads, weightFull, floor}`; `LeftOverReason{AfterWindow, UnavailableInWindow, NoReservation, EscalatedToM5, Unfittable, OverReservation, FloorEviction, UnitLoadCap, LaneUnsolved}`; `SelectionPlan{trucks, routes, leftOver, moved, reservations, penaltyExposureTrucks}`; `PackingMethod{Knapsack, GreedyKnapsack, PatternPacker, BinPacker, FirstFitDecreasing}` | Selection result and left-over accounting | M5 writes back `PlannedTruck`s, one `RouteOutcome` per route and `LeftOverItem`s so that M4 carry rules apply | Add `LeftOverReason::AcceptanceWithheld`, `M5NoResult`, `M5ConfigurationIncomplete`, `M5InvalidInput`; add `PackingMethod::M5PatternRepeat`, `M5RollingBlocks` (PROPOSED) | INV-01, RD-04, X-REPORT-07 |
| `selector.cpp:566` `setRowChecks(route, items, RowVerdicts&, ...)` and `:421` `TruckLimits truckLimits(...)` (f9d9945; e51d9c4 had one `truckCheck` at :408): `fits` = unit-load cap + every grouping of the rows, drawing on the route's search budget, returning `TruckFit`; `quickFits` = the rows without the grouping search; `mayStillFit` = relaxed rows through `loadGrowthCheck`; verdicts cached by sorted floor types in `RowVerdicts` (:34); `:687` `arrange(route, truck, budget)` and `:694` `static bool floorFits(layout)` (rows fit, within positions, no unplaceable item); `:550` `static bool keepsMethodLoad(...)` (e51d9c4 :394); all private members of `BundleSolver` in an anonymous namespace | Final physical check of a truck; clears `proven` when rows/cap trim goods | Physical half of M5 final validation | Extract the row checks (`setRowChecks`, `truckLimits`, `arrange`, `floorFits`, `keepsMethodLoad`) to `include/truckCheck.hpp` / `src/truckCheck/truckCheck.cpp` so M4 and M5 share one implementation (WP0 prerequisite P2) | U-AC-06, X-PAT-*, R-1c |
| `src/selector/selector.cpp:669` `selectLoads(...)`; bundles by lane-date (675-681); workers `min(threads, bundles)` (700); groups of a lane-date solved in sequence inside the bundle | Parallel planning across lane-dates | M5 runs inside a worker; must be reentrant | None; M5 holds no global mutable state | X-STATE-07, X-RUN-01, U-PF-03 |
| `src/shipDatePlanner/shipDatePlanner.cpp:287` `planShipDates(...)`; `DatePlanner::plan()` (93, dates ascending) -> `planDate(date)` (120: `classifyLanes` then `selectLoads` for one date); `carriesOver(LeftOverReason)` (21-32: only NoReservation, OverReservation, FloorEviction, UnitLoadCap carry); carry at 138-144; `offerPulledForward` (164; skips lane-dates above `m5Threshold`, 167) | Multi-date loop, carry unshipped forward, pull later goods (<= 10 days) | M5 runs per lane-date inside `selectLoads` of the current date; its leftovers then follow `carriesOver` | Map actual unshipped required goods through ED-15; `AcceptanceWithheld` applies only to an enabled optional policy. Enable supported can-go offers for registered M5 planners under P10/ED-16, after global required-service precedence | R-3, U-RS-08..11, X-DATE-*, RD-04 |
| `include/selectionParams.hpp` `daysPulledForwardAllowed()` (0 when pull-forward off) | Date rule input | Eligibility check per component | Reuse | R-3a, U-RS-10, X-DATE-06 |
| `src/stackBuilder/stackBuilder.cpp` `withShipDateLimit`, `maxDaysBetweenShipDatesInStack` | Stacks (M2) with date limits | M5 atoms are M4 stacks; component dates kept | Reuse; never re-stack across date limits | X-DATE-06, INV-07 |
| `include/selectionItem.hpp:46` `SelectionItem{groupIndex, stackIndex, copyIndex, lineIndices (bottom to top), footprintLongIn, footprintShortIn, heightIn, weightLb, unitLoads, cubeCubicIn, positions, shipDate, availDate, priority, tprio, productNumber, timing, mixesShipDates, daysPulledForward, availableAfterShipDate, unavailableInWindow}`; no stackable flag (stacking decided in M2 by `canStack`) | Stack-level planning items (one rigid floor item each) | Atom of M5 planning (ED-07) | Adapter exposes per-item components and physical class | U-SP-*, X-DATA-* |
| `include/reservationBook.hpp:41` `Reservation{origin, destination, shipCondition, shipDate, trucks, entries}`, `:94` `findReservation(...)`; entries carry only an equipment string ("53F" or blank) | Reserved trucks per lane and ship date; no per-truck size or weight | Source of the lane-date pool | Read-only view + commit delta applied by integration | U-RS-01, X-RES-* |
| `include/knapsack.hpp:190` `selectForTruck(items, capacity, settings, const TruckLimits& truck)` (branch and bound; a fast pass grows only loads `quickFits` accepts, then an exact pass seeded with its best cuts on floor, payload, `ItemLimit`s and `mayStillFit` and asks `fits` only of a whole load that would beat the best; node limit 200,000 a pass); `:215` `selectNextTruck(...)`; `:221` `selectForTrucks(...)` (truck after truck, not across trucks); `:158` `enum class TruckFit { Fits, DoesNotFit, Undecided }`; `:162` `TruckCheck = std::function<TruckFit(const std::vector<std::size_t>&)>`; `:177` `TruckLimits{fits, limits, mayStillFit, quickFits}`; `proven` stays true only when the exact pass finishes and no load better than the one chosen came back `Undecided` (`betterLoadUndecided`) (f9d9945; B17, B24, B25) | Exact single-truck selection | Best single-truck pattern for one product/class (Method A) and single-truck residuals | Adapter only | U-PB-*, R-1 |
| `include/columnGeneration.hpp:48` `solveCoveringLp(patterns, demand, iterationLimit)` (dual dense tableau, Bland's rule, cap 100,000 pivots; `CoveringLpSolution.typeValues` = duals, `patternUses`); `:55` `PatternPricer::best(values, available)` (bounded knapsack, exact unless node limit, `Priced.exact`); `include/patternPacker.hpp:48` `PatternPacking{lpTrucks, lpValue, volumeBoundTrucks, integerTrucks, gapToLp, converged, provenOptimal, unstable, lpIterationCapHit, patternsGenerated}`; integer step = LP floor + greedy fill | Cutting-stock patterns for repetitive lanes | Core of Method A, LP, duals and Farley bound | Extend in place: residual re-pricing replaces the greedy fill for M5 (WP2); evidence record | U-CG-*, X-CG-*, PUB-*, ACC-1 |
| `include/binPacker.hpp:74` `packBins(items, capacity, settings, reservedTrucks, groupIndices)` (branch and price, Ryan-Foster branching; falls back to FFD over `maxSearchItems` 100 or unsolved root LP); `:80` `packFirstFitDecreasing(...)`; `BinPacking.proven` = best equals lower bound or tree searched out, on floor area and payload | Exact-capable bin packing with FFD fallback | FFD result F; exact residual and small-model solving (branch and price) | Adapter; expose F, lower bound and proof scope | U-FF-*, X-CG-01/11 |
| `include/floorPattern.hpp:20-34` patterns wide-wide, wide-narrow, narrow-narrow, three-across (footprint <= 33.3 in), pinwheel, single; rotation allowed; `:136` `arrangeFloor(items, trailer, settings, FloorSearchBudget* = nullptr)` (defined `floorPattern.cpp:1050`; rows, used length, rear waste, positions): exact over every grouping up to `kExactFloorItems` = 10 stacks (`:121`); above ten, the sorted rows (`arrangeFloorSorted :144`) when they fit, otherwise a search over every grouping into rows (dynamic programming over footprint counts, pruned on row-length shares), bounded by `floor.searchStateLimit` (200,000 a search) and by the route's `routeSearchStateLimit` (1,000,000; `FloorSearchBudget :72`); an exhausted search returns the sorted refusal with `FloorLayout::fitUnproven` set, which is not a proof that the load does not fit (at e51d9c4 only the sorted rows decided above ten stacks: Tom's eleven stacks, 586 in, were refused at 640 in, B24). `arrangeFloor` with a null budget searches; an empty budget (`statesLeft` <= 0) does not | Floor layout of stacks (each stack one rigid floor item) | Geometry witness for every M5 truck | Adapter only; no new M6 generator | U-PB-04, X-PAT-*, X-E2E-09 |
| `include/floorPattern.hpp:158` `candidateRows(items, members, trailer, settings)` (defined `floorPattern.cpp:1162`; every row the allowed patterns form from one to four members, too wide or not); `:152` `rowLengthShares` (least trailer length each item is charged); `:169` `loadGrowthCheck(items, trailer, settings, budget)` (false only when no load of these items holding the given load fits; relaxed rows that only grow as goods are added) | Row candidates, per-item length shares, and the growth cut used by the knapsack's exact pass | M5's physical adapter and its independent oracle use `candidateRows` (the exact row-grouping oracle of R-5b/R-8 is a dynamic program over its rows and shares no code with `arrangeFloor`) | Adapter only; never call `arrangeFloor` above ten stacks and read its refusal as a proof without checking `fitUnproven` | U-PB-04, R-5a, R-5b, R-8 |
| `ob::unitLoadMetricsFor` (applies `Converter::to_pallets`, `resolvePalletSpec`, `unitLoadFootprint`, `unitLoadHeightIn`, deck rule) | Unit-load footprint, height, weight | Physical class metrics for atoms | Reuse; quantize to exact lattice (ED-ARITH) | U-PB-05/06, X-PAT-06/07, U-DA-* |
| `Converter::to_pallets` | Unit conversion CS/DIS/PAL / `Cases_Unit_Load` | Only in M4 import; M5 never calls it on already-normalized loads | None (guard test X-DATA-01) | U-DA-01, X-DATA-01/08 |
| `Pipeline::run` (`src/pipeline.cpp`): selection via `runSelection` (334/343, called at 612 when `--select`); trailer chosen once per run (144-152: 53FT_NA 630 x 100 x 108 in, 45,000 lb); stack ship-date limit set at 556; lines ~465-490 count stacks left over by reason | Import -> stacks -> selection -> report | Wiring point for injecting `LargeLanePlanner` | Add injection (WP8) | RD-*, X-E2E-01 |
| `bindingConstraint.cpp` (`trucksIfCube = totalPallets / stackPositions`) | M3 floor / binding limit | Lower-bound source for estimates only, labelled estimate | None | X-REPORT-03, U-RP-02 |
| `include/demand_types.hpp` `kMaxPriority = 11` | Priority domain | Input validation | Reuse | U-RS-05 |
| `selectionReporter` (HANDOVER); `SelectorSettings` (`selector.hpp:67`) has `secondsPerLane`, `threads`, `maxUnitLoadsPerTruck`, `std::function<double()> clock`, `beforeMethod` hook | Text report; existing clock and method hooks | Renders M5 sections 3A-3E from `M5ReportData` | Extend (WP8) | U-RP-*, X-REPORT-* |
| `src/benchmarkHarness/benchmarkHarness.cpp`, `tests/benchmarkHarness/testM4Benchmark.cpp` (`heldDay()`, `realDataSettings()`, `LinesInOurPlan`) | Four-method benchmark vs client loads; writes m4BenchmarkReport.txt / m4FillDiagnosis.txt | Extend for M4/A/B threshold benchmark (U-PF-02) | Extend; keep benchmark isolated from commit | U-PF-02/04, U-RT-08, X-RUN-10 |
| `tests/m4SyntheticDay.hpp` (`selectorSettings()`, `threads = 1`), `tests/m4AugustFixtures.hpp`, `tests/importer/crossDayFixtures.hpp`, `acceptanceTests::readAchievedLoads` | Test fixtures and client-load reader | Reuse for real-data tests | None | RD-*, U-PB-12 |
| `tests/selector/testSelector.cpp` lines 593 (now 26 unit loads, was 25) and 610 | Saif's reproductions of Tom's R-1 cases (not Tom's exact inputs); lines 937-981 hold Tom's eleven (:948) and fourteen (:971) stacks through `selectLoads` (and `tests/shipDatePlanner/testShipDatePlanner.cpp:278/286` through `planShipDates`) | Keep as M4 regressions; do not relabel as R-1a/b | None | R-1 (context only) |

### 2.3 New symbols (all PROPOSED)

`include/largeLanePlanner.hpp` (interface used by selector), `include/m5Types.hpp`, `include/m5Policy.hpp`,
`include/m5Ledger.hpp`, `include/m5Allocation.hpp`, `include/m5Solver.hpp`, `include/m5Physical.hpp`,
`include/m5PatternPlanner.hpp`, `include/m5BlockPlanner.hpp`, `include/m5Validation.hpp`, `include/m5Repair.hpp`,
`include/m5Budget.hpp`, `include/m5Orchestrator.hpp`, `include/m5Commit.hpp`, `include/m5Report.hpp`,
`include/truckCheck.hpp` (extracted), sources under `src/<module>/<module>.cpp`, tests under `tests/m5/`.

### 2.4 Prerequisites in M4 (become WP0 items)

| ID | Prerequisite | Why | Regression tests |
|---|---|---|---|
| P1 | DONE at 7cef94e, again at e51d9c4 (m4FixVerification.txt) and again at f9d9945 (m4Fix2Verification.txt, NEW BASELINE FOR M5: yes); repeat at the commit Tom accepts for M4 | RD-02, X-REPORT-03, INV-16 baseline | Existing `m4Benchmark`, RD-02 |
| P2 | Extract the row checks (`setRowChecks`, `truckLimits`, `arrange`, `floorFits`, `keepsMethodLoad`; selector.cpp:421-694 at f9d9945) from `BundleSolver` (anonymous namespace) into a shared unit without behaviour change | One physical check for M4 and M5 | Full `ob_tests` unchanged; fingerprints unchanged |
| P3 | Add gathering function `collectEscalatedLaneDates(plan, book, items)` returning per lane-date routes, items, groups, reserved count, `methodBeforeEscalation` | Today the hand-over is spread over four fields (REPORTED) | RD-02 |
| P4 | Make `tests/params` temp files unique (or document serial-only) | Parallel test runs collide | `ob_tests` params cases |
| P5 | DONE: float32 values, 9-18 decimals; M4 uses 0.001 grains (B9) | ED-ARITH revised | X-PAT-13 |
| P6 | DONE: witness supported (B10); no per-truck capability in input (B4) | PC-3 closed, PC-4/C9 open | X-E2E-09, X-RES-12, U-DA-05 |
| P7 | DONE: rule recorded (B6) | ED-02 | R-2, R-4, X-RES-06 |
| P8 | Fix backwards asserts `testM4Benchmark.cpp` ~552/554 once threshold evidence exists; verify thread test ~497 under `--success` | Align routing evidence and thread regression expectations | U-RT-08, U-PF-04 |
| P9 | M4 per-lane `pastDueOrder` override (default AgeFirst), with a test that only the switched lane changes and fingerprints do not move with no overrides. DEFERRED (Tom, 12 Nov): AgeFirst stays the current run-wide behaviour until a change is agreed; a per-lane choice needs a lane override and an agreed milestone (B27) | Tom: "user selectable by lane" (B21) | R-7, R-4, X-DATE-08 |
| P10 | Let M4 pull-forward offer can-go goods into M5 lane-dates (remove the skip at shipDatePlanner.cpp line 167 when a planner is registered), each good offered once across dates | Tom: can-go = pull-forward, preserve global required-service precedence | U-CF-01..08, RD-02/04/13, m4Benchmark |

### 2.5 What the M4 baseline confirmed and changed (Revision 2)

Baseline: `m4BaselineForM5.txt` at `7cef94e`, clean MSVC Release build (0 warnings), `ob_tests` 1131 passed,
`augustAcceptance` 11, `m4Benchmark` 18; plan fingerprints identical at 1 and 4 threads; post-fix figures SAME;
timings re-run on mains. Files: `build-msvc-m4base/baseline/m4LaneDates_Aug4/5/6.csv` (the "must not move"
file), `m5Handover_Aug5_0807.csv` (747 stacks), `inputHashes.txt`. These become the WP0 baseline once committed
under `docs/m5/baselines/` by Saif.

| # | Finding | Design change |
|---|---|---|
| B1 | Hand-over confirmed: Aug5 only, 2028 -> 2574 TL, 7 Aug, 21 reserved, S01/S03/S45, 747 stacks, 763 unit loads, estimate 22 | None. P3 gathering function still needed (no hand-over object) |
| B2 | Escalated goods are not carried and nothing is pulled into an M5 lane-date (`carriesOver`, line 167) | Historical baseline behavior only. Current design: M5 leftovers use M4 carry reasons (ED-15); P10/ED-16 add can-go offers to registered M5 lane-dates, preserving source/date accounting |
| B3 | M5 runs inside `selectLoads` of one date; dates are sequential, lane-date bundles parallel | Matches Section 7.1; seam is `BundleSolver::solve` case M5 |
| B4 | One trailer type per run; reservations carry no per-truck size/weight; equipment must be "53F" or blank | `TrailerCapability` kept per slot but all slots equal from real input; X-RES-12/X-E2E-06 run at the adapter seam; U-DA-05 capability gap (C9) |
| B5 | No stackable flag per item; each SelectionItem is a rigid stack placed as one floor item; stacking is M2's `canStack` | M-PHY never stacks items on items; nonstackability is inherited from M2 stacks; U-PB-07/X-PAT-15 assert through the stack builder (PC-8) |
| B6 | Comparator recovered (6.4): urgency classes (rank date, priority, ship date) with `rankDateOf` per `pastDueOrder`; lexicographic; then fill; then fixed index order | Reported class definition informs ED-02; the equal-service tie rule is ED-21 (Tom, 14 Nov; OPEN-COMP resolved) |
| B7 | columnGeneration: dual simplex with Bland, duals available, pricing exact unless node limit; binPacker has branch and price (Ryan-Foster) | Section 8.3/8.4: Farley bound only when `Priced.exact`; reuse `packBins` for exact residual/small models instead of new diving code |
| B8 | Proof today: knapsack per truck as built; pattern/bin proofs on floor area and payload only; 24/62/54 routes unproven, exit 8 normal | M5 proof scope names the resource model; M5 never inherits M4 `proven` |
| B9 | Masters hold float32 values printed to 9-18 decimals; M4 counts weight and area in 0.001 grains, items rounded up, capacity down | ED-ARITH revised (Section 9.3) |
| B10 | `floorPattern` places 30 units 48 x 40 x 50 in as 15 wide-wide rows, 600 x 96 in | X-E2E-09 witness confirmed (PC-3 closed) |
| B11 | Unused reservations: harness 154 = 1,440 in-window reservations - 1,286 client trucks on reserved lane-dates; the repo's client-stated figure is 195 = 1,503 placeholder trucks - 1,308 client loads | Our 98 (1,440 - 1,342) and the harness 154 use the same basis and are comparable; 195 is a different basis. RD-11/X-E2E-08 updated (PC-7) |
| B12 | Slowest lane-dates are small knapsack lanes (Aug5 2299 -> 2050, 8 Aug, 7.09 s, 2 reserved); the escalated lane costs 0 s today; one Aug6 route over 10 s (12.47 s on mains) | CL-08 calibration must measure real lane times; the large-lane threshold is not where M4's time goes today (RK-9) |
| B13 | Existing hooks: `SelectorSettings::clock` and `beforeMethod` | FX-CLOCK binds to `clock` through the `IClock` adapter |
| B14 | CLI exit 7 when goods are escalated, 8 when routes are unproven | ED-11 revised |
| B15 | 8 Aug of the same lane (20 reserved) is solved by M4: 20 trucks, 678 of 763 offered unit loads shipped (at 7cef94e) | RD-04 baseline value; re-measure at e51d9c4 |
| B16 | e51d9c4 (m4FixVerification.txt, 11 Nov): clean MSVC build, ob_tests 1149, augustAcceptance 11, m4Benchmark 18 (1,437 s), all twelve extracts present; GCC 16.1 needs `-Wno-error=array-bounds` | New M5 baseline commit |
| B17 | Knapsack no longer prunes a partial selection on a failed floor check; proof only for a truly searched truck. About 178,000 random single-truck cases match brute force; 92 failures on 7cef94e kept | M5 may reuse `selectForTruck` for single-class patterns; it still never trusts M4 `proven` beyond its stated model (R-5a/b) |
| B18 | Reservations are dealt by each group's best feasible truck (`dealByBestTruck`, selector.cpp:305), not by area estimates (Tom's 60 x 60 / 48 x 40 case) | Same principle as M5's allocation over group supplies; R-6 binds both |
| B19 | Unproven routes 32 / 73 / 66 (was 24 / 62 / 54): in 32 of 34 the plan is identical, the exact pass hits the 200,000-node limit | Honest labels; exit 8 more frequent; no design change |
| B20 | All plan fingerprints changed; Aug6 2028 -> 2574 ships 32 fewer unit loads but more of the most urgent class over its two linked dates (verified CORRECT under date-first) | Compare across linked dates (RD-13); M5 related-date checks use the same rule |
| B21 | `pastDueOrder` is still one run-wide setting at e51d9c4 | Per-lane setting is an M4 prerequisite (P9) for R-7 |
| B22 | M5 hand-over byte-identical at e51d9c4 (747 stacks, 763 unit loads); Aug5 CLI middle 20.08 s; slowest route 10.36 s (Aug6) | Hand-over baseline unchanged |
| B23 | f9d9945 (m4Fix2Verification.txt, 14 Nov; e6dbb0f was not available and e51d9c4 stood in): clean MSVC build (0 warnings), `ob_tests` 1,165 cases (18 skipped) and 1,861,160 assertions, `augustAcceptance` 11, `m4Benchmark` 18 (2,535 s), all twelve extracts present; GCC 16.1 build and tests pass with `-Wno-error=array-bounds` on the command line. Trucks 296 / 489 / 557 of 324 / 532 / 584 (98 unused) unchanged; unit loads shipped +33 / +27 / +44 against e51d9c4; cube 3,182 / 3,217 / 3,196 cu ft; rear waste 33.6 / 25.2 / 29.3 in; unproven routes 29 / 75 / 67; fingerprints d023d0dc844bc96a / 9463a19ed51df529 / f79518632646835c (all changed); hand-over byte-identical (747 stacks, 763 unit loads); 8 Aug 20 trucks, 678 of 763 | New M5 baseline commit (NEW BASELINE FOR M5: yes); previous baselines e51d9c4 and 7cef94e kept for diffs |
| B24 | Row grouping is exact: `fits` tries every grouping above ten stacks and gives up only at its state limits (200,000 a search, 1,000,000 a route), when it returns `Undecided`. Against an exact oracle (dynamic programming over every grouping, about 12,000 cases) 6 to 20 stacks were all decided, proven and equal to the best; from about 21 stacks of many sizes the knapsack node limit (200,000) leaves routes unproven; the floor limit was reached on 2 of about 1,800 sweep routes and on 5 / 6 / 11 August routes. e51d9c4 shipped less than the best on 37 percent of 11-16 stack cases, all proven. Tom's eleven stacks ship 11 in 586 in; the twenty-by-twenty case ships 26 (was 25) | M5 distinguishes a checked fit witness from a certified refusal and an undecided search above ten stacks (rule R-GROUP, Section 8.1); R-5a, R-5b, R-8 |
| B25 | A refusal by a heuristic or an exhausted floor search no longer supports `proven`: a load better than the one chosen left `Undecided` makes the truck unproven and the route says which limit it hit; the CLI exits 8 when any route is unproven and nothing lowers the code (Aug4: exit 8, 29 routes named). Tom's `floor_proof_probe` is vacuous at f9d9945 as written (its gate calls `arrangeFloor`, which now searches, so no trial reaches selection); with `arrangeFloorSorted` 503 trials reach selection and all ship eleven | Rule R-PROOF (Section 8.1): a heuristic refusal or unresolved improving load cannot support an optimization proof; a checked witness still proves fit. R-8 counts the trials that reach selection |
| B26 | The unit-load cap is configuration: `selection.maxUnitLoadsPerTruck` (config line 19, required, read at `selectionParams.cpp:113-114`, used at `selector.cpp:427/584/641/678/773`), 64 for this customer, ONE value for the whole run: no per-customer or per-equipment key and no per-slot value. 34 two-high 48 x 36 stacks ship 64 at cap 64 (576 in) and 68 at cap 68 (612 in). No repository test uses 68 | ED-20: M5 reads the cap from its trailer/equipment capability per slot, never a universal 64 and never a blanket 68; per-customer or per-slot caps need the M4 input or a code change (C9); R-9 |
| B27 | `pastDueOrder` is still one run-wide setting at f9d9945 (`config:24`, AgeFirst); there is no per-lane override. Tom: AgeFirst stays current until a change is agreed; a per-lane choice needs a lane override and an agreed milestone | P9 deferred; R-7 integration variants BLOCKED for that reason; direct-context override variants NOT_RUN |
| B28 | The October 1 and 2 extracts exist in `tests/importer/crossDay/20261001` and `20261002` (gitignored): Demands, PlaceHolder and Product-Data (pallet table and product master byte-identical on the two days); file names, sizes and SHA-256 are listed in m4Fix2Verification.txt A7. Delivery to Tom is pending (not sent or copied) | Open item 11.3; Tom's reviewer needs them |
| B29 | f9d9945 is slower: Aug5 CLI middle 38.09 s (e51d9c4 21.15 s, same session; +80 percent), `planShipDates` alone 17.4 / 36.3 / 43.8 s (e51d9c4 10.3 / 19.7 / 26.3 s), `m4Benchmark` 42.2 min (23.9 min); the slowest route per day is 4.4 / 5.5 / 12.8 s (Aug6 2299 -> 2496 TF 10 Aug, 205 stacks of 60 sizes, over the 10 s budget as before). At 10,000,000 states a route the plan takes 36.9 / 112.4 / 127.6 s and over-budget routes rise to 2 / 14 / 10 | CL-08/RK-9: M4's time grows with the exact grouping search; record it in the threshold benchmark (U-PF-02); no M5 design change |
| B30 | Against e51d9c4 trucks built are unchanged on every lane-date; plans change on 34 / 56 / 53 lane-dates (unit loads +33 / +27 / +44 net). Two Aug5 lanes ship 1 to 2 unit loads fewer by the linked-date rule (2023 -> 2508 TF 8 Aug, same pool, unproven route, -2; 2299 -> 2292 TF 8 Aug, a larger-area equal-class truck on 7 Aug leaves a different pool, -1): the rule's verdict is BUG for both, with no wrong proof | RD-13 keeps the linked-date rule; M5 related-date checks use it and report such cases instead of calling them correct |

## 3. Scope

In scope: planning lane-dates escalated by M4; Method A (repeated patterns) and Method B (rolling blocks);
shared optimization core extension; final validation; fullness labels (CL-03); can-go fill after required
goods; repair and rebalance; budget, selection, commitment; structured report; tests and evidence. The
customer's minimum-load acceptance test is NOT in scope: it runs at export, after M5 (optional policy seam only).

Out of scope (unchanged, v7 Section 8): M6 general floor-pattern generation and seven-day planning; M7 axle and
side-overhang repair and Truck Builder scoring; raw material/WIP lanes; multi-stop, temperature class, combo
lanes (parked, guarded); changing M4's own dispatch rules (RK-1); a new single-pallet filler path (CL-07).

## 4. Module structure

### 4.1 Modules and responsibilities

| Code | Module (PROPOSED unless noted) | Owns |
|---|---|---|
| M-INT | `m5Integration` | `LargeLanePlanner` implementation registered with selector; hand-over gathering; mapping results back to `SelectionPlan`; direct test seam; forced-route test hook |
| M-POL | `m5Policy` | Immutable `M5PlanningContext`, `M5PolicyProfile`, config parsing and validation, acceptance predicate, escalation/timing/commit/method policy objects |
| M-LED | `m5Ledger` | `DemandSnapshot`, source/component identity, quantity ledger, eligibility per departure date, lane-date reservation pool view, candidate-local claims |
| M-PHY | `m5Physical` | Adapter over truckCheck, floorPattern, stack rules, unitLoadMetricsFor; physical classes; layout witnesses; geometry cache |
| M-OPT | `m5Solver` + extended `columnGeneration` | Scalar/cutting-stock models, LP, bounds, integer completion, exact small solver, acceptance-adapter model, solver evidence |
| M-MA | `m5PatternPlanner` | Method A: single-class patterns, repetition, residual packing, per-group supply |
| M-MB | `m5BlockPlanner` | Method B: rolling blocks per group, short final block, per-group supply |
| M-ALC | `m5Allocation` | Comparator (CL-02 + fixture comparators), allocation keys, allocation master across groups and trailer slots |
| M-VAL | `m5Validation` | Final validation sections, acceptance on final contents, invariant checks used in production |
| M-REP | `m5Repair` | Bounded rebalance and repair moves, restoration of removed goods, invalidation |
| M-ORC | `m5Orchestrator` + `m5Budget` | Budget declaration, method scheduling, finalization, selection, outcome |
| M-COM | `m5Commit` | Commit delta, atomic apply through integration, idempotency, retry snapshots |
| M-RPT | `m5Report` (+ `selectionReporter` extension) | `M5ReportData`, ASCII rendering for 3A-3E, evidence fields |

### 4.2 Dependency diagram (compile-time, acyclic)

```text
                       config/orderBuilderParams.json
                                   |
   selector (M4) ---> largeLanePlanner.hpp (interface only, no M5 code)
        ^                          ^
        |                          | implements
   pipeline.cpp  ---registers--->  M-INT m5Integration ------------------+
                                   |   |                                  |
                                   |   v                                  v
                                   |  M-ORC m5Orchestrator ----------> M-COM m5Commit (delta only)
                                   |   |    |       |        \
                                   |   |    |       v         v
                                   |   |    |    M-ALC     M-RPT m5Report --> selectionReporter (M4)
                                   |   |    v     ^  ^
                                   |   |  M-MB ---+  |
                                   |   v   |         |
                                   |  M-MA-+---------+
                                   |   |   \       |
                                   |   |    v      v
                                   |   |   M-REP -> M-VAL
                                   |   v    |       |
                                   |  M-OPT |       |
                                   |   |    v       v
                                   +-> M-PHY ----> truckCheck / floorPattern / knapsack /
                                   |   |            binPacker / columnGeneration / stackBuilder (M4)
                                   v   v
                                  M-LED ---> selectionItem / reservationBook / selectionParams (M4)
                                   |
                                  M-POL (leaf: config + types)
```

Rules that keep it acyclic:

1. `selector` depends only on `include/largeLanePlanner.hpp`, which contains an abstract interface and plain
   data types built from existing M4 types. `ob_core` has no compile dependency on M5 implementation files.
2. M5 calls M4 **packers and checks** (M-PHY, M-OPT adapters). It never calls `selector`, `laneClassifier`
   routing or `planShipDates`. This removes the recursion risk (C2).
3. M-VAL depends on M-PHY, M-LED, M-POL only. M-REP depends on M-VAL (to recheck) but M-VAL never on M-REP.
4. M-ALC depends on M-LED and M-POL; methods depend on M-ALC only for the comparator type, never the reverse.
5. M-RPT consumes data structures; nothing depends on M-RPT except M-INT and tests.
6. M-COM produces a delta; only M-INT applies it to M4 structures.

## 5. Data contracts

All declarations are PROPOSED C++17 in namespace `ob::m5`, header `include/m5Types.hpp` unless stated. Existing
M4 types are referenced by their REPORTED names (`ob::SelectionItem`, `ob::LeftOverItem`, `ob::LeftOverReason`,
`ob::ReservationBook`); WP0 replaces any name that differs at the head commit. No C++20 features are used.

These snippets specify interfaces, not a standalone compilable header or an executed implementation.
WP1 orders definitions by dependency: physical/policy/identity primitives, validation and solver evidence,
truck/delta types, candidates, then outcomes. Value members and std::optional require complete types;
forward declarations alone do not make the conceptual presentation order valid C++. Compile the actual
assembled headers under the verified C++17 build before closing WP1.

### 5.1 Exact quantities (ED-ARITH)

```cpp
// Proposed numeric contracts, with representation/range frozen in WP1.
// ExactValue preserves the authoritative normalized numeric value (validated decimal or
// rational), its unit, and source precision. It must not round to report precision or to
// M4's physical search grain. Its backing type is chosen from verified repository support.
struct ExactLength { ExactValue value; };  // inches
struct ExactMass   { ExactValue value; };  // pounds
struct ExactCube   { ExactValue value; };  // cubic inches; 1728 cubic inches = 1 cubic foot
struct ExactTotals { ExactMass weight; ExactCube cube; };

// M4 search-lattice adapters remain distinct and conservative (reported B9).
struct MilliInch  { std::int64_t v = 0; };
struct MilliPound { std::int64_t v = 0; };
using CubeUnits = ExactCube;               // compatibility alias in proposed snippets only
// Display conversion is never an input to a decision.
double toCubicFeet(const ExactCube&);
```

WP0 records the authoritative normalized values and supported input precision actually retained by M4.
If original decimal text is unavailable, preserve and identify the normalized representation rather than
claiming to recover the original decimal. WP1 selects checked exact decimal/rational operations for that
contract; unsupported range/precision is a named input/configuration error, not silent rounding.

M4's conservative search lattice (items up, capacity down) may still be used by its physical helpers.
It must never relax a hard limit; authoritative final metrics are checked separately. Optional strict x/y
comparisons and fullness boundaries use authoritative values without nearest-grain quantization. Float32
representation error is not assumed smaller than 0.001 at every magnitude. Derived cube, sums, products,
percentage comparisons and duration conversions all require checked arithmetic. Overflow or a
nonrepresentable configured allowance returns a named error before planning (INV-23, X-DATA-12).

### 5.2 Identity and demand snapshot (M-LED)

```cpp
using SourceLineId = std::string;      // M4 source/demand line identity, preserved verbatim (X-DATA-09)
using ProductId    = std::string;      // resolved by M4 + master, never renormalized by M5
using GroupId      = std::string;      // M4 do-not-mix group label, e.g. "S01"
using ReservationVersion = std::uint64_t; // monotonic pool version; exact repository binding in WP0
using AtomIndex    = std::size_t;      // index of an M4 SelectionItem (stack) in the snapshot

enum class OfferKind { Required, CarriedRequired, CanGo };
struct Component {                     // one source line's share inside an atom
    SourceLineId source;
    OfferKind offerKind;
    Date homeShipDate;                  // original ownership date; an unused can-go offer stays here
    ProductId product;
    std::int64_t unitLoads = 0;        // already normalized by M4; M5 never divides again
    Date dueDate;                      // original due date (kept through carry)
    Date availableDate;                // AVAIL_DATE
    int priority = 0;                  // 0..kMaxPriority, validated
};

struct AtomClass {                     // physical class: identical atoms share one record
    std::string signature;             // geometry/weight/stacking only; not a cap-aware packing-class key
    ExactLength length, width, height; // final authoritative values; deck included once
    ExactMass weight;                 // final gross, tare counted once
    StackingRule stacking;             // from M4 rules: notStackable / maxHigh / ...
};

struct Atom {                          // an M4 stack (ED-07): indivisible in M5
    AtomIndex index;                   // M4 itemIndex
    GroupId group;
    std::size_t atomClass;             // index into DemandSnapshot::classes
    std::vector<Component> components; // >= 1; multi-line stacks keep every component
};

struct PackingClass {                  // homogeneous for cap-aware pattern generation
    std::size_t atomClass;             // geometry/weight class above
    std::int64_t unitLoadsPerCopy;     // checked sum of Component::unitLoads for one rigid atom
    std::string signature;             // physical class + unitLoadsPerCopy; no source ownership implied
};
struct CargoUse { AtomIndex atom; std::int64_t copies; }; // count-based, positive whole copies
using CargoView = std::vector<CargoUse>; // one truck's cargo, derived without duplicate representation

struct DemandSnapshot {                // immutable after build; shared by A and B
    SnapshotId id;                     // hash of inputs + context revision
    LaneKey lane;                      // origin, destination, ship condition, client lane id
    Date departure;                    // the lane-date being planned
    std::vector<AtomClass> classes;
    std::vector<PackingClass> packingClasses; // physical class refined by unit loads per copy
    std::vector<Atom> atoms;           // canonical prototypes/lots, not mandatory per-copy expansion
    std::vector<std::int64_t> multiplicities; // aligned with atoms; per-copy component composition retained
    std::vector<Eligibility> eligible; // per atom, for `departure` under the context date rules
    ReservationPool pool;              // see 5.3
};

struct Eligibility {
    bool eligible = false;
    EligibilityReason reason;          // Ok, NotAvailable, DueBeyondPull, PullForwardOff, OutOfScope
    std::vector<std::size_t> failingComponents;   // component-level, never hidden by the atom (X-DATE-06)
};
```

Large multiplicities: identical atoms are not expanded into copies of geometry; patterns record `copies` counts
and draw component quantities from a count-based ledger (`std::int64_t` per source). The adapter may use
multiplicity 1 for each existing M4 item, but compressed fixture/class-count inputs retain multiplicities
instead of expanding copies. `PatternUse.draws` binds repeated prototypes to exact source quantities. A
truck's atom indices/multiplicities and a repeated pattern are alternative representations of the same
cargo, never both counted. X-PAT-04 (2^31 units) works without allocating 2^31 objects.

For each rigid atom, `unitLoadsPerCopy = checked_sum(component.unitLoads)`. For a truck, sum that value
times the selected whole-copy multiplicity exactly once. An M4 unit-load total may be checked for agreement,
but stack count, row count, source-line count and a stale cached total are not substitutes. Preserve the
component composition when compressing atoms; the cap-aware packing key refines the geometry key by this
per-copy count. Equal external geometry/weight does not imply equal unit-load consumption (X-PAT-16).
Zero/negative source quantities and arithmetic overflow follow the existing input-error contract; M5 never
splits an over-cap atom to make it fit.

Required/carry ownership and can-go offers are distinct: offering a future source does not consume it or
create a second copy. Only committed source draws remove units from their home date. Unselected can-go
offers remain there and are not carried a second time. Mixed stacks preserve each component's ownership
and urgency; all components must remain eligible. Required-service precedence is global across groups,
not a gate requiring impossible required goods to be shipped before any feasible optional fill is tried.

### 5.3 Reservation pool and claims (M-LED)

```cpp
struct TrailerCapability {             // per reserved truck slot
    ExactLength length, width, height;
    ExactMass maxWeight;
    std::int64_t maxUnitLoads;         // unit-load cap of this slot: the customer/equipment limit, never a universal 64 (ED-20, R-9)
    std::string signature;             // equality key for pattern reuse (X-RES-12, X-PAT-08)
};

struct ReservationPool {               // one lane and ONE ship date (separate per date)
    ReservationVersion version;        // copied into validation and compared atomically at apply
    LaneKey lane;
    Date shipDate;
    std::vector<TrailerCapability> slots;   // size = reserved trucks; shared by all groups
    std::optional<PenaltyRate> penalty;     // CL-06; nullopt = unknown, never zero
};

class CandidateClaims {                // candidate-local; never touches the real book
public:
    explicit CandidateClaims(const DemandSnapshot& s);
    bool claimSlot(std::size_t slot);                 // false if already claimed
    bool claimUnits(const SourceLineId&, std::int64_t n);  // false if not enough left
    void release(const ClaimMark& mark);              // undo to a mark exactly once
    ClaimMark mark() const;
    const QuantityLedger& ledger() const;
};
```

`CandidateClaims` copies nothing from the real `ReservationBook`; it reads the immutable pool. Validation never
consumes anything (INV-17). Only `m5Commit` produces a delta that integration applies.

### 5.4 Candidates, validation and proof

```cpp
struct TruckPlan {
    std::size_t slot;                  // reservation slot used
    GroupId group;
    CargoView cargo;                  // whole atom copies, or resolved PatternUse draws; never both counted
    LayoutWitness layout;              // floor positions, rows, stacks; from M-PHY
    ExactTotals totals;                // recomputed on every revision
};

struct PatternUse {                    // count-based repetition, no expansion
    PatternId pattern;
    std::int64_t copies = 0;
    std::vector<SourceDraw> draws;     // which source quantities each copy consumes
};

struct CandidatePlan {
    MethodId method;                   // A, B, or a test stub
    std::uint64_t revision = 0;        // incremented by every mutation (CandidateEditor)
    std::vector<TruckPlan> trucks;
    std::vector<PatternUse> patterns;
    std::vector<LeftOver> leftOver;    // atom + reason (AcceptanceWithheld, OverReservation, NotEligible, ...)
    SolverEvidence evidence;
    std::optional<ValidationReport> validation;   // valid only if validation->revision == revision
};

struct ValidationReport {
    std::uint64_t revision;            // the candidate revision it describes
    ProfileKey profile;                // customer/profile id + revision + config hash
    SnapshotId sourceSnapshot;          // exact demand/source snapshot validated
    ReservationVersion reservationVersion; // exact slot-pool version validated
    SectionResult physical, conservation, eligibility, groups, reservations, acceptance, metadata;
    bool mandatoryChecksComplete = false; // dispatch checks only, not advisory no-fit probing
    bool allPassed() const;            // mandatory checks complete && applicable sections Pass
    // Optional acceptance OFF is NotApplicable; advisory fullness is held separately in the outcome.
    std::vector<ValidationFinding> findings;   // truck, section, reason
};

enum class ProofStatus { NotAttempted, Unproven, ProvenOptimal, ProvenInfeasible };

struct BoundRecord {
    std::int64_t value;                // trucks (count objectives)
    ModelId model;                     // e.g. "capacityOnly:allRequired:groupsSeparate"
    BoundSource source;                // L1 volume, LP(full pricing), enumeration, ...
    ModelId targetModel;
    BoundRelationship relation;        // SameModel or explicitly justified ValidRelaxation
};

struct SolverEvidence {
    std::optional<Rational> lpValue;   std::string lpStatus;        // e.g. "converged", "pricingIncomplete"
    std::optional<std::int64_t> restrictedMasterInteger;           // over generated columns only
    std::optional<std::int64_t> incumbent;                         // feasible, checked
    std::optional<BoundRecord> lowerBound;
    ProofStatus proof = ProofStatus::NotAttempted;
    ModelId proofModel;                // the model and objective the proof covers
    std::string layoutScope;           // "M4 supported floor patterns"
    Termination termination;           // Completed, IterationCap, Budget, Cancelled, Error
    std::uint64_t forRevision;         // stale if != candidate revision
};
```

Metadata that becomes invalid when a candidate changes: `validation`, every `TruckPlan::totals` of changed
trucks, `evidence.proof`/`incumbent` claims, the allocation key, acceptance and fullness metadata. Any
remaining-demand change can invalidate labels of otherwise untouched trucks. `CandidateEditor` clears
affected metadata in the same call that increments `revision` (Section 9.2).

### 5.5 Allocation key and comparator (M-ALC)

```cpp
struct UrgencyClass {
    Date rankDate;                     // rankDateOf(originalShipDate, departure, lane order)
    int priority;
    Date originalShipDate;             // complete reported M4 tuple; carry does not reset it
};
struct AllocationKey {
    std::vector<UrgencyClass> classes;  // common ordered domain across every candidate/group
    std::vector<std::int64_t> shippedByClass; // component unit loads; required classes precede can-go
    std::int64_t loadedTrucks = 0;     // nonempty, valid trucks only; no fictitious utilization
    std::vector<ExactLength> rearInchesByTruck;
    std::vector<ExactMass> weightByTruck;
    ExactCube shippedCube;            // reporting, not a substitute for rear-space comparison
    CanonicalMembership canonicalMembership; // structured records with an explicit merge/order
    std::optional<ExactValue> penalty; // excluded unless a known rate and ranking rule select it
    ComparatorId comparator;
    std::uint64_t candidateRevision = 0;
};

class AllocationComparator {
public:
    virtual ~AllocationComparator() = default;
    // true if a is strictly better than b. Must be a strict weak order.
    virtual bool better(const AllocationKey& a, const AllocationKey& b) const = 0;
    virtual std::string id() const = 0;         // printed in report and evidence
};
std::unique_ptr<AllocationComparator> makeM4Comparator(const OrderPolicy&);   // ED-02 (production)
std::unique_ptr<AllocationComparator> makeFxPriorityComparator();              // tests only
```

ED-02 specifies the confirmed precedence and the reported M4 urgency tuple. Equal-service ties use ED-21
(Tom, 14 Nov 2026: "any tiebreaker would work as long as it didn't always push to the same single side"):
T1 compares shipped unit loads per group in a group order (stable group ID, ascending) rotated by
`(laneKey mod g + dayNumber(shipDate) mod g) mod g`, where laneKey is the 32-bit FNV-1a hash of
"origin|destination|mode" (Tom's engineer, 16 Nov: rotation by lane and date); T2 compares canonical records (stable source-line ID, then slot ID). Neither
step uses input order, lane ID, truck count, fill, rear space or weight. Do not invent a sum/average,
a preference for fewer trucks, or a requirement to spread the same cargo across extra trucks. Synthetic
comparators remain executable with their explicitly declared operators. A production comparison that
requires an unfrozen operator is configuration-incomplete; it must not silently use a synthetic one.


### 5.6 Budget and cancellation (M-ORC, `include/m5Budget.hpp`)

```cpp
class IClock { public: virtual ~IClock() = default; virtual Duration now() const = 0; };
class CancellationToken { public: bool requested() const; void request(); };

struct BudgetDeclaration {             // fixed BEFORE search; never changed afterwards
    std::optional<Duration> wallAllowance;
    std::optional<std::int64_t> workAllowance;      // deterministic work units (CL-09)
    TimingContractId contract;                      // e.g. "FX-CLOCK", "secondsPerLoadTimesReserved"
};

class Budget {
public:
    Budget(BudgetDeclaration d, const IClock& clock, CancellationToken& token);
    bool exhausted() const;
    void charge(Phase p, std::int64_t workUnits);   // Phase: methodA, methodB, pricing, repair, validation, ...
    BudgetSlice slice(Phase p, std::optional<std::int64_t> cap) const;  // shares the parent deadline
    BudgetLedger ledger() const;                    // raw elapsed per phase, total, stop reason
};
```

`slice` never adds time: a slice's end is `min(parent end, cap)`. No method, block or repair receives a fresh
allowance (X-BLK-07, X-RUN-04, X-RUN-13).

Floor-state accounting is a separate limit within that same wall/work allowance:

```cpp
struct FloorSearchLimits {
    std::int64_t perSearchStates;      // selection.floor.searchStateLimit; validated positive integer
    std::int64_t perRouteStates;       // selection.floor.routeSearchStateLimit; validated positive integer
};
struct FloorStateLedger {
    std::int64_t declaredStates, statesUsed, statesLeft; // checked totals for the owned route scope
};
class FloorRouteBudget {               // one owner per planning invocation and route scope
public:
    explicit FloorRouteBudget(const FloorSearchLimits&);
    std::int64_t statesLeft() const;
    bool chargeState();               // checked debit; cannot go below zero or reset on a new call
    FloorStateLedger ledger() const;
};
using FloorRouteBudgets = std::map<GroupId, FloorRouteBudget>; // one lane-date invocation; WP0 verifies mapping
struct FloorSearchContext {
    const FloorSearchLimits& limits;
    FloorRouteBudget& route;           // borrowed shared consumption, never a copied fresh allowance
    BudgetSlice& parent;               // current phase slice shares the lane-date deadline/cancellation
};
```

The orchestrator owns the route budgets for one lane-date invocation; group methods, pricing, physical
repair and advisory probes borrow the same applicable budget. The proposed mapping is one M4 group-route
within that lane-date; WP0 verifies the actual route mapping before extraction. Preserve M4's route scope,
and record any necessary mapping change explicitly. A/B switching, a new block or candidate, or retrying a
search within the invocation cannot replenish its route states. A genuinely new independent planning
invocation receives a new declared allowance; independent routes cannot leak counters to each other.
Per-search state counters reset only for the next search, which remains limited by the route's remaining
states and the parent deadline/cancellation. M5 calls the shared floor search with an explicit context;
the reported null-budget default must not bypass this accounting. Floor-state caps, wall stops and
cancellation have distinct evidence. U-PF-09 uses a per-search cap of 3 and a route cap of 5 to verify the
shared ledger; these are synthetic values, not production settings.

### 5.7 Outcome and commit delta

```cpp
enum class StopReason { Completed, IterationCap, FloorSearchStateLimit, FloorRouteStateLimit,
                        BudgetExhausted, Cancelled, InputInvalid, ConfigurationIncomplete };
enum class ServiceLevel { Full, Partial, Zero };
enum class OutcomeKind {
    CompletedNoEligibleInput, CompletedZeroDispatch, CompletedWithDispatch,
    TimeoutNoIncumbent, TimeoutWithIncumbent, SearchLimitNoIncumbent, SearchLimitWithIncumbent,
    InvalidInput, ProvenInfeasible, ConfigurationIncomplete };

struct M5LaneOutcome {
    OutcomeKind kind;
    StopReason stop;
    bool hasCandidate = false;
    bool finalValidationPassed = false;
    ServiceLevel service = ServiceLevel::Zero; // scope named by the objective/report
    ServiceLevel requiredService = ServiceLevel::Zero;
    ServiceLevel offeredService = ServiceLevel::Zero; // includes eligible can-go offers
    ProofStatus proof = ProofStatus::NotAttempted;  ModelId proofModel;
    std::optional<CandidatePlan> selected;          // only if finalValidationPassed
    std::vector<MethodRecord> methods;              // A/B status and metrics, never summed
    std::vector<FullnessLabel> fullness; // separate advisory results for selected revision/context
    BudgetLedger timing;
    std::optional<CommitDelta> delta;               // built from `selected` only
    bool committed = false;                         // set by integration after apply
    std::vector<std::string> pendingPolicies;       // current dependencies, e.g. "R-1a/b inputs"; P9 recorded as deferred
};

struct CommitDelta {
    IdempotencyKey key;                // stable planning/commit operation, not candidate revision alone
    SnapshotId expectedSnapshot;      // input inventory version compared atomically
    ReservationVersion expectedReservationVersion; // pool version checked in the same operation
    ProfileKey expectedProfile;       // effective config hash includes resolved slot capabilities/limits
    PayloadHash payloadHash;          // selected content/profile; duplicate key must match payload
    std::vector<TruckPlan> trucks;     // final, validated
    std::vector<LeftOver> leftOver;    // with reasons, mapped to ob::LeftOverItem
    std::size_t reservationsUsed;
};
```

Empty result: `kind = CompletedNoEligibleInput` (no eligible goods) or `CompletedZeroDispatch` (search completed,
no truck passes all rules). Invalid input is `InvalidInput` with a finding list; no candidate exists and nothing
is planned. No feasible candidate after complete search with a certificate is `ProvenInfeasible`; without a
certificate it is `CompletedZeroDispatch` only if the declared search completed. A terminating floor-state
or other search cap produces `SearchLimitNoIncumbent`/`SearchLimitWithIncumbent` with its named stop;
wall-allowance exhaustion uses the timeout kinds. None implies impossibility (X-CG-12, X-RUN-12,
X-PAT-18, U-PF-09). A per-call limit need not stop the lane if other permitted work remains; preserve the
query's Undecided evidence. Advisory no-fit exhaustion alone does not change a completed dispatch outcome.

### 5.8 Interface used by selector (`include/largeLanePlanner.hpp`, PROPOSED)

```cpp
namespace ob {
struct LargeLaneRequest {              // built by M4 from existing structures (P3)
    LaneKey lane;  Date shipDate;
    std::vector<std::size_t> itemIndices;          // selected/offerable SelectionItems, all groups
    std::vector<std::size_t> sourceLineIndices;    // declared lane reporting universe, including
                                                  // unoffered date/order-excluded source rows
    SnapshotId sourceVersion;
    bool sourceScopeComplete = false;
    std::size_t reservedTrucks;
    std::vector<SolveMethod> methodBeforeEscalation;
};
struct LargeLaneResult {               // what BundleSolver writes back for the lane-date
    std::vector<PlannedTruck> trucks;              // existing M4 type (selector.hpp:130)
    std::vector<RouteOutcome> routes;              // one per route (group) of the lane-date
    std::vector<LeftOverItem> leftOver;            // reasons per ED-15
    long long reservationsUsed = 0;                // for ReservationUse.built
    std::string reportKey;                         // link to M5 report data
};
class LargeLanePlanner {
public:
    virtual ~LargeLanePlanner() = default;
    // Called once per escalated lane-date from a selector worker thread. Must be reentrant.
    // Must not mutate any argument. Returns the selected, validated result or leftovers with reasons.
    virtual LargeLaneResult plan(const LargeLaneRequest& r, const SelectionInputs& in,
                                 const SelectorSettings& s) const = 0;
};
}
```

`itemIndices` alone is not a complete residual reporting universe. P3/P10 must preserve normalized source
rows/reasons for the declared lane/date/group scope even when a row was not offered because of its date,
availability or order. `sourceLineIndices` references those immutable `SelectionInputs` rows; WP0 verifies
or supplies that public view without adding a compile dependency from M4 onto M5 implementation types.
Can-go offers are gathered before any required-demand-only no-work exit (U-CF-05). If source scope is
incomplete, report the missing residual/bucket evidence rather than treating missing rows as zero goods.

The call sits in `BundleSolver::solve` (selector.cpp:173-177), case `SolveMethod::M5`, on the worker that owns the
lane-date bundle. If no planner is registered, selector keeps its current `leaveAll(..., EscalatedToM5, ...)` behaviour, so all
existing M4 tests and the M4 benchmark stay valid while M5 is built.

## 6. Module specifications

Each module lists: purpose and owned decisions; files; public interface; inputs/outputs; units and invariants;
ownership and mutability; allowed dependencies; errors, cancellation and partial results; caches; tests and WP.

### 6.1 M-INT m5Integration (WP8; seam prerequisites in WP0)

- Purpose: connect M4 and M5; own the decision of *where* M5 runs (inside selector's per-lane-date step) and how
  results map back. Owns the direct test seam and the forced-route hook.
- Files: `include/m5Integration.hpp`, `src/m5Integration/m5Integration.cpp`, `include/largeLanePlanner.hpp`.
- Interface:
  ```cpp
  class M5LargeLanePlanner final : public ob::LargeLanePlanner {
  public:
      M5LargeLanePlanner(std::shared_ptr<const M5PlanningContextFactory> ctx, M5RunServices services);
      ob::LargeLaneResult plan(const ob::LargeLaneRequest&, const ob::SelectionInputs&,
                               const ob::SelectorSettings&) const override;
  };
  M5LaneDateInput gatherLaneDate(const ob::LargeLaneRequest&, const ob::SelectionInputs&);   // P3 data
  // Test seam: plans one lane-date directly, no M4 routing involved (v7 Section 0.4).
  M5LaneOutcome planLaneDate(const M5LaneDateInput&, const M5PlanningContext&, M5RunServices&);
  #ifdef M5_TEST_HOOKS
  void forceRoute(ob::SelectorSettings&, const LaneKey&, Date);   // never changes reservations
  #endif
  ```
- Inputs/outputs: M4 items and book -> `M5LaneDateInput`; `M5LaneOutcome` -> `LargeLaneResult`.
- Invariants: every escalated item appears exactly once in trucks or leftOver (INV-01); reservationsUsed <=
  reservedTrucks (INV-05); untouched lanes unchanged (INV-16).
- Ownership: holds `shared_ptr<const>` context factory; no mutable members; reentrant.
- Allowed dependencies: M4 public headers, M-ORC, M-COM, M-RPT, M-POL.
- Errors: configuration incomplete (malformed M5 settings) -> every item left with
  `M5ConfigurationIncomplete`, reported; invalid input -> `M5InvalidInput`; timeout without incumbent ->
  `M5NoResult`. A terminating search cap without a checked incumbent also maps to `M5NoResult`, with the
  named floor/search limit retained in its outcome. CLI exit code: keep 7 (INCOMPLETE) only when any M5
  lane ends without a planned result (ConfigurationIncomplete, InvalidInput, TimeoutNoIncumbent or
  SearchLimitNoIncumbent); normal completed leftovers are not incomplete (ED-11).
- Caches: none.
- Tests: U-RT, U-SP, U-DA, X-ROUTE, X-RUN-08, RD-01/04/05/08/09, X-E2E-01.

### 6.2 M-POL m5Policy (WP1)

- Purpose: build the immutable `M5PlanningContext` and own policy objects. Owns: config validation domains,
  acceptance predicate, which policies are confirmed / proposed / pending (printed in reports).
- Files: `include/m5Policy.hpp`, `src/m5Policy/m5Policy.cpp`; config section `"m5"` in `config/orderBuilderParams.json`.
- Interface:
  ```cpp
  enum class EvidenceScope { Synthetic, RealCustomer, ConfigIndependent };
  struct NumericAcceptance {           // CL-01: weight > x OR cube > y, strict
      ExactMass x;  ExactCube y;
      std::string profileId;  std::uint32_t profileRevision;  std::string source;   // provenance
  };
  struct FullnessRule { int weightFullPct = 98; ExactLength rearInchLimit; }; // default 14 in recorded
  enum class RemainingFitStatus { NotApplicable, Fits, ProvenNoFit, Unknown };
  enum class FullnessState { Full, NotFull, Undetermined };
  struct FullnessLabel {
      FullnessState state;
      bool byWeight, byRearInches, byFloorLimit; // independent reasons, not an exclusive enum
      ExactValue weightPct;
      ExactLength rearInches;
      RemainingFitStatus fitStatus;
      std::string fitScope;             // e.g. supported M4 layouts; never global geometry
      enum Bucket { GoodsRemain, NoGoodsRemain, RuleLimited };
      std::optional<Bucket> bucket;     // null only when required source-scope evidence is unavailable
      std::uint64_t candidateRevision;
      ProfileKey profile;
      SnapshotId remainingDemandSnapshot;
  };
  struct M5PolicyProfile {
      EvidenceScope scope;
      std::optional<NumericAcceptance> acceptance;   // OPTIONAL policy; nullopt = OFF (this customer: export check after M5)
      FullnessRule fullness;                         // CL-03: weightFullPct 98, rearInchLimit (default 14), floor-limited
      ExactValue secondsPerLoad;                         // CL-05, user setting; allowance = secondsPerLoad x reserved trucks
      std::map<LaneKey, OrderPolicy> orderByLane;    // synthetic direct-context/future P9 seam; production empty while deferred
      bool canGoFill = true;                         // FX-CANGO: pull-forward goods offered after required goods
      OrderPolicy order;                             // DateFirst (default, Q7) | PriorityFirst
      RoutingPolicy routing;                         // reservedTrucks, perLaneShipDate, threshold T
      MethodPolicy method;                           // ED-Q3
      EscalationPolicyId escalation;                 // same-objective F > B; Q6 answered
      TimingContractId timing;                       // production secondsPerLoadTimesReserved; FX-CLOCK for tests
      CommitContract commit;                         // ED-10
      ComparatorId comparator;                       // M4 (ED-02) or fixture
      std::optional<PenaltyRate> penalty;            // CL-06
      std::uint32_t blockSize = 19;
  };
  struct AcceptanceDecision { bool accepted; bool byWeight; bool byCube; ProfileKey profile; };
  AcceptanceDecision evaluateAcceptance(const ExactTotals&, const NumericAcceptance&);  // pure
  Expected<M5PolicyProfile, ConfigError> loadProfile(const nlohmann::json& m5Section, EvidenceScope);
  ```
- Invariants: synthetic profiles are constructed only in tests (`tests/m5/support/m5Fixtures.hpp`); the loader
  never returns synthetic thresholds; an absent acceptance section means the optional policy is OFF (this
  customer); a present but malformed one is `ConfigError` (U-AC-04). Rear-inch limit and seconds per load
  are validated (U-FL-03, U-PF-05). Current production order maps the existing run-wide M4 setting,
  with AgeFirst as its default/current customer value. `orderByLane` is available only in explicit synthetic
  contexts until P9 is agreed and supported upstream; the current production loader rejects an unsupported
  per-lane override instead of silently enabling it. R-4's explicit run-wide alternatives and R-7's direct
  profile-isolation variants remain runnable.
  Zero/range domain for x/y is a recorded engineering definition (U-AC-04 `zeroAndRange` variant BLOCKED until set).
- Ownership: value types; contexts are `shared_ptr<const>`.
- Allowed dependencies: json.hpp, M4 params types.
- Caches: none (profiles are values; their key is `ProfileKey{profileId, revision, configHash}`). The effective
  context's config hash includes the validated shared selection settings and resolved slot capabilities.
- Tests: U-AC-01/04, U-RT-04/05, U-RB-03, U-FF-04, X-ROUTE-06, X-REPORT-04, U-AC-08 (with M-ORC).

`loadProfile(m5Section, ...)` reads only the M5 section. The planning-context factory also receives the
validated shared M4 selection settings: run-wide order, global unit-load cap and floor-state limits.
It resolves these into the immutable slot capabilities and search limits before planning, using the
existing M4 parser/domain checks plus the stated M5 checks. It must not pretend those fields exist in the
M5-only JSON argument or silently supply their observed baseline values when absent. This mapping is
covered at the integration seam; direct tests may construct explicit capabilities/limits.

### 6.3 M-LED m5Ledger (WP3; validation types WP1)

- Purpose: preserve identity and quantities; decide eligibility per component for the planned departure; give
  candidates isolated claims on the shared lane-date pool.
- Files: `include/m5Ledger.hpp`, `src/m5Ledger/m5Ledger.cpp`.
- Interface:
  ```cpp
  Expected<DemandSnapshot, InputError> buildSnapshot(const M5LaneDateInput&, const M5PlanningContext&);
  Eligibility componentEligibility(const Component&, Date departure, const DateRules&);   // pure, M4 rules
  struct ResidualSource {
      SourceLineId source;
      GroupId group;
      Date homeShipDate;
      std::int64_t remainingUnitLoads;
      OfferKind ownership;
      Eligibility eligibility;          // physical impossibility does not make a date-eligible row absent
      std::optional<AtomIndex> atom;     // absent for an unoffered/excluded unstacked source
  };
  struct RemainingDemandView {
      SnapshotId sourceVersion;
      std::uint64_t selectedRevision;
      LaneKey lane;
      Date departure;
      bool completeForDeclaredScope;
      std::vector<ResidualSource> sources; // includes eligible and rule-excluded residual source rows
  };
  RemainingDemandView remainingAfterSelected(const M5LaneDateInput&, const CandidatePlan&,
                                             const M5PlanningContext&); // pure final reconciliation
  class QuantityLedger {                         // per source line, product, group
  public:
      std::int64_t remaining(const SourceLineId&) const;
      ReconciliationReport reconcile(const CandidatePlan&) const;   // shipped + left + carried + rejected = input
  };
  ```
- Units: unit loads as `int64` counts; stacks counted separately (RD-02 keeps 747 stacks and 763 unit loads).
- Invariants: normalization happens once in M4 (X-DATA-01); identities from M4/master kept (X-DATA-09); duplicate
  source lines detected (X-DATA-11); date arithmetic uses the M4 helper (X-DATA-10, X-DATE-10).
- Ownership: snapshot immutable and shared by A and B; claims owned by each candidate.
- Allowed dependencies: M-POL, M4 selectionItem/reservationBook/selectionParams/stackBuilder headers.
- Errors: `InputError` with source line named (X-DATA-03/13/14, X-RES-05, U-RS-05).
- Remaining-demand view: derive from the original declared source universe minus only selected source draws;
  keep unoffered date/order-excluded rows with reasons. A candidate-local eligible queue cannot establish
  NoGoodsRemain. Physically impossible but otherwise eligible residuals remain GoodsRemain. Missing source
  or geometry evidence produces an explicit incomplete diagnostic, not an invented empty set.
- Caches: eligibility memo keyed by (component id, departure date, DateRules hash, snapshot id) (X-DATE-09).
- Tests: R-3, U-RS-01/05/08..11, X-DATE-*, X-DATA-*, X-RES-05.

### 6.4 M-PHY m5Physical (WP3)

- Purpose: one gateway to M4 physical behaviour. Owns physical classes, single-class maximum loads per trailer
  capability, layout witnesses, and the geometry cache.
- Files: `include/m5Physical.hpp`, `src/m5Physical/m5Physical.cpp` (adapters over `truckCheck`, `floorPattern`,
  stack rules, `unitLoadMetricsFor`, `knapsack`).
- Interface:
  ```cpp
  struct PlacedStack {                 // sketch: identity/dimensions must match authoritative atoms
      AtomIndex atom;
      std::int64_t copyOrdinal;        // distinct whole copy within that atom's selected multiplicity
      ExactLength x, y, length, width, height;
      Rotation rotation;
  };
  struct LayoutWitness {
      std::vector<PlacedStack> stacks;  // authoritative final coordinates/dimensions
      ExactLength occupiedExtent;      // recomputed max(x + length), never trusted as a stale cache
      std::optional<MilliInch> searchUsedLength; // conservative search metric; not the fullness metric
      std::string patternKind;
  };
  enum class FloorVerdict { Fits, DoesNotFit, Undecided };
  enum class FloorStop { WitnessFound, CertifiedNoFit, SearchStateLimit, RouteStateLimit,
                         ParentBudget, Cancelled };
  struct FloorSearchEvidence {
      std::string layoutScope;
      FloorStop stop;
      std::int64_t statesUsed, routeStatesLeft;
      bool negativeCertified = false; // hard-limit certificate or complete supported-layout refusal
  };
  struct PhysicalCheck { bool ok; std::vector<PhysicalFinding> findings; }; // pure hard validation
  struct PhysicalSearchResult {
      FloorVerdict verdict;
      std::optional<LayoutWitness> witness;
      std::vector<PhysicalFinding> findings;
      FloorSearchEvidence search;
  };
  PhysicalSearchResult checkTruck(const TrailerCapability&, const CargoView&,
                                  const DemandSnapshot&, FloorSearchContext&); // bounded M4 search
  PhysicalCheck validateSuppliedWitness(const TrailerCapability&, const CargoView&,
                                       const LayoutWitness&, const DemandSnapshot&); // no replanning
  Expected<ExactLength, PhysicalFinding> occupiedExtentOf(const LayoutWitness&); // exact coordinates
  struct SingleClassSearchResult {
      std::optional<SingleClassPattern> bestChecked;
      bool maximumCertified = false; // separate from whether a checked pattern exists
      std::string proofScope;
      FloorSearchEvidence floor;
      Termination termination;        // includes an interrupted optimization/node limit
  };
  SingleClassSearchResult bestSingleClassLoad(const PackingClass&, const TrailerCapability&,
                                             const DemandSnapshot&, std::int64_t available,
                                             FloorSearchContext&);
  class GeometryCache {                          // thread-safe; geometry only, never acceptance
  public:
      std::optional<LayoutWitness> find(const GeometryKey&) const;
      void put(const GeometryKey&, LayoutWitness);
  };
  struct GeometryKey { std::string classSignature; std::string trailerSignature;
                       std::string masterRevision; std::string stackingRulesRevision;
                       std::string allowedRotationRulesRevision; std::int64_t count; };
  ```
- Search contract: `Fits` contains a validated witness; `DoesNotFit` has a hard-limit contradiction or a
  complete negative certificate for the named layout scope. A heuristic refusal or stopped search without
  such a certificate returns `Undecided`. Callers preserve the distinction in pruning, leftovers, pattern
  completeness, fullness and proof metadata. A best checked pattern can survive without a maximum proof.
  A quick positive must validate its witness; a quick negative is only a search hint. A negative growth
  prune must follow M4's documented safe relaxation, not an ordinary failed arrangement. X-PAT-18 binds
  these distinctions. Every search takes the explicit Section 5.6 floor context; pure witness validation does not
  consume grouping states and remains possible when the state allowance is spent, within the parent budget.
- Witness contract: validate supplied coordinates, exact cargo coverage, allowed orientation, overlaps, bounds and totals
  without replacing them by a newly compacted arrangement. U-FL-10's translated supplied witness exercises
  the pure validation/measurement seam, not a new production search/layout generator. U-FL-06 retains its
  sub-grain extent even when the planner's conservative search metric has coarser grains. Production labels
  use the actual final witness, and the extent is recomputed rather than read from rounded `usedLength`.
- Invariants: cube never replaces a layout (INV-20); nonstackable atoms never stacked (U-PB-07, X-PAT-15,
  X-E2E-09); footprint and deck per M4 rules (U-PB-05/06); full-precision limits (X-PAT-13).
- Errors: oversized atom -> `PhysicalFinding::Oversized` reported, never placed (U-PB-09/10, X-CG-09).
- Tests: U-PB-04..07, X-PAT-06..18, X-DATA-06, X-E2E-06, U-AC-06 (physical variants), R-8/9,
  U-PF-09 and X-STATE-11 at their physical seams.

Unit-load cap (ED-20, R-9, EC-43/44): both bounded search and pure final witness validation enforce
`checked_sum(copies * sum(component.unitLoads)) <= TrailerCapability::maxUnitLoads`. The cap is a positive
representable integer: missing, zero, negative, fractional, non-finite or out-of-range input is a named
configuration error before planning. It is read from the resolved slot capability, never defaulted to 64
or raised to 68 for every customer. Current real runs fill every slot from validated M4
`selection.maxUnitLoadsPerTruck` (64 in the REPORTED customer config, B26). Direct-seam tests can provide
different resolved caps now; production per-slot values need a supporting feed and explicit resolution
rule (C9). Do not invent precedence between future customer/equipment overrides.

The cap is part of each trailer signature and model identity. A cap-aware maximum pattern also keys on
`PackingClass::unitLoadsPerCopy`; equal geometry alone cannot authorize equal counts. Generated columns,
copies, residual loads, donor/recipient moves and actual slot assignments enforce the same resource.
Geometry-only cached witnesses still undergo current cargo/cap validation. R-9 retains Tom's 64/68 example;
X-PAT-16 checks component counting and equal geometry with different composition, X-PAT-17 checks actual
slot caps/rigid atoms across A/B/copy/repair paths, and X-STATE-11 checks cap 7 -> 5 -> 7 in one process.

### 6.5 M-OPT optimization core (WP2)

- Purpose: scalar and cutting-stock solving with honest certificates. Owns LP, bounds, integer completion,
  exact small solver, acceptance-adapter model. Extends the existing `columnGeneration`; adds `m5Solver`.
- Files: existing `patternPacker`/`columnGeneration` sources (HANDOVER; exact paths verified in WP0) extended;
  `include/m5Solver.hpp`, `src/m5Solver/m5Solver.cpp`.
- Interface:
  ```cpp
  struct CuttingStockModel {           // scalar or class-count model
      std::vector<ItemType> types;     // resources include component unit loads, cube, weight, floor; demand in whole atoms
      std::vector<Capacity> binTypes;  // resources include effective unit-load cap; distinct actual slot capabilities/counts
      DemandMode demand;               // AllRequired | PartialMaximize
      std::optional<AcceptanceAdapter> acceptance;   // per-bin minimum (X-CG-19); off by default
      GroupPartition groups;           // items of different groups never share a bin (X-CG-13)
  };
  struct SolveRequest { const CuttingStockModel& model; BudgetSlice budget; SolveOptions options; };
  struct SolveResult { std::optional<Packing> packing; SolverEvidence evidence; };
  SolveResult solveCuttingStock(const SolveRequest&);   // pricing + integer completion + proof
  SolveResult solveExactSmall(const SolveRequest&);     // enumeration; only within declared size limits
  std::optional<BoundRecord> lowerBound(const CuttingStockModel&, BoundKind);   // L1, LP(full pricing)
  FfdResult firstFitDecreasing(const CuttingStockModel&);                       // via binPacker adapter
  ```
- Invariants: INV-19 (bound tied to model/objective), INV-10 (proof on final packing), INV-18.
- Errors: invalid generated column rejected before use (X-CG-16); impossible item reported (X-CG-09).
- Cancellation: returns current checked incumbent (if any) with `Termination::Budget`/`Cancelled`; never a
  partially applied column set.
- Caches: pattern columns per (model hash) within one solve only; the hash includes unit-load composition,
  effective slot caps and required whole-atom counts. An explicitly named relaxation may omit the cap or
  indivisibility to derive a valid bound; it cannot certify a cap-constrained packing. X-CG-21 distinguishes
  the valid weak bound `ceil(14/5)=3` from the exact four-truck optimum for seven indivisible two-unit-load
  atoms at cap 5. Do not split atoms or label a three-truck packing feasible because the bound is 3.
- Tests: U-CG-*, X-CG-*, X-META-01/02/07..09, PUB-*, ACC-1. Details in Section 8.

### 6.6 M-MA m5PatternPlanner, Method A (WP4)

- Purpose: build per-group supply using repeated single-class patterns, residual packing and acceptance repair.
- Files: `include/m5PatternPlanner.hpp`, `src/m5PatternPlanner/m5PatternPlanner.cpp`.
- Interface:
  ```cpp
  struct GroupSupply {                 // what a method offers the allocation master for one group
      GroupId group;
      // plans[k] = best plan this method found with at most k trucks drawn from `slotMix`
      std::map<SlotMix, GroupPlan> plans;
      bool exactForAllK = false;       // true only when produced by an exact solver
  };
  class MethodPlanner {                // implemented by A and B
  public:
      virtual ~MethodPlanner() = default;
      virtual MethodId id() const = 0;
      virtual GroupSupply supplyForGroup(const DemandSnapshot&, const GroupId&, const SupplyRequest&,
                                         BudgetSlice&, FloorRouteBudget&) const = 0; // borrowed shared route states
  };
  class PatternPlanner final : public MethodPlanner { /* Section 7.4 */ };
  ```
- Invariants: copies <= available eligible count / pattern size and <= slots (X-PAT-01/02); remainder exactly
  once (U-PB-02); copies draw sources in comparator order and keep source identity (X-PAT-05, X-DATA-07).
- Dependencies: M-PHY, M-OPT, M-LED, M-VAL (for acceptance-aware supply), M-REP, M-ALC (comparator type only).
- Partial results: on budget stop returns the plans already finalized for some k; others absent.
- Tests: U-PB-01..03/08..12, U-RM-01..04, X-PAT-01..05/10, X-REB-01..06, X-E2E-03.

### 6.7 M-MB m5BlockPlanner, Method B (WP6)

- Purpose: Tom's rolling blocks: limit each search to a slice of demand (Q1 speed purpose) while the global
  allocation still decides reservations.
- Files: `include/m5BlockPlanner.hpp`, `src/m5BlockPlanner/m5BlockPlanner.cpp`.
- Interface: `class BlockPlanner final : public MethodPlanner` with `BlockSettings{blockSize}`.
- Invariants: blocks of exactly `blockSize` trucks except the last; the last is never padded (X-BLK-01..03);
  block claims are candidate-local; a block never takes reservations from the shared pool directly (X-BLK-04).
- Tests: U-RB-01/02, X-BLK-01..05, X-RUN-05/06.

### 6.8 M-ALC m5Allocation (WP5)

- Purpose: choose the complete allocation across groups and trailer slots. Owns the comparator and the
  allocation algorithms (exact enumeration for small instances, supply DP for large ones).
- Files: `include/m5Allocation.hpp`, `src/m5Allocation/m5Allocation.cpp`.
- Interface:
  ```cpp
  struct AllocationResult { std::vector<GroupPlan> chosen; AllocationKey key; bool provenBest; };
  AllocationResult allocate(const std::vector<GroupSupply>&, const ReservationPool&,
                            const AllocationComparator&, BudgetSlice&);
  AllocationKey keyOf(const CandidatePlan&, const DemandSnapshot&, const OrderPolicy&);
  ```
- Invariants: INV-05, INV-11; independent of group/input order (X-E2E-07); never adds reservations.
- Tests: R-2, R-4, U-RS-03/04/06/07, X-RES-*, X-REB-07, X-STATE-08, X-DATE-08, X-E2E-02/07, X-META-08/09.

### 6.9 M-VAL m5Validation (WP3; acceptance composition WP4)

- Purpose: decide whether a candidate is dispatchable on its **final** contents under the **current** context.
- Files: `include/m5Validation.hpp`, `src/m5Validation/m5Validation.cpp`.
- Interface:
  ```cpp
  ValidationReport validateFinal(const CandidatePlan&, const DemandSnapshot&, const M5PlanningContext&,
                                 BudgetSlice&); // mandatory dispatch checks only
  std::vector<FullnessLabel> evaluateFullness(const CandidatePlan&, const RemainingDemandView&,
                                            const M5PlanningContext&, BudgetSlice&, FloorRouteBudgets&); // advisory diagnostics
  ```
  Neither function mutates the candidate, snapshot or operational book. `validateFinal` charges the shared
  budget and marks mandatory checks incomplete if interrupted. `evaluateFullness` consumes the same remaining
  allowance and applicable borrowed route-state budgets but may return Undetermined diagnostics; it never changes an already passed mandatory validation
  to failed/incomplete. Budget types are common leaf contracts, not an upward dependency on orchestration.
- Sections (all must pass): physical (M-PHY per truck), conservation (ledger), eligibility (component-level),
  groups, reservations (per slot, per lane-date), optional acceptance (only when the profile selects it),
  metadata (proof/evidence revision current; proof matches its certificate/model; bound same-model or a validated relaxation).
- Also produces `FullnessLabel` values on final contents (CL-03/ED-FULL) after the selected plan's remaining
  demand has been reconciled. A label requires the current candidate/profile/remaining-demand snapshot;
  labels never reject or remove trucks. Section 9.7 defines no-fit evidence and report buckets.
- Tests: U-AC-02/03/05/06, U-FL-01/02/04, X-REB-04, X-DATE-06, X-E2E-04/09; used by every planning test.

### 6.10 M-REP m5Repair (WP4)

- Purpose: bounded changes to reach a valid accepted candidate: floor-repair restoration and acceptance
  rebalance between trucks of the same group and date.
- Files: `include/m5Repair.hpp`, `src/m5Repair/m5Repair.cpp`.
- Interface:
  ```cpp
  class CandidateEditor {              // the only way to mutate a CandidatePlan
  public:
      explicit CandidateEditor(CandidatePlan&);
      void moveAtoms(std::size_t fromTruck, std::size_t toTruck, const std::vector<AtomIndex>&);
      void removeToLeftOver(std::size_t truck, const std::vector<AtomIndex>&, LeftOverReason);
      // ++revision; clears validation/proof/key/touched totals and labels affected by changed remaining demand
  };
  RepairResult rebalanceForAcceptance(CandidatePlan&, const DemandSnapshot&, const M5PlanningContext&,
                                      BudgetSlice&, FloorRouteBudgets&, const RepairLimits&);
  ```
- Invariants: removed goods restored once (R-1c, X-PAT-10); donor and recipient rechecked (U-RM-03, X-REB-09);
  never mixes groups or dates (X-REB-02/06); rechecks actual donor/recipient unit-load caps (X-PAT-17); bounded by `RepairLimits{maxMoves, maxRounds}` and the shared wall/floor budgets.
- Cancellation: repair works on a copy; only a fully revalidated copy replaces the candidate; an interrupted
  repair is discarded (X-RUN-03, U-AC-07, INV-24).
- Tests: R-1c, U-RM-03/04, U-AC-07, X-PAT-10, X-REB-05/06/08/09.

### 6.11 M-ORC m5Orchestrator and m5Budget (WP7)

- Purpose: run the lane-date plan end to end; own budget, method scheduling, finalization and selection.
- Files: `include/m5Orchestrator.hpp`, `include/m5Budget.hpp`, `src/m5Orchestrator/*.cpp`.
- Interface: `M5LaneOutcome runLaneDate(const DemandSnapshot&, const M5PlanningContext&, M5RunServices&);`
  where `M5RunServices{const IClock&, CancellationToken&, GeometryCache&, EvidenceSink*, MethodRegistry&}`.
- Owned decisions: method order and slices (ED-Q3), stop handling, which candidate is selected.
- Tests: U-RB-04/05, U-FF-01..03/05, U-PF-03, X-STATE-*, X-RUN-01..04/11..13, X-BLK-07, U-AC-08, X-ROUTE-09.

### 6.12 M-COM m5Commit (WP7)

- Purpose: separate selection from operational mutation; idempotent apply; retry snapshots.
- Interface:
  ```cpp
  CommitDelta buildDelta(const CandidatePlan& selected, const DemandSnapshot&,
                        const M5PlanningContext&); // validation must match this effective context
  enum class ApplyStatus { Applied, AlreadyApplied, StaleSnapshot, StaleContext, PayloadConflict };
  class CommitLedger {
  public:
      // Integration owns mutation. Source/pool/current effective-profile checks, mutation and idempotency
      // form one atomic operation in the declared process/transaction scope.
      ApplyStatus applyIfCurrent(const CommitDelta&, OperationalState&);
      DemandSnapshot residualAfter(const DemandSnapshot& original, const CommitDelta&) const;
  };
  ```
- buildDelta requires mandatory validation to match the selected candidate revision, current effective profile, sourceSnapshot
  and reservationVersion; mismatch is a named validation/context error and no delta is returned. The delta
  copies that exact source/pool/effective-profile context. Before a new application, applyIfCurrent compares
  both versions and expectedProfile atomically with source/slot mutation and the operation record. An already
  recorded identical operation remains an idempotent no-op. A validation report cannot be reused on a new
  source or capability context merely because candidate revision numbers match (X-STATE-10/11).
- Tests: X-BLK-08, X-RUN-03/07, X-RES-11, X-E2E-05, INV-17/24.

### 6.13 M-RPT m5Report (WP8)

- Purpose: structured report data and ASCII rendering in the preview 3A-3E layout with v7 0.7 content.
- Interface: `M5ReportData buildReportData(const M5LaneOutcome&, const M5PlanningContext&, const RunIdentity&);`
  `std::string renderAscii(const M5ReportData&);` (pure; ASCII only; X-REPORT-09).
- Rules: production totals only from selected results (X-RUN-13, INV-22); benchmark rows labelled and separate
  (X-REPORT-02); estimates labelled (22 vs 21 is an estimate, U-RP-02); N/A for zero loads (X-RUN-12);
  fullness labels and buckets per truck (CL-03/ED-FULL); render Undetermined as "not established / fit check
  incomplete", not conclusive NOT FULL; bucket means weighted by truck count, N/A for empty buckets;
  overlapping FULL reasons counted independently without duplicating trucks; the minimum-load note ("checked at export, after M5"); seconds
  per load and the lane-date allowance; any open item printed by name.
- Tests: U-RP-*, X-REPORT-*, X-RES-10, U-RS-02, X-E2E-08, RD-10/11/12.

## 7. Planning workflow

### 7.1 End-to-end sequence

```text
planShipDates -> DatePlanner::plan -> planDate(date)  (M4, sequential over dates)
  classifyLanes(date) ; selectLoads(date) -> BundleSolver::solve per lane-date bundle (parallel workers)
    laneClassifier rule 4: reserved > T ?  -- no --> M4 methods (unchanged)
                                            -- yes --> LargeLanePlanner::plan (M-INT)
      1 gather      M-INT  gatherLaneDate: items, groups, reserved slots, methodBeforeEscalation
      2 context     M-POL  context for (profile, date rules, run-wide order, fullness, seconds per load,
                                       validated effective caps and floor-state limits; P9 deferred)
                           malformed M5 settings -> ConfigurationIncomplete (no search)
      3 snapshot    M-LED  buildSnapshot: identity, components, eligibility at this departure, pool
                           invalid input -> InvalidInput (no search)
      4 budget      M-ORC  declare allowance and owned floor-route ledgers BEFORE search (M-POL contract)
      5 candidates  M-ORC  for each scheduled method (A, then B within what is left):
                       M-MA / M-MB  supplyForGroup per group (isolated, candidate-local claims)
                       M-ALC        allocate across groups and slots (comparator)
                       assemble     CandidatePlan (trucks, patterns, leftovers with reasons)
                       finalize     M-VAL validateFinal -> M-REP repair if needed -> M-VAL again
      6 select      M-ORC  best finally-validated candidate by comparator; proof scope recorded
        diagnostic M-LED  remainingAfterSelected from complete source universe and selected draws
                   M-VAL  evaluateFullness within remaining allowance; incomplete diagnostics stay advisory
      7 outcome     M-ORC  OutcomeKind, StopReason, service level, method records, timing ledger
      8 delta       M-COM  CommitDelta from the selected candidate only
      9 report      M-RPT  M5ReportData (3A-3E)
    back in selector: M-INT applies delta atomically to the lane-date's plan and book
                      (idempotency key), writes LeftOverItems with reasons
  planDate: leftovers whose reason passes carriesOver move to the lane's next reserved date (ED-15);
  before selection, pull-forward offers can-go goods into the M5 lane-date too (ED-16 revised, P10); they rank
  after required-service precedence across all groups; impossible required atoms are reported and do not deadlock feasible fill
```

Failure and cancellation propagate as values, never as exceptions crossing the selector thread boundary. A
thrown exception inside M5 is caught in M-INT, converted to `M5NoResult` with the message, and reported; the
lane-date's items are left over (INV-15).

### 7.2 Candidate isolation

- The snapshot, context and pool are immutable and shared by A and B. Each method gets its own
  `CandidateClaims`; nothing touches `ReservationBook` or `SelectionPlan` until step 8 is applied by M-INT.
- Benchmarks (U-PF-02, RD-06/07, X-E2E-01 forced runs) call the same code with an `EvidenceSink` and a
  null commit sink; their deltas are discarded (X-ROUTE-09, X-REPORT-02).
- Method records are kept separately; totals never add A and B (INV-22, X-STATE-02).

### 7.3 Global allocation

Reservations are shared by all groups of a lane-date (U-SP-02, X-ROUTE-02, X-BLK-04). Neither a method nor a
block may consume them directly. The design uses **supply per group, decision across groups**:

1. Each method returns `GroupSupply` for every group: the best plan it found for each slot mix `k` (number of
   trucks per trailer capability) up to the pool size.
2. M-ALC chooses one plan per group so that the slot mixes fit the pool and the summed `AllocationKey` is best
   under the comparator.
3. Group order, input order, pattern repetition and block order cannot pre-empt this choice, because no plan
   is committed before step 2 and step 2 iterates groups in canonical `GroupId` order with an order-independent
   result (X-E2E-07 permuted variant).

Allocation algorithms (ED-12):

| Situation | Algorithm | Result label |
|---|---|---|
| Sum of each group's full-demand trucks fits the pool | Take every group's full plan | Optimal for the supplies |
| Small instance (<= 4 slots, <= 60 atoms, <= 6 classes per group: `ExactLimits`, configurable, test-visible) | Supplies computed by the exact small solver for every k; enumerate all combinations | `provenBest` for the stated model and comparator |
| Larger instance, scarce pool | Dynamic programme over groups; state = remaining slots per capability; value = summed key | Best over supplied plans; `provenBest = false` unless every supply is exact |

Urgency-class counts add over a common class domain. Other key fields require explicit aggregation;
canonical membership is merged and sorted, not numerically added. A DP state may discard an alternative
only when dominance under the frozen comparator is preserved by every feasible extension. If that cannot
be established for a fill/canonical operator, retain the nondominated alternatives or use exhaustive
small-case comparison; label larger results heuristic. Shared source draws, slot capability and global
required-before-can-go ordering remain constraints. For the reported three-group, 21-slot case there are
3 x 22 single-capability capacity-state positions, not necessarily only 66 retained alternatives.

Supplies for k below a group's full plan are computed as follows: the full-demand plan's trucks are ranked by
their comparator contribution and prefixes are offered for every k; in addition, for the `scarcityWindow` values
of k nearest the full plan (default 2, configurable), the method re-solves with a cap of k so that
acceptance-coupled cases (X-REB-09: one truck carries 30, not 25) are not underestimated. Exact mode re-solves
every k.

Production comparator (ED-02, urgency tuple reported from corrected M4/B6; equal-service ties ED-21): shipped unit loads by urgency class compared
lexicographically, where a class is M4's tuple (rank date, priority, ship date) sorted ascending and the rank date
comes from `rankDateOf(shipDate, truckDate, pastDueOrder)`: AgeFirst (default) ranks by the item's own ship date;
PriorityFirst ranks past-due goods at the truck date, so they compete with today's goods on priority, while goods
pulled forward keep their later date. Tom's goal order puts shipping the highest-priority goods and using all
available trucks first (can-go goods count in their later classes, so using spare trucks raises the key);
fill is best effort. After the class vector, apply ED-21 (rotating group order, then canonical records).
Rear waste and weight are reported for every truck but do not break an equal-service tie. There is no
production preference for fewer trucks. Do not infer a positive truck-count tie-break merely from that
absence, or treat a minimum-count packing certificate as proof of this comparator.
Corrected M4 deals short reservations by each group's best feasible truck (`dealByBestTruck`, B18); M5's
supply DP is the multi-truck form of the same principle, and R-6 binds both. Penalty is excluded until CL-06 is sourced. Fixture comparators (FX-PRIORITY, R-4's explicit
comparison, X-RUN-13's synthetic comparator) are separate implementations selected by test contexts only.

An unproven priority incumbent may commit in production (ED-02b) if it passes final validation; it is labelled
`Unproven` in the report. Exact Tom regressions and small oracle fixtures must still reach their stated optimum;
the tests assert that with an independent enumerator.

### 7.4 Method A - repeated patterns

```text
supplyForGroup(snapshot, g, request, slice, floorRoute):
  atoms  := eligible atoms of g, ranked by order policy then canonical source order
  separate proven-unfittable atoms (actual slot capabilities) into reported leftovers, never erase quantities
  for each packing class c in g, for each distinct trailer capability s in the pool:
      result[c,s] := M-PHY.bestSingleClassLoad(c, s, snapshot, count(c), floorContext(slice, floorRoute))
      pat[c,s] := result[c,s].bestChecked                      # unitLoadsPerCopy known; whole atoms
      usable[c,s] := pat exists and its supplied witness/cargo pass current slot validation
      preserve maximumCertified/Undecided separately; a missing pattern is not proof of no fit
      if usable[c,s] and ctx.acceptance is enabled:
          usable[c,s] &= evaluateAcceptance(pat[c,s].totals, ctx.acceptance).accepted
      # OFF never reads x/y, withholds a pattern, or invokes acceptance repair
  full plan P*:
      for classes with usable nonempty patterns in rank order of their best goods:
          copies := min(floor(eligibleCount(c) / size(pat[c,s])), slots of capability s still free in this plan)
          each copy draws source quantities in rank order (PatternUse.draws), counts only, no expansion
          verify component unit-load sum on its actual slot; never copy an over-cap pattern or split an atom
      residual R := atoms not drawn (remainders and classes without a usable full pattern)
      residual trucks := packResidual(R, slots left)              # Section 8.4: FFD, escalation, integer completion
      validate physical/eligibility/source/group/reservation constraints; repair physical violations
      if ctx.acceptance is enabled:
          trucks failing acceptance -> M-REP.rebalanceForAcceptance (same group, same eligible date window)
          trucks still failing -> removed, atoms to leftOver(AcceptanceWithheld)
      else:
          keep every physically and operationally valid truck; compute fullness labels only
      finalize and validate before publishing any incumbent
  plans[k] := prefix of P* ranked trucks for k < |P*| (+ re-solve inside scarcityWindow / exact mode); P* otherwise
```

Repetition limits: copies are bounded by eligible stock, free slots of the matching capability, and the pool;
null/empty patterns never enter copy-count division; proven-unfittable required atoms cannot deadlock feasible
can-go fill (U-CF-08). A pattern built for one capability is copied to another only if its rebound supplied
witness validates the exact cargo against the actual slot, including cap (X-RES-12, X-PAT-08/17). If a new
layout is needed, bounded `checkTruck` must return Fits with a checked witness; Undecided cannot authorize
the copy. Higher-priority goods outside the first pattern stay available
because copies draw in rank order and the allocation decides k per group, not the method.

### 7.5 Method B - rolling blocks

```text
supplyForGroup(snapshot, g, request, slice, floorRoute):
  queue := eligible atoms of g, ranked
  separate proven-unfittable atoms into reported leftovers before prefix construction (U-CF-08)
  blocks := []
  while queue not empty and trucks(blocks) < kMax and slice not exhausted:
      cap   := min(blockSize, kMax - trucks(blocks))              # last block short, never padded
      goods := longest prefix of queue whose valid lower bound (Section 8.2) is <= cap trucks
      plan  := M-OPT.solveCuttingStock(goods, cap trucks of available capabilities)  # includes unit-load resource
      physical searches borrow floorContext(slice, floorRoute); no fresh state allowance per block
      validate all hard constraints and repair physical violations
      if ctx.acceptance is enabled:
          acceptance repair inside the block; failing trucks removed, their atoms return to the queue
      # With acceptance OFF no truck is removed merely because it is underfilled.
      # Requeued atoms must not prevent progress: stop with a reason if state/queue repeats.
      if plan has no truck: stop (X-RUN-06: no empty trucks, no loop)
      blocks += plan; queue -= shipped atoms
  plans[k] := prefix of block trucks in block order (+ re-solve inside scarcityWindow / exact mode)
```

Blocks only bound the search size (Q1 purpose). The pool is consumed only by the allocation step, so an early
low-priority block cannot starve urgent goods in another group (X-RUN-05: A 20 x P11 listed first, B 2 x P0;
the DP takes both P0 trucks and 19 P11).

### 7.6 Method scheduling (Q3 delegated: ED-Q3)

Production policy `SequentialAThenB`:

1. Run A with a slice capped at 60% of the declared allowance.
2. A same-goods, same-objective valid minimum-truck bound can stop the truck-count improvement phase
   when equality is reached, as Q6 requires. That certificate is not a proof of priority, all-reservation
   use, rear-space fill or the linked-date objective. Record its scope. If required/can-go service
   opportunities remain, evaluate the service-improvement decision separately; do not suppress it because
   a different required-goods subset reached a count bound. Fill remains best effort; this rule does not
   require exhaustive search merely to prove globally best fill.
3. Run B when the applicable count gap or recorded service-improvement policy calls for it, using the
   remaining allowance. A skipped B has an explicit reason. A result may remain Unproven for the lane
   objective even when its truck-count subproblem is proven.
4. Select the better finally-validated candidate by the comparator; record both method records.

Rationale: large lanes are repetitive (on the three large lanes of 4-6 August, 66 of 82 client trucks, 80%,
carried one product; historical), so A is usually complete and cheap; B is the fallback for mixed lanes and is limited to the
remaining time, which matches Tom's speed concern (Q1). The 60% split is a configurable engineering default.
Tests use explicit `MethodPolicy` values (U-RB-04, X-RUN-04, X-RUN-13 stubs).

### 7.7 Related dates

- One snapshot per lane-date; pools are never merged across dates. The both-dates benchmark builds two
  snapshots and runs them in date order with carry, keeping 21 and 20 slots separate (ACC-2b, RD-06).
- Component due dates and availability are original values; carry does not reset age (X-DATE-04/08).
- Today M4 does not carry escalated goods (B2). With M5, leftovers with reasons OverReservation, FloorEviction,
  UnitLoadCap and AcceptanceWithheld (enabled optional policy only) carry to the lane's next reserved date (ED-15); M5NoResult,
  M5ConfigurationIncomplete and M5InvalidInput do not carry (like LaneUnsolved: the run must report them).
  This legitimately changes the next date's M4 input (8 Aug baseline: 20 trucks, 678 of 763 unit loads shipped),
  so RD-04 allows that change if it reconciles unit by unit; independent lanes stay identical (RD-09, INV-16).
- Can-go fill (ED-16 revised): pull-forward goods due D+1..D+10 and available by D are offered into the M5
  lane-date (P10). They cannot worsen the achieved feasible required-service vector anywhere in that lane-date,
  including other groups. Physically impossible required atoms are recorded; they do not prevent feasible
  can-go fill. Only selected committed draws remove units from the home date, exactly once (U-CF-01..08,
  RD-13). A can-go-only existing reserved date is supported as an explicit working engineering interpretation
  (U-CF-05); no date or reservation is invented. Pull-forward OFF prevents these offers.
- Results on linked dates are judged together: a per-date comparison can show a loss that is only a transfer
  of goods between dates (B20, RD-13).

## 8. Solver correctness and proof

### 8.1 Terms (never used interchangeably)

| Term | Meaning in M5 |
|---|---|
| LP objective value | Value of the linear relaxation over the columns generated so far; status `converged` only when exact pricing finds no improving column |
| Valid lower bound | Integer bound for a named model and objective: L1 volume per resource, ceil of a dual (Farley) bound with exact pricing, or exhaustive enumeration |
| Restricted-master integer optimum | Best integer solution using only generated columns; not a global optimum |
| Feasible incumbent | A complete integer packing that passes the model's checks |
| Full integer optimum | Incumbent equal to a valid lower bound of the same model, or proven by exhaustive enumeration |
| Physical packing witness | Exact cargo and `LayoutWitness` accepted by pure `validateSuppliedWitness` for the actual slot; no exhaustive search required to prove existence |
| Supported-layout limitation | Optimality is only with respect to layouts M4's floorPattern can produce; reported as `layoutScope` |
| Termination | Completed, IterationCap, Budget, Cancelled, Error |

Proofs from area-and-payload models (patternPacker, binPacker; B8) are scoped as such; a floor-row trim afterwards
clears them. The word "optimal" is printed only with model, objective, required goods and layout scope, for example
`ProvenOptimal(model=capacityOnly/allRequired/groupsSeparate, objective=minTrucks, layout=M4 supported patterns)`.

Row-grouping and proof rules (ED-19; B24, B25):

- R-PROOF: a validated witness proves that its particular load fits; it need not enumerate every alternative
  grouping. A negative floor verdict needs a hard-limit contradiction or a complete supported-layout
  certificate. A heuristic/sorted refusal or exhausted search without that certificate is `Undecided`,
  never proof of no fit. An unresolved load that could improve the objective prevents optimality unless
  an independent applicable certificate already excludes its improvement. A checked incumbent matching
  a valid same-objective bound may retain that scoped proof despite unrelated incomplete searches.
  Do not inherit an M4 optimization flag without checking model, objective, limits and unresolved alternatives.
- R-GROUP: within M4's supported row patterns, ANY validated grouping establishes `Fits`. Search above ten
  stacks must not treat sorted-order failure as `DoesNotFit`. An exhausted search returns `Undecided` with
  its specific stop and state evidence, retaining any earlier checked incumbent under the existing stop
  contract. A quick positive witness remains usable; physical feasibility is separate from globally best
  grouping, service, fill or minimum-truck proof (X-PAT-18).
- R-5b/R-8 use an independent exact row-grouping oracle in its declared scope; scalar `BRUTE` and
  `ENUM-ALLOC` do not certify a floor refusal. Shared `candidateRows` defines the supported row vocabulary;
  grouping search and witness checking remain independent of `arrangeFloor`. Search-limit behavior is
  exercised separately by U-PF-09, not excused as a PASS on an exact-optimum regression.


### 8.2 Bounds

- L1: for a homogeneous capability, ceil(total / capacity) per resource (including component unit loads,
  cube, weight and floor positions), per group, summed across separate groups (X-CG-13). For heterogeneous
  slots use a justified relaxation respecting its stated scope, not an average cap that overstates a
  lower bound. Unit-load totals count whole atom copies; a fractional resource bound can be weak because
  atoms are indivisible. X-CG-21 has valid bound 3 and exact optimum 4. Bound validity never makes an
  infeasible packing feasible. These are all-required minimum-truck bounds, not full service/fill proofs.
- Dual (Farley) bound: for any nonnegative dual vector u, with V = max over feasible patterns of u-value found
  by an **exact** pricing knapsack, sum(d_i u_i) / max(1, V) is a valid LP lower bound; ceil gives an integer
  bound. Computed in rational arithmetic, so an incomplete or approximate LP still yields a valid bound
  (X-CG-06, X-CG-14).
- Never: a restricted-master LP value as a bound, a bound from different goods, or a bound for one model used
  for another (INV-19). An estimate such as M4's 22 trucks is printed as an estimate (U-RP-02, X-REPORT-03).

### 8.3 Extending the existing columnGeneration

Recorded at 7cef94e (B7): `solveCoveringLp` solves the covering LP through its dual with a dense tableau and
Bland's rule (cap 100,000 pivots); duals are `CoveringLpSolution.typeValues`; `PatternPricer::best` is an exact
bounded knapsack unless its node limit stops it (`Priced.exact = false`); the integer step floors the LP uses and
fills the rest greedily; `binPacker::packBins` already runs branch and price with Ryan-Foster branching for up to
`maxSearchItems` (100) items. The extension adds only:

1. An evidence record (LP value and status, restricted integer, termination, generated columns count).
2. A column validator (negative/fractional counts, unknown items, over capacity rejected; X-CG-16).
3. Farley bound from `typeValues` and `PatternPricer::best`, used only when `Priced.exact` is true; otherwise the
   volume bound stands.
4. Duplicate-column detection and tolerance handling (X-CG-07/08).

5. Residual re-pricing as an alternative to the greedy fill after `lpFloor`, selected by M5 only, so M4's
   patternPacker results and fingerprints stay unchanged.

No second cutting-stock implementation is created (PC-2).

### 8.4 Integer completion

```text
solveCuttingStock(model, slice):
  B   := best valid lower bound (L1, Farley with exact pricing)
  inc := FFD packing (binPacker adapter)                      # F trucks
  if inc == B and inc passes every check of this model: return ProvenOptimal for this model/objective only
  if EscalationPolicy.shouldEscalate(F, B, slice):          # FX-GAP: F - B >= G and allowance left
      run column generation to convergence or slice end; B := max(B, ceil(Farley))
      residual rounding with re-pricing:
          fix floor(x_j) copies of each LP column; residual := demand - fixed
          repeat only while at least one unit is fixed and residual demand strictly decreases
          if no progress, switch to a bounded integer completion/fallback; never repeat identical residuals
          residual within declared exact limits -> supported exact solver / packBins
          larger residual -> bounded heuristic or whole-model branch-and-price if supported, otherwise
                             retain the best checked incumbent with an honest stop/proof status
      inc := best of (FFD, completed packings)
      if inc == B and inc passes every check of this model: ProvenOptimal for this model/objective only
      else if exact mode applicable: exhaustive search over pattern counts with bound B -> ProvenOptimal or improve
      else if slice left and items <= maxSearchItems: packBins branch and price on the whole model
  return inc (Unproven with gap = inc - B unless proven), evidence with termination
```

X-CG-18 trace: initial columns are the pure patterns [5,5], [5], [2,2,2,2,2], ... . The LP converges at 2.7
with duals 0.5 and 0.2; every column has reduced cost >= 0, so pricing adds nothing. The restricted-master
integer optimum is 4. B = ceil(2.7) = 3 (L1 = ceil(27/10) = 3 as well). Residual rounding fixes one [5,5] and one
[2,2,2,2,2]; residual {5, 2} is priced again and packed as [5,2]. Incumbent 3 = B, so the result is
`ProvenOptimal(capacityOnly/allRequired)` with LP 2.7 (converged), restricted integer 4, incumbent 3, bound 3.
The packing [5,5], [2,2,2,2,2], [5,2] is an equivalent valid optimum to v7's example [5,5], [5,2,2], [2,2,2,2];
the test checks count, feasibility and all nine items through the independent checker. An interrupted run
before completion reports its real termination and does not pass this case.

PUB-01..03: the L1 bounds are 48, 49 and 46 (item totals 7,078, 7,205 and 6,794 at capacity 150), equal to
the published optima, so any complete packing of that size is provably optimal once the independent checker
accepts it.

### 8.5 Acceptance-adapter model (X-CG-19)

With `CuttingStockModel::acceptance` set, every nonempty bin must pass weight > x OR cube > y.

- Columns are generated only among accepted patterns. Pricing solves two constrained knapsacks (cube > y; weight
  > x) and takes the better.
- Demand uses equality, not covering: a covering solution may "drop" surplus items, which can break acceptance.
- Feasibility is a separate question from minimum trucks. Small models are decided by exhaustive enumeration and
  return `ProvenInfeasible` with the enumeration as certificate. Larger models without a certificate return
  `CompletedZeroDispatch`/`Unproven`, never "infeasible".
- X-CG-19: accepted patterns are [6,2] and [6,2,2]; there is one 6, so at most one accepted bin; total cube 12
  exceeds one bin. Result: `ProvenInfeasible(model=accepted/allRequired)`. The capacity-only bound 2 is reported
  as a relaxation bound of a different model. The `partialService` variant (objective maximise shipped cube)
  returns [6,2,2] with one 2 left, labelled partial service.

### 8.6 Exact results are not excused by time

ExactLimits cases, Tom regressions and known-optimum cases are executed under a declared generous test budget;
failing to reach the stated value is a FAIL, whatever the stop reason. Larger interrupted runs may return a
checked incumbent with `Unproven` status only under the declared stop and commit contracts (INV-11).

## 9. Final validation, repair and caching

### 9.1 Validation sections

| Section | Checks | Source |
|---|---|---|
| Physical | Every truck: pure supplied-witness validation against exact cargo and actual slot (dimensions, overlap, height/deck, orientation/stacking, weight and component unit loads <= capability); search Unknown is not a negative certificate | M-PHY over M4 |
| Conservation | Per source line, product, group: shipped + left + carried + rejected = input; no unit twice | M-LED |
| Eligibility | Every component of every atom eligible at the departure under current date rules | M-LED |
| Groups | One group per truck | M-VAL |
| Reservations | Trucks <= slots of the lane-date; each truck's exact cargo/witness passes pure validation for its assigned slot capability | M-LED |
| Acceptance | Enabled optional policy: evaluate exact final totals on every truck. OFF: explicit NotApplicable, no x/y read and no acceptance withholding; it satisfies composition without pretending a predicate passed | M-POL |
| Metadata | Current revision/profile; proof supported by its own objective/model certificate; bound same-model or an explicitly validated relaxation. X-CG-19 may report a capacity relaxation bound beside a separately proved accepted-model infeasibility | M-VAL |

All applicable sections run; any applicable failure makes the candidate non-dispatchable. Optional
acceptance OFF is explicit NotApplicable, which satisfies validation composition. Acceptance never overrides
a hard constraint (U-AC-06). Fullness is report metadata rather than an acceptance section. Partial or
cancelled mandatory validation never promotes a new incumbent; retain the previous fully checked result.

### 9.2 Revisions and invalidation

`CandidateEditor` is the only mutator. Each call increments `revision` and clears, for the touched trucks and the
plan: totals, acceptance, validation, allocation key, proof/incumbent claims and labels affected by changed remaining demand. `validateFinal`
recomputes totals from components and whole atom-copy multiplicities, including unit loads. Selection and commitment require `validation->revision == revision` and
`validation->profile == current ProfileKey`. The effective profile/config hash includes the resolved slot
capabilities. A new cap context therefore differs even if inventory, reservation count and physical
dimensions stay the same. `buildDelta` rejects validation from the older context; the delta carries
`expectedProfile`, and `applyIfCurrent` atomically compares it with the operational context as well as the
source/pool versions. A mismatch is StaleContext and consumes nothing. X-STATE-11 constructs a new
effective context for its stale-precommit check; it does not mutate a frozen run. Pattern/model keys also
change, so stale maxima or proof cannot authorize dispatch.

### 9.3 Exact arithmetic (ED-ARITH)

Physical search retains M4's documented conservative lattice and supported-layout scope. Final hard limits,
optional acceptance and fullness use the authoritative normalized values under Section 5.1, not rounded report
text. Do not round each atom to the nearest 0.001 and then call the result full precision. In particular,
strict comparisons just above/below x/y and weight/rear fullness boundaries must retain their distinctions.
Cube is computed from authoritative dimensions with exact unit conversion and summed before display rounding (U-AC-09, U-FL-06, X-E2E-09).
WP1 records representation, range and overflow behavior; independent boundary oracles do not reuse the
production conversion helper. M4's float32 provenance is evidence to preserve, not permission to erase tails.

### 9.4 Repair loop

```text
rebalanceForAcceptance(plan, snapshot, ctx, slice, limits):
  if ctx.acceptance is OFF: return plan  # no acceptance repair/withholding in customer production
  work := copy of plan                                  # the original stays intact
  for round in 1..limits.maxRounds while slice not exhausted:
      failing := trucks in work failing acceptance (exact totals, current profile)
      if none: break
      for each failing truck t:
          donors := trucks of the same group whose atoms are eligible for t's slot and date, ranked by surplus
          move the fewest atoms that lift t over x or y while every donor still passes
          (search donor/recipient with the borrowed route-state budget, then validate both supplied witnesses,
           exact unit-load sums against actual slot caps, acceptance and eligibility; Undecided authorizes no move)
  if any truck still fails: remove it (atoms -> leftOver AcceptanceWithheld)
  report := validateFinal(work)
  if report.allPassed and validation completed within the declared stop contract: return work
  otherwise return the previously fully validated incumbent, or NoValidatedCandidate
  # Never return a newly mutated "original with trucks removed" without complete validation.
```

Bounded by `RepairLimits{maxRounds=8, maxMovesPerTruck=64}` (configurable) and the shared budget. Interrupted
repair discards `work` (X-RUN-03, U-AC-07 interrupt variant).

### 9.5 Caches

| Cache | Key | Holds | Never holds |
|---|---|---|---|
| Geometry | class signature, trailer signature, master revision, stacking/rotation-rules revision, count | Layout witness, used length | Acceptance, eligibility, stock |
| Eligibility memo | component id, departure date, DateRules hash, snapshot id | Eligible flag and reason | Anything across snapshots |
| Pattern columns | model hash including whole-atom unit-load composition, effective slot caps and resources | Cap-feasible columns within one solve | Results across solves or cap/composition changes |
| Acceptance | none (recomputed per validation call) | - | - |

A geometry hit is rebound to the actual cargo and passed through pure supplied-witness validation for
the actual slot, including unit-load cap, then acceptance, stock and date checks on every copy (INV-21).
A valid witness hit does not debit a new grouping search; validation still consumes the parent wall/work
allowance. Do not cache a cap-aware maximum under a geometry-only key. A combined row/physical verdict
cache must include all constraints it certifies, or keep pure geometry evidence separate from cap checks.
An `Undecided` result is never a reusable negative certificate; retain its stop evidence without preventing
a later fresh, properly budgeted search. X-STATE-11 checks cap 7 -> 5 -> 7 and stale validation; U-PF-09
checks that cached Unknown does not become DoesNotFit.

### 9.6 Demonstrations

U-AC-07, U-AC-08, X-REB-09 and X-RES-12 below run with the optional acceptance policy ON (synthetic). Their
productionProfile variants (v7 Section 4.14) run with it OFF: the same revision and cache rules then apply to
fullness labels instead of acceptance decisions.

- U-AC-07: candidate of 25 FX-30 units validated (37,500 lb, 3,125 cu ft, accepted on cube). Injected repair
  removes one unit through `CandidateEditor`: revision 1 -> 2, validation cleared. Even if the fault injector
  restores stale `accepted=true` fields, selection checks `validation->revision`; `validateFinal` recomputes 24
  units = 36,000 lb and exactly 3,000 cu ft (integer equality) and rejects. Interrupt variant: repair copy
  discarded; claims released once by mark.
- U-AC-08: same 20-unit candidate validated under profile A (y = 2,400: accepted), B (y = 3,000: rejected), A
  again (accepted). Geometry cache hits are allowed; acceptance is recomputed each time because `ProfileKey`
  differs and acceptance is never cached. Same-id revision variant changes `profileRevision`, so the key differs.
- X-REB-09: 50 FX-30 units, two slots. A builds 30 + 20; the 20-unit truck fails (30,000 lb, 2,500 cu ft).
  Rebalance moves 5 units: 25 + 25 (37,500 lb, 3,125 cu ft each), donor and recipient rechecked; 26 + 24 is
  rejected because 24 units = exactly 3,000 cu ft. With 49 units, every two-truck split leaves one truck <= 24
  units, so supply k = 2 has no accepted plan; k = 1 gives 30. The allocation picks one truck of 30; 19 left,
  one slot unused. Exact mode makes this provable.
- X-RES-12: slots with 45,000 and 30,000 lb. The 30-unit pattern is built for the 45,000 lb signature only; on
  the 30,000 lb slot `bestSingleClassLoad` gives 20 units. Variant A (y = 2,400): both accepted, 50 shipped.
  Variant B (y = 3,000): the 20-unit truck fails both branches, so one truck of 30 ships, 20 left, one slot unused.

### 9.7 Fullness and remaining-fit contract (ED-FULL, engineering clarification)

Compute weight and rear-space reasons independently on final contents. Weight uses exact
`100 * finalWeight >= 98 * trailerLimit`; rear-space uses `0 <= rearInches <= configuredLimit` from a
validated M4 layout. Rear space is trailer length minus the farthest occupied longitudinal extent,
not total cube, a sum of row widths or an unvalidated placement count (U-FL-10). Invalid layout or negative
rear space is a physical error, not fullness. U-FL-07 checks maximum legal stacking upstream through M2;
M5 preserves the resulting rigid atoms and component constraints.
Report all applicable reasons; presentation may choose an ordered primary reason without losing the rest.

For the floor-limited reason, form the final remaining same-group candidate set after source reconciliation.
Only atoms that otherwise satisfy source/date/priority eligibility are physical-fit probes. A nonempty set
must have `ProvenNoFit` for every atom within the explicitly supported M4 layout model before floor-limited
is asserted. A heuristic rejection, interruption or incomplete enumeration yields `Unknown`, not a proof of
no fit. Do not claim global two-dimensional packing impossibility from one failed arrangement. M5 does not
introduce M6 layouts or split/re-stack M4 atoms to manufacture a label.

This non-vacuity/no-fit-evidence rule is an engineering interpretation of "no other items could be placed",
not an additional quote or approval from Tom. If the remaining candidate set is empty, no floor-limited
reason is asserted: use NoGoodsRemain when no source goods remain, or RuleLimited when goods remain but
only rule-excluded goods are left. Weight/rear reasons can still make such a truck FULL. If eligible goods
remain, use GoodsRemain; track physical impossibility separately from date/priority exclusion. Any unknown
fit status is reported. If weight/rear do not establish FULL and probing is incomplete, set
FullnessState::Undetermined and render "not established / fit check incomplete", not conclusive NOT FULL.
A positive weight/rear reason may establish FULL while no-fit probing is Unknown. Diagnostic probing shares
the allowance and must not gate dispatch of an otherwise fully validated truck. Buckets use final demand,
not demand when the truck was first built (U-FL-06..10, X-REPORT-10).

Labels are tied to candidate revision, profile and remaining-demand snapshot. Can-go addition/removal,
repair, source reconciliation or threshold changes invalidate them. Report aggregation uses the trucks'
actual labels and counts, with N/A for empty buckets.

## 10. Budget, outcomes and commitment

### 10.1 Budget

- Declared in step 4 before any search from the timing contract: FX-CLOCK in tests (explicit allowance or work
  count); in production (ED-14, stated to Tom) the allowance is `secondsPerLoad x reservedTrucks`, where seconds
  per load is a user setting (Tom: "<<user set>> seconds per load"); the formula
  uses only the pre-search reservation count, never a candidate or final truck count.
- Charged phases: methodA, methodB, pricing, integer completion, repair, rejected candidates, final validation,
  selection. One ledger per lane-date; slices share its end. The separate shared route-state ledgers of
  Section 5.6 also cover all floor searches; state exhaustion does not extend the wall allowance.
- Production stop is the user-set wall-clock allowance, using a monotonic clock. Reject nonfinite,
  nonpositive or nonrepresentable seconds-per-load and overflow in multiplication/conversion before search;
  zero reservations yield zero allowance and zero dispatch. Deterministic work counts are test/benchmark
  contracts, not a silent replacement for user seconds (ED-09).
- Budget start/end cover the declared quick/careful search, repair, validation and selection interval.
  Reserve finalization work within the same allowance or retain an already fully checked incumbent.
  No timeout authorizes publishing unchecked work or an unbudgeted second validation allowance. Cleanup
  remains safe and actual elapsed/overrun is reported honestly; a measured overrun cannot pass timing.
- Reported: raw elapsed total and per phase, selected nonempty finally validated dispatched trucks (from the selected candidate only), average
  = elapsed / dispatched loaded trucks or N/A when zero, and the maximum per-load time only where the contract records
  per-load times. Average and maximum are separate fields (X-RUN-11).
- The production denominator is the selected nonempty dispatched/loaded truck count. Optional acceptance
  OFF/NotApplicable does not make that count zero. Never derive it by counting `AcceptanceDecision.accepted`
  flags, FULL labels, A/B candidate totals or export-stage decisions. For an enabled synthetic policy,
  dispatched trucks have also passed that policy, but report that scope explicitly.

### 10.2 Outcome classification

| Situation | kind | stop | hasCandidate | finalValidationPassed | service |
|---|---|---|---|---|---|
| No eligible goods | CompletedNoEligibleInput | Completed | false | false | Zero |
| Search completed, no truck passes all rules | CompletedZeroDispatch | Completed | false | false | Zero |
| Search completed with dispatch | CompletedWithDispatch | Completed | true | true | Full or Partial |
| Budget ended, no checked result | TimeoutNoIncumbent | BudgetExhausted | false | false | Zero |
| Budget ended, checked result exists | TimeoutWithIncumbent | BudgetExhausted | true | true | Full or Partial |
| Floor/search cap terminates required search, no checked result | SearchLimitNoIncumbent | FloorSearchStateLimit / FloorRouteStateLimit / IterationCap | false | false | Zero |
| Floor/search cap terminates required search, checked result exists | SearchLimitWithIncumbent | FloorSearchStateLimit / FloorRouteStateLimit / IterationCap | true | true | Full or Partial |
| Bad input | InvalidInput | InputInvalid | false | false | Zero |
| Infeasible with certificate | ProvenInfeasible | Completed | false | false | Zero |
| Malformed M5 settings (e.g. invalid seconds per load or rear-inch limit) | ConfigurationIncomplete | ConfigurationIncomplete | false | false | Zero |

`committed` is a separate flag set by M-INT after the delta is applied.

Search-limit outcomes preserve the checked incumbent and its independently justified proof scope; an
unresolved improving alternative prevents a complete-objective proof. A per-query cap followed by useful
remaining work need not terminate the lane. A purely advisory fullness probe reaching its limit leaves
the mandatory dispatch outcome intact and reports Undetermined diagnostics (U-FL-09).

X-RUN-11: fake clock records ten finally validated dispatched loads, one at 11 s and nine at 1 s: total 20 s, 10 loads, average 2 s,
maximum 11 s; `maxPerLoad <= 10` is false and `average <= 10` is true; the verdict names the contract (lane-date allowance, ED-14), so a maximum above 10 s is reported but the lane passes
if total time is within its allowance.
The formula is a stated setting, not a newly inferred maximum-per-load SLA. X-RUN-12: variants map to rows 1, 2 and 4 of the table; average is N/A in
all three. X-RUN-13: allowance 20 declared before search; A 8, B 7, repair 3, validation 2 = 20; synthetic
comparator selects B (two trucks); delta and totals contain only B; amortised 20 / 2 = 10 s per dispatched load.

### 10.3 Commitment lifecycle

```text
provisional work (candidate-local claims, editor revisions)
  -> validated candidate (validateFinal passed at current revision and profile)
  -> selected result (best validated candidate)
  -> operational commitment (M-INT applies CommitDelta atomically; CommitLedger records the idempotency key)
```

Production contract (ED-10): **keep-validated, atomic per lane-date**. At a stop, the selected result is the best
candidate that passed final validation; it is itself a complete lane-date plan (possibly partial service).
Within one lane-date there is never a partially applied delta. Retry before commit: discard provisional state
(release marks exactly once) and rebuild from the original snapshot. Retry after commit: `applyIfCurrent` returns AlreadyApplied only for the same stable operation key
and identical payload. For a new application, source/pool versions and the current effective ProfileKey,
resource application and idempotency record are checked/updated in one atomic
operation. The same key with different payload is PayloadConflict; a different key based on stale original
inventory is StaleSnapshot; an uncommitted delta built under a different effective capability/configuration
context is StaleContext and consumes nothing. New work starts from `residualAfter` with a new snapshot/version
and the applicable immutable context. A candidate
revision is not an operation identity, so changing A/B selection cannot replay committed goods. Scope this
contract to the actual in-memory batch pipeline; persistent restart recovery requires a durable transaction
only if the operational integration later introduces persistence. The whole-lane-rollback mode exists only in the test seam (FX-COMMIT) so that X-BLK-08
and X-E2E-05 exercise both seam variants without requiring two production modes.

X-RUN-03: a checked candidate exists; the stop arrives during repair of a copy; the copy is dropped; the checked
candidate is returned with `TimeoutWithIncumbent`, still uncommitted until selected and applied. X-BLK-08: block 1
validated, block 2's provisional claims released to the mark; keep variant returns block 1 as a labelled partial
candidate, rollback variant returns none. X-RUN-07 covers before-commit, retained-blocks and after-partial-commit
retries through the same `CommitLedger`.

## 11. Decision register

### 11.1 Confirmed working requirements (from v10 Sections 0.3 and 10)

| Ref | Requirement | Where enforced |
|---|---|---|
| Q7 | Current production uses the run-wide M4 order; AgeFirst remains the default/current customer value. A per-lane override is deferred until agreed | `OrderPolicy` maps the run-wide setting; `orderByLane` remains a synthetic direct-context/future P9 seam |
| Q4 / CL-01 | The minimum-load test (weight > x OR cube > y) runs at export, after M5; M5 withholds nothing for it | Profile `acceptance` = nullopt; optional seam tested synthetically |
| Q8 / Q9 / CL-03 | Full = 98% weight, or rear inches <= limit (default 14, user setting) with stacks as high as allowed, or floor-limited | `FullnessLabel` in M-VAL; report buckets |
| Goals | Ship the highest-priority goods and use all available trucks first; fill best effort | ED-02 comparator |
| Can-go | Can-go = pull-forward (due within 10 days, available); fills after required goods | P10, ED-16 revised |
| Q6 / CL-04 | Careful method whenever the quick result is above a valid minimum; user-set time limit; best found at the limit | ED-13 |
| Q5 | Three real lanes plus the 38-truck reference | Acceptance lane-set manifest |
| v7 0.3 | Physical, eligibility, group and reservation limits apply independently of acceptance | M-VAL conjunctive sections |
| U-SP-02 | Groups separate inside trucks, one shared lane-date pool | M-ALC over `ReservationPool` |
| Q3 | Method choice delegated to the team | ED-Q3 |
| Q1 / CL-08 | Routing threshold is a performance choice; 20 is the fixture value | ED-08 |
| Q9 (part) | Nonstackability must be honoured (stacks built in M2; M5 never stacks items) | M-PHY |
| v7 0.6 | Synthetic fixture policies are never production defaults | Fixtures only in tests; loader never returns them |

### 11.2 Engineering decisions (recorded by the team; not customer questions)

| ID | Decision | Basis | Tests that bind it | Revisit when |
|---|---|---|---|---|
| ED-02 (CL-02) | Production comparator: shipped unit loads per M4 urgency class (rank date, priority, ship date; rank date per the lane's past-due order), lexicographic (can-go goods count in their later classes); then the ED-21 equal-service tie rule; no fewer-trucks preference or synthetic fallback. Penalty excluded until sourced | Recovered from corrected M4 (B6: laneClassifier.cpp:95-135, selectionItem.cpp:57-60/280-294, knapsack.cpp:97-100/239-241) | R-2, R-4, U-RS-03/04/06/07, X-RES-*, X-STATE-07/08, X-E2E-07, U-RB-05 | P7 finds a different rule |
| ED-02b | An unproven priority incumbent may commit if it passes final validation; labelled Unproven | Larger cases may remain unproven within the budget; v9 INV-11 permits it under a declared contract | INV-11, X-RUN-03 | Customer requires proof |
| ED-06 (CL-06) | Penalty rate/unit/source optional in config; missing = unknown (printed "unknown"), excluded from ranking; zero printed as zero | v9 CL-06 | U-RS-02, X-RES-10, X-STATE-02, U-RM-05 | Rate sourced |
| ED-07 (CL-07) | Packing atom is the M4 stack (`SelectionItem`); M5 never splits a stack or adds single-pallet filler; components inside a stack keep identity | M2/M4 design; v9 CL-07 | U-PB-*, X-PAT-*, X-DATE-06, X-DATA-* | M4 atom differs at head (P7/WP0) |
| ED-08 (CL-08) | Routing: reserved trucks per lane-date > T (existing rule 4); T in config; demand-count and per-lane modes rejected; production T chosen from the U-PF-02 benchmark using actual per-lane-date reserved counts and the final timing contract | Q1; REPORTED rule 4 | U-RT-*, X-ROUTE-*, U-PF-02, RD-01/05 | Benchmark evidence |
| ED-09 (CL-09) | Production uses the configured wall-clock secondsPerLoad x reserved allowance; deterministic work-count stops are explicit test/benchmark modes. Completed and deterministic runs use canonical output; wall-clock runs promise validity and honest status, not identical cargo across machine/thread changes | CL-05/09; selector parallelises lane-dates (REPORTED) | INV-12, U-PF-03, X-RUN-01, X-RUN-11/13 | Measurement or stop contract changes |
| ED-10 (CL-10) | Keep-validated, atomic per lane-date commit; idempotency key; retry from original before commit and from reconciled residual after commit; whole-lane rollback only as test seam | v9 CL-10 | X-BLK-08, X-RUN-03/07, X-RES-11, X-E2E-05 | - |
| ED-Q3 | `SequentialAThenB`: A capped at 60% of allowance; count-phase equality stops that same-objective phase only, with service opportunity checked separately; B uses remainder; select the better checked result by comparator; patterns stay open to mixed rebalancing | Q3 delegated; Tom on the review: "good start"; repetitive large lanes (historical 80% single-product) | U-RB-04/05, X-BLK-06, X-STATE-08, X-RUN-04/13 | Benchmark shows B better |
| ED-ARITH | Conservative M4 physical-search lattice is separate from authoritative normalized metrics. Preserve supported precision for final hard limits, optional strict acceptance, fullness and exact cube; checked arithmetic; no nearest-grain threshold rounding | Full-precision contracts; reported float32 provenance requires verification (B9) | U-AC-01/07, X-PAT-13, X-REPORT-04, X-E2E-09 | WP0/WP1 establishes actual numeric representation |
| ED-11 | CLI exit 7 only when an M5 lane ends without a planned result (configuration incomplete, invalid input, timeout or terminating search cap without incumbent); an unproven M5 result counts toward exit 8 like an unproven M4 route; normal completed leftovers are neither. Search-state stops retain their names and are not labelled wall timeouts | Exit 7 escalation and exit 8 provisional today (B14); typed search-limit mapping is an engineering extension | X-RUN-09, U-RP-05, X-PAT-18, U-PF-09 | Tom's pipeline needs a different code |
| ED-15 | Actual unshipped required/carry goods use mapped carry reasons; optional AcceptanceWithheld only when enabled; unselected can-go offers remain at their home date, never re-carried as new ownership; M5NoResult, M5ConfigurationIncomplete, M5InvalidInput do not carry | Tom's carry rule (16/28 Oct); `carriesOver` (B2) | RD-04, U-RS-08, X-DATE-04/05, X-REPORT-07 | - |
| ED-16 (revised) | Can-go fill on M5 lane-dates: M4 pull-forward offers goods due D+1..D+10, available by D, into the M5 lane-date (P10); planned after all goods due by D; each good once across dates | Tom: can-go = pull-forward; use all available trucks | U-CF-01..03, RD-02/04/13, X-DATE-01/12 | - |
| ED-17 / ED-FULL | Fullness labels on final contents/current remaining-demand snapshot; independent weight/rear reasons; floor-limit requires nonempty eligible probe set and proved no-fit in the declared M4 scope; Unknown is explicit; never withhold a truck; Section 9.7 defines buckets | Tom's Q8/Q9 notes and chart | U-FL-01..05, U-RP-07, RD-10/12 | Tom changes the default 14 in |
| ED-18 | Minimum-load acceptance OFF in the production profile; predicate kept as an optional seam with synthetic tests and required productionProfile variants | Tom: export check after the algorithm | U-AC-*, X-REB-04/09, X-E2E-04/09, INV-14 | A customer needs it inside M5 |
| ED-19 | Typed Fits/DoesNotFit/Undecided search; a valid witness proves existence, a negative requires a certificate, and unresolved improving alternatives prevent optimum claims absent an independent applicable certificate. Per-search and shared route-state limits remain inside the parent allowance (Section 5.6, Section 8.1) | Tom's e6dbb0f review and REPORTED f9d9945 fix; the proposed API/accounting details are engineering decisions | R-5a, R-5b, R-8, X-PAT-18, U-PF-09, EC-42/45 | A different supported-layout model or documented budget scope |
| ED-20 | Effective slot cap is a validated positive integer; count checked component unit loads times whole-atom multiplicity. Refine packing classes and model/cache identities by this resource; validate every actual slot/copy/move. Current real slots use the one global M4 cap; heterogeneous direct seams run without a new feed | Tom: 68 is a physical-capacity example, not a global raise; M4 global input is REPORTED (B26); accounting/API details are engineering | R-9, X-PAT-16/17, X-STATE-11, X-CG-21, X-RES-12, EC-43/44/46 | A feed carrying resolved per-truck caps or an explicit override-resolution contract (C9) |
| ED-21 | Equal-service tie rule (resolves OPEN-COMP): T1 shipped unit loads per group in a stable-ID group order rotated by (laneKey mod g + dayNumber mod g) mod g, laneKey = FNV-1a 32-bit of "origin|destination|mode"; T2 canonical records (stable source-line ID, then slot ID). No input order, lane ID, truck count, fill, rear space or weight | Tom, 14 Nov 2026: "any tiebreaker would work as long as it didn't always push to the same single side"; his engineer, 16 Nov: deterministic rotation by lane and date | X-RES-13, X-META-03/05/06, CL-02 tests | Tom asks for a different rule |
| ED-12 | `ExactLimits` (<= 4 slots, <= 60 atoms, <= 6 classes per group) and `scarcityWindow` = 2, configurable | Small oracle fixtures must be exact | R-2, X-REB-09, X-RES-*, X-E2E-07 | Performance |
| ED-13 | Escalation: careful method whenever F > B (valid same-model bound) with allowance left; equal stops; gaps reported in trucks and percent; may be refined from measurements | Q6 answered ("Computer time is cheap. Trucks are expensive") | U-FF-*, X-CG-17, ACC-3 | Measurements |
| ED-14 | Allowance = `secondsPerLoad x reservedTrucks` per lane-date, seconds per load a user setting, declared before search; time reported per truck, group, lane-date and total | Q2 "each load" + "user set"; stated to Tom, applies unless he objects | U-PF-01/05, X-RUN-04/11/13, ACC-2 | Tom objects |

### 11.3 Remaining open items

| Open item | Implement and test now | Where it lives | Dependent tests | Closing evidence | Focused regression |
|---|---|---|---|---|---|
| Tom's exact R-1a/b inputs (R-5a, R-8 and R-9 now have his e6dbb0f files; Saif commits them under tests/fixtures/tomReview/e6dbb0f/) | R-5b stress, exact row-grouping oracle, Saif's reconstructions | Test fixtures only | R-1a, R-1b | Tom's report probes | Those tests, then WP4 suite |
| M4 per-lane past-due setting (P9): DEFERRED until Tom agrees a lane override and a milestone; AgeFirst run-wide stays current (B27) | Run-wide AgeFirst/PriorityFirst and synthetic direct-context isolation; no production per-lane key | Run-wide config mapping; `orderByLane` synthetic/future seam | Only integration override variants are deferred; R-4/direct variants stay applicable | Explicit applicability change, agreed milestone, M4 implementation and fingerprint check | R-7, R-4, X-DATE-08, RD-09 |
| Stated settings (CL-05 formula, 14 in default) | Apply the documented settings unless changed; retain their stated-unless-objection provenance | `secondsPerLoad`, `FullnessRule::rearInchLimit` | U-PF-01/05, U-FL-03, ACC-2, RD-10 | Record the notice/reference and any later change; do not invent explicit approval or an unstated silence deadline | Those tests |
| OPEN-COMP complete allocation tie operator: RESOLVED 14 Nov 2026 as ED-21 | Implement ED-21 in the comparator | ED-02/ED-21, AllocationKey and comparator | X-RES-13, R-2/R-4, U-RS-04, U-RB-05 | X-RES-13 PASS with current evidence | WP5 allocation suite |
| October 1-2 fixtures for Tom's reviewer | Our copies present; names, sizes and SHA-256 listed (B28, m4Fix2Verification.txt A7) | - | Tom's own runs | Delivery to Tom (not sent) | - |

Reports print the fullness rule, export minimum-load note, seconds per load and the effective current run-wide past-due order; identify P9 as deferred rather than implying a supported per-lane override. Bucket/source evidence uses the declared complete residual scope; missing scope is an explicit diagnostic gap, not NoGoodsRemain.

## 12. Difficult-case walkthrough

| Case | Path through the design | What it shows |
|---|---|---|
| X-CG-18 | M-OPT `solveCuttingStock`: LP converged 2.7, restricted integer 4, residual rounding with re-pricing -> 3 = ceil(2.7); evidence lists all four numbers | Integer completion; LP convergence is not proof |
| X-CG-19 | M-OPT acceptance-adapter model with equality demand; exhaustive enumeration -> ProvenInfeasible(accepted/allRequired); capacity bound reported for the capacity-only model; partial variant labelled | Claims scoped by model |
| U-AC-06 | M-VAL runs all sections; acceptance passes but the injected hard violation fails its section; no delta built | Acceptance cannot override |
| U-AC-07 | `CandidateEditor` increments revision; stale fields ignored; `validateFinal` recomputes 3,000 cu ft exactly and rejects | Repair invalidates acceptance and proof |
| U-AC-08 | Same geometry, ProfileKey A/B/A; acceptance recomputed each call; geometry cache allowed | Profile changes re-evaluate |
| X-REB-09 | Method A 30 + 20 -> M-REP rebalance 25 + 25 (donor and recipient rechecked); 49 units -> supply k = 2 empty -> allocation picks 30 | Both sides accepted; no invented unit |
| X-RES-12 | Pool slots carry capability signatures; patterns per signature; acceptance per truck | Trailer limits respected |
| X-REB-07 | Allocation over accepted single-truck options only; B variant: P0-only truck fails y = 8 equality, P11 truck accepted | Priority among eligible accepted alternatives |
| X-DATE-06 / X-DATE-09 | Component-level `Eligibility`; memo keyed by departure date and rules; fresh snapshot per date | Correct snapshot and scope |
| X-RUN-05 | Method B blocks per group produce supplies; DP chooses B's 2 P0 trucks + 19 of A | Early block cannot starve urgent goods |
| X-RUN-11 / 12 / 13 | Budget ledger and outcome table keep total, count, average, maximum, stop and kind separate; only selected trucks counted | Timing and zero outcomes distinct |
| X-RUN-03 / X-BLK-08 | Repair on a copy; release to mark; keep vs rollback at the FX-COMMIT seam; ED-10 production | Stop and retry follow the commit contract |
| U-FL-04 / X-E2E-09 | M-PHY witness 15 wide-wide rows x 2, 600 x 96 in (confirmed by floorPattern, B10); exact cube 30 x 96,000 in^3 = 5000/3 cu ft; physical fit and production dispatch do not depend on x/y. FULL floor-limit additionally needs the nonempty/proved-no-fit context of ED-FULL; synthetic enabled acceptance y = 3,000 withholds and y = 1,000 dispatches | Exact cube, no-fit evidence and optional-policy scope kept separate |

## 13. Integration risks and verification items

| ID | Risk / unknown | Effect | Mitigation |
|---|---|---|---|
| RK-1 | CLOSED: the minimum-load test runs at export after the algorithm (Tom) | - | - |
| RK-11 | Per-lane override P9 is deferred (B27) | Integration override variants are outside the current milestone; direct-context isolation remains testable | Keep run-wide behavior and no-override fingerprints; explicit deferred applicability, never fabricated PASS or an unconditional WP5 prerequisite |
| RK-12 | Can-go fill needs the M4 pull-forward skip removed (P10) | Without it, M5 lane-dates cannot use spare trucks | P10 in WP8 with cross-date reconciliation tests |
| RK-2 | Exact paths/signatures of REPORTED symbols may differ at head | Adapter rework | WP0 inventory before WP1 |
| RK-3 | `columnGeneration` may lack duals/exact pricing | Bound validity | Section 8.3 minimal extension; Farley bound with exact pricing |
| RK-4 | Selector worker threads call M5 concurrently | Data races | Reentrant design; only the geometry cache is shared, with a mutex; TSan run on Clang in WP7 |
| RK-5 | M4 benchmark is slow on MSVC (planShipDates ~68 s per extract single-threaded, REPORTED) | Long real-data runs | Real-data tests in a separate ctest entry; correctness before timing |
| RK-6 | GCC 16.1 `-Werror=array-bounds` issue in libstdc++ regex (REPORTED) | Strict GCC warning gate unresolved; the reported command-line exception is not a clean pass | Record compiler/options and exception separately; resolve or obtain an explicit gate disposition; do not silently weaken tracked flags |
| RK-7 | Per-truck trailer capability does not exist in inputs (B4) although a 16 Oct client rule mentions it | X-RES-12, X-E2E-06 seam-only; U-DA-05 BLOCKED | Raise with Tom and Saif (C9) |
| RK-9 | M4's time goes to small knapsack lanes at the node limit, not to the large lane (B12) | Routing threshold and per-load timing need real lane timings | U-PF-02 benchmark records per-lane times; report to Tom with CL-08 evidence |
| RK-10 | Client-facing text called 154 "your unused"; the repo's client-stated figure is 195 on a different basis (B11) | Mislabelled comparison | Correct in the next preview revision: our 98 vs harness 154 on the same in-window basis; 195 is all placeholder trucks |
| RK-8 | Pending customer answers change expected results | Rework | Isolated in profile/policy objects; focused regression lists (11.3) |

## 14. Completion check (this phase)

- Existing (REPORTED/HANDOVER) and new (PROPOSED) symbols are separated (Section 2).
- M4 packers, checks, floor patterns, converters and date rules are reused through adapters; no second
  normalization, packing engine or column generation.
- Each module has an interface, ownership, dependencies and failure behaviour (Section 6); the graph is acyclic
  and M5 never re-enters selector routing (Section 4.2).
- Groups and blocks cannot bypass the allocation step (Section 7.3).
- Candidate construction, validation, selection and commitment are separate (Sections 7, 10.3).
- Solver claims carry model, objective and layout scope (Section 8).
- The manifest maps every named ID in the current v9 specification once (`m5TestTraceability.md` Section 1); the machine audit checks counts and variants.
- Pending answers live in named profile fields and policy interfaces with focused regression sets (11.3).

## 15. Revision 3 changes (11 November 2026)

- Minimum-load acceptance moved out of M5 (export check after the algorithm): ED-18, C8 closed, RK-1 closed,
  M-POL/M-VAL changed to an optional policy; profile gains `FullnessRule`, `secondsPerLoad`, `orderByLane`,
  `canGoFill`.
- Fullness labels by rear inches / 98% weight / floor-limited (ED-17, CL-03).
- Q6 answered (ED-13), timing contract stated (ED-14), Q5 closed (C5), goal order applied to the comparator
  (ED-02: no fewer-trucks preference), Method policy confirmed as a good start (ED-Q3).
- Can-go fill on M5 lane-dates (ED-16 revised, P10); linked-date evaluation (B20, RD-13).
- M4 baseline moved to e51d9c4 (B16-B22); new prerequisites P9 (per-lane order) and P10 (pull-forward into
  M5 lane-dates).
- Test specification v6: 273 IDs (new R-5a, R-5b, R-6, R-7, U-FL-01..05, U-CF-01..03, U-PF-05, RD-13).



## 16. Revision 4 review corrections (11 November 2026)

This is a documentation review. No application build, runtime test, original Tom response or source commit
was independently executed/inspected from this bundle. Reported historical evidence remains attributed.

- Removed active v5-era Q2/Q6/Q9/x/y blockers and the obsolete no-can-go design path. Optional acceptance
  is explicitly guarded in Method A, Method B, validation and repair; production OFF never withholds for it.
- Completed the proposed comparison data: full urgency tuple, authoritative rear/weight metrics, nonempty
  truck count, canonical records and revision. OPEN-COMP records the unresolved complete tie operator rather
  than silently claiming a closed comparator or using an additive key unsupported by the DP.
- Kept Q6's same-objective count equality stop while separating lane-service opportunity and proof scope.
  Added residual no-progress protection and bounded integer-completion fallbacks.
- Separated authoritative comparison precision from M4's conservative physical search grains. Added checked
  duration arithmetic and production wall-clock budgeting; deterministic work mode is explicit in tests.
- Defined ED-FULL: non-vacuous no-fit evidence in the supported layout model, Unknown on incomplete probing,
  final-demand buckets, independent reasons and label invalidation. This is a recorded engineering contract.
- Made delta application/version checking/idempotency recording atomic and payload-sensitive. Stale snapshots
  and same-key/different-payload retries cannot consume inventory twice.
- Corrected repair failure return: only a previously or newly fully validated result may escape.

New regression references and audited counts are maintained in the v7 test specification and manifest.


### 16.1 Added regression coverage in v7

| IDs | Review risk covered | Architecture owner |
|---|---|---|
| U-AC-09 | Strict predicates retain supported sub-0.001 precision | M-POL, ED-ARITH |
| U-FL-06..10 | Fullness boundaries, upstream legal stacking, final residual buckets, unknown no-fit, farthest extent | M-VAL/M-PHY, ED-FULL |
| U-CF-04..08 | Global required precedence, existing-date optional service, committed linked-date draws, exact spare fill, impossible required cargo | M-INT/M-LED/M-ALC, ED-16 |
| U-PF-06..08 | Checked allowance arithmetic, parallel wall deadlines, separate lane-date budgets | M-POL/M-ORC, ED-09/14 |
| X-CG-20 | Minimum-count bound cannot certify the partial-service priority objective | M-ORC/M-OPT, Section 7.6 |
| X-STATE-09 | Optional acceptance ON/OFF/ON cannot leak through either method | M-POL/M-ORC/M-VAL |
| X-STATE-10 | Atomic application, stale snapshot and conflicting retry payload | M-COM/M-INT |
| X-REPORT-10 | Weighted bucket means and overlapping fullness reasons | M-RPT |

The 18 additions bring v7 to 291 named IDs. Existing IDs are preserved. EC-33..41 record the new
coverage obligations, bringing the EC checklist to 41; the 24 invariants and 10 CL IDs are retained.


### 16.2 Final fixture/interface alignment

- U-FL-06/10 use authoritative exact witness coordinates and a supplied-witness validation/measurement seam;
  no implicit compaction or replacement of a translated fixture is allowed.
- Mandatory dispatch validation and advisory no-fit diagnostics have separate completeness. U-FL-09 may
  dispatch a checked truck with an Undetermined label; incomplete mandatory checks still block a new result.
- RemainingDemandView includes unoffered excluded source rows/reasons and final selected draws, enabling the
  U-FL-08 GoodsRemain/NoGoodsRemain/RuleLimited distinction. Missing data is not an empty inventory proof.
- Production timing/report denominators count selected nonempty dispatched trucks, including acceptance OFF.
- U-CF-05 uses its one indivisible FX-BLOCK atom on an existing reserved date; it does not force spreading cargo
  over otherwise unused trucks. U-CF-06 controls selection of A/B to check commit deltas; it does not assert
  an unverified global preference between linked-date plans with equal eventual service.


## 17. Revision 5 changes (14 November 2026)

This is a documentation revision after Tom's review of M4 at e6dbb0f (12 November) and the M4 verification at f9d9945
(m4Fix2Verification.txt, REPORTED; e6dbb0f itself was not available and e51d9c4 stood in for it). No M5 code was written or run.

- Baseline moved from e51d9c4 to f9d9945 (B23): tests, fingerprints, unproven counts, cube, timings and the unchanged hand-over.
- New baseline findings B24-B30: row grouping status and limit, proof labelling on a heuristic refusal, the unit-load cap as configuration,
  the per-lane past-due status (deferred), the October fixtures, the slower M4 plan, and two lanes shipping 1-2 unit loads fewer.
- M5 rules added: R-PROOF and R-GROUP (Section 8.1; ED-19) and the unit-load cap per slot (Section 6.4, 5.3; ED-20). AgeFirst stays the
  run-wide default; the per-lane override stays an M4 prerequisite (P9) with an agreed milestone still to be set.
- Reuse-table rows updated with f9d9945 file:line facts for `arrangeFloor`, `candidateRows`/`loadGrowthCheck`, `selectForTruck`/`TruckFit`
  and the row checks formerly `truckCheck` (P2 names changed).
- Tests v8: R-5a with Tom's exact inputs, R-5b extended, R-7 deferral, new R-8 and R-9, EC-42/43; 293 named tests, 43 EC rows.


## 18. Revision 6 review (14 November 2026)

This review changes documents and conceptual interfaces only. It does not verify repository code, Tom's
raw probe attachments, f9d9945 measurements or application results. All M4 figures in Sections 2.1-2.5
retain their REPORTED evidence level. In particular, the GCC warning exception remains unresolved and
B30's linked-date service declines remain recorded bugs to investigate; the baseline update does not close them.

- The physical adapter returns Fits, DoesNotFit or Undecided with evidence. Pure supplied-witness validation
  remains separate and does not search or compact. A positive witness proves existence; optimization proof
  still requires the named objective's certificate and honest treatment of unresolved improving alternatives.
- Per-search floor-state caps debit one owned route ledger, also bounded by the original lane-date wall/work
  allowance. Method/block/repair/probe changes cannot reset it; no null-budget bypass or negative Unknown cache.
- Unit-load capacity counts components and whole copies. Cap-aware packing classes, generated columns,
  actual slot assignment, repairs, final validation and cache/model identities all carry that resource.
  Current global production input and heterogeneous direct-seam capabilities are distinguished explicitly.
- P9 stays deferred in current requirements, loader behavior, risks and acceptance applicability. Direct
  profile-isolation tests continue now; OPEN-COMP and genuinely missing current evidence remain visible.
- Tests v9 adds X-PAT-16, X-PAT-17, X-STATE-11, X-CG-21, X-PAT-18 and U-PF-09. The package has 299 named
  tests, 24 invariants, 46 EC items and 10 CL rules; the manifest is authoritative for variants/applicability.


## 19. Revision 7 changes (14 November 2026)

Documents only. No repository code was read or changed and no application test was run.

- Tom's 14 Nov reply to Saif's M5 progress note answered two engineering questions.
- OPEN-COMP is resolved as ED-21. Tom accepts any tie rule "as long as it didn't always push to the same
  single side". A fixed canonical order would always favour the same group, so T1 rotates the group order
  by ship date; T2 uses canonical records. Fill, rear space, weight and truck count never break the tie.
  ED-02, the comparator text (Sections 6 and 7.3) and the open-items table are updated.
- C9 is recorded. Refusing a lane-date with mixed trailers is acceptable for now. A truck-type mix chosen
  from an availability list is not in the current inputs and is out of scope for M5.
- Tests v10 adds X-RES-13 (WP5) and EC-47: 300 named tests, 24 invariants, 47 EC items and 10 CL rules.


## 20. Revision 8 changes (17 November 2026)

Documents only. No repository code was read or changed and no application test was run.

- Tom's 16 Nov email (his engineer's review) sent the exact R-1a, R-1b and R-5a inputs. R-1a and R-1b
  are runnable; their files and SHA-256 are in tests v11 Section 40. R-1b uses a 100 x 100 in floor.
- ED-21 now rotates the group order by lane and date: laneKey is the 32-bit FNV-1a hash of
  "origin|destination|mode", and r = (laneKey mod g + dayNumber mod g) mod g. The engineer asked for tests
  of replay, input-order independence and variation across lane-dates (X-RES-13, including acrossLanes).
  A copied lane may now break a tie differently (X-META-06).
- C9 extended with the engineer's conditions for a future mixed fleet. A single alternate trailer type per
  run is in scope: U-DA-07 tests a 40 ft high-cube container. Its payload and unit-load limit are still
  needed, and a profile without them is a configuration error, never a silent 53F default.
- Tests v11: 301 named tests, 522 variants (515 NOT_RUN, 7 BLOCKED), 24 invariants, 48 EC items, 10 CL rules.


## 21. Revision 9 test coverage update (17 November 2026)

No new production feature is mandated by the 162-case audit. Test specification v12 has 304 named tests and 557 variants. Three named synthetic tests verify M4's literal 33.3-in row candidate threshold without a false global no-fit proof (X-PAT-19), finite pricing with zero-cost duplicate columns (X-CG-22), and repair-cycle termination with best checked incumbent preservation (X-REB-10).

New variants exercise physical equality and component cap counting; ED-21 tie rotation with ineligible groups, unsigned FNV-1a, duplicate candidates and concurrent retries; and a typed malformed reservation handoff. M5 still consumes typed M1/M4 data and has no durable crash/restart store. Future mixed truck-type selection is not an M5 deliverable.

P2 stress variants are conditional and unselected for the current acceptance manifest; real-data requirements and the original Tom fixtures retain their original gates. The reconciliation file maps every P12 proposal.
