---
name: robustness-review
description: Audit Order Builder changes for the defect classes past reviews and the customer design documents exposed — bad external input read as absent or unvalidated, lines or pallets lost while the run exits 0, copy-unsafe pointer structs, hard-coded limits, invalid truck floors, unbounded or silently-exhausted loops, hard rules not held end to end, Truck Builder or Pallet Builder failures read as approval, repairs that worsen a load or oscillate, planner/Truck Builder coupling, non-deterministic or non-replayable runs, solver guarantees, date/key/platform input quirks, acceptance figures not pinned by tests, and PRs missing the team's required plan, self-critique and explanation or whose code drifts from that plan. Runs the build and test suite. Use before merging any change under src/, include/, config/, tests/ or CMakeLists.txt.
allowed-tools: Read, Grep, Glob, Bash(grep *), Bash(git diff *), Bash(git log *), Bash(git show *), Bash(gh pr view *), Bash(cmake --build *), Bash(ctest *)
---

# Robustness review

This project shipped a milestone with four defects that compiled clean, passed
the existing test suite, and only surfaced when someone deliberately fed the
pipeline malformed input. All four came from the same root cause: a check
that was assumed to exist somewhere else in the pipeline, and didn't.

Milestone 2 then shipped three more that this skill did not catch, because it
only looked at the M1 files and only at the input boundary:

- a demand line with Strength 11, a Weight of 0, or a pallet type with no
  params spec passed validation, was dropped by `buildUnitLoad`, produced no
  stack — and the program still exited 0 (Checks 6 and 7);
- one physical pallet was reported as 0.5 floor positions, because stacking
  worked in fractional pallet-equivalents (Check 8);
- provisional business rules (pallet-variant fallback, do-not-mix reading,
  our interpretation of the five T3 stacking methods) were documented only in
  `docs/`, so the output read as settled (Check 9).

Checks 10–15 come from the customer-facing design documents, which state
rules the code must hold and which are easy to break without any test
failing (see **Source documents** at the end):

- every size, limit and rule is a setting, never a constant (Check 10);
- a stack's footprint is the larger of the load and its pallet (Check 11);
- the truck floor is per segregated group, summed, against the largest
  trailer (Check 12);
- every search loop has a cap, and hitting it is reported (Check 13);
- hard rules hold through every later stage, not only where they were
  computed (Check 14);
- the same input gives the same output, and any run can be replayed from
  its records (Check 15).

Checks 16–24 close gaps a review of this skill against the same documents
found — edge cases the documents imply but no earlier check named:

- every pallet is placed exactly once, and no result beats the floor
  (Check 16);
- a wrong unit of measure that yields a plausible, inflated count (Check 17);
- dimension fits compared with an explicit tolerance (Check 18);
- stack height, stack weight and truck weight rules (Check 19);
- highest priority first, lowest priority dropped first (Check 20);
- empty, zero-total, tied and odd-count inputs (Check 21);
- both pallet orientations, and pattern counts that outgrow enumeration
  (Check 22);
- CRLF, BOM and decimal-comma input across Windows, Ubuntu and countries
  (Check 23);
- the customer's acceptance figures pinned as regression tests (Check 24).

Checks 25–30 come from a second pass over the same four documents, looking
for edge cases they imply at the seams *between* stages rather than inside
one:

- the binding limit is decided once per lane-day and every later stage,
  including the filler, works to it (Check 25);
- a repair move never leaves a load worse on any rule (Check 26);
- a Truck Builder timeout, error or malformed reply is never an approval
  (Check 27);
- dates, the planning horizon and placeholder date ranges (Check 28);
- duplicate or ambiguous keys in the product master, placeholders and the
  do-not-mix list (Check 29);
- a lane whose floor cannot be measured, or whose fixed trucks are fewer
  than its floor, is named rather than reported as solved (Check 30).

Checks 31–33 come from a third pass, checking every requirement in the four
documents against the check that owns it. Three areas had no owner:

- the Stage 2 solvers themselves — every truck holds **both** limits, not
  only the binding one; knapsack, first-fit-decreasing and column
  generation each keep their own guarantee; selector thresholds behave at
  their boundaries (Check 31);
- the do-not-mix list read with the right semantics (symmetric, not
  transitive, typo-proof), and a hard rule that can be switched off per
  customer (Design §14) only ever switched off explicitly and visibly
  (Check 32);
- the build and the test suite actually run as part of the review, since
  every milestone is judged on "builds clean, tests pass" (Check 33).

The same pass added edge cases to existing checks: the filler loop's G→C
re-entry and the "whole load refused" cascade (Check 13), cross-load swaps
(Check 26), layout caches keyed by trailer (Check 22), CSV column shifts
(Check 23), partial pallets (Check 8), lanes with placeholders but no demand
(Check 30), unknown countries/trailer types (Check 19), soft rules never
overriding hard ones (Check 14), and the baseline figures for the floor gap
and side overhang (Check 24).

A fourth pass re-read the figures, not only the text: Algorithms Fig. 1, 2
and 4 and Side Overhang Fig. 2 and 3. It added Check 34, Pass 2's three
stacking strategies and how the best attempt is chosen. It also added edge
cases to existing checks:

- a `ceil` on a float ratio that turns exactly one truck into two (Check 12);
- a mixed fleet, where the floor divides by the largest trailer but each
  real truck must be planned against its own trailer (Check 12);
- the Truck Builder budget counted per shipment (Check 13);
- Pass 1 comparing truck counts in the right direction, and a later day's
  limit recomputed after pull-forward takes its lines (Check 25);
- pull-forward eligibility (`CanGo`), with the window measured from the
  original due date (Check 14);
- row depths summed against the trailer length (Check 22);
- repair moves that add goods or use an extra floor position, and the
  playbook's thresholds given in metres (Check 26);
- unknown or inverted Truck Builder scores (Check 27);
- conservative knapsack rounding that makes an exactly-full truck infeasible
  (Check 31).

A fifth pass read the Design's part list (§9) and its Truck Builder score
table (§10.2) against the checks. Only side overhang had a repair check, and
nothing covered how the parts depend on each other. It added:

- the module boundaries §9 requires: the planner does not know Truck Builder
  exists, and the repair controller is tested against recorded answers
  (Check 35);
- repairs driven by every Truck Builder score, not only side overhang. A fix
  for one score must not break another, rules added by repair must persist,
  and two repairs must not undo each other in a loop (Check 36).

It also added edge cases to existing checks:

- Pallet Builder is a second external service, and its failures are held to
  the Check 27 rule;
- a self-check that refuses every plan (Check 27);
- a half-written output file on a failed run (Check 6);
- settings layered by country and customer (Check 10);
- stock availability shared between lanes that run in parallel, and filler
  that follows the pull-forward rules (Check 14);
- a replay that asks Truck Builder a question the record does not hold
  (Check 15);
- enumerated and generated layouts that disagree (Check 22);
- more pinned figures: the Fig. 4 example (198 in → 18 in) and the five
  largest lanes (Check 24);
- a receiving day whose binding limit flips once filler arrives (Check 25);
- neighbours in a pinwheel row, and a severity threshold that differs by
  trailer or region (Check 26);
- code supplied by the customer, reviewed like our own (Check 33).

A sixth pass compared the documents with the fields the demand feed actually
carries (`include/demand_types.hpp`). The documents talk about priority,
due dates and stock availability as if each were one clear value. The feed
has a 0–11 `TPRIO` with no stated direction, four different dates per line,
and `AVAIL_QTY`, which is below `TRANS` on every line. This pass also found
that Check 1 had called `get_or` safe for scalars, and it is not. It added:

- scalar fields read with `get_or<int>` truncate `2.9` to `2` and `true` to
  `1`, and a missing field quietly becomes a real value such as priority 0
  (Check 1);
- the unit of every ratio threshold, such as whether a gap of 5 means 5 %
  or 5 trucks (Check 10);
- pull-forward and filler that add a truck instead of filling a spare
  position, and a line only partly pulled forward (Check 14);
- a worker thread that throws, and totals summed in the order threads
  finish (Check 15);
- which way the priority scale runs, and priority that is missing or out
  of range (Check 20);
- an answer cache that outlives a run while Truck Builder changes
  (Check 27);
- the real date formats, the four dates on a demand line, impossible
  calendar dates, and a weekday-only `CTL` schedule (Check 28);
- more fixed trucks than the goods can fill (Check 30);
- `PLANNER_TRANS_NMIX` and other keep-apart attributes that are read and
  never used (Check 32).

A seventh pass read the team's AI coding workflow (*AI Coding V2*, see
**Source documents**). It asks for a written plan before any code, a
self-critique from a separate prompt, a block-by-block explanation before
merge, and tests, and it rejects a PR that lacks any of them. It also names
areas where AI should not make the decision. This pass added:

- the PR carries its plan, self-critique and explanation, and the code
  matches them. Review becomes "does the code match the plan?" instead of
  reverse-engineering intent from the diff (Check 37);
- a change to the architecture, or to security or credentials, names the
  person who made that decision (Check 38);
- this review is itself the separate critique, so it notes when it runs in
  the session that wrote the code (Scope);
- a defect class no check covers goes back into this skill or `CLAUDE.md`,
  the shared context every session loads (Reporting).

Changes that touch only `tests/` or `CMakeLists.txt` are also in scope now.
Weakening a test, or dropping a warning flag, is a regression that no source
diff shows.

The file paths and field names in each check's grep are **examples from the
milestone that first exposed the defect, not the scope**. Run every grep
across all of `src/` and `include/` — M2 shipped three defects precisely
because this skill once only looked at M1 files.

This skill re-runs that specific audit — it is not a general code review.

Read [include/pipeline.hpp](../../../include/pipeline.hpp),
[include/validator.hpp](../../../include/validator.hpp),
[include/json_util.hpp](../../../include/json_util.hpp),
[src/stackRules/stackRules.cpp](../../../src/stackRules/stackRules.cpp), and
[src/stackReporter/stackReporter.cpp](../../../src/stackReporter/stackReporter.cpp)
first if you have not already — they contain the reference fixes every check
below points back to.

## Scope

1. If the user gave an argument (a branch, PR number, or commit range), review
   that. Otherwise run `git diff` (working tree) and, if that is empty,
   `git diff main...HEAD`.
2. Pull the list of changed files out of that diff. If any changed file is
   under `src/` or `include/` (M1: importers, converter, validator, reporter,
   joiner; M2: params, segregation, bindingConstraint, stackRules,
   stackBuilder, stackReporter; M3 onward: the floor, case selector,
   knapsack / bin packing / cutting stock, floor-pattern generation,
   multi-day planning, Truck Builder and Pallet Builder clients, repair
  controller, results and records; plus
   `src/pipeline.cpp`, `src/main.cpp`), or
   is a record header — note both naming styles, `include/*_types.hpp` (M1)
   and `include/*Types.hpp` (M2) — or is `config/*.json`, or is under
   `tests/` (a deleted or weakened assertion, Checks 5 and 24), or is a
   `CMakeLists.txt` (a dropped warning flag or test target, Check 33), run every check
   below against the CURRENT state of the repo (not just the diff hunks) — a
   gap these checks look for is usually an absence, and an absence never
   shows up in a diff.
3. If none of those files changed, say so and stop; this skill has nothing to
   check.
4. Otherwise build and run the suite first (Check 33). A build or test
   failure is itself a finding, and the checks below read more reliably
   against code that compiles.
5. Find the PR's plan, self-critique and explanation (Check 37). For a PR,
   read its description with `gh pr view <number> --json title,body,files`
   and any file it links to. For a working-tree or branch review with no PR,
   look in the user's message and in a plan file the branch adds. Read the
   plan **before** the code, so the code is judged against what it was meant
   to do, not against what it happens to do.
6. The workflow keeps generating and critiquing in separate prompts, because
   a model reviewing its own fresh output tends to confirm it. If this
   session wrote or edited the code under review, say so at the top of the
   report and recommend re-running the review in a fresh session.

## Check 1 — a present-but-wrong-type JSON block must not read as absent

