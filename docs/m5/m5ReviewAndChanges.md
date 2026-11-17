## Revision 9.2 (17 November 2026): repository decisions and evidence gate

- The M5 base is the existing `feature/m5` code; WP1-WP10 are closed to tests v12 one pull request at a time.
  Decisions D1-D7 recorded in architecture Section 22; handover and traceability (Section 11.2) updated.
- D1: R-7 m4Path/m5Path selected (conditional, NOT_RUN). Variant statuses 552 NOT_RUN, 5 BLOCKED. No test ID or
  variant was added, removed or renumbered.
- D3: v12 Section 43 appended (supported input step 0.01 in / 0.01 lb, plus the D1, D4 and D5 notes); no earlier
  line moved, so every `sourceSection` line reference stands. Requirement SHA-256 is now
  `bf12a0f81fcffe9aaa089d47d02c265bf0b5658088c7d5894d5c378fb31a03ea` in the manifest and traceability.
- PR-6 (17 Nov 2026): Section 43 gains the X-PAT-19 note (M4's three-across rule as implemented: the
  three short sides sum to the trailer width, no separate 33.3-in test; decided by Mahib, not confirmed by
  Tom). Requirement SHA-256 is now `4176d265ff74cd2662bed7bb18da707aabf07bc0d1ae25a0af31a7cfe84b6c2d`.
- PR-7 (17 Nov 2026): ED-19 implemented (architecture Section 22, "ED-19 as implemented"): one floor-search
  state ledger per lane-date run shared by every M5 floor check, typed stops, decided-only caching, pure
  supplied-witness validation. Spec unchanged; X-PAT-18 and U-PF-09 testFile now tests/m5/testM5FloorSearch.cpp.
- PR-8 (17 Nov 2026): ED-21 implemented (architecture Section 22, "ED-21 as implemented"): lane-and-date
  rotated group order, then canonical records (source-line ID, then atom ID in place of the slot ID, which is
  assigned after allocation). Spec unchanged; X-RES-13 and X-META-06 testFile now tests/m5/testM5TieRule.cpp.
- PR-9 (17 Nov 2026): recorded pricing tolerance and pricing seam (X-CG-22); bounded repair loop runRepair
  (X-REB-10, R-1c/invalidDonorRestore); architecture Section 22 "Solver and repair as implemented". Spec
  unchanged; X-CG-22 testFile tests/m5/testM5Solver.cpp, X-REB-10 tests/m5/testM5RepairLoop.cpp.
- D4: manifest `testFile` paths follow the flat `tests/m5/` layout, from the test names in the repository.
- D5: evidence records live in the build folder and are not committed.
- Validation re-run and recorded in `m5DocumentValidation.json` (`revision92Checks`).
- Traceability test map: RD-13 baselineAug5_2023_2508 and baselineAug5_2299_2292 corrected from BLOCKED to
  NOT_RUN to match the manifest (a Revision 9.1 mismatch; the manifest counts already had them NOT_RUN).

## Revision 9.1 (17 November 2026): v12 precision and history patch

- Rewrote the v12 introduction with the 304/557/24/48/10 counts, kept the v11 record intact and added Section 42 to the reading order.
- Corrected X-PAT-13/twoAcross100, U-PB-08/payloadExactAndEpsilon and X-PAT-19/threeAcrossM4Cutoff to use one supported normalized input step (normally 0.01 in or 0.01 lb) instead of 0.0001. Candidate generation and geometrical proof remain distinct.
- Synchronized matching Phase 1 reconciliation entries, source-section line references, source SHA-256, manifest and traceability. Historical checks are archived unchanged in the validation JSON; the current check block was rerun separately.
- No named test IDs or variants were added, removed or renumbered. No C++ build or application test run is claimed.

# M5 document review and changes

Revision 9.1 note (17 November 2026): the three new tests were added to the work-package lists in the developer handover (X-CG-22 in WP2, X-PAT-19 in WP3, X-REB-10 in WP4), so the lists now cover all 304 tests. No test text changed.

## Revision 9 (17 November 2026): Phase 1 audit reconciled against v11

The source of authority is the provided `m5_docs_rev8_1.zip` containing v11, 301 named tests and 522 variants. The earlier 162 P12 proposals were audited individually against that source. Results: 104 ALREADY_COVERED, 33 ADD_VARIANT, 3 ADD_NEW_TEST, 22 OUT_OF_SCOPE. There are 32 unique ADD_VARIANT keys because P12-E10 and P12-K08 request the same U-DA-07/unknownTypeNoFallback variant. Three new named tests supply four variant keys, resulting in 304 named tests / 557 unique variants.

