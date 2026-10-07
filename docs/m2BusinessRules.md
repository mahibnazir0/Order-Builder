# Milestone 2 — Proposed Business Rules

This note sets out the rules Order Builder applies in Milestone 2 where the customer has not
yet confirmed the behaviour. Each section gives the proposed rule, the reasoning behind it, a
small worked example and the decision we need the customer to confirm. Every rule is a
configuration value or a single function, so a different answer from the customer is a small,
contained change.

**Decisions requiring customer confirmation** are collected in section 6.

---

## 1. Partial pallets: M1 accounting versus M2 physical counts

The two milestones count pallets differently, for different purposes.

| | M1: pallet-equivalents | M2: physical pallets |
|---|---|---|
| Purpose | Volume totals per lane and day; cube-versus-weight decision | Floor positions in a trailer |
| Per line | `cases ÷ Cases_Unit_Load`, fraction kept | Pallet-equivalents rounded **up** to whole pallets |
| Summed across a lane | Fractions summed, never rounded | Whole pallets summed |
| Config | Always on | `stackWholePallets: true` (shipped) |

**Proposed rule.** M1 totals stay fractional. For stacking, each demand line is rounded up to
whole pallets independently (`stackedPalletsForLine`), and only whole stacks are committed.
A partial pallet is stacked at its full unit-load height, which is conservative.

**Rationale.** A part pallet still occupies a whole floor position, so a fractional stack
count understates trailer use: one pallet must take one position, not 0.5. Rounding per
line, rather than per lane, is correct because two partial pallets of different products
cannot be merged onto one pallet.

**Worked example.** Product P has 16 cases per unit load. Two lines on one lane:

| Line | Demand | M1 pallet-equivalents | M2 pallets stacked |
|---|---|---:|---:|
| 1 | 36 CS of P | 2.25 | 3 |
| 2 | 0.5 PAL of Q | 0.50 | 1 |
| **Lane total** | | **2.75** | **4** |

With `stackWholePallets: false` the stacks would hold 2.25 and 0.5, which is kept only for
comparison and is labelled in the report as not a physical count. On 17 August the
whole-pallet figure is 129,785 floor positions against a fractional 129,526.8.

**How this is verified.** The guarantee test checks, for every demand line on all four real
days, that M1 pallet-equivalents reach the groups unchanged, and that each line's stacks hold
exactly `ceil(pallet-equivalents)` pallets in whole mode and exactly the fraction in
fractional mode, including lines with a partial pallet (`tests/testMilestone2Guarantees.cpp`).

**To confirm:** that partial pallets should be rounded up per line for stacking, and that a
partial pallet should be treated as full height.

---

## 2. Every demand line is accounted for

**Rule (implemented).** A run is complete only when every demand line is in a stack. Any
line that is not, for whatever reason, is listed in the report by line number and material
with its reason, the run is reported `INCOMPLETE`, and the program exits with code 1. This
holds even when every other line stacks successfully.

Reasons a line can be left out:

| Reason | Example |
|---|---|
| Rejected by validation | Material not in the product master; raw material with zero dimensions |
| Unit of measure cannot be converted | `EA`; only `CS`, `PAL` and `DIS` convert to pallets |
| No unit load can be built | Missing pallet specification; invalid strength or weight |
| Taller than the trailer ceiling | One pallet over 108 in |
| Invalid quantity | Negative or not a number |

**Worked example.** Two lines: material GOOD, 1 PAL, and material GOOD, 5 EA.

```
  Result                    INCOMPLETE: 1 demand line(s) are in no stack
      line 1, material GOOD: unit of measure 'EA' cannot be converted to pallets (supported: CS, PAL, DIS)
```

Exit code 1. The PAL line is still stacked and shown, so the planner sees both the partial
result and what is missing.

---

## 3. Pallet-variant selection

18 product IDs appear more than once in the product master, once per pallet type (TLD, PTL,
PGM, GMA), with the same dimensions and weight but a different `Cases_Unit_Load`. The demand
file has no pallet-type field, so the variant has to be inferred.

**Current behaviour.** A fixed preference order, TLD, then PTL, PGM, GMA, picks the variant,
and every such line is flagged (144 lines, 0.59%, on 17 August). The report prints the count
so no figure is presented as settled.

