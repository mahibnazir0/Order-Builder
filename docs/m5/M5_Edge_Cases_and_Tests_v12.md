# M5 - Edge Cases and Unit Tests (v12)

Order Builder, Milestone 5: planning the large lanes (cutting stock and scale).
Updated 17 November 2026 (v12). Working test specification for the M5 build.

v12 update (17 November 2026): Section 42 reconciles 162 proposed edge cases against v11,
preserving all previous test IDs and variant keys. Three new named tests and 35 unique test/variant
identities extend the coverage. The current plan defines 304 named tests, 557 unique variants,
24 invariants, 48 edge-case checklist rows and 10 clarification (CL) rules. Section 42 and the
reconciliation files distinguish already-covered cases, new coverage and out-of-scope work.

v11 record (kept):
v11 applies Tom's 16 November email. The exact inputs for R-1a, R-1b and R-5a arrived, so R-1a and
R-1b are no longer blocked (Section 40). The tie rule ED-21 now rotates by lane and date. A new test,
U-DA-07, covers a 40 ft high-cube container as the single trailer type; its full run waits for the
container's payload and unit-load limit. v11 keeps all 300 v10 test IDs and adds one: 301 named
tests, 24 invariants, 48 edge-case checklist entries and 10 CL rules. Change record: Section 41.

v10 record (kept): 
v10 applies Tom's 14 November answers to two engineering questions. OPEN-COMP is resolved: an
equal-service tie between groups is broken by a group order that rotates with the ship date, so no
group always wins (ED-21, Section 0.8). C9 is accepted for now: a lane-date whose reserved trucks have
different trailers is refused, not guessed; a truck-type mix chosen from an availability list is out
of scope (Section 8). v10 keeps all 299 v9 test IDs and adds X-RES-13 (Section 38): 300 named tests,
24 invariants, 47 edge-case checklist entries and 10 CL rules. The change record is in Section 39.
The M4 baseline remains f9d9945. R-5a's exact fixture package is recorded as available in v8; the
per-lane past-due override remains deferred, while AgeFirst run-wide is the current production behavior.
The 1,716 generated inputs remain instances of one named test.

v9 record (kept): v9 reviewed the seven v8 / revision-5 documents together, kept all 293 v8 test IDs
and added six (Section 36; change record Section 37).

Evidence scope for this review: only the seven supplied documents were inspected. The original Tom
reproduction files, m4Fix2Verification.txt, application repository and raw August fixtures were not
attached to this review. The v8 author records having read those originals and run an M4 verification;
its measurements and repository claims remain REPORTED evidence here. Fixture availability in that
record does not mean the fixture bytes have been independently checked in this review. Before running
R-5a/R-8/R-9, commit and hash the exact files; do not reconstruct their measurements from this prose.
This revision claims no new customer answer, M4 rerun, application build or M5 execution.

Customer corrections already recorded in v6 (retained; details in Section 32):
- The customer's minimum-load check (weight > x OR cube > y) runs at export, AFTER M5. M5 no longer
  withholds trucks for it. The predicate stays as an optional policy seam with synthetic tests.
- "Full" is measured by the space left at the back of the trailer (rear inches, everything stacked as
  high as allowed), or 98% of weight, or no remaining stack fits. Not 84% cube.
- Q6 answered: run the careful method whenever the quick result is above a valid minimum; the careful
  method stops at a user-set time limit and keeps the best plan found.
- Can-go product (pull-forward, due within 10 days and available) fills spare space on M5 lane-dates
  after required goods.
- Goal order: ship the highest-priority goods and use all available trucks; fill is best effort.
- Q5 closed: the three real lanes plus the 38-truck reference case.
- Past-due order: AgeFirst run-wide remains current; per-lane overrides are deferred until a change and milestone are agreed (R-7).
- M4 is now at commit f9d9945 (Tom's 12 Nov review fixes on top of e51d9c4's 3da08ab fixes). It is the M5 baseline (NEW BASELINE FOR M5: yes, m4Fix2Verification.txt).

All application execution results start NOT_RUN. A synthetic pass applies only to its fixture.
Record results separately (Section 26).

Answer sources, in order:
- `M5_Output_Preview_Rev3_Dated_11_6_26.docx` (internally Revision 3, 6 November 2026): answers to Q1-Q9.
- Tom's follow-up on Q6: "Computer time is cheap. Trucks are expensive"; stop when the quick method is at
  the minimum, otherwise run the careful method with a user-set limit in seconds per load.
- Tom's answers to four follow-up questions with his weekly rear-inch chart (weeks 39-45).
- Tom's forwarded review of the preview with his notes (Q3 "good start", Q7 "user selectable by lane",
  Q8 "distance to the back of the trailer ... stacked as high as possible", Q9 "see notes above").
- Tom's M4 re-reviews at 3da08ab and e6dbb0f (with his reproduction files), and the verifications of e51d9c4 (m4FixVerification.txt) and
  f9d9945 (m4Fix2Verification.txt).
- Tom's 14 November reply to Saif's M5 progress note: OPEN-COMP ("any tiebreaker would work as long as
  it didn't always push to the same single side") and C9 ("in this case, it's okay").
- Tom's 16 November email (his engineer's review): exact R-1a/R-1b/R-5a inputs, tie rotation by lane
  and date accepted, C9 conditions for a future mixed fleet, and the 40 ft high-cube container to try.
- Two settings stated to Tom that apply unless he objects: lane-date time budget = seconds per load x
  reserved trucks; rear-inch limit default 14 in.

Start with Sections 0.6, 30 and 42. Section 10 records every answer. Changes from earlier versions are in
Sections 27-29 and 31; v6 changes are in Section 32, v7 changes in Section 34, v8 changes in Section 35, v9 in Sections 36-37, v10 in Sections 38-39 and v11 in Sections 40-41.

---

## 0. How to use this file

### 0.1 Labels