Corrected rules: literal M4 three-across side <=33.3 in, not 100/3; no durable M5 restart store; importer text-format parsing stays M1/M4; P0 only legality/count/proof/urgency/ties, P1 robustness, P2 optional scale. No new M6/M7 or mixed-fleet feature is implied.

Original Tom exact inputs under `tomInputs/` are preserved untouched from the supplied archive. The supplied archive did not include the separate test-spec editing handover as a separate file; the review follows the v11 package's embedded stable-ID, manifest, trace, hash, fixture and applicability rules, plus the explicitly attached Phase-1 prompt. A later handover-specific instruction would require separate verification.

Document validation: unique IDs and keys, no dropped v11 entries, current requirement SHA-256, trace and manifest coverage, ASCII, and independent small fixture arithmetic. No C++ application build, real-data test or M5 execution occurred; all new variants are NOT_RUN.

---

## Revision 8 (17 November 2026): Tom's 16 November email

Test specification v11. Documents only; no application build or test was run.

1. R-1a, R-1b and R-5a inputs arrived (Tom's package, synthetic review cases). R-1a and R-1b move from
   BLOCKED to NOT_RUN with the exact files, SHA-256 values and expected answers 25 and 2. R-1b uses a
   100 x 100 in floor. R-5a also points to the new copy; 617 in is a witness, and 624 in is valid.
2. ED-21 rotates by lane and date, as Tom's engineer and Saif described. laneKey is the 32-bit FNV-1a hash
   of "origin|destination|mode". X-RES-13 gains acrossLanes (9 variants); its copiedLane expectation and
   X-META-06 now allow each lane its own tie winner. EC-47 updated.
3. New test U-DA-07: a 40 ft high-cube container as the run's only trailer type (exact mm conversion,
   row patterns from its 92.52 in width, missing limits rejected). containerRun is BLOCKED until Tom sends
   payload and unit-load limit. New EC-48.
4. C9 record extended with the engineer's conditions for a future mixed fleet.
5. The handover has a "M5 delivery to Tom" checklist: PR, per-variant output, Tom's three cases, OR-Library,
   the 38-truck reference with timing, and build logs.

6. Revision 8.1 (same day): four leftover lines that still said R-1a/b were waiting were corrected (tests v11
   Section 0.5 and Section 30; handover WP0 and final gates). No test changed.

Counts: 301 named tests (U 109), 522 variants (515 NOT_RUN, 7 BLOCKED), 24 invariants, 48 EC rows, 10 CL
rules. All 300 v10 IDs and all 517 v10 variant keys are kept. Checks: m5DocumentValidation.json (revision8Checks).

---


## Revision 7 (14 November 2026): Tom's answers on OPEN-COMP and C9

Test specification v10. Documents only; no application build or test was run.

1. OPEN-COMP resolved. Saif asked Tom which tie rule to use when two plans ship exactly the same goods by
   urgency class. Tom: "any tiebreaker would work as long as it didn't always push to the same single
   side". The fixed canonical order in use would always give the tie to the same group, so the rule is
   now ED-21: compare shipped unit loads per group in a group order rotated by ship date, then canonical
   records. Fill, rear space, weight and truck count never break the tie. New test X-RES-13 (8 variants)
   and checklist row EC-47.
2. C9 recorded. Tom accepts that M5 refuses a lane-date whose reserved trucks have different trailers.
   He normally expects a truck-type list ("2 of A, 5 of B, and 1 of C"); that is not in the current inputs
   and is listed as out of scope for M5.
3. R-1a/b stay BLOCKED: Tom is waiting for his engineer. R-5a, R-8 and R-9 already have his e6dbb0f files.
4. The RD-13 Aug5 variant notes use the wording from the latest manifest update (WP0 verifies and preserves
   the evidence).
5. The handover has a "Coming from tests v7" table and a note on reporting progress per variant.

Counts: 300 named tests (X 158), 517 variants (507 NOT_RUN, 10 BLOCKED), 24 invariants, 47 EC rows, 10 CL rules.
All 299 v9 IDs and all 509 v9 variant keys are kept. Checks are in m5DocumentValidation.json (revision7Checks).

---


## Revision 6.1 (14 November 2026): text and status fixes

Small fixes on top of revision 6. No test ID, variant key, fixture value or expected result changed.

1. Missing spaces restored. Many lines in v9 Sections 33 and 36, the manifest, the traceability
   and this validation file had lost the space before a number ("ships5 and leaves7", "cap5",
   "mass600*n"). They now read "ships 5 and leaves 7", "cap 5", "mass 600*n". Identifiers such as
   variant keys (case1, production7Aug), m4Path, int64 and binpack1 were left unchanged.
2. Plain ASCII restored. The section sign and the en dash in the architecture and handover are now
   "Section " and "-".
