# M5 Test Traceability

Status: Revision 9 (17 Nov 2026). Generated from `docs/m5/m5TestManifest.json` (schemaVersion 2).
Requirement source: `M5_Edge_Cases_and_Tests_v12.md` (17 November 2026). This file maps tests; it does not replace their definitions.
Read the complete current source case and variant contract before implementing it.

Revision 8: 301 named tests. R-1a/R-1b unblocked with Tom's 16 Nov inputs; ED-21 rotates by lane and date (X-RES-13 acrossLanes); U-DA-07 container test.

Revision 7: 300 named tests. X-RES-13 records Tom's 14 Nov tie rule (ED-21, OPEN-COMP resolved); C9 is accepted for now and a truck-type mix is out of scope.

Revision 6: 299 named tests. All 293 v8 IDs are preserved; six focused cases add layout/proof-state, cap/count/cache and budget coverage. R-7 production integration is conditional and explicitly unselected; direct isolation remains required. R-9 global/direct cap testing is independent of unsupported input fields. RD-13 retains two reported linked-date regressions. No application execution is claimed.

Prior revision 5 added R-8/R-9 and recorded the reported f9d9945 baseline. Prior revision 4 corrected policy/fullness, source binding and evidence contracts. Historical measurements retain their original attribution.
Repository evidence: the document author had no direct repository access. The current review has only the supplied documents; original probe files, application repository and M4 verification reports were not attached. The following are inherited source claims, awaiting reproduction in WP0. Existing paths and symbols marked REPORTED come from
read-only tool reports at commit `7cef94e` on `feature/m4` (m5MissingDataCheck.txt, m5Rev6Check.txt, msvcCleanRun.txt,
5-6 November 2026), m4BaselineForM5.txt (7cef94e, 8 Nov), m4FixVerification.txt (e51d9c4, 11 Nov) and m4Fix2Verification.txt (f9d9945, 14 Nov; Tom's e6dbb0f was not available there and e51d9c4 stood in for it). Paths marked PROPOSED do not exist yet.

No application test has been executed for M5. Every variant starts NOT_RUN, or BLOCKED with a named reason.

## 1. Counts (checked by script against v12)

| Family | v12 count | Manifest count |
|---|---:|---:|
| ACC | 3 | 3 |
| R | 15 | 15 |
| U | 109 | 109 |
| RD | 13 | 13 |
| X | 161 | 161 |
| PUB | 3 | 3 |
| Total | 304 | 304 |

Unique named IDs: 304. Unique `(testId, variant)` keys: 557; duplicates: none. Repeated explanatory headings do not add named tests.

Variants: 557 (552 NOT_RUN, 5 BLOCKED since Revision 9.2; v12 as issued: 550 NOT_RUN, 7 BLOCKED). The 1,716 X-META-01 multisets remain generated instances of one named test.

Invariants: 24. EC checklist rows: 48. CL definitions: 10.

Requirement SHA-256: `2084b1a999bcc3aea276aa9d525b6d2a6c92df435449bbfb51d27c4f40019a53` (v12 with the Section 43 repository notes, Revision 9.2; v12 as issued: `5a6105c61244ed08faa8915d3fd35b74be5768e5a704f9bab3fdebea49e17340`). This identifies the exact Markdown specification, not application execution evidence.

R-7 m4Path/m5Path are conditional integration variants. Revision 9.2 decision D1 (17 Nov 2026) selects them: the per-lane setting is implemented on the M5 branch, off by default, awaiting Tom's confirmation. They are NOT_RUN and their missing evidence blocks the gate like any selected variant. Other conditional variants require their own recorded controlling decision.

Revision 9: v12 reconciles all 162 proposals, adds three named tests and 35 unique variants. See `m5Phase1V11Reconciliation.md` for every case's exact v11 match or accepted addition. Three P2 stress variants remain conditional and unselected.

## 2. Status rules

Three separate axes are recorded for every variant (v12 Sections 0.1 and 26):

| Axis | Values | Rule |
|---|---|---|
| executionStatus | NOT_RUN, PASS, FAIL, BLOCKED | PASS only with current execution at the recorded build. BLOCKED names the missing fixture, rule or capability. Writing a test is not a PASS |
| solver outcome / proof scope | see architecture Section 8 | Recorded in evidence; an intended timeout can PASS its assertions |
| customerAcceptance | NOT_EVALUATED, PENDING, PASS, FAIL | Only real-customer variants can PASS/FAIL. A synthetic PASS never approves missing customer policy |

Requirement values: `required` (a selected mandatory variant; synthetic, core or real-data); `requiredForAcceptance`
(a variant explicitly required only for the customer acceptance manifest); `conditional` (an alternative or capability
variant required only if its recorded controlling decision selects it). A known missing prerequisite is BLOCKED with
a named reason. Required does not mean already implemented or executed. Answered Q6 is an active required rule.

## 3. Codes used in the tables

### 3.1 Modules

| Code | Module | Paths | Status |
|---|---|---|---|
| M-INT | m5Integration | src/m5Integration/, include/m5Integration.hpp | PROPOSED |
| M-POL | m5Policy | src/m5Policy/, include/m5Policy.hpp | PROPOSED |
| M-LED | m5Ledger | src/m5Ledger/, include/m5Ledger.hpp | PROPOSED |
| M-ALC | m5Allocation | src/m5Allocation/, include/m5Allocation.hpp | PROPOSED |
| M-OPT | optimization core (columnGeneration extended + m5Solver) | src/patternPacker/ (columnGeneration, REPORTED) + src/m5Solver/, include/m5Solver.hpp | EXTEND+PROPOSED |
| M-PHY | m5Physical (adapter over floorPattern/stackBuilder/truckCheck/unitLoadMetricsFor) | src/m5Physical/, include/m5Physical.hpp | PROPOSED adapter over REPORTED code |
| M-MA | m5PatternPlanner (Method A) | src/m5PatternPlanner/, include/m5PatternPlanner.hpp | PROPOSED |
| M-MB | m5BlockPlanner (Method B) | src/m5BlockPlanner/, include/m5BlockPlanner.hpp | PROPOSED |
| M-VAL | m5Validation (final validation + fullness labels) | src/m5Validation/, include/m5Validation.hpp | PROPOSED |
| M-REP | m5Repair | src/m5Repair/, include/m5Repair.hpp | PROPOSED |
| M-ORC | m5Orchestrator (budget, scheduling, selection) | src/m5Orchestrator/, include/m5Orchestrator.hpp, include/m5Budget.hpp | PROPOSED |
| M-COM | m5Commit | src/m5Commit/, include/m5Commit.hpp | PROPOSED |
| M-RPT | m5Report (+ selectionReporter rendering) | src/m5Report/, include/m5Report.hpp; selectionReporter (REPORTED) | PROPOSED+EXTEND |
| M-M4 | existing M4 modules (reuse, no M5 logic) | src/selector, src/laneClassifier, src/knapsack, src/binPacker, src/patternPacker, src/floorPattern, src/stackBuilder, src/pipeline.cpp | REPORTED |
| TEST | m5 test support (fixtures, oracles, evidence) | tests/m5/support/ | PROPOSED |

### 3.2 Work packages

| WP | Scope | Tests owned |
|---|---|---:|
| WP0 | Discovery, baseline reproduction, M4 prerequisites | 3 |
| WP1 | Contracts, policy/config, fixtures, test seams, oracles | 21 |
| WP2 | Scalar optimization core: bounds, exact cases, integer completion, acceptance adapter | 32 |
| WP3 | Ledger, eligibility, reservations, physical adapter, final validator | 49 |
| WP4 | Method A, residual packing, repair and rebalance | 39 |
| WP5 | Allocation master and comparator | 24 |
| WP6 | Method B rolling blocks | 9 |
| WP7 | Orchestrator: budget, scheduling, selection, commitment, retry, caches | 43 |
| WP8 | Routing seam, handover gathering, report rendering, integration | 55 |
| WP9 | Generated, metamorphic and published-fixture regression | 12 |
| WP10 | Real-data acceptance and performance | 17 |

### 3.3 Entry points

| Code | Meaning |
|---|---|
| ROUTE | Normal production routing through the selector escalation seam (FX-ROUTE or real config) |
| DIRECT | Direct M5 lane-date seam `m5::planLaneDate` with an explicit context; never routes through M4 selection |
| FORCED | Test-only forced route (`forceRoute`, compiled only with M5_TEST_HOOKS); never raises reservations or bypasses hard constraints |
| CORE | Direct optimization-core or physical-adapter API (scalar or geometry model) |
| VALID | Direct final validator on a supplied candidate (fault injection allowed) |
| POLICY | Policy/config loader and acceptance predicate |
| REPORT | Report renderer from a structured outcome |
| M4 | Existing M4 path (regression of the corrected M4 behaviour) |
| REAL | Pipeline/CLI on real fixtures |
| BENCH | Benchmark harness; isolated from operational commitment |

### 3.4 Oracles

| Code | Independent check |
|---|---|
| BRUTE | Independent brute-force bin packing / feasibility enumerator (tests/m5/support/oracles), not the solver under test |
| LAYOUT-ENUM | Independent exhaustive subset and supported-layout search for declared small physical fixtures (including 6-14 rigid stacks; for 11-20 stacks the exact row-grouping search over every grouping into M4 candidate rows, by dynamic programming over subsets or, for few footprints, over counts); compare service objective and independently check the witness. Never infer a 2-D optimum from a scalar bin count; bounded/incomplete searches report no optimality claim |
| CLASSCOUNT | Independent bounded count arithmetic for homogeneous capacity-known groups or whole-truck atoms. X-RUN-05 requires 2 P0 + 19 P11 in 21 slots; U-CF-04 uses independently established FX-30 capacity 30 and required-first group/slot counts, including its 61-unit control. This is not the <=60-unit general enumerator |
| ENUM-ALLOC | Exhaustive enumeration of complete feasible allocations under the case's explicit comparator (FX-PRIORITY or case comparator) |
| ARITH | Hand-computed expected values written in the test (exact arithmetic; current v12 numbers) |
| LAYOUT | Independent layout checker: rectangles inside trailer, no overlap, heights, stacking flags, weight |
| LEDGER | Source-level conservation: per source line/product/group/date shipped + left + carried + rejected = input |
| STATE | Inspection of outcome/state flags, revisions, claims and reservations after injected faults or stops |
| CLOCK | Fake clock / work counter ledger: declared allowance, charged phases, stop reason |
| CONFIGERR | Expected configuration or input error; nothing planned or committed |
| REPORTPARSE | Parse the ASCII report and compare fields with the structured outcome |
| PUBOPT | Published optimum (source statement) plus independent check of the returned full packing |
| BASELINE | Reproduced, recorded real-data baseline with commit/input/master/config identity |
| META | Metamorphic relation checked against an exact solver on small models |
| CODEREVIEW | Structural/link-level check (single implementation, assert direction) |

## 4. Fixture catalog

All values are synthetic test inputs (v12 Sections 0.5, 0.6, 36 and 42). They live only in `tests/m5/support/m5Fixtures.hpp` and
`tests/m5/support/fixtures/*.json`. Production configuration never reads fixture defaults. This customer has the minimum-load policy OFF: an absent
acceptance section is valid and no x/y values are required. An explicitly malformed section is a configuration error.
An ON profile must supply valid thresholds and units; missing values never fall back to synthetic defaults.

| Profile | Exact setting | Builder (PROPOSED) |
|---|---|---|
| FX-30 | TLD, footprint exactly 20 x 100 in, height 108 in final, 1,500 lb final gross, not stackable, no deck/tare added; default trailer 630 x 100 x 108 in, 45,000 lb; 30 units = 600 in, 45,000 lb, 3,750 cu ft (125 cu ft each); defaults one group, available/due D, P0, one lane, ship date D, date-first; OPTIONAL acceptance x = 40,000 lb, y = 3,000 cu ft, strict OR, only where a test selects it; FX-FULL labels | `fx30Units(n, overrides)`, `fx30Context(reservations, overrides)` |
| FX-ROUTE | Reserved trucks per lane and ship date, T = 20 unless the case changes it, route iff reserved > T; other modes rejected | `fxRouteConfig(T)` |
| FX-GAP | Feasible FFD count F, valid same-objective integer bound B, G = 1, trigger iff F - B >= G and positive allowance remains; equality to B never triggers; equals the production rule (Q6 answered) | `fxGapPolicy()` |
| FX-CLOCK | Fixed synthetic allowance or deterministic work count declared before search; all named work shares it; no reset per method/block/repair; report raw elapsed, loaded truck count, average or N/A. Production: secondsPerLoad x reserved trucks | `FakeClock`, `fxClockBudget(units)` |
| FX-PRIORITY | Maximize lexicographic shipped-unit vector P0..P11; then fewer trucks; then lexicographically smallest canonical truck-membership record; penalty excluded unless stated; R-4 uses its own explicit comparison; case comparators override | `FxPriorityComparator` |
| FX-COMMIT | Case selects keep-validated or whole-lane-rollback at the commit seam; isolated candidates; only selected checked result commits; actual partial commit changes retry snapshot to reconciled residual | `fxCommitContract(mode)` |
| FX-COUNT | Scalar/arithmetic models with stated capacities/stock, no dispatch minimum unless introduced, all required items conserved, whole units indivisible | `fxCount(sizes, capacity)` |
| FX-FULL | Rear-inch limit 14 in (default stated to Tom, user setting), weight-full 98%, floor-limited when no same-group eligible stack fits; buckets goods remain / no goods remain / rule-limited | `fxFullRule(limitIn)` |
| FX-CANGO | Pull-forward window 10 days; can-go = goods due D+1..D+10 available by D, offered after all goods due by D | `fxCanGo(window)` |
| FX-BLOCK | Indivisible non-stackable TLD 630 x 100 in footprint, 108 in, 45,000 lb gross, no deck; fills one default trailer; passes x = 40,000 / y = 3,000; one group/lane, available/due D, P0, no carry/pull | `fxBlockAtoms(n)` |
| FX-CAP-ATOM | Already valid rigid T2/T3 atoms, each 48 x 36 x 90 in and 600 lb; T2 contains two 300 lb unit loads, T3 three 200 lb unit loads. Distinct products, exact source quantities, acceptance OFF, one lane/group/P0/date, no carry/pull. Standard 630 x 100 x 108 in / 45,000 lb trailer; every slot cap explicitly set. Never split/restack atoms or infer unit loads from geometry/record count. Full fixture and case overrides in v11 Section 36 | `fxCapAtoms(kind, count, sourceShares)` |

Local overrides that must be preserved exactly (they change the expected result):

- R-2: local y = 2,400 cu ft (x = 40,000); no carry/pull
- R-4a: 30 P11 due D-1 vs 30 P0 due D; carry on
- R-4b: 30 P11 due D-1 vs 30 P0 due D; carry on
- R-4c: 30 P11 due D-1 vs 30 P0 due D; carry on
- U-RT-03: T = 50
- U-RT-04: unsupported mode in config
- U-RT-05: unsupported mode in config
- U-PB-07: stacking asserted through the M2 stack builder (canStack); M5 never stacks items (B5)
- U-RM-05: explicit penalty rate or penalty excluded
- U-AC-06: six single-violation variants + control
- U-AC-07: 25 units, injected removal to 24; interrupt variant
- U-AC-08: profiles A(y=2,400) / B(y=3,000); same-id revision variant
- U-RB-03: block size 0, negative, fractional
- U-FF-04: invalid gaps
- U-RS-02: explicit rates and missing rate; realRate uses Tom's shipped rate and a synthetic override
- U-RS-05: -1, 12, 99, empty, text, 1.5
- X-ROUTE-09: forced benchmark then normal run, same process
- X-CG-19: x = 10, y = 6, strict
- X-PAT-15: stacking asserted through the M2 stack builder (canStack); M5 never stacks items (B5)
- X-REB-04: x = 10, y = 8.4
- X-REB-07: variant A y = 7.9, variant B y = 8; priority-first
- X-REB-09: 50 units / 49 units, two reservations
- X-BLK-07: allowance 100 units
- X-RES-08: case 1 unavailable P0; case 2 oversized P0
- X-RES-10: rates 5, 7, missing, 0
- X-RES-12: 45,000 / 30,000 lb; variant A y = 2,400; variant B y = 3,000
- X-DATE-06: pull off / pull on 10
- X-DATE-08: age-first and priority-first
- X-DATE-09: fresh snapshots for D and D+7
- X-RUN-05: 21 reserved; A 20 x P11 listed first; B 2 x P0; priority-first
- X-E2E-03: pull off / pull on 10
- X-E2E-04: y = 2,000 and y = 2,800
- X-E2E-09: 30-position witness (15 rows x 2); y = 3,000 and y = 1,000

Other local fixtures retained from v6 and specified in current v9 cases: X-BLK-06 and X-E2E-03 (TLD 63 x 100 x 108 in, 4,500 lb; ten per trailer);
X-DATE-06 (TLD 20 x 100 x 50 in, 1,500 lb, stackable two high); X-E2E-04 (378 x 100 x 108 in, 10,000 lb); X-E2E-09
(display TLD 48 x 40 x 50 in, 800 lb, not stackable; 30-position witness 15 rows x 2, 600 x 96 in); X-CG-19 and X-REB-04/07
(scalar acceptance adapter with explicit x/y); U-FL-02 (light 20 x 100 in stacks, 300 lb); R-6 (60 x 60 and 48 x 40 stacks); R-8 (Tom's eleven stacks, 1,000 lb, height 50 in, P100..P110) and R-9 (34 two-high 48 x 36 stacks, height 100 in, 1,000 lb), from Tom's e6dbb0f files.

## 5. Complete test map

Rows are derived from the manifest. Exact source hash/section identify the case. Variant expectations and scoped overrides specialize it without silently relaxing a hard constraint. This is coverage intent, never application PASS evidence.

P = priority; Own = owner plus collaborators; INV/EC = checks. Dependencies in this table apply at parent scope; a variant may declare additional scoped dependencies. `runnableAfter` is recorded in the JSON.

### Current requirements Section 1

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| ACC-1 | unstated | Column generation on standard cutting-stock cases with a known best answer | M-OPT / TEST | WP2 | FX-COUNT + PUB binpack1.txt | CORE | PUBOPT + BRUTE | 01 10 18 19 | - | - | scalarSuite (required, NOT_RUN) |
| ACC-2 | unstated | The 38-truck lane, 2028 -> 2574 TL, run 5 Aug | M-ORC / M-INT M-RPT M-VAL | WP10 | Real August fixtures (Aug5) + REAL_CUSTOMER profile | REAL + BENCH | BASELINE + LEDGER + LAYOUT | 01 02 03 04 05 07 08 09 12 13 14 16 17 18 20 | - | CL-01, CL-02, CL-03, CL-05, CL-08, CL-10, Q2 (answered), Q5 (answered), Q9 (answered) | production7Aug (required, NOT_RUN); bothDatesBenchmark (required, NOT_RUN); syntheticDiagnostic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| ACC-3 | unstated | Hand-over from first-fit-decreasing | M-ORC / M-OPT M-POL M-RPT | WP7 | FX-GAP (G=1, >=) | DIRECT | ARITH + STATE | 10 15 19 23 24 | - | CL-04, Q6 (answered) | fxGap (required, NOT_RUN); approvedPolicy (required, NOT_RUN) |
### Current requirements Section 3

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| R-1a | unstated | One 630 x 100 x 108 in trailer (45,000 lb, cap 64): 20 A-LARGE items 98.75 x 40 in and 20 B-SMALL items 48 x 40 in, each 50 in high, 1,000 lb and one unit load; fixed prebuilt stacks, floor rotation allowed, all P0, ship 6 Aug 2026, one reserved truck (Tom's case; exact file R-1a.json, 16 Nov) | M-MA / M-OPT M-VAL M-M4 | WP4 | Tom's exact inputs (16 Nov): tests/fixtures/tomReview/m5ExactInputs/R-1a.json (SHA-256 fc58a9a4c1912b2d88b9a7cf28ee1bdaf5f0a1b1bcea3be2b72e65b47effd052); override: Use Tom's exact file and the independently checked 25-unit witness; do not run the <=14-stack exhaustive oracle on the 40-item case or infer global optimality from the witness alone. | M4 + DIRECT | LAYOUT + LEDGER + ARITH | 01 02 03 04 09 10 18 19 20 | - | SOURCE:Tom exact R-1 inputs (received 16 Nov 2026; v11 Section 40) | m4Path (required, NOT_RUN); m5Path (required, NOT_RUN) |
| R-1b | unstated | Tom's small four-item case (exact file R-1b.json, 16 Nov): trailer FLOOR 100 x 100 in, not 630 x 100; height 108 in, 45,000 lb, cap 64. Two 60 x 60 and two 50 x 50 items, each 50 in high, 100 lb and one unit load; rotation allowed, all P0, one reserved truck | M-MA / M-OPT M-VAL M-M4 | WP4 | Tom's exact inputs (16 Nov): tests/fixtures/tomReview/m5ExactInputs/R-1b.json (SHA-256 40958f86d06dc40ca6d2e3b792b20c12b8bad2ac00ef551d70cc2f01470c0b8e); override: Trailer floor 100 x 100 in for this run (from the file) | M4 + DIRECT | LAYOUT-ENUM + LAYOUT + LEDGER | 01 02 03 04 09 10 18 19 20 | - | SOURCE:Tom exact R-1 inputs (received 16 Nov 2026; v11 Section 40) | m4Path (required, NOT_RUN); m5Path (required, NOT_RUN) |
| R-1c | unstated | Any case where floor repair removes goods | M-REP / M-VAL M-OPT | WP4 | FX-30 + injected repair | DIRECT | STATE + LEDGER | 01 10 15 18 19 23 24 | 11 | - | synthetic (required, NOT_RUN); invalidDonorRestore (required, NOT_RUN) |
| R-2 | unstated | One reserved truck. Group A: 1 unit at P0 + 29 at P11. Group B: 20 at P0. Groups may not mix. Runnable fixture: FX-30... | M-ALC / M-VAL M-POL | WP5 | FX-30; override: local y = 2,400 cu ft (x = 40,000); no carry/pull | DIRECT | ENUM-ALLOC | 01 05 06 11 14 18 | 12 | CL-01, CL-02, CL-03 | synthetic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| R-3a | unstated | Both date rules off. An Aug 6 line and an Aug 8 line that could stack together. Window Aug 6-7 | M-LED / M-VAL M-M4 | WP3 | FX-30 + local two-date stack | DIRECT | STATE + LEDGER | 01 07 08 18 | 13 | - | synthetic (required, NOT_RUN) |
| R-3b | unstated | Same as R-3a with pull-forward on, limit 10 days | M-LED / M-VAL M-M4 | WP3 | FX-30 + local two-date stack | DIRECT | STATE + LEDGER | 01 07 08 18 | - | - | synthetic (required, NOT_RUN) |
| R-4a | unstated | FX-30 units: 30 overdue P11 due D-1 versus 30 P0 due D, both available D and eligible on D; one group, one reservatio... | M-ALC / M-POL M-LED | WP5 | FX-30 (R-4 explicit comparison); override: 30 P11 due D-1 vs 30 P0 due D; carry on | DIRECT | ENUM-ALLOC | 07 08 11 14 | 14 | CL-02 | synthetic (required, NOT_RUN) |
| R-4b | unstated | Same complete fixture as R-4a; priority-first | M-ALC / M-POL M-LED | WP5 | FX-30 (R-4 explicit comparison); override: 30 P11 due D-1 vs 30 P0 due D; carry on | DIRECT | ENUM-ALLOC | 07 08 11 14 | 14 | CL-02 | synthetic (required, NOT_RUN) |
| R-4c | unstated | Same complete fixture as R-4a; order setting omitted | M-ALC / M-POL M-LED | WP5 | FX-30 (R-4 explicit comparison); override: 30 P11 due D-1 vs 30 P0 due D; carry on | DIRECT | ENUM-ALLOC | 07 08 11 14 | 14 | CL-02, Q7 (answered) | synthetic (required, NOT_RUN) |
| R-5a | unstated | Tom's 14-stack single-truck case on the standard 630 x 100 floor (3da08ab review): 22 unit loads in 616 in were repor... | M-MA / M-PHY M-OPT M-M4 | WP4 | Tom's exact 14-stack inputs (e6dbb0f): exactness-counterexample.json, monotonicity-standard-results.json, proof-standard-results.jsonl (nine distractor variants); also tests/fixtures/tomReview/m5ExactInputs/R-5a.json (SHA-256 2534cf00edaa5980e71f93806f0971ce1d3d4aebed8ac1ceab53579bbe6a2d32) | M4 + DIRECT | LAYOUT-ENUM + LAYOUT + LEDGER | 01 02 03 04 09 10 18 19 20 | 42 | DATA:Tom e6dbb0f review files committed under tests/fixtures/tomReview/e6dbb0f/ (Saif; synthetic) | m4Path (required, NOT_RUN); m5Path (required, NOT_RUN); distractorVariants (required, NOT_RUN) |
| R-5b | unstated | Randomized single-truck cases, fixed seeds, through final selection: 6-10 stacks; 11-16 stacks (the region where M4 j... | M-PHY / M-MA M-OPT M-M4 TEST | WP9 | Random single-truck stacks, fixed seeds; exact row-grouping oracle; failures_old (7cef94e) and failures_e51d9c4 regressions | M4 + DIRECT | LAYOUT-ENUM + LAYOUT + LEDGER | 02 03 04 09 10 19 20 | 42 | - | m4Path (required, NOT_RUN); m5Path (required, NOT_RUN); groupingRegion11to16 (required, NOT_RUN); fewFootprints14to20 (required, NOT_RUN) |
| R-6 | unstated | One truck, two do-not-mix groups, all priority 0. Group A: twelve 60 x 60 stacks, only ten fit. Group B: eleven 48 x ... | M-ALC / M-PHY M-M4 | WP5 | Local: group A 12 x 60x60 (10 fit), group B 11 x 48x40, one truck, all P0 | M4 + DIRECT | ENUM-ALLOC + LAYOUT-ENUM + LAYOUT | 02 03 04 05 06 09 11 20 | - | CL-02 | m4Path (required, NOT_RUN); m5Path (required, NOT_RUN) |
| R-7 | unstated | Two independent FX-30 lanes, each one direct-seam slot, 30 P11 due D-1 and 30 P0 due D, all available D, carry ON; Ag... | M-INT / M-POL M-M4 | WP8 | Two lanes, FX-30, one lane switched to PriorityFirst | M4 + DIRECT | STATE | 11 12 13 | - | - | m4Path (conditional, NOT_RUN, selected by D1); m5Path (conditional, NOT_RUN, selected by D1); overrideABA (required, NOT_RUN); cacheReplay (required, NOT_RUN); removeOverride (required, NOT_RUN) |
| R-8 | unstated | Tom's eleven-item single-truck case (e6dbb0f review): eleven single-unit-load stacks, equal priority and ship date, 1... | M-PHY / M-MA M-OPT M-M4 | WP3 | Tom's eleven items (e6dbb0f): floor-proof-results.json, verified-layout.json; floor_proof_probe.cpp search seed 120112, 15,000 trials | M4 + DIRECT | LAYOUT-ENUM + LAYOUT + LEDGER | 02 03 04 09 10 19 20 | 42 | DATA:Tom e6dbb0f review files committed under tests/fixtures/tomReview/e6dbb0f/ (Saif; synthetic) | m4Path (required, NOT_RUN); m5Path (required, NOT_RUN); probeSearch (required, NOT_RUN) |
| R-9 | unstated | Unit-load cap from configuration (Tom's `capacity_probe.cpp`, e6dbb0f review): 34 two-high stacks of 48 x 36 in (heig... | M-PHY / M-POL M-M4 | WP3 | Tom's 34 two-high 48 x 36 stacks (e6dbb0f): capacity_probe.cpp, capacity-results.json; cap 64 and cap 68 | M4 + DIRECT | ARITH + LAYOUT + LEDGER | 02 03 04 09 20 23 | 43 | DATA:Tom e6dbb0f review files committed under tests/fixtures/tomReview/e6dbb0f/ (Saif; synthetic) | m4Path (required, NOT_RUN); m5Path (required, NOT_RUN) |
### Current requirements Section 4.1

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-RT-01 | unstated | Lane-date with 20 reserved | M-INT / M-M4 M-POL | WP8 | FX-ROUTE | ROUTE | STATE | 05 16 17 | 01 | CL-08 | synthetic (required, NOT_RUN); plannerAbsentFallback (required, NOT_RUN) |
| U-RT-02 | unstated | Lane-date with 21 reserved | M-INT / M-M4 M-POL | WP8 | FX-ROUTE | ROUTE | STATE | 05 16 17 | 02 | CL-08 | synthetic (required, NOT_RUN) |
| U-RT-03 | unstated | Threshold read from config (`m5Threshold`), not hard-coded | M-INT / M-POL M-M4 | WP8 | FX-ROUTE; override: T = 50 | ROUTE | STATE | 16 17 | - | CL-08 | synthetic (required, NOT_RUN) |
| U-RT-04 | unstated | Count mode | M-POL / M-INT | WP1 | FX-ROUTE; override: unsupported mode in config | POLICY | CONFIGERR | 15 23 24 | - | CL-08 | synthetic (required, NOT_RUN) |
| U-RT-05 | unstated | Count scope | M-POL / M-INT | WP1 | FX-ROUTE; override: unsupported mode in config | POLICY | CONFIGERR | 15 23 24 | - | CL-08 | synthetic (required, NOT_RUN) |
| U-RT-06 | unstated | Reserved 0, valid demand present | M-INT / M-M4 M-POL | WP8 | FX-ROUTE | ROUTE | STATE | 05 16 17 | 04 | CL-08 | synthetic (required, NOT_RUN) |
| U-RT-07 | unstated | Lane over threshold but all goods are raw materials / WIP (e.g. SHIP_COND TL_N102) | M-INT / M-M4 | WP8 | FX-ROUTE + raw-material rows | ROUTE | STATE | 01 16 17 18 | 23 | - | synthetic (required, NOT_RUN) |
| U-RT-08 | unstated | Backwards asserts in `testM4Benchmark.cpp` (around lines 552 and 554) | M-M4 / TEST | WP0 | Real August fixtures | BENCH | CODEREVIEW | - | - | CL-08 | existingTest (required, NOT_RUN) |
### Current requirements Section 4.2

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-SP-01 | unstated | Lane with two ship dates | M-INT / M-LED M-M4 | WP8 | FX-30 | ROUTE | STATE + LEDGER | 01 05 06 18 | 03 | - | synthetic (required, NOT_RUN) |
| U-SP-02 | unstated | Lane with two do-not-mix groups | M-INT / M-LED M-M4 | WP8 | FX-30 | ROUTE + DIRECT | STATE + LEDGER | 01 05 06 18 | - | - | synthetic (required, NOT_RUN) |
| U-SP-03 | unstated | Client lane id with underscores (e.g. 1757_1724_1725) | M-INT / M-LED M-M4 | WP8 | FX-30 | ROUTE | STATE + LEDGER | 01 05 06 18 | - | - | synthetic (required, NOT_RUN) |
| U-SP-04 | unstated | Group with a single unit load | M-INT / M-LED M-M4 | WP8 | FX-30 | ROUTE | STATE + LEDGER | 01 05 06 18 | 26 | - | synthetic (required, NOT_RUN) |
| U-SP-05 | unstated | Same-site goods that may mix | M-INT / M-LED M-M4 | WP8 | FX-30 | ROUTE | STATE + LEDGER | 01 05 06 18 | - | - | synthetic (required, NOT_RUN) |
### Current requirements Section 4.3

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-PB-01 | unstated | Product with exactly N full trucks of goods | M-MA / M-PHY M-LED | WP4 | FX-30 | DIRECT | ARITH + LEDGER | 01 02 03 04 09 18 20 | 06 | - | synthetic (required, NOT_RUN) |
| U-PB-02 | unstated | Product with N trucks plus a part truck | M-MA / M-PHY M-LED | WP4 | FX-30 | DIRECT | ARITH + LEDGER | 01 02 03 04 09 18 20 | - | - | synthetic (required, NOT_RUN) |
| U-PB-03 | unstated | Product with less than one truck of goods | M-MA / M-PHY M-LED | WP4 | FX-30 | DIRECT | ARITH + LEDGER | 01 02 03 04 09 18 20 | 07 | - | synthetic (required, NOT_RUN) |
| U-PB-04 | unstated | Pattern uses M4's `floorPattern` | M-PHY / M-M4 | WP3 | Local physical fixtures (M4 pallet types) | CORE | LAYOUT + ARITH | 02 03 04 09 20 | - | CL-07 | synthetic (required, NOT_RUN); rotationOnly (required, NOT_RUN) |
| U-PB-05 | unstated | Footprint rule | M-PHY / M-M4 | WP3 | Local physical fixtures (M4 pallet types) | CORE | LAYOUT + ARITH | 02 03 04 09 20 | - | CL-07 | synthetic (required, NOT_RUN) |
| U-PB-06 | unstated | Pallet deck | M-PHY / M-M4 | WP3 | Local physical fixtures (M4 pallet types) | CORE | LAYOUT + ARITH | 02 03 04 09 20 | - | CL-07 | synthetic (required, NOT_RUN) |
| U-PB-07 | unstated | Stacking | M-PHY / M-M4 | WP3 | Local physical fixtures (M4 pallet types); override: stacking asserted through the M2 stack builder (canStack); M5 never stacks items (B5) | CORE | LAYOUT + ARITH | 02 03 04 09 20 | - | CL-07 | synthetic (required, NOT_RUN) |
| U-PB-08 | unstated | Weight-bound product (Build) | M-MA / M-PHY M-VAL | WP4 | Local physical fixtures | DIRECT | LAYOUT + ARITH | 02 03 04 09 15 20 23 24 | 05 | - | synthetic (required, NOT_RUN); payloadExactAndEpsilon (required, NOT_RUN); simultaneousBinding (required, NOT_RUN) |
| U-PB-09 | unstated | Product taller than the trailer | M-MA / M-PHY M-VAL | WP4 | Local physical fixtures | DIRECT | LAYOUT + ARITH | 02 03 04 09 15 20 23 24 | 18 | - | synthetic (required, NOT_RUN) |
| U-PB-10 | unstated | Single unit load heavier than the trailer limit | M-MA / M-PHY M-VAL | WP4 | Local physical fixtures | DIRECT | LAYOUT + ARITH | 02 03 04 09 15 20 23 24 | 18 | - | synthetic (required, NOT_RUN) |
| U-PB-11 | unstated | Pattern is checked as best on its final contents | M-MA / M-REP M-OPT | WP4 | FX-30 + injected trim | DIRECT | STATE | 10 19 | 11 | - | synthetic (required, NOT_RUN) |
| U-PB-12 | unstated | Real data: 7 Aug, 2028 -> 2574 | M-MA / M-RPT | WP10 | Real Aug5 7 Aug lane-date | REAL | BASELINE + REPORTPARSE | 01 02 03 04 09 18 20 | - | CL-01, CL-03 | configIndependentPatterns (required, NOT_RUN); productionProfile (required, NOT_RUN) |
### Current requirements Section 4.4

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-RM-01 | unstated | Remainder goes to M4's packers | M-MA / M-PHY M-M4 | WP4 | FX-30 | DIRECT | LEDGER + CODEREVIEW | 01 18 | - | - | synthetic (required, NOT_RUN) |
| U-RM-02 | unstated | Remainder is empty | M-MA / M-PHY M-M4 | WP4 | FX-30 | DIRECT | LEDGER + CODEREVIEW | 01 18 | - | - | synthetic (required, NOT_RUN) |
| U-RM-03 | unstated | A truck could be fuller by moving goods between trucks of the same group and date | M-REP / M-VAL M-POL | WP4 | FX-30 | DIRECT | ARITH + LEDGER | 01 02 03 04 09 14 18 20 | 09 | CL-01, CL-02, CL-03 | synthetic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| U-RM-04 | unstated | Fullness and optional minimum acceptance remain independent | M-REP / M-VAL M-POL | WP4 | FX-30; override: Distinct belowFullAccepted and fullButWithheld fixtures; no equivalence between fullness labels and optional acceptance. | DIRECT | ARITH + LEDGER | 01 02 03 04 09 14 18 20 | 09 | CL-01, CL-03 | synthetic (required, NOT_RUN); belowFullAccepted (required, NOT_RUN); fullButWithheld (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| U-RM-05 | unstated | Use usable reservations without inventing cargo | M-ORC / M-ALC M-RPT | WP7 | FX-30; override: explicit penalty rate or penalty excluded | DIRECT | STATE + REPORTPARSE | 05 14 | - | CL-01, CL-06 | synthetic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| U-AC-01 | unstated | Acceptance rule, both limits. Synthetic x = 40,000 lb, y = 3,000 cu ft. Trucks: 40,000 lb / 2,000 cu ft; 40,001 lb / ... | M-POL / TEST | WP1 | FX-30 thresholds | POLICY | ARITH | 14 | - | CL-01 | synthetic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| U-AC-02 | unstated | Rule applies to every truck. A lane with five trucks where the second truck, not the last, fails the rule | M-VAL / M-MA M-POL | WP4 | FX-30 | DIRECT | ARITH + LEDGER | 01 14 18 | 09 | CL-01, CL-03 | synthetic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| U-AC-03 | unstated | Rule applies across do-not-mix groups. Two groups, each with one truck that fails | M-VAL / M-ALC M-POL | WP4 | FX-30, two groups | DIRECT | ARITH + LEDGER | 06 14 | - | CL-01, CL-03 | synthetic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| U-AC-04 | unstated | Read x/y and profile/config identity; change values; test missing, malformed, NaN, infinity and overflow; separate ze... | M-POL / M-RPT | WP1 | FX-30 + malformed profiles | POLICY | CONFIGERR | 14 15 23 24 | - | CL-01 | valuesAndMalformed (required, NOT_RUN); zeroAndRange (conditional, NOT_RUN); productionProfile (required, NOT_RUN) |
| U-AC-05 | unstated | Physically floor-full truck below x/y, under the optional synthetic x/y-only policy | M-VAL / M-PHY M-RPT | WP4 | X-E2E-09 nonstackable display witness (48x40x50in, 800 lb); optional x40000/y3000; production uses 31 source/30 selected U-FL-04 fixture | DIRECT + VALID | LAYOUT + ARITH | 02 03 04 09 14 20 | - | CL-01, CL-03, Q9 (answered) | synthetic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| U-AC-06 | P0 | Acceptance never overrides a hard constraint | M-VAL / M-PHY M-LED M-POL | WP3 | FX-30; override: six single-violation variants + control | VALID | STATE + ARITH | 02 03 04 05 06 07 08 09 14 15 20 23 24 | - | CL-01 | control (required, NOT_RUN); weightLimit40k (required, NOT_RUN); dimension (required, NOT_RUN); unavailable (required, NOT_RUN); dueAfterPullOff (required, NOT_RUN); mixedGroups (required, NOT_RUN); zeroReservations (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| U-AC-07 | P0 | Recompute acceptance after a final repair | M-REP / M-VAL M-COM | WP4 | FX-30; override: 25 units, injected removal to 24; interrupt variant | VALID + DIRECT | STATE + ARITH + LEDGER | 01 10 14 15 18 19 23 24 | 09 11 | CL-01, CL-03 | mutation (required, NOT_RUN); interruptAfterMutation (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| U-AC-08 | P0 | Customer and configuration changes invalidate cached acceptance | M-POL / M-ORC M-PHY | WP7 | FX-30; override: profiles A(y=2,400) / B(y=3,000); same-id revision variant | DIRECT | STATE | 14 16 17 21 | 30 39 | CL-01, CL-03 | ABAinProcess (required, NOT_RUN); freshProcess (required, NOT_RUN); sameIdRevision (required, NOT_RUN); productionProfile (required, NOT_RUN) |
### Current requirements Section 4.5

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-RB-01 | unstated | Build: goods for exactly 38 full trucks, enough reservations | M-MB / M-LED | WP6 | FX-BLOCK | DIRECT | ARITH | 01 05 18 | - | - | synthetic (required, NOT_RUN); thirtyNineBlocks (required, NOT_RUN) |
| U-RB-02 | unstated | Build: goods for exactly 21 full trucks, 21 reserved | M-MB / M-LED | WP6 | FX-BLOCK | DIRECT | ARITH | 01 05 18 | - | - | synthetic (required, NOT_RUN) |
| U-RB-03 | unstated | Block size read from config | M-POL / M-MB | WP1 | FX-BLOCK; override: block size 0, negative, fractional | POLICY | CONFIGERR | 15 23 24 | - | - | synthetic (required, NOT_RUN) |
| U-RB-04 | unstated | A and B run from isolated copies of the same lane input under an explicitly selected method policy | M-ORC / M-MA M-MB M-ALC | WP7 | FX-30 + FX-CLOCK + FX-PRIORITY | DIRECT | STATE + CLOCK | 15 16 17 22 23 24 | - | CL-02, CL-05, Q3 (answered) | synthetic (required, NOT_RUN) |
| U-RB-05 | unstated | A and B use the same number of trucks | M-ALC / M-ORC | WP7 | FX-30 + FX-PRIORITY | DIRECT | ENUM-ALLOC | 11 | 22 | CL-02 | synthetic (required, NOT_RUN) |
### Current requirements Section 4.6

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-CG-01 | unstated | Small cases, up to 10 items, also solved by brute force | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE | 01 10 18 19 | - | - | synthetic (required, NOT_RUN) |
| U-CG-02 | unstated | Standard published cases with a known best answer | M-OPT / TEST | WP2 | PUB binpack1.txt | CORE | PUBOPT | 01 10 18 19 | - | DATA:binpack1.txt committed with SHA-256 | published (required, NOT_RUN) |
| U-CG-03 | unstated | One item type only | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE | 01 10 18 19 | 08 | - | synthetic (required, NOT_RUN) |
| U-CG-04 | unstated | Item exactly the width or length of the trailer | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE | 01 10 18 19 | 17 | - | synthetic (required, NOT_RUN) |
| U-CG-05 | unstated | Item one unit too big | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE | 01 10 18 19 | 17 | - | synthetic (required, NOT_RUN) |
| U-CG-06 | unstated | Iteration cap reached | M-OPT / M-ORC | WP2 | FX-COUNT + FX-CLOCK | CORE | STATE + CLOCK + LEDGER | 01 10 15 18 19 23 24 | 21 | - | synthetic (required, NOT_RUN) |
| U-CG-07 | unstated | Time limit reached, before and after a first result | M-OPT / M-ORC | WP2 | FX-COUNT + FX-CLOCK | CORE | STATE + CLOCK + LEDGER | 01 10 15 18 19 23 24 | 21 | - | synthetic (required, NOT_RUN) |
| U-CG-08 | unstated | Reuses M4's shared `columnGeneration` | M-OPT / M-M4 | WP2 | n/a | CORE | CODEREVIEW | - | - | - | structural (required, NOT_RUN) |
### Current requirements Section 4.7

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-FF-01 | unstated | FX-GAP: F - B exactly G = 1, with positive remaining allowance | M-ORC / M-OPT M-POL M-RPT | WP7 | FX-GAP + FX-CLOCK | DIRECT | ARITH + STATE | 10 19 | - | CL-04, Q6 (answered) | fxGap (required, NOT_RUN); approvedPolicy (required, NOT_RUN); ffdConcreteGap (required, NOT_RUN) |
| U-FF-02 | unstated | FX-GAP: F - B = G + 1, with positive remaining allowance | M-ORC / M-OPT M-POL M-RPT | WP7 | FX-GAP + FX-CLOCK | DIRECT | ARITH + STATE | 10 19 | - | CL-04, Q6 (answered) | fxGap (required, NOT_RUN); approvedPolicy (required, NOT_RUN) |
| U-FF-03 | unstated | FFD already equal to a valid lower bound | M-ORC / M-OPT M-POL M-RPT | WP7 | FX-GAP + FX-CLOCK | DIRECT | ARITH + STATE | 10 19 | - | CL-04, Q6 (answered) | fxGap (required, NOT_RUN); approvedPolicy (required, NOT_RUN) |
| U-FF-04 | unstated | Gap config: positive whole trucks; FX-GAP uses G = 1; invalid 0, negative, fractional, NaN and overflowing values | M-POL | WP1 | FX-GAP; override: invalid gaps | POLICY | CONFIGERR | 15 23 24 | - | CL-04, Q6 (answered) | synthetic (required, NOT_RUN) |
| U-FF-05 | unstated | Report | M-ORC / M-OPT M-POL M-RPT | WP7 | FX-GAP + FX-CLOCK | DIRECT | ARITH + STATE | 10 19 | - | CL-04, Q6 (answered) | fxGap (required, NOT_RUN); approvedPolicy (required, NOT_RUN) |
### Current requirements Section 4.8

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-RS-01 | unstated | Trucks used never exceed reserved, per lane and ship date | M-LED / M-VAL | WP3 | FX-30 | DIRECT | STATE | 05 | - | - | synthetic (required, NOT_RUN); hundredAndOneReserved (required, NOT_RUN) |
| U-RS-02 | unstated | Unused reservations and penalty | M-ORC / M-RPT M-POL | WP7 | FX-30; explicit and missing rates, shipped rate and override | DIRECT + REPORT | ARITH + REPORTPARSE | 05 15 22 23 24 | - | CL-06 | synthetic (required, NOT_RUN); realRate (conditional, NOT_RUN; selected by Tom's CL-06 decision) |
| U-RS-03 | unstated | Fewer trucks than goods | M-ALC / M-VAL | WP5 | FX-30 + FX-PRIORITY | DIRECT | ENUM-ALLOC | 05 11 12 13 | 10 | CL-02 | synthetic (required, NOT_RUN) |
| U-RS-04 | unstated | Priority ties | M-ALC / M-VAL | WP5 | FX-30 + FX-PRIORITY | DIRECT | ENUM-ALLOC | 05 11 12 13 | 15 | CL-02 | synthetic (required, NOT_RUN) |
| U-RS-05 | unstated | Priority outside 0-11 | M-LED / M-M4 | WP1 | Input-validation fixture; override: -1, 12, 99, empty, text, 1.5 | DIRECT | CONFIGERR | 15 23 24 | 16 | DATA:TPRIO 99 (historical open question) | synthetic (required, NOT_RUN) |
| U-RS-06 | unstated | Order setting: ship date first | M-ALC / M-POL | WP5 | FX-30 (R-4 setup) | DIRECT | ENUM-ALLOC | 07 08 11 | - | CL-02 | synthetic (required, NOT_RUN) |
| U-RS-07 | unstated | Order setting: priority first | M-ALC / M-POL | WP5 | FX-30 (R-4 setup) | DIRECT | ENUM-ALLOC | 07 08 11 | - | CL-02 | synthetic (required, NOT_RUN) |
| U-RS-08 | unstated | Carry forward on | M-LED / M-M4 M-VAL | WP3 | FX-30 | DIRECT | STATE + LEDGER | 01 07 08 18 | - | - | synthetic (required, NOT_RUN) |
| U-RS-09 | unstated | Pull forward on, 10 days | M-LED / M-M4 M-VAL | WP3 | FX-30 | DIRECT | STATE + LEDGER | 01 07 08 18 | - | - | synthetic (required, NOT_RUN); zeroDayWindow (required, NOT_RUN) |
| U-RS-10 | unstated | Pull forward off | M-LED / M-M4 M-VAL | WP3 | FX-30 | DIRECT | STATE + LEDGER | 01 07 08 18 | - | - | synthetic (required, NOT_RUN) |
| U-RS-11 | unstated | Goods available after the ship date | M-LED / M-M4 M-VAL | WP3 | FX-30 | DIRECT | STATE + LEDGER | 01 07 08 18 | 27 | - | synthetic (required, NOT_RUN) |
### Current requirements Section 4.9

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-DA-01 | unstated | CS, DIS and PAL all divided by `Cases_Unit_Load` | M-INT / M-M4 M-LED | WP8 | Unit-conversion fixtures (M4 converter) | ROUTE + DIRECT | ARITH + LEDGER | 01 15 18 23 24 | 19 | - | synthetic (required, NOT_RUN) |
| U-DA-02 | unstated | Wrong unit of measure (e.g. eaches) | M-INT / M-M4 M-LED | WP8 | Unit-conversion fixtures (M4 converter) | ROUTE + DIRECT | ARITH + LEDGER | 01 15 18 23 24 | 19 | - | synthetic (required, NOT_RUN) |
| U-DA-03 | unstated | Product missing from the master | M-INT / M-M4 M-LED | WP8 | Unit-conversion fixtures (M4 converter) | ROUTE + DIRECT | ARITH + LEDGER | 01 15 18 23 24 | 20 | - | synthetic (required, NOT_RUN) |
| U-DA-04 | unstated | Pallet-type variant chosen by whole pallets | M-INT / M-M4 M-LED | WP8 | Unit-conversion fixtures (M4 converter) | ROUTE + DIRECT | ARITH + LEDGER | 01 15 18 23 24 | - | - | synthetic (required, NOT_RUN) |
| U-DA-05 | unstated | Per-truck size or weight given in the input | M-INT / M-M4 M-LED | WP8 | Unit-conversion fixtures (M4 converter) | ROUTE + DIRECT | ARITH + LEDGER | 01 15 18 23 24 | - | CAPABILITY:per-truck trailer size/weight in input (NOT SUPPORTED at 7cef94e, C9) | adapterSeam (required, NOT_RUN); inputFeed (conditional, BLOCKED); mixedRunRejected (required, NOT_RUN) |
| U-DA-06 | unstated | Multi-stop, temperature class or combo lane appears (PARTIAL: only the combo-lane stop is tested. Multi-stop loads and temperature classes have no input field today (each STR line has one LOCFRNO and one LOCTONO and no temperature), so they cannot be fed in; question for Tom.) | M-INT / M-M4 | WP8 | Parked-feature rows | ROUTE | CONFIGERR | 15 23 24 | 24 | - | synthetic (required, NOT_RUN) |
### Current requirements Section 4.10

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-RP-01 | unstated | Run header, Section 0.7 / preview 3A structure | M-RPT / M-POL | WP8 | FX-30 profile + missing-profile variant | REPORT | REPORTPARSE | 22 | - | CL-01, CL-03 | synthetic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| U-RP-02 | unstated | Lane report, Section 0.7 / preview 3B structure | M-RPT / M-ORC | WP8 | FX-30 structured outcome | REPORT | REPORTPARSE | 22 | - | CL-03 | synthetic (required, NOT_RUN) |
| U-RP-03 | unstated | Method evidence, Section 0.7 / preview 3C structure | M-RPT / M-ORC | WP8 | FX-30 structured outcome | REPORT | REPORTPARSE | 22 | - | CL-04, Q6 (answered) | synthetic (required, NOT_RUN) |
| U-RP-04 | unstated | Threshold benchmark, Section 0.7 / preview 3D structure | M-RPT / M-ORC | WP10 | Real August fixtures | BENCH | REPORTPARSE | 22 | - | CL-05, CL-08 | benchmarkReport (required, NOT_RUN) |
| U-RP-05 | unstated | Summary, Section 0.7 / preview 3E structure | M-RPT / M-ORC | WP8 | FX-30 structured outcome | REPORT | REPORTPARSE | 22 | - | CL-05, Q2 (answered) | synthetic (required, NOT_RUN) |
| U-RP-06 | unstated | Output is plain ASCII | M-RPT | WP8 | any | REPORT | REPORTPARSE | 22 | - | - | synthetic (required, NOT_RUN) |
| U-RP-07 | unstated | Fill comparison | M-RPT | WP10 | Real August fixtures | REAL | BASELINE | 22 | - | CL-03 | realData (required, NOT_RUN) |
### Current requirements Section 4.11

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-PF-01 | unstated | Each large lane; declared timing contract and valid result | M-ORC / M-RPT | WP10 | FX-CLOCK + real lanes | DIRECT + REAL | CLOCK | 12 13 | - | CL-05, Q2 (answered) | fakeClockMetrics (required, NOT_RUN); realPerformance (required, NOT_RUN) |
| U-PF-02 | unstated | Benchmark M4/A/B on identical eligible goods and constraints for historical sizes 12/16/20/23/38 | M-ORC / M-INT M-RPT | WP10 | Real August fixtures | BENCH | BASELINE | 12 13 | - | CL-05, CL-08 | benchmark (required, NOT_RUN) |
| U-PF-03 | unstated | Same input on 1 thread and many threads | M-ORC / M-ALC | WP7 | FX-30 + work-count stop | DIRECT | STATE | 12 13 | 25 | CL-09 | workCountStop (required, NOT_RUN); wallClock (required, NOT_RUN) |
| U-PF-04 | unstated | `testM4Benchmark.cpp` thread test (around line 497) that crashed under `--success` | M-M4 / TEST | WP0 | Real August fixtures | BENCH | STATE | - | - | - | existingTest (required, NOT_RUN) |
| U-PF-05 | unstated | Time contract arithmetic and configuration: secondsPerLoad 1 and 2.5 with 21 and 20 reserved trucks; invalid values 0... | M-POL / M-ORC | WP1 | FX-CLOCK arithmetic | POLICY | ARITH + CONFIGERR | 12 13 15 23 24 | - | CL-05 | synthetic (required, NOT_RUN) |
### Current requirements Section 4.12

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-FL-01 | unstated | Controlled label seam: 30 traceable FX-30 source units; candidate contains 29, then 30; one reservation; production OFF | M-VAL / M-PHY | WP3 | FX-30 / local light stacks + FX-FULL | VALID | ARITH + LAYOUT | 02 03 04 09 14 20 | - | CL-03 | synthetic (required, NOT_RUN) |
| U-FL-02 | unstated | Controlled label seam: 31 traceable nonstackable TLD units, each 20 x 100 x 108 in and 300 lb final, default trailer,... | M-VAL / M-PHY | WP3 | FX-30 / local light stacks + FX-FULL | VALID | ARITH + LAYOUT | 02 03 04 09 14 20 | - | CL-03 | synthetic (required, NOT_RUN) |
| U-FL-03 | unstated | Rear limit: missing, 14, 10, 30; malformed, nonfinite, negative, zero and out-of-range inputs tested separately | M-POL / M-VAL | WP1 | FX-FULL config variants | POLICY | CONFIGERR + STATE | 14 15 23 24 | - | CL-03 | synthetic (required, NOT_RUN); zeroAndMaximumRange (conditional, NOT_RUN) |
| U-FL-04 | unstated | Production display label fixture: 31 available/due-D X-E2E-09 nonstackable displays, one group/reservation; selected ... | M-VAL / M-PHY | WP3 | X-E2E-09 display fixture + FX-FULL | VALID | LAYOUT + ARITH | 02 03 04 09 14 20 | 34 | CL-03 | synthetic (required, NOT_RUN) |
| U-FL-05 | unstated | Final selected-plan buckets: eligible residual, empty residual, only rule-excluded residual, mixed residual, and reve... | M-RPT / M-VAL M-LED | WP8 | FX-30 three-truck bucket fixture | DIRECT + REPORT | STATE + REPORTPARSE | 14 22 | 41 | CL-03 | synthetic (required, NOT_RUN) |
### Current requirements Section 4.13

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-CF-01 | unstated | M5 lane-date D with spare space after all goods due by D are loaded; can-go goods due D+3 available on D | M-INT / M-LED M-ALC M-M4 | WP8 | FX-30 + FX-CANGO | ROUTE + DIRECT | ENUM-ALLOC + LEDGER | 01 07 08 11 18 | - | CL-02 | synthetic (required, NOT_RUN) |
| U-CF-02 | unstated | Can-go window and availability: goods due D+10 and D+11; goods due D+5 available D+1 | M-INT / M-LED M-ALC M-M4 | WP8 | FX-30 + FX-CANGO | ROUTE + DIRECT | ENUM-ALLOC + LEDGER | 01 07 08 11 18 | - | CL-02 | synthetic (required, NOT_RUN) |
| U-CF-03 | unstated | One lane: D has 21 reservations (normal M5 routing), D+1 has 20 (normal M4). Ten FX-30 units due D and twenty due D+1... | M-INT / M-LED M-M4 | WP8 | FX-30 + FX-CANGO, two dates; override: Normal routing: 21 reservations on D and 20 on D+1; preserve separate date pools and exactly-once source reconciliation. | ROUTE + DIRECT | LEDGER | 01 07 08 16 17 18 | 37 | - | synthetic (required, NOT_RUN) |
### Current requirements Section 6

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| RD-01 | unstated | Aug5: lane-dates sent to M5 under the current rule (reserved, per lane-date, > 20) | M-INT / M-M4 | WP8 | Real Aug4/5/6 | REAL | BASELINE | 16 17 | - | CL-08 | realData (required, NOT_RUN) |
| RD-02 | unstated | Aug5, 7 Aug: goods M5 receives from M4 | M-INT / M-M4 | WP0 | Real Aug5 | REAL | BASELINE | 01 18 | - | DATA:baseline committed under docs/m5/baselines (B1) | realData (required, NOT_RUN) |
| RD-03 | unstated | Aug5, 7 Aug: trucks M5 builds | M-ORC / M-RPT | WP10 | Real Aug5 7 Aug | REAL | BASELINE + LEDGER | 01 05 14 18 | - | CL-01, CL-03 | syntheticPolicyDiagnostic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| RD-04 | unstated | Aug5, 8 Aug: 20 reserved | M-INT / M-M4 M-LED | WP10 | Real Aug5 | REAL | BASELINE + LEDGER | 01 16 17 18 | - | DATA:baseline committed under docs/m5/baselines (B15) | realData (required, NOT_RUN) |
| RD-05 | unstated | Aug4 and Aug6 under the current rule | M-INT / M-M4 | WP8 | Real Aug4/5/6 | REAL | BASELINE | 16 17 | - | CL-08 | realData (required, NOT_RUN) |
| RD-06 | unstated | Aug5, both dates joined | M-ORC / M-RPT M-INT | WP10 | Real Aug5/Aug6 joined dates | BENCH | BASELINE + LEDGER | 05 07 08 16 17 | - | CL-01, CL-05, Q2 (answered), Q5 (answered) | benchmark (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| RD-07 | unstated | Aug6, 2028 -> 2190, both dates joined | M-ORC / M-RPT M-INT | WP10 | Real Aug5/Aug6 joined dates | BENCH | BASELINE + LEDGER | 05 07 08 16 17 | - | CL-01, CL-05, Q2 (answered), Q5 (answered) | benchmark (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| RD-08 | unstated | Raw-material lanes (TL_N102) on Aug4 | M-INT / M-M4 | WP8 | Real Aug4 | REAL | BASELINE | 16 17 | - | Q5 (answered) | realData (required, NOT_RUN) |
| RD-09 | unstated | M4 figures for independent lanes M5 does not touch, same hand-over input, production run | M-INT / M-M4 M-LED | WP10 | Real Aug5 | REAL | BASELINE + LEDGER | 01 16 17 18 | - | - | realData (required, NOT_RUN) |
| RD-10 | unstated | Fill comparison | M-RPT | WP10 | Real Aug4/5/6 | REAL | BASELINE | 22 | - | CL-03 | realData (required, NOT_RUN) |
| RD-11 | unstated | Reservation totals add up | M-RPT | WP10 | Real Aug4/5/6 + client figures | REAL | ARITH + BASELINE | 22 | - | - | reconciliation (required, NOT_RUN) |
| RD-12 | unstated | Fullness of historical and planned trucks | M-RPT / M-VAL M-PHY M-LED | WP10 | Historical client trucks and final M4/M5 plans with actual M4 layout witnesses, final leftovers and current fullness profile | REAL | BASELINE + LAYOUT + ARITH + REPORTPARSE | 14 | - | CL-03 | productionProfile (required, NOT_RUN) |
| RD-13 | unstated | Linked dates of one lane: Aug6 run, 2028 -> 2574 TL, both dates, 21 trucks | M-RPT / M-LED M-M4 | WP10 | Exact linked-date August fixtures and normalized lane-date baseline evidence: original Aug6 2028->2574TL control plus reported Aug5 2023->2508TF and 2299->2292TF regressions from B30 | REAL | BASELINE + LEDGER | 01 11 18 | - | CL-02 | linkedDates (required, NOT_RUN); baselineAug5_2023_2508 (required, NOT_RUN); baselineAug5_2299_2292 (required, NOT_RUN) |
### Current requirements Section 11

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-ROUTE-01 | P0 | Thresholds T = 1, 20, 50, each with T-1, T and T+1 reserved | M-INT / M-M4 M-LED | WP8 | FX-ROUTE | ROUTE | STATE | 05 16 17 | 01 02 | CL-08 | synthetic (required, NOT_RUN) |
| X-ROUTE-02 | P0 | One lane-date, 21 reserved, two groups needing 11 and 10 trucks | M-INT / M-M4 M-LED | WP8 | FX-ROUTE | ROUTE | STATE | 05 16 17 | 28 | CL-08 | synthetic (required, NOT_RUN) |
| X-ROUTE-03 | P0 | Lane X: day 1 = 21, day 2 = 20 reserved. Lane Y: day 1 = 20, day 2 = 21 | M-INT / M-M4 M-LED | WP8 | FX-ROUTE | ROUTE | STATE | 05 16 17 | 03 | CL-08 | synthetic (required, NOT_RUN) |
| X-ROUTE-04 | P0 | Two different lanes (different origin or destination) with the same products, 11 reserved each | M-INT / M-M4 M-LED | WP8 | FX-ROUTE | ROUTE | STATE | 05 16 17 | - | CL-08 | synthetic (required, NOT_RUN) |
| X-ROUTE-05 | P0 | The same reservation record twice; and two genuinely different reservations with equal values | M-INT / M-M4 M-LED | WP8 | FX-ROUTE | ROUTE | STATE | 05 16 17 | - | CL-08 | synthetic (required, PARTIAL: repeated record never doubles trucks; placeholder feed has no reservation identity, so genuine equal reservations cannot be distinguished; question for Tom); malformedHandoffReservation (required, NOT_RUN) |
| X-ROUTE-06 | P0 | Threshold or block size negative, 0, 1.5, missing, or too large | M-POL / M-INT | WP1 | FX-ROUTE | POLICY | CONFIGERR | 15 23 24 | - | CL-08 | synthetic (required, NOT_RUN) |
| X-ROUTE-07 | P0 | FX-ROUTE: same reservations, scope and T; vary current goods and historical client built counts. Current goods stay n... | M-INT / M-POL | WP8 | FX-ROUTE | ROUTE + POLICY | STATE + CONFIGERR | 16 17 | - | CL-08 | synthetic (required, NOT_RUN) |
| X-ROUTE-08 | P0 | One run with TF/TL goods, raw-material rows, and one parked-feature row | M-INT / M-M4 | WP8 | FX-ROUTE + raw + parked rows | ROUTE | STATE + CONFIGERR | 15 16 17 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-ROUTE-09 | P1 | Forced M5 benchmark on a 20-reserved lane, then a normal run in the same process | M-ORC / M-INT | WP7 | FX-ROUTE; override: forced benchmark then normal run, same process | FORCED + ROUTE | STATE | 16 17 21 | - | - | synthetic (required, NOT_RUN) |
| X-ROUTE-10 | P0 | Very repetitive lane with 19 reserved, then 21 reserved | M-INT / M-M4 M-LED | WP8 | FX-ROUTE | ROUTE | STATE | 05 16 17 | - | CL-08 | synthetic (required, NOT_RUN) |
### Current requirements Section 12

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-CG-01 | P0 | FFD trap case | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE + ARITH | 01 10 15 18 19 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-CG-02 | P0 | 6, 6, 6 | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE + ARITH | 01 10 15 18 19 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-CG-03 | P0 | Five items of 3 | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE + ARITH | 01 10 15 18 19 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-CG-04 | P0 | Sizes 6 and 4, demand one of each. Start the LP with only columns (1,0) and (0,2). Formulation: minimise copies x1 + ... | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE + ARITH | 01 10 15 18 19 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-CG-05 | P0 | Pricing check: sizes 6 and 4, dual values 3/5 and 11/20 | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE + ARITH | 01 10 15 18 19 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-CG-06 | P0 | Same as X-CG-05 but pricing stopped early (cap, timeout, heuristic miss) | M-OPT / M-ORC | WP2 | FX-COUNT + FX-CLOCK | CORE | STATE + CLOCK + LEDGER | 01 10 15 18 19 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-CG-07 | P1 | Reduced costs very close to the tolerance on both sides | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE + ARITH | 01 10 15 18 19 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-CG-08 | P1 | Pricing returns the same pattern again (also reordered) | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE + ARITH | 01 10 15 18 19 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-CG-09 | P0 | One item of 11 (bigger than capacity 10) | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE + ARITH | 01 10 15 18 19 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-CG-10 | P0 | No goods (also: reservations but no eligible goods) | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE + ARITH | 01 05 18 | - | - | synthetic (required, NOT_RUN) |
| X-CG-11 | P0 | FFD trap case, stopped before the better answer is found | M-OPT / M-ORC | WP2 | FX-COUNT + FX-CLOCK | CORE | STATE + CLOCK + LEDGER | 01 10 15 18 19 23 24 | 21 | - | synthetic (required, NOT_RUN) |
| X-CG-12 | P0 | Stopped before any result exists | M-OPT / M-ORC | WP2 | FX-COUNT + FX-CLOCK | CORE | STATE + CLOCK + LEDGER | 01 10 15 18 19 23 24 | 21 | - | synthetic (required, NOT_RUN) |
| X-CG-13 | P0 | 6 in group A and 4 in group B, no mixing. Then A = [6,6], B = [4], trailers of 12 and 8 | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE + ARITH | 01 10 15 18 19 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-CG-14 | P0 | Result 2 with a claimed lower bound of 3; also a bound from different goods; also no bound | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE + ARITH | 01 10 15 18 19 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-CG-15 | P1 | Random small cases (10 items or fewer), fixed seeds, checked by brute force | M-OPT / TEST | WP9 | FX-COUNT random, fixed seeds | CORE | BRUTE | 01 10 18 19 | - | - | seeded (required, NOT_RUN) |
| X-CG-16 | P0 | A generated pattern with a negative or fractional count, an unknown item, or over capacity | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE + ARITH | 01 10 15 18 19 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-CG-17 | P0 | Feasible result 12, valid integer bound 10 for the same required goods/objective; FX-GAP | M-OPT / M-RPT M-POL | WP2 | FX-GAP | CORE + REPORT | ARITH | 10 19 | - | CL-04, Q6 (answered) | synthetic (required, NOT_RUN) |
| X-CG-18 | P0 | Capacity 10. Demand three items of 5 and six items of 2, all required. 1-D maths only: no kerf, trailer rules, dates ... | M-OPT / TEST | WP2 | FX-COUNT | CORE | BRUTE + ARITH | 01 10 15 18 19 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-CG-19 | P0 | A capacity bound does not certify an accepted all-goods packing | M-OPT / M-POL | WP2 | FX-COUNT + acceptance adapter; override: x = 10, y = 6, strict | CORE | BRUTE + ARITH | 01 10 14 18 19 | - | CL-01 | allGoodsFeasibility (required, NOT_RUN); partialService (required, NOT_RUN) |
### Current requirements Section 13

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-PAT-01 | P0 | Pattern holds 5. Goods 0, 4, 5, 15, 17 | M-MA / M-LED | WP4 | FX-COUNT / FX-30 | DIRECT | ARITH + LEDGER | 01 15 18 23 24 | 06 | - | synthetic (required, NOT_RUN) |
| X-PAT-02 | P0 | Mixed pattern uses 2 of A and 1 of B; stock A = 5, B = 4 | M-MA / M-LED | WP4 | FX-COUNT / FX-30 | DIRECT | ARITH + LEDGER | 01 15 18 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-PAT-03 | P0 | Empty pattern, all-zero pattern, pattern needing a product with no stock | M-MA / M-LED | WP4 | FX-COUNT / FX-30 | DIRECT | ARITH + LEDGER | 01 15 18 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-PAT-04 | P1 | Arithmetic only: 2^31 units, pattern of 64 | M-MA / M-LED | WP4 | FX-COUNT / FX-30 | DIRECT | ARITH + LEDGER | 01 15 18 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-PAT-05 | P0 | Same product: 30 units eligible now, 70 due 11 days later, pull limit 10, pattern size 10 | M-MA / M-LED | WP4 | FX-COUNT / FX-30 | DIRECT | ARITH + LEDGER | 01 07 08 18 | 29 | - | synthetic (required, NOT_RUN) |
| X-PAT-06 | P0 | PTL layers 49x39 and 47x41 vs TLD | M-PHY / M-M4 M-VAL | WP3 | Local physical fixtures | CORE | LAYOUT + ARITH | 02 03 04 09 20 | - | CL-07 | synthetic (required, NOT_RUN) |
| X-PAT-07 | P0 | Content height 51 in per unit load, excluding any deck. Trailer 108 in | M-PHY / M-M4 M-VAL | WP3 | Local physical fixtures | CORE | LAYOUT + ARITH | 02 03 04 09 20 | - | CL-07 | synthetic (required, NOT_RUN) |
| X-PAT-08 | P0 | A pattern that needs 625 in of length, reused on a 600 in trailer; a 42,000 lb pattern reused against a 40,000 lb limit | M-PHY / M-ORC | WP7 | Local physical fixtures + master V1/V2 | DIRECT | STATE + LAYOUT | 02 03 04 09 20 21 | 30 | - | synthetic (required, NOT_RUN) |
| X-PAT-09 | P0 | Master V1, then V2 with taller or heavier product, same id | M-PHY / M-ORC | WP7 | Local physical fixtures + master V1/V2 | DIRECT | STATE + LAYOUT | 02 03 04 09 20 21 | 30 | - | synthetic (required, NOT_RUN) |
| X-PAT-10 | P0 | One pattern copied to 3 trucks; repair only the middle one | M-REP / M-MA M-LED | WP4 | FX-30 | DIRECT | STATE + LEDGER | 01 18 | - | - | synthetic (required, NOT_RUN) |
| X-PAT-11 | P0 | Trailer 100x100x100; two non-stackable loads 60x60x100 | M-PHY / M-M4 M-VAL | WP3 | Local physical fixtures | CORE | LAYOUT + ARITH | 02 03 04 09 20 | - | CL-07 | synthetic (required, NOT_RUN) |
| X-PAT-12 | P0 | Trailer 100x60x100; load 60x100x100, turning allowed | M-PHY / M-M4 M-VAL | WP3 | Local physical fixtures | CORE | LAYOUT + ARITH | 02 03 04 09 20 | - | CL-07 | synthetic (required, NOT_RUN) |
| X-PAT-13 | P0 | Each dimension and weight exactly at the limit, one supported input step under, one step over (stacked height too). A... | M-PHY / M-M4 M-VAL | WP3 | Local physical fixtures | CORE | LAYOUT + ARITH | 02 03 04 09 20 | 17 | CL-07 | synthetic (required, NOT_RUN); twoAcross100 (required, NOT_RUN); microLengthBoundary (required, NOT_RUN) |
| X-PAT-14 | P0 | Limits W = 10, V = 10. Two items each W = 6, V = 1. Then swap W and V | M-PHY / M-M4 M-VAL | WP3 | Local physical fixtures | CORE | LAYOUT + ARITH | 02 03 04 09 20 | 05 | CL-07 | synthetic (required, NOT_RUN) |
| X-PAT-15 | P0 | Three 36 in layers in 108 in: one variant stackable, one marked not stackable, one limited to two high | M-PHY / M-M4 | WP3 | Local physical fixtures; override: stacking asserted through the M2 stack builder (canStack); M5 never stacks items (B5) | CORE | LAYOUT + ARITH | 02 03 04 09 20 | - | CL-07 | synthetic (required, NOT_RUN) |
### Current requirements Section 14

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-REB-01 | P0 | Capacity 10: two A of 6, two B of 4, same date and priority | M-MA / M-OPT M-REP | WP4 | FX-COUNT model-level rebalance; explicit source group/date predicates; physical fullness NOT_APPLICABLE | CORE | BRUTE + LEDGER | 01 06 07 08 18 | - | - | synthetic (required, NOT_RUN) |
| X-REB-02 | P0 | Same, but A and B in different groups; then B not available yet | M-MA / M-OPT M-REP | WP4 | FX-COUNT model-level rebalance; explicit source group/date predicates; physical fullness NOT_APPLICABLE | CORE | BRUTE + LEDGER | 01 06 07 08 18 | - | - | synthetic (required, NOT_RUN) |
| X-REB-03 | P0 | Copies use up all goods; then a remainder that is all ineligible by date | M-MA / M-OPT M-REP | WP4 | FX-COUNT model-level rebalance; explicit source group/date predicates; physical fullness NOT_APPLICABLE | CORE | BRUTE + LEDGER | 01 06 07 08 18 | - | - | synthetic (required, NOT_RUN) |
| X-REB-04 | P1 | Scalar acceptance adapter: three items each cube 8/weight 1; physical capacities V = 10/W = 10; synthetic x = 10/y = ... | M-VAL / M-OPT M-POL | WP4 | FX-COUNT acceptance adapter; override: x = 10, y = 8.4 | CORE | BRUTE + ARITH + LEDGER | 01 14 18 | 09 | CL-01 | synthetic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| X-REB-05 | P0 | Floor repair removes goods from a proven truck; another choice now becomes better | M-REP / M-VAL | WP4 | FX-COUNT / FX-30 | DIRECT | STATE + LEDGER | 01 02 03 04 07 08 09 10 18 19 20 | 11 | - | synthetic (required, NOT_RUN) |
| X-REB-06 | P0 | Rebalancing makes a mixed-date stack with pull-forward off | M-REP / M-VAL | WP4 | FX-COUNT / FX-30 | DIRECT | STATE + LEDGER | 01 02 03 04 07 08 09 10 18 19 20 | 13 | - | synthetic (required, NOT_RUN) |
| X-REB-07 | P0 | Scalar capacity V = 10/W = 10. P0 item cube 8/weight 1; P11 item cube 9/weight 1; same group and due/available D, one... | M-ALC / M-VAL M-POL | WP5 | FX-COUNT acceptance adapter; override: variant A y = 7.9, variant B y = 8; priority-first | CORE | BRUTE + ARITH + LEDGER | 11 14 | - | CL-01, CL-02 | variantA (required, NOT_RUN); variantB (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| X-REB-08 | P0 | A (V = 6, W = 4) + B (V = 4, W = 7) in a truck of V = 10, W = 10 | M-REP / M-VAL | WP4 | FX-COUNT / FX-30 | DIRECT | STATE + LEDGER | 01 02 03 04 07 08 09 10 18 19 20 | - | - | synthetic (required, NOT_RUN) |
| X-REB-09 | P0 | Rebalance both sides of a strict acceptance boundary | M-REP / M-ALC M-VAL | WP4 | FX-30; override: 50 units / 49 units, two reservations | DIRECT | ENUM-ALLOC + ARITH | 01 05 14 18 | 09 | CL-01, CL-03 | fifty (required, NOT_RUN); fortyNine (required, NOT_RUN); productionProfile (required, NOT_RUN) |
### Current requirements Section 15

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-BLK-01 | P0 | Block 19, goods for 0, 1, 18, 19, 20, 37, 38, 39 trucks | M-MB / M-ALC M-LED M-VAL | WP6 | FX-BLOCK | DIRECT | ARITH + LEDGER | 01 05 18 | - | - | synthetic (required, NOT_RUN) |
| X-BLK-02 | P0 | 40 trucks of goods, 38 reserved | M-MB / M-ALC M-LED M-VAL | WP6 | FX-BLOCK | DIRECT | ARITH + LEDGER | 01 05 18 | - | - | synthetic (required, NOT_RUN) |
| X-BLK-03 | P0 | 30 trucks of goods, 38 reserved | M-MB / M-ALC M-LED M-VAL | WP6 | FX-BLOCK | DIRECT | ARITH + LEDGER | 01 05 18 | - | - | synthetic (required, NOT_RUN) |
| X-BLK-04 | P0 | Several groups share 21 reserved; first block uses 19 | M-MB / M-ALC M-LED M-VAL | WP6 | FX-BLOCK | DIRECT | ARITH + LEDGER | 01 05 18 | 28 | - | synthetic (required, NOT_RUN) |
| X-BLK-05 | P0 | A uses fewer trucks but one fails the floor check; B is valid. Then both invalid | M-MB / M-ALC M-LED M-VAL | WP6 | FX-BLOCK | DIRECT | ARITH + LEDGER | 01 05 18 | - | - | synthetic (required, NOT_RUN) |
| X-BLK-06 | P0 | Thirty same-group P0 units due/available D, three reservations. Local TLD fixture: 63 x 100 x 108 in, 4,500 lb final,... | M-ALC / M-MA M-MB | WP7 | Local 63 x 100 TLD (X-BLK-06) + FX-PRIORITY | DIRECT | ENUM-ALLOC | 01 11 18 | - | CL-02, Q3 (answered) | synthetic (required, NOT_RUN) |
| X-BLK-07 | P0 | FX-CLOCK fixed allowance 100 time units; block 1 ends at 60, repair at 85 | M-ORC / M-MB | WP7 | FX-CLOCK; override: allowance 100 units | DIRECT | CLOCK | 15 23 24 | - | CL-05, Q2 (answered) | synthetic (required, NOT_RUN) |
| X-BLK-08 | P0 | Block 1 passes all final candidate checks. Block 2 mutates provisional goods then fails. Exercise FX-COMMIT keep-vali... | M-COM / M-MB M-ORC | WP7 | FX-BLOCK + FX-COMMIT | DIRECT | STATE + LEDGER | 01 15 16 17 18 23 24 | - | CL-10 | keepValidated (required, NOT_RUN); wholeLaneRollback (required, NOT_RUN) |
### Current requirements Section 16

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-RES-01 | P0 | Two reserved. Group A 90 units, group B 60, equal priority | M-ALC / M-LED M-VAL | WP5 | FX-30 + FX-PRIORITY | DIRECT | CLASSCOUNT + LAYOUT + LEDGER | 01 05 06 11 18 | 28 | CL-02 | synthetic (required, NOT_RUN) |
| X-RES-02 | P0 | Three reserved. 60 of X (two patterns) + 15 Y + 15 Z (remainder) | M-ALC / M-LED M-VAL | WP5 | FX-30 + FX-PRIORITY | DIRECT | CLASSCOUNT + LAYOUT + LEDGER | 01 05 06 11 18 | 10 | CL-02 | synthetic (required, NOT_RUN) |
| X-RES-03 | P0 | Lane: 2 reserved on D, none on D+1, 3 on D+2. More than 2 trucks of goods on D | M-ALC / M-LED M-VAL | WP5 | FX-30 + FX-PRIORITY | DIRECT | CLASSCOUNT + LAYOUT + LEDGER | 01 05 06 11 18 | - | CL-02 | synthetic (required, NOT_RUN) |
| X-RES-04 | P0 | Several client labels for the same lane-date; another lane with the same products | M-ALC / M-LED M-VAL | WP5 | FX-30 + FX-PRIORITY | DIRECT | ENUM-ALLOC + LEDGER | 01 05 06 11 18 | - | CL-02 | synthetic (required, NOT_RUN) |
| X-RES-05 | P0 | Reserved count -1, 1.5, NaN, infinity, huge; and a real 0 | M-LED / M-POL | WP1 | Reservation input validation | DIRECT | CONFIGERR | 15 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-RES-06 | P0 | Two reserved. A: 1 P0 + 29 P11. B: 20 P0 + 10 P11. C: 30 P0. Priority first | M-ALC / M-LED M-VAL | WP5 | FX-30 + FX-PRIORITY | DIRECT | CLASSCOUNT + LAYOUT + LEDGER | 01 05 06 11 18 | 12 | CL-02 | synthetic (required, NOT_RUN) |
| X-RES-07 | P0 | Two reserved. 90 P11 of X, 15 P0 of Y and 15 P0 of Z; one shared group, same date, priority-first; FX-30 physical/acc... | M-ALC / M-LED M-VAL | WP5 | FX-30 + FX-PRIORITY | DIRECT | CLASSCOUNT + LAYOUT + LEDGER | 01 05 06 11 18 | - | CL-02 | synthetic (required, NOT_RUN) |
| X-RES-08 | P0 | One reserved, priority first. Case 1: 30 valid P0 not available until D+1; 30 P11 available on D. Case 2: the 30 P0 a... | M-ALC / M-VAL M-LED | WP5 | FX-30; override: case 1 unavailable P0; case 2 oversized P0 | DIRECT | ENUM-ALLOC + STATE | 07 08 11 15 23 24 | - | CL-10 | case1 (required, NOT_RUN); case2 (required, NOT_RUN) |
| X-RES-09 | P0 | One reserved. A's first plan claims 30 P0, repair leaves 10 P0 + 20 P11. B: 20 P0 + 10 P11 | M-ALC / M-LED M-VAL | WP5 | FX-30 + FX-PRIORITY | DIRECT | ENUM-ALLOC + LEDGER | 01 05 06 11 18 | - | CL-02 | synthetic (required, NOT_RUN) |
| X-RES-10 | P1 | Two lane-dates leave 2 and 1 unused. Test rates 5 and 7; then second rate missing; then rate 0 | M-RPT / M-ORC | WP7 | FX-30; override: rates 5, 7, missing, 0 | DIRECT + REPORT | ARITH | 15 22 23 24 | - | CL-06 | synthetic (required, NOT_RUN) |
| X-RES-11 | P0 | Chosen plan claims 2 of 3 reservations, then fails a final check | M-COM / M-VAL | WP7 | FX-30 | DIRECT | STATE + LEDGER | 15 16 17 23 24 | - | CL-10 | synthetic (required, NOT_RUN) |
| X-RES-12 | P0 | Two reserved trailers, default dimensions, weight limits 45,000 and 30,000 lb; 50 FX-30 units same group/date/P0, no ... | M-ALC / M-PHY M-VAL | WP5 | FX-30 with heterogeneous reserved trailers; override: 45,000 / 30,000 lb; variant A y = 2,400; variant B y = 3,000 | DIRECT | ENUM-ALLOC + LAYOUT | 02 03 04 05 09 14 20 21 | - | CAPABILITY:per-truck trailer capability not in input; seam-level test (B4, C9), CL-01, CL-03 | variantA (required, NOT_RUN); variantB (required, NOT_RUN); productionProfile (required, NOT_RUN) |
### Current requirements Section 17

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-DATE-01 | P0 | Pull on, 10 days. 10 units each due D, D+9, D+10, D+11; one truck | M-LED / M-M4 M-VAL | WP3 | FX-30 | DIRECT | STATE + LEDGER | 01 05 07 08 18 | - | - | synthetic (required, NOT_RUN) |
| X-DATE-02 | P0 | 30 due D; 30 due D+5 but available D+1. Pull on, 2 trucks on D | M-LED / M-M4 M-VAL | WP3 | FX-30 | DIRECT | STATE + LEDGER | 01 05 07 08 18 | 27 | - | synthetic (required, NOT_RUN) |
| X-DATE-03 | P0 | Same product: 30 due D, 30 due D+1. Pull off, 2 trucks on D | M-LED / M-M4 M-VAL | WP3 | FX-30 | DIRECT | STATE + LEDGER | 01 05 07 08 18 | - | - | synthetic (required, NOT_RUN) |
| X-DATE-04 | P0 | 60 due D. One truck on D, next one on D+4. Carry on | M-LED / M-M4 M-VAL | WP3 | FX-30 | DIRECT | STATE + LEDGER | 01 05 07 08 18 | - | - | synthetic (required, NOT_RUN) |
| X-DATE-05 | P0 | Last reserved date, 60 units, one truck, carry on | M-LED / M-M4 M-VAL | WP3 | FX-30 | DIRECT | STATE + LEDGER | 01 05 07 08 18 | - | - | synthetic (required, NOT_RUN) |
| X-DATE-06 | P0 | Component-date validation after repair/remainder mutation, not full-truck dispatch: two local TLD units 20 x 100 x 50... | M-VAL / M-REP M-LED | WP4 | Local 20 x 100 x 50 in stackable TLD; override: pull off / pull on 10 | VALID | STATE + LEDGER | 01 07 08 18 | - | - | pullOff (required, NOT_RUN); pullOn (required, NOT_RUN) |
| X-DATE-07 | P0 | Two dates, carry and pull both on | M-LED / M-M4 M-VAL | WP3 | FX-30 | DIRECT | STATE + LEDGER | 01 05 07 08 18 | - | - | synthetic (required, NOT_RUN); reversedInputDates (required, NOT_RUN) |
| X-DATE-08 | P0 | Carry 30 overdue P11 from D to D+4; 30 new P0 due D+4; one truck. Run age-first and priority-first | M-ALC / M-LED | WP5 | FX-30; override: age-first and priority-first | DIRECT | ENUM-ALLOC | 07 08 11 | 14 | - | ageFirst (required, NOT_RUN); priorityFirst (required, NOT_RUN) |
| X-DATE-09 | P0 | FX-30 goods due D+17 and available D; pull on with limit 10; separately check departure D and D+7 from fresh snapshot... | M-LED / M-ORC | WP3 | FX-30; override: fresh snapshots for D and D+7 | VALID | STATE | 07 08 21 | - | - | orderDthenD7 (required, NOT_RUN); orderD7thenD (required, NOT_RUN) |
| X-DATE-10 | P1 | Date maths across month, year and leap day | M-LED / M-M4 | WP3 | Date arithmetic | CORE | ARITH | - | - | - | synthetic (required, NOT_RUN) |
| X-DATE-11 | P0 | Two lanes, same product and origin, different destinations | M-LED / M-M4 M-VAL | WP3 | FX-30 | DIRECT | STATE + LEDGER | 01 05 07 08 18 | - | - | synthetic (required, NOT_RUN) |
| X-DATE-12 | P0 | M4's planning period plus data beyond it | M-LED / M-M4 M-VAL | WP3 | FX-30 | DIRECT | STATE + LEDGER | 01 05 07 08 18 | - | - | synthetic (required, NOT_RUN) |
### Current requirements Section 18

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-DATA-01 | P0 | M4 hands over 60 unit loads for a product with `Cases_Unit_Load` = 10 | M-LED / M-INT M-M4 | WP3 | FX-30 / conversion fixtures | DIRECT | LEDGER + ARITH | 01 18 | - | - | synthetic (required, NOT_RUN) |
| X-DATA-02 | P0 | Input X = 10, Y = 20; a faulty step returns X = 20, Y = 10 (same total) | M-LED / M-INT M-M4 | WP3 | FX-30 / conversion fixtures | DIRECT | LEDGER + ARITH | 01 18 | - | - | synthetic (required, NOT_RUN) |
| X-DATA-03 | P0 | `Cases_Unit_Load` 0, negative, missing, NaN, infinity | M-LED / M-INT M-M4 | WP1 | Input-validation fixtures | DIRECT | CONFIGERR + ARITH | 01 15 18 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-DATA-04 | P0 | 9 cases, `Cases_Unit_Load` = 2, no other variant | M-LED / M-INT M-M4 | WP1 | Input-validation fixtures | DIRECT | CONFIGERR + ARITH | 01 15 18 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-DATA-05 | P0 | Two variants both divide 12 (by 2 and by 3). Master rows reversed | M-LED / M-INT M-M4 | WP1 | Input-validation fixtures | DIRECT | CONFIGERR + ARITH | 01 15 18 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-DATA-06 | P0 | Saved pattern from V1 (1,500 lb per unit); V2 = 1,600 lb | M-PHY / M-ORC | WP7 | FX-30 V1 / V2 (1,600 lb) | DIRECT | STATE + ARITH | 02 03 04 09 20 21 | 30 | - | synthetic (required, NOT_RUN) |
| X-DATA-07 | P0 | Same product on lines with different priorities, dates and groups | M-LED / M-INT M-M4 | WP3 | FX-30 / conversion fixtures | DIRECT | LEDGER + ARITH | 01 18 | 29 | - | synthetic (required, NOT_RUN) |
| X-DATA-08 | P0 | 240 CS / 10, 432 DIS / 18, 60 PAL / 2 | M-LED / M-INT M-M4 | WP3 | FX-30 / conversion fixtures | DIRECT | LEDGER + ARITH | 01 18 | 19 | - | synthetic (required, NOT_RUN) |
| X-DATA-09 | P0 | Fixture declares 001234 and 1234 as two distinct resolved product identities (as M4 and the master hold them); group ... | M-LED / M-INT M-M4 | WP3 | FX-30 / conversion fixtures | DIRECT | LEDGER + ARITH | 01 18 | - | - | synthetic (required, NOT_RUN) |
| X-DATA-10 | P0 | Demand and reservations from different runs, or a different master than the expected results | M-LED / M-INT M-M4 | WP1 | Input-validation fixtures | DIRECT | CONFIGERR + ARITH | 01 15 18 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-DATA-11 | P0 | A source line repeated with the same values; then with different values | M-LED / M-INT M-M4 | WP3 | FX-30 / conversion fixtures | DIRECT | LEDGER + ARITH | 01 18 | - | - | synthetic (required, NOT_RUN) |
| X-DATA-12 | P1 | Totals of about 3,000,000,000 units (counts only) | M-LED / M-INT M-M4 | WP1 | Input-validation fixtures | DIRECT | CONFIGERR + ARITH | 01 15 18 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-DATA-13 | P0 | Size, weight or capacity 0, negative, NaN, infinity, or overflowing; plus a deck height of 0 for a TLD | M-LED / M-INT M-M4 | WP1 | Input-validation fixtures | DIRECT | CONFIGERR + ARITH | 01 15 18 23 24 | - | - | synthetic (required, NOT_RUN); inconsistentSlotCapabilities (required, NOT_RUN) |
| X-DATA-14 | P0 | Quantity negative, NaN, 0, 1.5 for a whole unit, or too large | M-LED / M-INT M-M4 | WP1 | Input-validation fixtures | DIRECT | CONFIGERR + ARITH | 01 15 18 23 24 | - | - | synthetic (required, NOT_RUN) |
### Current requirements Section 19

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-STATE-01 | P0 | Run A then B, B then A, and each alone, same input, run to completion | M-ORC / M-ALC M-COM M-LED | WP7 | FX-30 | DIRECT | STATE + LEDGER | 12 13 16 17 22 | - | - | synthetic (required, NOT_RUN) |
| X-STATE-02 | P0 | A and B each ship 60 units in 2 trucks from 3 reserved; test rate 5 | M-ORC / M-ALC M-COM M-LED | WP7 | FX-30 | DIRECT | STATE + LEDGER | 12 13 16 17 22 | - | CL-06 | synthetic (required, NOT_RUN); unselectedMethodProof (required, NOT_RUN) |
| X-STATE-03 | P0 | Freeze an M4 lane, then run an unrelated M5 lane with the same products | M-ORC / M-ALC M-COM M-LED | WP7 | FX-30 | DIRECT | STATE + LEDGER | 12 13 16 17 22 | - | - | synthetic (required, NOT_RUN) |
| X-STATE-04 | P0 | Same process: pull on then off; age-first then priority-first. Compare with fresh runs | M-ORC / M-ALC M-COM M-LED | WP7 | FX-30 | DIRECT | STATE + LEDGER | 12 13 16 17 22 | - | - | synthetic (required, NOT_RUN); thresholdSnapshotInFlight (required, NOT_RUN) |
| X-STATE-05 | P0 | Run M5 twice from the same hand-over | M-ORC / M-ALC M-COM M-LED | WP7 | FX-30 | DIRECT | STATE + LEDGER | 12 13 16 17 22 | - | - | synthetic (required, NOT_RUN) |
| X-STATE-06 | P0 | Three copies of one pattern from 90 traceable units; repair one copy | M-ORC / M-ALC M-COM M-LED | WP7 | FX-30 | DIRECT | STATE + LEDGER | 12 13 16 17 22 | - | - | synthetic (required, NOT_RUN) |
| X-STATE-07 | P0 | Two groups compete for the last reservation at the same time | M-ORC / M-ALC M-COM M-LED | WP7 | FX-30 | DIRECT | STATE + LEDGER | 12 13 16 17 22 | - | CL-02 | synthetic (required, NOT_RUN) |
| X-STATE-08 | P0 | A ships 30 P0, B ships 30 P1; same trucks, fill, dates and everything else | M-ALC / M-ORC | WP5 | FX-30 + FX-PRIORITY | DIRECT | ENUM-ALLOC | 11 | 22 | CL-02 | synthetic (required, NOT_RUN) |
### Current requirements Section 20

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-REPORT-01 | P0 | Aug5 production report | M-RPT / M-ORC | WP8 | Real Aug5 structured outcome / recorded baseline | REPORT + REAL | REPORTPARSE | 22 | - | - | structuredInput (required, NOT_RUN); realRun (required, NOT_RUN) |
| X-REPORT-02 | P0 | Both-dates benchmark and production in one run | M-RPT / M-ORC | WP8 | Real Aug5 structured outcome / recorded baseline | REPORT + REAL | REPORTPARSE | 22 | - | - | structuredInput (required, NOT_RUN); realRun (required, NOT_RUN) |
| X-REPORT-03 | P0 | Report after M4 fixes or master/config changes | M-RPT / M-ORC | WP8 | Real Aug5 structured outcome / recorded baseline | REPORT + REAL | REPORTPARSE | 22 | - | DATA:baseline committed under docs/m5/baselines (B1) | structuredInput (required, NOT_RUN); realRun (required, NOT_RUN) |
| X-REPORT-04 | P0 | Synthetic x = 40,000 lb/y = 3,300 cu ft; every candidate weighs 20,000 lb. Cubes 3,299.9, 3,300.0, 3,300.1, 3,307.5; ... | M-POL / M-RPT | WP1 | Synthetic x = 40,000 / y = 3,300 | POLICY + REPORT | ARITH + REPORTPARSE | 14 22 | - | CL-01, CL-03, Q8 (answered) | synthetic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| X-REPORT-05 | P1 | Lane A one truck of 2,000; lane B trucks of 3,200 and 3,400 | M-RPT / M-ORC | WP8 | Structured outcome fixtures | REPORT | REPORTPARSE + ARITH | 22 | - | - | synthetic (required, NOT_RUN) |
| X-REPORT-06 | P1 | No M5 lanes; completed M5 run with zero accepted trucks; timeout before any checked result | M-RPT / M-ORC | WP8 | Structured outcome fixtures | REPORT | REPORTPARSE + ARITH | 22 | - | - | synthetic (required, NOT_RUN) |
| X-REPORT-07 | P0 | 90 units: 60 shipped, 30 carried | M-RPT / M-ORC | WP8 | Structured outcome fixtures | REPORT | REPORTPARSE + ARITH | 22 | - | - | synthetic (required, NOT_RUN) |
| X-REPORT-08 | P0 | A finishes; B not started, timed out, or failed; after an earlier full run | M-RPT / M-ORC | WP8 | Structured outcome fixtures | REPORT | REPORTPARSE + ARITH | 22 | - | - | synthetic (required, NOT_RUN) |
| X-REPORT-09 | P1 | Ids with non-ASCII, quotes, commas, newlines, very long text | M-RPT / M-ORC | WP8 | Structured outcome fixtures | REPORT | REPORTPARSE + ARITH | 22 | - | - | synthetic (required, NOT_RUN) |
### Current requirements Section 21

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-RUN-01 | P1 | Same input on 1, 2 and 8 threads, stopped by work count | M-ORC / M-ALC | WP7 | FX-30 + work-count stop | DIRECT | STATE | 12 13 | 25 | CL-09 | workCount (required, NOT_RUN); wallClock (required, NOT_RUN) |
| X-RUN-02 | P0 | Time runs out before any result (fake clock) | M-ORC / M-COM M-VAL | WP7 | FX-CLOCK + FX-BLOCK / FX-30 | DIRECT | CLOCK + STATE + LEDGER | 01 15 16 17 18 23 24 | - | CL-05 | synthetic (required, NOT_RUN) |
| X-RUN-03 | P0 | Time runs out during provisional repair after a fully checked candidate exists; explicitly use FX-COMMIT keep-validat... | M-ORC / M-COM M-VAL | WP7 | FX-CLOCK + FX-BLOCK / FX-30 | DIRECT | CLOCK + STATE + LEDGER | 01 15 16 17 18 23 24 | - | CL-10 | keepValidated (required, NOT_RUN) |
| X-RUN-04 | P0 | FX-CLOCK: predeclared synthetic lane allowance 100 time units; A consumes 85; B would need 20; any existing result is... | M-ORC / M-COM M-VAL | WP7 | FX-CLOCK + FX-BLOCK / FX-30 | DIRECT | CLOCK + STATE + LEDGER | 01 15 16 17 18 23 24 | - | CL-05, Q2 (answered), Q3 (answered) | synthetic (required, NOT_RUN) |
| X-RUN-05 | P0 | 21 reserved. Group A needs 20 trucks of P11, group B 2 trucks of P0. Method B, priority first, A listed first | M-MB / M-ALC | WP6 | FX-BLOCK + FX-PRIORITY; override: 21 reserved; A 20 x P11 listed first; B 2 x P0; priority-first | DIRECT | CLASSCOUNT + LEDGER + STATE | 05 11 | 32 | CL-02 | synthetic (required, NOT_RUN); adverseBlockOrderingP2 (conditional, NOT_RUN) |
| X-RUN-06 | P1 | Method B with 100 reserved and no goods; or only impossible goods | M-MB / M-ORC | WP6 | FX-BLOCK | DIRECT | STATE | 15 23 24 | - | - | synthetic (required, NOT_RUN) |
| X-RUN-07 | P0 | Failure after provisional reservation claims but before any operational commit, then retry. Second variant retains va... | M-COM / M-ORC M-LED | WP7 | FX-COMMIT | DIRECT | STATE + LEDGER | 01 15 16 17 18 23 24 | 40 | CL-10 | beforeCommit (required, NOT_RUN); retainedBlocks (required, NOT_RUN); afterPartialCommit (required, NOT_RUN) |
| X-RUN-08 | P0 | Parked feature found after other goods already processed | M-INT / M-ORC | WP8 | Parked-feature row mid-run | ROUTE | STATE | 15 23 24 | 24 | - | synthetic (required, NOT_RUN) |
| X-RUN-09 | P0 | Acceptance evidence gate: missing/mismatched fixtures, failed build, old results, duplicate named IDs, duplicate test... | TEST / M-RPT | WP8 | Evidence-gate fixtures | BENCH | STATE | 12 13 | - | - | synthetic (required, NOT_RUN); duplicateID (required, NOT_RUN); duplicateVariant (required, NOT_RUN); noAssertion (required, NOT_RUN); missingVariant (required, NOT_RUN); staleSpecHash (required, NOT_RUN); deferredConditionalExcluded (required, NOT_RUN); deferredConditionalSelected (required, NOT_RUN); crossCompilerGate (required, NOT_RUN) |
| X-RUN-10 | P1 | Time M4, A and B on the same input and machine, in different orders | M-ORC / M-RPT | WP10 | Real lanes | BENCH | BASELINE | 12 13 | - | CL-05 | benchmark (required, NOT_RUN); coldWarmCache (required, NOT_RUN) |
| X-RUN-11 | P0 | Distinguish per-load maximum from an amortised average | M-ORC / M-COM M-VAL | WP7 | FX-CLOCK + FX-BLOCK / FX-30 | DIRECT | CLOCK + STATE + LEDGER | 01 15 16 17 18 23 24 | 21 | CL-05, Q2 (answered) | synthetic (required, NOT_RUN) |
| X-RUN-12 | P0 | Zero dispatch is distinct from failure to find a result | M-ORC / M-COM M-VAL | WP7 | FX-CLOCK + FX-BLOCK / FX-30 | DIRECT | CLOCK + STATE + LEDGER | 01 14 15 18 23 24 | 21 | CL-05 | noGoods (required, NOT_RUN); completedZeroDispatch (required, NOT_RUN); timeoutNoIncumbent (required, NOT_RUN) |
| X-RUN-13 | P0 | Freeze the budget and count only the chosen result | M-ORC / M-COM M-VAL | WP7 | FX-CLOCK + FX-BLOCK / FX-30 | DIRECT | CLOCK + STATE + LEDGER | 01 15 16 17 18 23 24 | 21 | CL-02, CL-05, Q2 (answered) | synthetic (required, NOT_RUN) |
### Current requirements Section 22

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-META-01 | P0 | Every unordered multiset with repetition of 0 to 7 item occurrences, each size an integer from 1 to 6, capacity 6. Th... | M-OPT / TEST | WP9 | FX-COUNT generated | CORE | BRUTE + META | 01 10 18 19 | - | - | ciSubset (required, NOT_RUN); full (required, NOT_RUN) |
| X-META-02 | P0 | Small exact case: multiply all sizes and capacity by 10, then 100 | M-OPT / TEST | WP9 | FX-COUNT generated | CORE | BRUTE + META | 01 10 18 19 | - | - | synthetic (required, NOT_RUN) |
| X-META-03 | P1 | Shuffle input rows (Scoped to M5 input. Shuffling raw input-file rows changes results because M4 stacking follows file order (architecture 22.9); for Saif/Tom.) | M-ORC / M-ALC M-LED | WP9 | FX-30 / FX-COUNT | DIRECT | META | 01 12 13 18 | - | CL-02 | synthetic (required, NOT_RUN) |
| X-META-04 | P0 | Split one line into two with the same total; merge back | M-ORC / M-ALC M-LED | WP9 | FX-30 / FX-COUNT | DIRECT | META | 01 12 13 18 | - | CL-02 | synthetic (required, NOT_RUN) |
| X-META-05 | P0 | Rename products/groups/source lines without changing physical or priority data; include a tied-allocation case and a ... | M-ORC / M-ALC M-LED | WP9 | FX-30 / FX-COUNT | DIRECT | META | 01 12 13 18 | - | CL-02 | synthetic (required, NOT_RUN) |
| X-META-06 | P0 | Copy a lane into a second, fully separate lane | M-ORC / M-ALC M-LED | WP9 | FX-30 / FX-COUNT | DIRECT | META | 01 12 13 18 | - | CL-02, ED-21 | synthetic (required, NOT_RUN) |
| X-META-07 | P0 | Small exact case: raise capacity; or remove an item | M-OPT / TEST | WP9 | FX-COUNT generated | CORE | BRUTE + META | 01 10 18 19 | - | - | synthetic (required, NOT_RUN) |
| X-META-08 | P0 | Small exact case: add a do-not-mix rule; then remove it | M-ALC / M-OPT | WP9 | FX-COUNT small exact | DIRECT | BRUTE + META | 05 06 11 | - | - | synthetic (required, NOT_RUN) |
| X-META-09 | P0 | Small scarce-truck case solved exactly: add one reservation, nothing else changed | M-ALC / M-OPT | WP9 | FX-COUNT small exact | DIRECT | BRUTE + META | 05 06 11 | - | - | synthetic (required, NOT_RUN) |
| X-META-10 | P1 | Random valid and invalid physical inputs, fixed seeds | TEST / M-PHY M-VAL | WP9 | Random physical inputs, fixed seeds | DIRECT | META | 02 03 04 09 15 20 23 24 | - | - | seeded (required, NOT_RUN); thousandStacksP2 (conditional, NOT_RUN); fiftyNineFootprintsP2 (conditional, NOT_RUN) |
### Current requirements Section 23

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-E2E-01 | P0 | Both dates, production kept separate (P0) | M-INT / M-ORC M-RPT M-COM | WP10 | Real Aug5 frozen input | REAL + FORCED | BASELINE + LEDGER | 05 07 08 16 17 22 | - | CL-01, CL-03 | syntheticPolicyDiagnostic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| X-E2E-02 | P0 | Priority after repair (P0) | M-ALC / M-REP M-VAL | WP5 | FX-30 (R-2 local y = 2,400) | DIRECT | ENUM-ALLOC + STATE | 01 10 11 18 19 | - | CL-01, CL-02, CL-03 | synthetic (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| X-E2E-03 | P0 | Patterns with pull-forward and unavailable stock (P0) | M-MA / M-LED M-VAL M-ALC | WP4 | Local 63 x 100 TLD, eight reservations; override: pull off / pull on 10 | DIRECT | CLASSCOUNT + LAYOUT + LEDGER | 01 02 03 04 07 08 09 14 18 20 | - | - | pullOff (required, NOT_RUN); pullOn (required, NOT_RUN) |
| X-E2E-04 | P0 | Acceptance rule across groups (P0) | M-VAL / M-ALC | WP4 | Local 378 x 100 units, two groups; override: y = 2,000 and y = 2,800 | DIRECT | ARITH + LEDGER | 06 14 | 31 | CL-01, CL-03 | y2000 (required, NOT_RUN); y2800 (required, NOT_RUN); productionProfile (required, NOT_RUN) |
| X-E2E-05 | P0 | Blocks stopped during the last repair (P0) | M-COM / M-MB M-ORC | WP7 | FX-BLOCK + FX-CLOCK + FX-COMMIT | DIRECT | STATE + LEDGER + CLOCK | 01 15 16 17 18 23 24 | - | CL-10 | keepValidated (required, NOT_RUN); wholeLaneRollback (required, NOT_RUN) |
| X-E2E-06 | P0 | Different trailers and an old pattern (P0) | M-PHY / M-ORC M-RPT | WP7 | Big/small trailers + master V1/V2 | DIRECT | STATE + LAYOUT | 02 03 04 09 20 21 | - | CAPABILITY:per-truck trailer capability not in input; seam-level test (B4, C9) | synthetic (required, NOT_RUN) |
| X-E2E-07 | P0 | Choosing across groups (P0) | M-ALC / M-VAL | WP5 | FX-30 + FX-PRIORITY | DIRECT | CLASSCOUNT + LAYOUT + LEDGER | 05 06 11 | - | CL-02 | inputOrderABC (required, NOT_RUN); inputOrderPermuted (required, NOT_RUN) |
| X-E2E-08 | P0 | Report with mixed-up comparison numbers (P0) | M-RPT | WP8 | Reported figures 1,440 / 1,308 / 154 | REPORT | ARITH | 22 | - | - | synthetic (required, NOT_RUN) |
| X-E2E-09 | P0 | Non-stackable displays, floor occupied (P0) | M-VAL / M-PHY M-RPT | WP4 | Synthetic display TLD 48 x 40 x 50; override: 30-position witness (15 rows x 2); y = 3,000 and y = 1,000 | DIRECT | LAYOUT + ARITH | 01 02 03 04 09 14 18 20 | - | CL-01, CL-03, Q9 (answered) | y3000 (required, NOT_RUN); y1000 (required, NOT_RUN); productionProfile (required, NOT_RUN) |
### Current requirements Section 24

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| PUB-01 | unstated | u120_00 | M-OPT / TEST | WP2 | binpack1.txt (OR-Library) | CORE | PUBOPT | 01 10 18 19 | - | DATA:binpack1.txt committed with recorded SHA-256 | published (required, NOT_RUN) |
| PUB-02 | unstated | u120_01 | M-OPT / TEST | WP2 | binpack1.txt (OR-Library) | CORE | PUBOPT | 01 10 18 19 | - | DATA:binpack1.txt committed with recorded SHA-256 | published (required, NOT_RUN) |
| PUB-03 | unstated | u120_02 | M-OPT / TEST | WP2 | binpack1.txt (OR-Library) | CORE | PUBOPT | 01 10 18 19 | - | DATA:binpack1.txt committed with recorded SHA-256 | published (required, NOT_RUN) |
### Current requirements Section 33

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-AC-09 | P0 | Sub-grain strict acceptance comparisons | M-POL / M-VAL M-RPT | WP1 | Optional ON x40000/y3000; exact cube +/-1/4096 and weight +/-1/256; OFF control | POLICY | ARITH + STATE | 14 23 | 33 | CL-01 | cubeBelow (required, NOT_RUN); cubeEqual (required, NOT_RUN); cubeAbove (required, NOT_RUN); weightBelow (required, NOT_RUN); weightEqual (required, NOT_RUN); weightAbove (required, NOT_RUN); policyOff (required, NOT_RUN) |
| U-FL-06 | P0 | Exact fullness boundaries and actual trailer capability | M-VAL / M-PHY M-POL | WP3 | Exact weight 98% and rear 14 boundaries; actual 30000/45000 lb slot; independently fitting leftovers | VALID | ARITH + LAYOUT | 02 03 14 21 23 | 33 | CL-03, ED-FULL (engineering label and residual scope) | weightBelow (required, NOT_RUN); weightEqual (required, NOT_RUN); weightAbove (required, NOT_RUN); smallTrailer (required, NOT_RUN); largeTrailer (required, NOT_RUN); rearAboveLimit (required, NOT_RUN); rearEqualLimit (required, NOT_RUN); rearBelowLimit (required, NOT_RUN) |
| U-FL-07 | P0 | Rear space after maximum legal upstream stacking | M-PHY / M-VAL M-M4 M-LED | WP3 | Three TLD210x100x36/1000 lb components plus 20in fitting leftover; upstream legal stacking 3/2/1 high | M4 + VALID | LAYOUT + ARITH + LEDGER + STATE | 01 03 04 14 | 35 | CL-03, CL-07, ED-FULL (engineering label and residual scope) | threeHigh (required, NOT_RUN); twoHigh (required, NOT_RUN); nonstackable (required, NOT_RUN); staleLabel (required, NOT_RUN) |
| U-FL-08 | P0 | Residual scope, stock exhaustion and bucket precedence | M-VAL / M-PHY M-LED | WP3 | Controlled600x100in candidate; exact fitting/nonfitting/empty/excluded/other-group residual snapshots | VALID | LAYOUT + LEDGER + STATE | 01 06 07 08 14 21 22 | 34 | CL-03, ED-FULL (engineering label and residual scope) | fittingResidual (required, NOT_RUN); noFitResidual (required, NOT_RUN); emptyResidual (required, NOT_RUN); ruleExcludedResidual (required, NOT_RUN); mixedResidual (required, NOT_RUN); otherGroupResidual (required, NOT_RUN); residualCacheRefresh (required, NOT_RUN); truckOrder (required, NOT_RUN) |
| U-FL-09 | P0 | Interrupted no-fit diagnostics remain unknown | M-VAL / M-ORC M-PHY | WP3 | U-FL-08 geometry; stop before fitting 20in residual class is tested; weight-full independent control | VALID | CLOCK + STATE + LAYOUT | 10 14 19 21 24 | 34 38 | CL-03, CL-05, ED-FULL (engineering label and residual scope) | interrupted (required, NOT_RUN); completed (required, NOT_RUN); reversedProbeOrder (required, NOT_RUN); weightFullControl (required, NOT_RUN); budgetIntegration (required, NOT_RUN) |
| U-FL-10 | P0 | Rear metric comes from final occupied extent | M-PHY / M-VAL | WP3 | Supplied 500x50 and 600x50 side-by-side rectangles; translations, invalid extents and coordinates | VALID | LAYOUT + ARITH + STATE | 03 14 20 23 | 35 | CL-03, ED-FULL (engineering label and residual scope) | maxExtent (required, NOT_RUN); rowOrder (required, NOT_RUN); translatedWitness (required, NOT_RUN); outsideTrailer (required, NOT_RUN); overlap (required, NOT_RUN); nonfinite (required, NOT_RUN) |
| U-CF-04 | P0 | Required goods win scarce reservations across groups | M-INT / M-ALC M-MA M-MB M-LED | WP8 | A31 requiredP11 versus B30 futureP0 FX30; groups separate; two/three slots; exact homogeneous capacity 30; override: Use homogeneous group-count arithmetic for the 61-unit third-slot variant; the <=60-unit general enumerator is not its oracle. | DIRECT | CLASSCOUNT + LAYOUT + LEDGER | 01 05 06 07 08 11 14 17 | 32 36 | CL-02 | methodA (required, NOT_RUN); methodB (required, NOT_RUN); inputPermutation (required, NOT_RUN); thirdReservation (required, NOT_RUN); outsideWindow (required, NOT_RUN); unavailable (required, NOT_RUN) |
| U-CF-05 | P0 | Can-go-only demand on an existing reserved M5 date | M-INT / M-LED M-ALC M-M4 | WP8 | 21 reserved >T20; no required cargo; one indivisible future FX-BLOCK atom; ON/OFF/date/availability controls | ROUTE | ARITH + LEDGER + STATE | 01 05 07 08 16 | 37 | CL-02, CL-08, ED-CANGO (working interpretation; verify adapter/source in WP0) | normalRouteCanGoOnly (required, NOT_RUN); pullOff (required, NOT_RUN); zeroReservations (required, NOT_RUN); outsideWindow (required, NOT_RUN); unavailable (required, NOT_RUN) |
| U-CF-06 | P0 | Only the selected future draw changes later M4 input | M-INT / M-ORC M-COM M-LED M-M4 | WP8 | D21/D+1 20 slots; R10/F20 FX30; controlled selectedA vs selectedB and exactly-once future draw | ROUTE + DIRECT | LEDGER + STATE | 01 05 07 08 16 17 24 | 37 40 | CL-10 | AthenB (required, NOT_RUN); BthenA (required, NOT_RUN); Aalone (required, NOT_RUN); Balone (required, NOT_RUN); failureBeforeCommit (required, NOT_RUN); retryAfterCommit (required, NOT_RUN) |
| U-CF-07 | P0 | Exact spare-capacity fill without displacing required goods | M-INT / M-MA M-MB M-ALC M-LED | WP8 | One slot; 25 requiredP11+10 eligible futureP0+two ineligible FX30; required-first capacity 30 | DIRECT | ENUM-ALLOC + ARITH + LAYOUT + LEDGER | 01 02 07 08 11 14 18 | 36 | CL-02 | methodA (required, NOT_RUN); methodB (required, NOT_RUN); pullOff (required, NOT_RUN); requiredAlreadyFull (required, NOT_RUN); equalFutureIds (required, NOT_RUN) |
| U-CF-08 | P0 | Impossible required cargo cannot deadlock feasible can-go | M-INT / M-ALC M-VAL M-LED | WP8 | One impossible required 45001 lb TLD vs 30 eligible future FX30; valid/unproven required controls | DIRECT | ARITH + ENUM-ALLOC + LEDGER | 01 02 11 15 18 24 | 36 | CL-02, ED-CANGO (working interpretation; verify adapter/source in WP0) | impossibleRequired (required, NOT_RUN); feasibleRequiredControl (required, NOT_RUN); unprovenRequiredControl (required, NOT_RUN) |
| U-PF-06 | P0 | Budget arithmetic, zero reservations and overflow | M-POL / M-ORC | WP1 | Checked secondsPerLoad*reserved arithmetic and injected integer-clock representation bounds | POLICY + DIRECT | ARITH + CONFIGERR + CLOCK + STATE | 05 15 23 24 | 38 | CL-05 | fractionalSeconds (required, NOT_RUN); zeroReservations (required, NOT_RUN); invalidSeconds (required, NOT_RUN); multiplicationOverflow (required, NOT_RUN); durationOverflow (required, NOT_RUN); deadlineOverflow (required, NOT_RUN) |
| U-PF-07 | P0 | Parallel work shares one wall-clock deadline | M-ORC / M-VAL M-COM | WP7 | 21-second fake wall deadline; optional parallel A/B8seconds; sequential control and boundary checks | DIRECT | CLOCK + STATE | 13 17 24 | 38 | CL-05, CL-09, CL-10 | parallelWallTime (required, NOT_RUN); sequentialWallTime (required, NOT_RUN); finishBeforeDeadline (required, NOT_RUN); finishAtDeadline (required, NOT_RUN); uncheckedAtDeadline (required, NOT_RUN); noPriorIncumbent (required, NOT_RUN); wallClockShift (required, NOT_RUN) |
| U-PF-08 | P0 | Separate lane-date allowances cannot be pooled | M-ORC / M-INT M-RPT | WP7 | Separate lane/date 21/20-second allowances; no pooling, reservation-version reset only by new context | DIRECT + FORCED | CLOCK + ARITH + STATE | 05 13 16 24 | 38 | CL-05, CL-08 | laneDateIsolation (required, NOT_RUN); independentLaneIsolation (required, NOT_RUN); reordered (required, NOT_RUN); concurrent (required, NOT_RUN); reservationVersionChange (required, NOT_RUN) |
| X-CG-20 | P0 | A subset count bound cannot certify priority service | M-ORC / M-OPT M-ALC | WP7 | Two slots/five FX-BLOCK atoms (2P0/3P11); subset/full-objective bound mismatch; scalar[6,6,4,4] control | DIRECT + CORE | CLASSCOUNT + ARITH + STATE + LEDGER | 10 11 19 | 21 32 | CL-02, CL-04 | subsetBound (required, NOT_RUN); wrongAllGoodsBound (required, NOT_RUN); timedIncumbent (required, NOT_RUN); matchingCountControl (required, NOT_RUN) |
| X-STATE-09 | P0 | Optional acceptance ON/OFF/ON across every planning path | M-ORC / M-POL M-MA M-MB M-REP M-VAL M-RPT | WP7 | TwentyFX30 units, one slot; ON(x40000/y3000)/OFF(absentsection)/ON with fresh and same-ID revised contexts | DIRECT + REPORT | ARITH + STATE + LEDGER + REPORTPARSE | 01 14 16 17 21 24 | 39 | CL-01, CL-03, CL-10 | methodA (required, NOT_RUN); methodB (required, NOT_RUN); remainder (required, NOT_RUN); repair (required, NOT_RUN); finalValidation (required, NOT_RUN); report (required, NOT_RUN); sameIdRevision (required, NOT_RUN); freshProcess (required, NOT_RUN); malformedOffConfig (required, NOT_RUN) |
| X-STATE-10 | P0 | Atomic commitment, payload conflicts and stale snapshots | M-COM / M-LED M-ORC | WP7 | V7 = 20 identified FX-30/two slots; atomic O1 ten-unit commit, conflicts/staleversion/replay/continuation | DIRECT | STATE + LEDGER | 01 05 13 17 18 24 | 40 | CL-10, ED-COMMIT (atomic versioned operation identity) | concurrentIdentical (required, NOT_RUN); conflictingPayload (required, NOT_RUN); staleSnapshot (required, NOT_RUN); atomicFailure (required, NOT_RUN); residualContinuation (required, NOT_RUN); replayAfterContinuation (required, NOT_RUN); changedValidationContext (required, NOT_RUN); sameSlotTieConcurrent (required, NOT_RUN) |
| X-REPORT-10 | P0 | Bucket averages and overlapping FULL reasons | M-RPT / M-VAL M-LED | WP8 | Structured rear values 10/30/100/20 with three residual buckets and overlapping weight/rear reasons | REPORT | ARITH + REPORTPARSE | 14 22 | 41 | CL-03, ED-FULL (engineering label and residual scope) | bucketMeans (required, NOT_RUN); emptyBucket (required, NOT_RUN); reordered (required, NOT_RUN); overlappingReasons (required, NOT_RUN); unknownFit (required, NOT_RUN); unselectedAndBenchmarkExcluded (required, NOT_RUN) |
### Current requirements Section 36

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-PAT-16 | P0 | Unit-load coefficients survive physical class compression | M-PHY / M-LED M-MA | WP3 | FX-CAP-ATOM: T2/T3 completed atoms both 48x36x90in/600 lb, but 2/3 unit loads; three T2 plus two T3; cap 5 one slot.; override: Acceptance OFF, rigid indivisible upstream M4 atoms, fresh snapshots for independent runs; exact case objective is declared in the source. | DIRECT | BRUTE + ARITH + LAYOUT + LEDGER | 01 03 04 09 10 18 20 21 23 | 43 44 46 | CL-07, ED-20 | mixedClasses (required, NOT_RUN); homogeneousTwo (required, NOT_RUN); homogeneousThree (required, NOT_RUN); sameGeometryABA (required, NOT_RUN); coalescedSourceShares (required, NOT_RUN); invalidCountsAndCaps (required, NOT_RUN); singleAtomOverCap (required, NOT_RUN) |
| X-PAT-17 | P0 | Caps follow actual slots through methods, pattern copies and repair | M-VAL / M-PHY M-MA M-MB M-REP M-LED | WP4 | FX-CAP-ATOM eight T2 = 16 unit loads; two directly injected same-geometry slots S-big cap 5 / S-small cap 3; exact max 6 units.; override: Acceptance OFF, rigid indivisible upstream M4 atoms, fresh snapshots for independent runs; exact case objective is declared in the source. | DIRECT | BRUTE + ARITH + LAYOUT + LEDGER + STATE | 01 05 09 10 17 18 21 24 | 11 30 43 44 46 | CL-07, CL-10, ED-20 | methodA (required, NOT_RUN); methodB (required, NOT_RUN); copiedPatternRepair (required, NOT_RUN); stoppedRepair (required, NOT_RUN); smallCapFour (required, NOT_RUN); slotOrder (required, NOT_RUN) |
| X-STATE-11 | P0 | Cap changes invalidate feasibility and proof while preserving legal geometry reuse | M-PHY / M-POL M-VAL M-COM | WP3 | FX-CAP-ATOM six T2 = 12 unit loads; one slot; same profile/equipment/master/dimensions, caps 7/5/7, new effective revisions.; override: Acceptance OFF, rigid indivisible upstream M4 atoms, fresh snapshots for independent runs; exact case objective is declared in the source. | DIRECT | ARITH + LAYOUT + STATE + LEDGER | 01 09 10 13 17 21 24 | 30 40 43 44 | CL-10, ED-20 | capABA (required, NOT_RUN); sameIdentityRevision (required, NOT_RUN); stalePrecommit (required, NOT_RUN) |
| X-CG-21 | P0 | A unit-load relaxation is a bound, not an all-goods packing certificate | M-OPT / M-PHY M-ORC M-VAL | WP2 | FX-CAP-ATOM seven T2 = 14 unit loads; four cap 5 slots; whole-atom minimum-truck model; exact optimum 4.; override: Acceptance OFF, rigid indivisible upstream M4 atoms, fresh snapshots for independent runs; exact case objective is declared in the source. | CORE + DIRECT | BRUTE + ARITH + LAYOUT + LEDGER + STATE | 01 03 09 10 18 19 20 | 21 43 46 | CL-04, CL-07, ED-20 | exactCapacity (required, NOT_RUN); weakBoundTrigger (required, NOT_RUN); matchingBoundStop (required, NOT_RUN); invalidQuickCandidate (required, NOT_RUN); threeSlots (required, NOT_RUN) |
| X-PAT-18 | P0 | Floor search uncertainty is distinct from a checked witness and a proof of refusal | M-PHY / M-OPT M-VAL | WP3 | Eleven rigid 1-UL TLD atoms 52x100x50in/1000 lb each; standard slot cap 64; W11 extent 572 and W10 extent 520; injected bounded-search statuses.; override: Acceptance OFF, rigid indivisible upstream M4 atoms, fresh snapshots for independent runs; exact case objective is declared in the source. | DIRECT | ARITH + LAYOUT + LEDGER + STATE | 01 03 09 10 19 20 24 | 42 45 | ED-19 | undecidedWithIncumbent (required, NOT_RUN); undecidedNoIncumbent (required, NOT_RUN); witnessCertificate (required, NOT_RUN); provedNoFitControl (required, NOT_RUN); invalidSuppliedWitness (required, NOT_RUN) |
| U-PF-09 | P0 | Shared floor-state limits, exact exhaustion and retryable Unknown | M-ORC / M-PHY M-VAL | WP7 | Instrumented search for eleven-atom candidates P/Q/R of 52/53/54x100x50in and 1000 lb each; backend costs 4/2/1 states; per-call 3/shared-route 5.; override: Acceptance OFF, rigid indivisible upstream M4 atoms, fresh snapshots for independent runs; exact case objective is declared in the source. | DIRECT | CLOCK + ARITH + STATE + LAYOUT | 03 10 13 19 21 23 24 | 38 42 45 | CL-05, ED-19 | sequentialLimits (required, NOT_RUN); exhaustedRetry (required, NOT_RUN); freshContextRetry (required, NOT_RUN); parallelAccounting (required, NOT_RUN); wallDeadlineWins (required, NOT_RUN) |

### Current requirements Section 38

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-RES-13 | P0 | Equal-service ties rotate between groups (ED-21) | M-ALC / M-ORC M-VAL | WP5 | FX-TIE: FX-CAP-ATOM T2 atoms; groups G1/G2(/G3), three atoms (six unit loads) each, all P0; one slot cap 6 per lane-date; lanes LA 9001|9004|TL, LB 9001|9003|TL, LC 9001|9002|TL; ship dates 6/7/8 Aug 2028; override: Acceptance OFF, carry/pull OFF, fresh snapshots per run, direct M5 invocation | DIRECT | ENUM-ALLOC + ARITH + LEDGER | 01 05 06 11 12 17 18 | 47 | CL-02, CL-09, ED-21 (equal-service tie rule; lane and date rotation, Tom 14/16 Nov 2026) | twoGroupsAlternate (required, NOT_RUN); threeGroupsCycle (required, NOT_RUN); shuffledInput (required, NOT_RUN); copiedLane (required, NOT_RUN); acrossLanes (required, NOT_RUN); repeatRun (required, NOT_RUN); nonTieControl (required, NOT_RUN); canonicalWithinGroup (required, NOT_RUN); renamedGroups (required, NOT_RUN); threadCountReplay (required, NOT_RUN); duplicateCandidates (required, NOT_RUN); specialGroupIds (required, NOT_RUN); fnvUnsignedOverflow (required, NOT_RUN); ineligibleGroupIgnored (required, NOT_RUN) |

### Current requirements Section 40

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| U-DA-07 | P1 | Single alternate trailer type: 40 ft high-cube container (Tom, 16 Nov) | M-INT / M-PHY M-VAL | WP8 | Container profile 473.700787 x 92.519685 x 106.220472 in (12,032 x 2,350 x 2,698 mm); 48 x 40 in stacks; FX-30 goods; override: One trailer type for the whole run | ROUTE + DIRECT | ARITH + LAYOUT + CONFIGERR | 01 15 18 23 24 | 48 | C9, SOURCE:container payload and unit-load limit (containerRun only) | mmConversion (required, NOT_RUN); rowPatterns (required, NOT_RUN); missingLimitsRejected (required, NOT_RUN); containerRun (required, BLOCKED); unknownTypeNoFallback (required, NOT_RUN) |

### Current requirements Section 42.1

| ID | P | Title | Own / collab | WP | Fixture | Entry | Oracle | INV | EC | Deps | Variants |
|---|---|---|---|---|---|---|---|---|---|---|---|
| X-PAT-19 | P0 | Literal M4 three-across 33.3-in row generation boundary | M-PHY / M-VAL M-M4 TEST | WP3 | FX-ROW-33-3 (Section 42) | CORE + VALID | ARITH + LAYOUT | 03 10 19 20 | 42 | ED-FLOOR | threeAcrossM4Cutoff (required, NOT_RUN) |
| X-CG-22 | P0 | Zero-reduced-cost duplicates and finite pricing completion | M-OPT / TEST M-ORC | WP2 | FX-ZERO-PRICING (Section 42) | CORE | BRUTE + ARITH + STATE | 01 10 18 19 | - | CL-04 | duplicateZeroCostColumns (required, NOT_RUN); nearZeroReducedCost (required, NOT_RUN) |
| X-REB-10 | P0 | Repair oscillation stops and preserves a checked incumbent | M-REP / M-VAL M-ORC M-LED | WP4 | FX-30 and injected repair toggles (Section 42) | DIRECT + VALID | STATE + LEDGER + LAYOUT | 01 10 17 18 24 | - | CL-02, CL-10 | oscillatingRepairCycle (required, NOT_RUN) |

## 6. Invariant coverage

Every applicable invariant is checked within its declared model. These mappings are coverage intent, not proof of execution.

| INV | Current definition | Tests targeting it | Count |
|---|---|---|---:|
| INV-01 | Every unit in = shipped + left over + rejected (with reason). Checked **per source line, product and group**, not only as a grand total. Goods carried to a later date are moved, not copied | ACC-1, ACC-2, PUB-01, PUB-02, PUB-03, R-1a, R-1b, R-1c, R-2, R-3a, R-3b, R-5a, RD-02, RD-03, RD-04, RD-09, RD-13, U-AC-02, U-AC-07, U-CF-01, U-CF-02, U-CF-03, U-CF-04, U-CF-05, U-CF-06, U-CF-07, U-CF-08, U-CG-01, U-CG-02, U-CG-03, U-CG-04, U-CG-05, U-CG-06, U-CG-07, U-DA-01, U-DA-02, U-DA-03, U-DA-04, U-DA-05, U-DA-07, U-FL-07, U-FL-08, U-PB-01, U-PB-02, U-PB-03, U-PB-12, U-RB-01, U-RB-02, U-RM-01, U-RM-02, U-RM-03, U-RM-04, U-RS-08, U-RS-09, U-RS-10, U-RS-11, U-RT-07, U-SP-01, U-SP-02, U-SP-03, U-SP-04, U-SP-05, X-BLK-01, X-BLK-02, X-BLK-03, X-BLK-04, X-BLK-05, X-BLK-06, X-BLK-08, X-CG-01, X-CG-02, X-CG-03, X-CG-04, X-CG-05, X-CG-06, X-CG-07, X-CG-08, X-CG-09, X-CG-10, X-CG-11, X-CG-12, X-CG-13, X-CG-14, X-CG-15, X-CG-16, X-CG-18, X-CG-19, X-CG-21, X-CG-22, X-DATA-01, X-DATA-02, X-DATA-03, X-DATA-04, X-DATA-05, X-DATA-07, X-DATA-08, X-DATA-09, X-DATA-10, X-DATA-11, X-DATA-12, X-DATA-13, X-DATA-14, X-DATE-01, X-DATE-02, X-DATE-03, X-DATE-04, X-DATE-05, X-DATE-06, X-DATE-07, X-DATE-11, X-DATE-12, X-E2E-02, X-E2E-03, X-E2E-05, X-E2E-09, X-META-01, X-META-02, X-META-03, X-META-04, X-META-05, X-META-06, X-META-07, X-PAT-01, X-PAT-02, X-PAT-03, X-PAT-04, X-PAT-05, X-PAT-10, X-PAT-16, X-PAT-17, X-PAT-18, X-REB-01, X-REB-02, X-REB-03, X-REB-04, X-REB-05, X-REB-06, X-REB-08, X-REB-09, X-REB-10, X-RES-01, X-RES-02, X-RES-03, X-RES-04, X-RES-06, X-RES-07, X-RES-09, X-RES-13, X-RUN-02, X-RUN-03, X-RUN-04, X-RUN-07, X-RUN-11, X-RUN-12, X-RUN-13, X-STATE-09, X-STATE-10, X-STATE-11 | 158 |
| INV-02 | No truck is over its weight limit: 45,000 lb by default, or the per-truck value from the input. Pallet tare counted once | ACC-2, R-1a, R-1b, R-5a, R-5b, R-6, R-8, R-9, U-PB-01, U-PB-02, U-PB-03, U-PB-04, U-PB-05, U-PB-06, U-PB-07, U-PB-08, U-PB-09, U-PB-10, U-PB-12, U-RM-03, U-RM-04, U-AC-05, U-AC-06, U-FL-01, U-FL-02, U-FL-04, X-PAT-06, X-PAT-07, X-PAT-08, X-PAT-09, X-PAT-11, X-PAT-12, X-PAT-13, X-PAT-14, X-PAT-15, X-REB-05, X-REB-06, X-REB-08, X-RES-12, X-DATA-06, X-META-10, X-E2E-03, X-E2E-06, X-E2E-09, U-FL-06, U-CF-07, U-CF-08 | 47 |
| INV-03 | Every truck has a real floor layout inside its size: 630 x 100 x 108 in by default, or the per-truck size from the input. Cube alone is not enough | ACC-2, R-1a, R-1b, R-5a, R-5b, R-6, R-8, R-9, U-AC-05, U-AC-06, U-FL-01, U-FL-02, U-FL-04, U-FL-06, U-FL-07, U-FL-10, U-PB-01, U-PB-02, U-PB-03, U-PB-04, U-PB-05, U-PB-06, U-PB-07, U-PB-08, U-PB-09, U-PB-10, U-PB-12, U-PF-09, U-RM-03, U-RM-04, X-CG-21, X-DATA-06, X-E2E-03, X-E2E-06, X-E2E-09, X-META-10, X-PAT-06, X-PAT-07, X-PAT-08, X-PAT-09, X-PAT-11, X-PAT-12, X-PAT-13, X-PAT-14, X-PAT-15, X-PAT-16, X-PAT-18, X-PAT-19, X-REB-05, X-REB-06, X-REB-08, X-RES-12 | 52 |
| INV-04 | No stack is taller than the trailer's inside height, deck included, and stacking limits are kept | ACC-2, R-1a, R-1b, R-5a, R-5b, R-6, R-8, R-9, U-PB-01, U-PB-02, U-PB-03, U-PB-04, U-PB-05, U-PB-06, U-PB-07, U-PB-08, U-PB-09, U-PB-10, U-PB-12, U-RM-03, U-RM-04, U-AC-05, U-AC-06, U-FL-01, U-FL-02, U-FL-04, X-PAT-06, X-PAT-07, X-PAT-08, X-PAT-09, X-PAT-11, X-PAT-12, X-PAT-13, X-PAT-14, X-PAT-15, X-REB-05, X-REB-06, X-REB-08, X-RES-12, X-DATA-06, X-META-10, X-E2E-03, X-E2E-06, X-E2E-09, U-FL-07, X-PAT-16 | 46 |
| INV-05 | Trucks used on a lane and ship date <= reserved on that lane and ship date, **across all groups together** | ACC-2, R-2, R-6, U-RT-01, U-RT-02, U-RT-06, U-SP-01, U-SP-02, U-SP-03, U-SP-04, U-SP-05, U-RM-05, U-AC-06, U-RB-01, U-RB-02, U-RS-01, U-RS-02, U-RS-03, U-RS-04, RD-03, RD-06, RD-07, X-ROUTE-01, X-ROUTE-02, X-ROUTE-03, X-ROUTE-04, X-ROUTE-05, X-ROUTE-10, X-CG-10, X-REB-09, X-BLK-01, X-BLK-02, X-BLK-03, X-BLK-04, X-BLK-05, X-RES-01, X-RES-02, X-RES-03, X-RES-04, X-RES-06, X-RES-07, X-RES-09, X-RES-12, X-DATE-01, X-DATE-02, X-DATE-03, X-DATE-04, X-DATE-05, X-DATE-07, X-DATE-11, X-DATE-12, X-RUN-05, X-META-08, X-META-09, X-E2E-01, X-E2E-07, U-CF-04, U-CF-05, U-CF-06, U-PF-06, U-PF-08, X-STATE-10, X-PAT-17, X-RES-13 | 64 |
| INV-06 | Two different do-not-mix groups never share a truck | R-2, R-6, U-SP-01, U-SP-02, U-SP-03, U-SP-04, U-SP-05, U-AC-03, U-AC-06, X-REB-01, X-REB-02, X-REB-03, X-RES-01, X-RES-02, X-RES-03, X-RES-04, X-RES-06, X-RES-07, X-RES-09, X-META-08, X-META-09, X-E2E-04, X-E2E-07, U-FL-08, U-CF-04, X-RES-13 | 26 |
| INV-07 | Nothing ships before its `AVAIL_DATE`, checked for **every unit inside a stack**, not just the stack | ACC-2, R-3a, R-3b, R-4a, R-4b, R-4c, U-AC-06, U-RS-06, U-RS-07, U-RS-08, U-RS-09, U-RS-10, U-RS-11, U-CF-01, U-CF-02, U-CF-03, RD-06, RD-07, X-PAT-05, X-REB-01, X-REB-02, X-REB-03, X-REB-05, X-REB-06, X-REB-08, X-RES-08, X-DATE-01, X-DATE-02, X-DATE-03, X-DATE-04, X-DATE-05, X-DATE-06, X-DATE-07, X-DATE-08, X-DATE-09, X-DATE-11, X-DATE-12, X-E2E-01, X-E2E-03, U-FL-08, U-CF-04, U-CF-05, U-CF-06, U-CF-07 | 44 |
| INV-08 | Nothing ships outside its allowed date window under the current pull-forward settings (R-3) | ACC-2, R-3a, R-3b, R-4a, R-4b, R-4c, U-AC-06, U-RS-06, U-RS-07, U-RS-08, U-RS-09, U-RS-10, U-RS-11, U-CF-01, U-CF-02, U-CF-03, RD-06, RD-07, X-PAT-05, X-REB-01, X-REB-02, X-REB-03, X-REB-05, X-REB-06, X-REB-08, X-RES-08, X-DATE-01, X-DATE-02, X-DATE-03, X-DATE-04, X-DATE-05, X-DATE-06, X-DATE-07, X-DATE-08, X-DATE-09, X-DATE-11, X-DATE-12, X-E2E-01, X-E2E-03, U-FL-08, U-CF-04, U-CF-05, U-CF-06, U-CF-07 | 44 |
| INV-09 | Every truck is planned against the lane's binding limit, and **all** other hard limits still apply, including the configured unit-load cap. Count sum(component.unitLoads) across its rigid atoms with checked integer arithmetic, not the number of stacks, source rows or pattern copies | ACC-2, R-1a, R-1b, R-5a, R-5b, R-6, R-8, R-9, U-PB-01, U-PB-02, U-PB-03, U-PB-04, U-PB-05, U-PB-06, U-PB-07, U-PB-08, U-PB-09, U-PB-10, U-PB-12, U-RM-03, U-RM-04, U-AC-05, U-AC-06, U-FL-01, U-FL-02, U-FL-04, X-PAT-06, X-PAT-07, X-PAT-08, X-PAT-09, X-PAT-11, X-PAT-12, X-PAT-13, X-PAT-14, X-PAT-15, X-REB-05, X-REB-06, X-REB-08, X-RES-12, X-DATA-06, X-META-10, X-E2E-03, X-E2E-06, X-E2E-09, X-PAT-16, X-PAT-17, X-STATE-11, X-CG-21, X-PAT-18 | 49 |
| INV-10 | Any "best possible" or "proven" claim is about the **final** truck contents, after any floor repair (R-1). A repair clears an old claim | ACC-1, ACC-3, PUB-01, PUB-02, PUB-03, R-1a, R-1b, R-1c, R-5a, R-5b, R-8, U-AC-07, U-CG-01, U-CG-02, U-CG-03, U-CG-04, U-CG-05, U-CG-06, U-CG-07, U-FF-01, U-FF-02, U-FF-03, U-FF-05, U-FL-09, U-PB-11, U-PF-09, X-CG-01, X-CG-02, X-CG-03, X-CG-04, X-CG-05, X-CG-06, X-CG-07, X-CG-08, X-CG-09, X-CG-11, X-CG-12, X-CG-13, X-CG-14, X-CG-15, X-CG-16, X-CG-17, X-CG-18, X-CG-19, X-CG-20, X-CG-21, X-CG-22, X-E2E-02, X-META-01, X-META-02, X-META-07, X-PAT-16, X-PAT-17, X-PAT-18, X-PAT-19, X-REB-05, X-REB-06, X-REB-08, X-REB-10, X-STATE-11 | 60 |
| INV-11 | Compare complete feasible allocations using the declared full CL-02 or fixture comparator and date/priority policy, never only the best item or a stale pre-repair score. Exact Tom regressions and small oracle fixtures must achieve their stated optimum. An interrupted/larger result may be a checked incumbent only under the declared stop/commit policy and labeled unproven unless a same-objective certificate exists. If production requires proven priority optimality before dispatch, an unproven incumbent cannot commit. Comparing only the feasible alternatives found is not proof of global optimality | R-2, R-4a, R-4b, R-4c, R-6, R-7, U-RB-05, U-RS-03, U-RS-04, U-RS-06, U-RS-07, U-CF-01, U-CF-02, RD-13, X-REB-07, X-BLK-06, X-RES-01, X-RES-02, X-RES-03, X-RES-04, X-RES-06, X-RES-07, X-RES-08, X-RES-09, X-DATE-08, X-STATE-08, X-RUN-05, X-META-08, X-META-09, X-E2E-02, X-E2E-07, U-CF-04, U-CF-07, U-CF-08, X-CG-20, X-RES-13 | 36 |
| INV-12 | For runs that complete, or that stop by an agreed deterministic rule (for example a work count), the same input gives the same canonical plan on every run and thread count. Timings and timestamps are excluded. For wall-clock-limited runs, CL-09 applies: the output is valid, its status is honest, and it repeats as far as the agreed rule says (U-PF-03, X-RUN-01) | ACC-2, R-7, U-RS-03, U-RS-04, U-PF-01, U-PF-02, U-PF-03, U-PF-05, X-STATE-01, X-STATE-02, X-STATE-03, X-STATE-04, X-STATE-05, X-STATE-06, X-STATE-07, X-RUN-01, X-RUN-09, X-RUN-10, X-META-03, X-META-04, X-META-05, X-META-06, X-RES-13 | 23 |
| INV-13 | Every run can be recreated from what was recorded: inputs, master, config, commit, thread count, stop rule | ACC-2, R-7, U-RS-03, U-RS-04, U-PF-01, U-PF-02, U-PF-03, U-PF-05, X-STATE-01, X-STATE-02, X-STATE-03, X-STATE-04, X-STATE-05, X-STATE-06, X-STATE-07, X-RUN-01, X-RUN-09, X-RUN-10, X-META-03, X-META-04, X-META-05, X-META-06, U-PF-07, U-PF-08, X-STATE-10, X-STATE-11, U-PF-09 | 27 |
| INV-14 | Fullness and the optional minimum-load policy. Production profile (this customer): M5 withholds no truck for a minimum load; every dispatched truck carries current fullness reasons/status, weight %, rear inches and report bucket per CL-03/0.8, on final contents. Incomplete no-fit diagnostics are explicit, never a false FULL/NOT FULL conclusion. When a test selects the optional acceptance policy, every dispatched truck passes weight > x OR cube > y on final contents, first/middle/last and all groups alike, and withheld cargo stays accounted for with its reason | ACC-2, R-2, R-4a, R-4b, R-4c, U-RM-03, U-RM-04, U-RM-05, U-AC-01, U-AC-02, U-AC-03, U-AC-04, U-AC-05, U-AC-06, U-AC-07, U-AC-08, U-FL-01, U-FL-02, U-FL-03, U-FL-04, U-FL-05, RD-03, RD-12, X-CG-19, X-REB-04, X-REB-07, X-REB-09, X-RES-12, X-REPORT-04, X-RUN-12, X-E2E-03, X-E2E-04, X-E2E-09, U-AC-09, U-FL-06, U-FL-07, U-FL-08, U-FL-09, U-FL-10, U-CF-04, U-CF-07, X-STATE-09, X-REPORT-10 | 43 |
| INV-15 | Every lane M5 cannot plan is named with a reason. "No result yet", "timed out" and "impossible" are reported differently | ACC-3, R-1c, U-RT-04, U-RT-05, U-PB-08, U-PB-09, U-PB-10, U-AC-04, U-AC-06, U-AC-07, U-RB-03, U-RB-04, U-CG-06, U-CG-07, U-FF-04, U-RS-02, U-RS-05, U-DA-01, U-DA-02, U-DA-03, U-DA-04, U-DA-05, U-DA-06, U-PF-05, U-FL-03, X-ROUTE-06, X-ROUTE-08, X-CG-01, X-CG-02, X-CG-03, X-CG-04, X-CG-05, X-CG-06, X-CG-07, X-CG-08, X-CG-09, X-CG-11, X-CG-12, X-CG-13, X-CG-14, X-CG-16, X-CG-18, X-PAT-01, X-PAT-02, X-PAT-03, X-PAT-04, X-BLK-07, X-BLK-08, X-RES-05, X-RES-08, X-RES-10, X-RES-11, X-DATA-03, X-DATA-04, X-DATA-05, X-DATA-10, X-DATA-12, X-DATA-13, X-DATA-14, X-RUN-02, X-RUN-03, X-RUN-04, X-RUN-06, X-RUN-07, X-RUN-08, X-RUN-11, X-RUN-12, X-RUN-13, X-META-10, X-E2E-05, U-CF-08, U-PF-06, U-DA-07 | 73 |
| INV-16 | Production only: for an independent lane that M5 does not touch, with the same M4 hand-over input, M4's result stays the same. Forced benchmark runs are excluded. A related date may change through authorised carry or pull-forward; such changes must reconcile (RD-04, RD-09) | ACC-2, U-RT-01, U-RT-02, U-RT-03, U-RT-06, U-RT-07, U-AC-08, U-RB-04, U-CF-03, RD-01, RD-04, RD-05, RD-06, RD-07, RD-08, RD-09, X-ROUTE-01, X-ROUTE-02, X-ROUTE-03, X-ROUTE-04, X-ROUTE-05, X-ROUTE-07, X-ROUTE-08, X-ROUTE-09, X-ROUTE-10, X-BLK-08, X-RES-11, X-STATE-01, X-STATE-02, X-STATE-03, X-STATE-04, X-STATE-05, X-STATE-06, X-STATE-07, X-RUN-02, X-RUN-03, X-RUN-04, X-RUN-07, X-RUN-11, X-RUN-13, X-E2E-01, X-E2E-05, U-CF-05, U-CF-06, U-PF-08, X-STATE-09 | 46 |
| INV-17 | Trying candidates (method A, method B, repairs) does not use up real reservations or goods. Only the chosen plan is committed | ACC-2, RD-01, RD-04, RD-05, RD-06, RD-07, RD-08, RD-09, U-AC-08, U-CF-03, U-CF-04, U-CF-06, U-PF-07, U-RB-04, U-RT-01, U-RT-02, U-RT-03, U-RT-06, U-RT-07, X-BLK-08, X-E2E-01, X-E2E-05, X-PAT-17, X-REB-10, X-RES-11, X-RES-13, X-ROUTE-01, X-ROUTE-02, X-ROUTE-03, X-ROUTE-04, X-ROUTE-05, X-ROUTE-07, X-ROUTE-08, X-ROUTE-09, X-ROUTE-10, X-RUN-02, X-RUN-03, X-RUN-04, X-RUN-07, X-RUN-11, X-RUN-13, X-STATE-01, X-STATE-02, X-STATE-03, X-STATE-04, X-STATE-05, X-STATE-06, X-STATE-07, X-STATE-09, X-STATE-10, X-STATE-11 | 51 |
| INV-18 | A chosen plan has whole, non-negative counts of trucks, patterns and unit loads | ACC-1, ACC-2, PUB-01, PUB-02, PUB-03, R-1a, R-1b, R-1c, R-2, R-3a, R-3b, R-5a, RD-02, RD-03, RD-04, RD-09, RD-13, U-AC-02, U-AC-07, U-CF-01, U-CF-02, U-CF-03, U-CF-07, U-CF-08, U-CG-01, U-CG-02, U-CG-03, U-CG-04, U-CG-05, U-CG-06, U-CG-07, U-DA-01, U-DA-02, U-DA-03, U-DA-04, U-DA-05, U-DA-07, U-PB-01, U-PB-02, U-PB-03, U-PB-12, U-RB-01, U-RB-02, U-RM-01, U-RM-02, U-RM-03, U-RM-04, U-RS-08, U-RS-09, U-RS-10, U-RS-11, U-RT-07, U-SP-01, U-SP-02, U-SP-03, U-SP-04, U-SP-05, X-BLK-01, X-BLK-02, X-BLK-03, X-BLK-04, X-BLK-05, X-BLK-06, X-BLK-08, X-CG-01, X-CG-02, X-CG-03, X-CG-04, X-CG-05, X-CG-06, X-CG-07, X-CG-08, X-CG-09, X-CG-10, X-CG-11, X-CG-12, X-CG-13, X-CG-14, X-CG-15, X-CG-16, X-CG-18, X-CG-19, X-CG-21, X-CG-22, X-DATA-01, X-DATA-02, X-DATA-03, X-DATA-04, X-DATA-05, X-DATA-07, X-DATA-08, X-DATA-09, X-DATA-10, X-DATA-11, X-DATA-12, X-DATA-13, X-DATA-14, X-DATE-01, X-DATE-02, X-DATE-03, X-DATE-04, X-DATE-05, X-DATE-06, X-DATE-07, X-DATE-11, X-DATE-12, X-E2E-02, X-E2E-03, X-E2E-05, X-E2E-09, X-META-01, X-META-02, X-META-03, X-META-04, X-META-05, X-META-06, X-META-07, X-PAT-01, X-PAT-02, X-PAT-03, X-PAT-04, X-PAT-05, X-PAT-10, X-PAT-16, X-PAT-17, X-REB-01, X-REB-02, X-REB-03, X-REB-04, X-REB-05, X-REB-06, X-REB-08, X-REB-09, X-REB-10, X-RES-01, X-RES-02, X-RES-03, X-RES-04, X-RES-06, X-RES-07, X-RES-09, X-RES-13, X-RUN-02, X-RUN-03, X-RUN-04, X-RUN-07, X-RUN-11, X-RUN-12, X-RUN-13, X-STATE-10 | 150 |
| INV-19 | Any gap identifies its objective, required goods and constraints. Its lower bound must be valid for that objective/model; a documented relaxation may supply a valid weaker bound but does not prove feasibility. A count gap cannot certify priority-optimal partial service. If no valid bound exists, show not available; never fabricate one from a restricted-master objective. | ACC-1, ACC-3, PUB-01, PUB-02, PUB-03, R-1a, R-1b, R-1c, R-5a, R-5b, R-8, U-AC-07, U-CG-01, U-CG-02, U-CG-03, U-CG-04, U-CG-05, U-CG-06, U-CG-07, U-FF-01, U-FF-02, U-FF-03, U-FF-05, U-FL-09, U-PB-11, U-PF-09, X-CG-01, X-CG-02, X-CG-03, X-CG-04, X-CG-05, X-CG-06, X-CG-07, X-CG-08, X-CG-09, X-CG-11, X-CG-12, X-CG-13, X-CG-14, X-CG-15, X-CG-16, X-CG-17, X-CG-18, X-CG-19, X-CG-20, X-CG-21, X-CG-22, X-E2E-02, X-META-01, X-META-02, X-META-07, X-PAT-18, X-PAT-19, X-REB-05, X-REB-06, X-REB-08 | 56 |
| INV-20 | A cube or weight number never stands in for a real floor layout | ACC-2, R-1a, R-1b, R-5a, R-5b, R-6, R-8, R-9, U-AC-05, U-AC-06, U-FL-01, U-FL-02, U-FL-04, U-FL-10, U-PB-01, U-PB-02, U-PB-03, U-PB-04, U-PB-05, U-PB-06, U-PB-07, U-PB-08, U-PB-09, U-PB-10, U-PB-12, U-RM-03, U-RM-04, X-CG-21, X-DATA-06, X-E2E-03, X-E2E-06, X-E2E-09, X-META-10, X-PAT-06, X-PAT-07, X-PAT-08, X-PAT-09, X-PAT-11, X-PAT-12, X-PAT-13, X-PAT-14, X-PAT-15, X-PAT-16, X-PAT-18, X-PAT-19, X-REB-05, X-REB-06, X-REB-08, X-RES-12 | 49 |
| INV-21 | Reuse a saved physical pattern only after validating its trailer, unit-load cap, per-atom unit-load composition, product master and stacking assumptions. Check stock, source dates and customer acceptance again on every copy. A physical cache hit never reuses a stale customer/profile acceptance decision | U-AC-08, X-ROUTE-09, X-PAT-08, X-PAT-09, X-RES-12, X-DATE-09, X-DATA-06, X-E2E-06, U-FL-06, U-FL-08, U-FL-09, X-STATE-09, X-PAT-16, X-PAT-17, X-STATE-11, U-PF-09 | 16 |
| INV-22 | Report totals add up by lane, date, group and method. Results of method A and B are never added together | U-RB-04, U-RS-02, U-RP-01, U-RP-02, U-RP-03, U-RP-04, U-RP-05, U-RP-06, U-RP-07, U-FL-05, RD-10, RD-11, X-RES-10, X-STATE-01, X-STATE-02, X-STATE-03, X-STATE-04, X-STATE-05, X-STATE-06, X-STATE-07, X-REPORT-01, X-REPORT-02, X-REPORT-03, X-REPORT-04, X-REPORT-05, X-REPORT-06, X-REPORT-07, X-REPORT-08, X-REPORT-09, X-E2E-01, X-E2E-08, U-FL-08, X-REPORT-10 | 33 |
| INV-23 | Bad numbers (negative, NaN, infinity, overflow) never turn into a valid-looking truck, gap, penalty or fill | ACC-3, R-1c, R-9, U-RT-04, U-RT-05, U-PB-08, U-PB-09, U-PB-10, U-AC-04, U-AC-06, U-AC-07, U-RB-03, U-RB-04, U-CG-06, U-CG-07, U-FF-04, U-RS-02, U-RS-05, U-DA-01, U-DA-02, U-DA-03, U-DA-04, U-DA-05, U-DA-06, U-PF-05, U-FL-03, X-ROUTE-06, X-ROUTE-08, X-CG-01, X-CG-02, X-CG-03, X-CG-04, X-CG-05, X-CG-06, X-CG-07, X-CG-08, X-CG-09, X-CG-11, X-CG-12, X-CG-13, X-CG-14, X-CG-16, X-CG-18, X-PAT-01, X-PAT-02, X-PAT-03, X-PAT-04, X-BLK-07, X-BLK-08, X-RES-05, X-RES-08, X-RES-10, X-RES-11, X-DATA-03, X-DATA-04, X-DATA-05, X-DATA-10, X-DATA-12, X-DATA-13, X-DATA-14, X-RUN-02, X-RUN-03, X-RUN-04, X-RUN-06, X-RUN-07, X-RUN-08, X-RUN-11, X-RUN-12, X-RUN-13, X-META-10, X-E2E-05, U-AC-09, U-FL-06, U-FL-10, U-PF-06, X-PAT-16, U-PF-09, U-DA-07 | 78 |
| INV-24 | A timeout, cancel or error never leaves a half-built pattern, a reservation used twice, or an unchecked repair in the output | ACC-3, R-1c, U-AC-04, U-AC-06, U-AC-07, U-CF-06, U-CF-08, U-CG-06, U-CG-07, U-DA-01, U-DA-02, U-DA-03, U-DA-04, U-DA-05, U-DA-06, U-DA-07, U-FF-04, U-FL-03, U-FL-09, U-PB-08, U-PB-09, U-PB-10, U-PF-05, U-PF-06, U-PF-07, U-PF-08, U-PF-09, U-RB-03, U-RB-04, U-RS-02, U-RS-05, U-RT-04, U-RT-05, X-BLK-07, X-BLK-08, X-CG-01, X-CG-02, X-CG-03, X-CG-04, X-CG-05, X-CG-06, X-CG-07, X-CG-08, X-CG-09, X-CG-11, X-CG-12, X-CG-13, X-CG-14, X-CG-16, X-CG-18, X-DATA-03, X-DATA-04, X-DATA-05, X-DATA-10, X-DATA-12, X-DATA-13, X-DATA-14, X-E2E-05, X-META-10, X-PAT-01, X-PAT-02, X-PAT-03, X-PAT-04, X-PAT-17, X-PAT-18, X-REB-10, X-RES-05, X-RES-08, X-RES-10, X-RES-11, X-ROUTE-06, X-ROUTE-08, X-RUN-02, X-RUN-03, X-RUN-04, X-RUN-06, X-RUN-07, X-RUN-08, X-RUN-11, X-RUN-12, X-RUN-13, X-STATE-09, X-STATE-10, X-STATE-11 | 84 |

## 7. Edge-case checklist coverage

| EC | Current definition | Covered by | Owning WPs |
|---|---|---|---|
| EC-01 | Lane-date with exactly 20 reserved | U-RT-01, X-ROUTE-01 | WP8 |
| EC-02 | Lane-date with 21 reserved | U-RT-02, X-ROUTE-01 | WP8 |
| EC-03 | Lane over threshold on one date, under on the other | U-SP-01, X-ROUTE-03 | WP8 |
| EC-04 | Large lane with zero reservations | U-RT-06 | WP8 |
| EC-05 | Large lane that is weight-bound | U-PB-08, X-PAT-14 | WP3, WP4 |
| EC-06 | Product that fills exactly N trucks | U-PB-01, X-PAT-01 | WP4 |
| EC-07 | Every product has less than one truck | U-PB-03 | WP4 |
| EC-08 | Only one product on the lane | U-CG-03 | WP2 |
| EC-09 | A truck is below fullness, or fails the optional acceptance policy, including a donor or repaired truck | U-RM-03, U-RM-04, U-AC-02, U-AC-07, X-REB-04, X-REB-09 | WP4 |
| EC-10 | More pattern copies than reservations | U-RS-03, X-RES-02 | WP5 |
| EC-11 | Floor repair removes goods from a proven or accepted truck | R-1c, U-PB-11, U-AC-07, X-REB-05, X-PAT-17 | WP4 |
| EC-12 | Low-priority group beats high-priority group on fill | R-2, X-RES-06 | WP5 |
| EC-13 | Mixed-date stack with pull-forward off | R-3a, X-REB-06 | WP3, WP4 |
| EC-14 | Overdue low-priority vs current high-priority | R-4a, R-4b, R-4c, X-DATE-08 | WP5 |
| EC-15 | Priority tie | U-RS-04 | WP5 |
| EC-16 | TPRIO outside 0-11 | U-RS-05 | WP1 |
| EC-17 | Item exactly trailer-sized / one unit too big | U-CG-04, U-CG-05, X-PAT-13 | WP2, WP3 |
| EC-18 | Unit load taller or heavier than the trailer | U-PB-09, U-PB-10 | WP4 |
| EC-19 | DIS units / wrong unit of measure | U-DA-01, U-DA-02, X-DATA-08 | WP3, WP8 |
| EC-20 | Product missing from the master | U-DA-03 | WP8 |
| EC-21 | Iteration cap, timeout, zero-dispatch result, changing candidate counts or wrong-objective bound | U-CG-06, U-CG-07, X-CG-11, X-CG-12, X-RUN-11, X-RUN-12, X-RUN-13, X-CG-20, X-CG-21 | WP2, WP7 |
| EC-22 | Method A and B tie | U-RB-05, X-STATE-08 | WP5, WP7 |
| EC-23 | Raw-material lane over threshold | U-RT-07 | WP8 |
| EC-24 | Parked feature appears in data | U-DA-06, X-RUN-08 | WP8 |
| EC-25 | Same input, different thread counts | U-PF-03, X-RUN-01 | WP7 |
| EC-26 | Single-unit do-not-mix group | U-SP-04 | WP8 |
| EC-27 | Goods available only after the ship date | U-RS-11, X-DATE-02 | WP3 |
| EC-28 | Two groups share one lane-date's reservations | X-ROUTE-02, X-BLK-04, X-RES-01 | WP5, WP6, WP8 |
| EC-29 | Same product split across lines with different dates or priorities | X-PAT-05, X-DATA-07 | WP3, WP4 |
| EC-30 | Saved pattern reused with a different trailer, master or customer acceptance configuration | U-AC-08, X-PAT-08, X-PAT-09, X-DATA-06, X-PAT-17, X-STATE-11 | WP3, WP4, WP7 |
| EC-31 | Several do-not-mix groups each with a part truck | X-E2E-04 | WP4 |
| EC-32 | A block of method B uses up reservations needed by urgent goods | X-RUN-05, U-CF-04, X-CG-20 | WP6, WP7, WP8 |
| EC-33 | Exact fullness/strict-predicate boundaries masked by report or packing rounding | U-AC-09, U-FL-06 | WP1, WP3 |
| EC-34 | Empty or interrupted residual-fit checks incorrectly claim floor limitation | U-FL-04, U-FL-08, U-FL-09 | WP3 |
| EC-35 | Rear space measured before legal stacking or from the wrong layout extent | U-FL-07, U-FL-10 | WP3 |
| EC-36 | Future can-go cargo consumes scarce slots needed by feasible required cargo | U-CF-04, U-CF-07, U-CF-08 | WP8 |
| EC-37 | Can-go-only date or selected/unselected future draw changes later M4 input | U-CF-03, U-CF-05, U-CF-06 | WP8 |
| EC-38 | Budget multiplication/clock overflow, parallel reset, or cross-date pooling | U-FL-09, U-PF-06, U-PF-07, U-PF-08, U-PF-09 | WP1, WP3, WP7 |
| EC-39 | Optional acceptance ON/OFF state leaks into customer planning or reports | U-AC-08, X-STATE-09 | WP7 |
| EC-40 | Duplicate/concurrent/conflicting commit or stale snapshot replays source units | X-RUN-07, U-CF-06, X-STATE-10, X-STATE-11 | WP3, WP7, WP8 |
| EC-41 | Bucket averages or overlapping FULL reasons double-count trucks | U-FL-05, X-REPORT-10 | WP8 |
| EC-42 | A refusal by a heuristic or exhausted floor search (rows not proven not to fit) backs a proven or optimal claim, or a valid supplied witness is incorrectly rejected for incomplete search | R-5a, R-5b, R-8, U-PF-09, X-PAT-18, X-PAT-19 | 6 |
| EC-43 | The unit-load cap is taken as a universal 64 (or raised for every customer) instead of the customer or equipment limit | R-9, X-PAT-16, X-PAT-17, X-STATE-11, X-CG-21 | WP2, WP3, WP4 |
| EC-44 | Component unit loads are confused with stacks, rows or pattern copies; equal geometry with different composition shares a cap-constrained maximum; repaired or reassigned cargo exceeds its actual slot cap | X-PAT-16, X-PAT-17, X-STATE-11 | WP3, WP4 |
| EC-45 | Per-search or shared route floor-state limits reset or are bypassed; cached Undecided becomes a proof or a permanent refusal on a fresh permitted run | X-PAT-18, U-PF-09 | WP3, WP7 |
| EC-46 | A relaxed unit-load count bound is mistaken for a feasible packing when indivisible atoms or different slot caps prevent that truck count | X-PAT-16, X-PAT-17, X-CG-21 | WP2, WP3, WP4 |
| EC-47 | An equal-service tie always goes to the same group: a fixed group order or input order decides it, the rotation ignores the lane or the date, or a fill or truck-count rule silently replaces ED-21 | X-RES-13 | WP5 |
| EC-48 | A trailer type with no payload or no unit-load limit silently takes the 53F defaults (45,000 lb, 64), or its mm sizes are rounded up instead of down | U-DA-07 | WP8 |

An EC row closes only after all selected applicable required variants have current execution evidence.

## 8. CL coverage

Current source Section 0.3 supplies the machine-readable clarification register. Parent and scoped variant dependencies are unioned for coverage only; their execution scope remains explicit.

| CL | Current source definition | Tests depending on it | Count |
|---|---|---|---:|
| CL-01 | Optional minimum-load predicate: weight > x lb OR cube > y cu ft, strict, full precision (form from Q8). For this customer the check runs at export to the host system, AFTER M5 (Tom's follow-up answer 3): the production profile has the predicate OFF and M5 withholds nothing for it. The predicate stays as an optional policy seam. FX-30 supplies synthetic x = 40,000 and y = 3,000 only where a test selects the policy. Missing x/y is not a blocker for this customer. A later customer that needs the rule inside M5 records its values, units and profile then | ACC-2, R-2, U-PB-12, U-RM-03, U-RM-04, U-RM-05, U-AC-01, U-AC-02, U-AC-03, U-AC-04, U-AC-05, U-AC-06, U-AC-07, U-AC-08, U-RP-01, RD-03, RD-06, RD-07, X-CG-19, X-REB-04, X-REB-07, X-REB-09, X-RES-12, X-REPORT-04, X-E2E-01, X-E2E-02, X-E2E-04, X-E2E-09, U-AC-09, X-STATE-09 | 30 |
| CL-02 | Complete-allocation comparison uses the corrected M4 urgency classes (rank date, priority, ship date, with rankDateOf under the effective lane order), confirmed required-before-can-go precedence, and feasible group candidates. Priority/service and use of usable reservations precede best-effort fill; do not prefer fewer trucks by itself. Equal-service ties are broken by ED-21 (Section 0.8; Tom, 14 and 16 Nov 2026): a group order that rotates with the lane and the ship date, then canonical records. Fill, rear space, weight and truck count never break such a tie; they are reported. No synthetic fewer-trucks fallback. An unproven checked incumbent may commit labelled Unproven. Linked-date comparisons preserve source identities and original due dates (RD-13) | ACC-2, R-2, R-4a, R-4b, R-4c, R-6, RD-13, U-CF-01, U-CF-02, U-CF-04, U-CF-05, U-CF-07, U-CF-08, U-RB-04, U-RB-05, U-RM-03, U-RS-03, U-RS-04, U-RS-06, U-RS-07, X-BLK-06, X-CG-20, X-E2E-02, X-E2E-07, X-META-03, X-META-04, X-META-05, X-META-06, X-REB-07, X-REB-10, X-RES-01, X-RES-02, X-RES-03, X-RES-04, X-RES-06, X-RES-07, X-RES-09, X-RES-13, X-RUN-05, X-RUN-13, X-STATE-07, X-STATE-08 | 42 |
| CL-03 | Fullness labels: weight >= 98% of the actual trailer limit, or exact rear space <= the configured limit after legal M4/M2 stacking, or an evidenced no-fit conclusion for the declared supported layout and residual scope. Default rear limit 14 in was stated to Tom and remains configurable. Section 0.8 records the engineering no-fit, incomplete-evidence and bucket definitions. No-goods alone is not a floor-capacity proof. Labels never withhold trucks; no last-truck minimum | ACC-2, R-2, U-PB-12, U-RM-03, U-RM-04, U-AC-02, U-AC-03, U-AC-05, U-AC-07, U-AC-08, U-RP-01, U-RP-02, U-RP-07, U-FL-01, U-FL-02, U-FL-03, U-FL-04, U-FL-05, RD-03, RD-10, RD-12, X-REB-09, X-RES-12, X-REPORT-04, X-E2E-01, X-E2E-02, X-E2E-04, X-E2E-09, U-FL-06, U-FL-07, U-FL-08, U-FL-09, U-FL-10, X-STATE-09, X-REPORT-10 | 35 |
| CL-04 | Q6 answered: for a feasible all-required-goods quick result F and valid integer lower bound B for the same objective, goods and model, run the careful count method when F > B and allowance remains; equality stops that count phase. Keep the best checked plan at the limit. FX-GAP G = 1 with >= is equivalent for integer truck counts. Scope equality certificates to that objective; a bound on a shipped subset or count relaxation is not a proof of priority, can-go, fill or full-service optimality. Report named gap denominator or N/A when unavailable/zero | ACC-3, U-FF-01, U-FF-02, U-FF-03, U-FF-04, U-FF-05, U-RP-03, X-CG-17, X-CG-20, X-CG-21, X-CG-22 | 11 |
| CL-05 | Time limit: seconds per LOAD, set by the user (Tom). Contract stated to Tom, applies unless he objects: lane-date allowance = secondsPerLoad x reserved trucks of that lane-date, declared before search, shared by quick and careful methods, blocks, repair and final checks, never enlarged by candidate or final truck counts. Report time per truck, group, lane-date and total, the loaded truck count, and average or N/A. Example at 1 s/load: 7 Aug 21 s, 8 Aug 20 s | ACC-2, U-RB-04, U-RP-04, U-RP-05, U-PF-01, U-PF-02, U-PF-05, RD-06, RD-07, X-BLK-07, X-RUN-02, X-RUN-04, X-RUN-10, X-RUN-11, X-RUN-12, X-RUN-13, U-FL-09, U-PF-06, U-PF-07, U-PF-08, U-PF-09 | 21 |
| CL-06 | Tom's post-delivery decision (19 Nov 2026): `m5.penaltyPerUnusedTruck` starts at $200 per unused reserved truck and may be overridden. The report charges the rate once per unused reservation on each lane-date. `null` means unknown; zero is explicit. The rate is for reporting. See architecture Section 22.11 | U-RM-05, U-RS-02, X-RES-10, X-STATE-02 | 4 |
| CL-07 | Packing atom and permitted splitting: recover M4's supported contract. FX-30 units and the block fixture atoms are indivisible; stacking never permits silently splitting source units. Do not add an unapproved single-pallet filler path. A test requiring a different atom declares it | U-PB-04, U-PB-05, U-PB-06, U-PB-07, X-PAT-06, X-PAT-07, X-PAT-11, X-PAT-12, X-PAT-13, X-PAT-14, X-PAT-15, U-FL-07, X-PAT-16, X-PAT-17, X-CG-21 | 15 |
| CL-08 | Routing is a performance choice (Q1). Keep reserved trucks per lane-date > T, T = 20, configurable; set the final T from measured lane times. Baseline evidence: M4's time is in small knapsack lanes at the node limit (one Aug6 route 12.83 s at f9d9945, 10.36 s at e51d9c4); the escalated lane costs no M4 time | ACC-2, U-RT-01, U-RT-02, U-RT-03, U-RT-04, U-RT-05, U-RT-06, U-RT-08, U-RP-04, U-PF-02, RD-01, RD-05, X-ROUTE-01, X-ROUTE-02, X-ROUTE-03, X-ROUTE-04, X-ROUTE-05, X-ROUTE-06, X-ROUTE-07, X-ROUTE-10, U-CF-05, U-PF-08 | 22 |
| CL-09 | Repeatability: completed/deterministically stopped fixtures use the recorded canonical comparator and seeds. Wall-clock runs must be valid and honestly labeled; do not promise identical cargo across thread counts unless the recorded implementation contract guarantees it. Freeze this engineering contract for acceptance | U-PF-03, X-RUN-01, U-PF-07 | 3 |
| CL-10 | Commit/rollback contract: provisional work, validated candidate blocks, selected result and operational commitment are separate. Candidate validation includes acceptance, physical, dates, groups, sources and reservations. Synthetic keep-validated and whole-lane-rollback variants test the seam; they do not require both production modes. Before any commit, retries use the full original snapshot. After an actual partial commit, use a reconciled residual snapshot or idempotency mechanism, never replay committed goods. Record the chosen production contract | ACC-2, U-CF-06, U-PF-07, X-BLK-08, X-E2E-05, X-PAT-17, X-REB-10, X-RES-08, X-RES-11, X-RUN-03, X-RUN-07, X-STATE-09, X-STATE-10, X-STATE-11 | 14 |

## 9. Test layers and independent verification

Layer 1 - scalar core (CORE, FX-COUNT): exact solver cases, bounds, certificates, X-CG, U-CG, X-META, PUB. Oracle: brute force
  (`m5BruteForcePacker`) for <= 10 items, exhaustive multiset generator for X-META-01, published optimum plus `m5PackingChecker`
  for PUB. The checker shares no code with `columnGeneration`.
Layer 2 - physical and acceptance (CORE/VALID/POLICY): layout witnesses checked by `m5LayoutChecker` (independent rectangle and
  height checks, not floorPattern), acceptance truth tables computed exactly in the test, config error cases.
Layer 3 - allocation and methods (DIRECT): FX-30/FX-BLOCK lanes; `m5AllocationEnumerator` enumerates complete feasible
  allocations for <= 3 reservations and <= 60 units (oracle limit; production ExactLimits are separate, ED-12) and ranks them with the case comparator. The production comparator is never
  the only oracle for its own result.
Layer 4 - orchestration, budget, commit (DIRECT with FakeClock, fault injection, commit seam): state, ledger and clock oracles.
Layer 5 - routing, integration, report (ROUTE/FORCED/REPORT): selector seam behaviour, report parsing, data hand-over.
Layer 6 - real data and performance (REAL/BENCH): reproduced baselines with recorded identity; synthetic-policy diagnostics labelled.

Injection points (all PROPOSED, compiled only into ob_tests via `M5_TEST_HOOKS`): `IClock` (FakeClock), `WorkCounter`,
`CancellationToken`, `RepairFaultInjector` (mutate after validation, interrupt after mutation), `ProfileProvider` (switch
profile/revision), `CommitSink` (record or fail commits), `MethodStub` (controlled A/B outputs for X-RUN-13, X-BLK-06),
`forceRoute`. Concurrency: X-STATE-07 injects competing group-candidate interleavings within the same lane-date reservation pool. Independent lane-date pools remain separate; this is not a requirement to add production parallelism; X-RUN-01/U-PF-03 run 1/2/8 threads with a work-count stop.

Exact arithmetic at strict boundaries: acceptance compares exact totals (architecture Section 9.3). Tests construct values
that are exactly representable (FX-30 125 cu ft units; 500/9 cu ft display units compared as rationals) and also check
that report rounding never changes a decision (X-REPORT-04, X-E2E-09).

Generated tests keep seeds in the evidence record and write every failing input to `tests/m5/generated/failures/` as a
minimal JSON case that becomes a permanent regression.

Equivalent valid packings are accepted where v8 does not demand a canonical arrangement (X-RES-07, X-META-02/05). Assert
feasibility, truck count, goods shipped and comparator key; assert exact assignment only under a recorded canonical tie rule.

Reusing corrected M4 validation inside M5 is allowed and expected. Every P0 allocation/solver test uses a matching independent oracle: small scalar BRUTE, bounded physical LAYOUT-ENUM, small general ENUM-ALLOC, or CLASSCOUNT for homogeneous groups/whole-truck atoms. The scalar <=10-item and general allocation <=3-slot/<=60-unit limits do not cover physical 14-stack or 21-slot fixtures. Larger homogeneous X-RES/X-E2E fixtures use the separately bounded class/group-count oracle with independently established per-truck capacity; they do not silently enlarge the general unit enumerator. Never silently label an unsupported oracle run as PASS.

## 10. Missing fixtures, capabilities and policy dependencies

| Dependency | Tests | Effect |
|---|---|---|
| CAPABILITY:per-truck trailer capability not in input; seam-level test (B4, C9) | X-RES-12, X-E2E-06 | Run at the physical/allocation seam |
| CAPABILITY:per-truck trailer size/weight in input (NOT SUPPORTED at 7cef94e, C9) | U-DA-05 | Tom, 14 Nov: refusing a mixed-trailer lane-date is acceptable for now; a truck-type availability feed is future scope. U-DA-05 input-feed variant remains conditional/BLOCKED; its adapter-seam variant runs. This input capability is not a dependency of R-9 global-cap or explicit direct-slot testing. |
| DATA:TPRIO 99 (historical open question) | U-RS-05 | U-RS-05 keeps rejecting > 11; not one of the five pending items |
| DATA:Tom e6dbb0f review files committed under tests/fixtures/tomReview/e6dbb0f/ (Saif; synthetic) | R-5a, R-8, R-9 | The latest supplied review reports these exact synthetic files available. This review has only their document record. Commit/verify unchanged bytes and hashes before executing R-5a/R-8/R-9; unavailable bytes at execution make that variant BLOCKED. A prose reconstruction is not the fixture. |
| DATA:baseline committed under docs/m5/baselines (B1) | RD-02, X-REPORT-03 | Baseline exists (m4BaselineForM5.txt); assert after Saif commits the files |
| DATA:baseline committed under docs/m5/baselines (B15) | RD-04 | 8 Aug baseline 20 trucks / 678 unit loads |
| DATA:binpack1.txt committed with SHA-256 | U-CG-02 |  |
| DATA:binpack1.txt committed with recorded SHA-256 | PUB-01, PUB-02, PUB-03 | Required bytes/hash must be verified in the target repository before execution. The supplied documents report an earlier downloaded copy but do not supply it. If unavailable at execution, mark the affected variant BLOCKED; never substitute a shortened list or invent the hash |
| DATA:exact August linked-date inputs, master/config and compared baseline lane-date evidence (RD-13/B30) | RD-13 | Verify the exact dependency and its declared scope before execution; unavailable required inputs produce BLOCKED, never invented PASS. |
| IMPLEMENTED:M4 per-lane pastDueOrder setting on the M5 branch (`selection.pastDueOrderByLane`, off by default), awaiting Tom's confirmation | R-7 | R-7 m4Path/m5Path: conditional, selected by decision D1 (Revision 9.2), NOT_RUN until executed with current evidence. Direct-context isolation remains required and has no dependency on that input adapter. |
| SOURCE:Tom exact R-1 inputs (received 16 Nov 2026; v11 Section 40) | R-1a, R-1b | Received; commit unchanged under tests/fixtures/tomReview/m5ExactInputs/ and check SHA-256 before running; never substitute Saif's reconstruction |
| SOURCE:container payload and unit-load limit (containerRun only) | U-DA-07 | containerRun BLOCKED until Tom sends them; mmConversion, rowPatterns and missingLimitsRejected run now |

Answered Q6 and production x/y are not blanket blockers. Reported source availability, actual fixture presence, application execution, and current variant selection are separate facts.

## 11. Evidence record

One JSON line per executed variant, written by `m5EvidenceWriter` to `<build>/m5Evidence/<commit>/<testId>.<variant>.json`:

```json
{
 "testId": "X-REB-09",
 "variant": "fifty",
 "fixtureProfile": "FX-30",
 "entryPoint": "DIRECT",
 "evidenceScope": "SYNTHETIC",
 "executionStatus": "NOT_RUN",
 "customerAcceptance": "NOT_EVALUATED",
 "pendingReferences": [],
 "build": {
  "commit": "<git rev-parse HEAD>",
  "dirty": false,
  "compiler": "<CMAKE_CXX_COMPILER_ID/VERSION>",
  "config": "Release"
 },
 "inputs": {
  "demand": "<path+sha256>",
  "master": "<path+sha256>",
  "config": "<path+sha256>",
  "profileId": "FX-30",
  "profileRevision": 1
 },
 "model": {
  "scope": "physical",
  "demand": "allRequired",
  "comparator": "FX-PRIORITY",
  "acceptance": "x=40000 lb,y=3000 cu ft,strict OR"
 },
 "expected": {
  "trucks": 2,
  "unitsShipped": 50,
  "perTruck": [
   25,
   25
  ]
 },
 "actual": null,
 "ledger": {
  "bySource": "<shipped/left/carried/rejected per source>"
 },
 "reservations": {
  "reserved": 2,
  "used": null,
  "unused": null
 },
 "validation": null,
 "proof": {
  "lp": null,
  "incumbent": null,
  "bound": null,
  "scope": null
 },
 "stop": {
  "reason": null,
  "allowance": null,
  "elapsed": null,
  "acceptedLoads": null,
  "average": "N/A"
 },
 "seed": null,
 "threads": 1,
 "machine": null,
 "notes": ""
}
```

Never write invented hashes, timings, datasets or certificates. Fields not produced by an actual run stay null.

### 11.1 Manifest/evidence validation

Before acceptance, validate unique named IDs and `(testId, variant)` keys, the complete selected required set, current requirement/profile identity, and the dependencies scoped to each selected variant. A no-assertion registration or process exit zero cannot turn NOT_RUN/BLOCKED into PASS. Reject duplicate instance identities, absent variants, stale source/config/build/probe identities and incomplete parameterized-case/path coverage.

`requirement: conditional` means selected only by its recorded controlling decision. (Revision 9.2: decision D1 selects R-7 m4Path/m5Path, `selectedForCurrentMilestone: true`; the text below describes the v12 deferral it replaced.) R-7 m4Path/m5Path explicitly set `selectedForCurrentMilestone: false`, `selectionReason`, `selectionDecisionRef` and `activationCondition`. Keep them in the catalog, report their deferred status and exclude them from the current required result set without counting them as passed. A later selecting decision requires fresh compatible evidence and all its dependencies. Do not infer the selection of other conditional variants; record each controlling decision before the final gate. Required direct isolation variants remain selected.

Variant `expectedBehavior`, `fixtureOverrides`, `entryPointOverride` and `oracleOverride` specialize their parent. `dependencies` on a variant apply only there; parent dependencies apply to every variant. `reportedBaseline` is historical attribution, never an assertion that execution occurred here and never a stricter substitute for the current expected behavior. A supplied feasible witness length does not force every valid result to use the same layout.

The final gate must distinguish a recorded intentional deferral from a required missing prerequisite. All selected required variants need current PASS evidence; an unselected conditional variant may remain visibly BLOCKED. Fixture hashes in inherited records must be checked against actual bytes when executing. Source-to-manifest equality demonstrates consistency, not correctness of the solver or fulfillment of the source assertions.

### 11.2 Repository implementation (Revision 9.2, 17 November 2026)

- Test files follow the repository's flat `tests/m5/` layout; the manifest's `testFile` names them (decision D4).
  Files of tests not yet written carry "(planned, PR-n)". The planned subfolders named elsewhere in this package
  (`tests/m5/solver/`, `tests/m5/validation/`, ...) are not used.
- A test case declares the variants it executes as `testId/variant` tokens in its name, for example
  `TEST_CASE("X-RUN-09/duplicateID: ...")`. A name carrying a test ID alone declares no variant and produces no
  record; such tests are confirmed variant by variant in a later pull request.
- A doctest listener (`tests/m5/m5EvidenceListener.cpp`) writes one record per declared variant of every
  executed case into `<build>/m5Evidence/<commit>/<testId>.<variant>.json` (`<commit>-dirty` when the build had
  changes not committed), in the shape above, plus `history.jsonl`. A second case declaring the same key in one
  run writes `<testId>.<variant>.duplicate-<n>.json`, so neither result is overwritten. Records are never
  committed (decision D5).
- The build writes its commit and dirty flag at every build (`cmake/m5BuildIdentity.cmake`). A record names the
  build, the SHA-256 of the specification, manifest and `config/orderBuilderParams.json`, and its assertion
  counts: PASS needs at least one assertion and none failed; a case that asserted nothing is NOT_RUN.
- The gate (`tests/m5/support/m5EvidenceGate.hpp`, X-RUN-09) needs every selected variant to have exactly one
  current record from a clean build of the current commit, and accepts when each is PASS, PARTIAL (a PASS whose
  record names the clause it did not cover), BLOCKED with a recorded reason, or NOT_RUN with a recorded reason
  (`tests/m5/data/m5VariantReasons.txt`). It rejects a FAIL, a stale or duplicated result, a NOT_RUN or BLOCKED
  key with no reason, and a key with no record. Every non-PASS outcome is listed by name with its reason. The
  ctest entry `m5EvidenceStatus` writes `m5EvidenceStatus.txt` beside the records; `ob_tests
  "-tc=m5Acceptance*" --no-skip` runs the gate. An unselected conditional variant must name its decision or
  blocker, or it is rejected. A test that compares with Tom's local, git-ignored reference file is skipped by
  name and recorded BLOCKED with that reason when the file is missing.
- Layout witnesses are checked by `tests/m5/support/m5LayoutChecker.hpp`, which never calls floor-pattern code.
- An existing M4 test that is the test of an M5 variant is named in `tests/m5/data/m5ExistingTestKeys.txt`
  (M4 test files are not edited); the listener writes the variant's record when that case runs. U-PF-04 is
  the M4 thread test, recorded only when the run uses `--success` (ctest entry `m4BenchmarkThreadsSuccess`):
  the case plans each held extract at one thread, again, and at four.
- X-RUN-09/crossCompilerGate (`tests/m5/testM5CrossCompiler.cpp`, ctest entry `m5CrossCompiler`): each
  compiler's build of the M5 core suite writes a record into `build-msvc-m5matrix/` (commit, warning flags,
  evidence folder). The gate needs MSVC and GCC records for this clean commit built with the strict flags
  (only `-Wno-error=array-bounds` may stay non-fatal) and the same result for every synthetic variant on each;
  a compiler with no record (Clang is not installed here) makes the variant BLOCKED with its name, never PASS.

## 12. Clarifications raised against v5 (PC-1..PC-9) and their v6 status (PC-10)

| Ref | v5 text | Observation | Proposed resolution | Impact |
|---|---|---|---|---|
| PC-1 | U-RM-01 "M4's normal small-lane path is used" | Taken literally, a full M4 route could re-escalate into M5 (recursion) | Read as the M4 packer layer (knapsack/binPacker/patternPacker + truck check), not selector routing. Assert no selector re-entry | Test asserts packer reuse and no recursion; expectation unchanged |
| PC-2 | U-CG-08 "No second copy" of columnGeneration | X-CG-18 needs integer completion beyond LP convergence | Extend the shared implementation in place; M5 adds an integer-completion layer that calls it | None; strengthens reuse |
| PC-3 | X-E2E-09 30-position witness | CLOSED: floorPattern places 15 wide-wide rows of 2, 600 x 96 in (baseline B10) | None | None |
| PC-4 | X-RES-12, X-E2E-06 heterogeneous reserved trailers; U-DA-05 per-truck override | Input has no per-truck size/weight; one trailer per run (baseline B4) although a 16 Oct client rule mentions per-truck values | Test at the physical/allocation seam; U-DA-05 input variant BLOCKED; raise with Tom and Saif | Explicit, not skipped |
| PC-7 | RD-11, X-E2E-08 and preview text "your 154 unused" | 154 is the harness figure (1,440 in-window reservations - 1,286 client trucks on reserved lane-dates); the repo's client-stated figure is 195 (1,503 placeholder trucks - 1,308 loads) (baseline B11) | Compare our 98 with 154 on the same basis; label 195 as the all-placeholder basis | Expected values in RD-11/X-E2E-08 text; preview wording |
| PC-8 | U-PB-07, X-PAT-15 stacking in trucks | M4 places each SelectionItem as one rigid floor stack; stacking is decided in M2 (canStack); no per-item stackable flag (baseline B5) | Assert stacking through the M2 stack builder and that M5 never stacks items | Test entry point, not expectation |
| PC-10 | v5 U-AC tests and the export check | Tom: the minimum-load test runs at export, after M5 | APPLIED in v6: optional policy seam plus required productionProfile variants (v6 Section 4.14) | Expected results for this customer |
| PC-9 | RD-04 baseline | 8 Aug (20 reserved) is solved by M4: 20 trucks, 678 of 763 offered unit loads (baseline B15); with M5, carried AcceptanceWithheld/OverReservation goods change this input (ED-15) | Assert reconciliation, not equality, for 8 Aug once M5 carries goods | None to v5 text |
| PC-5 | U-RS-05 "unless Tom confirms 99 is real" | Historical question, not among the five pending items | Keep as data dependency; keep rejecting > 11 | None |
| PC-6 | RD-07 reserved counts for 2028 -> 2190 | Not yet measured | Read from placeholder file in WP10 | None |