**Proposed rule.** Following the customer's statement that goods ship in whole pallets:

1. If exactly one variant divides the ordered case quantity evenly, use it.
2. If several divide evenly, or none does, fall back to the preference order and keep the
   line flagged.
3. A line that ships a partial pallet under its chosen variant is rounded up as in section 1.

**Rationale.** Measured on all four days, the divisibility rule resolves 69–75% of ambiguous
lines, and fewer than 0.1% of all lines are not a whole number of pallets, so the premise
holds. The remainder cannot be resolved arithmetically: for 105553001 (GMA 84 / TLD 168),
every valid TLD quantity is also a valid GMA quantity.

**Worked examples.** Product 105553001, GMA 84 cases per pallet, TLD 168.

| Demand | GMA | TLD | Divisibility rule | Current preference order |
|---|---|---|---|---|
| 252 CS | 3 pallets ✓ | 1.5 ✗ | **GMA, 3 pallets** | TLD, 1.5 → 2 pallets |
| 336 CS | 4 pallets ✓ | 2 ✓ | Ambiguous → TLD, 2 pallets, flagged | TLD, 2 pallets |
| 100 CS | 1.19 ✗ | 0.60 ✗ | No fit → TLD, 0.60 → 1 pallet, flagged | TLD, 1 pallet |

**Status.** The divisibility rule is not implemented yet. We propose implementing it once the
customer confirms rules 1 and 2. Until then the preference order applies.

**To confirm:** the divisibility rule; the fallback order when it cannot decide; and whether
the customer can supply the pallet type for the 105553001-style products, which no rule can
infer.

---

## 4. Do-not-mix (DNM) interpretation, including site 2028

Each DNM entry is a pair: a planner (`PLANNER_SNP`) and an origin site (`LOCFRNO`). A demand
line is *flagged* when its planner and origin match a pair. A lane is origin, destination and
ship condition. Two readings are built and selectable with `doNotMixReading`:

| Reading | Groups per lane |
|---|---|
| `Strict` (shipped) | Normal stock, plus **one group per flagged planner** |
| `FlaggedVsNormal` | Normal stock, plus **one group for all flagged planners together** |

The readings differ only where two or more flagged planners share a lane. On the real data
that happens at **site 2028**: planners S45, S01 and S03 are all flagged there, and the site
has no normal stock on any of the four days. This accounts for the difference between 17
split lanes under `Strict` and 5–6 under `FlaggedVsNormal`.

**Proposed rule.** `Strict`: a flagged planner's stock is never mixed with any other stock,
flagged or not.

**Rationale.** "Do not mix" reads most naturally as keeping that planner's stock on its own.
If `Strict` is wrong, the cost is some extra floor positions on a few lanes. If
`FlaggedVsNormal` is wrong, the plan mixes stock the customer asked to keep apart. The safer
error is the one we propose.

**Worked example.** Lane 2028 → 3001, TL. Lane 2027 → 3001, TL. Pairs: (S45, 2028),
(S01, 2028), (S23, 2027).

| Line | Origin | Planner | Flagged? |
|---|---|---|---|
| 1 | 2028 | S45 | yes |
| 2 | 2028 | S01 | yes |
| 3 | 2027 | S23 | yes |
| 4 | 2027 | S10 | no |
| 5 | 2027 | S45 | **no**: S45 is flagged only at 2028 |

| | `Strict` | `FlaggedVsNormal` |
|---|---|---|
| 2028 → 3001 | {1}, {2}: 2 groups | {1, 2}: 1 group |
| 2027 → 3001 | {4, 5}, {3}: 2 groups | {4, 5}, {3}: 2 groups |

Only the 2028 lane differs. The behaviour is covered by the unit tests in
`tests/segregation/testSegregation.cpp`.

**To confirm:** at site 2028, may S45, S01 and S03 share stacks with each other (they never
meet normal stock), or must each be kept on its own?

---

## 5. The five stacking methods

Truck Builder documents five methods by name. We do not have their algorithms, so each
method below is **Order Builder's own definition**, chosen to match its name. All five use
the same greedy builder; they differ only in the order in which bases and tops are tried.

**Common rules for every method:**