3. RD-13 baselineAug5_2023_2508 and baselineAug5_2299_2292 are NOT_RUN, not BLOCKED. Their only
   blocker was that the review itself had no August data. The developer repository has the Aug5
   fixtures, and the per-lane evidence is in build-msvc-m4fix2/verify (WP0 step 1). Variants are now
   499 NOT_RUN and 10 BLOCKED.
4. Commit check added to the handover. Saif's reply to Tom names d028f24 (PR #3) as the delivered fix,
   while the verified tree is f9d9945. WP0 starts by confirming which commit is the baseline.

The v9 file hash changed because of item 1 only; the manifest and traceability carry the new hash.
All structural checks were re-run (see m5DocumentValidation.json, revision61Checks).

---


Revision 6 reviewed 14 November 2026. Output package: revision 6. Test specification: v9.
The revision 5 and revision 4 records follow as historical records; the current instructions and
acceptance applicability are those in revision 6 / v9.

## Assessment

The v8 update is materially better: it preserves the previous customer corrections, records f9d9945
as the M4 baseline, restores the exact fourteen-stack regression, adds the eleven-stack and configured
unit-load-cap cases, and records the per-lane override deferral. Its files are structurally synchronized.
The remaining defects are in the implementation contracts and test applicability, not missing bulk
coverage. This review corrects them and adds six focused tests that can be built from self-contained
synthetic inputs without waiting for another customer answer.

The revised set is suitable as a modular development handover, with the recorded discovery, engineering
and fixture prerequisites resolved at their work packages. It is not final M5 acceptance. Application
code, original probe files and raw customer data were not attached, so no M4/M5 build or test run is
claimed here. All application execution records remain NOT_RUN or explicitly BLOCKED.

## What was independently checked

Reviewed all seven newly uploaded v8 / revision-5 files together, and compared their changes with the
previous v7 / revision-4 deliverables. The input package has 293 distinct named tests and 475 distinct
test/variant identities: 465 NOT_RUN and 10 BLOCKED. The source, manifest and traceability agree on all
293 IDs, source sections and expected text. The 24 invariant, 43 edge-case and 10 clarification registers
agree; the six current output hashes and seven previous-input hashes recorded by the supplied validation
report match the available files.

Those structural passes do not establish semantic correctness or executable coverage. The original
Tom reproduction package, m4Fix2Verification.txt, repository and August inputs are unavailable in this
review. Their results remain REPORTED from the supplied documents. This review independently checks
new fixture arithmetic, small cap-constrained combinatorial optima, witness geometry, reference
integrity and the revised document contracts. The validation JSON distinguishes these checks from
application execution.

## Corrections that matter to implementation

| Priority | Remaining issue in v8 | Revision-6 correction |
|---|---|---|
| P0 | The physical API returned a boolean even though the new M4 behavior distinguishes Fits, DoesNotFit and Undecided | Separate bounded layout search from pure supplied-witness validation. A failed heuristic, exhausted search or cancellation cannot become a certified negative result. Preserve a checked incumbent and name any unresolved improving alternative. |
| P0 | The proposed API did not carry the per-call and shared route floor-state budgets | Add an owned floor-search context with per-call cap, remaining shared states, counters, scope and parent deadline/cancellation. A/B, blocks, repair and probes cannot reset it within that declared context. WP0 verifies the M4 route/group scope and its M5 mapping. |
| P0 | AtomClass contained geometry/weight but not unit-load composition, so equal geometry could reuse the wrong cap-constrained maximum | Count authoritative component unit loads with checked arithmetic. Give the packing class or solver input the per-copy count. Carry the effective slot cap through generated patterns, copies, A/B, remainders, repair, allocation and final validation. Keep pure geometry caching separate. |
| P0 | A valid relaxed count bound could be confused with a feasible cap-constrained packing | Keep lower-bound validity separate from feasible FFD/count and from full-objective proof. Odd caps and indivisible two-load atoms receive an independent exact control. |
| P1 | R-7 production integration remained required and BLOCKED although the per-lane feature is deferred | Mark the integration variants conditional and explicitly unselected for current M5. Current run-wide controls and direct synthetic isolation remain required. Remove the stale P9-before-WP5 and no-open-item implications. |
| P1 | Some proof wording required exhaustive grouping even after a valid witness established fit | A checked witness proves existence. Certified infeasibility requires an applicable negative certificate; optimality requires a same-objective bound or complete search over relevant alternatives. Unrelated incomplete search does not invalidate an independent certificate. |
| P1 | R-8's variant expectation required the reported 586-inch layout, and R-9 tied a global/direct cap test to unsupported per-truck size/weight input | Require the correct quantities and a valid supported layout within 630 x 100 inches. Keep 586/576/612 inches as reference measurements, not unique layout optima. Global-cap and injected-slot tests run without the absent production per-slot feed. |
| P1 | Active sections still called R-5a waiting for data | Preserve v8's record that its exact package is available; require the original files and checksums at implementation time. R-1a/b remain the genuinely missing exact-input item. |
| P1 | A command-line GCC warning exception could be reported as a strict-warning-clean gate | Record the actual compiler/flags/exception. A run with -Wno-error=array-bounds is not a clean -Werror result; leave the existing engineering disposition visible. |

