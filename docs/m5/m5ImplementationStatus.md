# M5 implementation status

Branch `feature/m5`, cut from `feature/m4` at f9d9945. Implements M5 (large lanes) from the
revision-4 handover package: `m5ModularArchitecture.md`, `m5DeveloperHandover.md` and
`M5_Edge_Cases_and_Tests_v7.md`.

The figures below come from runs on this branch on macOS with Apple clang under
`-Wall -Wextra -Wpedantic -Werror`. The client's MSVC x64 VS2022 `/W4 /WX` build has not been
run on this branch yet.

## M4 prerequisites (WP0)

| ID | What changed | Where |
|---|---|---|
| P2 | The truck check (`floorFits`, `floorVerdict`, and a whole-load `checkTruckLoad`) is extracted, so the selector and M5 share one implementation | `include/truckCheck.hpp`, `src/truckCheck/` |
| P3 | The selector gathers each escalated lane-date (every route and group, the reservation, `methodBeforeEscalation`, the lane's past-due order) into a `LargeLaneRequest` | `include/largeLanePlanner.hpp`, `BundleSolver::solveLargeLane` |
| P9 | Optional `selection.pastDueOrderByLane`. Each `LaneRoute` carries its lane's order, and the classifier and selector rank by it. With no overrides, plans are unchanged | `laneClassifier`, `selector`, `selectionParams` |
| P10 | Pull-forward offers can-go goods to a lane-date above `m5Threshold` only when an M5 planner is registered. Goods offered and not loaded go back to their own date, as M4's do | `shipDatePlanner::offerPulledForward` |
| ED-15 | `AcceptanceWithheld` carries forward. `M5NoResult`, `M5ConfigurationIncomplete` and `M5InvalidInput` do not; they count as shortfalls (exit 7) | `shipDatePlanner::carriesOver`, `selectionShortfalls` |
| - | `SelectionItem::lineUnitLoads` keeps each level's share of its line, so M5's components carry exact quantities | `selectionItem` |

With no `m5` block in the params file, all 1,165 M4 tests pass unchanged. With the shipped
params, the only M4 test whose expectation changed is the one that asserted escalation through
those params. It is now two cases: escalation without a planner, and the same lane planned by M5.

## Modules (`namespace ob::m5`)

| Architecture | Files | Notes |
|---|---|---|
| M-POL | `m5Policy` | Profile loader with validation domains. An absent `acceptance` block means OFF (the production setting). Routing modes other than reserved trucks per lane-date are rejected. Lane allowance is checked as secondsPerLoad x reserved trucks |
| M-ORC budget | `m5Budget` | A frozen allowance on a monotonic clock, or a work count. Slices never extend the deadline. Per-load time summary (X-RUN-11) |
| M-LED | `m5Ledger` | Input checks (non-finite, negative, priority domain, dates, conflicting duplicate sources, slots), per-component eligibility, the final residual view, per-line conservation |
| M-PHY | `m5Physical` | `checkTruck` on each slot's trailer through the shared truck check, single-class pattern size, a thread-safe geometry cache, urgency-ordered packing, careful block packing (M4 `binPacker`, then the truck check) |
| M-OPT | `m5Solver` | L1 and count bounds per group, FFD, column generation on M4's `solveCoveringLp`/`PatternPricer` (Farley bound only with exact pricing), residual rounding with re-pricing, an exact branch and bound, restricted-master integer optimum, FX-GAP, and the acceptance-adapter model |
| M-MA / M-MB | `m5Methods` | Method A (repeated patterns, then the remainder) and method B (rolling blocks, never padded). Both supply each group a plan for every k |
| M-ALC | `m5Allocation` | Urgency domain, additive keys, ED-02 comparator and the synthetic FX-PRIORITY comparator, and an exact DP over groups and slots |
| M-VAL | `m5Validation` | Mandatory sections (physical, conservation, eligibility, groups, reservations, optional acceptance, metadata), plus ED-FULL fullness labels and buckets |
| M-REP | `m5Repair` | `CandidateEditor` (raises the revision, clears validation and proof). Acceptance rebalance is guarded by the optional policy and works on a copy |
| M-COM | `m5Commit` | The delta is built only from a current validated plan. `CommitLedger` applies atomically and idempotently and refuses stale snapshots and payload conflicts. `residualAfter` builds the post-commit snapshot |
| M-ORC | `m5Orchestrator` | Snapshot, budget, A, then B by Q6 (count above the same-goods bound, or goods left), allocate, validate, repair once, select, label, outcome |
| M-RPT | `m5Report` | `M5ReportData` and plain-ASCII sections 3A-3E, appended to the selection report |
| M-INT | `m5Integration` | `M5LargeLanePlanner` (reentrant; exceptions become `M5NoResult`), `gatherLaneDate`, and registration from the params file |

## Decisions recorded in code

- **ED-ARITH.** The doubles M4 hands over are the authoritative normalized values. Thresholds
  are compared exactly, never after rounding to M4's search grain. Cube is compared as cubic
  inches against y x 1728.
- **ED-02 / OPEN-COMP.** Shipped unit loads are compared class by class, most urgent first,
  with each class being M4's (rank date, priority, ship date). Equal service is broken by
  canonical membership. There is no fewer-trucks preference, and no invented fill operator.
  The report prints OPEN-COMP as open.
- **Q6.** Method B runs after A when A's trucks exceed the same-goods L1 bound, or when A left
  eligible goods. Equality skips B and the reason is recorded. A count proof is attached to a
  plan only when that plan ships every eligible good, so a bound for different goods is never
  attached. The service proof (every fitting eligible good shipped) is reported separately.
- **ED-FULL.** Floor-limited needs a nonempty set of eligible same-group probes, each of which
  fails to fit with a complete check. An interrupted check gives Unknown, and the label is then
  Undetermined. Buckets come from the final residual over the declared scope: offered goods,
  plus that group's goods not offered because they are due later or not yet available.
- **C9.** Real inputs give every slot the run's trailer. A pool with different trailers is
  rejected as invalid input rather than planned wrongly.

## Real data (shipped params, 1 s per load)

7 Aug, 2028 -> 2574 TL (Aug5 extract): 21 reserved, three groups.

- The required hand-over is 747 stacks and 763 unit loads, which reproduces RD-02.
- M5 dispatches 21 trucks carrying 697 unit loads, 42 of them can-go. Method A was selected.
- On the hand-over alone, A and B each ship 665 unit loads (U-PF-02); M4 escalates the lane.
- The labelled both-dates benchmark (7 and 8 Aug, 21 + 20 reserved) uses 41 trucks. The client's
  historical 38 is a comparison, not a target. Nothing from the benchmark is committed.
- Aug6, 2028 -> 2190, both dates joined: 23 trucks, against the historical 23 (RD-07).
- Reservations reconcile on all three runs: 324 = 296 + 28, 532 = 510 + 22 and 584 = 557 + 27
  (reserved = built + unused, RD-11).

Lanes M5 does not touch plan identically with and without M5 (RD-09 / INV-16). Aug4 and Aug6
have no lane-date above 20.

## Tests

| ctest entry | What it runs | Result on this branch (clang, macOS) |
|---|---|---|
| `ob_tests` | Everything not skipped | 1,359 of 1,359 pass |
| `m5Core` | `ob_tests -ts=m5`, synthetic | 193 cases pass |
| `m5RealData` | The August extracts | 15 cases pass |
| `m5Published` | PUB-01..03 on OR-Library `binpack1.txt` | 48 / 49 / 46 bins, checked independently |
| `m4Benchmark`, `m4BenchmarkThreadsSuccess` | The M4 benchmark, and its thread test under `--success` (U-PF-04) | 18 cases pass (906 s); thread test passes (795 s) |

`binpack1.txt` was downloaded from OR-Library on 13 Nov 2026 and is committed under
`tests/m5/data/orlib/` with its SHA-256 (`891fe4b3...b172`) in `README.txt` there.

**Coverage and evidence gate (X-RUN-09).** Since Revision 9.2 the gate works per variant against the
v12 manifest (`docs/m5/m5TestManifest.json`, 304 tests, 557 variants); the v7 ID list was removed.
Each run writes an evidence record per declared `testId/variant` into the build folder, and M5 is
accepted only when every selected variant has one current PASS record from a clean build
(`docs/m5/m5TestTraceability.md` Section 11.2). An ID-level check still requires every manifest ID
to have a test or a line in `m5CoverageExceptions.txt` with its status and reason.

Independent oracles do not reuse the code under test: brute-force bin packing, partition
enumeration, subset enumeration over M4's floor for R-5b, an invariant recount with its own floor
witness check (`tests/m5/support/m5LayoutChecker.hpp`, since Revision 9.2), and per-source
conservation.

### Still open

- **R-1a, R-1b, R-5a:** Tom's exact inputs are committed (`tests/fixtures/tomReview/m5ExactInputs/`);
  the tests are planned for the next pull request. The M5 path runs an exact knapsack for pools of up
  to 4 slots (ED-12), and R-5b (seeded single trucks against brute force) passes.
- **C9:** reservations carry no per-truck trailer, so a pool of different trailers is refused
  (X-RES-12 / X-E2E-06). The adapter seam itself is tested (U-DA-05).
- **OPEN-COMP:** the tie operator for equal service needs your sign-off.
- **Builds:** MSVC x64 `/W4 /WX`, GCC, and the ThreadSanitizer run have not been run on this branch.