- A load may go on top only if the combined height stays within the trailer ceiling
  (108 in), and every load beneath can carry the weight above it under its crush rating
  (CRI). A load with a blank CRI carries nothing.
- A product whose own upper layers already exceed its CRI limit — `(layers − 1) × cases per
  layer × case weight` above the table value — cannot carry anything. It still ships
  single-high and is reported as `Over own CRI lines`, a warning that does not make the run
  incomplete. 32 to 33 products per master are like this; on 17 August two demand lines
  (MATNR 106005500) name one.
- Stacks are at most `maxStackHeight` high (2 for this customer).
- Taking each base in base order, the builder adds the first top in top order that fits, then
  commits as many identical stacks as the scarcest line allows.
- Each group is built with every method and the method using the fewest floor positions
  wins. If two methods tie and the group is weight-bound, the one with the lighter heaviest
  stack wins; otherwise the first in the order below wins.

| Method | Base order | Top order | Intent |
|---|---|---|---|
| Natural | Input order | Input order | Baseline; the planner's own sequence |
| Target | Input order | Tallest first | Fill each stack toward the ceiling |
| Tall & Heavy | Heaviest first, then tallest | Shortest first | Heavy, tall loads carry; short loads ride on top |
| Base & Top | Highest CRI first | Lightest first | Strongest loads carry; light loads ride on top |
| Try Hard | Input order, rotated to several start points | Lightest first | Base & Top's top order from several starting bases; repeats `pass2AttemptCap` times (4) and keeps the best |

**Worked example.** One group, one pallet each, at most two high, 108 in ceiling.

| Load | Height | Weight | CRI | Can carry |
|---|---:|---:|---:|---:|
| A | 70 in | 500 lb | 9 | 3,099 lb |
| B | 40 in | 900 lb | 2 | 299 lb |
| C | 35 in | 200 lb | 9 | 3,099 lb |
| D | 60 in | 300 lb | 5 | 1,149 lb |

Feasible pairs (base / top): A/C and C/A (105 in); B/C (75 in, 200 ≤ 299) and C/B; C/D and
D/C (95 in); D/B (100 in, 900 ≤ 1,149). Not B/D, since D's 300 lb exceeds B's 299 lb, and
not A/B or A/D in either order, which exceed the ceiling.

| Method | Stacks built | Floor positions |
|---|---|---:|
| Natural | A/C, B, D | 3 |
| Target | A/C, B, D | 3 |
| Tall & Heavy | B/C, A, D | 3 |
| **Base & Top** | **A/C, D/B** | **2** ← chosen |
| Try Hard | best of 4 rotations: 3 | 3 |

Base & Top wins because it puts the weak, heavy load B on top of D instead of using it as a
base. This example is pinned by the test "documented worked example gives each method's floor
positions" in `tests/stackBuilder/testStackBuilder.cpp`.

**To confirm:** whether these definitions match Truck Builder's intent for each name; whether
any method should be added or dropped; the tie-break; and the Try Hard attempt cap of 4.

---

## 6. Decisions requiring customer confirmation

| # | Decision | Proposed | Effect if different |
|---|---|---|---|
| 1 | Round partial pallets up per line for stacking | Yes (`stackWholePallets: true`); M1 totals stay fractional | Config change |
| 2 | Stack a partial pallet at full unit-load height | Yes | One-line change in unit-load height |
| 3 | Pallet variant: divisibility first, then the preference order TLD, PTL, PGM, GMA | Yes, flagged when unresolved | Joiner change, not yet built |
| 4 | Pallet type for products no rule can resolve (e.g. 105553001) | Customer to supply, if available | Data change |
| 5 | DNM reading: `Strict`, with each flagged planner on its own, including the three at site 2028 | `Strict` | Config change (`FlaggedVsNormal`) |
| 6 | Definitions of the five stacking methods and the tie-break | As in section 5 | Contained to `stackBuilder` |
| 7 | Try Hard attempt cap | 4 | Config change |
| 8 | A run with any unstacked demand line is `INCOMPLETE` (exit 1) | Yes | — |
| 9 | A product over its own CRI limit ships single-high with a warning | Yes | Reject instead: one rule in `stackBuilder` |
| 10 | Placeholder entries repeating a lane are summed per lane | Yes, counted in the log | De-duplicate or overwrite: placeholder importer |