The previous decisions remain: minimum-load acceptance is OFF inside M5 for this customer; FULL is
an advisory label; required feasible demand precedes can-go; original source ownership is preserved;
commitment is atomic; exact normalized metrics are separate from packing/report rounding; and the
lane-date wall-time allowance is separate from the floor-state allowance.

## Six added tests

All 293 v8 IDs remain. These six are new definitions in v9 Section 36.

| New ID | What it catches | Decisive control |
|---|---|---|
| X-PAT-16 | Counting stacks instead of component unit loads, or merging cap-constrained classes solely by geometry | Equal-geometry two-load and three-load rigid atoms under cap 5; mixed optimum 5, homogeneous controls 4 and 3. |
| X-PAT-17 | Copies, Method A/B, remainders, slot reassignment or repair violating an actual slot cap | Two-load rigid atoms with slot caps 5/3 allow six unit loads; changing the second cap to 4 allows eight. |
| X-STATE-11 | A changed cap reusing stale patterns, validation or proof, including before commit | Same-process cap 7/5/7 gives six/four/six unit loads, matching fresh runs; stale-cap commitment fails safely. |
| X-CG-21 | A lower bound used as a feasible all-goods packing | Seven two-load atoms, cap 5: exact minimum four trucks; ceiling(14/5) = 3 remains a valid weaker bound. |
| X-PAT-18 | Undecided collapsed to infeasible, or valid witness confused with exhaustive search | Eleven explicit 52 x 100-inch rectangles fit in 572 inches; injected verdict controls separate physical validation, unresolved alternatives and scoped service proof. |
| U-PF-09 | State-budget resets, bypasses or permanent negative caching of an unfinished search | Per-search cap 3 and shared route allowance 5; independent contexts, retries, cached witness and fresh-budget controls. |

Three new checklist rows cover component-count/cache identity (EC-44), shared floor limits and
Undecided caching (EC-45), and relaxed bounds versus indivisible packing (EC-46). R-8/R-9 remain exact
source-fixture regressions. RD-13 gains explicit variants for the two linked-date service declines
reported in the new baseline; their actual source fixtures are required before execution.

| Family | v8 | v9 |
|---|---:|---:|
| ACC | 3 | 3 |
| R | 15 | 15 |
| U | 107 | 108 |
| RD | 13 | 13 |
| X | 152 | 157 |
| PUB | 3 | 3 |
| Total named tests | 293 | 299 |

The current set has 24 invariants, 46 edge-case checklist entries and 10 clarification rules.
Parameterized variants and generated fixture instances remain separate from the named-test count.
The manifest now has 509 distinct variants: 499 NOT_RUN and 10 BLOCKED (after the revision 6.1 RD-13 change), with no application PASS or FAIL claimed. The six new tests add 30 variants; RD-13 and X-RUN-09 add four more under existing IDs. The final validation JSON records the detailed counts and current statuses.

## What remains before application sign-off

1. Reproduce the current M4 baseline at the actual repository commit and compile the proposed contracts
   during WP0/WP1. Bind the physical adapter to the actual M4 fit verdict, route-state scope and supported
   row model. The reported f9d9945 results do not replace this implementation evidence.
2. Commit/checksum the exact Tom R-5a/R-8/R-9 files and obtain R-1a/b's exact inputs. Keep the counted,
   non-vacuous R-8 probe gate and preserve source ordering. Self-contained added tests can proceed now.
3. Record the existing engineering choices: OPEN-COMP, source/config identity, floor-context scope,
   selected commit/retry behavior and any remaining validation domains. This review adds no new request
   for customer x/y and does not reinstate the deferred per-lane feature as an M5 blocker.
4. Resolve or explicitly disposition the baseline risks already reported by v8: the two Aug5 linked-date
   service declines, the slower runtime and the Aug6 route exceeding its recorded 10-second allowance.
   Compare linked-date urgency/service vectors, not net totals or per-date counts alone. Do not raise
   production budgets or weaken a service oracle merely to make a check pass.
