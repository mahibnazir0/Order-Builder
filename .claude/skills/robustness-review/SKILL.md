---
name: robustness-review
description: Review Order Builder code for the defect classes external reviews have found — malformed/wrong-type external input treated as absent, unvalidated negative/non-finite numeric fields, external fields with no validation owner, copy-unsafe structs holding raw pointers, lines dropped downstream while the run still exits 0, a Validator that accepts values a downstream consumer rejects, fractional counts of physical objects, and provisional business rules not surfaced in the output. Use before merging any change to an importer, the Validator, the Converter, a reporter, the params loader, any M2 module (segregation, bindingConstraint, stackRules, stackBuilder, stackReporter), the pipeline, main.cpp, or a *_types.hpp / *Types.hpp record.
allowed-tools: Read, Grep, Glob, Bash(git diff *), Bash(git log *), Bash(git show *), Bash(cmake --build *), Bash(ctest *)
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
   stackBuilder, stackReporter; plus `src/pipeline.cpp`, `src/main.cpp`), or
   is a record header — note both naming styles, `include/*_types.hpp` (M1)
   and `include/*Types.hpp` (M2) — or is `config/*.json`, run every check
   below against the CURRENT state of the repo (not just the diff hunks) — a
   gap these checks look for is usually an absence, and an absence never
   shows up in a diff.
3. If none of those files changed, say so and stop; this skill has nothing to
   check.

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
grep -rn 'contains(' src/importer/*.cpp
```

For every match, confirm it either uses `get_optional_array` or the field
being checked is a *scalar* (via `get_or`, which already logs+defaults
correctly for scalars — this rule is about arrays/objects treated as
optional blocks, not individual fields).

**Fails if:** any new `if (root.contains("X") && root["X"].is_array())` (or
equivalent) pattern appears instead of `get_optional_array`.

## Check 2 — a divisor, multiplier, or running total needs range + finiteness validation

Grep for arithmetic on fields read from external input:

```bash
grep -n 'cases_unit_load\|weight_lb\|no_of_loads' src/converter/*.cpp src/reporter/*.cpp
```

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

For every field on `STRRecord`, `ProductRecord`, `PlaceholderRecord`,
`CTLRecord`, or `DNMRecord` (`include/*_types.hpp`), confirm something in
`src/validator/validator.cpp` actually inspects it — reading it in an
importer and summing it in the Reporter is not validation. This is exactly
how `PlaceholderRecord::no_of_loads` went unvalidated: it was read, summed
into `total_loads`/`trucks_requested`, and never checked by anything.

```bash
# For a field named FIELD, look for it outside its own struct/importer:
grep -rn 'FIELD' include/validator.hpp src/validator/validator.cpp
```

**Fails if:** a field that flows into a total, a threshold comparison, or a
downstream decision has zero hits in `validator.cpp`. (Purely descriptive
fields — free-text labels, dates used only for display — are exempt; use
judgment, but justify the exemption explicitly rather than skipping silently.)

## Check 4 — a struct holding raw pointers/references must have explicit copy semantics

```bash
grep -rn 'const [A-Za-z_]*\*\|&\s*[a-z_]*;' include/joiner.hpp include/pipeline.hpp
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

For each gap Checks 1–4 and 6–9 did NOT find (i.e. the code is correct), confirm
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

**Fails if:** a stage can drop a line, or produce no output for non-empty
input, and the only trace is a log line or a report count while the exit code
stays 0. The reference fix is `isRunComplete` + the report's `Result` line.

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

**Fails if:** a count of physical objects can be non-integer in the default
configuration without an "estimate" label in the printed output, or a test
asserts a fractional physical count (e.g. `Approx(4.0 / 3.0)` floor
positions) as the default behaviour.

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

**Fails if:** a provisional rule changes a printed figure and the output
does not say so.

## Reporting

Use the `ReportFindings` tool if available in this session, one entry per
concrete gap found (not per file scanned), ranked most-severe first:
copy-safety (Check 4), silent numeric corruption (Check 2) and incomplete runs
that exit 0 (Check 6) before validator/consumer disagreement (Check 7) and
missing validation ownership (Check 3) before malformed-type gaps (Check 1)
and fractional physical counts (Check 8) before unsurfaced provisional rules
(Check 9) before missing test coverage (Check 5). `failure_scenario` must state the WHY, not
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

Do not soften this into "you may want to consider..." — write it as a direct
instruction, the way you would brief someone picking up the fix cold. Order
the fix prompts to match the findings' severity order above.

If every check passes, say so plainly — do not invent findings, and do not
manufacture fix prompts, to justify the review.
