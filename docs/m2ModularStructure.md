# Order Builder — M2 Modular Structure

**Segregation and Stacks**

Revision 3, 22 September 2026. Supersedes the 14 September version. Changes: modules 1
and 2 are built and green; Tom has answered three of the open questions; the data now
covers four days instead of one; and the modules are grouped by dependency so the
parallelisable work is visible.

Figures measured from four client extracts: `20260818` (17 Aug), `20260902` (two files),
`20260903`.

---

## 1. Status

| # | Module | State |
|---|---|---|
| 1 | `params` | [x] **Done.** Merged to `main`. 3 rounds, 2 independent reviews. Clean both platforms, ASan and UBSan. |
| 2 | `segregation` | [x] **Done.** Merged to `main` (PR #12, rebuilt as `segregation-v2`). Verified across all four days. Clean both platforms, ASan and UBSan. |
| 3 | `stackRules` | [x] **Done.** Merged to `main` (PR #13, rebuilt as `stackrules-v2`). Clean on the normal build and under ASan/UBSan. |
| 4 | `bindingConstraint` | [x] **Done.** Merged to `main` (PR #14). Real data reproduces 384 cube-bound / 3 weight-bound. |
| 5 | `stackBuilder` | [x] **Done** on `feature/m2-module-5-stackbuilder`, not yet merged. Method definitions are provisional pending Tom. Normal build clean; sanitizer run not done. |
| 6 | `stackReporter` | [ ] Not started. No code on any branch. |
| 7 | pipeline integration | [ ] Not started. |

Repo check, 22 September 2026: `main` contains modules 1 to 4. Module 5 is on its own
branch, not yet merged.

Suite: 213 cases / 181,810 assertions. M1 baselines unmoved throughout — 24,357 demand
lines, hash total 8,708,934, 152,911.2 pallet-equivalents, 103,005,833 lb.

---

## 2. Dependencies — what can be built in parallel

The modules fall into five tiers. Within a tier, modules do not depend on each other and
can be built at the same time by different people.

```
  TIER 0   params                          independent of everything
              |
              +---------------+
              |               |
  TIER 1   segregation     stackRules      independent of EACH OTHER
              |               |
              +-------+-------+
                      |
  TIER 2       bindingConstraint
                      |
  TIER 3         stackBuilder
                      |
  TIER 4   stackReporter  ->  pipeline integration
```

| Tier | Module | Depends on | Can start when |
|---|---|---|---|
| 0 | `params` | nothing | — (done) |
| 1 | `segregation` | `params` (one enum), M1 joiner output | — (done) |
| 1 | `stackRules` | `params` (CRI table, pallet specs, ceiling) | now |
| 2 | `bindingConstraint` | `segregation` (groups), `stackTypes.hpp` (`UnitLoad`) | after tier 1 |
| 3 | `stackBuilder` | all of the above | after tier 2 |
| 4 | `stackReporter` | outputs of all above | after tier 3 |
| 4 | pipeline integration | all modules | last |

### The one genuinely independent module

`params` depends on nothing. Everything else depends on it, directly or through a header.

### The one parallelisable pair

**`segregation` and `stackRules` never reference each other.** Segregation is about
origin and planner; stack rules are about weight and height. Neither includes the other's
header. With two developers they can be built simultaneously — which is worth knowing,
since `stackRules` is 3 days and `segregation` was 3.

That independence is not accidental and should be preserved. If `stackRules` ever needs
to know which group a unit load is in, or `segregation` ever needs a weight, something
has gone wrong in the split.

### Where the chain becomes strictly serial

From tier 2 onward there is no parallelism left. `bindingConstraint` needs real groups to
sum over. `stackBuilder` needs the binding constraint to know what to optimise against,
and `stackRules` to filter candidates. `stackReporter` needs everything to have run.

So of 19 estimated days, at most 3 can be overlapped, and only if a second developer
takes `stackRules` while `segregation` is in progress. That window has now passed —
segregation is finished — so the remaining work is serial.

---

## 3. What Tom has now answered

Three questions from Revision 2 are closed.

**Trailer interior height: 108 inches** for this customer. The value is confirmed, not
assumed. It barely changes the analysis below — 3.24% of stack pairs pass at both 108 and
110 — which means the design was robust to the uncertainty.

**No stacking restrictions for this customer.** Truck Builder's model enforces
AlwaysBottom, AlwaysTop and OwnStack, but Tom confirmed this customer has none of them.
The fields are absent from the product master because they do not apply.

> **Consequence: own-stack is not implemented in M2.** [x] Done: `SplitReason::OwnStack` is a
> placeholder in `segregationTypes.hpp` (segregation branch). Revision 2 proposed deriving it
> from CRI 1. That is no longer needed. `SplitReason::OwnStack` stays in the enum as a
> placeholder for a future customer, with no code path behind it. This removes Decision 3
> entirely.

**Maximum weight above is calculated for this customer** and Tom is sending the dataset.
Until it arrives, `stackRules` derives it from the CRI table. The module should read a
supplied value in preference to deriving one, so the switch is a data change rather than
a code change.

**Pallet variants** — Tom's rule: goods ship in whole pallets, so divide the ordered
quantity by cases-per-pallet to identify the variant. Measured below; it works, but not
completely.

---

## 4. Findings from the data

### Finding A — height, not CRI, is what blocks stacking

Across the 1,420 products demanded on 17 August, testing every ordered pair (2,016,400
combinations):

| Gate | Pairs surviving | Share |
|---|---:|---:|
| Fits under the 108 in ceiling | 65,295 | **3.24%** |
| …and passes the CRI weight check | 43,021 | 2.13% |

Height eliminates **96.8%**. CRI removes a further 1.1%. The median demanded unit load is
101.1 inches against a 108 inch ceiling.

**Design consequence:** the height test runs *before* the CRI test in `stackRules`. It is
cheaper and rejects thirty times more, which matters inside an O(n²) pairing loop under a
10-second budget. M2's honest headline is that most goods ship single-high and the rules
say why.

### Finding B — footprint is not in the product master

`Length`, `Width` and `Height` describe the **case**, not the unit load. 18,437 rows are
case-sized and 91.2% satisfy `Cases_Layer × case area ≤ 48 × 40`; the 1,403 pallet-sized
rows are almost all `Cases_Layer = 1`, where the case is the unit load.

So unit-load footprint comes from a pallet-type table `params` owns, defaulting to 48x40
until Tom supplies one. Computing it from `Length × Width` would give fifteen-fold errors
in rows per truck.

### Finding C — the variant rule resolves three quarters of cases

Tom's divisibility rule, measured on all four days:

| | 17 Aug | 02 Sep #1 | 02 Sep #2 | 03 Sep |
|---|---:|---:|---:|---:|
| Ambiguous lines | 144 | 184 | 192 | 154 |
| **Resolved** | 108 (75%) | 136 (74%) | 143 (75%) | 106 (69%) |
| Both variants divide evenly | 35 | 48 | 49 | 47 |
| No whole-pallet fit | 1 | 0 | 0 | 1 |

His premise holds: only 2 lines across four days are not a whole number of pallets, so
"always ships in even multiples" is true at better than 99.9%.

The unresolved quarter fails structurally, not because of bad data. When a quantity
divides evenly by *both* case-per-pallet values, the arithmetic is satisfied twice. Two
products cause nearly all of it on 17 August:

- **105553001** — GMA 84 / TLD 168, 28 lines. 168 is the only variant pair where one
  value is an exact multiple of the other, so every valid TLD quantity is also a valid
  GMA quantity. **Never resolvable this way.**
- **104875007** — GMA 104 / TLD 182, 7 lines. 728 cases is 7 pallets or 4.

**Design consequence:** [ ] *not yet done, no joiner change on any branch.* Divisibility becomes the primary rule in the joiner; the existing
preference order (TLD, PTL, PGM, GMA) remains the fallback, and the line stays flagged.
Ambiguity drops from 144 lines to 36 on 17 August.

### Finding D — the do-not-mix pairs are stable across four days

| | 17 Aug | 02 Sep #1 | 02 Sep #2 | 03 Sep |
|---|---:|---:|---:|---:|
| Demand lines | 24,357 | 23,462 | 23,003 | 21,909 |
| Lanes | 360 | 368 | 367 | 369 |
| Pairs with demand | **4** | **4** | **4** | **4** |
| Lines segregated | 2,849 | 2,289 | 2,283 | 2,279 |
| Strict groups / split | 387 / **17** | 395 / **17** | 394 / **17** | 397 / **17** |
| FlaggedVsNormal groups / split | 365 / 5 | 374 / 6 | 373 / 6 | 375 / 6 |
| Site 2028 unflagged lines | **0** | **0** | **0** | **0** |

Three properties hold on every day and are therefore properties of the customer:
the same four pairs carry demand; site 2028 has no unsegregated stock; exactly 17 lanes
split under `Strict` despite the lane count moving.

**The 18 idle pairs are not idle planners.** Nineteen of the twenty planners named in the
list ship heavily — S23 moves around 900 lines from site 2027, S97 nearly 1,800 across
2029 and 2032. The pairs are idle because that planner-and-site *combination* has no
demand. **S30 is worth watching:** absent on 17 August, present in all three September
files. A pair going live would change the group counts.

### Finding E — two data-quality issues in the September extracts

**The 3 September product master has thirteen columns**, not twelve: a lowercase
`strength` alongside `Strength`. All 20,317 rows agree, so no value is wrong, but the
reader was silently taking the *last* occurrence. It now warns and takes the first. [x] Done (segregation branch, `testProductHeaders.cpp`).

**The placeholder filename changed case** — `PlaceHolder-1.json` became
`Placeholder-1.json`; the directory stayed `PlaceHolder`. Harmless on Windows, fatal on
Linux. Now covered by a test. [x] Done (segregation branch).

**The product master drifts between extracts:** 116 new IDs, none removed, and 39 then 71
existing products with changed field values. **Module 3 tests should assert distributions
and counts, not individual products' CRI or weight.**

---

## 5. Modules 3 to 7

Modules 1 and 2 are built; their specifications are in the code and tests. What follows
is the remaining work.

### Module 3 — `stackRules` (tier 1, 3 days) — [x] done, merged

**Owns** the predicates deciding whether one unit load may sit on another.
**Never** throws, logs, allocates or chooses. Pure functions, like M1's Converter. All
four strategies in `stackBuilder` call in here; none reimplements a rule.

```cpp
struct UnitLoad {
    std::string id;
    double footprintLengthIn;   // from params by pallet type - NOT Length/Width
    double footprintWidthIn;
    double heightIn;            // Height * Layers_Unit_Load + pallet deck
    double weightLb;            // Weight * Cases_Unit_Load + pallet weight
    double ownWeightAboveLb;    // supplied if available, else (Layers_Unit_Load - 1) * Cases_Layer * Weight
    int    cri;                 // 0 = blank
};

struct StackFeasibility {
    enum class Reason { Ok, HeightCeiling, CriExceeded, BlankCri, FootprintMismatch };
    bool   isFeasible;
    Reason reason;
    double marginInOrLb;
};
```

Checks, in this order:

1. **Height** — `base.heightIn + top.heightIn <= 108`. Cheapest, rejects 96.8%.
2. **Blank CRI** — not stackable until Tom rules otherwise. One master row.
3. **CRI weight** — `base.ownWeightAboveLb + top.weightLb <= safeLimit(base.cri)`, top
   weight including its pallet.

`BottomOnly` is gone — this customer has no own-stack restriction.

**Two arithmetic traps.** `Weight` is per *case*, median about 9 lb; a unit load is
`Weight × Cases_Unit_Load`, and misreading it understates by sixty times so every stack
passes. `Length`/`Width` are case dimensions, not footprint.

**Also owed here:** the pallet-id completeness check. Module 1 dropped the hardcoded
`{TLD, PTL, PGM, GMA}` whitelist, so `"PTL "` with a trailing space is now a valid
distinct id that silently fails to match. `stackRules` calls `palletSpecFor` once per
demand line and must treat a `nullptr` as an error, reporting every pallet type present
in the joined demand that has no spec in the config.

**Module 3 checklist** (from branch code):

- [x] `UnitLoad` / `StackFeasibility` in `stackTypes.hpp`; pure `canStack`
- [x] Check order: height, blank CRI, CRI weight
- [x] Supplied weight-above preferred over derived
- [x] Pallet-id completeness check (`missingPalletIds`, exact ids, all reported together)
- [x] Whole-rules regression on the 17 August data: 65,295 and 43,021 of 2,016,400 pairs (other days and the 110 in sensitivity check not covered)
- [x] Blank-CRI base honours `blankCriIsStackable` (skips the CRI weight check when true)
- [x] Merged to `main`

**Tests.** Assert the measured distribution as a whole-rules regression: 65,295 of
2,016,400 pairs pass height, 43,021 pass both. Adversarial: CRI 0, 11, negative; the
blank strength row; the 33 products failing their own CRI check — only 1 is demanded on
17 August; zero, negative, NaN and Inf for height, weight, layers, cases per layer and
cases per unit load; `Layers_Unit_Load` of 0 and 1; a unit load taller than 108 alone.

### Module 4 — `bindingConstraint`, Pass 1 (tier 2, 2 days) — [x] done, merged

Decides per group whether cube or weight binds, before any stack exists.

```
trucksIfWeight = totalWeightLb / weightLimitLb
trucksIfCube   = totalPallets  / stackPositions
binding        = trucksIfWeight > trucksIfCube ? Weight : Cube
```

Crude is fine: the output is one of two labels, not a truck count. Measured on 17 August
under `Strict`: **384 cube-bound, 3 weight-bound** — not close, which is the evidence the
estimate is safe. Both figures depend on the still-provisional 45,000 lb and 30 positions.

**Module 4 checklist:**

- [x] `assessBinding` in `bindingConstraint.hpp`: one `GroupBinding` per group, in group order
- [x] Cube vs weight rule as above; an exact tie counts as cube-bound
- [x] Every line counted, stackable or not
- [x] Negative and non-finite line figures are excluded and counted, not summed
- [x] Bad trailer spec (weight limit or positions not positive), mismatched vectors and out-of-range indices throw `std::invalid_argument`
- [x] Real data, `Strict`: 387 groups, 384 cube-bound, 3 weight-bound
- [x] Merged to `main`
- [ ] Read the trailer from params by code once more than one trailer exists (the caller passes one `TrailerSpec` today)

Non-stackable products must reach this module, not only Pass 2 — a single-high product
consumes a floor position a stackable one would share.

### Module 5 — `stackBuilder`, Pass 2 (tier 3, 5 days) — [x] done on branch

Generates candidate stack sets within a group and keeps the best. New build.

**Naming to settle with Tom.** There are currently three vocabularies for overlapping
ideas: my four strategies, Tom's Pass 2 note ("horses with jockeys", equal-weight,
maximised weight difference), and Truck Builder's five documented methods — Natural,
Target, Tall & Heavy, Base & Top, Try Hard. **Ask whether Order Builder's strategies
should mirror the T3 names** before writing them.

**Module 5 checklist:**

- [x] `buildStacks` in `stackBuilder.hpp`: one `GroupStacking` per group, in group order, with the best `StackSet` and every method's floor positions
- [x] Five methods named after T3's: Natural, Target, Tall & Heavy, Base & Top, Try Hard
- [x] Stacks of any depth; every level is checked with `canStack`, counting all weight and height above it. No rule is reimplemented
- [x] Attempt cap from `pass2AttemptCap` bounds Try Hard; a cap of 0 skips it
- [x] Fewest floor positions wins; ties go to the lower heaviest stack when the group is weight-bound
- [x] Lines that cannot become a unit load, and lines with negative or non-finite quantity, are reported and left out
- [x] Every pallet placed exactly once, no stack over the ceiling, and repeatable output, checked on the real 17 August data
- [x] Whole day (387 groups, all five methods) completes well inside the 10 second budget; the whole real-data test takes about 3 s including loading
- [ ] Merge to `main`
- [ ] Tom to confirm the method definitions (see below)
- [ ] Choose the attempt cap by measurement; the config default of 4 is unmeasured

**The five methods are Order Builder's reading of T3's names, not T3's algorithms.** Each is the
same greedy builder with a different base order and top order: Natural uses input order for
both; Target puts the tallest load that fits on top; Tall & Heavy takes the heaviest, tallest
bases and the shortest tops; Base & Top takes the highest crush rating as base and the lightest
as top; Try Hard reruns Base & Top from rotated starting bases up to the attempt cap.

**Quantities are pallet-equivalents, so stacking is fractional.** A line of 4 short pallets can
report 4/3 floor positions when a real trailer would need 2. This follows the standing rule that
fractions are summed and never rounded, so the figures are estimates, and they understate
floor use when a group has few pallets per product.

Given Finding A, expect most groups to return mostly singles. A group of tall products
produces one valid set with no pairs. That is correct, not a failure, and the reporter
must say so plainly.

**Performance.** O(n²) pairing against a 478-line largest group under 10 seconds per
shipment. Height filter first; sort once by height before pairing, not per strategy;
`reserve` the candidate vector; pass `UnitLoad` by `const&`. Record runtime on the real
largest group as a test.

### Module 6 — `stackReporter` (tier 4, 2 days) — [ ] not started

Prints groups and stacks so Tom can confirm boundaries without reading C++. Computes
nothing. Presentation only, as M1's Reporter.

Must print `defaultedKeys` from `params` (field exists, [x] in params; printing is [ ] not done) — that is what turns a misspelled config key
into a visible line rather than a silent default.

### Module 7 — pipeline integration (tier 4, 2 days) — [ ] not started

```
import -> join -> convert -> validate -> segregate -> pass1 -> pass2 -> report
```

Conversion still precedes validation because the large-line check needs pallet figures.
**Do not reorder.** `main()` parses arguments only. `PipelineResult` stays move-only.
`main` in exactly one translation unit. Three CMake targets unchanged.

---

## 6. Still open

| # | Question | Blocks | Status |
|---|---|---|---|
| 1 | Which do-not-mix reading — strict or flagged-vs-normal? | reporting | Both built and tested. Decides which output is shown. Stable across four days. |
| 2 | Pallet-type footprint table | `params` | Not derivable (Finding B). Default 48x40 and flag. |
| 3 | Maximum weight above dataset | `stackRules` | Tom is sending it. Derive from CRI meanwhile. |
| 4 | Weight limit and stack positions | `bindingConstraint` | 45,000 lb / 30 provisional. The 384/3 split rests on them. |
| 5 | Pass 2 attempt cap | `stackBuilder` | Pick, measure, report. |
| 6 | Strategy naming vs T3's five methods | `stackBuilder` | Ask before writing. |
| 7 | The 33 self-failing products | `stackRules` | Warn and continue. One demanded on 17 Aug. |

**Closed since Revision 2:** trailer height (108 in), own-stack (not applicable to this
customer), variant resolution method (divisibility, 70–75% effective).

### Not estimated here

Two items from the 2 September call are new scope and are **not** in the 19 days:

- **The large-line threshold moving from 300 to about 3,000 pallets.** The current
  threshold covers roughly 10 trucks; real lanes run to 42+ trucks and single-item orders
  to 100+.
- **The sampling algorithm for large lanes** — sort the pallet list by the primary
  constraint and take every Nth, so the sample spans the range of characteristics rather
  than clustering.

The T3/P3 API arriving fourteen weeks early is a scope decision for Tom; this breakdown
assumes M2 stays self-contained.

### On the estimate

The modules total **19 working days against 15** in the three-week figure, which was set
when Phases 2–3 were expected to adapt Tom's stack-building code. He has confirmed the
code will not be shared, though he has since supplied 14 pages of Truck Builder technical
documentation — five methods, class hierarchy, algorithm flowcharts — which partially
offsets it. Documentation of an implementation is not the implementation, so the gap
stands and should be re-confirmed rather than reported as a slip later.

---

## 7. Assumptions still in force

- A lane is ship-from + ship-to + ship condition. `SHIP_COND` adds nothing on any of the
  four days — three-part and two-part lane counts are identical every time — but it stays
  in the key because it is part of the lane definition.
- Pallet deck height is taken as 5.5 in for PTL and PGM, 0 for TLD and GMA. **Assumed** —
  Tom confirmed the 60 lb weight, never a thickness. This matters more now that the
  ceiling is a confirmed 108 rather than an assumed 110.
- Pallet footprint defaults to 48x40 for all types pending Tom's table.
- Floor truck counts are volume divided by capacity — a lower bound for comparing rules,
  not a plan.
- Pallet-equivalents are summed as fractions across a group, never rounded per line.
- The blank `PLANNER_SNP` is an edge case, not an invariant: 1 line on 17 August, 0 in all
  three September files.