5. Run every currently selected required variant against the implemented M5 paths, including real-data
   and published-fixture gates. Keep unavailable required fixtures BLOCKED; keep agreed deferred
   integration variants explicitly unselected. Record the GCC warning exception honestly.

## Files

The package contains M5_Edge_Cases_and_Tests_v9.md, m5ModularArchitecture.md,
m5DeveloperHandover.md, m5TestTraceability.md, m5TestManifest.json,
m5ReviewAndChanges.md and m5DocumentValidation.json. The companion Markdown, JSON and work-package
references use the same current test source. The ZIP includes the same bytes under docs/m5/.

---

# Historical revision-5 and revision-4 records

The text below preserves the prior authors' records. Claims that their original source reports or
probes were read/run describe those earlier reviews, not new actions in this revision. Current active
contracts, applicability and counts above supersede older wording where they differ.

# M5 document review and changes

Revision 5 reviewed 14 November 2026. Output package: revision 5. Test specification: v8.
The revision 4 review (11 November 2026) follows unchanged after the revision 5 section.

## Revision 5 update (14 November 2026)

### Assessment

The revision 4 package is updated for Tom's review of M4 at e6dbb0f (12 November 2026) and Saif's fix at f9d9945, using the M4 verification
`m4Fix2Verification.txt` (14 November 2026). Documentation only: no application code was changed, built or run for M5, and no M5 result is claimed. Every M5
execution entry remains NOT_RUN or explicitly BLOCKED. The package remains a foundation for the documented discovery/contracts work, not an implemented M5 system.

### Scope and evidence

Read: the seven revision 4 files; Tom's e6dbb0f reproduction package (REPRODUCE.txt, floor_proof_probe.cpp, floor-proof-results.json, verified-layout.json,
exactness-counterexample.json, proof_standard_probe.cpp, monotonicity-standard-results.json, proof-standard-results.jsonl, capacity_probe.cpp,
capacity-results.json; all synthetic); m4Fix2Verification.txt and its data. The M4 figures below are REPORTED evidence from that verification, not M5 results.

Facts that limit the evidence and that the next reader must not lose:

- The commit e6dbb0f and Saif's reply d028f24 were not in the repository used for the M4 verification. e51d9c4 stood in for e6dbb0f: Tom's probes gave
  byte-identical results there (trial 31, ten unit loads proven in 530 in, capacity and nine-variant outputs equal to his files). This is evidence, not proof, that the
  trees match. The verification should be repeated against e6dbb0f when it is available.
- Tom's `floor_proof_probe` prints "No counterexample found" at f9d9945 for a reason that makes it vacuous as written: its gate calls `arrangeFloor`, which now searches
  every grouping, so no trial reaches selection. With `arrangeFloorSorted` in the gate, 503 trials reach selection and all ship eleven. R-8 requires the probe to count them.
