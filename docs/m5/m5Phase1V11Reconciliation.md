# M5 Phase 1 reconciliation: all 162 P12 proposals against v11

Review date: 17 November 2026. v11 SHA-256: `fb238f84fb04775013431e9237cb254bf7ed4c0145ce680339691cdb4a3f085f`.
Baseline: v11 with 301 named tests and 522 variants. Updated: v12 with 304 named tests and 557 variants.

## Disposition summary

| Disposition | P12 cases | What happened |
|---|---:|---|
| ALREADY_COVERED | 104 | Existing v11 test ID and exact existing variant cited |
| ADD_VARIANT | 33 | Maps to 32 unique new variant keys; P12-E10 and P12-K08 share one |
| ADD_NEW_TEST | 3 | X-PAT-19, X-CG-22, X-REB-10 |
| OUT_OF_SCOPE | 22 | 15 importer, 3 restart, 2 mixed fleet, 1 extreme scale, 1 deferred per-lane |
| **Total** | **162** | Every source proposal classified once |

## Scope and priority corrections

- Three-across uses the literal M4 row-pattern rule `side <= 33.3 in`, not `100/3`. Row-candidate omission is not automatically a global physical impossibility proof.
- M5 is a batch planner with no persistent durable store: crash/restart recovery is OUT_OF_SCOPE; in-process commit idempotency remains tested.
- CSV parsing, BOM, quoting, locale, date string interpretation, long paths and JSON lexer belong to importer/config-loader hardening. A typed invalid M5 handoff remains an M5 test.
- P0 is reserved for counting, legality, proof honesty, urgency and tie correctness. P1 covers robustness/reporting/portability; optional large stress is P2.
- These are specifications, not successful application test executions; no test was built or run for this review.

## Complete audit table

