---
name: robustness-review
description: Review Order Builder code for the four defect classes an external review found once already — malformed/wrong-type external input treated as absent, unvalidated negative/non-finite numeric fields, external fields with no validation owner, and copy-unsafe structs holding raw pointers. Use before merging any change to an importer, the Validator, the Converter, the Reporter, or a *_types.hpp record.
allowed-tools: Read, Grep, Glob, Bash(git diff *), Bash(git log *), Bash(git show *), Bash(cmake --build *), Bash(ctest *)
---

# Robustness review

This project shipped a milestone with four defects that compiled clean, passed
the existing test suite, and only surfaced when someone deliberately fed the
pipeline malformed input. All four came from the same root cause: a check
that was assumed to exist somewhere else in the pipeline, and didn't. This
skill re-runs that specific audit — it is not a general code review.

Read [include/pipeline.hpp](../../../include/pipeline.hpp),
[include/validator.hpp](../../../include/validator.hpp), and
[include/json_util.hpp](../../../include/json_util.hpp) first if you have not
already — they contain the reference fixes every check below points back to.

## Scope

1. If the user gave an argument (a branch, PR number, or commit range), review
   that. Otherwise run `git diff` (working tree) and, if that is empty,
   `git diff main...HEAD`.
2. Pull the list of changed files out of that diff. If any changed file is
   `include/*_types.hpp`, `src/importer/*.cpp`, `src/converter/*.cpp`,
   `src/validator/*.cpp`, `src/reporter/*.cpp`, or `include/pipeline.hpp` /
   `include/joiner.hpp`, run every check below against the CURRENT state of
   the repo (not just the diff hunks) — a gap these checks look for is
   usually an absence, and an absence never shows up in a diff.
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

For each gap Checks 1–4 above did NOT find (i.e. the code is correct), confirm
a test exercises the malformed case, not just the valid one — grep the
relevant test file (`tests/importer/test_*.cpp`, `tests/validator/
test_validator.cpp`, `tests/test_end_to_end.cpp`) for a case matching the
new behavior. A validation rule with no test proving it fires is one
refactor away from silently regressing.

## Reporting

Use the `ReportFindings` tool if available in this session, one entry per
concrete gap found (not per file scanned), ranked most-severe first:
copy-safety (Check 4) and silent numeric corruption (Check 2) before missing
validation ownership (Check 3) before malformed-type gaps (Check 1) before
missing test coverage (Check 5). `failure_scenario` must state the WHY, not
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