- f9d9945 is slower (Aug5 CLI 38.09 s against 21.15 s, benchmark 42.2 minutes against 23.9) and one Aug6 route is over the 10 s budget (12.83 s).
- Two Aug5 lanes ship one or two unit loads fewer than at e51d9c4 under the linked-date rule (verdict BUG by the rule's letter, no wrong proof).
- The unit-load cap is one global configuration value (64); there is no per-customer or per-equipment key. `pastDueOrder` is one run-wide setting.

### Changes that matter to implementation

| Priority | Item | Change and reason |
|---|---|---|
| P0 | M4 baseline | All baseline references move from e51d9c4 to f9d9945 (NEW BASELINE FOR M5: yes): 1,165 cases and 1,861,160 assertions, 18 benchmark cases, 296 / 489 / 557 trucks, fingerprints d023d0dc844bc96a / 9463a19ed51df529 / f79518632646835c, unproven routes 29 / 75 / 67, cube 3,182 / 3,217 / 3,196 cu ft, hand-over unchanged. e51d9c4 and 7cef94e stay named as previous baselines |
| P0 | Row grouping and proofs | New rules R-PROOF and R-GROUP (architecture Section 8.1, ED-19): a load fits when any grouping of the allowed rows fits; a refusal by a heuristic or exhausted search is never a proof; M5 never trusts an M4 `proven` on a load whose grouping was not exact. B24/B25 record the measured limit: exact and proven up to about 20 stacks of few footprints, honestly unproven above it |
| P0 | R-5a | Tom's exact fourteen-stack inputs and the nine distractor variants are available: 23 unit loads in every variant. "Waiting for data" removed |
| P0 | R-8 (new) | Tom's eleven-item case: eleven fit in 586 in, M4 once shipped ten proven. Through `selectLoads`, `planShipDates` and the M5 seam, with the independently checked rectangles and the counted 15,000-trial probe |
| P1 | R-5b | Extended with the 11-16 stack region, 14-20 stacks of few footprints and an exact row-grouping oracle. e51d9c4 shipped less than the best on 37 percent of the 11-16 stack cases, all proven |
| P1 | R-9 (new), ED-20 | Unit-load cap from configuration: 64 gives 64 unit loads in 576 in, 68 gives 68 in 612 in. The cap is part of the trailer/equipment capability per slot, never a universal 64 and never a blanket 68 |
| P1 | R-7, P9 | AgeFirst run-wide stays current; the per-lane override is deferred until Tom agrees it and an M4 milestone is set. R-7 integration variants stay BLOCKED for that reason |
| P1 | Reuse table | `arrangeFloor`, `candidateRows`/`loadGrowthCheck`, `selectForTruck`/`TruckFit` and the row checks formerly `truckCheck` carry f9d9945 file:line facts; P2's extraction names changed |

### Added tests and checklist rows

| New ID | What it catches |
|---|---|
| R-8 | A heuristic or exhausted floor search refusing a load that some grouping fits, and that refusal backing `proven = true`; a vacuous probe |
| R-9 | A universal 64 cap, or a blanket 68, instead of the customer/equipment limit |

New EC checklist entries: EC-42 (a heuristic or exhausted floor-search refusal backs a proven claim) and EC-43 (a universal 64 cap). R-5a, R-5b and R-7 change in place without renumbering.

| Family | v7 | v8 |
|---|---:|---:|
| ACC | 3 | 3 |
| R | 13 | 15 |
| U | 107 | 107 |
| RD | 13 | 13 |
| X | 152 | 152 |
| PUB | 3 | 3 |
| Total named tests | 291 | 293 |

There are 24 invariants, 43 EC checklist entries and 10 CL definitions. The manifest has 475 variants (465 NOT_RUN, 10 BLOCKED); BLOCKED are R-1a, R-1b, R-7 (m4Path, m5Path), and the other four unchanged
variants from v7. m5TestTraceability.md is regenerated from the manifest by a script that was first run on the v7 manifest and reproduced the v7 file line for line.

### Decisions still requiring an explicit record

1. Tom's e6dbb0f (and Saif's d028f24) must be supplied so the M4 verification can be repeated against the reviewed revision.
2. Saif should commit Tom's files under `tests/fixtures/tomReview/e6dbb0f/` and keep the probe gate on `arrangeFloorSorted` (or a test that counts the trials that reach selection).
3. Whether M4's slower plan (and the Aug6 route over budget) needs a time limit change before M5's CL-08 threshold benchmark; the two Aug5 lanes that ship fewer unit loads.
4. A per-customer or per-slot unit-load cap needs an input that carries it (C9); the milestone for the per-lane past-due override is still to be agreed with Tom.
5. The OPEN-COMP, exact-representation, no-fit/bucket, commitment and source prerequisites listed in the revision 4 review below are unchanged.

### Files in the package

- M5_Edge_Cases_and_Tests_v8.md (new; v7 is kept unchanged), m5ModularArchitecture.md and m5DeveloperHandover.md (Revision 5), m5TestTraceability.md (regenerated),
  m5TestManifest.json (schema 2), m5ReviewAndChanges.md (this file) and m5DocumentValidation.json.

---

# Revision 4 review (11 November 2026), kept unchanged

Reviewed 11 November 2026. Output package: revision 4. Test specification: v7.

## Assessment

The supplied documents provide a substantial foundation for implementation, but revision 3 was not
consistent enough to hand to a developer unchanged. Several active instructions still expressed
pre-correction rules, and some interface and test contracts could let incorrect behavior pass.

This review corrects those conflicts, adds 18 targeted test specifications, and synchronizes the
architecture, developer work packages, traceability and machine-readable manifest. The revised
package is suitable for beginning the documented discovery/contracts work. It is not an implemented
M5 system or a completed acceptance run. The remaining engineering and fixture dependencies below
must be resolved at their named work packages before their acceptance results can close.

## Scope and evidence

Reviewed all five supplied files and the supplied m5_docs_rev3.zip. Every ZIP member matched its
corresponding individual attachment byte for byte. The starting set had 273 named test IDs and
351 manifest variant rows; RD-12 repeated the productionProfile variant key.

The original Tom follow-up messages, current application repository, raw customer datasets and
referenced M4 verification reports were not attached. The latest customer corrections are therefore
taken from the supplied v6/architecture records. Statements about e51d9c4, 1,149 M4 tests, the stress
corpus and August results remain attributed REPORTED evidence, not independently rerun results.

Checks performed here cover document consistency, source-to-manifest identity, references, fixture
mathematics, physical-model scope and proposed interface contracts. No application code was changed,
compiled or executed. Every M5 execution entry remains NOT_RUN or explicitly BLOCKED.

