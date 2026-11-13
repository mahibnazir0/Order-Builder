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
- M5 dispatches 21 trucks carrying 697 unit loads, 42 of them can-go.
- Method A was selected over B.
- The M5 work took 0.05 s.

Lanes M5 does not touch plan identically with and without M5 (RD-09 / INV-16). Aug4 and Aug6
have no lane-date above 20.

## Tests

- `ctest -R m5Core` (`ob_tests -ts=m5`): 96 synthetic cases.
- `ctest -R m5RealData`: 6 cases on the August extracts.
- `ctest -R m5Published`: PUB-01..03.

Together they cover 119 of the 291 v7 IDs, including:

- all X-META-01 multisets (1,716) checked against an independent brute force;
- X-CG-18, with LP 2.7 converged, restricted integer 4, incumbent 3 and bound 3;
- X-CG-19, X-REB-09, R-2, R-4, R-6, X-RUN-05, X-E2E-07, U-FL-01/02/04/08/09, U-CF-01..05/08,
  X-RUN-02/04/11/12/13 and X-STATE-10.

### Blocked or not yet covered

- **PUB-01..03 (ACC-1)** need OR-Library's `binpack1.txt` committed under
  `tests/m5/data/orlib/`, with its SHA-256 recorded. `m5Published` fails by name until then.
- **R-1a, R-1b, R-5a** wait for Tom's exact inputs.
- **U-DA-05 and the X-RES-12 input variant** are blocked by the C9 capability gap.
- **ACC-2's both-dates benchmark, U-PF-02 and the 3D threshold rows** are not built. The
  production run never commits benchmark rows.
- **The manifest/evidence gate (X-RUN-09) and per-variant evidence records** are not built.
- **The remaining named IDs** have no test yet. These include most of X-DATA, the X-DATE
  carry/pull integration cases, X-BLK-01/04..08, X-RES-02..05/07..12, the X-REPORT checks
  on real data, and U-PF-03/07 thread checks.