| Proposal | Revised priority | Disposition | v11 exact ID / variant or v12 target | Reason |
|---|---|---|---|---|
| P12-A01 - Empty mandatory file | P1 | OUT_OF_SCOPE | IMPORTER | Empty/header-only parsing belongs to M1/M4; verify no M5 handoff is issued |
| P12-A02 - UTF-8 BOM and line endings | P1 | OUT_OF_SCOPE | IMPORTER | BOM and LF/CRLF interpretation belong to importer; normalized M5 records are already the entry point |
| P12-A03 - Quoted delimiters | P1 | OUT_OF_SCOPE | IMPORTER | CSV quoting/embedded newlines belong to importer; no M5 delimiter parser is in scope |
| P12-A04 - Missing or renamed required column | P1 | OUT_OF_SCOPE | IMPORTER | Missing/reordered input columns are validated before M5 receives typed records |
| P12-A05 - Extra unknown column | P1 | OUT_OF_SCOPE | IMPORTER | Unknown CSV columns are an importer schema-policy issue |
| P12-A06 - Decimal locale and thousands strings | P1 | OUT_OF_SCOPE | IMPORTER | Decimal/thousands separator lexing belongs to importer; M5 never reparses source text |
| P12-A07 - Integer overflow before normalization | P0 | ALREADY_COVERED | `X-DATA-12 / synthetic` | Typed handoff count overflow is already exercised; lexical CSV overflow stays importer hardening |
| P12-A08 - NaN and infinity in physical fields | P0 | ALREADY_COVERED | `X-DATA-13 / synthetic` | NaN/infinite normalized physical values are rejected by current M5 handoff tests |
| P12-A09 - Duplicate source key | P0 | ALREADY_COVERED | `X-DATA-11 / synthetic` | Duplicate source key with same/conflicting data is already tested |
| P12-A10 - Leading-zero identifiers | P0 | ALREADY_COVERED | `X-DATA-09 / synthetic` | M4-resolved leading-zero IDs are kept distinct by M5 |
| P12-A11 - Mixed metric unit inputs | P0 | ALREADY_COVERED | `U-DA-01 / synthetic` | Existing units-to-loads normalization handoff case; mixed text units themselves are importer concern |
| P12-A12 - Invalid and leap dates | P1 | OUT_OF_SCOPE | IMPORTER | Bad calendar string parsing and leap-day date lexing belong to importer |
| P12-A13 - Time-of-day and time-zone fields | P1 | OUT_OF_SCOPE | IMPORTER | Time zones and time-of-day parsing belong to importer; M5 consumes normalized ship-day values |
| P12-A14 - Very long field and Unicode source | P1 | OUT_OF_SCOPE | IMPORTER | Extreme Unicode/length handling of input fields belongs to importer; report escape covered elsewhere |
| P12-A15 - Reservation rows with same lane-date | P0 | ALREADY_COVERED | `X-ROUTE-05 / synthetic` | Duplicate reservation records do not create duplicate available slots |
| P12-A16 - File paths with spaces and nonroot working directory | P1 | OUT_OF_SCOPE | IMPORTER | File path portability belongs to CLI/importer; M5 direct seam sees typed values |
| P12-B01 - Exact width equality | P0 | ALREADY_COVERED | `U-CG-04 / synthetic` | The exact width/length boundary is explicitly tested |
| P12-B02 - Three-across epsilon boundary | P0 | ADD_NEW_TEST | `X-PAT-19 / threeAcrossM4Cutoff` | Three-across candidate generator uses <=33.3, not 100/3; distinguish row pattern from physical certificate |
| P12-B03 - Two-across exact boundary | P0 | ADD_VARIANT | `X-PAT-13 / twoAcross100` | Verify two 50-in widths exactly fit and +delta is rejected by the same orientation |
| P12-B04 - Rotation-only placement | P0 | ADD_VARIANT | `U-PB-04 / rotationOnly` | Make the legal rotation the sole way a specific rigid atom can fit |
| P12-B05 - Exact trailer height including deck | P0 | ALREADY_COVERED | `X-PAT-07 / synthetic` | Deck-inclusive height equality and just-over are specified |
| P12-B06 - Stackability blocked by display | P0 | ALREADY_COVERED | `U-FL-07 / nonstackable` | Non-stackable display cannot gain illegal height or floor positions |
| P12-B07 - Rounded 48 versus 48.01 inches | P0 | ALREADY_COVERED | `X-PAT-13 / synthetic` | Full-precision near-equal dimensions/weight control is present |
| P12-B08 - Row grouping false impossibility | P0 | ALREADY_COVERED | `R-5a / m5Path` | Exact Tom row-grouping regression with witness/proof restrictions is present |
| P12-B09 - Overlapping rectangle witness | P0 | ALREADY_COVERED | `X-PAT-18 / invalidSuppliedWitness` | Overlap invalidates a supplied layout witness |
| P12-B10 - Out-of-bounds witness after translation | P0 | ALREADY_COVERED | `U-FL-10 / outsideTrailer` | Translation beyond trailer end is an existing invalid-witness variant |
| P12-B11 - Supplied witness with exhausted search | P0 | ALREADY_COVERED | `X-PAT-18 / witnessCertificate` | Checked supplied witness remains valid despite exhausted search budget |
| P12-B12 - Cache footprint version change | P0 | ALREADY_COVERED | `X-DATA-06 / synthetic` | Product-master change invalidates stale weight and cached packing |
| P12-B13 - Permitted pinwheel switch | P1 | ALREADY_COVERED | `U-PB-04 / synthetic` | Pinwheel and supported orientation generation are explicitly in the case |
| P12-B14 - Incomplete search cannot prove no-fit | P0 | ALREADY_COVERED | `U-FL-09 / interrupted` | Interrupted no-fit is Unknown, never a certificate of impossibility |
| P12-C01 - Exact weight boundary | P0 | ADD_VARIANT | `U-PB-08 / payloadExactAndEpsilon` | Ensure 45000.000000 fits and one supported positive increment fails, independent of fullness labels |
| P12-C02 - One indivisible overweight atom | P0 | ALREADY_COVERED | `U-PB-10 / synthetic` | An indivisible unit heavier than the truck is rejected |
| P12-C03 - Component unit-load cap versus atoms | P0 | ALREADY_COVERED | `X-PAT-16 / mixedClasses` | Component unit-load coefficients, not atom count, govern cap selection |
| P12-C04 - Cap 7 to 5 to 7 in same process | P0 | ALREADY_COVERED | `X-STATE-11 / capABA` | Cap 7/5/7 cache and proof invalidation already specified |
| P12-C05 - Invalid unit-load-cap config | P0 | ALREADY_COVERED | `X-PAT-16 / invalidCountsAndCaps` | Missing/zero/fractional/overflowing cap rejected at input seam |
| P12-C06 - One atom exceeds cap | P0 | ADD_VARIANT | `X-PAT-16 / singleAtomOverCap` | Atom with 6 indivisible unit loads against cap 5 is never split or loaded |
| P12-C07 - Two constraints bind together | P0 | ADD_VARIANT | `U-PB-08 / simultaneousBinding` | Two hard limits exactly bind; next atom violates both and cannot be admitted |
| P12-C08 - Different trailer type for entire run | P0 | ALREADY_COVERED | `U-DA-07 / mmConversion` | Different single run trailer type uses exact container dimensions |
| P12-C09 - Unsupported mixed equipment in one lane-date | P0 | ADD_VARIANT | `U-DA-05 / mixedRunRejected` | Current input rejects unsupported mixed types with reason; no guessed single default |
| P12-C10 - Typed availability matrix future scope | P2 | OUT_OF_SCOPE | FUTURE_FLEET | Truck-type inventory matrix and mixed fleet composition require a new input contract accepted as future scope |
| P12-C11 - Missing trailer capability fields | P0 | ALREADY_COVERED | `U-DA-07 / missingLimitsRejected` | Alternate trailer without payload/unit cap is rejected, no defaulting |
| P12-C12 - Length equality and one-micron excess | P0 | ADD_VARIANT | `X-PAT-13 / microLengthBoundary` | Trailer length exactly at boundary versus one input-supported microunit excess |
| P12-D01 - Required goods before can-go | P0 | ALREADY_COVERED | `U-CF-04 / methodA` | Required cargo outranks can-go across shared reservations |
| P12-D02 - Overdue date beats nominal priority | P0 | ALREADY_COVERED | `R-4a / synthetic` | AgeFirst picks older P11 over newer P0 under its explicit fixture |
| P12-D03 - Strict original due date after carry | P0 | ALREADY_COVERED | `X-DATE-08 / ageFirst` | Carried goods preserve original age for urgency comparison |
| P12-D04 - 10-day pull boundary and availability | P0 | ALREADY_COVERED | `U-CF-05 / outsideWindow` | Can-go outside pull window is excluded even with availability |
| P12-D05 - Mixed component stack prevents future leakage | P0 | ALREADY_COVERED | `X-DATE-06 / pullOff` | Mixed component stack cannot smuggle future-unavailable units |
| P12-D06 - Can-go-only existing reservation | P0 | ALREADY_COVERED | `U-CF-05 / normalRouteCanGoOnly` | Existing date with can-go only does not prematurely exit |
| P12-D07 - Impossible required cannot block feasible service forever | P0 | ALREADY_COVERED | `U-CF-08 / impossibleRequired` | Impossible required atom cannot deadlock eligible feasible future goods |
| P12-D08 - Carry and pull same source offered twice | P0 | ALREADY_COVERED | `U-CF-06 / retryAfterCommit` | Selected future draw is deducted exactly once on later date |
| P12-D09 - Two destinations with same product ID | P0 | ALREADY_COVERED | `X-DATE-11 / synthetic` | Same product on different destinations cannot cross reservation identity |
| P12-D10 - Zero-day pull window | P0 | ADD_VARIANT | `U-RS-09 / zeroDayWindow` | A configured zero-day pull window permits only same-day demand |
| P12-D11 - Date arithmetic leap and year boundary | P0 | ALREADY_COVERED | `X-DATE-10 / synthetic` | Leap and year-boundary day arithmetic already tested |
| P12-D12 - Unknown priority fails early | P0 | ALREADY_COVERED | `U-RS-05 / synthetic` | Invalid priority outside supported rank domain is rejected |
| P12-D13 - Equal-age all same-class tie | P1 | ALREADY_COVERED | `X-RES-13 / canonicalWithinGroup` | ED-21 includes equal-class canonical source-line tie |
| P12-D14 - Reservation absent while demand exists | P0 | ALREADY_COVERED | `U-RT-06 / synthetic` | Goods with zero reservation do not consume/ship trucks |
| P12-D15 - Linked-date selection evaluated in reverse order | P0 | ADD_VARIANT | `X-DATE-07 / reversedInputDates` | Reverse order of source rows for linked dates; no duplicate goods and exact selected deltas |
| P12-E01 - Threshold boundary 20 and 21 | P0 | ALREADY_COVERED | `U-RT-02 / synthetic` | 21 versus configured 20 route boundary has direct control |
| P12-E02 - Configurable threshold not hard-coded | P0 | ALREADY_COVERED | `U-RT-03 / synthetic` | Alternate threshold 50 proves routing is configurable |
| P12-E03 - M5 absent fallback invariant | P0 | ADD_VARIANT | `U-RT-01 / plannerAbsentFallback` | When no M5 planner is installed, the M4 path must remain byte-for-byte unchanged |
| P12-E04 - Rolling block 21 | P0 | ALREADY_COVERED | `U-RB-02 / synthetic` | 21 trucks form a full 19-block and short second block |
| P12-E05 - Rolling block 38 and 39 | P0 | ADD_VARIANT | `U-RB-01 / thirtyNineBlocks` | Add 39-slot case with 19+19+1 and no invented cargo |
| P12-E06 - Same lane-date groups share reservations | P0 | ALREADY_COVERED | `U-RS-01 / synthetic` | Groups compete across one shared lane-date reservation ceiling |
| P12-E07 - Duplicate reservation row idempotence | P0 | ALREADY_COVERED | `X-ROUTE-05 / synthetic` | Duplicate reservation record and distinct equal-valued records are separate |
| P12-E08 - No goods but 21 reserved | P0 | ALREADY_COVERED | `X-RUN-12 / noGoods` | A completed empty run is not a timeout, and reservation count remains unchanged |
| P12-E09 - Date-scoped reservations | P0 | ALREADY_COVERED | `ACC-2 / bothDatesBenchmark` | The joined reference keeps independent 21/20 date pools |
| P12-E10 - Unknown trailer with availability | P0 | ADD_VARIANT | `U-DA-07 / unknownTypeNoFallback` | Unknown equipment type with availability must fail before planning, never choose 53F |
| P12-E11 - Reservation count 100 plus | P1 | ADD_VARIANT | `U-RS-01 / hundredAndOneReserved` | Count 101 reserves accurately without needing to create empty shipments |
| P12-E12 - Threshold change while plan is in flight | P0 | ADD_VARIANT | `X-STATE-04 / thresholdSnapshotInFlight` | Changing config after planning starts cannot change frozen routing decision |
| P12-F01 - Two equal sides over balanced lane dates | P0 | ALREADY_COVERED | `X-RES-13 / twoGroupsAlternate` | ED-21 alternates tied groups across consecutive dates |
| P12-F02 - Fairness input permutation | P0 | ALREADY_COVERED | `X-RES-13 / shuffledInput` | Source and group row order do not change rotated winner |
| P12-F03 - Fairness replay and thread count | P1 | ADD_VARIANT | `X-RES-13 / threadCountReplay` | Tie winner is stable across completed deterministic runs at 1/2/8 threads |
| P12-F04 - Urgency strictly outranks fairness | P0 | ALREADY_COVERED | `X-RES-13 / nonTieControl` | Urgency overrides any rotated group preference |
| P12-F05 - Duplicate candidates do not bias side | P0 | ADD_VARIANT | `X-RES-13 / duplicateCandidates` | Equivalent generator duplicates of one group's plan do not bias rotation |
| P12-F06 - Cancel and retry fairness state | P0 | ALREADY_COVERED | `X-RES-13 / repeatRun` | Rotation is stateless from lane/date, and fresh rerun chooses identical winner |
| P12-F07 - Cross-date fairness key | P0 | ALREADY_COVERED | `X-RES-13 / twoGroupsAlternate` | Date number affects rotation exactly as ED-21 specifies |
| P12-F08 - Special characters in IDs | P1 | ADD_VARIANT | `X-RES-13 / specialGroupIds` | Delimiter-like group IDs sort as stable IDs; lane hash uses normalized lane fields only |
| P12-F09 - Hash modulo and signed arithmetic | P0 | ADD_VARIANT | `X-RES-13 / fnvUnsignedOverflow` | FNV-1a values above signed 2^31 remain unsigned mod 2^32 before group rotation |
| P12-F10 - Three-way equal sides | P0 | ALREADY_COVERED | `X-RES-13 / threeGroupsCycle` | G1/G2/G3 each win once in specified three-date cycle |
| P12-F11 - Group with zero eligible units | P0 | ADD_VARIANT | `X-RES-13 / ineligibleGroupIgnored` | Group without eligible demand never changes g or the rotated winner |
| P12-F12 - Concurrent same-slot tie | P0 | ADD_VARIANT | `X-STATE-10 / sameSlotTieConcurrent` | Concurrent comparisons cannot commit conflicting choices into the same reservation slot |
| P12-G01 - FFD suboptimal numeric control | P0 | ADD_VARIANT | `U-FF-01 / ffdConcreteGap` | Capacity10 items 6,5,3,2,2,2 have FFD=3, certified optimum=2, escalation follows chosen threshold |
| P12-G02 - LP versus integer completion | P0 | ALREADY_COVERED | `X-CG-18 / synthetic` | LP relaxation 2.7 cannot certify pure-pattern integer four instead of true three |
| P12-G03 - Zero reduced-cost degenerate columns | P0 | ADD_NEW_TEST | `X-CG-22 / duplicateZeroCostColumns` | Duplicate zero-reduced-cost patterns do not cause infinite pricing loop or false integer proof |
| P12-G04 - Pricing tolerance around zero | P0 | ADD_VARIANT | `X-CG-22 / nearZeroReducedCost` | Numeric reduced costs straddling zero follow declared tolerance without a false certificate |
| P12-G05 - Iteration cap in middle of feasible search | P1 | ALREADY_COVERED | `U-CG-06 / synthetic` | Iteration cap produces named stop with a checked incumbent/proof scope |
| P12-G06 - Timeout before first incumbent | P0 | ALREADY_COVERED | `X-RUN-02 / synthetic` | Deadline before first incumbent is reported as no result, not a PASS plan |
| P12-G07 - Timeout after valid incumbent | P0 | ALREADY_COVERED | `X-RUN-03 / keepValidated` | Timeout after incumbent returns only a previously checked result |
| P12-G08 - Lower bound weaker than packing optimum | P0 | ALREADY_COVERED | `X-CG-21 / weakBoundTrigger` | Weak count lower bound is never confused with all-goods packing proof |
| P12-G09 - Bound on subset cannot certify service | P0 | ALREADY_COVERED | `X-CG-20 / subsetBound` | Partial-service objective cannot inherit a subset count proof |
| P12-G10 - Count equality does not prove fill best | P0 | ALREADY_COVERED | `U-FF-03 / fxGap` | Equal quick truck count to same-objective lower bound stops that phase, not global service optimum |
| P12-G11 - Repair invalidates proof and source map | P0 | ALREADY_COVERED | `R-1c / synthetic` | Repair clears prior proof and updates source quantities |
| P12-G12 - No-fit unknown with exhausted row search | P0 | ALREADY_COVERED | `X-PAT-18 / undecidedWithIncumbent` | Incomplete floor search cannot prove impossibility |
| P12-G13 - Method B block starvation | P0 | ALREADY_COVERED | `X-RUN-05 / synthetic` | Early 19-truck block cannot starve a better urgent group allocation |
| P12-G14 - Two repair methods oscillate | P0 | ADD_NEW_TEST | `X-REB-10 / oscillatingRepairCycle` | Repeated repair states must terminate without losing the best validated incumbent |
| P12-G15 - Overlarge objective vector | P0 | ALREADY_COVERED | `X-DATA-12 / synthetic` | Large count overflow is reported without wraparound |
| P12-G16 - Unselected method proof pollution | P0 | ADD_VARIANT | `X-STATE-02 / unselectedMethodProof` | Candidate B certificate must not be attached to selected candidate A |
| P12-G17 - Best incumbent after invalid donor repair | P0 | ADD_VARIANT | `R-1c / invalidDonorRestore` | Keep earlier validated result after donor repair fails; no intermediate state commits |
| P12-G18 - Permutation-invariant completed exact solve | P1 | ALREADY_COVERED | `X-META-03 / synthetic` | Small complete exact solve respects the applicable canonical tie rule under row shuffles |
| P12-G19 - Zero reservations search misuse | P0 | ALREADY_COVERED | `X-RUN-12 / completedZeroDispatch` | With no reservations no search silently claims service |
| P12-H01 - Candidates A and B are isolated | P0 | ALREADY_COVERED | `X-STATE-01 / synthetic` | A/B candidates isolated when run in both orders |
| P12-H02 - Idempotent duplicate commit | P0 | ALREADY_COVERED | `X-STATE-10 / concurrentIdentical` | Exact duplicate commit is idempotent |
| P12-H03 - Conflicting payload same operation key | P0 | ALREADY_COVERED | `X-STATE-10 / conflictingPayload` | Same operation key with different payload is rejected |
| P12-H04 - Stale inventory snapshot at commit | P0 | ALREADY_COVERED | `X-STATE-10 / staleSnapshot` | Stale inventory revision cannot commit |
| P12-H05 - Crash after reservation debit before inventory debit | P1 | OUT_OF_SCOPE | RESTART | Batch M5 has no durable store; crash/restart atomic recovery is not a current delivery feature |
| P12-H06 - Cancel after repair before final validation | P0 | ALREADY_COVERED | `X-RUN-03 / keepValidated` | Cancellation during provisional repair preserves only checked incumbent |
| P12-H07 - No commit on benchmark comparison | P0 | ALREADY_COVERED | `X-STATE-02 / synthetic` | Benchmark/unselected candidate has no actual claim or inventory debit |
| P12-H08 - Parallel lane-date isolation | P0 | ALREADY_COVERED | `X-META-06 / synthetic` | Independent lanes preserve independent stock and reservations |
| P12-H09 - Concurrent shared availability claim | P2 | OUT_OF_SCOPE | FUTURE_FLEET | Shared typed fleet inventory across lanes is not in current C9/reservation feed; no global availability pool exists |
| P12-H10 - Cache OFF ON OFF policy | P0 | ALREADY_COVERED | `X-STATE-09 / sameIdRevision` | Acceptance OFF/ON/OFF cache identity and policy isolation are present |
| P12-H11 - Frozen plan revision after repair | P0 | ALREADY_COVERED | `X-STATE-11 / stalePrecommit` | Stale proof/validation is rejected if cap/revision changes |
| P12-H12 - Unique temp files under parallel doctest | P1 | ALREADY_COVERED | `U-PF-04 / existingTest` | Concurrent temp-file collision is already a recorded M4 regression |
| P12-I01 - Per-source conservation not just totals | P0 | ALREADY_COVERED | `X-DATA-02 / synthetic` | Per-source conservation detects swapped cargo even when totals match |
| P12-I02 - Production method results not summed | P0 | ALREADY_COVERED | `X-STATE-02 / synthetic` | A and B results are alternatives, never summed |
| P12-I03 - Historical 18 versus 38 scope | P0 | ALREADY_COVERED | `ACC-2 / bothDatesBenchmark` | Historical 38 is a comparison, not 21-date capacity |
| P12-I04 - No result versus no goods versus proven infeasible | P0 | ALREADY_COVERED | `X-RUN-12 / timeoutNoIncumbent` | Zero service completed is distinct from timeout/no incumbent |
| P12-I05 - No fullness for scalar math | P1 | ALREADY_COVERED | `X-REB-04 / synthetic` | Scalar capacity/acceptance tests do not infer 3D physical FULL |
| P12-I06 - Overlapping FULL reasons counted once | P1 | ALREADY_COVERED | `X-REPORT-10 / overlappingReasons` | Overlapping FULL reasons counted once |
| P12-I07 - Report rounding never used for legality | P0 | ALREADY_COVERED | `U-AC-09 / cubeEqual` | Strict acceptance and physical decisions use unrounded values |
| P12-I08 - Zero accepted trucks averages | P1 | ALREADY_COVERED | `X-REPORT-10 / emptyBucket` | Empty selected-truck denominators yield N/A as specified |
| P12-I09 - Report ASCII with Unicode labels | P1 | ALREADY_COVERED | `X-REPORT-09 / synthetic` | Escaped Unicode input identifiers give safe ASCII report output |
| P12-I10 - Unknown penalty is not zero | P1 | ALREADY_COVERED | `U-RS-02 / synthetic` | Unknown penalty is not treated as zero |
| P12-I11 - Evidence identity prevents fake PASS | P1 | ALREADY_COVERED | `X-RUN-09 / noAssertion` | Evidence must contain a real assertion; placeholder cannot pass |
| P12-I12 - Stable source identity across report formats | P1 | ALREADY_COVERED | `X-DATA-09 / synthetic` | Source IDs preserve distinct leading zeros and stable identity |
| P12-J01 - Thousands of stacks bounded memory | P2 | ADD_VARIANT | `X-META-10 / thousandStacksP2` | Optional P2 1000-stack memory stress under bounded budget; never part of current release gate |
| P12-J02 - Fifty-nine distinct footprints | P2 | ADD_VARIANT | `X-META-10 / fiftyNineFootprintsP2` | Optional P2 59-footprint fuzz distribution; retain seed and resource logs |
| P12-J03 - Three-billion unit-load count | P2 | OUT_OF_SCOPE | EXTREME_SCALE | Three-billion physically materialized unit loads is unnecessary; count-only overflow already X-DATA-12 |
| P12-J04 - Budget arithmetic overflow | P0 | ALREADY_COVERED | `U-PF-06 / multiplicationOverflow` | Overflow in secondsPerLoad * reservations already checked |
| P12-J05 - Clock monotonic under wall time shift | P0 | ADD_VARIANT | `U-PF-07 / wallClockShift` | Wall-clock jumps must not change monotonic deadline; fake clock control |
| P12-J06 - Shared floor-state budget across all phases | P0 | ALREADY_COVERED | `U-PF-09 / sequentialLimits` | Shared floor-state caps cannot reset between phases |
| P12-J07 - Independent dates do not pool unused time | P0 | ALREADY_COVERED | `U-PF-08 / laneDateIsolation` | Lane-date time budgets never pool |
| P12-J08 - Parallel cancellation and evidence | P0 | ALREADY_COVERED | `U-PF-07 / uncheckedAtDeadline` | Cancellation cannot publish unchecked output |
| P12-J09 - Cold versus warm cache benchmark | P1 | ADD_VARIANT | `X-RUN-10 / coldWarmCache` | Benchmark explicitly compares isolated cold and warm caches without changing cargo decisions |
| P12-J10 - Windows path with long names | P1 | OUT_OF_SCOPE | IMPORTER | Long Windows file paths belong to path layer/CLI, not M5 planning contracts |
| P12-J11 - Decimal-comma locale invariant | P1 | OUT_OF_SCOPE | IMPORTER | Decimal-comma locale belongs to the source importer, not M5 math after normalization |
| P12-J12 - Fresh builds under all compilers | P1 | ADD_VARIANT | `X-RUN-09 / crossCompilerGate` | Fresh builds/tests on MSVC, GCC and Clang are a machine-evidence acceptance variant |
| P12-K01 - Missing params file | P1 | OUT_OF_SCOPE | IMPORTER | Missing required params file is CLI/config loader error, not an M5 planner acceptance case |
| P12-K02 - Missing optional rear threshold | P1 | ALREADY_COVERED | `U-FL-03 / synthetic` | Optional rear threshold config check is present |
| P12-K03 - Mutually inconsistent capability | P0 | ADD_VARIANT | `X-DATA-13 / inconsistentSlotCapabilities` | A nonfinite/mismatched slot capability is rejected rather than silently corrected |
| P12-K04 - Duplicate JSON config keys | P1 | OUT_OF_SCOPE | IMPORTER | Duplicate JSON keys are an input-config parsing policy upstream of M5; record separately |
| P12-K05 - Unsupported unknown key | P1 | OUT_OF_SCOPE | IMPORTER | Unknown JSON config key policy belongs to config loader; no M5 feature implied |
| P12-K06 - Optional acceptance OFF versus ON fields | P0 | ALREADY_COVERED | `U-AC-04 / productionProfile` | Optional predicate OFF does not require synthetic x/y production values |
| P12-K07 - Per-lane past-due override deferred | P1 | OUT_OF_SCOPE | DEFERRED | Per-lane past-due override is expressly deferred; current run-wide AgeFirst remains required |
| P12-K08 - Unknown equipment no fallback | P0 | ADD_VARIANT | `U-DA-07 / unknownTypeNoFallback` | Unknown equipment must fail without 53F fallback (same new variant as P12-E10) |
| P12-K09 - Same profile name different versions | P0 | ALREADY_COVERED | `X-STATE-11 / sameIdentityRevision` | Profile version change invalidates cached proof/feasibility |
| P12-K10 - Routing based on reserved count not demand estimate | P0 | ALREADY_COVERED | `U-RT-03 / synthetic` | Routing uses configured reservation threshold instead of estimated demand trucks |
| P12-K11 - Timeout defaults and disabled flags | P1 | ALREADY_COVERED | `U-PF-06 / invalidSeconds` | Invalid secondsPerLoad is a policy error, not a hidden default |
| P12-K12 - Config snapshot changes after worker start | P1 | ALREADY_COVERED | `X-STATE-04 / synthetic` | Planning uses frozen config snapshot as shown by OFF/ON replay |
| P12-L01 - Tom exact inputs as immutable regression | P0 | ALREADY_COVERED | `R-1a / m5Path` | Tom's exact unmodified source probes and expected witnesses available in v11 |
| P12-L02 - M4 baseline drift guard | P0 | ALREADY_COVERED | `RD-04 / realData` | Same-input independent M4 lanes cannot change under M5 integration |
| P12-L03 - End-to-end shortage with fairness | P0 | ALREADY_COVERED | `X-RES-13 / acrossLanes` | Shortage tied groups rotate by lane-date and cannot override urgency |
| P12-L04 - End-to-end cargo source leakage | P0 | ALREADY_COVERED | `U-CF-06 / retryAfterCommit` | Linked-date source draw is applied once to later remaining inputs |
| P12-L05 - Final repair changes FULL but not acceptance | P0 | ALREADY_COVERED | `U-AC-07 / mutation` | Repair changes optional acceptance and physical labels are independently rechecked |
| P12-L06 - Last-block timeout retains valid incumbent | P0 | ALREADY_COVERED | `X-E2E-05 / keepValidated` | Last block interrupted during repair preserves only verified earlier block candidate |
| P12-L07 - Multiple hard limits and urgency | P0 | ALREADY_COVERED | `X-E2E-07 / inputOrderABC` | Global urgency allocation wins despite multiple groups and constraints |
| P12-L08 - Date-specific capacity and no pooling | P0 | ALREADY_COVERED | `ACC-2 / bothDatesBenchmark` | Linked dates have separate reservations and eligibility |
| P12-L09 - M4 handoff already normalized | P0 | ALREADY_COVERED | `X-DATA-01 / synthetic` | M4-normalized loads are not divided twice |
| P12-L10 - No-fit uncertainty after M4 repair | P0 | ALREADY_COVERED | `X-PAT-18 / undecidedWithIncumbent` | No-fit from exhausted M4 search is Undecided, not proven |
| P12-L11 - Malformed availability and shortage | P0 | ADD_VARIANT | `X-ROUTE-05 / malformedHandoffReservation` | Typed negative/invalid reservation handoff must fail visibly rather than silently skip a shortage |
| P12-L12 - Crash-safe linked-date commit | P1 | OUT_OF_SCOPE | RESTART | Batch M5 has no durable store; cross-process linked-date crash recovery is out of scope |
| P12-L13 - Scale plus adverse ordering | P2 | ADD_VARIANT | `X-RUN-05 / adverseBlockOrderingP2` | Optional P2 many-block adverse ordering test (bounded 100 slots), not current gate |
| P12-L14 - Configuration identity across restarts | P1 | OUT_OF_SCOPE | RESTART | Configuration identity after process restart is a deployment/batch-repeatability responsibility, not durable M5 recovery |