## Changes that matter to implementation

| Priority | Issue in the supplied set | Correction and reason |
|---|---|---|
| P0 | Acceptance OFF in the decision register, but unconditional acceptance/withholding remained in method and repair pseudocode | Guard the optional predicate in every path. This customer's absent acceptance section is valid; numeric acceptance is evaluated only when enabled. Production underfilled shipments cannot be withheld by stale x/y logic. |
| P0 | Q6 was answered at the top but still pending in active tests, conflicts and handover gates | Use the current same-goods/same-objective F > B rule. Equality stops its count phase; missing customer answers are not fabricated as continuing blockers. |
| P0 | Count-optimality could be used as a complete allocation certificate | A subset or capacity bound does not certify priority, partial service, can-go or fill. Separate objective/proof metadata and service comparison; retain exact regression expectations. |
| P0 | Validation and commit identity were not sufficiently bound to actual source/resource state | Validation now records candidate, profile, source snapshot and reservation version. Version checks, selected deltas and idempotency records apply atomically; stale/conflicting work cannot replay cargo. |
| P0 | Optional acceptance and fullness could use nearest-0.001 values | Preserve authoritative normalized precision for final comparisons. Conservative packing grains and report rounding are separate. Added exact sub-grain boundary fixtures. |
| P0 | Residual-fit failure or empty inventory could produce an unjustified FULL claim | ED-FULL records nonempty eligible probe scope, complete no-fit evidence, explicit Unknown/Undetermined diagnostics and final residual-snapshot buckets. Labels remain advisory. |
| P0 | Global required/can-go precedence and future-source ownership were insufficiently explicit | Required feasible goods compete before future fill across all groups sharing slots. Only the selected draw changes the later input; unselected offers are not consumed or carried twice. |
| P1 | U-AC-08's production A/B/A fixture could not change its label | The original 20 FX-30 units leave 230 rear inches, so 14/30/14 does not test the intended flip. The replacement label fixture leaves exactly 30 rear inches and a fitting residual, producing false/true/false. |
| P1 | Scalar X-REB-04 asserted physical fullness; U-RM-04 equated below-full with numeric rejection | Scalar fullness is NotApplicable. Numeric acceptance and physical fullness have independent control cases; neither substitutes for the other. |
| P1 | Parallel work, clock arithmetic and mandatory/advisory checks had inconsistent budget treatment | One frozen wall-clock allowance per lane-date; checked multiplication/conversion; no worker reset or date pooling. Mandatory dispatch checks complete before use; an incomplete advisory fit diagnostic is explicit. |
| P1 | Manifest evidence key duplicated and current mappings referred to v5 | Merged the duplicate RD-12 key, retained stable test IDs, refreshed exact source references/hash and added machine-readable CL definitions and per-variant expectations. |
| P1 | General small enumerators were claimed as oracles for larger or physical cases | Separate scalar brute force, supported-layout enumeration, supplied witness checking and analytical group/urgency count oracles. An unsupported enumerator cannot certify a larger fixture. |

## Added tests

All 273 original named IDs remain. The following 18 cases are new definitions in v7 Section 33.

| New ID | What it catches |
|---|---|
| U-AC-09 | Strict optional acceptance immediately below/equal/above a threshold at precision smaller than 0.001. |
| U-FL-06 | Exact 98% weight and rear-space boundaries, including actual per-slot trailer limits. |
| U-FL-07 | Measuring rear space before maximum legal upstream stacking or retaining an obsolete FULL label. |
| U-FL-08 | Confusing stock exhaustion, rule exclusion and actual no-fit evidence; stale residual-snapshot labels. |
| U-FL-09 | Claiming no-fit after an interrupted search that has not yet examined a fitting residual. |
| U-FL-10 | Calculating rear space from summed lengths or row order instead of the supplied final occupied extent. |
| U-CF-04 | Future P0 cargo taking scarce shared reservations from required P11 cargo in another group. |
| U-CF-05 | A premature no-work exit on an existing reserved date containing only eligible can-go demand. |
| U-CF-06 | Deducting an unselected or repeated future draw from the later M4 input. |
| U-CF-07 | Overfilling or displacing required goods while filling exactly the remaining physical capacity. |
| U-CF-08 | An independently impossible required atom blocking feasible future fill or causing endless retries. |
| U-PF-06 | Overflow, nonfinite values, fractional truncation and zero-reservation budget handling. |
| U-PF-07 | Multiplying wall-clock allowance by workers, adding parallel CPU intervals as wall time, or publishing incomplete validation. |
| U-PF-08 | Pooling unused allowance between dates or lanes, or changing an in-flight deadline after its snapshot. |
| X-CG-20 | Treating a bound for shipped low-priority goods as a certificate for the full partial-service problem. |
| X-STATE-09 | Optional acceptance ON/OFF/ON leaking across methods, repair, validation and reports. |
| X-STATE-10 | Duplicate/concurrent commitment, conflicting payloads, half-applied state and stale-original retries. |
| X-REPORT-10 | Averaging averages, weighting by cargo instead of trucks, double-counting overlapping FULL reasons and including unselected results. |