| Label | Meaning |
|---|---|
| Measured | Expected value was measured earlier from the real August fixtures (our own scripts or the M4 benchmark reports). This document does not re-check it. Assert the exact number once the fixture is confirmed |
| Historical | A figure from the client's solved output or from before the M4 fixes. Used for comparison only, never as a pass/fail target for M5 |
| Reported after fixes | A current value stated in the returned preview, awaiting reproduction from its named M4 report, commit, inputs, master and configuration. Do not call it newly measured by this document |
| Re-measure | A real figure from before the M4 review fixes. Measure it again after the fixes, record it once, then assert it |
| Pending Qn | An item still open in Section 10 (v6: Tom's exact R-1/R-5 inputs, the M4 per-lane past-due setting, and no objection yet to the two stated settings). Write the synthetic fixture now; keep the dependent customer acceptance gate pending |
| Proposed (our call) | Engineering proposal or delegated decision, identified separately in Section 10. Q3 is delegated; Q6 is answered in Section 10. Engineering choices retain their own provenance. A proposal is not a confirmed production rule |
| Needs a rule | An engineering definition in Section 0.3 is not yet frozen. Use the explicit fixture contract for development tests; record the actual production contract before its acceptance gate |
| Waiting for data | Needs an input we do not have yet (for example Tom's exact R-1 case) |
| Build | Does not occur in the August data. Build a small synthetic input |
| Synthetic setting | A value set inside a test only to make it reproducible (a threshold, a rate, a policy). It is a test input, never an approved production default |
| P0 / P1 | P0 = catches wrong cargo, wrong rules or a false "best" claim. P1 = robustness and repeatability |

- Every test has an ID. Put it in the test name or a comment.
- A skipped test or a missing fixture is **not** a pass.
- A planning or performance acceptance run that does not produce its required result does not pass.
- A deliberate failure-handling test (for example X-CG-12 or X-RUN-02) **passes** when it shows the
  required "no result" or "timed out" status, leaves state unchanged as specified, and accounts for
  every unit. A solver status of "timeout" is not a failed test when that status is the intended outcome.
- Final sign-off rule: open decisions may remain during review and implementation. Any unresolved
  question or rule that decides an acceptance result (the open or proposed items in Section 10, and
  every CL definition needed by that acceptance result in Section 0.3) blocks final M5 sign-off. Listing it as open does
  not complete the milestone.
- Write pending and waiting cases now. Execution status and customer acceptance status are separate (Section 26). A complete synthetic variant can PASS when run, while its customer variant remains PENDING. Missing fixtures are BLOCKED, not passed.
- Numbers used inside synthetic fixtures are test inputs. They are not proposed production defaults.
- Line numbers and file names inherited from older notes (for example `testM4Benchmark.cpp:552`) are
  pointers. Find them at the current commit before relying on them.

### 0.2 Ground rules for every run

| Rule | Detail |
|---|---|
| Compiler | MSVC x64, VS2022, Release. Also clean on GCC and Clang with `-Wall -Wextra -Wpedantic -Werror`; MSVC `/W4 /WX /utf-8` |
| Fixtures | Real fixtures copied in by hand (`tests/importer/august/...`, `tests/importer/crossDay/<yyyymmdd>/...`). Development subsets name their included and blocked cases. Final acceptance has zero skipped or blocked required cases; missing fixtures never count as passes |
| Output | Plain ASCII only. No em dash, no curly quotes |
| Naming | camelCase for new files, functions and variables |
| Evidence | Fresh build and full test run at the exact head commit being handed over. Old evidence does not count |
| Missing data | If the October 1-2 fixtures are still missing, say so. Do not claim a full twelve-extract pass |
| Run order | Check config and inputs, set the M4 baseline, run small exact tests, then group/date tests, then published cases, then real data, then timing. A fast wrong plan must never pass a speed check |

### 0.3 Rule and decision register

Customer decisions and engineering definitions are separate. Synthetic settings make a fixture
executable now; they do not silently become customer defaults. Any unresolved definition that
controls an acceptance result must be fixed before that result can close M5.

| Ref | Current rule, status and development treatment | Tests affected |
|---|---|---|
| CL-01 | Optional minimum-load predicate: weight > x lb OR cube > y cu ft, strict, full precision (form from Q8). For this customer the check runs at export to the host system, AFTER M5 (Tom's follow-up answer 3): the production profile has the predicate OFF and M5 withholds nothing for it. The predicate stays as an optional policy seam. FX-30 supplies synthetic x = 40,000 and y = 3,000 only where a test selects the policy. Missing x/y is not a blocker for this customer. A later customer that needs the rule inside M5 records its values, units and profile then | U-AC, INV-14, X-REB-04/09, X-CG-19, X-E2E-04/09, X-REPORT-04, RD-12 |
| CL-02 | Complete-allocation comparison uses the corrected M4 urgency classes (rank date, priority, ship date, with rankDateOf under the effective lane order), confirmed required-before-can-go precedence, and feasible group candidates. Priority/service and use of usable reservations precede best-effort fill; do not prefer fewer trucks by itself. Equal-service ties are broken by ED-21 (Section 0.8; Tom, 14 and 16 Nov 2026): a group order that rotates with the lane and the ship date, then canonical records. Fill, rear space, weight and truck count never break such a tie; they are reported. No synthetic fewer-trucks fallback. An unproven checked incumbent may commit labelled Unproven. Linked-date comparisons preserve source identities and original due dates (RD-13) | R-2, R-4, R-6, INV-11, X-RES, U-CF-04, X-CG-20, A/B comparison, RD-13 |
| CL-03 | Fullness labels: weight >= 98% of the actual trailer limit, or exact rear space <= the configured limit after legal M4/M2 stacking, or an evidenced no-fit conclusion for the declared supported layout and residual scope. Default rear limit 14 in was stated to Tom and remains configurable. Section 0.8 records the engineering no-fit, incomplete-evidence and bucket definitions. No-goods alone is not a floor-capacity proof. Labels never withhold trucks; no last-truck minimum | U-FL-01..10, U-RM-03/04, U-AC-05/08, X-E2E-04/09, X-REPORT-10, RD-10/12 |
| CL-04 | Q6 answered: for a feasible all-required-goods quick result F and valid integer lower bound B for the same objective, goods and model, run the careful count method when F > B and allowance remains; equality stops that count phase. Keep the best checked plan at the limit. FX-GAP G = 1 with >= is equivalent for integer truck counts. Scope equality certificates to that objective; a bound on a shipped subset or count relaxation is not a proof of priority, can-go, fill or full-service optimality. Report named gap denominator or N/A when unavailable/zero | ACC-3, U-FF, X-CG-17/20 |
| CL-05 | Time limit: seconds per LOAD, set by the user (Tom). Contract stated to Tom, applies unless he objects: lane-date allowance = secondsPerLoad x reserved trucks of that lane-date, declared before search, shared by quick and careful methods, blocks, repair and final checks, never enlarged by candidate or final truck counts. Report time per truck, group, lane-date and total, the loaded truck count, and average or N/A. Example at 1 s/load: 7 Aug 21 s, 8 Aug 20 s | ACC-2, U-PF-01/02/05, X-BLK-07, X-RUN-04/11/12/13 |
| CL-06 | Unused-reservation penalty rate, units and effective configuration: record the real source before using the penalty for acceptance or ranking. Missing rate is unknown, never zero. A synthetic case states its rate or excludes penalty from its objective explicitly | U-RS-02, X-RES-10, X-STATE-02 |
| CL-07 | Packing atom and permitted splitting: recover M4's supported contract. FX-30 units and the block fixture atoms are indivisible; stacking never permits silently splitting source units. Do not add an unapproved single-pallet filler path. A test requiring a different atom declares it | Pattern, stacking, remainder and conservation tests |
| CL-08 | Routing is a performance choice (Q1). Keep reserved trucks per lane-date > T, T = 20, configurable; set the final T from measured lane times. Baseline evidence: M4's time is in small knapsack lanes at the node limit (one Aug6 route 12.83 s at f9d9945, 10.36 s at e51d9c4); the escalated lane costs no M4 time | U-RT, X-ROUTE, U-PF-02, RD-01/05 |
| CL-09 | Repeatability: completed/deterministically stopped fixtures use the recorded canonical comparator and seeds. Wall-clock runs must be valid and honestly labeled; do not promise identical cargo across thread counts unless the recorded implementation contract guarantees it. Freeze this engineering contract for acceptance | INV-12/13, U-PF-03, X-RUN-01 |
| CL-10 | Commit/rollback contract: provisional work, validated candidate blocks, selected result and operational commitment are separate. Candidate validation includes acceptance, physical, dates, groups, sources and reservations. Synthetic keep-validated and whole-lane-rollback variants test the seam; they do not require both production modes. Before any commit, retries use the full original snapshot. After an actual partial commit, use a reconciled residual snapshot or idempotency mechanism, never replay committed goods. Record the chosen production contract | INV-17/24, X-BLK-08, X-RUN-07, X-E2E-05 |

### 0.4 Routing preconditions

FX-ROUTE uses reserved trucks per lane and ship date, threshold T = 20, and routes to M5 only when
reserved > T. Twenty stays in M4; twenty-one goes to M5; zero reservations allow zero dispatch.
This profile matches the current M4 setting (`selection.m5Threshold`); Tom confirmed the threshold is a
performance choice, not a business rule.
Other routing modes are unsupported in this working profile and are rejected clearly.

Small M5 solver, reservation, repair and end-to-end fixtures with one or two reservations invoke
the M5 planning/validation seam directly, or use an explicitly test-only forced route. They must
not accidentally test M4 merely because their counts are below T. Only routing tests assert the
production escalation seam. A forced route never raises reservations or bypasses hard constraints.

### 0.5 Standard physical fixture FX-30

Used wherever a test says "FX-30 units". All values are synthetic settings.

| Property | Value |
|---|---|
| Pallet type | TLD |
| Footprint | exactly 20 x 100 in |
| Height | 108 in final (no deck or tare added afterwards) |
| Weight | 1,500 lb final gross per unit |
| Stacking | not stackable |
| Trailer | default 630 x 100 x 108 in, 45,000 lb |
| Result | 30 units use 600 in, weigh 45,000 lb and contain 3,750 cu ft. Each unit is exactly 125 cu ft. A 31st exceeds the weight limit |

Defaults for tests that use FX-30, unless the test states otherwise: one do-not-mix group; all goods
available on D and due on D; priority P0; one lane; ship date D; the reservation count stated in the
test; order policy **date-first** (confirmed by Tom, Q7), which has no effect when all
goods share one date. Any test whose result depends on group, availability, due date, reservations or
order policy states that value in its setup.

Tom's exact regression fixtures are **not** FX-30. R-1a/b exact inputs arrived on 16 Nov (Section 40).
R-5a's exact files are recorded as available in v8; copy them unchanged and verify their committed checksums before execution.

Optional acceptance policy for FX-30 tests that select it (synthetic): x = 40,000 lb and y = 3,000 cu ft,
strict ">". A full FX-30 truck (45,000 lb) passes on weight. Tests whose synthetic expectation depends on
this policy keep it ON in their synthetic variant; their `productionProfile` variant runs with the policy
OFF, as for this customer (Section 4.14).

Fullness for FX-30 (FX-FULL): 30 units = 45,000 lb (100% of weight) is FULL on weight even though 30 in
remain at the back (600 of 630 in).

---

### 0.6 Working fixtures and execution while answers are pending

Implement complete synthetic fixtures now. Execution status is NOT_RUN until code is actually run;
a later PASS applies only to that declared variant. Customer acceptance can remain PENDING at the
same time. Never silently use synthetic values when a real-customer configuration is incomplete.

| Fixture or contract | Exact development setting | Production dependency |
|---|---|---|
| FX-30 | Section 0.5 physical units; optional synthetic acceptance x = 40,000 lb, y = 3,000 cu ft, strict OR, only where a test selects it; FX-FULL labels; case overrides take precedence | Production profile for this customer: acceptance OFF |
| FX-ROUTE | Section 0.4: reserved per lane-date, T = 20 unless changed in the case, route only when reserved > T | CL-08 mechanism/calibration |
| FX-GAP | Feasible FFD bin count F and valid integer bound B for the same all-required-goods objective; G = 1; trigger iff F - B >= G and positive budget remains. Equal to B never triggers. Gap input is a positive whole truck count | Equals the production rule (Q6 answered) |
| FX-CLOCK | Each case declares a fixed synthetic allowance or deterministic work count before search. All work named by the case shares that allowance; no reset on a method, block or repair. Report raw elapsed, loaded truck count and average separately | Production: secondsPerLoad x reserved trucks (CL-05), user-set seconds per load |
| FX-PRIORITY | For same-date synthetic allocation oracles: maximize the lexicographic shipped-unit count vector P0, P1, ... P11. If that vector ties, prefer fewer trucks, then the lexicographically smallest canonical truck-membership record (sort source IDs within each truck, then sort trucks by lane/date/group/source-ID list). Penalty is excluded unless the case states otherwise. R-4 defines its explicit date-first/priority-first comparison. Case-specific comparators override this fixture | Production comparator differs after the class vector: better fill, then M4 item order, and no fewer-trucks preference (CL-02) |
| FX-COMMIT | The case selects keep-validated or whole-lane-rollback behavior at the test seam. Both use isolated candidates and only the selected checked result may commit. An actual partial commit changes the retry snapshot to the reconciled residual | Production CL-10 contract; alternative synthetic variants do not require both production modes |
| FX-COUNT | Arithmetic, pattern-copy and scalar packing checks use their stated capacities/stock with no dispatch minimum unless explicitly introduced. All required items are conserved exactly; whole physical units are indivisible | Not an operational customer shipment |
| FX-FULL | Rear-inch limit 14 in, weight-full 98% of the trailer limit, floor-limited when no remaining eligible stack of the same group fits on M4's layout. Buckets: goods remain / no goods remain / rule-limited | Default stated to Tom; user-settable per customer (CL-03) |
| FX-CANGO | Pull-forward window 10 days; can-go = goods due D+1..D+10, available by D, offered after all goods due by D; groups and stacking rules unchanged | Tom: can-go = pull-forward (follow-up answer 4) |

Tests that select the optional acceptance policy must name FX-30 or state both x and y, with weights,
cube and eligibility sufficient to decide the OR predicate. All other physical tests run without it. Pure geometry, conversion,
solver and arithmetic tests do not inherit customer dispatch minimums. Cases that explicitly add
acceptance, groups, heterogeneous capacities or dates keep those additional constraints.

Real-data runs use the production profile: acceptance OFF, FX-FULL fullness labels with the customer's
rear-inch setting (default 14 in), the current run-wide AgeFirst setting, can-go fill on, and the CL-05
time contract. A run that selects the optional acceptance policy is a labelled diagnostic only.

The test manifest records each ID and parameterized variant, entry point, fixture/profile,
applicable invariants, evidence scope, required status and open dependencies. A missing code path
or fixture is BLOCKED, not silently skipped. Nonselected alternative policy variants stay visible
outside the required production manifest with a reason; the selected variant remains required.
In particular R-7's deferred M4/M5 per-lane integration variants are conditional and unselected for
current M5 sign-off. They remain visible as BLOCKED with a deferral reason, not passed or silently
skipped. R-7 direct profile-isolation and current run-wide controls remain required.

### 0.7 Report contract for this working revision

Use preview Sections 3A-3E for the report structure, with the current expectations below.
The answered Word document is a response record; stale percentage, pending-Q7 and ten-second-lane
labels in its body are not literal assertions for the new report. The preview itself is not edited
by this test-file revision.

| Area | Required fields and interpretation |
|---|---|
| Header | Run/input/master/commit/config identity; SYNTHETIC or PRODUCTION scope; customer/profile; "minimum-load check: at export, after M5" (or the optional policy values when a test selects it); fullness rule (98% weight, rear-inch limit, floor-limited); routing count/scope/T; past-due order (effective run-wide setting; per-lane overrides only in an explicitly selected future integration or direct synthetic variant); can-go window; methods attempted; seconds per load and lane-date allowance |
| Lane | Origin/destination, client lane ID, M4 group IDs, date, shared reservations, source quantities, final checked cargo/layout, per truck: weight %, rear inches, FULL yes/no or not-established with fit status/reason, report bucket; can-go goods loaded; leftovers/carry, unused count and known or unknown penalty |
| Method evidence | Separate A/B status and metrics; selected method; known optimum and independently checked packing for scalar benchmarks; LP result, integer result, valid bound and proof scope; trigger gap and policy status |
| Threshold benchmark | Actual reserved count per lane-date, historical built count separately, goods/product/group/date complexity, elapsed and timing metric, machine/threads/cache conditions. Joined-date rows stay labeled benchmark and preserve each date's rules |
| Summary | Selected production results only; 7 Aug comparator 18, both-dates comparator 38 only in the labeled benchmark; totals reconcile. Average rear inches per bucket (goods remain / no goods remain / rule-limited) for comparison with Tom's chart. Print total time, loaded truck count, average or N/A, and the allowance used |

Historical 84%/3,307.5 cu ft figures are reference calculations only; fullness is now CL-03.
A reported M4 estimate of 22 trucks against 21 reservations is not an infeasibility certificate.
To call full service impossible, record an applicable proof or valid integer lower bound; otherwise
label the number as an estimate and report the actual checked allocation and remaining goods.

---

### 0.8 Engineering definitions added by this review

These definitions make the new tests reproducible. They are engineering contracts, not additional
customer quotations. Record them in the implementation decision register and verify the M4 adapters.

- ED-FULL: rear inches = inside trailer length minus the farthest longitudinal occupied extent of
  the final valid supported layout. Measure after maximum legal upstream stacking, without changing
  rigid M4 atoms inside M5. Compare authoritative normalized metrics before report rounding; a
  conservative packing lattice is separate from label and optional strict-predicate arithmetic.
- FloorLimited requires a nonempty set of remaining otherwise eligible same-group atoms and a
  complete no-fit result for every atom in the recorded M4 layout scope. A fitting witness disproves
  FloorLimited; a heuristic failure or interrupted search does not prove it. Empty inventory is not
  a physical-capacity proof. Do not extend this evidence to unsupported global M6 geometry.
- A complete fit control with direct thresholds false yields NOT FULL. Incomplete no-fit evidence
  with direct thresholds false yields not established/Unknown, not a conclusive negative label.
  Independently true weight/rear reasons may establish FULL even when fit diagnostics are incomplete.
  Advisory diagnostic incompleteness alone never withholds a physically validated production load.
- Buckets use the final selected lane/date/group residual snapshot: any otherwise eligible residual
  gives GoodsRemain (even if physically impossible); no residual gives NoGoodsRemain; only date/order
  excluded residual gives RuleLimited. Other groups do not enter that truck's fit universe. Label
  revisions include candidate, profile/trailer and remaining-source snapshot identities.
- ED-21 (resolves OPEN-COMP; Tom, 14 Nov 2026: "any tiebreaker would work as long as it didn't always
  push to the same single side"). It applies only when two complete, finally validated allocations for
  one lane-date ship the same unit loads in every urgency class (CL-02). Steps, in order:
  T1 Rotating group order. List the lane-date's groups with eligible demand in the frozen lane-date
     input by stable group ID, ascending; g is their count.
     r = (laneKey mod g + dayNumber mod g) mod g, where dayNumber counts whole days from 1970-01-01 to
     the ship date, and laneKey is the 32-bit FNV-1a hash (offset basis 2166136261, prime 16777619,
     arithmetic mod 2^32) of the ASCII text origin + "|" + destination + "|" + mode, using the lane
     codes as M4 normalizes them (for example "2028|2574|TL"). Start the list at position r and wrap
     around. (Tom's engineer, 16 Nov: a deterministic rotation by lane and date is acceptable.)
     Compare the two allocations' shipped unit loads per group in that rotated order; the first
     difference decides, and more unit loads for the earlier group wins.
  T2 Canonical records. If still equal, list each allocation's shipped records by stable source-line
     ID, then actual slot ID, ascending; the first difference decides, and the allocation holding the
     lower stable ID wins.
  The rule never uses input order, truck count, fill, rear space or weight, and it never overrides
  urgency, eligibility, hard limits or reservations. Same input gives the same plan (INV-12), and
  shuffled rows give the same winner (X-META-03). A copied lane has a different lane key, so its tie
  may go to another group (X-META-06). Renaming groups may change the winner on a date, which
  X-META-05 allows for this ID-based rule. On one lane, over g consecutive ship dates with the same
  tie, each group wins once. A test that accepts
  any equal-service witness (for example X-RES-07, X-REB-09) still accepts it; ED-21's choice is
  asserted by X-RES-13. Fill, rear space and weight stay report metrics. Tom's note: such ties
  usually mean a shortage across the whole network.
- ED-CANGO: required feasible goods take precedence across the shared lane-date pool, then eligible
  can-go goods can use spare usable reservations, including an already reserved date with no required
  goods. No new date/reservation is invented. This records the working interpretation of the v6
  use-reservations goal; verify it against the actual adapter/source record in WP0. An available but
  physically impossible required atom is recorded, not allowed to deadlock feasible future fill.
- ED-TIME: production allowance is elapsed wall-clock secondsPerLoad x the pre-search reserved count,
  as stated to Tom. Work-count clocks are separate deterministic fixtures. Checked arithmetic and
  clock conversion must not wrap, saturate silently or pool budgets between lane-dates.
- ED-COMMIT: the selected delta applies atomically with expected inventory/reservation versions and
  a stable operation identity/payload digest. Exact retry is idempotent; conflicting payload or stale
  original snapshot cannot consume units again. A reconciled residual continuation has its own
  explicit operation and expected version (X-STATE-10).
- ED-FLOOR: layout search returns Fits, DoesNotFit, or Undecided, separately from validating a supplied
  witness. Fits needs a checked witness, not exhaustive row enumeration. DoesNotFit needs a hard-limit
  contradiction or a complete negative certificate within the named supported-layout model. A failed
  heuristic, exhausted search or cancellation without that evidence is Undecided. Potentially better
  unresolved alternatives block an optimality claim unless an independent same-objective certificate
  excludes improvement. A valid witness still may dispatch under the recorded stop/commit contract.
- ED-FLOOR-BUDGET: pass an explicit planning-owned floor context; each call is bounded by its own
  search-state cap, the remaining shared route-state allowance, and the parent wall/work budget.
  Changing methods, blocks, repairs or probes within that context never resets it. WP0 records the
  M4 route/group scope and M5 mapping; do not silently equate a do-not-mix group with the whole lane-date.
  State exhaustion and time exhaustion are named separately. Undecided is never a permanent negative
  cache entry. Pure supplied-witness validation remains available without spending search states.
- ED-UL-CAP: unit loads in an atom = checked sum of its component counts; truck total = checked sum
  across selected whole atoms and multiplicities, counted once. The positive integral effective slot
  cap is a hard limit in pattern generation, copies, A/B, residual packing, repair and final validation.
  Geometry-equal atoms with different unit-load composition must not share a cap-constrained maximum.
  Current production slots receive the validated global maxUnitLoadsPerTruck (64 in the reported
  customer configuration); direct synthetic slots may carry different explicit caps. Per-slot input
  support is a separate capability, not a blocker for global-config or direct-seam tests. Missing,
  nonpositive, fractional or unrepresentable cap/count values are errors, never defaults or wraparound.
  An indivisible atom cannot be split to fill an odd cap. A bound which relaxes the cap can be valid
  but never stands in for a feasible cap-constrained packing or a full-objective certificate.


---

## 1. Acceptance tests (from the delivery plan)

These three decide whether M5 is done.

| ID | Test | Pass when |
|---|---|---|
| ACC-1 | Column generation on standard cutting-stock cases with a known best answer | Finds the known best on PUB-01 to PUB-03 plus the capacity-only exact cases in Section 12, including X-CG-18. Return a complete feasible packing checked independently, not just an LP value. X-CG-19 is a separate acceptance-adapter feasibility test; its expected infeasibility is not a missing all-goods packing in this capacity-only gate |
| ACC-2 | The 38-truck lane, 2028 -> 2574 TL, run 5 Aug | Run (a) production-style 7 Aug only, 21 reserved, production profile, and (b) the labelled both-dates benchmark, preserving 21/20 reservations and each date's rules; the benchmark never commits as production. Thirty-eight is the historical built count, not a cap or target. Lane set confirmed (Q5): the three real lanes plus this reference case. Pass when the result is valid, reconciles, meets the CL-05 contract at the configured seconds per load, and reports fullness per CL-03 |
| ACC-3 | Hand-over from first-fit-decreasing | Q6 answered: the careful method runs whenever FFD is above a valid same-model integer bound and time remains; equal to the bound never triggers; gaps reported in trucks and percent. FX-GAP boundary tests are the acceptance tests |

---

## 2. Invariants - true for every M5 output

Check all applicable invariants after each M5 test that produces a plan, using its declared model and profile.
Tom's M4 review showed that conservation alone does not prove the selection is right,
so these are necessary, not enough.

Apply the invariants that fit each test's scope. A 1-D maths fixture does not need a trailer
layout. For tests that are meant to fail (bad input), check instead: a clear error, nothing committed,
and every unit accounted for.

| ID | Invariant |
|---|---|
| INV-01 | Every unit in = shipped + left over + rejected (with reason). Checked **per source line, product and group**, not only as a grand total. Goods carried to a later date are moved, not copied |
| INV-02 | No truck is over its weight limit: 45,000 lb by default, or the per-truck value from the input. Pallet tare counted once |
| INV-03 | Every truck has a real floor layout inside its size: 630 x 100 x 108 in by default, or the per-truck size from the input. Cube alone is not enough |
| INV-04 | No stack is taller than the trailer's inside height, deck included, and stacking limits are kept |
| INV-05 | Trucks used on a lane and ship date <= reserved on that lane and ship date, **across all groups together** |
| INV-06 | Two different do-not-mix groups never share a truck |
| INV-07 | Nothing ships before its `AVAIL_DATE`, checked for **every unit inside a stack**, not just the stack |
| INV-08 | Nothing ships outside its allowed date window under the current pull-forward settings (R-3) |
| INV-09 | Every truck is planned against the lane's binding limit, and **all** other hard limits still apply, including the configured unit-load cap. Count sum(component.unitLoads) across its rigid atoms with checked integer arithmetic, not the number of stacks, source rows or pattern copies |
| INV-10 | Any "best possible" or "proven" claim is about the **final** truck contents, after any floor repair (R-1). A repair clears an old claim |
| INV-11 | Compare complete feasible allocations using the declared full CL-02 or fixture comparator and date/priority policy, never only the best item or a stale pre-repair score. Exact Tom regressions and small oracle fixtures must achieve their stated optimum. An interrupted/larger result may be a checked incumbent only under the declared stop/commit policy and labeled unproven unless a same-objective certificate exists. If production requires proven priority optimality before dispatch, an unproven incumbent cannot commit. Comparing only the feasible alternatives found is not proof of global optimality |
| INV-12 | For runs that complete, or that stop by an agreed deterministic rule (for example a work count), the same input gives the same canonical plan on every run and thread count. Timings and timestamps are excluded. For wall-clock-limited runs, CL-09 applies: the output is valid, its status is honest, and it repeats as far as the agreed rule says (U-PF-03, X-RUN-01) |
| INV-13 | Every run can be recreated from what was recorded: inputs, master, config, commit, thread count, stop rule |
| INV-14 | Fullness and the optional minimum-load policy. Production profile (this customer): M5 withholds no truck for a minimum load; every dispatched truck carries current fullness reasons/status, weight %, rear inches and report bucket per CL-03/0.8, on final contents. Incomplete no-fit diagnostics are explicit, never a false FULL/NOT FULL conclusion. When a test selects the optional acceptance policy, every dispatched truck passes weight > x OR cube > y on final contents, first/middle/last and all groups alike, and withheld cargo stays accounted for with its reason |
| INV-15 | Every lane M5 cannot plan is named with a reason. "No result yet", "timed out" and "impossible" are reported differently |
| INV-16 | Production only: for an independent lane that M5 does not touch, with the same M4 hand-over input, M4's result stays the same. Forced benchmark runs are excluded. A related date may change through authorised carry or pull-forward; such changes must reconcile (RD-04, RD-09) |
| INV-17 | Trying candidates (method A, method B, repairs) does not use up real reservations or goods. Only the chosen plan is committed |
| INV-18 | A chosen plan has whole, non-negative counts of trucks, patterns and unit loads |
| INV-19 | Any gap identifies its objective, required goods and constraints. Its lower bound must be valid for that objective/model; a documented relaxation may supply a valid weaker bound but does not prove feasibility. A count gap cannot certify priority-optimal partial service. If no valid bound exists, show not available; never fabricate one from a restricted-master objective. |
| INV-20 | A cube or weight number never stands in for a real floor layout |
| INV-21 | Reuse a saved physical pattern only after validating its trailer, unit-load cap, per-atom unit-load composition, product master and stacking assumptions. Check stock, source dates and customer acceptance again on every copy. A physical cache hit never reuses a stale customer/profile acceptance decision |
| INV-22 | Report totals add up by lane, date, group and method. Results of method A and B are never added together |
| INV-23 | Bad numbers (negative, NaN, infinity, overflow) never turn into a valid-looking truck, gap, penalty or fill |
| INV-24 | A timeout, cancel or error never leaves a half-built pattern, a reservation used twice, or an unchecked repair in the output |

---

## 3. Regression tests from Tom's M4 review

Tom's own examples. They must exist as tests, in M4 and again through the M5 path.

| ID | Case | Expected |
|---|---|---|
| R-1a | One 630 x 100 x 108 in trailer (45,000 lb, cap 64): 20 A-LARGE items 98.75 x 40 in and 20 B-SMALL items 48 x 40 in, each 50 in high, 1,000 lb and one unit load; fixed prebuilt stacks, floor rotation allowed, all P0, ship 6 Aug 2026, one reserved truck (Tom's case; exact file R-1a.json, 16 Nov) | 25 unit loads. Tom's witness: five large items as single rows plus ten rows of two small items, 600 in long, 25,000 lb. 24 is a fail. Another valid 25-unit layout is acceptable; check it independently. Use the file unchanged (Section 40) |
| R-1b | Tom's small four-item case (exact file R-1b.json, 16 Nov): trailer FLOOR 100 x 100 in, not 630 x 100; height 108 in, 45,000 lb, cap 64. Two 60 x 60 and two 50 x 50 items, each 50 in high, 100 lb and one unit load; rotation allowed, all P0, one reserved truck | 2 unit loads (witness: the two 50 x 50 items side by side, 50 in), the truly best selection, checked by brute force. The run must use the 100 x 100 floor; shipping 3 or 4 means the default trailer was used. Use the file unchanged (Section 40) |
| R-1c | Any case where floor repair removes goods | Removed goods go back once, the selection is solved again, the old "proven" flag is cleared. If stopped mid-way, return a checked unproven result or "no result", never the old proof |
| R-2 | One reserved truck. Group A: 1 unit at P0 + 29 at P11. Group B: 20 at P0. Groups may not mix. Runnable fixture: FX-30 physical units with local synthetic x = 40,000 lb, y = 2,400 cu ft; both groups due/available on D, no carry/pull | Group B ships, as required by Tom. Both candidates are physically feasible and accepted (B has 30,000 lb/2,500 cu ft). Choosing A is a fail; compare complete priority, not most urgent item then fill. X-E2E-02 inherits this local override |
| R-3a | Both date rules off. An Aug 6 line and an Aug 8 line that could stack together. Window Aug 6-7 | The Aug 8 line does not ship on Aug 6 or Aug 7, including inside a mixed-date stack. It stays accounted for |
| R-3b | Same as R-3a with pull-forward on, limit 10 days | The Aug 8 line **may** ship Aug 6, if availability, group and reservations also allow it |
| R-4a | FX-30 units: 30 overdue P11 due D-1 versus 30 P0 due D, both available D and eligible on D; one group, one reservation on D, carry on; date-first | The 30 overdue P11 ship. Both 30-unit alternatives are physically valid and accepted under the synthetic FX-30 policy |
| R-4b | Same complete fixture as R-4a; priority-first | The 30 P0 ship; both candidates remain accepted and physically feasible |
| R-4c | Same complete fixture as R-4a; order setting omitted | Confirmed default is date-first (Q7), so the 30 overdue P11 ship |
| R-5a | Tom's 14-stack single-truck case on the standard 630 x 100 floor (3da08ab review): 22 unit loads in 616 in were reported proven while 23 fit in 617 in under the same weight, height and unit-load limits; same priority and date. Exact inputs now available (Tom's e6dbb0f review package): `exactness-counterexample.json` (13 stacks plus a 100 x 100 distractor, products P100..P112 in that order) and the nine distractor variants of `proof_standard_probe.cpp` with `monotonicity-standard-results.json` | 23 unit loads in every one of the nine variants (distractor 100 x 100, 120 x 60, 96 x 60, 80 x 80, 100 x 60, 120 x 48, 140 x 60, 90 x 90, 100 x 80). `proven = true` only for a truly best truck, checked by an exact oracle that tries every grouping into rows. Use the files unchanged (sizes, order, product names). M4 at f9d9945 gives 23 in all nine, proven (REPORTED, m4Fix2Verification.txt A3c). Fixtures: Tom's files, suggested home `tests/fixtures/tomReview/e6dbb0f/` (synthetic data, committed by Saif). Saif's 16-stack reconstruction stays a separate M4 test. Tom's 16 Nov package repeats the case as R-5a.json (Section 40). The 617 in layout is a witness, not a required layout: at d028f24 Tom's replay selects another valid 23-unit set in 624 in, proven |
| R-5b | Randomized single-truck cases, fixed seeds, through final selection: 6-10 stacks; 11-16 stacks (the region where M4 judged rows by two sorted orders before f9d9945); 14-20 stacks of few distinct footprints. Mixed footprints (Tom's list 72 x 60, 80 x 48, 120 x 48, 48 x 48, 96 x 40, 60 x 60, 40 x 33, 100 x 50, 110 x 40, 90 x 55, 80 x 60, 100 x 60 plus 48 x 36, 48 x 40, 40 x 48 and narrow), one- and two-high stacks, one or two priorities. Oracle: exact row-grouping search (every grouping into M4 candidate rows, dynamic programming over subsets or, for few footprints, over counts), not the solver under test | The selector's truck is never worse than the exact best, and `proven = true` is never given to a worse truck, at every stack count. When a limit leaves a potentially better allocation unresolved, the route is unproven and names the limit. A validated witness proves feasibility; an independent applicable bound may still certify its stated objective without exhaustive row enumeration. An unproven route may ship less than the best but never claims otherwise. The positive fixed-corpus oracle variants use a declared sufficient deterministic allowance and must complete at their exact optimum with the required scoped proof; marking every result Unproven cannot pass those variants. Deliberate state/node-limit injections are separate failure-state variants and never replace the positive corpus. REPORTED at f9d9945 (m4Fix2Verification.txt A4): about 12,000 oracle cases, no wrong proof; 6-20 stacks all proven and equal to the best; from about 21 stacks the knapsack node limit makes routes unproven. Controls: e51d9c4 shipped less than the best on 37 percent of the 11-16 stack cases, all proven (kept as regressions); 92 failures on 7cef94e kept as regressions |
| R-6 | One truck, two do-not-mix groups, all priority 0. Group A: twelve 60 x 60 stacks, only ten fit. Group B: eleven 48 x 40 stacks, all fit (Tom's 3da08ab review) | Group B gets the truck and ships eleven, through `selectLoads`/`planShipDates` in M4 and through the M5 allocation. Groups compete on their best feasible truck, not on area estimates |
| R-7 | Two independent FX-30 lanes, each one direct-seam slot, 30 P11 due D-1 and 30 P0 due D, all available D, carry ON; AgeFirst defaults, then PriorityFirst override for lane A, then remove it | A/B/A configuration sequence: both ship overdue; only A ships current P0 after override; both ship overdue again after removal. Same-process cached/multi-lane runs match fresh runs, and lane B never inherits A's setting. AgeFirst run-wide stays the current behaviour: at f9d9945 M4 has one `selection.pastDueOrder` (AgeFirst) and no per-lane override (REPORTED, m4Fix2Verification.txt A6). Tom: AgeFirst stays current until a change is agreed; a per-lane choice needs a lane override and an agreed milestone. The per-lane override is therefore deferred: the M4/M5 integration variants are conditional and explicitly unselected for current M5 sign-off, with BLOCKED status and the deferral recorded. Direct profile-isolation and current run-wide controls remain required. No-override real fingerprints remain unchanged |
| R-8 | Tom's eleven-item single-truck case (e6dbb0f review): eleven single-unit-load stacks, equal priority and ship date, 1,000 lb each, height 50 in, one reserved truck. They fit in 586 in (`verified-layout.json`, 11,000 lb); M4 shipped ten in 530 in with `proven = true`, also through `planShipDates`, because above ten stacks only two sorted row orders (640 in) were tried. Inputs: `floor-proof-results.json` (items P100..P110, order preserved, copyIndex i) and `verified-layout.json`; plus Tom's `floor_proof_probe.cpp` deterministic search, seed 120112, 15,000 trials | 11 unit loads in a valid layout of at most 630 in, through M4 `selectLoads` and `planShipDates` (date rules on, window 10) and through the M5 direct seam; the layout is checked independently as rectangles (unique items, inside 630 x 100, no overlap; `verified-layout.json` = 586 in, 11,000 lb). A valid supplied or heuristic layout proves feasibility immediately; it need not exhaust every grouping. A heuristic refusal or exhausted floor search is `Undecided`, never certified infeasibility. A potentially better unresolved load prevents an optimality claim unless an independent same-objective certificate closes the gap; name the limit and proof scope. Tom's probe: "No counterexample found" over its 15,000 trials, counted so that the search is not vacuous: the probe gate must use the sorted arrangement (`arrangeFloorSorted`) because `arrangeFloor` itself became exact at f9d9945; 503 trials then reach selection and all ship eleven (REPORTED, m4Fix2Verification.txt A3a/A3b). M5 validates final witnesses and applicable optimality certificates; it never promotes an unresolved negative floor verdict to a proof |
| R-9 | Unit-load cap from configuration (Tom's `capacity_probe.cpp`, e6dbb0f review): 34 two-high stacks of 48 x 36 in (height 100 in, 1,000 lb each), one reserved truck on the standard 630 x 100 floor, the cap set by the customer/equipment limit (64, then 68) | Cap 64: 64 unit loads; cap 68: 68 unit loads, in independently checked layouts within 630 x 100 in and all other hard limits. The unchanged reference probe reports 576 in and 612 in respectively, both proven with zero invariant violations (`capacity-results.json`); those lengths are reference measurements, not unique-layout requirements for another valid M5 packing. Cap/demand upper bounds plus valid witnesses certify these single-truck unit-count optima. The supplied v8 summary describes 1,000 lb per completed two-high stack; import the original fixture's component weights unchanged and check their sum. That source summary is not an independent remeasurement. The cap comes from the customer/equipment limit, never a universal 64 and never a blanket 68: M4 reads `selection.maxUnitLoadsPerTruck` (64 for this customer, one global value at f9d9945, REPORTED A5); M5 takes the cap from its trailer/equipment capability per slot (per slot where the input allows) and rejects a missing or non-positive cap. Tom: the 68 example is a physical capacity example, not authorisation to raise every customer's cap |

---

## 4. Unit tests by module

### 4.1 Routing into M5 (the escalation seam in `selector.cpp`)

Precondition for U-RT-01, U-RT-02 and U-RT-06: the routing setting in Section 0.4
(reserved trucks, per lane and ship date, threshold 20).

| ID | Test | Expected |
|---|---|---|
| U-RT-01 | Lane-date with 20 reserved | Stays in M4 |
| U-RT-02 | Lane-date with 21 reserved | Sent to M5. The 21 is the shared pool for all groups on that lane-date |
| U-RT-03 | Threshold read from config (`m5Threshold`), not hard-coded | Set T = 50: 21 reserved stays in M4, 51 goes to M5. Production T is an engineering choice supported by a benchmark using the actual routing feature and final timing contract (CL-08) |
| U-RT-04 | Count mode | Only "reserved trucks" is built (CL-08, proposed). A config asking for a demand-based count is rejected with a clear message |
| U-RT-05 | Count scope | Only "per lane and ship date" is built (CL-08, proposed). A config asking for per-lane counting is rejected with a clear message |
| U-RT-06 | Reserved 0, valid demand present | Under FX-ROUTE: no M5, no trucks, report no reservation. Test-only forcing the solver still permits zero dispatched trucks and does not create capacity |
| U-RT-07 | Lane over threshold but all goods are raw materials / WIP (e.g. SHIP_COND TL_N102) | Not planned. Out of scope |
| U-RT-08 | Backwards asserts in `testM4Benchmark.cpp` (around lines 552 and 554) | Find them at the current commit. They should check the chosen threshold, not "no rule is supportable" |

### 4.2 Splitting a lane

| ID | Test | Expected |
|---|---|---|
| U-SP-01 | Lane with two ship dates | Two pieces, each with its own reservations and date rules |
| U-SP-02 | Lane with two do-not-mix groups | Two cargo plans that **share one** reservation pool. No goods move between them |
| U-SP-03 | Client lane id with underscores (e.g. 1757_1724_1725) | One origin, one destination. Not multi-stop |
| U-SP-04 | Group with a single unit load | Gets a clear outcome: shipped, left over or rejected. Never dropped |
| U-SP-05 | Same-site goods that may mix | Kept in one piece |

### 4.3 Pattern building (method A: best single-product truck)

| ID | Test | Expected |
|---|---|---|
| U-PB-01 | Product with exactly N full trucks of goods | N copies, no remainder (if reservations allow) |
| U-PB-02 | Product with N trucks plus a part truck | N copies; the part goes to the remainder exactly once |
| U-PB-03 | Product with less than one truck of goods | No pattern. All goods go to the remainder |
| U-PB-04 | Pattern uses M4's `floorPattern` | Wide-wide, wide-narrow, narrow-narrow and pinwheel tried where they fit. No new M6 pattern generator |
| U-PB-05 | Footprint rule | TLD and GMA use the layer exactly; PGM and PTL use the larger of 48x40 and the layer, per dimension |
| U-PB-06 | Pallet deck | 6 in for PTL and PGM; 0 for TLD and GMA. Counted once per pallet |
| U-PB-07 | Stacking | Stack up to the height ceiling where M4's stacking rules allow (2, 3 or more high). Never assume two-high. Never stack what M4 marks as not stackable |
| U-PB-08 | Weight-bound product (Build) | Pattern stops at the weight limit, not the floor |
| U-PB-09 | Product taller than the trailer | Rejected and reported. Never placed |
| U-PB-10 | Single unit load heavier than the trailer limit | Rejected and reported |
| U-PB-11 | Pattern is checked as best on its final contents | Same rule as R-1c. If no proof is available, it is labelled as not proven |
| U-PB-12 | Real data: 7 Aug, 2028 -> 2574 | Products 105666801 and 105446303 each produce a pattern. Compare with the client's historical loads: 8 trucks of 32 and 6 trucks of 34 (unit loads from the older master). M5 does not have to copy these, but must report the difference |

### 4.4 Remainder and the last truck

Production profile for this customer: no minimum-load acceptance inside M5 (CL-01); every truck that is
physically valid and allowed by dates, groups and reservations may ship, labelled by CL-03. The U-AC tests
below exercise the optional acceptance policy with synthetic values; each one also has a `productionProfile`
variant described in Section 4.14.

| ID | Test | Expected |
|---|---|---|
| U-RM-01 | Remainder goes to M4's packers | Use the shared lower-level M4 packing/checking kernels through the adapter. Never re-enter normal selector routing and recursively escalate the residual into M5 |
| U-RM-02 | Remainder is empty | No extra truck created |
| U-RM-03 | A truck could be fuller by moving goods between trucks of the same group and date | Production: rebalance only when the complete plan improves under CL-02 (same or better class vector, then better fill); recheck donor and recipient. With the optional acceptance policy selected: rebalance to make the plan accepted, as in X-REB-09 |
| U-RM-04 | Fullness and optional minimum acceptance remain independent | Production OFF: a valid below-full truck may ship, with current labels and bucket. Enabled policy: withhold only if the numeric predicate fails. Controls: 28 of 29 available FX-30 units are 42,000 lb/3,500 cu ft, rear 70 in, another unit fits: accepted by default synthetic x/y but NOT FULL. Conversely 30 FX-30 units are FULL by weight but fail explicit synthetic x = 50,000/y = 4,000. Never substitute a FULL label for acceptance |
| U-RM-05 | Use usable reservations without inventing cargo | Do not leave a feasible service improvement unserved merely to save trucks. Unused slots are legitimate when no remaining eligible goods can form a legal nonempty load on an available supported trailer, or the declared budget/stop contract prevents further planning; report the exact reason and unproven status where applicable. Physically impossible cargo must not force an illegal load or endless retry. Include can-go under the recorded working contract; never create empty trucks or split indivisible units |
| U-AC-01 | Acceptance rule, both limits. Synthetic x = 40,000 lb, y = 3,000 cu ft. Trucks: 40,000 lb / 2,000 cu ft; 40,001 lb / 2,000 cu ft; 20,000 lb / 3,000 cu ft; 20,000 lb / 3,000.1 cu ft | Only the second and fourth are accepted. Equal is not more than (strict ">", CL-01) |
| U-AC-02 | Rule applies to every truck. A lane with five trucks where the second truck, not the last, fails the rule | The second truck is not shipped. No special treatment for first, middle or last trucks |
| U-AC-03 | Rule applies across do-not-mix groups. Two groups, each with one truck that fails | Both fail on their own. Goods are never mixed across groups to pass the rule |
| U-AC-04 | Read x/y and profile/config identity; change values; test missing, malformed, NaN, infinity and overflow; separate zero/range variant | Different finite declared thresholds change acceptance. Missing/malformed/nonfinite/overflow values are errors, never synthetic fallbacks. Zero and allowed numeric range follow the explicitly recorded validation domain; that variant is BLOCKED on the engineering domain definition until set. Do not reject a value merely because one OR branch cannot be reached on one trailer; the other branch may pass. Never lower the customer thresholds automatically |
| U-AC-05 | Physically floor-full truck below x/y, under the optional synthetic x/y-only policy | Synthetic expectation: withheld; floor fullness is not an override in this profile. Always preserve nonstackability and report floor occupancy/weight/cube. Production profile: use the explicit production display fixture and evidenced fullness rule in Section 4.14; the label does not gate dispatch |

#### U-AC-06 Acceptance never overrides a hard constraint

Priority: P0. Execution status: NOT_RUN. Evidence scope: SYNTHETIC.

Scope: Final dispatch validator, with an otherwise correctly formed candidate and traceable source units.

Setup:

- Synthetic customer x = 40,000 lb, y = 3,000 cu ft, strict OR.
- Start with 30 FX-30 units on the default trailer: 45,000 lb, 3,750 cu ft, same group, all available and due on D, one reservation.
- Run separate variants that introduce exactly one violated hard constraint: reduce the truck weight limit to 40,000 lb; make the final layout exceed a physical dimension; introduce an unavailable component; make a component due after D with pull-forward off; mix two incompatible groups; or provide zero reservations.
- Each variant still meets the customer predicate through weight or cube.

Expected:

- Every invalid variant is rejected before dispatch despite satisfying the customer minimum.
- The reported reason identifies the violated hard constraint.
- No cargo or reservation is committed from the invalid candidate.
- The unchanged control candidate passes the validator.

This tests the composition of customer acceptance with physical and eligibility validation, rather than duplicating the arithmetic truth table already in U-AC-01.

#### U-AC-07 Recompute acceptance after a final repair

Priority: P0. Execution status: NOT_RUN. Evidence scope: SYNTHETIC.

Scope: Final-validation/state seam; use controlled mutation or fault injection rather than inventing an unsupported floor geometry.

Setup:

- Synthetic x = 40,000 lb and y = 3,000 cu ft; strict OR.
- Candidate initially contains 25 traceable FX-30 units, all same group/date, one reservation.
- Before commitment, inject a repair that removes one unit, leaving 24. Also run a variant that interrupts immediately after the mutation.
- Keep the pre-mutation acceptance/proof fields in the injected stale state to ensure the final check does not trust them.

Expected:

- Initial totals: 37,500 lb and 3,125 cu ft; accepted on cube.
- Final totals: 36,000 lb and 3,000 cu ft; fails both branches, including cube equality.
- The stale accepted/proven flags cannot authorize the mutated truck.
- Rebuild and revalidate a valid alternative, or withhold this candidate. A timeout cannot publish its stale acceptance.
- Removed and unshipped units reconcile once. Provisional reservation claims are released once.
- A prior incumbent may be returned only if it is independently valid under the current rules and permitted commit contract.

#### U-AC-08 Customer and configuration changes invalidate cached acceptance

Priority: P0. Execution status: NOT_RUN. Evidence scope: SYNTHETIC.

Setup:

- Twenty FX-30 units form the same physically valid candidate in all runs: 30,000 lb and 2,500 cu ft.
- One group, all available and due on D, one reservation; fresh identical inventory and reservation snapshots for each run.
- Profile A: x = 40,000 lb, y = 2,400 cu ft.
- Profile B: x = 40,000 lb, y = 3,000 cu ft.
- Run A, then B, then A in one process; compare each with a fresh process. Repeat by revising the settings of the same profile identifier.

Expected:

- Acceptance is true, false, true respectively.
- Reusable physical geometry may remain cached when valid, but its accepted/rejected decision is recomputed against the effective profile and configuration revision.
- Rejected runs do not consume inventory or reservations needed by later independent snapshots.
- Reports identify the effective customer/profile and exact thresholds.

### 4.5 Method B: rolling blocks of 19

| ID | Test | Expected |
|---|---|---|
| U-RB-01 | Build: goods for exactly 38 full trucks, enough reservations | 19 + 19 |
| U-RB-02 | Build: goods for exactly 21 full trucks, 21 reserved | 19 + 2. No empty padding trucks |
| U-RB-03 | Block size read from config | Changing 19 changes the blocks. 0, negative and non-whole values rejected |
| U-RB-04 | A and B run from isolated copies of the same lane input under an explicitly selected method policy | Both start with identical goods/reservations/config; report real method statuses and commit only the selected checked result. Test the declared shared budget and comparator; independent benchmarks use equal separate allowances. Q3 delegates the production method choice to the team; record it before final acceptance. A not-started/timed-out B is not a zero-truck success |
| U-RB-05 | A and B use the same number of trucks | Not automatically a tie: first compare goods shipped and priority (CL-02). Only a true tie uses the fixed tie-breaker |

### 4.6 Column generation (cutting stock)

Core numeric cases use FX-COUNT: all demand required and no customer dispatch minimum unless the case explicitly adds it. Physical dimension cases exercise the geometry seam without a dispatch claim.

| ID | Test | Expected |
|---|---|---|
| U-CG-01 | Small cases, up to 10 items, also solved by brute force | Same number of trucks as brute force on every case, and a valid packing |
| U-CG-02 | Standard published cases with a known best answer | PUB-01 to PUB-03 (this is ACC-1) |
| U-CG-03 | One item type only | One pattern, repeated; any part remainder kept. Never round up into shipping more than was ordered |
| U-CG-04 | Item exactly the width or length of the trailer | Fits, no rounding error |
| U-CG-05 | Item one unit too big | Does not fit, unless a different allowed turn fits. No tolerance lets it in |
| U-CG-06 | Iteration cap reached | Stops; returns status, best checked result if any, its gap if a valid bound exists, and the goods left |
| U-CG-07 | Time limit reached, before and after a first result | Before: "no result found", all goods kept (this is not "impossible"). After: the checked result and an honest gap |
| U-CG-08 | Reuses M4's shared `columnGeneration` | No second copy of the code |

### 4.7 Hand-over from first-fit-decreasing

| ID | Test | Expected |
|---|---|---|
| U-FF-01 | FX-GAP: F - B exactly G = 1, with positive remaining allowance | Escalates under the synthetic >= boundary, which matches the answered Q6 production count rule. Record a valid same-objective bound and the actual remaining allowance |
| U-FF-02 | FX-GAP: F - B = G + 1, with positive remaining allowance | Escalates under the declared gap rule. With no allowance left, record budget exhaustion instead of starting fresh time |
| U-FF-03 | FFD already equal to a valid lower bound | Never escalates. A zero gap from a wrong bound or a rounded number does not count |
| U-FF-04 | Gap config: positive whole trucks; FX-GAP uses G = 1; invalid 0, negative, fractional, NaN and overflowing values | Reject invalid values in the explicit FX-GAP domain. G = 1 is the current production-equivalent setting; other synthetic settings are labelled as such and cannot silently change the answered F > B production trigger |
| U-FF-05 | Report | Every escalated lane named with its gap (preview box 3C) |

### 4.8 Reservations, priority and dates

| ID | Test | Expected |
|---|---|---|
| U-RS-01 | Trucks used never exceed reserved, per lane and ship date | Holds on every lane |
| U-RS-02 | Unused reservations and penalty | Reserved minus used, per lane-date. Penalty applied once. Missing rate shown as unknown, not zero (CL-06) |
| U-RS-03 | Fewer trucks than goods | Full priority decides, across groups (R-2) |
| U-RS-04 | Priority ties | Broken the same way every run |
| U-RS-05 | Priority outside 0-11 | Test -1, 12, 99, empty, text, 1.5. Rejected and reported (`kMaxPriority = 11`) unless Tom confirms 99 is real. Never silently clamped |
| U-RS-06 | Order setting: ship date first | Older goods ship first (R-4a) |
| U-RS-07 | Order setting: priority first | Urgent goods ship first (R-4b) |
| U-RS-08 | Carry forward on | Goods not taken move once to the lane's next reserved date. No next date = left over, not a made-up reservation |
| U-RS-09 | Pull forward on, 10 days | Goods due D+9 and D+10 can come in; D+11 cannot. `AVAIL_DATE` still applies |
| U-RS-10 | Pull forward off | Nothing ships early, including in mixed-date stacks (R-3a) |
| U-RS-11 | Goods available after the ship date | Not shipped on that date |

### 4.9 Units and data

| ID | Test | Expected |
|---|---|---|
| U-DA-01 | CS, DIS and PAL all divided by `Cases_Unit_Load` | Correct unit loads. A DIS is not counted as a whole unit load (e.g. 432 DIS / 18 = 24) |
| U-DA-02 | Wrong unit of measure (e.g. eaches) | Caught. Does not create hundreds of trucks |
| U-DA-03 | Product missing from the master | Reported by product id. Not silently dropped |
| U-DA-04 | Pallet-type variant chosen by whole pallets | Same rule as M4. Ambiguous or non-whole results reported, never silently rounded |
| U-DA-05 | Per-truck size or weight given in the input | Overrides the default trailer |
| U-DA-06 | Multi-stop, temperature class or combo lane appears | Run stops with a clear message (these stay parked) |

### 4.10 Report output

| ID | Test | Expected |
|---|---|---|
| U-RP-01 | Run header, Section 0.7 / preview 3A structure | Production OFF: print input/master/config identity, scope/profile, export-after-M5 note, fullness rule, actual rear default/override provenance, routing/order/can-go settings, secondsPerLoad and frozen lane-date allowance; missing x/y is valid. Optional enabled profile prints its exact thresholds, units and strict OR and rejects incomplete/malformed values |
| U-RP-02 | Lane report, Section 0.7 / preview 3B structure | Print client lane ID, M4 group ID, shared lane-date reservations, final checked cargo, fullness reasons/status, exact rear metric and report bucket, leftovers and penalties. AcceptanceWithheld is possible only with the optional predicate ON. Label the reported 22-truck estimate honestly unless a valid certificate supports it |
| U-RP-03 | Method evidence, Section 0.7 / preview 3C structure | Print known optimum, independent full packing check, LP/integer/bound/proof distinctions and actual escalation gap. Q6 is answered: show its same-model F > B trigger and stop reason; do not retain a pending-Q6 blocker or turn count equality into a full allocation certificate |
| U-RP-04 | Threshold benchmark, Section 0.7 / preview 3D structure | Print M4/A/B results with actual reserved counts per lane-date and historical 12/16/20/23/38 labels kept separate. Joined-date 23/38 rows are benchmark only; preserve per-date reservations. Record goods shipped/left, workload, timing contract and cache state |
| U-RP-05 | Summary, Section 0.7 / preview 3E structure | Selected production plan for 7 Aug compares with historical 18; 38 belongs only to the labelled both-dates benchmark. Print elapsed, selected dispatched trucks, average or N/A and the frozen secondsPerLoad x reserved allowance separately. Do not require a per-load maximum or average <= secondsPerLoad unless that is a separately declared diagnostic; those are not the current acceptance formula |
| U-RP-06 | Output is plain ASCII | Byte check on the whole report |
| U-RP-07 | Fill comparison | Average rear inches per bucket (goods remain / no goods remain / rule-limited) beside Tom's weekly chart ranges as context, plus the client's average cube (same master, average with average). 3,300-3,400 cu ft printed as the most ever seen, not a target |

### 4.11 Speed, threads and repeatability

| ID | Test | Expected |
|---|---|---|
| U-PF-01 | Each large lane; declared timing contract and valid result | Allowance = secondsPerLoad x reserved trucks (CL-05). Record time per truck, group, lane-date and total, loaded truck count, and average or N/A. Fake-clock tests pass now; real runs use the configured seconds per load |
| U-PF-02 | Benchmark M4/A/B on identical eligible goods and constraints for historical sizes 12/16/20/23/38 | Record real reserved counts per lane-date, product/group/date complexity, machine/threads and cold/warm cache state. Independent comparison gives equivalent declared allowances to each method. The production threshold is based on the actual routing feature and approved timing metric, not silently on historical or joined-date built counts |
| U-PF-03 | Same input on 1 thread and many threads | Same canonical plan for completed runs or runs stopped by an agreed deterministic rule. Clock-limited runs: valid output and honest status, per CL-09 (INV-12) |
| U-PF-04 | `testM4Benchmark.cpp` thread test (around line 497) that crashed under `--success` | Runs clean under `--success`, three runs out of three, fixtures present |
| U-PF-05 | Time contract arithmetic and configuration: secondsPerLoad 1 and 2.5 with 21 and 20 reserved trucks; invalid values 0, negative, NaN, missing | Allowances 21 s, 20 s, 52.5 s and 50 s declared before search and never enlarged; invalid values are configuration errors. The setting is per customer; changing it changes only the allowance |

### 4.12 Fullness labels (CL-03)

| ID | Test | Expected |
|---|---|---|
| U-FL-01 | Controlled label seam: 30 traceable FX-30 source units; candidate contains 29, then 30; one reservation; production OFF | 29: 43,500 lb, rear 50 in, the 30th eligible same-group unit demonstrably fits -> NOT FULL. 30: 45,000 lb, rear 30 in -> FULL by weight. Final residual bucket is GoodsRemain then NoGoodsRemain; no-fit is not inferred from empty stock |
| U-FL-02 | Controlled label seam: 31 traceable nonstackable TLD units, each 20 x 100 x 108 in and 300 lb final, default trailer, due/available D, one group/reservation; candidate 30 then 31 | 30: rear 30 in, 9,000 lb; the 31st fits -> NOT FULL. 31: rear 10 in, 9,300 lb -> FULL by rear (<= 14). No extra deck/tare; these are supplied candidate-label checks, not an assertion that the underfilled candidate is optimal |
| U-FL-03 | Rear limit: missing, 14, 10, 30; malformed, nonfinite, negative, zero and out-of-range inputs tested separately | Missing uses default 14 and reports default provenance. Finite positive overrides within the recorded supported range change labels. Malformed/nonfinite/negative values fail before planning. Zero and maximum-range behavior follow an explicit engineering domain; conditional domain variant stays BLOCKED until recorded. Do not describe missing as both invalid and defaulted |
| U-FL-04 | Production display label fixture: 31 available/due-D X-E2E-09 nonstackable displays, one group/reservation; selected 30 use 600 x 96 in; one remains | FULL by floor limit only with a complete supported-layout no-fit witness for the remaining display. Rear 30 in and weight 24,000 lb do not independently establish FULL. Record the witness/model; never infer no-fit merely from exhausted inventory or a failed heuristic. A fitting same-group control clears the floor-limit reason; labels never change dispatch |
| U-FL-05 | Final selected-plan buckets: eligible residual, empty residual, only rule-excluded residual, mixed residual, and reversed truck order | Use Section 0.8 final lane/date/group residual scope. Eligible residual takes GoodsRemain precedence; empty source residual -> NoGoodsRemain; only date/order-excluded residual -> RuleLimited. Buckets do not change cargo or depend on truck construction order. Compute truck-weighted bucket means; empty buckets are N/A (X-REPORT-10) |

### 4.13 Can-go fill (FX-CANGO)

| ID | Test | Expected |
|---|---|---|
| U-CF-01 | M5 lane-date D with spare space after all goods due by D are loaded; can-go goods due D+3 available on D | Can-go goods fill the spare space only after every eligible good due by D in that group is planned. They never displace a good due by D |
| U-CF-02 | Can-go window and availability: goods due D+10 and D+11; goods due D+5 available D+1 | D+10 may be loaded; D+11 and goods not available on D are not. The window is the configured pull-forward limit |
| U-CF-03 | One lane: D has 21 reservations (normal M5 routing), D+1 has 20 (normal M4). Ten FX-30 units due D and twenty due D+1, all available D, one group; pull ON10/OFF; also direct-M5 small fixture | Selected eligible can-go units are removed from the later date once, never by unselected candidates. OFF retains all future units for D+1. Reconcile both dates by source. The integration variant actually reaches M5 at 21 > 20; direct variants state their seam (U-CF-06) |

### 4.14 Production-profile variants of tests written for the optional acceptance policy

These IDs keep their synthetic expectations above and below. Each applicable ID also runs a required `productionProfile`
variant (acceptance OFF, FX-FULL labels) with this expectation:

| ID | productionProfile expectation |
|---|---|
| `U-AC-01`, `U-AC-04` | Predicate and its config are not used; an absent acceptance section is valid; a present but malformed one is still a configuration error |
| `U-AC-02`, `U-AC-03` | No truck is withheld for minimum load; each truck carries its CL-03 label |
| `U-AC-05` | Use the 31-source/30-selected production display fixture of U-FL-04: selected truck ships; FULL FloorLimited requires its complete supported no-fit witness |
| `U-AC-06` | Unchanged: every hard-constraint violation still rejects the candidate |
| `U-AC-07` | Repair still clears stale validation and fullness labels; labels recomputed from final contents |
| `U-AC-08` | Use a DIFFERENT explicit physical fixture: 31 nonstackable TLD units 20 x 100 x 108 in, 300 lb each, all available/due D, one group/slot; controlled candidate contains 30 (rear 30 in, 9,000 lb), one eligible unit remains and fits. Limits 14/30/14 produce NOT FULL / FULL by rear / NOT FULL. Repeat same profile ID with revised settings and fresh snapshots. Original 20-unit synthetic acceptance A/B/A remains unchanged |
| `U-RM-03`, `U-RM-04` | As stated in their rows (production clauses) |
| `X-REB-04` | All three scalar bins ship with acceptance OFF and all items conserved. Physical fullness, rear inches and physical buckets are NOT_APPLICABLE: this model has no trailer layout. Do not invent physical labels |
| `X-REB-07` | Choice by CL-02: the P0 truck |
| `X-REB-09` | All 50 or all 49 units ship in two legal nonempty trucks; no unit is left solely for numeric acceptance. 30+20, 25+25 and 30+19 are witnesses, not unique production optima. Other legal full-service splits remain valid unless the recorded complete comparator chooses a canonical one |
| `X-E2E-04` | Both 60%-cube trucks ship, labelled by bucket (no goods remain) |
| `X-E2E-09` | Original 30-source acceptance fixtures are unchanged. Production label variant adds one identified eligible 31st display, selects 30, and records a supported no-fit witness: dispatch regardless of x/y, FULL FloorLimited only on that evidence. All 31 sources reconcile |
| `X-RES-12` | Big trailer 30 units and small trailer 20 units both ship |
| `X-REPORT-04` | Header prints "minimum-load check: at export, after M5"; no x/y values printed |
| `U-RP-01` | Header prints the fullness rule and the export note |
| `RD-03`, `U-PB-12`, `X-E2E-01` | Run with the production profile; no x/y dependency |
| `RD-12` | Replaced by the rear-inch comparison in its row |
| R-2, X-E2E-02 | Unchanged expected result: group B ships |
| `X-CG-19` | Not applicable (scalar acceptance-adapter model only) |

---

## 5. Edge cases checklist

Each needs at least one test. Tick when the test exists **and** has a current result.

| ID | Edge case | Covered by |
|---|---|---|
| EC-01 | Lane-date with exactly 20 reserved | U-RT-01, X-ROUTE-01 |
| EC-02 | Lane-date with 21 reserved | U-RT-02, X-ROUTE-01 |
| EC-03 | Lane over threshold on one date, under on the other | U-SP-01, X-ROUTE-03, real data 5 Aug |
| EC-04 | Large lane with zero reservations | U-RT-06 |
| EC-05 | Large lane that is weight-bound | U-PB-08, X-PAT-14 |
| EC-06 | Product that fills exactly N trucks | U-PB-01, X-PAT-01 |
| EC-07 | Every product has less than one truck | U-PB-03 |
| EC-08 | Only one product on the lane | U-CG-03 |
| EC-09 | A truck is below fullness, or fails the optional acceptance policy, including a donor or repaired truck | U-RM-03/04, U-AC-02/07, X-REB-04/09 |
| EC-10 | More pattern copies than reservations | U-RS-03, X-RES-02 |
| EC-11 | Floor repair removes goods from a proven or accepted truck | R-1c, U-PB-11, U-AC-07, X-REB-05 |
| EC-12 | Low-priority group beats high-priority group on fill | R-2, X-RES-06 |
| EC-13 | Mixed-date stack with pull-forward off | R-3a, X-REB-06 |
| EC-14 | Overdue low-priority vs current high-priority | R-4a/b/c, X-DATE-08 |
| EC-15 | Priority tie | U-RS-04 |
| EC-16 | TPRIO outside 0-11 | U-RS-05 |
| EC-17 | Item exactly trailer-sized / one unit too big | U-CG-04, U-CG-05, X-PAT-13 |
| EC-18 | Unit load taller or heavier than the trailer | U-PB-09, U-PB-10 |
| EC-19 | DIS units / wrong unit of measure | U-DA-01, U-DA-02, X-DATA-08 |
| EC-20 | Product missing from the master | U-DA-03 |
| EC-21 | Iteration cap, timeout, zero-dispatch result, changing candidate counts or wrong-objective bound | U-CG-06/07, X-CG-11/12/20, X-RUN-11/12/13 |
| EC-22 | Method A and B tie | U-RB-05, X-STATE-08 |
| EC-23 | Raw-material lane over threshold | U-RT-07 |
| EC-24 | Parked feature appears in data | U-DA-06, X-RUN-08 |
| EC-25 | Same input, different thread counts | U-PF-03, X-RUN-01 |
| EC-26 | Single-unit do-not-mix group | U-SP-04 |
| EC-27 | Goods available only after the ship date | U-RS-11, X-DATE-02 |
| EC-28 | Two groups share one lane-date's reservations | X-ROUTE-02, X-RES-01, X-BLK-04 |
| EC-29 | Same product split across lines with different dates or priorities | X-PAT-05, X-DATA-07 |
| EC-30 | Saved pattern reused with a different trailer, master or customer acceptance configuration | X-PAT-08/09, X-DATA-06, U-AC-08 |
| EC-31 | Several do-not-mix groups each with a part truck | X-E2E-04 |
| EC-32 | A block of method B uses up reservations needed by urgent goods | X-RUN-05, U-CF-04, X-CG-20 |
| EC-33 | Exact fullness/strict-predicate boundaries masked by report or packing rounding | U-AC-09, U-FL-06 |
| EC-34 | Empty or interrupted residual-fit checks incorrectly claim floor limitation | U-FL-08/09, U-FL-04 |
| EC-35 | Rear space measured before legal stacking or from the wrong layout extent | U-FL-07/10 |
| EC-36 | Future can-go cargo consumes scarce slots needed by feasible required cargo | U-CF-04/07/08 |
| EC-37 | Can-go-only date or selected/unselected future draw changes later M4 input | U-CF-05/06, U-CF-03 |
| EC-38 | Budget multiplication/clock overflow, parallel reset, or cross-date pooling | U-PF-06/07/08 |
| EC-39 | Optional acceptance ON/OFF state leaks into customer planning or reports | X-STATE-09, U-AC-08 |
| EC-40 | Duplicate/concurrent/conflicting commit or stale snapshot replays source units | X-STATE-10, X-RUN-07 |
| EC-41 | Bucket averages or overlapping FULL reasons double-count trucks | X-REPORT-10, U-FL-05 |
| EC-42 | A refusal by a heuristic or exhausted floor search (rows not proven not to fit) backs a proven or optimal claim, or a valid supplied witness is incorrectly rejected for incomplete search | R-5a, R-5b, R-8, X-PAT-18 |
| EC-43 | The unit-load cap is taken as a universal 64 (or raised for every customer) instead of the customer or equipment limit | R-9, X-PAT-17, X-STATE-11 |
| EC-44 | Component unit loads are confused with stacks, rows or pattern copies; equal geometry with different composition shares a cap-constrained maximum; repaired or reassigned cargo exceeds its actual slot cap | X-PAT-16, X-PAT-17, X-STATE-11 |
| EC-45 | Per-search or shared route floor-state limits reset or are bypassed; cached Undecided becomes a proof or a permanent refusal on a fresh permitted run | U-PF-09, X-PAT-18 |
| EC-46 | A relaxed unit-load count bound is mistaken for a feasible packing when indivisible atoms or different slot caps prevent that truck count | X-CG-21, X-PAT-16, X-PAT-17 |
| EC-47 | An equal-service tie always goes to the same group: a fixed group order or input order decides it, the rotation ignores the lane or the date, or a fill or truck-count rule silently replaces ED-21 | X-RES-13 |
| EC-48 | A trailer type with no payload or no unit-load limit silently takes the 53F defaults (45,000 lb, 64), or its mm sizes are rounded up instead of down | U-DA-07 |

---

## 6. Real-data checks (August fixtures)

Run on the real files. Record the fixture files used. The "Measured" and "Historical" values below
were measured earlier from the August fixtures and the M4 benchmark reports; this document did not
re-check the raw files.

| ID | Check | Expected |
|---|---|---|
| RD-01 | Aug5: lane-dates sent to M5 under the current rule (reserved, per lane-date, > 20) | Exactly 1: 2028 -> 2574 TL, ship 7 Aug, 21 reserved (Measured) |
| RD-02 | Aug5, 7 Aug: goods M5 receives from M4 | 747 stacks, 763 unit loads; S01 = 506, S03 = 210, S45 = 47. Reproduced at 7cef94e and byte-identical at e51d9c4 and at f9d9945 (m4BaselineForM5.txt, m4FixVerification.txt, m4Fix2Verification.txt). With can-go fill on M5 lane-dates (FX-CANGO) the hand-over gains can-go goods; record that count separately |
| RD-03 | Aug5, 7 Aug: trucks M5 builds | <= 21, production profile. Compare with the client's historical **18**, and show goods shipped and left over for both, plus fullness labels. Fewer trucks with fewer goods shipped is not better |
| RD-04 | Aug5, 8 Aug: 20 reserved | Not sent to M5; M4 plans it. Baseline at 7cef94e and again at f9d9945: 20 trucks, 678 of 763 offered unit loads; re-measure at the commit Tom accepts. With M5, carry from 7 Aug and can-go pulled into 7 Aug change this input; the change must reconcile unit by unit across the two dates (RD-13) |
| RD-05 | Aug4 and Aug6 under the current rule | Nothing sent to M5 |
| RD-06 | Aug5, both dates joined | Preserve 21 and 20 reservations and all date rules; compare with historical 38. Benchmark never commits as production. Report timing under CL-05 at the configured seconds per load |
| RD-07 | Aug6, 2028 -> 2190, both dates joined | Completes and is reported. Compare with 23 built. Use the real reserved counts from the placeholder file; do not assume 7 and 16 |
| RD-08 | Raw-material lanes (TL_N102) on Aug4 | Never planned |
| RD-09 | M4 figures for independent lanes M5 does not touch, same hand-over input, production run | Unchanged by M5 (INV-16). Related-date changes from authorised carry or pull-forward must reconcile |
| RD-10 | Fill comparison | Average rear inches per bucket for M4 (f9d9945) and M5, beside Tom's chart (weeks 39-45: goods remain about 13-17 in, no goods remain 36-89 in, priority 17-29 in) as context, not pass/fail. Client average cube 3,219 / 3,188 / 3,201 cu ft as before |
| RD-11 | Reservation totals add up | Reconciled (baseline B11): 1,440 in-window reservations; M4 built 1,342, unused 98; the harness's client unused 154 = 1,440 - 1,286 client trucks on reserved lane-dates (same basis as our 98). The repo's client-stated 195 = 1,503 placeholder trucks - 1,308 client loads (different basis). Report all three with their basis |
| RD-12 | Fullness of historical and planned trucks | For the client's 1,308 historical trucks (M4 layout of their goods) and for M4/M5 trucks: count FULL by weight, by rear inches and floor-limited, and average rear inches per bucket. Reporting check only; historical shipments are not used to set thresholds |
| RD-13 | Linked dates of one lane: Aug6 run, 2028 -> 2574 TL, both dates, 21 trucks | Judge the change from 7cef94e to e51d9c4 across both dates: every urgency class older than 10 Aug ships the same, 10 Aug P0 gains 16 (210 -> 226), the 32 fewer unit loads are can-go goods due 11 Aug. A per-date comparison is not used to call this a loss. M5 applies the same rule to its own related-date results. Additional required diagnostic variants `baselineAug5_2023_2508` and `baselineAug5_2299_2292` use the original Aug5 data/master/config and e51d9c4/f9d9945 per-source/date evidence: B30 reports -2 unit loads for 2023 -> 2508 TF and -1 for 2299 -> 2292 TF over their linked dates. Compare complete original urgency-class vectors under matching inputs, eligibility, reservations and objective; do not invent missing per-item deltas. An earlier shipment may offset a later-date decrease; an actual worse linked-date service vector remains a regression despite an Unproven label or improvements on other lanes. These reported deltas are diagnostics until exact source bytes are verified. A diagnostic that detects the regression may PASS while the application issue remains open; closure needs corrected same-input evidence or an explicit owner-approved disposition. Preserve proof honesty as a separate assertion |

Baseline note: the M5 baseline is M4 at f9d9945 (m4Fix2Verification.txt, 14 Nov): clean MSVC
Release build, ob_tests 1165 cases (18 benchmark cases skipped there), augustAcceptance 11, m4Benchmark 18, all twelve
extracts present; trucks 296 / 489 / 557 of 324 / 532 / 584 reserved, 98 unused; unit loads shipped +33 / +27 / +44
against e51d9c4; cube per truck 3,182 / 3,217 / 3,196 cu ft; rear waste 33.6 / 25.2 / 29.3 in; unproven routes 29 / 75 / 67
(was 32 / 73 / 66); plan fingerprints d023d0dc844bc96a / 9463a19ed51df529 / f79518632646835c (all changed from e51d9c4's
e395fa2940e686a9 / 44e0c509bf8666d8 / aefd651df561e434); the M5 hand-over byte-identical (747 stacks, 763 unit loads). Timing is
slower: Aug5 CLI middle 38.09 s (e51d9c4 21.15 s in the same session), m4Benchmark 42.2 min (e51d9c4 23.9 min), one Aug6 route
12.83 s over the 10 s budget. The e51d9c4 figures (cube 3,173 / 3,211 / 3,189 cu ft, fingerprints above) and the 7cef94e figures
(cube 3,171 / 3,209 / 3,192 cu ft; 98 unused) are the previous baselines. When a per-lane past-due setting lands with no
overrides set, the fingerprints must not move; otherwise a newly labelled baseline is required.

---

## 7. What must not move

| Figure | Value | Source and status |
|---|---|---|
| 17 Aug demand lines | 24,357 | M1-M3 delivery figures, listed in the M5 handover. Not re-checked here |
| 17 Aug lanes | 371 | Same |
| Product master | 20,201 rows, 20,183 unique ids | Same |
| TF/TL lane groups, Aug 4-6 | 454 | Client solved output; measured earlier. Not re-checked here |
| Client trucks, Aug 4-6, TF/TL | 1,308 | Same |
| M4 results for lanes at or below the threshold | f9d9945 lane-date CSVs (build-msvc-m4fix2/verify/m4LaneDates_Aug4/5/6.csv) | Baseline for INV-16/RD-09; commit under docs/m5/baselines/f9d9945/ |

Run the benchmark before the first M5 commit and before the last. Any change to these is a
regression unless it is deliberate and written down. A changed input file needs a new labelled
baseline, not a silent update. Cube, weight and unit loads only stay fixed if the master stays the same.

---

## 8. Not tested in M5 (out of scope)

- Truck Builder repair of side overhang and axle weight (M7).
- Multi-stop, temperature class, combo lanes (parked; only U-DA-06 and X-RUN-08 check they stop the run).
- Raw materials and WIP lanes (only their exclusion is tested).
- New floor-pattern generation and seven-day planning (M6). M4's existing date and floor rules **are** tested here.
- Passing the published 1-D cases does not prove a real truck is legal. Physical tests are still needed.
- Choosing a truck-type mix from a truck availability list (for example 2 of A, 5 of B, 1 of C). Tom expects
  this (14 Nov), but the current inputs carry one trailer type per run. M5 refuses a lane-date with mixed
  trailers rather than guessing (C9). Needs a client input format and its own milestone. Tom's engineer
  (16 Nov): it needs reservations keyed by lane, date and trailer type, a rule for duplicate bookings,
  and the inside sizes, payload and unit-load limit of every type. The client's files so far use only
  53F or a blank type. A single alternate type per run is supported now (U-DA-07).

---

## 9. Progress and final acceptance checklists

### 9.1 Implementation progress while answers are pending

- [ ] Declare the fixture/profile and M5 entry point for every implemented case
- [ ] Implement the required available scalar, physical, routing, state and report tests
- [ ] Execute available synthetic variants and record PASS/FAIL separately from customer acceptance
- [ ] List missing fixtures and unresolved engineering variants as BLOCKED with their reason
- [ ] Preserve exact Tom regressions and known-optimum answers; never replace missing R-1 inputs with guesses
- [ ] Validate all applicable invariants, including intended failure-state outcomes
- [ ] Keep actual customer values absent until evidenced; no synthetic production fallback
- [ ] Preserve 299 named IDs and record parameterized variants under those same IDs

### 9.2 Final M5 acceptance

- [ ] ACC-1, ACC-2 and ACC-3 pass under their final applicable contracts
- [ ] The approved required acceptance manifest has no skipped, blocked or unexecuted required cases
- [ ] All currently selected R-1 through R-9 variants and required P0 cases pass through their declared entry points; R-7 deferred integration variants are explicitly conditional/unselected, while direct isolation controls remain required. R-1a/b use Tom's exact inputs; R-5a, R-8 and R-9 use Tom's e6dbb0f files unchanged
- [ ] Every one of the 46 EC checklist entries has current evidence; required P1 cases pass or have an explicit disposition
- [ ] RD-01 through RD-13 have the required real-fixture evidence and reconciliations
- [ ] PUB-01 through PUB-03 pass with full downloaded fixtures, real checksums and independent packing checks
- [ ] X-CG-18 and X-CG-19 pass their distinct exact models and honest status/proof requirements
- [ ] Seconds-per-load contract and rear-inch default confirmed or unchanged after notice to Tom; current AgeFirst run-wide controls pass. P9/per-lane integration is explicitly deferred and excluded from this gate until an agreed change/milestone selects it
- [ ] The team records the delegated Q3 method policy and the CL-08 routing mechanism/threshold evidence
- [ ] Every CL definition used by acceptance is recorded, including comparison, packing atom, repeatability and commitment
- [ ] Preview/report expectations and the accepted decisions agree; no 84% fullness, no in-M5 minimum-load withholding
- [ ] Clean MSVC, GCC and Clang builds and a fresh full required test run at the handed-over commit
- [ ] No stale proof/acceptance flag, duplicate cargo or overused reservation; all required results have evidence

A checked specification is not a completed checklist. No box is marked complete by this revision.

---

## 10. Tom's answers and what is still open

Sources: the returned preview (internally Revision 3, 6 November 2026) and Tom's later replies listed at
the top of this file. His words are quoted where they decide a rule.

| Q | Question | Tom's answer | Status and how this plan applies it | Tests that depend on it |
|---|---|---|---|---|
| Q1 | Routing threshold | "20 is an arbitrary number ... the concern is ... time to create shipments" | ANSWERED: performance choice. Reserved per lane-date > T, T = 20 now, calibrated from measured lane times (CL-08) | U-RT, X-ROUTE, RD-01/05, U-PF-02 |
| Q2 | Time limit | "Each load"; later: careful method "has a maximum time limit of ?? <<user set>> seconds per load" | ANSWERED (user-set seconds per load). Contract stated to Tom: allowance = secondsPerLoad x reserved trucks (CL-05) | ACC-2, U-PF-01/02/05, X-BLK-07, X-RUN-04/11/12/13 |
| Q3 | Method A, B or both | "Your call"; on the review: "It's a good start" | ANSWERED: run both on the same inputs and keep the better checked result within the shared budget; patterns stay open to mixed rebalancing | U-RB-04/05, X-BLK-06, X-STATE-08, X-RUN-13 |
| Q4 | Last-truck minimum | First: a customer criterion for all shipments. Follow-up: "the test of whether a truck should be exported ... is after the algorithm. So you don't need to worry about [it]" | ANSWERED: no minimum-load rule inside M5; report underfilled trucks and why (CL-01, CL-03) | INV-14, U-RM-03/04/05, U-AC, X-REB-04, X-E2E-04 |
| Q5 | Three lanes, not five | Review recommendation adopted with Tom's notes: the three real lanes plus the 38-truck reference, known-optimum benchmarks and M4 regressions | ANSWERED | ACC-2, RD-06, RD-07, RD-08 |
| Q6 | FFD hand-over gap | "Computer time is cheap. Trucks are expensive." Stop when the quick method is at the minimum; otherwise run the careful method with a user-set time limit; at the limit use the best found | ANSWERED (CL-04) | ACC-3, U-FF-01..05, X-CG-17 |
| Q7 | Past-due order | "Start with date"; later: "user selectable by lane" | ANSWERED: AgeFirst run-wide is current. Per-lane override is a deferred capability, conditional on an agreed change and milestone; its unselected integration variants do not block current M5 sign-off | R-4c, R-7, X-DATE-08 |
| Q8 | 84% cube or 3,300 cu ft | "Tom disagrees": full is "the distance to the back of the trailer ... assuming everything is stacked as high as possible"; goals "98% of weight capacity or only waste x inches at the back" | ANSWERED: fullness by rear inches, 98% weight, or floor-limited (CL-03). Rear-inch default 14 in stated to Tom from his chart | CL-03, U-FL, U-RP-01/07, RD-10, RD-12 |
| Q9 | Floor-full trucks | Displays that cannot stack fill the floor at about 50 in; "that trailer would be full because no other items could be placed" | ANSWERED: floor-limited counts as FULL (CL-03 c) | U-FL-04, U-AC-05, X-E2E-09 |
| Goals | Order of goals | "Generally, you can get 2 out of 3 (shipping higher-priority items and using all available trucks)"; 98% depends on the products | ANSWERED: priority and use-all-trucks first, fill best effort (CL-02) | CL-02 tests, U-RM-05 |
| Can-go | Meaning of can-go | "Yes" (same as pull-forward: due within 10 days, available) | ANSWERED: can-go fill on M5 lane-dates after required goods (FX-CANGO) | U-CF-01..03, RD-02/04/13 |
| OPEN-COMP (14 Nov) | Tie between plans that ship the same goods by urgency class | "This is to be expected. Generally, when you have this, we're dealing with a shortage across the whole network." "Any tiebreaker would work as long as it didn't always push to the same single side" | ANSWERED: ED-21, a group order rotated by ship date, then canonical records (Section 0.8) | CL-02, X-RES-13, X-META-03/05/06 |
| C9 (14 Nov) | Reserved trucks with different trailers on one lane-date | "In this case, it's okay." He normally expects a truck-type list ("2 of A, 5 of B, and 1 of C") to give the mix | ANSWERED for M5: refuse, do not guess. A truck-type mix from an availability list is not in the current inputs; out of scope for M5 (Section 8) | U-DA-05, X-RES-12, X-E2E-06 |
| Tie detail (16 Nov) | How the tie rotation varies | Tom's engineer: "a deterministic rotation based on lane and date among otherwise equal-urgency plans" is acceptable; test replay, input-order independence and variation across lane-dates; never override urgency, eligibility, hard limits or reservations | ANSWERED: ED-21 rotates by lane key and day number (Section 0.8) | X-RES-13, X-META-03/06 |
| Test trailer (16 Nov) | Other trailer types | "I'd suggest trying a 40 ft high-cube container", 12,032 x 2,350 x 2,698 mm inside | Single-type run supported; payload and unit-load limit still needed | U-DA-07 |

Still open (none blocks development):
- Payload and unit-load limit of the 40 ft high-cube container (U-DA-07 containerRun). R-1a/R-1b/R-5a inputs
  arrived on 16 Nov (Section 40); do not ask Tom for them again.
- Per-lane past-due override in M4: deferred until Tom agrees it and a milestone (AgeFirst run-wide stays current); needed for the R-7 integration variants.
- No objection yet from Tom to the two stated settings (CL-05 formula, 14 in default); both apply unless he objects.
- October 1-2 fixture packages for Tom's reviewer (our copies are present; file names, sizes and SHA-256 are listed in m4Fix2Verification.txt A7; delivery to Tom pending).

---

## 11. More routing tests

| ID | P | Setup | Expected |
|---|---|---|---|
| X-ROUTE-01 | P0 | Thresholds T = 1, 20, 50, each with T-1, T and T+1 reserved | Only T+1 goes to M5. The threshold and "more than" rule are printed |
| X-ROUTE-02 | P0 | One lane-date, 21 reserved, two groups needing 11 and 10 trucks | Goes to M5. The shared cap stays 21, not 21 per group |
| X-ROUTE-03 | P0 | Lane X: day 1 = 21, day 2 = 20 reserved. Lane Y: day 1 = 20, day 2 = 21 | Only the 21 dates go to M5. Input order does not change this |
| X-ROUTE-04 | P0 | Two different lanes (different origin or destination) with the same products, 11 reserved each | Kept separate. Not added into a fake 22-truck lane |
| X-ROUTE-05 | P0 | The same reservation record twice; and two genuinely different reservations with equal values | The duplicate does not double the trucks. The two real ones both count |
| X-ROUTE-06 | P0 | Threshold or block size negative, 0, 1.5, missing, or too large | Rejected before planning starts. No endless loop on block size 0 |
| X-ROUTE-07 | P0 | FX-ROUTE: same reservations, scope and T; vary current goods and historical client built counts. Current goods stay nonempty, valid, in scope and date-eligible. Then request an unsupported demand-count mode | Routing follows configured reserved count, not historical builds or changed demand size. Unsupported demand-count config is rejected. This does not forbid valid no-work/input-validation exits and is not a permanently skipped demand-mode test |
| X-ROUTE-08 | P0 | One run with TF/TL goods, raw-material rows, and one parked-feature row | Raw rows excluded and counted separately; parked row stops the run; excluded rows do not count toward the threshold |
| X-ROUTE-09 | P1 | Forced M5 benchmark on a 20-reserved lane, then a normal run in the same process | Normal run still sends it to M4. No leftover setting, cache or reservation from the benchmark |
| X-ROUTE-10 | P0 | Very repetitive lane with 19 reserved, then 21 reserved | 19 stays in M4, 21 goes to M5. Being repetitive does not change the route by itself |

---

## 12. More cutting-stock and "proven best" tests

These run exact scalar models (default capacity 10, whole-number sizes). All demand is required;
no dispatch minimum, physical trailer, date or reservation constraint applies unless a case
explicitly adds it. X-CG-13 adds groups and capacities; X-CG-19 deliberately adds acceptance.
Physical tests are in Section 13. Exact solver expectations need an independent checker.

Words to keep apart: a valid result, a linear-relaxation number, a lower bound, and a proven best
whole-number answer are four different things.

### Small exact cases (all checked by brute force)

| Case | Items | Best | Why |
|---|---|---|---|
| FFD trap | 6, 5, 3, 2, 2, 2 | 2 | [6,2,2] and [5,3,2]. FFD gives 3: [6,3], [5,2,2], [2] |
| Weak volume bound | 6, 6, 6 | 3 | No two fit together. Volume bound says 2, which is too low |
| Fractional relaxation | five items of 3 | 2 | [3,3,3] and [3,3]. Relaxation gives 5/3 |
| Mixed patterns | 6, 6, 4, 4 | 2 | Two bins of [6,4] |
| Missing mixed pattern | 6, 4 | 1 | One bin [6,4]. With only columns (1,0) and (0,2) the covering relaxation says 1.5 |
| Exact multiple | fifteen items of 2 | 3 | Five per bin |
| With a remainder | seventeen items of 2 | 4 | Three full bins and one with two items |
| Converged but not best | three 5s and six 2s | 3 | [5,5], [5,2,2], [2,2,2,2]. Total 27 needs at least 3. Pure patterns only need 4 (X-CG-18) |

| ID | P | Setup | Expected |
|---|---|---|---|
| X-CG-01 | P0 | FFD trap case | FFD uses 3; M5 finds 2. All items kept. Gap = 1 truck |
| X-CG-02 | P0 | 6, 6, 6 | Answer 3. Never turn the volume bound of 2 into a 2-truck plan or a zero gap |
| X-CG-03 | P0 | Five items of 3 | Answer 2, shipping exactly 5 items, not 6. The relaxation alone is not the proof |
| X-CG-04 | P0 | Sizes 6 and 4, demand one of each. Start the LP with only columns (1,0) and (0,2). Formulation: minimise copies x1 + x2 subject to covering, x1 >= 1 and 2 x2 >= 1, copies may be fractional | Restricted LP = 1.5 (x1 = 1, x2 = 0.5). Must find the mixed pattern (1,1) and answer 1. Final shipping is exactly one item of each size. Do not use 1.5 rounded to 2 as a lower bound |
| X-CG-05 | P0 | Pricing check: sizes 6 and 4, dual values 3/5 and 11/20 | Best new pattern is (1,1): value 23/20, reduced cost -3/20. Sign and counts correct |
| X-CG-06 | P0 | Same as X-CG-05 but pricing stopped early (cap, timeout, heuristic miss) | Pricing status reported as "incomplete". Incomplete pricing cannot by itself prove the result best. A "proven" flag is allowed only if a separate valid bound for the same model equals a complete feasible result |
| X-CG-07 | P1 | Reduced costs very close to the tolerance on both sides | A clearly improving pattern is not thrown away. No endless add/remove loop |
| X-CG-08 | P1 | Pricing returns the same pattern again (also reordered) | No endless loop or ever-growing pattern list; stop reason recorded |
| X-CG-09 | P0 | One item of 11 (bigger than capacity 10) | Reported as impossible and counted. No illegal pattern, no empty rescue truck |
| X-CG-10 | P0 | No goods (also: reservations but no eligible goods) | Zero trucks. No divide-by-zero, no reservation used |
| X-CG-11 | P0 | FFD trap case, stopped before the better answer is found | Keeps the valid 3-bin result and labels it timed out. Not called best |
| X-CG-12 | P0 | Stopped before any result exists | "No result found", all goods kept. Not a zero-truck success, not "impossible" |
| X-CG-13 | P0 | 6 in group A and 4 in group B, no mixing. Then A = [6,6], B = [4], trailers of 12 and 8 | First: answer 2, even though volume says 1. Second: bound 2 (A on 12, B on 8). Bounds are worked out per group, then added |
| X-CG-14 | P0 | Result 2 with a claimed lower bound of 3; also a bound from different goods; also no bound | Rejected or shown as invalid. Never a negative gap clipped to 0 and called best. No bound shown as "not available" |
| X-CG-15 | P1 | Random small cases (10 items or fewer), fixed seeds, checked by brute force | Always matches the brute-force best. Failing seeds saved |
| X-CG-16 | P0 | A generated pattern with a negative or fractional count, an unknown item, or over capacity | Rejected before use |
| X-CG-17 | P0 | Feasible result 12, valid integer bound 10 for the same required goods/objective; FX-GAP | Gap 2 trucks. A named percentage formula must reproduce its denominator: 20% above bound or 16.666...% of result; use one recorded report contract. Percentage is diagnostic; current escalation uses F > B with allowance remaining. Zero/no-bound percent variants print N/A instead of dividing by zero or fabricating optimality |
| X-CG-18 | P0 | Capacity 10. Demand three items of 5 and six items of 2, all required. 1-D maths only: no kerf, trailer rules, dates or dispatch minimum. The LP starts with every legal single-product pattern, including part patterns | See the case notes below |

**X-CG-18 - completed LP pricing does not prove the restricted integer result is the best.**

Expected maths:

1. Full LP optimum is 2.7 (total size 27 / capacity 10).
2. A feasible LP solution: 1.5 copies of [5,5] and 1.2 copies of [2,2,2,2,2].
3. The best whole-number answer using only single-product patterns is 4 bins (2 for the 5s, 2 for the 2s).
4. The true best is 3 bins: [5,5], [5,2,2], [2,2,2,2]. Total size 27 means at least 3.

Why it matters: with dual values 0.5 for size 5 and 0.2 for size 2, every legal pattern has reduced
cost >= 0, so LP pricing can stop correctly without ever generating [5,2,2] (reduced cost 0.1).

Required:

- Under the declared known-optimum test budget, the final result uses 3 bins and accounts for all
  nine items exactly, checked by an independent checker.
- A 4-bin result over the generated single-product columns must not be labelled best just because
  LP pricing converged.
- LP status, whole-number result, valid lower bound and final proof status are reported separately.
- An interrupted run reports its real status and does not pass this acceptance case unless it
  produces the required checked 3-bin result.
- This specifies behaviour and how to read the certificate. It does not prescribe a solver design.

---

### X-CG-19 A capacity bound does not certify an accepted all-goods packing

Priority: P0. Execution status: NOT_RUN. Evidence scope: SYNTHETIC.

Scope: An exact scalar acceptance-adapter fixture. This deliberately enables a minimum-acceptance rule; it is separate from the ordinary capacity-only solver cases.

Setup:

- Cube capacity 10; items with cubes [6, 2, 2, 2], all required for this all-goods feasibility question.
- Give each item weight 1, physical weight limit 10 and acceptance x = 10, so no truck can pass on weight.
- Synthetic cube acceptance y = 6, strict >.
- Enough reservations; no dates, groups, kerf or physical-layout restrictions in this mathematical fixture.

Expected:

- Ordinary capacity-only optimum is two bins. For example [6,2] and [2,2] fits capacity, but the second bin is not accepted.
- No all-goods packing exists in which every nonempty bin passes acceptance. Total cube is 12; one bin cannot hold it, and two accepted bins would require more than 12. More bins require still more total cube.
- The exact all-goods feasibility result is therefore infeasible under the stated acceptance policy, not timeout and not a physically oversized item.
- A valid capacity lower bound may still be reported as a relaxation bound. It must not be presented as a feasible accepted packing or an optimum certificate for the accepted all-goods model.
- A separate partial-service variant, with objective maximize shipped cube, may dispatch [6,2,2] and leave one 2. Label that as partial service rather than a complete all-goods solution.

---

## 13. More physical pattern tests

| ID | P | Setup | Expected |
|---|---|---|---|
| X-PAT-01 | P0 | Pattern holds 5. Goods 0, 4, 5, 15, 17 | Copies 0, 0, 1, 3, 3; left over 0, 4, 0, 0, 2. No extra truck at exact multiples |
| X-PAT-02 | P0 | Mixed pattern uses 2 of A and 1 of B; stock A = 5, B = 4 | 2 copies; uses A = 4, B = 2; left A = 1, B = 2 |
| X-PAT-03 | P0 | Empty pattern, all-zero pattern, pattern needing a product with no stock | Rejected or zero copies. No divide-by-zero, nothing half-used |
| X-PAT-04 | P1 | Arithmetic only: 2^31 units, pattern of 64 | 33,554,432 copies, or a clear range error. No overflow |
| X-PAT-05 | P0 | Same product: 30 units eligible now, 70 due 11 days later, pull limit 10, pattern size 10 | At most 3 copies. Each copy keeps its source lines, dates and priority |
| X-PAT-06 | P0 | PTL layers 49x39 and 47x41 vs TLD | PTL footprints 49x40 and 48x41; TLD uses the exact layer. Product id alone does not identify a saved pattern |
| X-PAT-07 | P0 | Content height 51 in per unit load, excluding any deck. Trailer 108 in | Two TLD (no deck) = 102 in fits. Two PTL, each with its own 6 in deck = 2 x (51 + 6) = 114 in does not |
| X-PAT-08 | P0 | A pattern that needs 625 in of length, reused on a 600 in trailer; a 42,000 lb pattern reused against a 40,000 lb limit | Both rejected or rebuilt. A smaller trailer is not a reason to reject a pattern that still genuinely fits |
| X-PAT-09 | P0 | Master V1, then V2 with taller or heavier product, same id | Old pattern re-checked or dropped. V2 never ships a V1-only load |
| X-PAT-10 | P0 | One pattern copied to 3 trucks; repair only the middle one | Other two unchanged. Removed goods go back once |
| X-PAT-11 | P0 | Trailer 100x100x100; two non-stackable loads 60x60x100 | Only one fits on the floor, even though cube is 72%. Cube alone never decides |
| X-PAT-12 | P0 | Trailer 100x60x100; load 60x100x100, turning allowed | Fits when turned. The turn is recorded and kept when copied |
| X-PAT-13 | P0 | Each dimension and weight exactly at the limit, one supported input step under, one step over (stacked height too). Allowed turns only; all other dimensions feasible | Equal and under fit; over does not. Decided on full-precision values with the documented tolerance, never on rounded report text |
| X-PAT-14 | P0 | Limits W = 10, V = 10. Two items each W = 6, V = 1. Then swap W and V | Never on one truck in either case. Planning by one limit never turns the other off |
| X-PAT-15 | P0 | Three 36 in layers in 108 in: one variant stackable, one marked not stackable, one limited to two high | Only the stackable one goes three high. Copies and remainders keep the flags |

---

## 14. More remainder and rebalancing tests

| ID | P | Setup | Expected |
|---|---|---|---|
| X-REB-01 | P0 | Capacity 10: two A of 6, two B of 4, same date and priority | Two full bins [6,4]. Single-product patterns first would give 3 - must not lock that in |
| X-REB-02 | P0 | Same, but A and B in different groups; then B not available yet | Mixing not allowed. Ends with the real fill and the left-over goods. No loop |
| X-REB-03 | P0 | Copies use up all goods; then a remainder that is all ineligible by date | No extra truck, no reservation used. Ineligible goods reported, not called shipped |
| X-REB-04 | P1 | Scalar acceptance adapter: three items each cube 8/weight 1; physical capacities V = 10/W = 10; synthetic x = 10/y = 8.4, strict OR; three reservations; otherwise eligible | No truck passes: at most one item fits, cube 8 <= 8.4 and weight 1 <= 10. Zero dispatched, all goods accounted for with acceptance reason; stops cleanly |
| X-REB-05 | P0 | Floor repair removes goods from a proven truck; another choice now becomes better | Old proof cleared, goods back once, choice made again and checked |
| X-REB-06 | P0 | Rebalancing makes a mixed-date stack with pull-forward off | Each unit's date checked again. The later one does not ship early |
| X-REB-07 | P0 | Scalar capacity V = 10/W = 10. P0 item cube 8/weight 1; P11 item cube 9/weight 1; same group and due/available D, one reservation, priority-first. Synthetic x = 10; variant A y = 7.9, variant B y = 8 | A: both accepted, P0 wins; do not replace it just for fill. B: P0-only truck fails equality and P11-only truck is the accepted feasible alternative. Items cannot fit together. Priority never authorizes an unaccepted shipment |
| X-REB-08 | P0 | A (V = 6, W = 4) + B (V = 4, W = 7) in a truck of V = 10, W = 10 | Rejected: weight 11. Rejected move fully undone |

---

### X-REB-09 Rebalance both sides of a strict acceptance boundary

Priority: P0. Execution status: NOT_RUN. Evidence scope: SYNTHETIC.

Setup:

- Fifty FX-30 units, same group, lane, due date D and priority P0; all available on D.
- Two default-trailer reservations on D. No other goods or dates; carry and pull-forward off.
- Synthetic x = 40,000 lb, y = 3,000 cu ft, strict OR.
- Exact small-case objective: maximize shipped units of this identical-priority demand, subject to all constraints.

Expected:

- Pattern-first 30 + 20 cannot dispatch both trucks: the 20-unit truck has only 30,000 lb and 2,500 cu ft.
- The complete accepted allocation is 25 + 25. Each truck has 37,500 lb and 3,125 cu ft.
- The proposed split 26 + 24 is not an accepted all-goods plan. The 24-unit truck has 36,000 lb and exactly 3,000 cu ft, and fails.
- Check both donor and recipient after every transfer. Exactly two reservations are committed and all 50 source units ship once.

Boundary variant under the same ID:

- Use 49 units with everything else unchanged.
- The best accepted shipment is one 30-unit truck; 19 units remain on D and one reservation is unused.
- Proof: an accepted FX-30 truck needs at least 25 units under these thresholds, and physical capacity is 30. Two accepted trucks need at least 50 units. Rebalancing must not invent the missing fiftieth unit or dispatch 24 at equality.

---

## 15. More rolling-block tests (method B)

FX-BLOCK: each whole-truck item is an indivisible non-stackable TLD with final footprint
630 x 100 in, height 108 in and gross weight 45,000 lb; no extra deck/tare. It fills one default
trailer and passes synthetic x = 40,000 lb/y = 3,000 cu ft. Unless stated, one group/lane,
all available and due D, priority P0, no carry/pull, and reservations as stated. These are synthetic
block-accounting items, not actual customer pallets. X-BLK-06 declares a different 10-unit fixture.
Cases that compare a deliberately invalid candidate do not treat it as a dispatchable FX-BLOCK.

| ID | P | Setup | Expected |
|---|---|---|---|
| X-BLK-01 | P0 | Block 19, goods for 0, 1, 18, 19, 20, 37, 38, 39 trucks | Blocks [], [1], [18], [19], [19,1], [19,18], [19,19], [19,19,1]. No empty block |
| X-BLK-02 | P0 | 40 trucks of goods, 38 reserved | 19 + 19; 2 left over. No third block gets fresh reservations. Priority decides which 2 |
| X-BLK-03 | P0 | 30 trucks of goods, 38 reserved | 19 + 11; 8 unused reported. No empty trucks to fill a block |
| X-BLK-04 | P0 | Several groups share 21 reserved; first block uses 19 | Only 2 left for everything else. Each group does not get its own 21 |
| X-BLK-05 | P0 | A uses fewer trucks but one fails the floor check; B is valid. Then both invalid | A cannot win. Both invalid = no plan, all goods accounted for |
| X-BLK-06 | P0 | Thirty same-group P0 units due/available D, three reservations. Local TLD fixture: 63 x 100 x 108 in, 4,500 lb final, not stackable; ten fill a default trailer. Synthetic x = 40,000/y = 3,000. Controlled A ships 30 in three trucks; B ships 20 in two and leaves 10 | Both physical candidates pass acceptance. Under FX-PRIORITY, A wins by shipping 30 P0 versus 20; B is not better simply because 2 < 3. Each method accounts for the same original 30 units |
| X-BLK-07 | P0 | FX-CLOCK fixed allowance 100 time units; block 1 ends at 60, repair at 85 | Only 15 remain for all later work and final checks. No block/method/repair resets or expands the allowance. Synthetic work units test accounting; production uses the stated CL-05 secondsPerLoad x frozen reserved-count formula |
| X-BLK-08 | P0 | Block 1 passes all final candidate checks. Block 2 mutates provisional goods then fails. Exercise FX-COMMIT keep-validated and whole-lane-rollback contracts at the test seam | Always undo block 2. Keep variant returns only block 1 as a labeled partial candidate; rollback variant returns no candidate. Validation alone never consumes real inventory. Only a selected checked result may operationally commit. Before commit retry from original; after actual partial commit use reconciled residual/idempotency. Production mode is recorded under CL-10; unchosen alternative is not silently skipped as a required pass |

---

## 16. Reservations and priority across groups

All units are FX-30 units with the FX-30 defaults (Section 0.5), unless the test states otherwise.
Where a test says "priority first", that is a synthetic setting for the test.

| ID | P | Setup | Expected |
|---|---|---|---|
| X-RES-01 | P0 | Two reserved. Group A 90 units, group B 60, equal priority | Groups share the two. 2 trucks, 60 units shipped, 90 left |
| X-RES-02 | P0 | Three reserved. 60 of X (two patterns) + 15 Y + 15 Z (remainder) | 3 trucks, 90 units. The remainder does not get a 4th reservation |
| X-RES-03 | P0 | Lane: 2 reserved on D, none on D+1, 3 on D+2. More than 2 trucks of goods on D | At most 2 leave on D, none on D+1. A lane total of 5 does not allow 5 on D |
| X-RES-04 | P0 | Several client labels for the same lane-date; another lane with the same products | No double reservations. The other lane's trucks never used |
| X-RES-05 | P0 | Reserved count -1, 1.5, NaN, infinity, huge; and a real 0 | Bad ones rejected with lane and date named. 0 means no trucks |
| X-RES-06 | P0 | Two reserved. A: 1 P0 + 29 P11. B: 20 P0 + 10 P11. C: 30 P0. Priority first | C and B ship; A left |
| X-RES-07 | P0 | Two reserved. 90 P11 of X, 15 P0 of Y and 15 P0 of Z; one shared group, same date, priority-first; FX-30 physical/acceptance settings and FX-PRIORITY | Ship all 30 P0 and 30 P11 in two accepted trucks; leave 60 P11. Y+Z in one truck and X in the other is a valid witness, not a required decomposition. Equally ranked valid mixes may pass unless the recorded canonical tie rule requires a particular assignment. Building patterns first never overrides priority |
| X-RES-08 | P0 | One reserved, priority first. Case 1: 30 valid P0 not available until D+1; 30 P11 available on D. Case 2: the 30 P0 are oversized (cannot fit any trailer) | Case 1: P11 ships; P0 stays left over and is not lost. Case 2: P0 classified as not shippable and reported with a reason, using the established failure scope. P11 ships only if that scope allows partial service; if the scope is a whole-lane or whole-run failure, that failure is reported instead |
| X-RES-09 | P0 | One reserved. A's first plan claims 30 P0, repair leaves 10 P0 + 20 P11. B: 20 P0 + 10 P11 | B wins. Decision uses A's final contents, not its old plan. A's removed goods return once |
| X-RES-10 | P1 | Two lane-dates leave 2 and 1 unused. Test rates 5 and 7; then second rate missing; then rate 0 | 2 x 5 + 1 x 7 = 17. Missing rate shown as unknown. Zero shown as zero |
| X-RES-11 | P0 | Chosen plan claims 2 of 3 reservations, then fails a final check | Claims released once. Only a valid alternative is committed; else "no plan" |
| X-RES-12 | P0 | Two reserved trailers, default dimensions, weight limits 45,000 and 30,000 lb; 50 FX-30 units same group/date/P0, no carry/pull. Variant A synthetic x = 40,000/y = 2,400; variant B x = 40,000/y = 3,000 | A: ship 30 on big and 20 on small, all 50. B: only big truck ships 30; small can hold at most 20 = 30,000 lb/2,500 cu ft and cannot pass either branch, so 20 left and one unused reservation. Never copy 30 onto small. Test physical adapter directly; an unsupported external input feature is explicit in the manifest, not a skipped P0 pass |

---

## 17. Dates (M4 rules used by M5)

These check that M5 follows M4's date rules. They do not add new date planning (that is M6).
Units are FX-30 units with the FX-30 defaults (Section 0.5). Each test states the reservations,
due dates, availability and pull/carry settings it depends on; where it names an order policy,
that is a synthetic setting.

| ID | P | Setup | Expected |
|---|---|---|---|
| X-DATE-01 | P0 | Pull on, 10 days. 10 units each due D, D+9, D+10, D+11; one truck | D, D+9 and D+10 fill the truck. D+11 stays. Checked per unit |
| X-DATE-02 | P0 | 30 due D; 30 due D+5 but available D+1. Pull on, 2 trucks on D | Only the first 30 ship on D |
| X-DATE-03 | P0 | Same product: 30 due D, 30 due D+1. Pull off, 2 trucks on D | Only one copy. The D+1 goods do not make a second copy |
| X-DATE-04 | P0 | 60 due D. One truck on D, next one on D+4. Carry on | 30 ship D; 30 move to D+4. Original due date and priority kept |
| X-DATE-05 | P0 | Last reserved date, 60 units, one truck, carry on | 30 ship; 30 left over. No made-up later date, no loop |
| X-DATE-06 | P0 | Component-date validation after repair/remainder mutation, not full-truck dispatch: two local TLD units 20 x 100 x 50 in, 1,500 lb each, stackable to two high, same group; due D and D+1, both available D; default trailer, one reservation. Test pull off and pull on with limit 10 | Combined height 100 in/weight 3,000 lb fits physically. Recheck every component date: with pull off the mixed stack is ineligible on D and the D+1 unit cannot be hidden by the D component; with pull on both are date-eligible. Removed/unshipped units reconcile once. No customer-minimum dispatch assertion is made by this validation-seam test |
| X-DATE-07 | P0 | Two dates, carry and pull both on | Each unit used once. Carry and pull never both hold the same goods. Shipped + left = original |
| X-DATE-08 | P0 | Carry 30 overdue P11 from D to D+4; 30 new P0 due D+4; one truck. Run age-first and priority-first | Age-first ships the P11; priority-first ships the P0. The carried goods keep their real age |
| X-DATE-09 | P0 | FX-30 goods due D+17 and available D; pull on with limit 10; separately check departure D and D+7 from fresh snapshots, one reservation per checked date, in either evaluation order | Component eligibility is false on D and true on D+7. Assert date eligibility, not a dispatch quantity. Cache keys include departure date/settings, not just product; evaluating one date does not consume the other snapshot |
| X-DATE-10 | P1 | Date maths across month, year and leap day | D = 2026-12-25 gives D+10 = 2027-01-04. D = 2028-02-20 gives 2028-03-01. Same helper as M4 |
| X-DATE-11 | P0 | Two lanes, same product and origin, different destinations | Goods never use the other lane's reservation |
| X-DATE-12 | P0 | M4's planning period plus data beyond it | M5 does not add days, widen the pull window, or create new departure dates |

---

## 18. Hand-over data and units

Where a test uses physical units, they are FX-30 units unless stated (X-DATA-06 uses FX-30 as V1).

| ID | P | Setup | Expected |
|---|---|---|---|
| X-DATA-01 | P0 | M4 hands over 60 unit loads for a product with `Cases_Unit_Load` = 10 | M5 works with 60. Not divided again on the way to M4's packers |
| X-DATA-02 | P0 | Input X = 10, Y = 20; a faulty step returns X = 20, Y = 10 (same total) | Caught by the per-product check, even though the total matches |
| X-DATA-03 | P0 | `Cases_Unit_Load` 0, negative, missing, NaN, infinity | Never used in a division. Product and line reported |
| X-DATA-04 | P0 | 9 cases, `Cases_Unit_Load` = 2, no other variant | Follows M4's rule for part pallets. Never silently 4 or 5 |
| X-DATA-05 | P0 | Two variants both divide 12 (by 2 and by 3). Master rows reversed | M5 keeps the variant M4 chose. Row order does not change it |
| X-DATA-06 | P0 | Saved pattern from V1 (1,500 lb per unit); V2 = 1,600 lb | Old 30-unit pattern dropped. Max is 28 (28 x 1,600 = 44,800; 29 = 46,400) |
| X-DATA-07 | P0 | Same product on lines with different priorities, dates and groups | The pattern shape can be shared; the goods stay tied to their own lines |
| X-DATA-08 | P0 | 240 CS / 10, 432 DIS / 18, 60 PAL / 2 | 24, 24 and 30 unit loads; 78 total, through both methods |
| X-DATA-09 | P0 | Fixture declares 001234 and 1234 as two distinct resolved product identities (as M4 and the master hold them); group labels with underscores; rows shuffled | M5 keeps the identities M4 and the master established: two products. No new normalisation added by M5. Labels not read as routes |
| X-DATA-10 | P0 | Demand and reservations from different runs, or a different master than the expected results | Rejected or reported as a mismatch. Never called a pass |
| X-DATA-11 | P0 | A source line repeated with the same values; then with different values | Not double-counted. Conflicts reported |
| X-DATA-12 | P1 | Totals of about 3,000,000,000 units (counts only) | Exact, or a clear range error. No overflow |
| X-DATA-13 | P0 | Size, weight or capacity 0, negative, NaN, infinity, or overflowing; plus a deck height of 0 for a TLD | Invalid physical capacities and dimensions rejected before any cube or pattern maths. Zero is judged by field meaning: a zero-size trailer is rejected, an explicitly absent deck (0) is valid |
| X-DATA-14 | P0 | Quantity negative, NaN, 0, 1.5 for a whole unit, or too large | Rejected or handled by the written rule. No silent rounding or absolute value |

---

## 19. Method A vs B: keeping them separate

Units are FX-30 units with the FX-30 defaults (Section 0.5). Reservations and policies are stated per test.

| ID | P | Setup | Expected |
|---|---|---|---|
| X-STATE-01 | P0 | Run A then B, B then A, and each alone, same input, run to completion | Each method gives the same result every way. Nothing leaks between them |
| X-STATE-02 | P0 | A and B each ship 60 units in 2 trucks from 3 reserved; test rate 5 | Committed: 60 units, 2 trucks, 1 unused, penalty 5. Not 120 units or 4 trucks |
| X-STATE-03 | P0 | Freeze an M4 lane, then run an unrelated M5 lane with the same products | Frozen lane unchanged |
| X-STATE-04 | P0 | Same process: pull on then off; age-first then priority-first. Compare with fresh runs | Same results as fresh runs. Old settings do not stick |
| X-STATE-05 | P0 | Run M5 twice from the same hand-over | Same result. The first run does not use up the hand-over |
| X-STATE-06 | P0 | Three copies of one pattern from 90 traceable units; repair one copy | The other two unchanged. No two trucks share the same units |
| X-STATE-07 | P0 | Two groups compete for the last reservation at the same time | Exactly one wins, by priority or the fixed tie rule. Never both |
| X-STATE-08 | P0 | A ships 30 P0, B ships 30 P1; same trucks, fill, dates and everything else | Not a tie: A strictly dominates and wins. For mixed compositions that do not strictly dominate, the result comes from the approved comparison (CL-02), not from this test |

---

## 20. Report checks

| ID | P | Setup | Expected |
|---|---|---|---|
| X-REPORT-01 | P0 | Aug5 production report | M5 on 7 Aug compares with 18, not 38. Never prints 38 as reserved (reserved is 21 + 20 = 41) |
| X-REPORT-02 | P0 | Both-dates benchmark and production in one run | Benchmark clearly labelled and kept apart from production. 23 and 38 are examples, not targets |
| X-REPORT-03 | P0 | Report after M4 fixes or master/config changes | Label 635 and old means as historical. Current preview reports 747 stacks/763 units and means 3,171/3,209/3,192; assert only after reproduction with the same evidence. A different current baseline is labeled and explained, never silently substituted. M4 estimate 22 is not called proven infeasible in 21 without an applicable certificate |
| X-REPORT-04 | P0 | Synthetic x = 40,000 lb/y = 3,300 cu ft; every candidate weighs 20,000 lb. Cubes 3,299.9, 3,300.0, 3,300.1, 3,307.5; all otherwise valid | Only 3,300.1 and 3,307.5 pass on cube; weight cannot change the result. Decide from full precision even if printed cubes round together. Report numeric x/y, strict OR and profile; 84%/3,307.5 is not substituted as the rule |
| X-REPORT-05 | P1 | Lane A one truck of 2,000; lane B trucks of 3,200 and 3,400 | Average per truck 2,866.7, median 3,200. Not the average of lane averages (2,650) |
| X-REPORT-06 | P1 | No M5 lanes; completed M5 run with zero accepted trucks; timeout before any checked result | Report no M5 work, completed zero dispatch with reasons, and no result/timeout distinctly. Empty averages and seconds per accepted load are N/A; no divide-by-zero or fictitious success (X-RUN-12) |
| X-REPORT-07 | P0 | 90 units: 60 shipped, 30 carried | 90 = 60 + 30. The 30 not counted twice |
| X-REPORT-08 | P0 | A finishes; B not started, timed out, or failed; after an earlier full run | B shown with its real status. No old B numbers reused, no "(run)" left in a final report |
| X-REPORT-09 | P1 | Ids with non-ASCII, quotes, commas, newlines, very long text | Report stays ASCII and rows stay intact |

---

## 21. Stops, failures and evidence

Whole-truck quantities use FX-BLOCK unless a case provides other units. Clock tests inject
controlled clocks/stops, not machine-speed assumptions. Record all states under Section 26.

| ID | P | Setup | Expected |
|---|---|---|---|
| X-RUN-01 | P1 | Same input on 1, 2 and 8 threads, stopped by work count | Same plan. Clock-stopped runs: valid and honestly labelled (CL-09) |
| X-RUN-02 | P0 | Time runs out before any result (fake clock) | "No result found", all goods kept. Not "impossible", not a zero-truck success |
| X-RUN-03 | P0 | Time runs out during provisional repair after a fully checked candidate exists; explicitly use FX-COMMIT keep-validated at this seam | Return only the previously checked valid candidate, with timeout/unproven status as applicable; never half-repaired contents. It remains isolated until selected and permitted to commit. Source goods/reservations reconcile; this case does not select the whole-lane-rollback variant |
| X-RUN-04 | P0 | FX-CLOCK: predeclared synthetic lane allowance 100 time units; A consumes 85; B would need 20; any existing result is fully checked | B can use at most the remaining 15 or is not started, according to the declared method scheduler. No fresh deadline. Report real statuses and only a checked result. Print total time and selected accepted count; average is N/A for zero. No allowance is derived from changing candidate/final truck count |
| X-RUN-05 | P0 | 21 reserved. Group A needs 20 trucks of P11, group B 2 trucks of P0. Method B, priority first, A listed first | Both P0 trucks ship; at most 19 P11. The first block does not lock out urgent goods |
| X-RUN-06 | P1 | Method B with 100 reserved and no goods; or only impossible goods | Stops quickly with a reason. No empty trucks, no loop until timeout |
| X-RUN-07 | P0 | Failure after provisional reservation claims but before any operational commit, then retry. Second variant retains validated earlier candidate blocks; third actually commits a selected partial result under FX-COMMIT | Before commit/after rollback: release provisional claims once, retry from full original. Retained candidate blocks remain isolated until selected and committed. After actual partial commit: retry from reconciled residual or idempotently suppress committed units; never start a fresh shipment of the full original. No stale acceptance/proof or double reservation survives |
| X-RUN-08 | P0 | Parked feature found after other goods already processed | Run stops. Earlier trucks not published as a complete run |
| X-RUN-09 | P0 | Acceptance evidence gate: missing/mismatched fixtures, failed build, old results, duplicate named IDs, duplicate testId+variant keys, unimplemented/no-assertion registration, missing required variant, stale specification/profile hash | Reject an incomplete evidence bundle even if a process exits zero. One current result per required unique ID+variant with real assertions, correct requirement/config/input/build identities, and fixture/oracle evidence. Missing prerequisites are BLOCKED, not PASS; duplicate keys never overwrite a prior result silently. Controls: an explicitly unselected deferred conditional R-7 integration variant remains visible BLOCKED with its reason and cannot by itself fail the current gate or count as PASS; selecting it after an agreed scope change makes its missing dependency/current evidence block the gate. Required current variants cannot be relabeled deferred to hide a failure |
| X-RUN-10 | P1 | Time M4, A and B on the same input and machine, in different orders | Times comparable, each with its goods shipped and left. No speed claim from different workloads |

---

### X-RUN-11 Distinguish per-load maximum from an amortised average

Priority: P0. Execution status: NOT_RUN. Evidence scope: SYNTHETIC. Contract: CL-05 lane-date allowance (secondsPerLoad x reserved trucks). Metric arithmetic is executable now.

Setup:

- Use a fake clock and a controlled sequential workload with ten final accepted loads.
- Recorded load times are 11 seconds for one load and one second for each of the other nine.
- Record any shared work separately according to the chosen accounting contract; for this distinguishing fixture, shared extra time is zero.

Expected:

- Total time = 20 seconds; accepted load count = 10; average = 2 seconds per accepted load; maximum observed load time = 11 seconds.
- These figures are not interchangeable. The average does not prove that every load met a ten-second maximum.
- The maximum-per-load <= 10 predicate is false; the average-per-load <= 10 predicate is true. Assert both metric calculations in this synthetic check.
- Under CL-05 the lane is judged on total time within its declared allowance (here 10 s/load x 10 reserved = 100 s, so 20 s passes); the 11 s maximum is still reported. The verdict names CL-05.

This is a contract-distinguishing fixture; it does not require supporting two production timing modes if only one is selected.

### X-RUN-12 Zero dispatch is distinct from failure to find a result

Priority: P0. Execution status: NOT_RUN. Evidence scope: SYNTHETIC.

Setup variants, each with a fake clock and the same declared budget mechanism:

1. No goods and a completed planning run.
2. Twenty eligible FX-30 units, one reservation, standard synthetic x = 40,000 lb/y = 3,000 cu ft, no carry or pull; the completed search establishes that no nonempty accepted truck can be built.
3. Eligible goods for an accepted load, but timeout is injected before any checked result exists.

Expected:

- Variant 1 is a completed empty result, using no reservation.
- Variant 2 is a completed zero-dispatch result under the policy; all 20 units remain accounted for, with the acceptance reason. It is not a physical oversized-item failure or an unknown search outcome.
- Variant 3 is timed out with no result. It cannot be reported as a successful empty plan or proven infeasibility.
- In all variants, zero accepted loads gives N/A for seconds per accepted load, never division by zero, infinity, NaN or an invented zero-time performance success.
- Report total elapsed time and the applicable status. The timing contract determines any performance verdict; the test itself passes by verifying the intended state and accounting.

### X-RUN-13 Freeze the budget and count only the chosen result

Priority: P0. Execution status: NOT_RUN. Evidence scope: SYNTHETIC.

Setup:

- Sixty FX-30 units, one group/date/priority, three reservations, synthetic x = 40,000 lb/y = 2,000 cu ft. No carry or pull.
- Controlled method A returns three accepted trucks of 20 units each. Method B returns two accepted trucks of 30 units each. Both ship the same 60 units with valid layouts.
- Use an explicitly synthetic predeclared lane allowance of 20 seconds; this value tests accounting and is not a customer default or a derivation from final output.
- Fake-clock work: A uses 8 seconds; B uses 7; repair/reselection accounting uses 3; final validation uses 2. Total is 20.
- For this accounting fixture only, use the explicit synthetic comparator: equal full service and equal priority are ranked by fewer trucks, with reservation penalty excluded from the comparison. This selects B. It does not resolve the production CL-02 comparator.

Expected:

- The deadline remains the original 20 seconds; starting B, considering more candidates or changing the selected truck count does not enlarge it.
- Only B is committed: two trucks and 60 units. The three A candidates are not added to production totals or the denominator.
- If amortised time is being reported, it is 20 / 2 = 10 seconds per accepted load, not 20 / 5 = 4.
- Repairs and final validation count in the same declared work interval.
- A variant that exceeds the allowance must preserve the same frozen deadline and show the real stopping status and only a checked result permitted by the commit contract; it cannot be labeled within-budget merely by dividing by more candidate trucks.
- No acceptance claim depends on using the synthetic deadline as the eventual production policy.

---

## 22. Generated tests

Scope of these tests:

- Capacity-only X-META-01/02/07/08 require all items, with dispatch minimums disabled. A case
  adding acceptance/date/group rules states them. Removing an item can destroy minimum-load
  feasibility; scaling sizes/capacity without scaling an active acceptance threshold changes the model.
- Exact best-answer comparisons and "never more trucks" checks apply to small models solved by an
  independent brute-force checker, with the same demand and objective.
- Ordinary time-limited heuristics are not guaranteed to return a monotone result after every input change.
- Equal best answers may use different pattern mixes. Exact pattern-count or assignment equality is
  checked only where a written canonical tie rule exists; otherwise compare truck count, feasibility
  and goods shipped.
- Every failing case keeps its seed and a small reproducing input.

| ID | P | Setup | Expected |
|---|---|---|---|
| X-META-01 | P0 | Every unordered multiset with repetition of 0 to 7 item occurrences, each size an integer from 1 to 6, capacity 6. That is 1,716 distinct multisets (empty demand included) | Matches an independent brute-force checker on every multiset: correct best bin count and a full feasible packing. A smaller subset can run in CI; the full set before hand-over. The 1,716 are instances of one test, not separate tests |
| X-META-02 | P0 | Small exact case: multiply all sizes and capacity by 10, then 100 | Same best bin count. Same pattern counts only where a canonical tie rule exists |
| X-META-03 | P1 | Shuffle input rows | Same goods shipped and same truck count |
| X-META-04 | P0 | Split one line into two with the same total; merge back | Same answer |
| X-META-05 | P0 | Rename products/groups/source lines without changing physical or priority data; include a tied-allocation case and a unique optimum | Preserve feasibility, truck count and the approved priority score/objective tuple. Exact mapped assignment is required only for a unique optimum or renaming-invariant tie rule. An approved ID-based tie-break may legitimately select a different equally scored composition after renaming |
| X-META-06 | P0 | Copy a lane into a second, fully separate lane | Each lane is planned on its own. Shipped unit loads per urgency class are identical in both lanes and totals double. Where groups tie, each lane follows ED-21 with its own lane key, so the winning group may differ. Nothing shared |
| X-META-07 | P0 | Small exact case: raise capacity; or remove an item | Best answer never needs more trucks |
| X-META-08 | P0 | Small exact case: add a do-not-mix rule; then remove it | With the rule, the exact best is never fewer trucks than without. Not asserted for time-limited heuristic runs |
| X-META-09 | P0 | Small scarce-truck case solved exactly: add one reservation, nothing else changed | Exact best result never worse. Extra reservation need not be used. Not asserted for time-limited heuristic runs |
| X-META-10 | P1 | Random valid and invalid physical inputs, fixed seeds | Every failure saved as a small input that reproduces it |

---

## 23. End-to-end scenarios

Each combines several modules. All applicable checks must pass under the stated evidence scope.
Small cases enter M5 directly as in Section 0.4; whole-truck atoms use FX-BLOCK unless specified.
A synthetic pass is recorded separately from any pending customer acceptance dependency.

**X-E2E-01 Both dates, production kept separate (P0).** Freeze the Aug5 input after the M4 fixes
(7 Aug = 21 reserved, 8 Aug = 20). Run production: only 7 Aug goes to M5. Then force A and B on both
dates from fresh copies. Check every truck, per-date reservations, groups and dates. Production
compares with 18, the labelled both-dates run with 38. The benchmark does not change production.

**X-E2E-02 Priority after repair (P0).** One reservation; R-2 groups. First, both groups valid: B wins.
Then make B's first layout invalid so repair removes goods; work out the new winner by brute force.
Check: decided again after repair, one truck at most, removed goods back once, no old proof kept.

**X-E2E-03 Patterns with pull-forward and unavailable stock (P0).** Local non-stackable TLD units
63 x 100 in, 108 in final height, 4,500 lb final gross: ten fill one default trailer. Synthetic
x = 40,000 lb/y = 3,000 cu ft, strict OR; one product/group, priority P0, eight reservations on D,
no carry. Lines A = 30 due D, B = 20 due D+10, C = 20 due D+11 are available D; E = 10 due D is
available only D+1. Run the small exact fixture to completion from fresh snapshots with default
date-first; its explicit synthetic objective maximizes eligible service, then minimizes trucks. Pull off: exactly A ships as three full trucks/30 units.
Pull on with limit 10: exactly A+B ship as five full trucks/50 units. C and E never ship on D.
Each shipped truck has 45,000 lb and 3,937.5 cu ft, passes acceptance and has a checked layout.
Every source line reconciles; an empty or partial result does not pass this exact fixture.

**X-E2E-04 Acceptance rule across groups (P0).** Two groups, one unit each (378 x 100 in, 108 in,
10,000 lb, not stackable), same ship date, two reserved. Each unit is 2,362.5 cu ft (60% of trailer
cube). Synthetic acceptance x = 40,000 lb: with y = 2,000 cu ft both trucks are accepted; with
y = 2,800 cu ft neither is. The rule is applied to each truck on its own, regardless of group or of
being the last truck (CL-03). A penalty never allows mixing or shipping a truck that fails the rule.

**X-E2E-05 Blocks stopped during the last repair (P0).** 39 FX-BLOCK atoms, block 19, 39 reserved,
explicit synthetic allowance. Blocks 1 and 2 pass physical, acceptance, date/group/source and
reservation checks; stop during block 3's provisional repair. FX-COMMIT keep variant returns a
checked partial candidate of 38 trucks with one item left; rollback returns no candidate.
Never a 39-truck success. Undo block 3 and retain the same deadline. Candidates remain isolated
until selection/operational commit. Before commit retry from original; after an actual partial
commit retry from reconciled residual or use idempotency. Never dispatch committed items twice.
The two seam variants do not require both production modes; record the chosen CL-10 contract.

**X-E2E-06 Different trailers and an old pattern (P0).** A big and a small trailer; a pattern that
only fits the big one, saved under master V1. Run with V2 (changed size or weight). The small trailer
never gets the old pattern; V2 never gets a V1-only load; the report names the master and trailers.

**X-E2E-07 Choosing across groups (P0).** Two reserved, three groups (Section 16 unit). A: 1 P0 + 29 P11;
B: 20 P0 + 10 P11; C: 19 P0 + 11 P11. Check all pairs by brute force with the M4 priority rule.
If total P0 ranks first, B + C wins (39 P0 + 21 P11). Group order in the input does not change it.

**X-E2E-08 Report with mixed-up comparison numbers (P0).** A valid plan, but a report built from
1,440 reserved, 1,308 client trucks and 154 client unused as if they were one set. The check flags it
(1,440 - 1,308 = 132, not 154). A valid plan does not make a wrong comparison right.

**X-E2E-09 Non-stackable displays, floor occupied (P0).** Synthetic TLD display units: 48 x 40 in,
50 in final height, 800 lb final gross, not stackable. Default trailer; one group/date/P0,
all available and due D, no carry/pull, one reservation. Use a supported witness of 30 positions:
15 rows, two units per row, 40 in along the trailer and 48 in across it, using 600 x 96 in.
Record this selected layout; do not claim 30 is the global maximum over all possible layouts.
Thirty source units occupy all positions in that selected witness; no additional units are invented.
Each unit is exactly 500/9 cu ft. Total cube is 5000/3 = 1,666.666... cu ft; total weight 24,000 lb.
Under explicit synthetic x/y-only policy with x = 40,000/y = 3,000, withhold and report floor
positions occupied plus below-acceptance reason. With y = 1,000, dispatch the same checked load.
Never stack the displays to raise cube. Sum exact cube before report rounding. These synthetic
outcomes can pass now. Production profile: the selected truck ships without a numeric minimum. Its fullness variant uses 31 traceable displays, 30 selected in the same witness and one eligible display left; assert FULL FloorLimited only with a complete no-fit witness within the declared supported layout model (U-FL-04). With only the original 30 sources and no separate capacity evidence, stock exhaustion alone does not prove floor limitation; NoGoodsRemain is the bucket. Never invent the 31st unit inside the original synthetic fixtures.

---

## 24. Published test cases (for ACC-1)

From OR-Library, file `binpack1.txt` (Falkenauer's uniform class, bin capacity 150, 120 items each).
The data owner states these values are the proven optimum for this class, except u120_08 and u120_19,
which are not used here.

Sources:

- Description and optimum statement: https://people.brunel.ac.uk/~mastjjb/jeb/orlib/binpackinfo.html
- Data file: https://people.brunel.ac.uk/~mastjjb/jeb/orlib/files/binpack1.txt

| ID | Instance | Capacity | Items | Proven best |
|---|---|---|---|---|
| PUB-01 | u120_00 | 150 | 120 | 48 |
| PUB-02 | u120_01 | 150 | 120 | 49 |
| PUB-03 | u120_02 | 150 | 120 | 46 |

Rules:

- Download `binpack1.txt` once, keep it in the repo, and record its SHA-256. Check the three best
  values above against the file's own header lines when you add it.
- Load the full 120 items. Never a shortened list.
- M5 must return a full packing using exactly that many bins, checked by an independent checker.
  Both are needed: the source's optimum statement and an independent check of the returned packing.
  A matching bin count alone is not enough.
- No SHA-256 is given here; record the real one from the downloaded file.
- These are 1-D tests of the solver core only. They do not replace the physical tests.

---

## 25. Coverage map

| Area | Tests |
|---|---|
| Known best answer and valid bounds | ACC-1, U-CG, X-CG including X-CG-18; X-CG-19 acceptance-adapter infeasibility separately; X-META-01/02/07/08; PUB-01 through PUB-03 |
| FFD hand-over | ACC-3, U-FF, X-CG-17 |
| Large lanes and rolling blocks | ACC-2, U-RB, U-PF, X-BLK, X-RUN including X-RUN-11/12/13, X-E2E-01/05 |
| Physical patterns and final proof | U-PB, R-1, INV-10, X-PAT, X-E2E-02/06 |
| Truck acceptance rule and remainders | U-AC-01 through U-AC-08, U-RM, X-REB including X-REB-09, X-CG-19, X-E2E-04/09, X-REPORT-04, RD-12; optional ON fixtures and this customer's production OFF profile are separate |
| Shared reservations and priority | R-2, R-4, U-RS, X-RES, X-ROUTE-02, X-E2E-07 |
| Dates | R-3, U-RS-08 to 11, X-DATE, X-E2E-03 |
| Units and master data | U-DA, X-DATA |
| Repeatability and threads | U-PF, INV-12/13, X-STATE, X-RUN-01 |
| Honest reports | U-RP, RD, X-REPORT, X-E2E-08 |
| M5 scope and untouched M4 lanes | U-RT, RD-04/05/08/09, X-ROUTE-08/09, X-STATE-03 |

---

## 26. What to record for each test result

This file specifies tests. No application result is created by writing, reviewing or adding a case.
Initialize every required variant as NOT_RUN. Record actual execution separately, one row per ID
and variant. Parameterized rows do not add new named test IDs.

| Field | Required meaning |
|---|---|
| testId and variant | Stable named ID plus exact parameter combination; no ID renumbering |
| fixtureProfile and entryPoint | FX-30/FX-CLOCK/etc or exact real configuration; normal route, direct M5 seam or test-only forced route |
| evidenceScope | SYNTHETIC, REAL_CUSTOMER or configuration-independent core check |
| executionStatus | NOT_RUN, PASS, FAIL or BLOCKED. PASS requires actual current execution and all stated assertions. BLOCKED names the missing fixture/rule/capability |
| applicability | Required/current variants participate in the current gate; conditional variants record their selection and reason explicitly. Deferred R-7 production integration is unselected until its agreed change/milestone. Unselected is not PASS and does not excuse a selected required dependency |
| customerAcceptance | Tracks real-customer policy applicability, not every milestone gate. NOT_EVALUATED for core-only evidence; PENDING where required customer policy is unresolved; PASS/FAIL only for a real-customer variant under its final contract. A synthetic PASS never approves missing customer policy |
| pendingReferences | Exact Q/CL references and required data, separate from executionStatus |
| provenance | Commit, compiler/build, input/master/fixture/config identity, seeds, thread count, machine where relevant; real checksums when available, never invented |
| model and objective | Physical/scalar scope, all-required vs partial-service demand, comparator/tie rule, relevant acceptance policy and proof scope |
| expected and actual | Quantities by source/product/group/date; final feasible packing; accepted/withheld cargo; shipped/left/carried/rejected; reservations used/unused; penalty known/unknown |
| stopping and time | Declared pre-search allowance, actual included work, stop status, selected dispatched load count, elapsed and average or N/A; separate A/B candidate results |
| evidence and reason | Output/log/checker reference, failure/block reason, unresolved reconciliation or policy; no old build/result reused as fresh evidence |

A milestone gate and customer-policy applicability are separate. ACC-1 can close with current
execution of its declared scalar/published benchmark suite; it does not need customer x/y. ACC-3
can use controlled boundary fixtures under the answered production trigger (CL-04). ACC-2 still needs its
required real-data result and final timing/policy contract.

The intended distinctions are:

- A synthetic U-AC-05 execution (optional policy ON) is separate from its productionProfile variant (policy OFF).
- U-FF boundary executions under FX-GAP are the ACC-3 evidence, because FX-GAP equals the answered Q6 rule.
- Fake-clock accounting tests check the CL-05 arithmetic; real performance evidence comes from real-data runs.
- RD-12 is a fullness reporting check (CL-03); it does not use x/y.
- A deliberately injected timeout can PASS its failure-state assertions while the solver status is
  timed out. It is not an ACC-1/ACC-2 success without those gates' required result.

Maintain a required acceptance manifest. Missing required fixtures and unsupported required
features stay BLOCKED. A nonselected alternative policy variant may be documented outside that
manifest with its reason; it is not counted as passed or silently dropped. Final acceptance has
no skipped, blocked or unexecuted required entries.

---

## 27. Changes from v1

Fixed in v1's own tests:

- RD-02: 635 unit loads is a before-fix number; now "Re-measure".
- RD-03, U-RP-05, X-REPORT-01: M5 on 7 Aug compares with 18, not 38.
- RD-06, ACC-2: the both-dates run keeps each date's reservations (21 and 20) and date rules.
- RD-07: use real reserved counts for 2028 -> 2190, not 7 and 16.
- U-PB-07: stacking must respect "not stackable" flags.
- U-RS-09: exact D+9 / D+10 / D+11 boundary.
- U-SP-02, U-RT-02: groups share one reservation pool.
- U-CG-07: "no result yet" is not "impossible".
- New RD-11: reservation totals do not reconcile with the client's 154 unused.

Added from the expanded list: Section 0.3 rules (CL-01 to CL-09), INV-17 to INV-24, EC-28 to EC-32,
Sections 11 to 26.

Not taken from the expanded list:

- Its source links and file references to an outside AI tool's library (no AI attribution ships).
- Its long status-tracking text (shortened into Section 0.1 and Section 26).
- Its CL item on whether "50" in the preview means something: the 50 is Tom's suggested threshold, already covered by Q1 and U-RT-03.

---

## 28. Changes from v2 to v3

- Title v3; references updated to Output Preview Rev5. Added Q8 (84% of trailer cube or a fixed 3,300 cu ft) with its test mapping.
- CL-01 now says 3,300 may be rounded shorthand or an intended fixed threshold. Added "acceptance" markers to CL items; new CL-10 (commit contract).
- Section 0.1: added Historical and Synthetic setting labels; split acceptance-run failure from deliberate failure-handling tests; added the final sign-off rule.
- New Section 0.4 (routing preconditions) and 0.5 (standard fixture FX-30 and its defaults). Sections 16-19 now refer to FX-30.
- INV-11 judged on complete feasible allocations; INV-12 split completed/deterministic runs from clock-limited runs; INV-16 production-only, related-date changes must reconcile. Added an applicability rule for invariants.
- R-2, U-RT-04, U-RT-06, U-RM-03, U-RM-04, U-PF-03, U-RP-01, RD-03, RD-04, RD-09 tightened.
- X-CG-04, X-CG-06, X-PAT-07, X-PAT-08, X-PAT-13, X-REB-04, X-RES-08, X-DATA-09, X-DATA-13, X-STATE-08, X-REPORT-04 corrected as specified.
- X-BLK-08, X-RUN-07 and X-E2E-05 now use one commit contract (CL-10) and separate provisional work, validated blocks and final commitment.
- X-E2E-04 now states its synthetic threshold, denominator (physical trailer cube) and scope (per group and ship date).
- Section 22 preamble on scope; X-META-01 defined as unordered multisets with repetition; X-META-02/08/09 limited to exact small models.
- Section 7 and Section 6 now state the source and verification status of historical figures; U-PB-12 labels its comparison as historical.
- Section 24: primary-source URLs and the two-part evidence rule (source optimum plus independent packing check).
- New test X-CG-18 (completed LP pricing does not prove the restricted integer result is the best). Total named tests: 244.

---

## 29. Changes from v3 to v4

Historical v4 change record: v4 referred to the nine questions as Rev6 (6 November). The supplied returned document is internally Revision 3; the current source identity and statuses are corrected in v5 above. The entries below describe the previous revision, not current approval:

- Section 10 rewritten: each question now shows Tom's answer, its status and how this plan applies it.
- CL-01 (Q8): truck acceptance is weight > x lb or cube > y cu ft, fixed numbers per customer, strict ">". x and y for this customer still open.
- CL-03 (Q4): one acceptance rule for every truck, first or last, in every group. No separate last-truck rule.
- CL-04 (Q6): Tom did not understand it; proposed internal rule of 1 truck above a valid lower bound.
- CL-05 (Q2, Q3): 10 seconds per load; proposed measure is lane time divided by loads built.
- CL-08 (Q1): the threshold of 20 is a speed setting; proposed to keep reserved trucks per lane-date and set the value from the threshold benchmark.
- Q7: date-first confirmed as the default (R-4c, FX-30 defaults, Section 0.4).
- New label "Proposed (our call)"; final sign-off rule now names the open and proposed items.
- Rewritten for the acceptance rule: INV-14, EC-09, U-RM-03, U-RM-04, X-REB-04, X-REPORT-04, X-E2E-04. Also U-RT-03, U-RT-04, U-RT-05, U-RB-04, U-FF-04, U-RP-01, U-PF-01, X-RUN-04, ACC-2, ACC-3.
- New tests: U-AC-01 to U-AC-05 (acceptance rule), RD-12 (rule checked against the client's own trucks), X-E2E-09 (non-stackable displays, floor full). Total named tests: 251.


---

## 30. Work now and continue when the answers arrive

### 30.1 Implementation order

1. Bind the test harness to the actual corrected M4 contracts and the explicit fixture profiles.
   Keep real customer configuration separate. Create test entries for every ID now; a missing
   fixture is an explicit dependency, not a reason to omit the case.
2. Implement scalar known-optimum and certificate cases, physical validation, conservation,
   reservation/date/group checks, and the corrected Tom regression fixtures that are available.
3. Implement production acceptance OFF and its optional numeric seam, final-repair rechecks, rebalance, customer/config isolation,
   rollback and method isolation using complete synthetic fixtures.
4. Implement controlled timing and report tests. Synthetic clock arithmetic is testable now;
   real customer performance still requires measured evidence under CL-05; the formula remains the stated engineering contract, not an unanswered Q6 rule.
5. Reproduce the reported M4 baseline with the actual fixtures/commit/master/config, then run
   real-data checks that have complete prerequisites. Label any synthetic-policy diagnostics.
6. Keep a current result/dependency ledger. Run the final required suite after answers and
   production contracts are incorporated; earlier synthetic results are retained as fixture evidence.

### 30.2 Remaining open items and affected work

| Open item | Work that can proceed now | When it arrives | Revisit or run next |
|---|---|---|---|
| Tom's exact R-1a/b inputs; exact R-5a/R-8/R-9 package must be committed and checksummed | R-5b stress and self-contained synthetic cases; v8 records R-5a/R-8/R-9 files as available | Verify exact bytes and commit fixtures without replacing them with a reconstruction | R-1a, R-1b; run R-5a/R-8/R-9 once their recorded files are present |
| Deferred per-lane past-due override (P9), outside current required integration | Current run-wide controls and direct synthetic profile isolation | Only after the change/milestone is agreed, select integration variants and read the per-lane value in the snapshot | R-7 conditional integration, R-4, X-DATE-08, then no-override fingerprint check |
| Objection to the stated settings (CL-05 formula, 14 in) | Build with both as settings | Change only the configuration value or formula | U-PF-01/05, U-FL-03, ACC-2, RD-10 |
| A future customer needing minimum-load inside M5 | Optional policy seam already tested | Record that customer's x/y profile | Synthetic U-AC variants become that customer's variants |

### 30.3 Engineering work that does not need another customer question

- Record Q3's delegated method choice, scheduling and selection behavior; validate method isolation.
- Recover corrected M4's complete CL-02 comparator/tie rule and CL-07 packing atom behavior.
- Freeze CL-09 repeatability, CL-10 atomic commitment/retry, ED-FULL and OPEN-COMP contracts; bind the required test variants.
- Record the CL-08 routing mechanism and calibration using actual reserved counts and the final
  timing metric. Keep historical built counts separate.
- Resolve config validation domains and penalty source/unit definitions from the actual interfaces
  and existing data. Missing monetary rates remain unknown, not zero.
- Commit/checksum Tom's R-1a/R-1b/R-5a files (Section 40) and the R-8/R-9 package, and any missing required real fixtures. Do not substitute guessed data.

### 30.4 Incorporating an answer without losing work

Record the response/source and decision date in Section 10 and the relevant CL entry. Add or update
the real-customer configuration; retain synthetic fixture values unless their own model changes.
Update every affected expected result and report assertion, not only the decision table. Preserve
all 299 named IDs; parameterized variants use the same IDs. Run affected tests and then
the full required acceptance manifest at the handover commit. A milestone gate closes only with current execution evidence for its required model and final
contract: ACC-1 uses the declared core benchmark suite, while other gates use their specified
policy and real-data evidence. A synthetic PASS never resolves missing customer policy or
substitutes for required real fixtures. An answer alone is not a test pass.

---

## 31. Changes from v4 to v5

Updated 8 November 2026 to allow implementation to proceed while five customer follow-ups remain open.

- Corrected source identity to the returned document's internal Revision 3; retained earlier revision
  notes as history. No unanswered item is presented as a new answer from Tom.
- Added working profiles, direct M5 test entry points, a current report contract, a two-axis result
  ledger and an explicit continuation map for later replies.
- Corrected X-RES-12, X-REB-04/07, R-2/E2E-02 acceptance preconditions, R-4 physical setup, X-BLK-06,
  X-ROUTE-07, INV-11/14/19/21, timing cases, report assertions and rollback/retry semantics.
- Distinguished current reported M4 measurements from reproduced baselines; corrected RD-12's
  historical comparability assumptions and the unsupported estimate 22/infeasibility conclusion.
- Scoped scalar/metamorphic cases correctly and corrected renaming ties and display cube precision.
- Completed X-E2E-03 policy/eligibility, X-DATE-06/09 local fixtures, X-RUN-03 commit contract and
  X-RES-07 alternative-optimum handling; no extra IDs were introduced for these corrections.
- Integrated eight new cases: U-AC-06/07/08, X-REB-09, X-RUN-11/12/13 and X-CG-19.
- Retained every existing named test, invariant, EC entry and CL ID. No application test result
  is claimed; all execution rows begin NOT_RUN until actually executed.

| Family | Named tests in v5 |
|---|---:|
| ACC | 3 |
| R | 9 |
| U | 84 |
| RD | 12 |
| X | 148 |
| PUB | 3 |
| Total | 259 |

The 24 invariants, 32 EC checklist rows, 10 CL rules, parameterized variants and generated
1,716-input collection are not counted as additional named tests.


---

## 32. Changes from v5 to v6

Updated 11 November 2026 after Tom's follow-up answers, his forwarded review notes and the corrected M4
(e51d9c4).

- Minimum-load acceptance moved out of M5 (export check after the algorithm). The predicate stays an
  optional policy seam; synthetic U-AC and related tests keep their IDs and gain a required
  `productionProfile` variant (Section 4.14). INV-14, CL-01, U-RM-03/04/05 rewritten.
- New fullness rule CL-03: 98% weight, rear inches (default 14 in, user setting), or floor-limited; report
  buckets matching Tom's chart. New tests U-FL-01..05; RD-10/12 and U-RP-07 rewritten.
- Q6 answered (CL-04): careful method whenever the quick result is above a valid minimum, user-set time
  limit, best plan at the limit. ACC-3 now has a production rule.
- Time contract (CL-05): secondsPerLoad x reserved trucks per lane-date. New test U-PF-05.
- Can-go fill on M5 lane-dates (FX-CANGO). New tests U-CF-01..03.
- Comparator (CL-02) recovered from corrected M4; goal order priority and use-all-trucks first; no production
  fewer-trucks tie-break; judge across linked dates (new RD-13).
- Q5 answered; Q7 per-lane setting required (new R-7).
- M4 baseline moved to e51d9c4; Tom's 3da08ab cases added as R-5a (waiting for exact data), R-5b (stress)
  and R-6. RD-02/04/11 updated with the baseline findings.

| Family | Named tests in v6 |
|---|---:|
| ACC | 3 |
| R | 13 |
| U | 93 |
| RD | 13 |
| X | 148 |
| PUB | 3 |
| Total | 273 |

New IDs: R-5a, R-5b, R-6, R-7, U-FL-01..05, U-CF-01..03, U-PF-05, RD-13. The 24 invariants, 32 EC rows,
10 CL rules and the 1,716 generated inputs are not counted as named tests.


---

## 33. Additional tests from the v6 architecture review

All cases below start NOT_RUN. They are specifications, not executed M5 results. Preserve the existing
273 IDs. These 18 additions target new-rule interactions and distinct failure modes. Each case names
its model; scalar predicates do not imply a physical packing. Unless overridden: synthetic inputs,
one lane/group, available and due on D, default 630 x 100 x 108 in / 45,000 lb trailer, whole source
units, no extra TLD deck/tare, and acceptance OFF. Small cases call M5 directly or the named seam.
No production-routing test silently increases reservations merely to reach M5.

### U-AC-09 Sub-grain strict acceptance comparisons

Priority: P0. Owner M-POL; WP1; direct POLICY seam. Optional numeric acceptance ON; x = 40,000 lb,
y = 3,000 cu ft. Supply authoritative normalized cube totals 3,000 - 1/4096, 3,000, and
3,000 + 1/4096 with weight 20,000 lb. These binary-exact values are 2,999.999755859375,
3,000 and 3,000.000244140625. No physical geometry is asserted by this predicate fixture.

Expected: false, false, true. Nearest-0.001 quantization would erase the distinction and is forbidden
for this comparison. All three may print as 3,000.000, but display formatting cannot change the decision.
Repeat the weight branch at x - 1/256, x and x + 1/256, cube 2,000: false, false, true. A policy-OFF
control bypasses the predicate and needs no x/y; it does not automatically validate a physical load.

Oracle: independent rational/integer cross-multiplication, not the implementation's rounding helper.
Variants: cubeBelow, cubeEqual, cubeAbove, weightBelow, weightEqual, weightAbove, policyOff.
References: CL-01, INV-14/23, EC-33; complements the larger offsets in U-AC-01/X-REPORT-04.

### U-FL-06 Exact fullness boundaries and actual trailer capability

Priority: P0. Owner M-VAL; WP3; VALID seam with independently checked layouts.

Weight variants: one loaded nonstackable TLD 200 x 100 x 108 in, final weight 44,099.999,
44,100 or 44,100.001 lb. One eligible same-group leftover TLD 20 x 100 x 108 in, 1 lb, fits
in every case. Rear space 430 in and the fitting leftover prevent other reasons masking the weight
comparison. FULL is false/true/true because 98% of 45,000 is exactly 44,100. Use exact supplied metric
values, not the rounded printed percentage. Separately, weight 29,400 is FULL against a 30,000 lb
slot and not FULL against a 45,000 lb slot, with the same layout and fitting 1 lb leftover.

Rear variants: one loaded TLD of length 616 - 1/16384, 616, or 616 + 1/16384 in, width 100,
height 108 and weight 1,000 lb. A leftover 1 x 100 x 108 in / 1 lb unit fits. Rear values are
14.00006103515625, 14 and 13.99993896484375 in. Limit 14 gives false/true/true by rear.
Conservative search grains and printed 14.000 values must not overwrite authoritative label metrics.

Oracle: exact rational threshold and extent arithmetic plus independent layouts. Fresh source/slot
snapshots per variant. References: CL-03, INV-02/03/14/21/23, EC-33.

### U-FL-07 Rear space after maximum legal upstream stacking

Priority: P0. Owner M-PHY with M-VAL; WP3; M2 stack-builder plus controlled VALID seam.

Three traceable TLD units are each 210 x 100 x 36 in, 1,000 lb, compatible for stacking up to
108 in. A fourth eligible leftover is nonstackable TLD 20 x 100 x 108 in, 100 lb. Candidate cargo
is only the first three units. One reservation; acceptance OFF. This is a label/stacking control,
not a claim that leaving the fourth fitting unit is an optimal production selection.

With maximum legal stack count three, upstream M2 yields one 108-in stack, floor length 210,
rear 420, weight 3,000. The leftover fits: NOT FULL. Inject an old unstacked witness occupying 630
in and an old FULL flag; the new candidate revision must discard that label. With max stack two,
two floor positions use 420 in, rear 210: NOT FULL. With explicit nonstackability, three floor
positions use 630 in: FULL by rear. Never stack beyond allowed compatibility/height, and never
re-stack rigid M4 selection atoms inside M5 merely to obtain a label.

Oracle: explicit stack/component and floor witnesses, source conservation and height arithmetic.
Variants: threeHigh, twoHigh, nonstackable, staleLabel. References: CL-03/07, INV-01/03/04/14, EC-35.

### U-FL-08 Residual scope, stock exhaustion and bucket precedence

Priority: P0. Owner M-VAL; WP3; VALID seam under engineering ED-FULL (Section 0.8).

Controlled candidate: one nonstackable TLD 600 x 100 x 108 in, 1,000 lb. Rear 30, weight below
98%, one reservation. Test fresh same-group snapshots, acceptance OFF, pull window 10:

1. Eligible leftover 20 x 100 x 108 in, 100 lb: fits -> NOT FULL, GoodsRemain.
2. Eligible leftover 40 x 100 x 108 in, 100 lb: complete no-fit -> FULL FloorLimited,
   GoodsRemain. The remaining region is 30 x 100 in: either orientation needs 40 or 100
   longitudinal inches. Total floor area is also 64,000 > 63,000 sq in, a valid no-fit proof.
3. No residual source goods: NoGoodsRemain. Do not assert FloorLimited from an empty probe set;
   direct weight/rear reasons remain false and no-fit is NotApplicable.
4. Only the 20-in unit due D+11, otherwise available: RuleLimited, no FloorLimited merely because
   the eligible set is empty. An independent physical-fit control succeeds.
5. Eligible 40-in unit plus excluded 20-in unit: GoodsRemain takes precedence; only the eligible
   unit enters the no-fit set and exclusions retain their reasons.
6. Residual units only in another do-not-mix group: they do not become this truck's fit probes.

Switch from variant 2 to 1 in one process with a changed residual-snapshot identity: cached floor-limit
evidence must clear. Reversing selected truck order does not change final-snapshot buckets. Labels
never withhold the checked truck. Oracle: independent fit witnesses, exact source/eligibility ledger.
References: CL-03, INV-01/06/07/08/14/21/22, EC-34.

### U-FL-09 Interrupted no-fit diagnostics remain unknown

Priority: P0. Owner M-VAL with M-ORC; WP3, budget integration runnable in WP7; VALID + fake-clock seam.

Use U-FL-08's 600-in candidate. Residual same-group classes are the 40-in nonfitting unit followed
by the 20-in fitting unit, both eligible. Inject a stop after probing only the first class.

Expected: no FloorLimited certificate. With weight/rear reasons false, report no-fit Unknown and
fullness not established, not a conclusively FULL or NOT FULL claim. Completion finds the fitting
20-in unit -> NOT FULL. Reversing probe order exposes a fitting witness immediately and must never
produce a false no-fit result. A control candidate of weight 44,100 lb may be FULL by weight even
when the additional fit diagnostic is incomplete; reason provenance stays separate.

The diagnostic consumes the existing budget, not a new one. An advisory incomplete label alone
does not withhold an otherwise finally validated OFF-profile shipment. Any old witness belongs to
its original candidate/profile/residual revision. Oracle: injected trace and independently known fit.
References: CL-03/05, INV-10/14/19/21/24, EC-34/38.

### U-FL-10 Rear metric comes from final occupied extent

Priority: P0. Owner M-PHY with M-VAL; WP3; geometry/VALID seam.

Two nonstackable rigid TLD rectangles share the floor side by side: A length 500, width 50 at
(x=0,y=0), B length 600, width 50 at (0,50), each height 108 and weight 1,000 lb. One eligible
leftover 20 x 100 x 108 / 100 lb fits in the rear. Default trailer, one reservation, acceptance OFF.
The farthest occupied extent is 600, rear is 30, and the truck is NOT FULL. Sum of item lengths
1,100 is not used length. Changing input/row order leaves the metric unchanged.

Translate both loaded rectangles forward to x=20 in a supplied valid witness: farthest extent 620,
rear 10, FULL by rear, with the 20-in front gap preserved in the witness. Do not silently compact
this label-only supplied witness. In a physical-validation fault variant put B at x=31: extent 631,
so reject the invalid layout instead of treating rear -1 as satisfying rear <= 14. The same rule
applies to overlap and nonfinite coordinates. Oracle: independent rectangle/extent arithmetic.
References: CL-03, INV-03/14/20/23, EC-35.

### U-CF-04 Required goods win scarce reservations across groups

Priority: P0. Owner M-INT with M-ALC/M-MA/M-MB; WP8 after WP5; DIRECT M5 seam.

Two default reservations on D; pull ON10; PriorityFirst; acceptance OFF. Group A has 31 FX-30
units due D at P11. Group B has 30 FX-30 units due D+1 at P0; all available D. No other goods.
Present B first, reverse rows/groups, and exercise both candidate methods.

Expected: all 31 required A units ship in two nonempty legal trucks (each <=30); B's 30 remain.
A30+B30 is wrong because it trades an eligible required unit for optional future service. The short
A truck cannot be withheld for minimum load, and B cannot mix into its spare space. With a third
reservation, all 61 units ship using three trucks. Controls due D+11 or available D+1 cannot ship
early even with the third slot. Required precedence is global across the shared pool, not per group.

Oracle: exhaustive group/slot counts with required-class precedence; no fill tie is needed to decide.
References: CL-02, INV-01/05/06/07/08/11/14/17, EC-32/36.

### U-CF-05 Can-go-only demand on an existing reserved M5 date

Priority: P0. Owner M-INT with M-LED/M-ALC; WP8; ROUTE integration, ED-CANGO working contract.

Existing lane-date D has 21 reservations and T=20. No goods are due by D. One indivisible FX-BLOCK
atom is due D+1 and available D, one group; pull ON10, acceptance OFF. Eligible offers must reach
the lane-date before a required-demand-only no-work exit. No date or reservation is manufactured.

Expected under recorded ED-CANGO: one checked truck carrying the one whole atom ships on D,
20 reservations remain unused and source quantities reconcile. Pull OFF retains the atom and ships none on D.
Zero-reservation control ships none. Due D+11 and unavailable D+1 controls retain the atom.
The count-routing integration runs normally at 21 > 20; a direct-only test cannot substitute for it.

This case records v6's usable-reservations interpretation; it is not a new quotation from Tom.
Verify the actual source/adapter contract in WP0. If a different explicitly agreed can-go-only rule
exists, record that conflict and disposition instead of silently changing/omitting this expectation.
Oracle: exact eligibility, source and reservation ledger. References: CL-02/08, INV-01/05/07/08/16, EC-37.

### U-CF-06 Only the selected future draw changes later M4 input

Priority: P0. Owner M-INT with M-ORC/M-COM/M-LED; WP8; linked ROUTE integration.

One lane: D has 21 reservations, D+1 has 20, T=20. R has ten FX-30 units due/available D;
F has twenty due D+1, available D. Same group, pull ON10, no carry, acceptance OFF. Original total 30.
Controlled candidate A ships R10+F20 in one D truck; B ships R10 only. Both inspect equivalent
snapshots; exercise A/B, B/A and each alone. The selection/commit seam explicitly selects A in
the main variant and B in the control. This isolates delta accounting; it does not assert A is
globally preferable across linked dates. Both alternatives may ultimately serve the same sources,
so a production comparison could reach the recorded OPEN-COMP tie contract.

Expected: only A's selected delta removes F20 from later demand, exactly once. D+1 remains an M4
date but sees F0 and builds no truck for those sources. Unselected B never consumes F. Before-commit
failure leaves F20 available; after-commit retry uses reconciled residuals or exact idempotent replay.
For isolated B-only control, D ships R10, D+1 sees and ships F20. Across both dates the original 30
ship once, never 50. An unused provisional offer is not a committed carry movement.

Oracle: independent per-source/date deltas and reservation versions, not only grand totals.
References: CL-10, INV-01/05/07/08/16/17/24, RD-04/13, EC-37/40.

### U-CF-07 Exact spare-capacity fill without displacing required goods

Priority: P0. Owner M-INT with M-MA/M-MB/M-ALC; WP8 after WP5; DIRECT seam.

One reservation; 25 required FX-30 units due D at P11; ten can-go units due D+10 at P0;
one extra unit due D+11 and one due D+5 available D+1. Others available D, same group,
PriorityFirst, pull ON10, acceptance OFF. Run both methods with explicit source IDs.

Expected: all 25 required plus exactly five of the ten eligible can-go units ship: 30 units,
45,000 lb, 3,750 cu ft. Five eligible future units and both ineligible units remain with distinct
reasons. Never replace required P11 goods by future P0 or exceed weight with a 31st unit.
Under pull OFF, only the 25 required ship. With required count 30, no future unit is added.
Equivalent choices among otherwise equal future source units follow the stated canonical rule;
the test does not invent a unique source winner without that rule.

Oracle: arithmetic capacity 30, exhaustive required/future counts, source-level ledger.
References: CL-02, INV-01/02/07/08/11/14/18, EC-36.

### U-CF-08 Impossible required cargo cannot deadlock feasible can-go

Priority: P0. Owner M-INT with M-ALC/M-VAL; WP8 after WP5; DIRECT seam, ED-CANGO.

One default slot. Required source H is one nonstackable TLD 20 x 100 x 108 in weighing
45,001 lb, due/available D. Future F is 30 FX-30 units due D+1, available D, same group,
pull ON10, acceptance OFF. H is independently impossible on every available slot.

Expected: never ship or split H; record the overweight reason once. F may fill the usable slot
and all 30 ship under the working fill contract. Do not loop forever waiting for all required
goods to ship or misreport H as acceptance-withheld. Total 31 = F30 shipped + H1 explicitly
unserved/rejected (not both). Counter-control replaces H with one valid FX-30 unit: it must ship,
with only 29 future units. A merely unsearched/unproven required alternative cannot be labelled
physically impossible to justify serving future goods.

Oracle: weight proof and exhaustive required-first counts. References: CL-02, INV-01/02/11/15/18/24, EC-36.

### U-PF-06 Budget arithmetic, zero reservations and overflow

Priority: P0. Owner M-POL with M-ORC; WP1, clock integration WP7; POLICY/CLOCK seams.

Positive secondsPerLoad 0.1 with 21 frozen reservations yields 2.1 seconds; 2.5 with 20 yields 50;
positive secondsPerLoad with zero reservations yields a valid zero allowance and zero dispatch.
The zero-reservation result still accounts for all offered sources and performs no search.

Reject NaN, infinity, negative/zero/missing secondsPerLoad as required by U-PF-05. Also test finite
inputs whose multiplication or duration/deadline conversion cannot be represented: e.g. 1e308 x21,
and a checked integer-clock deadline whose start plus allowance exceeds the clock range. Do not wrap,
silently cap, truncate fractions to whole seconds, or interpret overflow as unlimited time. The exact
duration representation and supported precision must be declared; the 2.1-second fixture is exact on
a synthetic nanosecond clock (2,100,000,000 ticks).

Oracle: independent checked arithmetic and injected clock bounds; errors occur before search/commit.
References: CL-05, INV-05/15/23/24, EC-38.

### U-PF-07 Parallel work shares one wall-clock deadline

Priority: P0. Owner M-ORC; WP7; fake monotonic CLOCK seam, no real sleeping.

One lane-date, 21 reserved, secondsPerLoad = 1, allowance 21. Inject optional parallel A/B work both
starting at t0 and each ending at t8. Their elapsed wall interval is 8, not 16. It does not create
two 21-second budgets or add worker counts to the allowance. A separate CPU-work metric may sum 16
only if labelled CPU work. Selection, repair and final checks then use the same clock/deadline.

Inject completion of all mandatory checks at t20.999 and at t21.000: the declared <=21 contract
allows the checked result. A new task at the deadline is not started. If a candidate still needs a
check after t21, it cannot replace a fully validated earlier incumbent or be published as checked.
Timeout returns the permitted earlier incumbent, or timeout-without-incumbent, with actual elapsed
and no false compliant timing claim after an overrun. Run a sequential 8+8 control: 16 elapsed,
five remain. Deterministic work-count fixtures are a different stop mode, not converted from CPU sum.

Oracle: injected event/clock ledger and validation revisions. This is a budget seam test, not a
requirement to introduce production parallel A/B scheduling. References: CL-05/09/10, INV-13/17/24, EC-38.

### U-PF-08 Separate lane-date allowances cannot be pooled

Priority: P0. Owner M-ORC with M-INT; WP7; CLOCK seam with explicit forced/direct M5 planning.

Same lane D has 21 reserved, D+1 has 20, secondsPerLoad = 1: allowances 21 and 20, each declared from
its own frozen count before that date's search. D uses 5 seconds; the unused 16 cannot enlarge
D+1 to 36. A D+1 simulated 21-second attempt fails that date's 20-second timing requirement even
though combined 26 is below 41. Joined benchmark reporting preserves both allowances and flags.

Repeat with two independent lanes at the same date, reordered and concurrent. Neither receives
the other's allowance. Candidate counts, reservation changes after snapshot, or a faster first
method cannot create new time. A changed reservation version requires a fresh explicitly identified
planning context; it never mutates an in-flight deadline. Normal T20 routing would keep the 20 date
in M4; this M5 budget fixture names its direct/forced seam rather than pretending normal escalation.

Oracle: independent per-context arithmetic and fake-clock trace. References: CL-05/08, INV-05/13/16/24, EC-38.

### X-CG-20 A subset count bound cannot certify priority service

Priority: P0. Owner M-ORC with M-OPT/M-ALC; WP7; DIRECT allocation/evidence seam.

Two reservations, one group, five FX-BLOCK atoms all due/available D. Three atoms are P11 and
two P0. Optional acceptance OFF, PriorityFirst. Inject a quick candidate shipping two P11 atoms,
with its valid count bound B=2 for that shipped two-atom subset. F=2 equals that bound only in
the subset count problem. The full all-required five-atom model has bound 5; the two-slot planning
objective is priority-optimal partial service, not all-required minimum trucks.

Expected: do not transfer subset equality into global optimality or bypass the required service
comparison. With declared sufficient deterministic budget, ship both P0 atoms; the three P11 remain.
A count gap on F=2 versus all-goods B=5 is not clipped into a zero-gap certificate. Report count
metric as noncomparable/not applicable to the partial objective. A timed early incumbent remains
Unproven and cannot pass this exact expected-selection variant.

Control: capacity 10 scalar goods [6,6,4,4], all required, quick result 2 and valid same-model bound 2.
Q6 correctly skips further COUNT escalation, with its proof scoped only to that model; it does not
claim physical, can-go or cross-truck-fill optimality. Oracle: analytic one-atom-per-truck count and
exhaustive five-choose-two priority check. References: CL-02/04, INV-10/11/19, EC-21/32.

### X-STATE-09 Optional acceptance ON/OFF/ON across every planning path

Priority: P0. Owner M-ORC with M-POL/M-MA/M-MB/M-REP/M-VAL/M-RPT; WP7, report assertions WP8.

Fresh identical snapshots per run: twenty FX-30 units, one group, due/available D, one reservation,
no pull/carry. Run ON with x = 40,000/y = 3,000, production OFF with the acceptance section absent,
then ON again; compare fresh-process runs. Exercise A, B, remainder, repair, final validator and
report paths. Profile identity/revision is recorded, including same-ID revised settings.

Expected: ON withholds all 20 for its numeric rule; OFF permits all 20 on one checked truck and
does not require x/y or run acceptance-driven withholding/rebalance; ON withholds again. OFF
report states export-after-M5 and contains no stale AcceptanceWithheld reason or old thresholds.
All 20 reconcile in each separate snapshot; provisional state from a previous run consumes nothing.
Fullness remains advisory with current snapshot metadata; empty residual alone is not FloorLimited.
A present but malformed acceptance section is still a configuration error per Section4.14.

Oracle: independent strict predicate, source ledger and event trace proving OFF bypasses the gate.
References: CL-01/03/10, INV-01/14/16/17/21/24, EC-39.

### X-STATE-10 Atomic commitment, payload conflicts and stale snapshots

Priority: P0. Owner M-COM with M-LED/M-ORC; WP7; transactional test double/interleaving seam.

Snapshot V7 contains 20 individually identified FX-30 units and two free slots; acceptance OFF.
Selected candidate C1 uses ten specified units and one slot. Operation O1 binds expected inventory
and reservation versions, C1's exact payload digest and its validation context. Run these variants:

- Concurrent identical O1 submissions: exactly one delta applies; both observe the same committed
  result/version V8, ten units remain and one slot remains. Idempotency check, version check, source
  delta, reservation delta and operation record are one atomic operation.
- Same O1 with different cargo/layout digest: conflict; no second delta. Candidate revision alone
  must not create a new authorization to replay the original sources.
- New O2 from stale V7 using C1's original units: reject stale context; no duplicate shipment,
  even if a different candidate ID or still-free slot would make a shallow check appear valid.
- Inject a failure between physical data updates and operation-record storage in a transactional
  test double: recover entirely pre-commit or entirely committed, never a half-applied ledger.
- Reconciled continuation from V8 uses the remaining ten units and last slot under its own operation
  and expected version, yielding V9 with 20 total shipped. Replaying O1 still returns its prior result
  without subtracting quantities again. Conflicting current master/policy validation contexts require
  revalidation before a new commit; an idempotent read of an already committed result does not dispatch again.

Oracle: independent per-source counters, slot ownership, version history and operation log, not
only a boolean alreadyApplied flag. Atomic in-memory semantics are sufficient unless the real
application requires persistence; no new database/service is mandated. References: CL-10,
INV-01/05/13/17/18/24, EC-40.

### X-REPORT-10 Bucket averages and overlapping FULL reasons

Priority: P0. Owner M-RPT; WP8; REPORT seam with explicit structured label outcomes.

Four synthetic selected truck records have rear spaces 10, 30, 100, 20 in. First two are GoodsRemain,
third NoGoodsRemain, fourth RuleLimited. Give them unequal cargo quantities so a cargo-weighted
mean would differ. Expected bucket means are 20, 100, 20 in; overall truck-weighted mean 40 in.
The mean of the three bucket means (140/3) is not the overall mean. Empty bucket control prints
N/A, not0. Reordering trucks/lanes/groups does not change totals.

Set the first record's independently supplied weight and rear reasons true. It is one FULL truck
with two reasons: unique FULL count 1, weight-reason count 1, rear-reason count 1. Reason counters
are explicitly overlapping and are never summed as disjoint shipments. Other records can be not
FULL or fit-Unknown; Unknown is reported separately and never labelled proved FloorLimited.
Unselected A/B candidates and benchmark-only results are excluded from selected production totals.

Oracle: independent sum/count arithmetic and parsed output. This report fixture tests supplied
structured labels, not new physical feasibility. References: CL-03, INV-14/22, EC-41.

---

## 34. Changes from v6 to v7

Reviewed 11 November 2026. No new customer answer or application execution is claimed.

- Preserved all 273 previous named IDs; added 18 targeted cases with explicit fixtures/oracles.
- Removed active stale x/y-required and Q6-pending expectations. Kept historical change records.
- Kept the 14-in default and reserved-count time formula attributed as settings stated to Tom,
  distinct from his directly answered rules. Added explicit engineering no-fit, bucket, can-go,
  numerical, comparator and atomic-commit contracts; OPEN-COMP remains a visible engineering item.
- Repaired U-AC-08's production fixture so 14/30/14 actually produces false/true/false, and stopped
  assigning physical fullness labels to scalar X-REB-04.
- Defined U-FL remaining-stock fixtures, invalid/default configuration distinction, and display
  no-fit evidence. Separated U-RM-04 numeric acceptance from fullness and qualified usable trucks.
- Strengthened R-7, U-CF-03 and X-RUN-09 variants without renumbering them. Added EC-33..41.
- Companion architecture/handover revision 4 and schema 2 manifest record modules, variants,
  dependencies, exact current source identity, unique evidence keys and remaining data gaps.

| Family | Named tests in v7 |
|---|---:|
| ACC | 3 |
| R | 13 |
| U | 107 |
| RD | 13 |
| X | 152 |
| PUB | 3 |
| Total | 291 |

New IDs: U-AC-09; U-FL-06..10; U-CF-04..08; U-PF-06..08; X-CG-20; X-STATE-09/10;
X-REPORT-10. The 24 invariants, 41 EC rows, 10 CL rules, parameterized variants and generated
inputs are separate from this named-test count. All M5 execution remains NOT_RUN/BLOCKED until
the implementation and its required fixtures are actually exercised.


---

## 35. Changes from v7 to v8

Updated 14 November 2026. No new customer answer and no M5 execution is claimed. Tom's e6dbb0f review of M4 and its
verification at f9d9945 (m4Fix2Verification.txt, REPORTED) change these cases:

- R-5a: Tom's exact fourteen-stack inputs are available. 23 unit loads in all nine distractor variants. "Waiting for
  data" removed; the m4Path and m5Path variants move from BLOCKED to NOT_RUN; a distractorVariants variant is added.
- R-5b: extended (not renumbered) with the 11-16 stack region, 14-20 stacks of few footprints and an exact
  row-grouping oracle; two region variants added.
- R-7: AgeFirst run-wide stays current until a change is agreed; the per-lane override is deferred (integration variants
  BLOCKED for that reason).
- New R-8: Tom's eleven-item case (ten shipped as proven; eleven fit in 586 in) through selectLoads, planShipDates and the
  M5 seam, plus his 15,000-trial probe (counted so that it is not vacuous).
- New R-9: unit-load cap from configuration (cap 64 gives 64 unit loads in 576 in, cap 68 gives 68 in 612 in).
- New EC-42 (a heuristic or exhausted floor search refusal backing a proven claim) and EC-43 (a universal 64 cap).
- Baseline moved from e51d9c4 to f9d9945 in RD-02, RD-04, RD-10, CL-08, Section 6 and Section 7; the figures are in the
  Baseline note of Section 6. e51d9c4 and 7cef94e remain named as previous baselines.
- Companion architecture/handover Revision 5, schema-2 manifest and regenerated traceability.

| Family | Named tests in v8 |
|---|---:|
| ACC | 3 |
| R | 15 |
| U | 107 |
| RD | 13 |
| X | 152 |
| PUB | 3 |
| Total | 293 |

New IDs: R-8, R-9. The 24 invariants, 43 EC rows, 10 CL rules, parameterized variants and generated inputs are
separate from this named-test count. All M5 execution remains NOT_RUN/BLOCKED until the implementation and its
required fixtures are actually exercised.


---

## 36. Additional tests from the v8 implementation-contract review

These six cases are synthetic, start NOT_RUN, and do not require the original Tom probe files. They
exercise the new cap, bounded floor search and evidence contracts at their declared seams. Complete
small exact controls must reach the stated objective under sufficient deterministic allowance;
injected-stop variants assert honest failure-state behavior instead.

### Shared synthetic fixture FX-CAP-ATOM

This fixture supplies already valid, rigid M4 stacking atoms to the direct M5 seam. M5 must not
split or restack them. All components are TLD, with no extra deck or tare. Each completed atom
has footprint 48 x 36 in, height 90 in and gross weight 600 lb:

- T2 contains two unit loads, each 48 x 36 x 45 in and 300 lb, legally stacked upstream.
- T3 contains three unit loads, each 48 x 36 x 30 in and 200 lb, legally stacked upstream.

The products for T2 and T3 are distinct resolved identities. Their completed physical geometry
and mass match, but their unit-load resource coefficients are 2 and 3. Each physical unit has
traceable source ownership. Unless a case explicitly coalesces shares from one source line,
each component has its own source line with unitLoads = 1. A coalesced share retains the sum
in Component::unitLoads; the length of the components vector is never a unit-load total.

Use one lane, group G, priority P0, all due and available on D, carry/pull OFF, minimum-load
acceptance OFF. Trailer dimensions are 630 x 100 x 108 in and maxWeight 45,000 lb. Each case
sets every slot's positive integer maxUnitLoads explicitly. These small fixtures invoke M5
directly; they do not assert the normal >20 routing threshold. Fresh source and reservation
snapshots are used for independent runs.

A simple legal witness for n atoms places atom k, starting at k=0, at
x = 36 * floor(k/2), y = 48 * (k mod 2), with placed length 36/width 48/height 90.
Extent is 36 * ceil(n/2), occupied width at most 96, mass 600*n. All fixture demands below have
at most eight atoms, so floor, height and weight are deliberately slack. These are witnesses,
not required optimal rear-space layouts. An independent rectangle checker verifies identities,
orientation, bounds and non-overlap. Source identities may vary among equal-service optima.

### X-PAT-16 Unit-load coefficients survive physical class compression

Priority: P0. Owner M-PHY with M-LED/M-MA; WP3, planning controls runnable after WP4.
DIRECT physical-class/pattern seam. Fixture FX-CAP-ATOM; one slot, cap 5.
The declared exact objective is maximum shipped unit loads of the single urgency class.

Use three T2 atoms A1..A3 and two T3 atoms B1..B2: five atoms, twelve source unit loads.
Enumerate all 32 atom subsets independently. The best cap-feasible service is five unit loads,
one T2 plus one T3. A witness uses two atoms in one row, mass 1,200 lb; seven unit loads remain.
Neither two T3 atoms (six units) nor any split of a rigid atom may be loaded to manufacture a
five-unit answer. Counting five atoms or five component records as five loads fails this case.

Required variants:

- mixedClasses: the complete five-atom fixture ships 5 and leaves 7.
- homogeneousTwo: A1..A3 only, six source units; at cap 5 exactly two T2 atoms ship 4 and leave 2.
- homogeneousThree: B1..B2 only, six source units; at cap 5 exactly one T3 ships 3 and leaves 3.
- sameGeometryABA: run homogeneousTwo, homogeneousThree, homogeneousTwo in one process from
  fresh snapshots, then compare with cold runs. Results are 4/3/4. Geometry witnesses may be
  reused after validation; a capacity coefficient, pattern count or optimum certificate from
  one composition cannot be reused as if it belonged to the other.
- coalescedSourceShares: repeat mixedClasses with each T2 represented by one component share
  of quantity 2 from its own source line and each T3 by one share of quantity 3. Result remains
  five shipped/seven left. Sum component quantities with multiplicity, not components.size().
- invalidCountsAndCaps: at the numeric/config seam, the proposed signed64 cap domain is whole
  integers 1..2^63-1. Controls 1/5/68/2^63-1 are representable; missing/null, nonnumeric text, 5.5,
  zero, negative, NaN, infinity and 2^63 are errors before planning. Use exact input text/integer
  parsing for the large values. A component-total addition (2^63-1)+1 and multiplication
  2 unit loads * 2^62 copies must fail checked arithmetic; these are count-only seam controls,
  not physically materialized atoms. Never wrap, round, clamp or expand those copies. No cargo
  or reservation is changed by an invalid control. This freezes the representability domain
  of the proposed int64 fields; any smaller implementation limit must be explicitly documented.

A physical-only class may share geometry, but optimization classes/columns must preserve the
per-atom unit-load coefficient. A single class coefficient is valid only when every member
has that coefficient. All final source quantities and slot counts are independently checked.
This test does not require a particular source-ID tie winner or a new M5 stacking rule.

Oracle: exhaustive whole-atom subsets, exact sum of source quantities, independent supplied
layouts. References: CL-07, ED-20, INV-01/03/04/09/10/18/20/21/23, EC-43/44/46.

### X-PAT-17 Caps follow actual slots through methods, pattern copies and repair

Priority: P0. Owner M-VAL with M-PHY/M-MA/M-MB/M-REP/M-LED; WP4, controlled repair at WP4,
Method A/allocation at WP5, Method B at WP6 and final A/B selection at WP7. The complete case
is runnable after WP7. DIRECT methods plus controlled validation/repair seam.

Use eight T2 atoms under FX-CAP-ATOM: sixteen unit loads. There are exactly two slots,
S-big with cap 5 and S-small with cap 3; all other capabilities match. The exact maximum
service is six unit loads: two atoms/four units on S-big and one atom/two units on S-small.
The upper bound is 2*floor(5/2) + 2*floor(3/2) = 6, attained by the supplied row witnesses.
Ten units remain; exactly two slots are used. Adding the raw caps 5+3 does not permit 8 units
when the rigid atoms each contain 2. Run the small complete fixture through A and B separately
under sufficient deterministic allowance, and then through final selection from isolated copies.

Required variants:

- methodA and methodB: each completed small fixture reaches the six-unit optimum and keeps
  the other method's input unchanged. Final selected totals are 6, never the sum of candidates.
- copiedPatternRepair: inject a four-unit/two-atom pattern copied onto both slots. Its geometry
  and weight are valid but S-small exceeds cap 3. Reject that slot's candidate before commitment;
  a repair may remove one whole T2 atom and revalidate to the six-unit plan. Restore its two
  source units exactly once. Clear stale validation/proof/fullness and rederive evidence on
  the repaired contents; never split the atom or silently change the slot's cap.
- stoppedRepair: stop after the invalid copied candidate is mutated but before revalidation.
  Return only a separately checked earlier incumbent, or no result if none exists. The stale
  eight-unit candidate never commits. Provisional source and reservation changes roll back.
- smallCapFour: from a fresh snapshot set S-small cap 4. Exact maximum becomes 8 units, four
  on each slot, leaving 8. A stale cap 3 decision must not deny the additional whole atom.
- slotOrder: reverse slot enumeration and source order. The same actual slot IDs still bind
  caps 5/3 and the service optimum remains 6; an array position cannot transfer a capability.

The heterogeneous capabilities are injected at the supported direct context seam. This does
not assert that the current external M4 reservation feed can supply per-slot caps. The current
global configured cap is replicated onto actual slots in ordinary integration.

Oracle: whole-atom capacity bounds plus exhaustive assignments of eight atoms to two slots
or leftover, source/claim ledger, independent physical witnesses. References: CL-07/10,
ED-20, INV-01/05/09/10/17/18/21/24, EC-11/30/43/44/46.

### X-STATE-11 Cap changes invalidate feasibility and proof while preserving legal geometry reuse

Priority: P0. Owner M-PHY with M-POL/M-VAL/M-COM; WP3, precommit variant after WP7.
DIRECT pattern-cache and validation-context seams.

Use six T2 atoms under FX-CAP-ATOM: twelve unit loads, one slot. Keep the same profile ID,
equipment ID, product master, physical dimensions and mass limit. Run cap 7, cap 5, cap 7 with
fresh inventory/reservation snapshots but a shared process and warm caches. Update the effective
configuration/capability revision each time. The exact service optima are 6/4/6, with leftovers
6/8/6: three, two, three whole atoms. Each result is compared with its cold-run control.

Required variants:

- capABA: completed runs return 6/4/6. A geometry cache hit is permitted, but cap-aware pattern
  maxima, final physical checks, validation keys and proof-model identities use the current cap.
  The six-unit cap 7 candidate fails cap 5 even though its coordinates, mass and height still fit.
- sameIdentityRevision: apply that sequence by revising the settings behind the same named
  profile/equipment. Neither stable names nor unchanged dimensions hide the effective change.
- stalePrecommit: prepare and validate the six-unit candidate under cap 7; before an uncommitted
  operation is applied, present current capability cap 5/revisionB. The old operation/context is
  rejected without consuming any of the twelve units or the free slot. If planning continues,
  it uses an explicitly new context and revalidated four-unit candidate. It cannot simply
  attach the old proof or validation token to the old six-unit cargo.

An idempotent read of a result already committed under an earlier valid context is not a new
dispatch. This test concerns uncommitted work under a changed effective capability.

Oracle: 2*floor(cap/2), source and slot ledger, independently tracked context revisions, cached
versus cold outcomes. References: ED-20, CL-10, INV-01/09/10/13/17/21/24, EC-30/40/43/44.

### X-CG-21 A unit-load relaxation is a bound, not an all-goods packing certificate

Priority: P0. Owner M-OPT with M-PHY/M-ORC/M-VAL; WP2, physical and escalation controls
runnable after WP7. CORE resource model plus DIRECT physical/FFD evidence seam.

Use seven T2 atoms under FX-CAP-ATOM: fourteen source unit loads, four identical slots,
each cap 5. All required goods must ship in this explicitly declared minimum-truck model.
Floor, mass, height and all other constraints are slack. Since no rigid two-unit atom can
be split, a slot holds at most floor(5/2)=2 atoms. The exact optimum is four trucks, with
atom counts 2/2/2/1 (unit-load counts 4/4/4/2); a permutation is equally valid. All 14 units ship.
Independent enumeration of at most 4^7 atom-to-slot assignments is sufficient for the oracle.

Required variants:

- exactCapacity: find the valid four-truck all-goods result under the complete small-fixture
  allowance. The atom-count bound ceil(7/2)=4 certifies this count optimum.
- weakBoundTrigger: supply only the valid relaxation B=ceil(14/5)=3 and a checked FFD result
  F=4 to the Q6 seam, with positive remaining allowance. F>B triggers careful count work.
  B=3 remains a legitimate weak bound; it is not a feasible three-truck result and is not a
  proof that a checked four-truck solution is suboptimal.
- matchingBoundStop: provide F=4 and the independent same-model integer bound B=4. Stop
  further count escalation. This is an all-required truck-count certificate, not a certificate
  of cross-truck fill, a different cap/profile, or a partial-service objective.
- invalidQuickCandidate: inject three trucks containing 2/2/3 atoms (4/4/6 units). Although
  floor, height and weight fit and all goods reconcile, the six-unit truck violates cap 5.
  Do not use F=3 as a feasible quick upper bound, dispatch it, or combine it with B=3 into
  a zero-gap proof. A one-truck cap-ignorant candidate is rejected for the same reason.
- threeSlots: supply only three cap 5 slots. The all-required model is proven infeasible
  by the four-truck lower bound; this does not mean every atom is physically impossible.
  In a separately labelled maximum-service variant, exactly six atoms/twelve units fit,
  two units remain, and all three slots are valid. Do not transfer its count 3 into a proof
  of a fourteen-unit all-required plan.

Relaxing or omitting the cap in a lower-bound model can produce a valid weaker bound. Omitting
it from a purported feasible incumbent cannot produce a valid physical plan. Preserve this
distinction rather than rejecting every weak relaxation.

Oracle: independent whole-atom arithmetic/enumeration, physical witnesses and checked ledger.
References: CL-04/07, ED-20, INV-01/03/09/10/18/19/20, EC-21/43/46.

### X-PAT-18 Floor search uncertainty is distinct from a checked witness and a proof of refusal

Priority: P0. Owner M-PHY with M-OPT/M-VAL; WP3, selection propagation after WP4.
DIRECT adapter/evidence seam; bounded-search statuses are deliberately injected. This is not
a substitute for running the actual Tom row-grouping probes.

Supply eleven rigid, nonstackable TLD atoms U0..U10. Each is one unit load, 52 x 100 x 50 in,
1,000 lb final gross, one unique source. One group/P0/dateD, all availableD, carry/pull OFF,
acceptance OFF, one 630 x 100 x 108 /45,000-lb slot with cap 64. The explicit objective is
maximum shipped unit loads. Witness W11 puts Ui at (52*i, 0) in orientation 52x100: extent 572,
weight 11,000; all eleven fit. W10 for U0..U9 has extent 520/weight 10,000. Independently verify
both witnesses as rectangles and source draws.

Required variants:

- undecidedWithIncumbent: keep checked W10, then inject Undecided on the potentially better
  eleven-atom search because its configured state allowance is exhausted before a witness
  is returned. A stop may retain W10 as ten shipped/one left, Unproven for maximum service,
  with the exhausted limit named. It must not report DoesNotFit, ProvenInfeasible or a proof
  that ten is best merely from the failed search. No stale proof survives a boolean adapter.
- undecidedNoIncumbent: the same exhaustion before any checked incumbent yields no result,
  all eleven units retained and no slot consumed. Unknown is not physical impossibility.
- witnessCertificate: independently validate W11 without requiring exhaustive enumeration
  of alternative row groupings. Feasibility is established by this legal witness. A separate
  upper bound of 11, from the entire available demand for this declared maximum-service
  objective, certifies service optimality if the solver reports it. That certificate is
  valid even if an unrelated floor search was incomplete; it certifies neither best rear
  space nor an unresolved complete-allocation tie.
- provedNoFitControl: use thirteen such atoms for this candidate check. Their area is
  13*52*100=67,600 sq in >63,000, so a complete independent area certificate proves this
  thirteen-atom load cannot fit. A twelve-atom witness uses 624 in/12,000 lb and does fit;
  do not turn the thirteen-atom refusal into a whole-lane no-service claim. If solving
  this complete one-slot maximum-service control, the exact best is 12, with one left.
- invalidSuppliedWitness: label the eleven-atom result Fits/proven but move U10 to U0's
  coordinates, keeping all IDs and the claimed extent 572. Pure witness validation rejects
  the overlap and stale extent; it may not silently arrange a replacement layout and call
  the supplied witness valid. No invalid candidate commits.

A known valid witness proves feasibility. A complete negative certificate proves refusal.
An interrupted or heuristic refusal proves neither. The search adapter carries these states
and limits explicitly, while pure supplied-witness validation is a separate deterministic
operation. References: ED-19, INV-01/03/09/10/19/20/24, EC-42/45.

### U-PF-09 Shared floor-state limits, exact exhaustion and retryable Unknown

Priority: P0. Owner M-ORC with M-PHY/M-VAL; WP7. Bounded-search adapter with an instrumented
deterministic backend; no reliance on machine speed or the production search's state order.

Use three candidate sets P/Q/R, each eleven one-unit nonstackable TLD atoms, height 50 in and
weight 1,000 lb each. Longitudinal lengths are 52/53/54 in respectively, width 100; all have
unique source IDs, same group/P0/dateD, acceptance OFF and a standard slot with cap 64. Their
independent consecutive-row witnesses use 572/583/594 in and weigh 11,000 lb. These
candidate-search checks do not commit cargo. The synthetic backend returns a valid witness
after exactly 4 admitted states for P,2 for Q,1 for R; before that it returns Undecided.

Declare one shared physical-search route/context budget of 5 states and per-call maximum 3.
The production adapter must record the exact route/context scope; wrappers/method retries
within that scope do not allocate another route budget. The ordinary lane-date wall deadline
continues to apply in addition. This synthetic accounting fixture does not raise the real
200,000/1,000,000 settings or infer an undocumented external route identity.

Required variants:

- sequentialLimits: call P, Q, R in that order. P spends 3 and returns Undecided with per-call
  limit; route remaining 2. Q spends 2 and returns Fits on its second admitted state; remaining 0.
  R receives no state permit and returns Undecided without an unbounded backend search.
  Aggregate charged states=5. Completing Q exactly on the last permit does not erase its
  valid witness. The report distinguishes call-limit and route-limit stops.
- exhaustedRetry: retry P again in the same exhausted context before creating a new one.
  It cannot reset the per-route counter, pass a null/unbounded budget or cache Unknown as
  DoesNotFit. No state is charged or admitted beyond the five already used.
- freshContextRetry: create an explicitly new route/context with per-call 6/route 6, preserving
  the process cache. P completes after 4 states and returns Fits. An earlier Unknown is not a
  permanent negative cache entry; reusing already verified compatible geometry is also valid,
  in which case report a cache hit and the actual lower charged work instead of inventing 4.
- parallelAccounting: two uncached four-state jobs compete for a shared route budget 5,
  each with per-call 3. Barrier-controlled interleavings never admit more than 5 states total
  or 3 per call, never underflow, and neither four-state job is falsely marked complete.
  Do not require a particular division of the five permits across threads.
- wallDeadlineWins: fresh per-call 6/route 6, one four-state job, explicit fake wall deadline 3
  seconds; each admitted state advances wall time by 1 second and the backend cannot finish
  before state 4. Stop at 3 seconds with 3 states used and Undecided(wallDeadline), even though
  three route permits remain. A new method, repair or final search does not reset wall time.

Validate Q's already supplied witness without rerunning floor search after its state budget
reaches zero; this validation still consumes normal wall time. A cache hit may reduce work,
but it cannot transfer a stale capability/proof or convert incomplete search into a negative
certificate. State limits govern search, not whether an existing legal rectangle witness is
allowed to be checked.

Oracle: independent permit ledger and fake-clock trace plus rectangle/source validation;
fresh identities for independent contexts. References: CL-05, ED-19, INV-03/10/13/19/21/23/24,
EC-38/42/45. All application statuses remain NOT_RUN until implemented and executed.

---

## 37. Changes from v8 to v9

Reviewed 14 November 2026. All seven v8 / revision-5 files were reviewed together. No application
build, original Tom probe rerun or M5 execution was performed. The f9d9945 baseline and all original
regression IDs remain; the supplied M4 measurements remain REPORTED.

- Fixed active R-5a missing-input wording; original package availability is recorded separately from
  its still-required committed bytes/checksums at execution. R-1a/b exact inputs remain outstanding.
- Made deferred R-7 M4/M5 per-lane integration conditional and explicitly unselected for current M5;
  run-wide controls and direct profile isolation remain required. X-RUN-09 gains applicability gates.
- Separated Fits/DoesNotFit/Undecided search from pure supplied-witness validation, negative proof and
  objective optimality. Reference layout lengths are not unique-layout acceptance requirements.
- Carried explicit per-search/shared route floor-state accounting into the physical API and work
  packages; no method/repair/probe reset or null-budget bypass. The wall deadline still applies.
- Required component unit-load counts in cap-aware packing classes, columns, copies, actual-slot
  checks and cache/model identities; preserved indivisible atoms and valid weaker bound semantics.
- Removed R-9's unrelated production per-truck size/weight-input dependency from global/direct tests.
- Added six named cases (Section 36) and EC-44..46; expanded RD-13 with the two REPORTED Aug5 linked-date
  decline diagnostics. Those diagnoses do not close the underlying application regressions.
- Synchronized revision-6 architecture/handover, schema-2 manifest, traceability, review and document
  validation. GCC warning-exception and baseline performance risks remain explicit.

| Family | Named tests in v9 |
|---|---:|
| ACC | 3 |
| R | 15 |
| U | 108 |
| RD | 13 |
| X | 157 |
| PUB | 3 |
| Total | 299 |

All 293 v8 IDs remain. New IDs: X-PAT-16, X-PAT-17, X-PAT-18, X-STATE-11, X-CG-21, U-PF-09.
There are 24 invariants, 46 EC checklist entries and 10 CL rules. Parameterized variants and generated
inputs are separate from this named-test count. Every application result remains NOT_RUN/BLOCKED
until the corresponding implementation, input and assertions are actually exercised.

---

## 38. Additional test from Tom's 14 November answers

### X-RES-13 Equal-service ties rotate between groups (ED-21)

Priority: P0. Owner M-ALC with M-ORC/M-VAL; WP5. DIRECT allocation seam. Synthetic.

Fixture FX-TIE uses the FX-CAP-ATOM T2 atom (Section 36): 48 x 36 x 90 in, 600 lb, two unit loads,
already legally stacked upstream. Do-not-mix groups G1, G2 and, where stated, G3. Each group has three
T2 atoms (six unit loads) on its own source line, all P0, all due and available on the run date. Carry
and pull-forward OFF, minimum-load acceptance OFF. One reserved slot per lane-date with the standard
630 x 100 x 108 in, 45,000 lb trailer and cap 6. M5 is invoked directly, without the >20 routing
threshold. Each run uses fresh source and reservation snapshots with the stated lane and ship date.

Every complete allocation ships one group's three atoms (six unit loads) on the one truck, so the
urgency-class vectors of the g single-group plans are equal; a plan shipping fewer atoms loses on
urgency first. The other groups' goods are left over with a reason (no reservation left).

Lane keys (FNV-1a, Section 0.8) and day numbers used below:

| Lane text | laneKey | mod 2 | mod 3 |
|---|---:|---:|---:|
| 9001\|9004\|TL (lane LA) | 3274617072 | 0 | 0 |
| 9001\|9003\|TL (lane LB) | 788187159 | 1 | 0 |
| 9001\|9002\|TL (lane LC) | 1943471446 | 0 | 1 |

6 Aug 2028 = day 21402 (mod 2 = 0, mod 3 = 0); 7 Aug 2028 = 21403; 8 Aug 2028 = 21404.

Required variants (lane LA unless stated):

- twoGroupsAlternate: G1 and G2. Separate runs dated 6 Aug 2028 and 7 Aug 2028. 6 Aug ships G1
  (r = 0); 7 Aug ships G2 (r = 1). Each run leaves the other group's six unit loads.
- threeGroupsCycle: G1, G2 and G3. Separate runs dated 6, 7 and 8 Aug 2028 ship G1, G2 and G3.
  Each group wins exactly once over the three dates.
- shuffledInput: repeat twoGroupsAlternate with source rows and group listing reversed. Same winners.
- copiedLane: the 6 Aug two-group goods on lane LA and an identical copy on lane LB, in one run,
  fully separate. LA ships G1 (r = 0); LB ships G2 (r = 1). Six unit loads ship on each lane, totals
  double, nothing is shared.
- acrossLanes: the 6 Aug two-group goods on lanes LA, LB and LC in one run. Winners G1, G2 and G1.
  The winner varies between lanes on the same date.
- repeatRun: run the 6 Aug two-group case on LA twice in one process (warm caches) and once cold.
  G1 ships every time.
- nonTieControl: lane LA, 6 Aug 2028, but G1's goods are P11 and G2's are P0. G2 ships, because
  urgency decides before ED-21; the rotation that would favour G1 never applies.
- canonicalWithinGroup: G1 only, with two source lines L1 and L2 (L1 has the lower stable ID), each
  three T2 atoms, same class. The truck ships L1's three atoms (T2). Reversing the input order of L1
  and L2 gives the same result.
- renamedGroups: twoGroupsAlternate on LA, 6 Aug 2028, with G1 renamed G9, so the sorted order is
  G2, G9. G2 ships. This asserts the documented ID-based behaviour, not renaming invariance.

Every run conserves all source units by line and group, uses at most one truck per lane-date and
never mixes groups.

Expected summary: ED-21 decides equal-service ties: a group order rotated by (laneKey mod g + dayNumber mod g) mod g, then canonical records. On lane LA two groups alternate on 6 and 7 Aug 2028 and three groups win once each over 6-8 Aug 2028; lanes LA, LB and LC give G1, G2 and G1 on 6 Aug; shuffled input and repeated runs give the same winner; urgency decides before ED-21; within one group the lower stable source-line ID ships; renaming groups follows the documented ID order.

Oracle: independent enumeration of every whole-atom plan (at most 9 atoms, one slot per lane-date),
an independent FNV-1a and day-number calculation and a source ledger. References: CL-02, CL-09,
ED-21, INV-01/05/06/11/12/17/18, EC-47. All application statuses start NOT_RUN.
---

## 39. Changes from v9 to v10

Updated 14 November 2026 for Tom's reply to Saif's M5 progress note. No application build or M5
execution was performed. All v9 IDs, variant keys and expected results are kept.

- OPEN-COMP resolved. Tom: "any tiebreaker would work as long as it didn't always push to the same
  single side". The rule is ED-21 (Section 0.8): a group order rotated by ship date, then canonical
  records. CL-02 now points to it. A fixed canonical order alone was rejected because it would always
  give ties to the same group.
- New test X-RES-13 (Section 38) and checklist row EC-47.
- C9 recorded. Tom accepts that a lane-date whose reserved trucks have different trailers is refused,
  not guessed. He normally expects a truck-type list (for example 2 of A, 5 of B and 1 of C) from
  which the algorithm builds the mix. That list is not in the current inputs or specification, so it
  is out of scope for M5 (Section 8). U-DA-05 inputFeed stays conditional.
- R-1a/b: Tom must wait for his engineer. R-5a, R-8 and R-9 already have his e6dbb0f files.

| Family | Named tests in v10 |
|---|---:|
| ACC | 3 |
| R | 15 |
| U | 108 |
| RD | 13 |
| X | 158 |
| PUB | 3 |
| Total | 300 |

All 299 v9 IDs remain. New ID: X-RES-13. There are 24 invariants, 47 EC checklist entries and 10 CL
rules. Every application result remains NOT_RUN/BLOCKED until it is actually exercised.

---

## 40. Tom's 16 November package and the container test

Tom's engineer sent the exact synthetic inputs for R-1a, R-1b and R-5a on 16 November 2026, together
with his original probes and replay results. They are review cases, not customer shipments. Commit
the files unchanged under `tests/fixtures/tomReview/m5ExactInputs/` (Saif commits; synthetic data) and
check these SHA-256 values before every run:

| File | SHA-256 |
|---|---|
| R-1a.json | fc58a9a4c1912b2d88b9a7cf28ee1bdaf5f0a1b1bcea3be2b72e65b47effd052 |
| R-1b.json | 40958f86d06dc40ca6d2e3b792b20c12b8bad2ac00ef551d70cc2f01470c0b8e |
| R-5a.json | 2534cf00edaa5980e71f93806f0971ce1d3d4aebed8ac1ceab53579bbe6a2d32 |
| exactness-counterexample.json | f6dd74e61f6ae42bd041cf3527a89820898ed79dc0489d6b16f32cba4ebcf917 |
| monotonicity-standard-results.json | 95a55f69289e1569d0be403faaf33cd9eee494df91bf0cbe1d4e57d1e254770a |
| original_fourteen_stack_probe.cpp | da803f590d7ad9138f1cb1279ff582d2d5809541a53f7917baa6c047126388fc |
| original_review_probe.cpp | 7ac2c4fa01dafe14af56df8ab37e7a0eb19614df796b37170b3ad462a0551fa8 |
| run_requested_cases.cpp | 6111672b84b1d32d8ae47b6aacc56242cecc7bea4d482c2acbe4bf3aca91e201 |
| results.json | 9f1f0fd3ca90621ff3f004de383f8f21b542c1c56f65072fd280d86e5bb9c5f6 |
| README.txt | c410cdd594fe366d33275fd8461501dd0d429eda239e509845b2274d2b0d8c2b |
| Mahib-M5-exact-review-inputs.zip (whole package) | c38a9a6c8199141bc3d602eff4af0fd232b2dcda110c35fca7e311d521df2f01 |

- All items are fixed prebuilt stacks: one floor item each, no restacking, floor rotation allowed under
  the existing row patterns. All P0, ship date 6 Aug 2026, one reserved truck.
- Tom's replay at d028f24 (results.json): R-1a 25 in 600 in, R-1b 2 in 50 in, R-5a 23 in 624 in, all
  proven. `run_requested_cases.cpp` reads the three JSON files and runs the library; its README has the
  build command. It is Tom's reference check, not a replacement for the R-1a/R-1b/R-5a tests.
- R-1b uses a 100 x 100 in floor. The test must set that trailer for the run.
- R-5a: the 617 in layout is a witness; another valid 23-unit set is acceptable.

Tom's engineer also asked, for M5 acceptance: publish the M5 branch or PR and the exact test output
after these cases are added, and demonstrate the 38-truck reference with its agreed time allowance.
The handover lists what that delivery contains.

### U-DA-07 Single alternate trailer type: 40 ft high-cube container (Tom, 16 Nov)

Priority: P1. Owner M-INT with M-PHY/M-VAL; WP8. ROUTE + DIRECT. Synthetic goods.

Tom suggests testing a 40 ft high-cube container as the run's only trailer type. Inside sizes as sent:
length 12,032 mm, width 2,350 to 2,352 mm, height 2,698 to 2,700 mm. Use the smaller value of each
range and 1 in = 25.4 mm exactly: 473.700787 x 92.519685 x 106.220472 in (floor 304.35 sq ft, cube
2,694.03 cu ft). Payload and unit-load limit were not sent. One trailer type per run is supported
today; mixing this container with 53F trucks on one lane-date stays refused (C9, U-DA-05).

Required variants:

- mmConversion: the profile converts with the exact factor and keeps the smaller value of each range.
  Floor, height and cube checks use 473.700787 x 92.519685 x 106.220472 in, never the 53F defaults
  and never rounded-up values.
- rowPatterns: with 48 x 40 in stacks the row patterns are derived from the 92.52 in width: 40 + 40 =
  80 in across fits, 48 + 40 = 88 in fits, 48 + 48 = 96 in does not. No row wider than 92.52 in appears.
- missingLimitsRejected: a container profile without payload or without unit-load limit is rejected at
  configuration time with a named error; it never takes 45,000 lb or 64 silently.
- containerRun (BLOCKED until Tom gives the payload and unit-load limit): one run with the container as
  the only trailer type on FX-30 goods. Fill, rear inches and fullness labels use the container's
  length, width and height.

Expected summary: a 40 ft high-cube container runs as the single trailer type with its exact inside
sizes, row patterns follow its width, and missing payload or unit-load limit is an error, never a default.

Oracle: exact arithmetic, independent row-width check and configuration-error check. References:
INV-01/15/18/23/24, EC-48.

---

## 41. Changes from v10 to v11

Updated 17 November 2026 for Tom's 16 Nov email. No application build or M5 execution was performed.
All v10 IDs and variant keys are kept.

- R-1a and R-1b are no longer waiting: their exact inputs arrived (Section 40). Both variants of each
  are NOT_RUN instead of BLOCKED. R-1b uses a 100 x 100 in floor.
- R-5a also points to Tom's 16 Nov copy; the 617 in layout is a witness, not a required layout.
- ED-21 now rotates by lane and date, as Tom's engineer and Saif described it. The lane key is the
  FNV-1a hash of "origin|destination|mode". X-RES-13 gains the acrossLanes variant; its copiedLane
  expectation now follows each lane's key. X-META-06 allows a different winning group in a tie.
  EC-47 updated.
- New test U-DA-07 (Section 40): a 40 ft high-cube container as the single trailer type. Three
  variants run now; containerRun is BLOCKED until Tom gives payload and unit-load limit. New EC-48.
- C9 record extended with the engineer's conditions for a future mixed fleet.

| Family | Named tests in v11 |
|---|---:|
| ACC | 3 |
| R | 15 |
| U | 109 |
| RD | 13 |
| X | 158 |
| PUB | 3 |
| Total | 301 |

There are 24 invariants, 48 EC checklist entries and 10 CL rules. Every application result remains
NOT_RUN or BLOCKED until it is actually exercised.

---

## 42. v12 case-by-case reconciliation against v11 (17 November 2026)

This is a targeted follow-up to the previous provisional 162-case P12 review. The authoritative base is v11, with 301 stable named IDs and 522 variant keys. The mapping and importer-hardening register are included in `m5Phase1V11Reconciliation.md` and `m5Phase1V11Reconciliation.json`. No v11 definition or variant has been removed or renumbered.

Classification: 104 ALREADY_COVERED, 33 ADD_VARIANT, 3 ADD_NEW_TEST and 22 OUT_OF_SCOPE. The out-of-scope group includes 15 importer/config-loader cases, 3 crash/restart cases, 2 future mixed-fleet cases, one extreme materialization case and one deferred per-lane behavior.

**Scope corrections:** three-across generation uses M4's literal `side <= 33.3 in`, not the mathematical 100/3 cutoff; a failed candidate-generation search cannot by itself prove global geometric infeasibility. M5 is a batch planner with no durable recovery store. M1/M4 import parsing is not a new M5 acceptance obligation. P0 is reserved for cargo count, legality, proof honesty, urgency and tie correctness; robustness is P1; optional high-scale stress is P2.

**Status:** New entries are test specifications, not code. Every newly added variant starts NOT_RUN. The required original Tom fixtures and archived original probes in `tomInputs/` are unchanged.

### 42.1 New named test definitions

### X-PAT-19 Literal M4 three-across 33.3-in row generation boundary

Priority: P0. Owner M-PHY, WP3; entry CORE + VALID. Provisional P12 source P12-B02. Required variant `threeAcrossM4Cutoff`.

**Fixture:** Row-generator control: one row of three unrotated rigid 33.30 x 20 in footprints (all 80 in high, 100 lb, fixed) in width 100. Repeat with short sides one supported input step above 33.30 in (33.31 in at 0.01-in normalization, or the repository's documented normalized precision). All other limits unbound and no other atoms.

**Expected result:** At 33.30 in, a three-across candidate is generated (3*33.30 = 99.90 <= 100). At one supported input step above 33.30 in (33.31 in by default), the literal M4 test side <= 33.3 excludes that three-across candidate. With the 0.01-in step the summed width is still 99.93 <= 100, so absence of the pattern is NOT a proof that no rectangular layout can exist. If normalized precision differs, recompute the geometric sum and keep candidate-generation status separate from a physical infeasibility proof. A supplied-witness check remains independent.

**Independent oracle:** DIRECT row-candidate enumeration + exact decimal width arithmetic and isolated witness validation.

All application evidence starts NOT_RUN. Do not infer source code or a certificate from these textual cases.

### X-CG-22 Zero-reduced-cost duplicates and finite pricing completion

Priority: P0. Owner M-OPT, WP2; entry CORE. Provisional P12 source P12-G03. Required variant `duplicateZeroCostColumns`.

**Fixture:** 1-D cutting stock capacity 10; required items two size-5 and five size-2. Pricing callback can return the already present zero-reduced-cost columns (2 size-5) and (5 size-2) repeatedly; LP dual values 0.5 and 0.2. Inject fixed work counter.

**Expected result:** Zero-reduced-cost duplicate columns do not cause an infinite pricing loop or extra demand. A validated two-bin integer plan [5,5] and [2,2,2,2,2] is optimal by total size 20 and lower bound 2. If the integer model is not completely solved, no global optimum claim is allowed.

**Independent oracle:** Independent exhaustive 1-D bin enumerator, dual reduced-cost check, column identity dedup and work counter.

A second required variant, `nearZeroReducedCost`, uses the declared pricing-tolerance control: Use the X-CG-22 scalar model and an injected pricing callback returning candidate reduced costs -1e-12, 0, +1e-12 around the configured solver tolerance; frozen input/config. Expected: Accepted versus treated-as-zero costs follow the recorded numeric tolerance. No extra copied demand, no unbounded repeated equivalent columns, and no global certificate from a tolerance rule alone. Record tolerance and proof scope explicitly. Oracle: Independent high-precision reduced-cost arithmetic, bounded iteration counts and certificate checker.

All application evidence starts NOT_RUN. Do not infer source code or a certificate from these textual cases.

### X-REB-10 Repair oscillation stops and preserves a checked incumbent

Priority: P0. Owner M-REP, WP4; entry DIRECT + VALID. Provisional P12 source P12-G14. Required variant `oscillatingRepairCycle`.

**Fixture:** Two checked FX-30 load snapshots on one group/date, 50 source units and two reservations, with feasible distributions 30+20 and 29+21. Inject repair policy that alternates those two candidate arrangements without increasing the recorded service comparator; repair iteration cap 3. Acceptance OFF.

**Expected result:** Repair terminates at its cap or repeated-state detection, returns only one fully validated incumbent, ships 50 source units once, consumes two slots, never publishes an intermediate mutation or a global proof without a certificate. Report explicit stop/no-progress reason.

**Independent oracle:** Independent per-source conservation, rectangle/weight checker, fake repair state sequence, and checked-candidate revision log.

All application evidence starts NOT_RUN. Do not infer source code or a certificate from these textual cases.

### 42.2 Additional variants on existing named tests

These are new `(testId, variant)` identities, not new named tests. Each row gives the complete synthetic test obligation; the parent v11 definition remains applicable.

**X-PAT-13 / twoAcross100** (P12-B03, P0):

- Fixture: A direct row check in a width-100 trailer, two nonstackable rigid rectangles with across widths exactly 50.00 and 50.00 in; then increase the first width by one supported input step (50.01 in at 0.01-in normalization, or the repository's documented normalized precision). Hold orientation fixed. Height 80 in, weight 100 lb each.
- Expected: The first declared orientation sums to exactly 100.00 in and fits; increasing the first width by one supported input step makes that row wider than 100 (100.01 in for a 0.01-in step), so the declared row fails. Do not claim the complete floor cannot fit after a different legal orientation.
- Independent oracle: Exact rational row-width check and independent orientation search.

**U-PB-04 / rotationOnly** (P12-B04, P0):

- Fixture: Rigid atom floor footprint 80 x 55 in, in a 60 x 100 in trailer floor, height 80 in, weight 100 lb. Floor rotation is allowed.
- Expected: Original 80-length orientation cannot fit length 60; turned footprint 55-length x 80-width does fit. A solver that never considers allowed rotation fails.
- Independent oracle: Enumerate both exact rectangle orientations, check one witness.

**U-PB-08 / payloadExactAndEpsilon** (P12-C01, P0):

- Fixture: One nonstackable item of 20 x 100 x 90 in, mass exactly 45000.00 lb in a 630 x 100 x 108 in/45000-lb truck; repeat at one supported input step above the payload limit (45000.01 lb at 0.01-lb normalization, or the repository's documented normalized precision). No other cargo.
- Expected: At payload equality the truck is legal; at one supported input step above the limit it is illegal. FULL labels have no authority to relax the physical limit. Use full normalized input precision at the typed physical seam (0.01 lb unless the repository documents another step).
- Independent oracle: Full-precision independent mass comparison.

**X-PAT-16 / singleAtomOverCap** (P12-C06, P0):

- Fixture: One indivisible prebuilt atom 48 x 36 x 90 in, weight 600 lb, 6 source unit loads; one slot with cap 5 and ample other limits. Control cap 6.
- Expected: At cap 5 the entire atom is left, no 5-of-6 split; at cap 6 the atom may ship intact. Count and source conservation are exact.
- Independent oracle: Integer whole-atom subset enumerator and per-source ledger.

**U-PB-08 / simultaneousBinding** (P12-C07, P0):

- Fixture: A rigid nonstackable atom 630 x 100 x 80 in, gross 45000 lb, one source unit load. Trailer floor 630 x 100, height 108, payload 45000. Attempt adding a second 1 x 1 x 80 in, 1 lb atom.
- Expected: First atom exactly binds floor coverage and payload. It fits alone; no supported disjoint floor position or payload remains for the second. Candidate claiming to ship both is rejected independently of cube/fullness.
- Independent oracle: Rectangle non-overlap/inside-trailer witness checker and exact payload arithmetic.

**U-DA-05 / mixedRunRejected** (P12-C09, P0):

- Fixture: At normal run adapter, availability advertises a single lane-date with one slot of equipment A and one slot of equipment B with different dimensions; no supported typed-slot selection feed is supplied.
- Expected: Return explicit unsupported mixed-equipment status, no guessed 53F homogeneous default and no inventory/reservation commits. Do not prevent direct physical adapter tests with explicit independent slot capabilities.
- Independent oracle: Input-to-M5 integration status and unchanged state oracle; current C9 scope.

**X-PAT-13 / microLengthBoundary** (P12-C12, P0):

- Fixture: A single fixed rectangle floor length exactly 630.00 in, width 100 in, height 80 in, 100 lb in standard trailer. Control 630.01 in; floor rotation would exceed width 100.
- Expected: Exact length fits, 630.01 does not. No report-rounding or floor-search grain may turn the excess into a legal witness. Use input-supported 0.01-in test precision (not an assumed micron parser).
- Independent oracle: Exact normalized length comparisons; supplied-witness validation.

**U-RS-09 / zeroDayWindow** (P12-D10, P0):

- Fixture: One date D and a can-go demand source originally due D+1 (available D), pull-forward ON but maxDaysPulledForward=0. One existing D reservation, no required goods.
- Expected: Future D+1 demand does not ship on D under zero-day window, remains available for its actual date. A due-D control may ship. No date is invented.
- Independent oracle: Exact date arithmetic and source/date ledger.

**X-DATE-07 / reversedInputDates** (P12-D15, P0):

- Fixture: Two one-slot lane-dates D and D+1, slot cap 15, same group. Twenty individually traceable unit loads due D+1 and available D; pull forward ON, ten-day window, no other required goods. Compare input records in normal and reversed source/date-row order, using the same chronological planner policy.
- Expected: Both row permutations produce the same selected original-source allocations and deltas under that deterministic policy. At most 15 ship on either date, at most 20 total, no duplicate carry/pull source claim. Do not change the algorithm to plan dates in reverse chronological order.
- Independent oracle: Independent source ledger and chosen plan canonicalization; exact date-window check.

**U-RT-01 / plannerAbsentFallback** (P12-E03, P0):

- Fixture: A lane with 21 reserved trucks and valid required goods reaches the M4 selector while no M5 LargeLanePlanner has been injected. Compare with the exact known M4 behavior at the accepted commit.
- Expected: The normal M4 fallback behavior, including its previously reported escalation/no-plan result, source status, and absence of commits, remains unchanged. No recursive M5 invocation and no fabricated successful plan.
- Independent oracle: Read-only exact M4 baseline replay and source/result fingerprint.

**U-RB-01 / thirtyNineBlocks** (P12-E05, P0):

- Fixture: Thirty-nine FX-BLOCK rigid atoms, each exactly fills one reserved trailer; 39 reservations and block size 19, one group/priority/date.
- Expected: Method B processes accepted nonempty blocks 19 + 19 + 1, never invents a 40th load or pads with a phantom item; all 39 atoms ship once. The final block consumes one valid reservation.
- Independent oracle: Count-only class oracle and source/reservation ledger.

**U-DA-07 / unknownTypeNoFallback** (P12-E10, P0):

- Fixture: Single-lane availability row refers to unknown equipment code UNKNOWN_TYPE (not in the capabilities catalogue); sufficient valid demand, 21 reservations. Keep all other fields legal.
- Expected: Reject the planning input with unknown-equipment reason before any candidate or commit; do not use 53F payload or cap as default.
- Independent oracle: Config catalogue lookup and unchanged-state oracle.

**U-RS-01 / hundredAndOneReserved** (P12-E11, P1):

- Fixture: One FX-30 source unit load and 101 equal homogeneous reservations on one date. No fake cargo is provided. Run direct or through supported routing.
- Expected: Exactly one used/nonempty truck and 100 unused reservations if no other cargo; no fabricated empty loads. Do not infer a numeric penalty when penalty rate is unknown.
- Independent oracle: Count and independent source/reservation ledger.

**X-STATE-04 / thresholdSnapshotInFlight** (P12-E12, P0):

- Fixture: Start a 21-reservation lane-date with routing threshold 20, freeze context and enter the M5 planner. While a fake-clock planner is paused, change live configuration threshold to 50; then start a fresh separate lane-date run.
- Expected: In-flight run stays routed via threshold 20 and validates against its frozen version. Fresh run with threshold 50 follows normal M4 routing. Neither run consumes the other snapshot; no cache contamination.
- Independent oracle: Injected snapshot/version context and isolated routing recorder.

**X-RES-13 / threadCountReplay** (P12-F03, P1):

- Fixture: X-RES-13 lane LA on 6 Aug 2028 (two tied G1/G2 groups, six unit loads each, one slot), repeated completed deterministic runs with thread counts 1, 2, 8.
- Expected: Always selects G1 because ED-21 r=0. Timing fields may differ; shipped source set, group and slot count do not. Budget-limited incomplete runs are not this fixture.
- Independent oracle: Independent unsigned FNV-1a/day calculation and canonical ledgers.

**X-RES-13 / duplicateCandidates** (P12-F05, P0):

- Fixture: X-RES-13 lane LA on 6 Aug 2028, but candidate generator offers the same feasible G1 plan three times and the G2 plan once; repeat with opposite multiplicities, fresh snapshots.
- Expected: G1 wins each complete tie as ED-21 dictates. Duplicate candidate multiplicity is irrelevant; neither reservations nor source counts multiply.
- Independent oracle: Deduplicated enumeration of complete allocations and independent ED-21 order.

**X-RES-13 / specialGroupIds** (P12-F08, P1):

- Fixture: Lane LA (9001|9004|TL) on 6 Aug 2028 with two tied groups whose stable IDs are A/B and A|B. Each has three T2 atoms as X-RES-13, one slot.
- Expected: Sort stable ASCII group IDs: A/B precedes A|B. With r=0, A/B wins six unit loads and the other group remains. Group delimiter characters are not interpreted as lane delimiters and neither group ID collides.
- Independent oracle: Bytewise ID comparison, independent FNV-1a/day and per-source ledger.

**X-RES-13 / fnvUnsignedOverflow** (P12-F09, P0):

- Fixture: X-RES-13 lane LA on 6 Aug 2028, three equal groups G1/G2/G3. Compute FNV-1a on ASCII 9001|9004|TL and day number 21402 using integer math. Compare implementations that incorrectly cast the hash to signed32.
- Expected: laneKey=3274617072 unsigned (greater than 2^31-1), mod 3=0; day mod 3=0; r=0 and G1 wins. Signed negative modulo must never change the rotation index or cause a negative array index.
- Independent oracle: Reference uint32 FNV-1a modulo 2^32 and calendar day-number calculation.

**X-RES-13 / ineligibleGroupIgnored** (P12-F11, P0):

- Fixture: Two eligible equal groups G1/G2 from X-RES-13 on 7 Aug 2028, plus G0 with records unavailable until D+1 and thus zero eligible units. G0 sorts first but is ineligible.
- Expected: Only G1/G2 count toward g=2; lane LA r=1 on 7 Aug, so G2 wins. G0 is untouched and cannot shift rotation to a different group.
- Independent oracle: Independent filtered eligible-group list and ED-21 arithmetic.

**X-STATE-10 / sameSlotTieConcurrent** (P12-F12, P0):

- Fixture: Same one-slot tied-lane allocation attempted simultaneously from two distinct candidate snapshots; use X-RES-13 LA 6 Aug, expected ED-21 winner G1.
- Expected: Only the selected G1 allocation can commit one slot and six source unit loads. Racing or stale competing G2 candidate is rejected/rebased; no duplicate or half-applied debit.
- Independent oracle: Atomic versioned state/ledger snapshot after concurrency barrier.

**U-FF-01 / ffdConcreteGap** (P12-G01, P0):

- Fixture: 1-D scalar capacity 10, required items [6,5,3,2,2,2], one urgency class, enough bins, no geometry or minimum-load policy. Apply the defined FFD order (nonincreasing sizes) and independently enumerate all partitions.
- Expected: FFD uses 3 bins: [6,3], [5,2,2], [2]. Two-bin packing exists: [6,2,2] and [5,3,2]; total=20 proves lower bound 2. Under FX-GAP G=1 and remaining allowance, F-B=1 triggers escalation. Do not claim escalation proves a full physical allocation optimum.
- Independent oracle: Independent exhaustive subset bin-packing and FFD simulator.

**X-STATE-02 / unselectedMethodProof** (P12-G16, P0):

- Fixture: Method A selected by complete urgency comparator ships 30 P0 units in valid loads with proofStatus=Unproven; Method B produces only 29 P0 plus one P11, with a correct certificate restricted to B model. Both candidates isolated.
- Expected: Select A on urgency/service. No proof or bound from losing B may be attached to A as a global certificate. Reporting and final commitment carry only the selected plan identity.
- Independent oracle: Independent class vector comparator, candidate revision/proof provenance checks.

**R-1c / invalidDonorRestore** (P12-G17, P0):

- Fixture: Start with a fully checked two-truck FX-30 incumbent 30+20. Inject a later donor-repair edit moving a unit that would invalidate the donor physical witness and stop immediately before revalidation.
- Expected: Discard the invalid provisional edit, keep the checked 30+20 result and unchanged source/reservation commitments. If no checked candidate exists, return no plan rather than an unchecked load.
- Independent oracle: Immutable incumbent snapshot, injected validation failure, independent layout/ledger check.

**X-META-10 / thousandStacksP2** (P12-J01, P2):

- Fixture: Optional P2 seeded stress: 1000 FX-CAP-ATOM items in a single independent lane-date. Declare finite wall/work budget and maximum resident memory observation, no new solver exactness guarantee.
- Expected: No overflow, leaked reservation, duplicated source or unbounded memory; completed solutions are validated, timeout outcomes are truthful. Resource observations are reported as measurements, not fabricated fixed thresholds.
- Independent oracle: Stable seed, per-source ledger, process resource sampler, stop reason and invariant checks.

**X-META-10 / fiftyNineFootprintsP2** (P12-J02, P2):

- Fixture: Optional P2 59 supported distinct stack footprints with deterministic seed, one lane-date and finite state/time limits; every dimension fits individually by a documented legal orientation.
- Expected: No crash or silent pattern truncation; every emitted truck has a checked supported-layout witness. Unknown/no-fit statuses and resource measurements are honest. No unsupported M6 all-pattern enumeration is demanded.
- Independent oracle: Seeded footprint generator plus independent supplied-witness validator.

**U-PF-07 / wallClockShift** (P12-J05, P0):

- Fixture: Freeze a 10-second monotonic fake deadline. Advance the monotonic clock by 3 seconds, then move the system wall clock +24h and -24h, then advance monotonic time by 7 seconds.
- Expected: The allowance expires after 10 monotonic seconds regardless of wall-clock jump; no bonus or premature cancel is credited to wall time. All unfinished work is correctly marked.
- Independent oracle: Injected dual-clock fake, monotonic duration ledger and stop status.

**X-RUN-10 / coldWarmCache** (P12-J09, P1):

- Fixture: Run identical frozen lane/date, same method and seed twice with cold caches and warm caches on the same build; collect independent raw wall and work metrics.
- Expected: All completed canonical decisions agree under deterministic rules; cold/warm raw durations reported separately, no cache-generated stale proof. Performance is observational; no fabricated fixed percent improvement.
- Independent oracle: Recorded input/config/cache/build identity, independent source ledger and budget monitor.

**X-RUN-09 / crossCompilerGate** (P12-J12, P1):

- Fixture: A fresh exact-head build matrix on MSVC VS2022 x64 Release with /W4 /WX /utf-8, GCC and Clang with -Wall -Wextra -Wpedantic -Werror; execute required synthetic M5 variants on each supported platform.
- Expected: All required build/test gates pass without weakening tracked warning flags. Missing compiler or skipped variants are recorded as blocked, not PASS. Compare canonical output and source ledgers where the contract guarantees repeatability.
- Independent oracle: Compiler/build logs with commit identity, test runner exit/results, per-variant evidence gate.

**X-DATA-13 / inconsistentSlotCapabilities** (P12-K03, P0):

- Fixture: A direct slot capability has a valid 630 x 100 x 108 trailer plus inconsistent physical facts: payload 0, cap 0, nonfinite width, or mismatch of declared equipment type and frozen capabilities. Run independently.
- Expected: Reject invalid or mismatched slot identities before any packing or commitment; no silent fallback to 53F or synthetic cap 64.
- Independent oracle: Independent typed capability validator and unmodified source/reservation snapshot.

**X-ROUTE-05 / malformedHandoffReservation** (P12-L11, P0):

- Fixture: Typed handoff contains one negative reserved count or invalid equipment identity alongside valid demand, then a separate exact duplicate reservation control. No CSV parsing is performed by M5.
- Expected: Invalid handoff fails or reports an explicit unplannable lane-date without shipping or committing. It must not quietly change available trucks to zero or suppress shortage reporting. Duplicate control remains idempotent.
- Independent oracle: Typed contract validator and source/reservation ledger; parsing belongs to importer.

**X-RUN-05 / adverseBlockOrderingP2** (P12-L13, P2):

- Fixture: Optional P2 seeded Method B stress with 100 reserved homogeneous slots and four 19-slot blocks plus remainder; urgent group appears last in shuffled source ordering, lower-priority goods can fill early slots. Finite budget.
- Expected: Regardless of input ordering, legal checked output never prioritizes a lower-urgency complete allocation over a feasible higher-urgency one. Interrupted results have honest proof/stop status. No exact global optimum claimed without same-objective oracle.
- Independent oracle: Analytical urgency upper bound for homogeneous full-slot atoms, independent ledger, seeded permutations and checked physical witness.

### 42.3 Importer hardening and excluded scope

The 15 importer/config-loader cases are retained in the separate reconciliation file and must be routed to the M1/M4 importer owner. They do not become M5 acceptance cases unless malformed values cross the typed handoff. Crash/restart recovery, durable state, future mixed-fleet composition and deferred per-lane past-due integration are not silently added to M5. Extreme physically materialized scale is excluded; count-only overflow remains covered by X-DATA-12.

The two P2 stress variants (`X-META-10 / thousandStacksP2`, `X-META-10 / fiftyNineFootprintsP2`) and the optional P2 adverse block variant (`X-RUN-05 / adverseBlockOrderingP2`) are recorded as conditional, unselected and NOT_RUN. They are not current acceptance gates.

### 42.4 Revision 11 to revision 12 and execution status

All 301 v11 IDs and 522 variant keys remain. Exactly 3 new named tests and 35 new unique variant keys were added (two on X-CG-22, one on each other new test, and 31 variants on existing IDs). The 33 ADD_VARIANT proposals map to 32 unique keys because P12-E10 and P12-K08 share U-DA-07/unknownTypeNoFallback. Every added variant is NOT_RUN. The full program test suite has not been compiled or run by this document review.

## 43. Repository notes (17 November 2026)

Recorded in the repository after v12. They change no test ID, variant or expected result; they fix the
terms the tests use where this specification leaves them to the repository.

- Supported input step (decision D3). "One supported input step" is 0.01 in for lengths and 0.01 lb for
  weights. Searches may use doubles. Every final legality check (length, width, height, weight, unit-load
  cap, and the rear inches of the FULL label) converts to whole units of 0.01 in and 0.01 lb with checked
  64-bit arithmetic and compares exactly. Architecture ED-ARITH and Section 22.
- R-7 selection (decision D1). The per-lane past-due setting is implemented on the M5 branch
  (`selection.pastDueOrderByLane`, off by default) and awaits Tom's confirmation. The recorded decision
  selects R-7 m4Path and m5Path for the current milestone; they stay NOT_RUN until executed.
- Test names (decision D4). A test case declares the variants it executes as testId/variant tokens in its
  name, for example `X-RUN-09/duplicateID: ...`. The manifest's testFile paths follow the repository's
  flat `tests/m5/` layout.
- Evidence records (decision D5) are written by every test run into `<build>/m5Evidence/<commit>/` and
  attached to deliveries. They are not committed.