## Importer hardening: separate backlog, not current M5 acceptance

These cases belong to the M1/M4 importer or command/config loader. In each case, M5 is responsible only for rejecting an invalid typed handoff if one is produced; it is not responsible for reparsing a CSV or file system path.

| P12 case | Importer task |
|---|---|
| P12-A01 | Empty mandatory file - Empty/header-only parsing belongs to M1/M4; verify no M5 handoff is issued |
| P12-A02 | UTF-8 BOM and line endings - BOM and LF/CRLF interpretation belong to importer; normalized M5 records are already the entry point |
| P12-A03 | Quoted delimiters - CSV quoting/embedded newlines belong to importer; no M5 delimiter parser is in scope |
| P12-A04 | Missing or renamed required column - Missing/reordered input columns are validated before M5 receives typed records |
| P12-A05 | Extra unknown column - Unknown CSV columns are an importer schema-policy issue |
| P12-A06 | Decimal locale and thousands strings - Decimal/thousands separator lexing belongs to importer; M5 never reparses source text |
| P12-A12 | Invalid and leap dates - Bad calendar string parsing and leap-day date lexing belong to importer |
| P12-A13 | Time-of-day and time-zone fields - Time zones and time-of-day parsing belong to importer; M5 consumes normalized ship-day values |
| P12-A14 | Very long field and Unicode source - Extreme Unicode/length handling of input fields belongs to importer; report escape covered elsewhere |
| P12-A16 | File paths with spaces and nonroot working directory - File path portability belongs to CLI/importer; M5 direct seam sees typed values |
| P12-J10 | Windows path with long names - Long Windows file paths belong to path layer/CLI, not M5 planning contracts |
| P12-J11 | Decimal-comma locale invariant - Decimal-comma locale belongs to the source importer, not M5 math after normalization |
| P12-K01 | Missing params file - Missing required params file is CLI/config loader error, not an M5 planner acceptance case |
| P12-K04 | Duplicate JSON config keys - Duplicate JSON keys are an input-config parsing policy upstream of M5; record separately |
| P12-K05 | Unsupported unknown key - Unknown JSON config key policy belongs to config loader; no M5 feature implied |