New EC checklist entries are EC-33 through EC-41. Existing cases were also strengthened without
renumbering: R-7, U-RM-04/05, U-AC-05/08, U-FL-01..05, U-CF-03, X-CG-17, X-BLK-07,
X-REB-04/09, X-RUN-09 and the relevant report/production-profile cases.

Two deliberate controls avoid creating unapproved production comparisons. U-CF-05 uses one indivisible
whole-truck atom, so its one-truck expectation does not depend on a fewer-trucks tie-break. U-CF-06
explicitly selects A or B at the commitment seam: serving future goods earlier alone does not prove a
better linked-date result when both alternatives ultimately serve the same source quantities.

## Final coverage and evidence

| Family | Original v6 | Reviewed v7 |
|---|---:|---:|
| ACC | 3 | 3 |
| R | 13 | 13 |
| U | 93 | 107 |
| RD | 13 | 13 |
| X | 148 | 152 |
| PUB | 3 | 3 |
| Total named tests | 273 | 291 |

There are 24 invariants, 41 EC checklist entries and 10 CL definitions. Parameterized variants and the
1,716 generated multiset inputs are separate from the named-test count. The current variant counts,
statuses and exact source SHA-256 are generated in m5TestTraceability.md and m5TestManifest.json.

m5DocumentValidation.json records the automated document checks and independent fixture calculations.
These checks validate the handover package; they are not application test results or customer approval.

## Engineering decisions still requiring an explicit record

1. **OPEN-COMP:** recover and freeze the complete production comparison for equal-service allocations
   with different truck usage/rear-space/weight distributions and canonical ties. The recovered urgency
   rules alone do not define every cross-truck aggregate. Core and non-tied fixtures can proceed.
2. **Exact representations and ranges:** bind the proposed precision/overflow contracts to the actual
   normalized M4 values and C++17 types. Do not claim to recover original decimal precision already
   lost upstream. Optional-acceptance zero/range and rear-limit zero/maximum-domain alternatives need
   an explicit engineering disposition.
3. **No-fit/bucket and can-go integration:** ED-FULL and ED-CANGO are clearly labelled working engineering
   definitions, not new quotations from Tom. Verify their adapter/source interpretation in WP0; retain
   the specified synthetic controls while recording any justified production difference.
4. **Commitment and repeatability:** bind atomicity to the actual batch process, source/reservation
   ownership and chosen partial-commit policy. A database or new service is not required by this design.
5. **Source and capability prerequisites:** obtain Tom's exact R-1a/R-1b/R-5a fixtures; verify/implement
   the reported missing per-lane past-due setting; record the per-truck input capability disposition;
   confirm actual baseline/published fixture bytes and identities before their required runs.

The supplied documents say the 14-inch default and secondsPerLoad x reserved formula were stated to
Tom and apply unless he objects. This review preserves that provenance. It does not re-open answered
Q6, require x/y for this customer's M5, or turn those two stated settings into fabricated direct approval.

## Developer start point

Use the revision-4 package as a set. Start with WP0 in m5DeveloperHandover.md: inspect the real repository,
verify the reported e51d9c4 baseline and current head, record the integration inventory, and identify
which reported prerequisite changes have already landed. Then implement WP1 contracts, fixtures,
independent oracles and evidence registration before the solver/integration packages.

Do not copy documentation snippets as a ready-compiled header: order complete C++ types by dependency
and compile the assembled interfaces. Do not close an acceptance gate because placeholder doctests
exit zero. The final applicable required variants must have current, unique results from the actual
build, fixture/configuration and requirement identities.

## Files in the package

- M5_Edge_Cases_and_Tests_v7.md: requirements, preserved cases, corrected fixtures and new cases.
- m5ModularArchitecture.md: revision-4 contracts, ownership, policies, evidence and dependencies.
- m5DeveloperHandover.md: revision-4 work packages and execution/acceptance gates.
- m5TestTraceability.md: generated complete mapping and coverage summaries.
- m5TestManifest.json: schema-2 machine-readable case/variant mapping and requirement identity.
- m5ReviewAndChanges.md: this assessment and change rationale.
- m5DocumentValidation.json: document checks and independent arithmetic evidence.