Every place external JSON is read for a top-level array block (`STR`, `CTL`,
`DNM`, `PHOLDER`, or any new block) must go through
`get_optional_array()` from `include/json_util.hpp`, not a hand-rolled
`root.contains(key) && root[key].is_array()`. The hand-rolled form silently
treats "the key is present but is an object/string/number" the same as
"the key is absent" — this repo already shipped that exact bug once (a
`"STR"` block given as a JSON object produced zero demand lines and a clean
exit).

```bash
grep -rn 'contains(\|is_array()\|is_object()' src include
```

This includes every JSON reader, not only `src/importer/`: `config/*.json`
via `paramsLoader`, and from M7 the Truck Builder client's responses (around
two dozen scores per load, Design §10.2) — a reply with a score block of the
wrong shape must be an error, never "no objections".

For every match, confirm it either uses `get_optional_array` or the field
being checked is a *scalar* read through one of the helpers below.

Scalars have their own version of the same defect. `get_or<T>` returns the
fallback with **no log at all** when a key is missing, and logs only at
debug level on a wrong type. nlohmann's `get<int>` also converts instead of
refusing: `2.9` becomes `2`, `true` becomes `1`, and `get<double>` turns
`true` into `1.0`. So:

- an **integer** field must be read with `getIntegerOr`, not
  `get_or<int>`. `getIntegerOr` exists for exactly this reason;
- a **required** field whose fallback is a legitimate value (priority `0`,
  a lead time of `0` days, an available quantity of `0`) must fall back to a
  sentinel the Validator rejects. The reference is `kUnreadableLoadCount`
  for `NO_OF_LOADS` in `placeholder_importer.cpp`. Otherwise a missing
  `TPRIO` reads as a real priority 0, and the Validator cannot tell;
- a fallback of `0` is safe only when the field is optional and `0` really
  means "none", and that is stated in a comment.

```bash
grep -rn 'get_or<int>\|get_or<long\|get_or<double>\|get_or<bool>' src include
```

**Fails if:** any new `if (root.contains("X") && root["X"].is_array())` (or
equivalent) pattern appears instead of `get_optional_array`; an integer
field is read with `get_or<int>`; or a required scalar falls back to a value
the Validator accepts.

## Check 2 — a divisor, multiplier, or running total needs range + finiteness validation

Grep for arithmetic on fields read from external input:

```bash
grep -rnE '[^/]/[^/*]|\*=|/=|\+=' src
```

The M1 fields (`cases_unit_load`, `weight_lb`, `no_of_loads`) are where this
was first found; the same applies to every numeric field on every record,
setting and Truck Builder score read later (heights, dimensions, weight
limits, stack positions, scores, caps).

For every numeric field used as a divisor (`/`), a multiplier, or accumulated
into a running total (`+=`), confirm `src/validator/validator.cpp` has a rule
that rejects:
- **negative** values (unless the field's real-world meaning allows negative
  — none currently do; case counts, weights, and load counts are all
  non-negative by definition)
- **non-finite** values (`!std::isfinite(...)`) for anything read as a
  `double` from JSON or CSV, since a literal `"nan"`/`"inf"` string parses
  successfully via `std::stod`/`nlohmann::json`

`negative_unit_load`, `invalid_weight`, `negative_load_count`, and
`zero_unit_load` in `validator.cpp` are the reference pattern — a Cases_Unit_
Load of `-10` or a Weight of `nan` must never reach `Converter::to_pallets`/
`to_weight_lb` or `Reporter::build`'s totals without an Error already on
record for that line.

**Fails if:** a new numeric field feeding a calculation has a zero-check but
no negative-check, or is a `double` with no finiteness check, or is `int`
with no bound at all.

## Check 3 — every field on a record type needs a validation owner

For every field on every record type — the M1 records (`STRRecord`,
`ProductRecord`, `PlaceholderRecord`, `CTLRecord`, `DNMRecord` in
`include/*_types.hpp`), the M2+ types in `include/*Types.hpp` (params,
segregation, stacks, and later trucks, patterns and Truck Builder
responses) — confirm something actually inspects it: `validator.cpp` for
demand/product data, `paramsLoader.cpp` for settings, the reading module
for anything newer — reading it in an
importer and summing it in the Reporter is not validation. This is exactly
how `PlaceholderRecord::no_of_loads` went unvalidated: it was read, summed
into `total_loads`/`trucks_requested`, and never checked by anything.

```bash
# For a field named FIELD, look for it outside its own struct/importer:
grep -rn 'FIELD' src include | grep -v 'Types.hpp\|_types.hpp'
```

**Fails if:** a field that flows into a total, a threshold comparison, or a
downstream decision has no hit that checks its range or type — only reads
and sums. (Purely descriptive
fields — free-text labels, dates used only for display — are exempt; use
judgment, but justify the exemption explicitly rather than skipping silently.)

## Check 4 — a struct holding raw pointers/references must have explicit copy semantics

```bash
grep -rnE 'const [A-Za-z_:<>]+ ?\*|[A-Za-z_>] ?& ?[a-zA-Z_]+;|std::reference_wrapper|string_view|span<' include src
```

Any struct with a non-owning pointer/reference member (like `JoinedLine::str`
/ `product` pointing into `DemandFile`/`ProductIndex`) must not rely on an
implicitly-generated copy constructor **if it — or anything that embeds it —
is ever copied while the pointed-to storage could plausibly outlive only one
of the two copies**. `PipelineResult` in `include/pipeline.hpp` is the fixed
reference case: it explicitly deletes copy, defaults move, because it owns
both `demand`/`products` (the pointed-to storage) and `join` (the pointers
into that storage) together, so a copy would leave the copy's pointers aimed
at the original's memory.

For any NEW struct that combines owned storage with pointers into that
storage in one aggregate, apply the same treatment. A struct whose pointers
reference storage owned by something else *external* to it (e.g. `JoinResult`
alone, copied while the original `DemandFile`/`ProductIndex` stays alive
elsewhere, as the test fixtures do) does not need this — only the aggregate
that owns both sides does.

**Fails if:** a new struct combines owned containers with pointers/references
into those same containers, and relies on the implicit copy constructor
without a comment or test establishing that it is never copied.

## Check 5 — malformed-input test coverage exists for what changed

For each gap the other checks did NOT find (i.e. the code is correct), confirm
a test exercises the malformed case, not just the valid one — grep the
relevant test file (`tests/importer/test_*.cpp`, `tests/validator/
test_validator.cpp`, `tests/test_end_to_end.cpp`) for a case matching the
new behavior. A validation rule with no test proving it fires is one
refactor away from silently regressing.

## Check 6 — every line dropped downstream must reach the exit code

Validation is not the only place a demand line can leave the result. Any later
stage that skips a line — a `continue` inside a per-line loop, a push onto an
`excluded*` / `overHeight*` / `invalid*` list, a counter such as
`excludedInvalidQuantityLines`, or a quantity silently treated as 0 — must be
counted in the run's completeness verdict, not only printed as a report line.

```bash
grep -n 'continue;\|excluded\|overHeight\|Invalid' src/*/*.cpp src/pipeline.cpp
grep -n 'return' src/main.cpp
```

For every skip path found, trace it to `isRunComplete()` in `src/pipeline.cpp`
(via `StackingResult::linesNotStacked()` / `StackReport::isComplete()`) and
confirm `main()` returns 1 when it fires. Also confirm the "nothing was
produced at all" case is covered: demand supplied, zero stacks built (e.g.
every line converted to 0 pallets because of an unknown UoM) must not exit 0.

The exit code is not the only signal a downstream reader uses. A run that
fails partway must not leave a plan or report file that looks finished: a
truncated file with no `Result` line, or the previous run's file left in
place. Write the output to a temporary name and rename it only after the
verdict is known. Or make the verdict the last line written, so that a
missing verdict itself means "incomplete".

**Fails if:** a stage can drop a line, or produce no output for non-empty
input, and the only trace is a log line or a report count while the exit code
stays 0; or a failed run leaves an output file that reads as complete. The
reference fix is `isRunComplete` + the report's `Result` line.

## Check 7 — the Validator must accept no value a downstream consumer rejects

A value can be in range for the Validator and out of range for the code that
consumes it later. `invalid_strength` is only a Warning, `invalid_weight`
allows exactly 0, and a missing pallet spec is only a log line — yet
`isBuildableProduct` in `stackRules.cpp` rejects all three, so the line
vanishes between validation and stacking. The M2 fix counts those rejections
toward completeness (Check 6) rather than tightening the Validator, because M1
does not need Strength and a raw-material row legitimately weighs 0.

For every predicate that makes a consumer refuse an input
(`isBuildableProduct`, `isStackableData`, `palletSpecFor(...) == nullptr`,
`requireAlignedInputs`, any `if (!valid) return error/continue`), list the
fields and the accepted range, then compare them with the Validator rule for
the same field in `validator.cpp`. Watch the boundaries: `> 0` versus `>= 0`,
`0..10` versus `1..10`, Warning versus Error.

**Fails if:** a value exists that the Validator passes (or only warns about)
and a consumer rejects, and that rejection is not counted by Check 6. Either
tighten the Validator rule to an Error, or make the consumer's rejection count
toward completeness — and say which, and why.

## Check 8 — counts of physical objects must be whole, or labelled as estimates

Pallets, stacks, floor positions, trailers and trucks are discrete. A figure
that counts them must not come out fractional (one pallet = 0.5 floor
positions) unless the output explicitly labels it an estimate.

```bash
grep -n 'quantity\|floorPositions\|pallets\|trucks' src/stackBuilder/*.cpp src/stackReporter/*.cpp src/bindingConstraint/*.cpp
```

For every quantity that is divided (`remaining / times`), split across
stacks, or summed into a physical count, confirm it is either rounded to whole
units at the right place (the reference is `stackWholePallets`: each line
rounded up with `std::ceil` before stacking, stacks committed with
`std::floor`) or printed with a label saying it is not a physical count.

Rounding a line up to whole pallets creates a **partial pallet**: planning
is at pallet level while demand may arrive in cases (Design §11.1). Confirm
the partial pallet carries the weight and height of the cases actually on
it, or that the report states it is costed as a full pallet. Costing it as
full overstates weight and cube, and can flip which limit binds (Check 25).
The report should count partial-pallet lines, because Design §14 names
"more volume arrives as cases than expected" as a risk to measure.

A single unstacked pallet on its own floor position is one position (the
"29.5 stacks" in Algorithms §2.2 means 29 stacks and one single pallet), not
half of one.

**Fails if:** a count of physical objects can be non-integer in the default
configuration without an "estimate" label in the printed output, a test
asserts a fractional physical count (e.g. `Approx(4.0 / 3.0)` floor
positions) as the default behaviour, or a rounded-up partial pallet is
silently given a full pallet's weight or height.

## Check 9 — provisional business rules must be visible in the output

A rule the customer has not confirmed — anything described in code or docs as
"provisional", "pending Tom", "our reading/interpretation of", "preference
order", "assumed", or "default until" — must be named in the run's printed
output, not only in `docs/` or a code comment. Otherwise a reader of the
report takes an assumption as a settled answer.

```bash
grep -rni 'provisional\|pending\|interpret\|reading of\|preference order\|assumed' src include config docs
```

For each hit, confirm the reporter prints it (the reference is the
`PROVISIONAL BUSINESS RULES` section in `stackReporter.cpp`, which names the
pallet-variant fallback with its line count, the do-not-mix reading in use,
the unconfirmed T3 method definitions, and the stacking basis).

The design documents list figures the customer has NOT yet confirmed. Any
code that depends on one of these is using a provisional rule, whether or
not the code calls it that:

| Open item | Source |
|---|---|
| Pass 1 cube divisor (read as ~28–30 stack positions on a 53 ft fat-thin trailer; `stackPositions` in config) | Algorithms Rev 5 §2.1, §5.3 |
| Pass 2 iteration cap before accepting the best stack set (`pass2AttemptCap`) | Algorithms Rev 5 §5.3 |
| Gap that escalates first-fit-decreasing to column generation | Algorithms Rev 5 §5.3 |
| Whether a single pallet may always fill the last open position | Algorithms Rev 5 §2.2, §5.3 |
| Side-overhang severity meaning, and what height difference is critical | Critical Side Overhang Rev 4 §6, §7 |
| Whether Truck Builder keeps the stack order Order Builder supplies | Critical Side Overhang Rev 4 §7 |
| Truck Builder re-score calls affordable per shipment | Side Overhang Rev 4 §7, Design §10.1 |
| Lane short of trucks: hold goods, or ship what fits and report | Design §15 |
| Pull-forward window per lane / customer | Design §5, §15 |
| Back overhang excluded as not physical — does it hold on a steep downgrade? | Side Overhang Rev 4 §7 |
| Acceptance bar: same trucks and fill at least as good (proposed), vs identical plan, vs no more trucks | Design §12.1, §15 Q3 |
| Whether the engine may choose truck type on cost, or only minimise trucks | Design §15 Q6 |
| How many pallet sizes to plan for (enumerate vs generate patterns) | Design §15 Q7, Appendix A |
| Question budget exhausted: return best approved plan, or report none approved | Design §10.1, §15 |
| How pull-forward cost is weighed against fill value | Design §5.1 |
| Which rules are enabled for a customer (rules can be turned on/off) | Design §14 |
| Selector thresholds — "few distinct items" (read as one or two) and "21+ trucks" — measured from three days, not agreed | Algorithms §3, §4 Fig. 4 |
| What "most valuable" means in output maximisation (priority, volume, or both) | Algorithms §4 Fig. 4 |
| Which countries and truck types are supported first | Design §15 Q4 |
| What "best" means when choosing among Pass 2 stacking attempts | Algorithms §2.1, Fig. 2 |
| Whether a placeholder's load count is exact or a minimum ("does not meet minimum requested = 16") | Design §2 |
| Which demand lines may be pulled forward (`CanGo` marking vs any later line) | Design §2, §5.1 |
| Side-overhang playbook thresholds ("add a 1–2 m stack") and their unit | Side Overhang §5, Fig. 3 |
| Which later-day goods the filler takes first (priority, nearest due date, or best fit) | Algorithms §2.2, Design §5.1 |
| Where stock availability on the shipping day comes from. The feed has `AVAIL_DATE` and `AVAIL_QTY`, but `TRANS` exceeds `AVAIL_QTY` on every line (`include/validator.hpp`), so neither can yet be read as "stock on hand" | Design §5.1 |
| Which way the 0–11 `TPRIO` scale runs (is 0 the highest priority or the lowest?) | Design §5.1, §6, §10.2 |
| Which date a demand line is due on: `DATFR_TA`, `DATTO_TA`, `CONFIRMED_DATE` or `CTL_DATE`. The pull-forward window is measured from it (Check 14) | Design §5 |
| What the `CTL` level-load block (`LEVEL_LOAD_START/END`, `AUTO_O2`) limits, and what applies on a day it has no entry for | Design §5 |
| What `PLANNER_TRANS_NMIX` means. It is read and never used (Check 32) | Design §7 |
| Which stacks count as side neighbours in a pinwheel or mixed-orientation row | Side Overhang §2, §4 |
| Which Truck Builder scores are acted on, and what counts as "low" for each | Design §10.2 |

**Fails if:** a provisional rule changes a printed figure and the output
does not say so — including a value from the table above that is read from
config and used as if it were settled.

## Check 10 — every size, limit and rule is a setting, validated when read

The design's central rule is that the engine works for any customer in any
country, so no pallet size, trailer dimension, weight limit, stack position
count or rule is written into the code (Design §3). Trailer interior width
varies from 90 to 101 inches in the US alone and must be per-trailer, never a
constant (Side Overhang §2). How many pallets fit across a trailer is worked
out from the widths, never assumed to be two (Design §3, Appendix A).

```bash
grep -nE '[^.0-9a-zA-Z_](2[0-9]{3,}|[3-9][0-9]{2,}|4[08]|9[0-9]|10[0-9]|53)(\.[0-9]+)?[^0-9a-zA-Z_]' src/*/*.cpp src/*.cpp include/*.hpp
grep -rn 'kCmPerInch\|/ 2.54\|\* 2.54' src include
```

The first grep is deliberately broad: skip hits in comments, string
literals, percentages and buffer sizes. For each remaining numeric literal
that is a physical size, a weight/height limit, a
position count or a pallets-across figure, it must come from
`config/*.json` via `paramsLoader`. Every such setting must be checked as it
is read — present, right type, positive and finite — and a bad value must
stop the run with a message naming the field (Design §14). The reference is
`readQuantity(requiredField(...))` in `src/params/paramsLoader.cpp`.

Units are converted once, when data is read, and one unit is used
everywhere after that (Design §3 "A note on units"). A cm↔inch conversion
outside the importer/converter boundary means a value is crossing modules
in the wrong unit. The one other allowed place is the output boundary, if
the report is printed in the customer's unit. Convert there once, and label
the unit on every printed figure.

Thresholds that are ratios need a unit too. The FFD → column generation
escalation gap, the selector's "few items" count, a severity threshold and
a self-check margin can each be read more than one way. A gap of `5` can
mean 5 %, 0.05 or 5 trucks. The setting's name or its schema states the
unit, and the loader rejects a value that is out of range for that unit,
such as a percentage above 100.

Truck types are set "per country and per customer" (Design §3.1), so one
setting can come from more than one layer. If a customer entry overrides a
country default, confirm:

- the order in which layers win is stated in one place;
- the overriding value gets the same range and finiteness validation as the
  default;
- a customer override of the wrong type is an error. It must never fall
  back to the country value (Check 1);
- the report states which layer supplied each limit that bound a lane.

**Fails if:** a business size or limit is a literal in `src/` or
`include/`; a new config field is read without range/finiteness validation
(or through a `get_or` whose fallback is itself a plausible value); a module
assumes a fixed number of pallets across; or unit conversion happens
anywhere after the read boundary or the output boundary; or a layered
setting has no stated precedence, or its override skips validation.

## Check 11 — a stack's footprint is the effective footprint, not the nominal pallet

Stack footprints are not fixed at the pallet size. The real footprint is
`max(load length, pallet length)` × `max(load width, pallet width)`, and
the pattern engine must work from that, not the nominal pallet (Algorithms
§5.2). A load that overhangs its pallet occupies more of the row than the
pallet does, so a nominal footprint over-states what fits across.

```bash
grep -rn 'footprintLengthIn\|footprintWidthIn' src include
```

Trace every place a `UnitLoad`/stack footprint is set or compared to a
trailer width. It must take the larger of the load's own dimensions and the
pallet's. A comment such as "from the pallet spec, never the case
Length/Width" is exactly the pattern this check exists to catch — confirm
which product-master columns hold the load's dimensions before accepting
either reading, and if that is unsettled, list it under Check 9.

**Fails if:** a footprint used for rows, floor positions, pattern
generation or fit checks comes only from the pallet spec, or a row/pattern
count test only uses nominal 48×40 footprints.

## Check 12 — the truck floor is per segregated group, summed, against the largest trailer

The floor — the fewest trucks the volume could occupy — is the bound every
Stage 2 method reports against, so an invalid floor corrupts every lane's
reported gap. Three rules make it valid (Delivery Plan M3; Algorithms §6):

- computed **per segregated group and summed**, never across a whole lane
  (two do-not-mix groups can never share a truck, so pooling them
  under-counts);
- each group's figure is **rounded up to whole trucks before summing**
  (Check 8 — a Pass 1 fractional ratio is fine only for comparing cube with
  weight, and must be labelled as such);
- the divisor is the **largest trailer available** for the lane; dividing by
  a smaller or arbitrarily-chosen trailer (e.g. `params.trailers.front()`)
  stops it being a lower bound.

```bash
grep -n 'trailers\|selectTrailer\|stackPositions\|weightLimitLb\|trucksIf' src/*/*.cpp src/pipeline.cpp
grep -rn 'ceil' src
```

Two more edge cases:

- **`ceil` on a float ratio.** A group that weighs exactly 45,000 lb can
  come out as `45000.0000001 / 45000` after unit conversion and summing,
  and `std::ceil` makes that 2 trucks. That is a phantom truck, and it
  breaks Design §12.2's test that "exactly one truck's worth uses one
  truck". Every round-up to whole trucks or positions must subtract the
  shared tolerance first (Check 18), e.g. `ceil(ratio - kEpsilon)`. A
  test must put a group exactly on the limit.
- **A mixed fleet.** The floor divides by the largest trailer because it
  is a lower bound. The real trucks do not all have that trailer's
  capacity. A solver, filler or repair step that reuses the floor's
  divisor as a truck's capacity overloads every smaller trailer on the
  lane. Each truck is planned against the trailer it was actually given.

**Fails if:** any truck floor is summed before rounding, pools groups, or
divides by a trailer that is not the largest one configured for that lane;
the Pass 1 cube/weight division figures (45,000 lb, the stack-position
count) are anything but per-trailer settings; a round-up to whole units has
no tolerance; or the floor's trailer is used as the capacity of a truck
that has a different trailer.

## Check 13 — every search loop has a configured cap, and exhausting it is reported