## Other out-of-scope and optional cases

| P12 | Decision | Reason |
|---|---|---|
| P12-C10 | FUTURE_FLEET | Truck-type inventory matrix and mixed fleet composition require a new input contract accepted as future scope |
| P12-H05 | RESTART | Batch M5 has no durable store; crash/restart atomic recovery is not a current delivery feature |
| P12-H09 | FUTURE_FLEET | Shared typed fleet inventory across lanes is not in current C9/reservation feed; no global availability pool exists |
| P12-J03 | EXTREME_SCALE | Three-billion physically materialized unit loads is unnecessary; count-only overflow already X-DATA-12 |
| P12-K07 | DEFERRED | Per-lane past-due override is expressly deferred; current run-wide AgeFirst remains required |
| P12-L12 | RESTART | Batch M5 has no durable store; cross-process linked-date crash recovery is out of scope |
| P12-L14 | RESTART | Configuration identity after process restart is a deployment/batch-repeatability responsibility, not durable M5 recovery |

## What changed in the authoritative tests

New named: `X-PAT-19`, `X-CG-22`, `X-REB-10`. Exactly 35 unique test-variant identities were added; each is defined in v12 Section 42. The three P2 stress variants are conditional and unselected for current M5 acceptance. All v11 IDs and variants remain.

No original Tom review fixture was edited. The standalone test-spec editing handover was not present in the supplied ZIP or as a separate mounted attachment; synchronization follows the checked rules and contracts in the v11 package and the supplied Phase-1 prompt.