Truck Builder is slow and is the only authority on whether a load is safe
and legal (Design §10). Every loop that retries — Pass 2 stacking attempts,
first-fit-decreasing → column generation escalation, side-overhang repair,
Truck Builder re-score calls — must be bounded by a setting, and hitting the
bound must return the best result found **and flag it as unresolved, never
silently** (Side Overhang §5 failure modes; Delivery Plan M7: "a load that
cannot be repaired is reported, never silently passed").

```bash
grep -n 'while\|for (;;)\|Cap\|attempt\|retry\|rescore\|reScore' src/*/*.cpp src/pipeline.cpp
```

For each loop, confirm: the bound is read from params (Check 10); the
cap-hit path sets an unresolved flag that reaches the report **and** the
completeness verdict (Check 6); and Order Builder's own rough pre-check never
reports a plan as good by itself — only a Truck Builder answer may (Design
§10.3). The same applies to fixed truck counts: if a lane is given N trucks
and fewer are filled, the output names the lane and the shortfall rather than
quietly producing fewer (Design §6). Producing **more** loads than requested
is the mirror defect (4 August: 372 loads against 327 requested) and must be
named the same way.

Truck Builder answers are cached: the same plan is never sent twice, and a
cache hit does not spend a re-score call (Design §10.4).

The re-score budget is **per shipment** (Delivery Plan M7: "re-score calls
stay within the agreed number per shipment"). It is not per lane or per
run. A counter held per run lets one bad lane use up every other lane's
budget. A counter held per repair option lets the 1 → 2 → 3 escalation
chain (Side Overhang §5) spend the budget three times over. One counter per
shipment covers every option, restart and retry for that shipment.

Three loops are easy to miss because they span modules rather than sitting
in one `while`:

- the **filler loop** (Algorithms Fig. 1, G → C): filler that needs new
  stacks sends the lane back through stack building, which can produce new
  filler work. It needs its own pass cap, and every pass must shrink the
  remaining filler pool, or it can cycle;
- the **"whole load refused" cascade** (Design §10.2): the lowest-priority
  pallet comes off and is planned into the next truck, which can in turn be
  refused. Bound it, and when there is no next truck (fixed count, Check 30)
  the pallet goes to "not shipped" with a reason, never to nowhere (Check
  16);
- **mend versus restart** (Design §10.2): the rare "start again" path needs
  a limit too, and counts against the same question budget.

**Fails if:** a loop has no bound, a hard-coded bound, or a cap-hit /
shortfall / over-production path that is only logged; an identical plan can
be re-sent to Truck Builder; or a result is marked accepted without
Truck Builder having scored it.

## Check 14 — hard rules hold through every later stage

A rule enforced where it is computed can still be broken by a later stage
that moves goods around. Each of these must be re-checked (or structurally
impossible to violate) wherever lines are regrouped, stacked, assigned to
trucks, filled or repaired:

- **Do-not-mix** is a hard rule: no pair may ever share a group, a stack or a
  truck — tested against all 22 pairs (Design §7; Delivery Plan M2).
- **Heavy items are never stacked** — the CRI / Strength limit holds after
  repair moves such as "1 heavy for 2 lighter" (Delivery Plan M2; Side
  Overhang §5).
- **Same-site** is a soft rule — same-site goods may mix on at most one
  truck — and must be **carried through to assignment**, not settled and
  dropped at segregation (Algorithms §2.3). `sameSiteFlag` must survive into
  whatever consumes groups next.
- **Volume used once is never shipped again**: in multi-day planning, a line
  pulled forward is removed from its later day, and pull-forward stays inside
  the per-lane window (Design §5; Delivery Plan M6). A line may be pulled
  forward only if its stock is available on the shipping day, and the reason
  for every pulled line is recorded in the output (Design §5.1, §14).
  Eligibility is data. The current output marks pulled lines `CanGo`
  (Design §2), so confirm which field or setting makes a line eligible, and
  that an ineligible line is never pulled. The window is measured from the
  line's **original** due date. A line already pulled forward is not
  pulled again ("only once", Design Fig. 4), so no chain of moves
  (day 3 → day 2 → day 1) can take it past its window.
- **Pull-forward fills trucks. It never adds one.** Design §5.1 pulls goods
  forward only "if a truck is still not full". Pulled goods take spare
  room on trucks that today's demand already needs. The one exception is
  the fixed trucks a placeholder asks the engine to fill (Design §6,
  Check 30). A pull that makes the day's truck count go up has moved a
  later day's truck earlier, tied up stock early, and saved nothing. A
  test must show that pull-forward never raises a day's truck count above
  what its own demand, or its fixed count, needs.
- **A line can be split.** When only some of a line's pallets fit as
  filler, the pulled part ships today and the rest stays on its own day.
  Both parts are counted (Check 16). The "only once" rule applies to the
  part that was pulled. The rest can still be pulled later, within the
  window measured from the line's original due date. The report shows the
  line on both days, with the split quantity.
- **Stock is shared, lanes are not.** Design §12.3 plans lanes in
  parallel because "lanes do not affect one another". Stock availability
  (Design §5.1) is a warehouse fact, though. Two lanes can both pull the
  same available stock forward. Each check passes on its own and the
  stock ships twice. Availability must be reserved once across lanes, in
  a fixed lane order (Check 15), or the report must say it was checked
  per lane only.
- **Filler is pull-forward.** The filler loop (Algorithms §2.2) takes
  goods "pulled from later days". It obeys every pull-forward rule above
  — eligibility, window, availability, only once, recorded reason — and
  the rule for which later goods it takes first (Check 9). A filler path
  that picks from the later day's lines directly skips all of them.
- **Fill beats grouping**: when splitting a group costs fill, fill wins
  (Algorithms §6) — but never at the cost of a hard rule above.
- **Soft never overrides hard**: soft preferences such as keeping the same
  product together (Design §7) and same-site are scored, not enforced, and
  no soft-rule weight, however large, may produce a plan that breaks a hard
  rule. A soft rule implemented as a filter rather than a score is the
  mirror defect: it quietly becomes hard and costs trucks.

```bash
grep -rn 'sameSiteFlag\|doNotMix\|DNM\|canStack\|pullForward\|window' src include
```

**Fails if:** a stage after segregation can combine lines from two groups
without re-checking do-not-mix, a repair/fill move bypasses `canStack`, a
flag computed at segregation has no reader downstream, a pulled-forward
line can be counted on two days, a line can be pulled forward without an
availability check or a recorded reason, an ineligible line can be pulled,
the window is measured from anything but the original due date, two
lanes can claim the same stock, filler bypasses the pull-forward rules,
pull-forward raises a day's truck count, or a split line loses or
double-counts either part.

## Check 15 — the same input gives the same output, and any run can be replayed

Any run must be reproducible from its recorded input and settings (Design
§12.4; Delivery Plan M8: "Same input gives the same output every time", "Any
solve can be recreated from recorded data").

```bash
grep -rn 'unordered_map\|unordered_set\|rand\|random_device\|mt19937\|time(\|chrono::system_clock' src include
grep -rn 'thread\|async\|mutex\|atomic\|static [A-Za-z_:<>]* [a-zA-Z_]* *[=;{]' src include
```

Iteration over an `unordered_*` container must not decide output order,
group/stack/truck numbering, or which of two equal candidates wins — sort,
or use an ordered container, first. Any randomness takes a fixed seed from
settings. Parallel lanes (Design §12.3) must merge results in a fixed lane
order, not completion order. `tests/testPipelineDeterminism.cpp` is the
reference test; a new module that produces ordered output should be covered
by it.

Once lanes run in parallel, any state shared across lanes — the logger, the
Truck Builder answer cache, a function-local `static`, a counter — must be
synchronised or per-lane; a data race is non-determinism that no
single-threaded test will show.

Two more parallel defects look fine in a single-threaded run:

- **A lane that throws.** An exception that escapes a `std::thread` calls
  `std::terminate`, so there is no report and the output file may be half
  written (Check 6). A `std::future` from `std::async` that is never
  `get()` loses the exception, and the lane is simply missing from the
  merged result. Every worker must catch its own exception and turn it
  into a named, failed lane. The run's verdict must count that lane, and
  a test must make one lane throw.
- **Totals summed in the order threads finish.** Floating-point addition
  is not associative. A run-wide cube or weight total summed in
  completion order can differ in its last bit from run to run, and that
  is enough to flip a `ceil` or a threshold (Check 12). Totals across
  lanes are summed in the fixed lane order, after the workers join.

A **wall-clock** budget (the 10 s per shipment target, Design §10.1) is the
subtle case: a loop that stops "when time runs out" returns a different
plan on a slower machine or under load. Search caps that decide output must
count iterations or questions, not seconds; a time limit may only abort and
flag the run, and if it cuts a search short the point it stopped at must be
recorded so a replay stops at the same place.

Determinism alone is not replay. The run must record what it needs to be
recreated: the input files (or their hashes), the settings in effect, and
every Truck Builder question with the answer given (Design §10.4, §11.2).

A replay answers Truck Builder questions from that record. If the replayed
run asks a question the record does not hold, the run has diverged. It must
stop and name the first question that differs. It must never fall through
to a live Truck Builder call, or to the cache, and carry on as if the
replay had matched.

**Fails if:** output order or a tie-break depends on hash iteration, thread
completion, wall-clock time (including a time-based search cutoff), or an
unseeded RNG; mutable state is shared
across parallel lanes without synchronisation; or a stage whose output
depends on something outside the input files and settings (a Truck Builder
answer, a seed) does not record it; or a replay that diverges from its
record continues silently; a worker exception can end the run or drop a
lane without a named failure; or a cross-lane total depends on the order
threads finish.

## Check 16 — every pallet is placed exactly once, and nothing beats the floor

Check 6 catches a line that disappears. It does not catch a line that
survives with fewer pallets than it came in with, a pallet counted in two
stacks or two trucks, or two pallets in the same floor position. Design
§12.2 requires a separate checker that reads only the plan and confirms
nothing is over width, length or weight, every pallet is placed exactly
once, and no two pallets share a place.

```bash
grep -rn 'quantity\|pallets\|remaining\|assigned\|placed' src/*/*.cpp src/pipeline.cpp
```

Between every pair of stages (convert → segregate → stack → assign to
trucks → fill → repair), confirm the total pallets per line going in equals
the total coming out plus the total reported as not shipped, and that a
test asserts it on the real fixtures. Also confirm no lane ever reports
fewer trucks than its floor (Check 12): the floor is a lower bound, so a
result under it means the floor or the solution is wrong.

**Fails if:** any stage can change a line's pallet total without that
difference appearing in the report and the completeness verdict; the plan
checker (once it exists) re-uses the planner's own bookkeeping instead of
recomputing from the plan; or a result below the floor is accepted.

## Check 17 — a wrong unit of measure that gives a plausible number

The M1 acceptance test names this case: a wrong unit of measure "generates
hundreds of phantom trucks" (Delivery Plan M1). Check 6 only catches an
unknown unit turning into 0 pallets. The more dangerous case converts to a
believable but wrong figure: cases read as pallets, centimetres read as
inches (102 × 122 read as inches), pounds read as kilograms.

```bash
grep -rn 'uom\|UoM\|unit\|kCmPerInch\|lb\|kg' src/importer src/converter src/validator include
```

Confirm the unit of every feed comes from settings (Design §3.1 "set per
data feed") and that the Validator has plausibility rules that would trip on
the common mix-ups: a unit-load dimension or height larger than any
configured trailer interior, a pallet count per line far above the lane's
trucks × stack positions, a converted pallet footprint that matches no
configured pallet type in either unit.

**Fails if:** swapping a feed's unit (cm ↔ in, CS ↔ PAL, lb ↔ kg) produces
a clean run with a different truck count and no warning or error.

## Check 18 — dimension fits are compared with a stated tolerance

After converting 102 cm to 40.157 in, "do two fit across a 96 in trailer"
is a floating-point comparison at the exact boundary the business cares
about — two pallets wide-on need 96 in, so a 95 in trailer forces the
pinwheel (Side Overhang §2). An exact `<=` on converted doubles can flip
either way.

```bash
grep -rnE '(<=|>=|<|>|==) *[a-zA-Z_.]*(Width|Length|Height|width|length|height)' src
grep -rn 'kQuantityEpsilon\|epsilon\|tolerance' src include
```

Every comparison of a physical dimension, weight or height against a limit
must use one shared, named tolerance (the reference is `kQuantityEpsilon` in
`stackBuilder.cpp` for quantities), and the direction of the tolerance must
favour the conservative answer (does-not-fit, over-weight). No `==` on
doubles.

**Fails if:** a fit/limit comparison on converted doubles has no tolerance,
uses a different tolerance from the rest of the code, or rounds in the
permissive direction; or no test puts a value exactly on a limit and a
hair either side of it.

## Check 19 — stack and truck height and weight limits hold

Check 14 covers "heavy items never stacked". The M2 acceptance test also
requires **stack height and weight limits honoured**, and the design makes
truck weight a per-country rule — axle limits in America, a single contract
weight in Europe (Design §3.1, §10.2 AXLE BUFFER, "Whole load refused").

```bash
grep -rn 'maxStackHeight\|interiorHeight\|heightLimit\|weightLimit\|axle\|contractWeight' src include config
```

Confirm: a stack's height never exceeds the trailer's interior height (per
trailer, Check 10) or the configured stack-height limit; a stack's weight
never exceeds its limit; a truck's weight is checked under the weight rule
configured for its country, not always the US figure; and all of this is
re-checked after filler and repair moves (Check 14).

A lane whose country or trailer type has no configured entry must stop the
run with the lane and the missing key named. It must never fall back to the
first configured trailer or to the US weight rule. The fallback produces a
clean-looking plan against the wrong legal limit.

**Fails if:** any of these limits is enforced only where stacks are first
built, is missing entirely, is checked against a single global figure
rather than the trailer or country in use, or an unconfigured country or
trailer type silently takes a default.

## Check 20 — highest priority first, lowest priority dropped first

Each day's trucks are filled with what is due that day, highest priority
first; a fixed truck count is filled with the highest priority goods
available; and when Truck Builder refuses a whole load, the lowest priority
pallet comes off (Design §5.1, §6, §8.2, §10.2).

```bash
grep -rn 'priority\|Priority' src include
```

Confirm priority is read and validated (Check 3), used as the primary key
wherever goods compete for limited trucks, and used (ascending) wherever a
pallet is removed. Ties fall back to a deterministic key (Check 15).

"Highest" is not obvious from the data. The feed carries `TPRIO` as an
integer from 0 to 11 (`include/demand_types.hpp`), and nothing in the
documents says whether 0 is the most urgent or the least. A comparator
written the wrong way round still fills every truck, with the wrong goods,
and no count-based test notices. So:

- the direction is one setting per feed, read in one place, and listed
  under Check 9 until the customer confirms it. It is never a `<` or `>`
  repeated in each module;
- a test with two lines of different priority competing for one position
  asserts which one ships, named by the setting;
- a missing `TPRIO` must not read as priority 0 (Check 1), and a value
  outside the feed's stated range is an error, not clamped.

**Fails if:** a stage that chooses between goods for limited space ignores
priority, sorts it the wrong way, or removes a pallet by any rule other than
lowest priority first without the report saying why; the priority direction
is hard-coded; or a missing or out-of-range priority is accepted.

## Check 21 — empty, zero-total, tied and odd-count inputs

The supplied days never contain these, so they must be tested
deliberately:

- a day or lane with **no demand**, and a segregation group left **empty**
  after filtering — no division by zero, no phantom truck, no crash;
- a lane whose **total weight or total cube is 0** (every line a 0-weight raw
  material row, Check 7) — the binding-limit ratio must not divide by it;
- cube and weight **tying exactly** as the binding limit — a stated,
  deterministic tie-break;
- exactly **one truck's worth** of demand uses exactly one truck, and **one
  more pallet** produces two (Design §12.2);
- an **odd number of stacks** in sort-and-pair leaves one stack with no
  partner — the 59-pallet case (Side Overhang §3) — which must be placed by
  a stated rule and flagged if it has no side support.

```bash
grep -rn 'empty()\|size() == 0\|/ *total\|ratio\|% *2' src
```

**Fails if:** any of these inputs crashes, divides by zero, produces a
non-integer or extra truck, picks a tie-break by accident, or has no test.

## Check 22 — both pallet orientations, and pattern counts that outgrow enumeration

A row pattern must consider every pallet in both orientations — a Euro
pallet turned sideways is 80 cm, which is why three fit across in Europe;
the pinwheel exists because pallets alternate orientation (Design §3; Side
Overhang §2; Algorithms §5.2: the extra three positions come from
interlocking pallets of different orientation). Pattern counts grow from 12
(two footprints) to 331 (eleven) to more than 25,000 (thirty-two), at which
point patterns must be generated, not listed (Design Appendix A).

```bash
grep -rn 'rotate\|orientation\|swap(.*[Ww]idth\|pattern' src include
```

Confirm the pattern builder tries both orientations of every effective
footprint (Check 11), and that full enumeration is bounded by a setting
(Check 10) with a switch to generation, or an explicit reported error,
above it — never an unbounded allocation.

There is no built-in upper limit on pallet size (Design §3.1: 53 in and
wider handled like any other). So look for hidden ceilings — a fixed-size
array of positions across, a `std::array<..., 3>`, a max-width assert — and
for the opposite extreme: a footprint wider than the trailer in **both**
orientations fits in no pattern, and must be reported as unshippable by
name, not produce zero patterns, a divide by zero, or a silent drop (Check
6). A 20 in footprint puts five across (Appendix A).

Every pattern result has an area bound: positions ≤ floor area ÷ footprint
area (21 fat-thin positions in a 40 ft container against an area limit of
22, Algorithms §5.2). A pattern that beats its own area bound is wrong.

Length matters as much as width. A pinwheel row is 48 in deep on one side
and 40 in on the other (Side Overhang Fig. 2), and the interlocking that
turns 18 positions into 21 depends on those depths. The rows placed along a
trailer must sum to no more than its interior length, using each row's real
depth rather than one nominal row depth. The same rule applies to 40 ft
containers and 53 ft trailers, which are different lengths.

The layout builder runs "once at the start of a run" (Design §9, part 3),
but trailers differ by lane, country and customer, with widths from 90 to
101 in in the US alone. Any cached or precomputed layout set must be keyed
by the trailer's interior width and length **and** the set of effective
footprints it was built from. A lane that reuses another trailer's layouts
passes every test that has only one trailer configured.

Above the enumeration bound, layouts are generated rather than listed.
Appendix A requires both paths to use "the same setup and the same measure
of what best means". A generator with its own scoring picks a different
layout from the enumerator on the same input, and the plan changes when the
footprint count crosses the threshold. A test must run both paths on an
instance small enough to enumerate and assert they choose the same best
layout, or an equally scored one.

**Fails if:** the enumerated and generated paths can disagree on the best
layout for the same input, or only one orientation is considered, pallets-across is capped
at a fixed number, enumeration has no size bound, a footprint wider than
every trailer is not reported, no test asserts the area bound, rows are
never summed against the trailer's interior length, or a layout
cache key omits the trailer dimensions or the footprint set.

## Check 23 — input that differs by platform or country

The code must build and run on Windows and Ubuntu (every milestone) and read
feeds from any country (Design §3). Three input quirks break naive readers:

- **CRLF** line endings leave a trailing `\r` on the last CSV cell, so the
  last column's key or number fails to match or parse;
- a **UTF-8 BOM** before the first header name makes that column
  "missing";
- a **decimal comma** (`1,5`) in a European feed, and `std::stod` /
  `strtod` depending on the global C locale.

One quirk is not in the input but in the compiler: `long` is 32 bits on
Windows (LLP64) and 64 bits on Ubuntu. A total that fits on Linux can wrap
on Windows — cube in cubic inches is the likely one (a 53 ft trailer is
roughly 7 million in³, so a few hundred trucks passes `INT32_MAX`), as is a
product of two `int` counts. `placeholder_types.hpp` uses `long long` for
`total_loads`; totals added later must do the same or stay `double`.

```bash
grep -rn 'getline\|\\r\|BOM\|\\xEF\|stod\|strtod\|stoi\|locale' src
grep -rnE '\blong\b[^ ]|\bint\b [a-zA-Z]*([Tt]otal|[Ss]um|[Cc]ube|[Vv]olume)' src include
```

Row structure is the other half. `split_csv` in `product_importer.cpp`
splits on every comma and assumes no quoted fields, so a description such as
`"Tissue, 2-ply"` shifts every later column one place right. Weight then
lands in a dimension column, and it may still parse as a number.
`std::getline(ss, cell, ',')` also drops a trailing empty cell, so
`a,b,` yields two cells, not three. Confirm every CSV and TSV reader:

- compares each row's cell count with the header's and rejects a mismatch
  by line number — the check that turns a column shift from silent
  corruption into an error, whether or not quoting is supported;
- finds columns by header name, not position, and rejects a duplicate or
  missing required header;
- skips blank lines explicitly rather than reading them as a record of
  defaults.

Design §11.1 expects a reader per customer layout. Each new reader must
feed the same Validator rather than carry its own partial checks.

**Fails if:** a CSV or TSV reader does not strip `\r` and a leading BOM, a
numeric parse depends on the process locale, a running total is held in
`int` or plain `long`, or a row with the wrong number of cells is accepted;
each needs a test with that input.

## Check 24 — the customer's acceptance figures are pinned as tests

The delivery plan judges each milestone against numbers measured from the
customer's own files: M3 floor of 1,219 trucks over 454 lanes; M4 all 503
lanes routed to the expected method, knapsack matching brute force, FFD
within its bound; M5 the 38-truck lane on 5 August inside the time budget;
M6 12 and 331 row patterns and 21 positions in a 40 ft container; M8 under
10 s per shipment, reported as typical and worst case, not average (Design
§12.3).

```bash
grep -rn '1219\|1,219\|454\|503\|331\|\b21\b\|38\|198\|\b59\b' tests
```

When a change touches the module a figure belongs to, confirm a regression
test asserts that figure on the real fixtures (not a re-derived one).

The M8 acceptance is a per-lane comparison against the customer's current
output (1,308 trucks on 4–6 August; Design §12.1): any lane using more
trucks than today is a fault. That comparison must be produced per lane,
not only as a total, so one worse lane cannot hide behind a better one.
The number of Truck Builder questions is reported next to the timings
(Design §12.3).

Two baselines from the same days make the per-lane comparison checkable:

- the floor gap distribution: 86% of the 454 measurable lanes at the floor,
  53 one truck above, 11 two or more (Algorithms §5.1). Pin it when the
  floor or gap report changes;
- side overhang: 91 of 1,308 scored loads (7.0%), 77% of them severity 2 or
  below, 8 at severity 6 or higher (Side Overhang §6). M7 repair is judged
  against this population, so the report must give the before and after
  count at each severity.

More figures from the documents, each checkable on known input:

- Side Overhang Fig. 4: eight stacks, whose total height gap falls from
  198 in to 18 in after sort-and-pair. That is a small, exact input for the
  sort-and-pair test (Check 26).
- M5 names the **five largest lanes** inside the time budget, not only the
  38-truck lane. The largest lane carries 59 items across 38 trucks
  (Algorithms §3).
- Per day, trucks requested against loads produced: 327 / 372, 587 / 551
  and 589 / 534 (Design §6). On 4 August, 150 loads filled on volume and
  38 on weight (Design §2). The engine's report must give the same
  breakdown, so the M8 comparison can show both figures moving.
- M8's proposed bar is "same trucks, filled at least as well" (Design
  §12.1). The per-lane comparison therefore covers fill and score as well
  as truck count.

**Fails if:** a milestone's module is changed and its acceptance figure is
not asserted by any test, the test computes the expected value with the
same code it is checking, or the comparison with current output is only a
total.

## Check 25 — the binding limit is decided once per lane-day, and everything after works to it

The two-pass method exists to break a circular dependency: stacks need the
binding limit, and the limit is computed from stacks (Algorithms §2.1).
Pass 1 decides the limit from **raw weights and a maximum-height cube
estimate** — never from finished stacks. Pass 2 rebuilds stacks with that
limit known. Design §4.1 then requires every truck on the lane that day to
be planned against the one limit.

```bash
grep -rn 'binding\|Binding\|assessBinding\|cubeBound\|weightBound' src include
```

Confirm:
- Pass 1 reads nothing produced by the stack builder (otherwise the loop is
  back);
- Pass 1 compares two **truck counts**, trucks if it weighs out
  (Σ weight ÷ the trailer's weight limit) against trucks if it cubes out
  (Σ max-height cube ÷ stack positions), and **whichever is larger** binds
  (Algorithms Fig. 2). A reversed comparison, or one that compares raw
  totals, still picks one of the two limits, so it looks plausible. Tests
  must include a clearly dense lane and a clearly light lane, and assert
  which limit binds for each. The "max-height" cube uses the height limit
  of the trailer in use (Check 19), not a global figure and not the
  height the stacks were actually built to;
- in multi-day planning, pull-forward moves lines out of a later day
  (Check 14). That day's totals, floor and binding decision are computed
  from what is left after the pull, or the stored decision is flagged as
  stale. A later day planned against its pre-pull totals can have its
  binding limit flip. The same applies to the day that **receives** the
  pulled lines: dense filler on a cube-bound day can make weight bind. The
  receiving day's decision covers what it finally ships, or the flip is
  reported;
- every consumer after Pass 1 (Pass 2 strategy choice, selector, filler,
  repair) reads the same stored per-lane-day decision rather than
  re-deriving it from its own subset — a subset of a lane can bind
  differently from the whole;
- if Pass 2's stacks would make the other limit bind, that is reported,
  not silently swapped;
- the **filler** takes its single-pallet path only when the truck is
  cube-bound with one position left, and re-checks the weight limit, the
  stack rules and do-not-mix before adding it (Algorithms §2.2) — a
  weight-bound truck topped up by one pallet is overweight;
- which pallets are double-stacked in a 40-pallet shipment that weighs out
  is a recorded decision, because it moves axle weight (Algorithms §2.1).

**Fails if:** a later stage recomputes the limit from part of the lane,
Pass 1 depends on finished stacks or compares in the wrong direction, the
filler can add a pallet to a weight-bound truck, a day is planned against
totals that pull-forward has already changed, or a change in which limit
binds is not reported.

## Check 26 — a repair move never leaves a load worse

Critical side overhang is a height difference between neighbouring stacks
(Side Overhang §1), and the M7 test is that repair **reduces** it. The
greedy playbook ("add a 1–2 m stack, 1 heavy for 2 lighter, 2 for 3") has no
guarantee and can pick a worse move first (§5).

```bash
grep -rn 'repair\|Repair\|overhang\|Overhang\|heightDiff\|sortAndPair\|pair' src include
```

Confirm, for every repair move:
- it is kept only if the total and the worst-pair height difference do not
  get worse;
- it re-checks every hard rule after moving goods — weight and axle limits,
  heavy never stacked, do-not-mix, stack height (Checks 14, 19);
- it is logged by name, so a planner can follow what was tried (§5 Option
  1), and the order moves are tried in is fixed (Check 15);
- **sort-and-pair** groups stacks by the number that actually stand across
  that trailer, not always two — three across in Europe (Design §3) — and
  sorts on the same height the defect is measured on (the effective stack
  height, including the pallet);
- sort-and-pair's proof (§4) holds for stacks standing in plain rows of
  equal-width positions. In a pinwheel or fat-thin row (Side Overhang
  Fig. 2), which stacks are side neighbours depends on the pattern, not on
  the index order. So the pairing must use the pattern's real adjacency.
  Where the pattern is not known, because Truck Builder decides placement
  (Algorithms §2.3), the report must say the guarantee is an estimate
  (Check 9);
- the height difference that counts as critical is a setting (§7 asks
  whether it varies by trailer or region), so it is keyed the same way as
  the trailer (Check 10), never a single literal. The repair loop's stop
  condition must not depend on the severity scale until the severity
  meaning is settled (Check 9);
- a load whose best pairing still exceeds the threshold is reported as "no
  repair exists for this stack set" and escalated, not accepted (§5 Option 2);
- the playbook moves change more than height (Fig. 3). Move A ("add a
  1–2 m stack") **adds goods**, and they must come from a source that obeys
  the pull-forward rules and pallet accounting (Checks 14, 16), not appear
  from nowhere. Move C ("2 stacks → 3") **uses an extra floor position**,
  so the load's position count and pattern are re-checked (Check 22).
  Move B ("1 heavy → 2 lighter") must not stack the heavy item (Check 14).
  The "1–2 m" threshold is in metres in an engine that runs internally in
  one unit, so it is a converted setting, not a literal (Check 10);
- **re-stack and swap** (Option 3) moves goods between loads, so it "ripples
  into other loads and into the binding-constraint decision" (§5). Both
  loads must be re-checked in full, the pallet total across them must be
  unchanged (Check 16), the lane's stored binding decision must still hold
  or be reported as changed (Check 25), and the other load must not gain a
  side-overhang breach it did not have. Fixing one load by breaking its
  neighbour is not a repair.

**Fails if:** a move can raise the height difference and be kept, a move
skips a hard-rule re-check, a move that adds goods or positions skips
pallet accounting or the pattern re-check, sort-and-pair assumes pairs or
pairs by index in a pinwheel row, the critical height difference is a
single literal, an unrepairable load is not reported, or a cross-load swap
re-checks only the load it was meant to fix.

## Check 27 — a Truck Builder failure is never an approval

Truck Builder is the only authority (Design §10), and the client "handles
timeouts" (§9). Every non-answer must be treated as *not approved*:

- a timeout, a connection error, a non-success status, an empty reply, or a
  reply missing a score the engine reads — none may default to a passing
  score (Check 1 for the shape; Check 3 for each of the ~two dozen scores);
- a failed call still counts against the question budget, and retries are
  bounded by a setting (Check 13);
- only a real answer is cached — a timeout must never be stored as the
  answer for that plan;
- the cache key covers **everything** Truck Builder sees (stacks, their
  order, trailer, country rules), so two different plans can never share
  an answer; if Truck Builder ignores stack order (open question, Side
  Overhang §7), the key may ignore it too, but that must be a stated
  choice;
- "The same plan always gets the same answer" (Design §10.4) holds only
  while Truck Builder itself stays the same. If the cache is kept on disk
  between runs, its key must also include Truck Builder's version and the
  rule settings it scored against. Otherwise the cache belongs to one run
  and is discarded at the end. A cache kept from last week keeps
  approving loads that the updated Truck Builder would refuse. Replay
  records (Check 15) name the Truck Builder version they came from;
- the refusal rate is measured and reported (Design §14);
- a score name the engine does not recognise is reported, not ignored.
  A new Truck Builder measure is a check the engine cannot act on. The
  direction of every score (for these, low means a problem, Design §10.2)
  and the threshold that counts as "low" are settings with a test. A score
  read the wrong way round approves exactly the loads it should refuse.

Truck Builder is not the only outside program. The engine also calls
**Pallet Builder** to work out how cases stack onto a pallet (Design §1.2).
That is the partial-pallet path from Check 8. Every rule above applies to
it too. A Pallet Builder timeout, error or malformed reply must never turn
into a default pallet, such as a full pallet's height or the nominal
footprint. Its calls are bounded, recorded for replay (Check 15), and
cached only on a real answer.

The engine's own rough self-check (Design §10.3) is tuned to be strict. It
can therefore refuse every plan for a load, and then no question is ever
asked. That load has no approved plan, and it is reported as unresolved,
the same as a Truck Builder refusal. It is never "not checked, so not
refused". Count how often the self-check and Truck Builder disagree, in
both directions, next to the refusal rate. A self-check that refuses what
Truck Builder would approve costs trucks without anyone noticing.

```bash
grep -rn 'timeout\|Timeout\|cache\|Cache\|score\|Score\|status\|PalletBuilder\|palletBuilder\|selfCheck' src include
```

**Fails if:** any failure path can yield an accepted load, a failed call is
free, a timeout is cached, the cache key omits something Truck Builder
receives, a cache kept between runs has no Truck Builder version in its key, an unknown score is dropped silently, a score's direction or
threshold is hard-coded, a Pallet Builder failure yields a default pallet,
or a load the self-check refuses outright is not reported.

## Check 28 — dates, the planning horizon and placeholder ranges

The engine plans several days at once, including weekends and holidays (a
Friday run plans Saturday and Sunday, Design §5), and placeholder files name
a lane, a **date range** and a number of loads (§6). Dates come in two
formats. The demand and placeholder feeds use ISO `YYYY-MM-DD`
(`"2026-08-17"`). The current system's output messages use `YYYYMMDD`
(`solve date 20260806`). A comparison between the two, as strings, is
always false. Convert both to one date type when they are read.

A demand line carries four dates of its own: a ship window
(`DATFR_TA`..`DATTO_TA`), `CONFIRMED_DATE`, `AVAIL_DATE` and `CTL_DATE`.
Which one makes a line "due" on a day is a provisional rule (Check 9). The
day a line is planned on, and the start of its pull-forward window
(Check 14), both depend on that choice.

```bash
grep -rn 'date\|Date\|day\|Day\|horizon\|window' src/importer include/*types*.hpp include/*Types.hpp src/pipeline.cpp
```

Confirm:
- a date is parsed and validated (month 13, day 32, an empty or
  non-numeric string are errors, not "no date"). A parser that only
  checks each part's range still accepts impossible dates: 30 February,
  31 April, and 29 February outside a leap year (2027-02-29 is invalid,
  2028-02-29 is valid). Day arithmetic for the horizon and the window
  crosses month and year ends, so it runs on a calendar type, never on
  the digits of the string;
- a demand line whose ship window is inverted (`DATFR_TA` after
  `DATTO_TA`) is reported, the same as an inverted placeholder;
- the `CTL` level-load block is keyed by a specific date **or** by a
  weekday, and the weekday values seen so far run only `MONDAY`..`FRIDAY`.
  Trucks leave at weekends too (Design §5). So a weekend day with no
  entry needs a stated treatment, and it is never read as zero capacity.
  A date entry takes precedence over the weekday entry for that day, and
  that precedence is stated. An entry with both fields set, or neither, is
  an error. An unknown weekday string such as `"Mon"` is an error too;
- a placeholder whose start is after its end, or which overlaps another
  placeholder for the same lane, is reported — overlapping ranges must not
  double-count requested trucks;
- a placeholder range spanning several days assigns its loads by a stated
  rule, not to every day in the range;
- demand dated **before** the first planned day (overdue) and **after** the
  horizon each has a stated treatment and a count in the report;
- weekend and holiday days are planned, not skipped because no planner is
  working;
- pull-forward never moves goods across the horizon or outside the lane's
  window, and never displaces goods due that day — "fill today first"
  (Design §5.1).

**Fails if:** a malformed or impossible date reads as absent or is
accepted, dates in the two formats are compared as strings, overlapping or
inverted ranges pass (placeholder or demand window), loads from one range
are counted on several days, demand outside the horizon disappears without
a count, or a day with no `CTL` entry is silently given zero or unlimited
capacity.

## Check 29 — duplicate and ambiguous keys in reference data

A join that finds two matches duplicates the demand line; a lookup that
finds none drops it; a rule keyed on a string that does not match
byte-for-byte does nothing. None of these fail a test on clean fixtures.

```bash
grep -rn 'emplace\|insert\|operator\[\]\|find(\|count(' src/joiner src/importer src/segregation src/params
```

Confirm:
- a **duplicate key** in the product master (the same material number
  twice) is detected — not overwritten silently by `operator[]`, and not
  joined twice;
- a product with **several pallet sizes** is a choice to make (Design
  §11.1): the choice is deterministic, recorded per line, and listed under
  Check 9 while its rule is provisional;
- two placeholder entries for the same lane and day are reported, not
  summed or overwritten silently;
- keys are normalised the same way on both sides of every join and every
  do-not-mix match — case, surrounding whitespace, leading zeros in a
  numeric material number;
- a demand line with a **blank** planner or origin cannot escape
  do-not-mix by matching nothing: it is either rejected or segregated
  conservatively, and the report says which.

**Fails if:** duplicate keys pass silently, one side of a join or match is
normalised and the other is not, or a blank key lets a line bypass a hard
rule.

## Check 30 — a lane the engine cannot measure or solve is named, not reported as solved

The floor is measurable on 454 of the 503 lanes (Algorithms §5.1), so 49
lanes have no floor. And when trucks are fixed and too few, the selector
sends the lane to priority knapsack, not packing (§4) — some volume will
not ship by design.

```bash
grep -rn 'floor\|Floor\|gap\|measurable\|selector\|method' src include
```

Confirm:
- a lane with no measurable floor reports "floor not measurable", never a
  gap of 0 or a gap computed against a floor of 0;
- a lane whose fixed trucks are fewer than its floor reports the shortfall
  and the goods left behind, by priority (Checks 13, 20);
- every lane is routed to exactly one method, the routing thresholds are
  settings (Check 10), and a lane matching no rule is an error, not a
  default;
- a placeholder lane-day with **no demand** reports its requested trucks as
  unfilled ("fill the trucks you are given" cannot hold with nothing to
  ship). It is not skipped, and it must not produce empty trucks counted as
  loads;
- the same holds for a lane-day with **more fixed trucks than its goods
  can fill**, including every eligible pull-forward (Check 14). It is the
  mirror of "fixed and too few", and the selector has no branch for it
  (Algorithms Fig. 4). The engine fills what it can and reports each
  unfilled or near-empty truck by lane. It does not spread the goods
  thinly so that every truck looks used, and it does not report a
  one-pallet truck as a filled load. Design §15 Q1 (hold or ship) decides
  what happens to those trucks, and it stays under Check 9 until answered;
- a lane-day with demand but **no placeholder** takes a stated treatment
  (trucks not fixed → input minimisation) that the report names. It is not
  an error, and it is not a lane with zero trucks allowed;
- each lane's output carries what Design §11.2 requires: trucks used, what
  is on each, fill by volume **and** by weight, which limit was reached,
  and anything not shipped with the reason.

**Fails if:** an unmeasurable lane shows a gap, an infeasible fixed-truck
lane reads as solved, a lane can fall through the selector, a placeholder
with no demand or demand with no placeholder is dropped or mis-counted, more
fixed trucks than the goods can fill are reported as filled loads, or a
required per-lane output field is missing.

## Check 31 — Stage 2 solvers hold both limits and keep their own guarantees

Stage 2 is six methods behind one selector (Algorithms §3–§6, Fig. 4):
exact 0/1 knapsack for a single truck, priority selection for several
fixed trucks, cutting stock for few distinct items, first-fit-decreasing
bin packing for many items, and column generation for 21+ trucks. Each
has a property that tests on clean fixtures rarely break:

- **Every truck satisfies both limits.** The binding limit decides what the
  lane is planned *against* (Check 25), but a cube-bound lane can still
  hold a dense subset. A solver that packs by cube alone and never checks
  weight (or the reverse) produces an overweight truck on exactly the
  mixed lanes Design §4 is about. Confirm every solver's feasibility test
  covers cube, weight and floor positions.
- **Dense goods are spread** across the lane's trucks, so no truck fills
  early on the wrong limit (Design §4.1 step 4, §8.2). FFD sorted by one
  measure bunches the densest items into the first trucks. Confirm the
  report shows fill by volume and by weight per truck (Check 30), so a
  lopsided result is visible.
- **First-fit-decreasing** sorts on the binding measure with a
  deterministic tie-break (Check 15). Its bound — no more than about 11/9
  of the optimum plus a constant (Algorithms §6) — is asserted by a test,
  not assumed.
- **Knapsack** is "exact" only as far as its discretisation. A dynamic
  programme over pounds or cubic inches must round capacity **down** and
  item sizes **up**. Rounding the other way can admit a truck that is
  over its limit by a fraction. The table size must be bounded (Check 13),
  the value it maximises is a stated rule (Check 9), and small instances
  are cross-checked against brute force (Delivery Plan M4). Conservative
  rounding has its own failure: an item set that fills the truck
  **exactly** can round to one unit over and be refused. Round by the
  shared tolerance (Check 18), not a whole DP unit, and test an
  exactly-full single truck (Check 21). Also, a single-truck lane whose
  demand fits entirely must ship all of it. The knapsack chooses only when
  something has to be left behind.
- **Column generation** solves an LP whose pattern counts are fractional.
  The integer solution must round up and then trim over-production, and
  never report a fractional number of trucks or patterns (Check 8). Its
  reduced-cost test uses the shared tolerance (Check 18), or it can cycle
  on a pattern at zero. Its iteration cap is a setting (Check 13). Test it
  against standard cutting-stock instances with known optima (Delivery
  Plan M5).
- **Escalation** from FFD to column generation fires at the configured gap
  (Check 9 lists it as open). A test sits on each side of that gap.
- **Selector boundaries** — exactly 21 trucks, exactly the "few items"
  threshold, trucks fixed and exactly equal to the floor (not "too few") —
  each route to one stated method, with a test on each boundary value.

```bash
grep -rn 'knapsack\|Knapsack\|ffd\|FFD\|firstFit\|columnGen\|cuttingStock\|selectMethod\|reducedCost' src include
```

**Fails if:** any solver checks only the binding limit, knapsack
discretisation rounds in the permissive direction (or rounds so hard that an
exactly-full truck is refused), column generation can
emit a fractional count or has no cap, FFD's order or bound is untested, or
a selector threshold has no test at its exact boundary.

## Check 32 — do-not-mix data is read with the right semantics, and rules switch off only visibly

Do-not-mix is the one hard rule driven entirely by customer data (Design
§7: planner and origin pairs covering hazardous goods, released versus
unreleased stock, and product families). A reading error in that data
disables the rule silently. It produces a *better-looking* plan, which is
why no test notices.

- **Symmetric**: a pair listed as (A, B) also forbids (B, A). Confirm the
  lookup does not depend on the order in the file.
- **Not transitive**: A✗B and B✗C does not make A✗C. Building groups by
  connected component over the pair graph over-segregates and costs trucks
  (Algorithms §6: fill wins). Under-segregating breaks the rule.
  Confirm segregation forbids exactly the listed pairs.
- A **self-pair** (A, A), or a pair that names a planner or origin found
  nowhere in the demand or product master across the whole input, is
  reported. The second is most likely a typo, and a typo in this file means
  a hazardous pair is travelling together. A pair that merely has no demand
  *today* is normal (`doNotMixPairsWithDemand` already counts those) and
  is not an error.
- **The pair list may not be the only keep-apart data.** Design §7 names
  released versus unreleased stock and product families as well as
  hazardous goods. The `DNM` block holds planner and origin pairs only.
  Each demand line also carries `PLANNER_TRANS_NMIX` ("NMIX" reads as
  "no mix"), which the importer reads and nothing uses. Until the
  customer says what it means, list it under Check 9, and flag any
  non-empty value the run ignores in the report. A keep-apart field that
  is read and never used is the same defect as Check 3, on a hard rule.
  For any keep-apart rule keyed on a line attribute, a line with that
  attribute blank is treated the way Check 29 treats a blank planner:
  rejected, or kept apart conservatively, and the report says which.
- Rules "can be turned on or off per customer" (Design §14). A hard rule
  may be disabled only by an explicit setting. A missing or malformed rules
  block is an error (Check 1), never "rule off". Every rule that is off
  for the run is printed in the output alongside the provisional rules
  (Check 9).

```bash
grep -rn 'doNotMix\|DoNotMix\|DNM\|enabled\|Enabled\|ruleOn\|rules' src include config
```

**Fails if:** the pair lookup is order-sensitive, pairs are closed
transitively, a pair matching nothing in the whole input passes silently,
a keep-apart field is read and never used without being reported, or a
hard rule can be off without an explicit setting and a line in the output.

## Check 33 — the build and the test suite actually run

Every milestone is judged on "builds clean on Windows and Ubuntu, its own
unit tests pass" (Delivery Plan §5). A review that reads the code and never
builds it can pass a change that does not compile, or whose new test fails.

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

`build/` is the directory the README configures. If it does not exist, say
so rather than configuring one (configuring is outside this skill's
allowed tools). Report:

- any compile error. CMake applies `-Wall -Wextra -Wpedantic -Werror`, so
  a warning already fails the build. A narrowing or sign-compare warning is
  often the Check 2 or Check 23 defect itself. Confirm that no change adds
  a `#pragma` or per-file flag to silence one;
- any failing or newly skipped test;
- code under `#ifdef _WIN32` / `_MSC_VER`, or a Windows-only behaviour
  (Check 23), that this machine cannot build. Name it as unverified rather
  than implying the Windows build passed.

The Delivery Plan's M2 risk is reuse of the stack-building code the
customer offered to share. Code brought in that way is in scope like our
own. It builds under the same warning flags, and it gets its own tests and
every check above. It must not sit in a directory excluded from the build
flags or from this review.

**Fails if:** the build fails, a warning is suppressed rather than fixed, a
test fails, Windows-specific code changed and is reported as verified, or
imported code is exempt from the flags, the tests or this review.

## Check 34 — Pass 2 strategies each do what they claim, and the pick among them is stated

Pass 2 rebuilds stacks with the binding limit known, using three strategies
with "multiple attempts" possible (Algorithms §2.1, Fig. 2):

- **A. Horses with jockeys**: heavy items matched with light ones;
- **B. Equal weight**: all stacks about the same weight;
- **C. Maximum difference**: the weight difference between stacks is
  maximised.

B and C are exact opposites. A swapped sign or comparator turns one into
the other, and both still produce valid-looking stacks, so no hard-rule
test notices.

```bash
grep -rn 'strategy\|Strategy\|horse\|jockey\|equalWeight\|maxDifference\|attempt' src include
```

Confirm:
- each strategy has a test asserting its **defining property** on the same
  input: B gives a smaller spread of stack weights than C, and A pairs each
  heavy base with a lighter top. A test that only checks "stacks were
  built" is not enough;
- "best attempt" is chosen by a stated, deterministic score (Check 15),
  and the score is listed under Check 9 while it is provisional. Ties
  resolve by a fixed key, never by the order attempts finished in;
- every attempt holds every hard rule (Checks 14, 19). An attempt that
  breaks one is discarded, not scored lower. The winning strategy and the
  number of attempts tried are recorded per lane in the output;
- attempts are bounded by `pass2AttemptCap` (Check 13). When no attempt is
  valid, the lane is reported, never given an empty stack set;
- the strategy follows the binding limit (building tall only pays off on a
  cube-bound lane, Algorithms §2.1). A weight-bound lane that still maximises
  height is wasting the decision Pass 1 made.

**Fails if:** a strategy has no test for its defining property, the "best"
criterion is implicit or order-dependent, an attempt that breaks a hard
rule can win, the chosen strategy is not recorded, or no valid attempt
leads to a silent empty result.

## Check 35 — the parts stay separate, and the planner never talks to Truck Builder

Design §9 splits the engine into seven parts that "can each be tested on
their own". It gives two boundaries in plain words. Settings "knows nothing
about files or networks". The planner "knows nothing about Truck Builder",
and the repair controller is kept apart so that neither part needs a live
Truck Builder to test. Once a planner reaches into the client directly,
every planner test becomes slow and unreliable. That is the failure the
split exists to prevent.

```bash
grep -rn '#include' src include | grep -i 'truckBuilder\|palletBuilder\|client\|repair'
grep -rn 'ifstream\|ofstream\|fopen\|socket\|curl\|http' src include
```

Confirm:

- nothing under the planner (binding constraint, selector, solvers, layout
  builder, multi-day planning) includes the Truck Builder or Pallet Builder
  client, or holds a pointer or callback to one. It returns a plan. The
  repair controller alone passes that plan to the client;
- the settings types and the planner do no file or network I/O. Reading
  files belongs to the importers and `paramsLoader`, and writing to the
  results part;
- the repair controller reaches Truck Builder through one narrow interface.
  Its tests use answers recorded from real Truck Builder replies, the same
  records Check 15 writes (Design §10.4: "those records are also what the
  testing compares against"). Truck Builder is a network boundary, so a
  fake is allowed there, and only there (CLAUDE.md, Testing). A fake that
  always approves proves nothing. The recorded set must include refusals,
  timeouts and malformed replies (Check 27);
- adding a customer means adding a reader (Design §11.1). A reader's
  output is the common record types, and nothing after the reader branches
  on which customer's layout the data came from.

**Fails if:** a planner module includes or calls a Truck Builder or Pallet
Builder client, a settings or planner module does I/O, the repair
controller cannot be tested without a live service, its fake only ever
approves, or code after the read step branches on the customer's file
layout.

## Check 36 — every Truck Builder repair leaves the load no worse, and repairs settle

Check 26 covers side overhang. Truck Builder returns about two dozen scores,
and Design §10.2 gives each low score its own fix:

| Low score | Fix | What else it can break |
|---|---|---|
| Side overhang | turn the pallet, or pair it with a closer size | turning changes the footprint's orientation, so the row pattern and width fit (Checks 18, 22) |
| Axle buffer | move heavy pallets forward or back | left-right balance, side-overhang pairing |
| Fit factor | try a different mix of pallet sizes | the pallet-variant choice per line (Check 29), the layout, the binding limit (Check 25) |
| Natural stacking | stop those two being stacked together | adds a rule; the rebuilt stacks can need an extra floor position |
| Left-right balance | swap pallets between the two sides | axle, side-overhang pairing |
| Back overhang | drop or reorder the last row | a dropped row is pallets not shipped (Check 16) |
| Whole load refused | lowest-priority pallet to the next truck | Checks 13, 20 |

```bash
grep -rn 'AXLE\|axle\|BALANCE\|balance\|FIT\|fitFactor\|NATURAL\|naturalStack\|BACK\|backOverhang\|repair\|mend\|restart' src include
```

Confirm, for each fix:

- after the fix the load is re-checked on **every** score the engine reads
  and on every hard rule (Checks 14, 19), not only the score it targeted.
  Fixing axle weight by moving heavy pallets back, then losing left-right
  balance, is a new refusal, not a repair;
- a rule the repair adds, such as "do not stack X on Y" from a low natural
  stacking score, stays in force for every later re-plan of that shipment.
  It is recorded for replay (Check 15) and printed with the load. A re-plan
  that forgets it rebuilds the refused stack. Whether it also applies to
  other shipments is a stated choice (Check 9);
- repairs converge. Two fixes that undo each other (A lowers axle and
  raises balance, B does the reverse) loop until the question budget runs
  out. The answer cache (Check 27) only stops an **identical** plan being
  re-sent. A plan that alternates between two near-identical states is not
  identical, so it will not stop the loop. Detect a repeated state, or a
  fix reversing an earlier fix, and stop with the load reported as
  unresolved (Check 13);
- "drop the last row" moves those pallets to the next truck or to "not
  shipped" with a reason, following the priority rule (Checks 16, 20);
- "mend, not restart" (§10.2) is a rule the controller follows. A restart
  happens only when the answer shows a misunderstanding. The reason is
  logged, and the restart draws on the same question budget.

**Fails if:** a fix is kept after it worsens another score or breaks a hard
rule, a rule added by repair is lost in a re-plan or never recorded, two
fixes can alternate until the budget runs out without the load being
reported, a dropped row loses its pallets, or a restart happens without a
logged reason.

## Check 37 — the PR carries its plan, self-critique and explanation, and the code matches them

The team's PR policy (*AI Coding V2*) rejects any AI-assisted PR that lacks
one of four parts. Small bug fixes are exempt. Here, "small" means the change
stays inside one function, and adds no new type, field, setting, module or
test file. Anything bigger is not exempt, even if it is labelled `fix:`.

| Part | What it must contain | Not enough |
|---|---|---|
| Plan | the goal; the assumptions; the edge cases; how errors are handled; what a passing test looks like | a restated title; "handle edge cases" without naming them |
| Self-critique | answers from a **separate** prompt: assumptions that might be wrong, where it could fail, tradeoffs, best practices it breaks, missing tests, whether this is the best approach | "looks good"; a critique with no specific risk; one written in the same prompt as the code |
| Explanation | the code explained block by block, with every non-obvious decision named | a summary of the diff; a list of files changed |
| Tests | tests for the plan's edge cases, including the invalid ones (Check 5) | only the fixture happy path |

A missing part is a finding on its own, and it blocks the merge. Then use
the plan to review the code. Every item in the plan must be traced to the
code:

- **every edge case in the plan** is handled in the code **and** has a test.
  A case the plan names that has no test is the Check 5 gap, and the plan
  already shows that the author knew about it;
- **every assumption** is either checked in code (the Check 2/3 validation
  owner) or listed as provisional in the output (Check 9). An assumption
  that is only written in the PR text disappears once the PR is merged;
- **the error handling the plan describes** is what the code does. If the
  plan says "reject the line" and the code skips it, that is the Check 6
  defect, and the plan is the proof;
- **the plan's "passing test"** exists as a real test that would fail if the
  behaviour broke, not as a manual check;
- **the explanation matches the code.** When the explanation says a block
  does X and the code does Y, the code has a hidden assumption. Report it
  as a correctness finding, not as a documentation problem;
- **the code does nothing the plan leaves out.** A new setting, rule or
  fallback that the plan never mentions has had no review against the
  plan. Ask for the plan to be updated, or the code to be removed;
- **the self-critique's risks are answered.** Each risk it raises is either
  fixed or explicitly accepted in the PR. A risk raised there that this
  review then confirms in the code is a finding at that check's severity.

The workflow asks for code to be generated in small units (one function,
one transformation, one interface), because AI drifts architecturally at
scale. Report a PR that changes several pipeline stages for unrelated
reasons (an importer, a solver and the reporter, each for its own purpose)
as one that should be split. A PR that is too large to trace back to its
plan cannot be reviewed against it.

The explanation belongs in the PR, not in the source. Stage 5 asks the AI
to "add documentation". That does not override `CLAUDE.md` § Comments: only
the non-obvious decisions the explanation names (a business rule, a
workaround, a hidden invariant) become code comments. A block-by-block
narration pasted into the source as comments is a finding.

**Fails if:** a non-exempt PR lacks the plan, the self-critique, the
explanation or the tests; the self-critique is generic or came from the
generating prompt; a plan edge case has no handling or no test; a plan
assumption is neither checked in code nor surfaced as provisional; the code
handles errors differently from the plan; the explanation contradicts the
code; the code adds behaviour the plan does not mention; or the explanation
was pasted into the source as narrating comments.

## Check 38 — architecture and security decisions name a human owner

The workflow names three areas where AI must not make the decision: security
logic, authentication, and core architecture. An undetected flaw there costs
too much. AI may still write the code. A person must make the decision,
and the PR must say who.

In this repo those areas are:

- **core architecture**: adding, merging or removing one of the seven parts
  of Design §9; changing an interface between them (planner → repair
  controller → Truck Builder / Pallet Builder client, Check 35); changing a
  record type that crosses stages (`include/*_types.hpp`,
  `include/*Types.hpp`); changing ownership or copy rules for a shared
  aggregate such as `PipelineResult` (Check 4); and changing the replay
  record format (Check 15);
- **security and authentication**: anything the Truck Builder or Pallet
  Builder client sends or receives that identifies the caller (keys,
  tokens, endpoints, TLS settings), and anything that reads such values
  from the environment or from `config/`.

```bash
grep -rn 'token\|Token\|apiKey\|API_KEY\|secret\|password\|auth\|Auth\|getenv\|https\?://' src include config
```

Confirm, for a change in either area:

- the PR names the person who made the decision, and the plan records the
  options that were considered (Check 37). "AI suggested it" is not a
  decision owner;
- no credential, key or token is written in source, in `config/*.json`, in
  a test fixture, or in a replay record (Check 15). These records are kept
  and compared against later, so a secret in one stays in the repo
  history;
- a failed authentication is an error that reaches the exit code
  (Checks 6, 27). It must never be treated as "the service is unavailable,
  carry on without it".

**Fails if:** an architecture or security change has no named human
decision owner; a credential appears in source, config, fixtures or replay
records; or a failed authentication is not reported as an error.

## Reporting

Use the `ReportFindings` tool if available in this session, one entry per
concrete gap found (not per file scanned), ranked most-severe first:
a failing build or test (Check 33), a PR missing its plan, self-critique,
explanation or tests, code that contradicts its plan or explanation, and an
architecture or security decision with no human owner (Checks 37, 38) — each
of these blocks the merge under the team's PR policy — then copy-safety
(Check 4), silent numeric
corruption (Checks 2, 17), pallets
lost or duplicated between stages (Checks 16, 29), incomplete runs that exit 0
(Check 6), a Truck Builder or Pallet Builder failure read as approval
(Check 27), and hard
rules or physical limits broken downstream, including do-not-mix data read
wrongly, solvers that check only one limit, and a Pass 2 strategy that does
the opposite of its name, and repairs that break another score or loop
(Checks 14, 19, 25, 26, 31, 32, 34, 36) before an
invalid truck floor (Check 12), unbounded or silently-exhausted loops
(Check 13), unsolved lanes reported as solved (Check 30), date and range
mistakes (Check 28) and crashes on degenerate input (Check 21) before
validator/consumer disagreement (Check 7), wrong stack
footprints or fit comparisons (Checks 11, 18, 22), priority ignored
(Check 20), missing validation ownership (Check 3) and a planner coupled
to Truck Builder (Check 35) before hard-coded
settings (Check 10), malformed-type gaps and platform input quirks (Checks
1, 23) and non-determinism or non-replayable runs (Check 15) before
fractional physical counts (Check 8) and unsurfaced provisional rules
(Check 9) before missing test coverage and unpinned acceptance figures
(Checks 5, 24). `failure_scenario` must state the WHY, not
just the symptom: name the concrete input/state that triggers it AND the
mechanism that lets it through (e.g. "get_or defaults on the wrong type, and
nothing downstream re-checks the type" — not just "wrong type is accepted").
If `ReportFindings` is not available, print the same information as a short
list: file, line, which check failed, why (the mechanism, not just the
symptom), and the concrete input that would trigger it.

After the findings are reported (via the tool or the fallback list), give
one **fix prompt** per finding, in your chat reply, as its own fenced block
labeled with the file it targets. Each prompt must be self-contained enough
to hand to a fresh Claude session with no other context and get the right
fix back:
- the exact file(s) and function/struct to change
- what concrete change to make (not just "add validation" — name the rule,
  its severity, and where in the existing check order it belongs)
- the reference pattern already in this codebase to mirror (point at the
  specific prior fix — e.g. "follow the negative_unit_load pattern at
  validator.cpp" — rather than describing the pattern from scratch)
- the test(s) to add alongside it, matching the style of the existing test
  for that same rule family

- one unit of work per prompt — one function, one transformation, or one
  interface. Split a finding that needs changes in several stages into
  one prompt per stage, in dependency order;
- an instruction to build and run `ctest` after the change and to iterate
  on any failure. The workflow expects 3–5 fix cycles, not one;
- an instruction to finish by writing a short plan of the fix and a
  block-by-block explanation for the PR (Check 37), unless the fix is
  small enough to be exempt.

Do not soften this into "you may want to consider..." — write it as a direct
instruction, the way you would brief someone picking up the fix cold. Order
the fix prompts to match the findings' severity order above.

Finally, keep the shared context current (the workflow's team practice).
If a finding belongs to a defect class that no check above covers, or a
check had to be stretched to cover it, end the report with a proposed
addition: a new check or edge case for this skill, or a new anti-pattern
line for `CLAUDE.md`, which every session loads. Propose it, do not apply
it. This skill only reads.

If every check passes, say so plainly — do not invent findings, and do not
manufacture fix prompts, to justify the review.

## Source documents

Checks 8–36 cite these customer documents, kept in `../docs/` beside the
repo (outside git). Checks 37–38 cite the team's workflow document, kept in
`../` beside the repo (outside git). When a later revision changes one of them, update the
check that cites it. Several figures exist only as images in the PDFs:
the flow and its two loops (Algorithms Fig. 1), the Pass 1 formulas and
strategies A–C (Fig. 2), the selector (Fig. 4), and the row patterns and
repair moves (Side Overhang Fig. 2, 3). Re-read the figures, not only the
extracted text, when a revision lands. The "methods, case by case" table in
Algorithms Rev 5 §5 has only its header row in the PDF, so if a later
revision fills it in, check it against Check 31.

- *Order Builder — The Algorithms*, Revision 5 — two-pass binding constraint,
  filler loop (G → C), Stage 2 case selector and its six methods, FFD
  bound, effective stack footprints, floor validity, 454 of 503 lanes
  measurable, floor-gap distribution, open items.
- *Critical Side Overhang — Repair Approaches*, Revision 4 — overhang is
  height difference between neighbours; trailer width per trailer; the three
  repair options and their failure modes (Option 3 ripples into other
  loads); severity baseline across 1,308 loads; open questions.
- *Truck Load Planning Engine — Design Document*, Revision 3 — everything is a
  setting; the four rules (one limit per lane per day, multi-day with
  pull-forward, fill the trucks given, keep goods apart); Truck Builder is the
  authority, with timeouts and a remembered-answer cache; "whole load
  refused" handling; placeholder date ranges; rules switchable per customer;
  per-lane output fields; reproducibility; the seven parts and their
  boundaries (§9); the fix for each low score (§10.2); Pallet Builder as a
  second called program (§1.2); the self-check (§10.3).
- *Four and a Half Month Plan* — per-milestone acceptance tests (M1: wrong
  unit of measure rejected, Windows and Ubuntu; M2: 22 do-not-mix pairs,
  heavy never stacked; M3: floor of 1,219 trucks over 454 lanes; M4:
  knapsack vs brute force, FFD bound, 503 lanes routed; M5: column
  generation vs known optima, escalation at the agreed gap; M6: 12 / 331 row
  patterns, 21 positions in a 40 ft container; M7: unrepairable loads
  reported; M8: deterministic, under 10 s per shipment). Every milestone:
  builds clean and its tests pass.
- *AI Coding V2* (4-26), the team's AI coding workflow. Five stages: plan
  (goal, assumptions, edge cases, error handling, passing test), generate in
  small units, critique in a separate prompt, test and fix in a 3–5 cycle
  loop, explain before merge. The PR policy requires a plan, a
  self-critique, an explanation and tests, with only small bug fixes exempt.
  AI does not decide security, authentication or core architecture. The team
  keeps a shared context file (here, `CLAUDE.md`) and adds to it what
  reviews find.
