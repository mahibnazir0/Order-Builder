# Code generation instructions

## Optimization

Always write optimized code:

- Choose efficient algorithms and data structures for the access pattern (e.g.
  hash map for lookups, not linear scan; reserve/pre-size containers when the
  size is known).
- Avoid unnecessary copies and allocations — pass by `const&` for anything
  non-trivial, prefer moves over copies, avoid rebuilding a `std::stringstream`
  or similar per iteration when it can be built once.
- Don't repeat work a loop can hoist out, and don't do a second pass over data
  a single pass could handle.
- Optimize for the real bottleneck (usually I/O or the hot loop over the
  largest collection), not for constant-factor micro-optimizations in cold
  paths.

## Naming

Use camelCase for variable and function names (e.g. `toPallets`,
`casesUnitLoad`, `lineIndex`), not snake_case.

Existing code in this repo predates this convention and is written in
snake_case throughout. Apply camelCase to new code going forward; don't mass-
rename existing identifiers as a side effect of an unrelated change.

The same split applies to filenames: existing files (`product_importer.cpp`,
`demand_types.hpp`, ...) stay snake_case — don't rename them as a side effect
of an unrelated change. Any brand-new file (a new module, header, or test
file) uses camelCase instead (e.g. `loadPlanner.cpp`, `loadPlanner.hpp`), so
naming across the codebase gradually shifts the same way variable and
function names already do, without a disruptive one-shot rename.

Variable and function names must be self-explanatory — name a thing for what
it holds or does (e.g. `unmatchedMatnrs`, `palletHasWood`) rather than a
generic or abbreviated name that needs a comment to explain it.

## Simplicity

Keep code simple and as clean as possible:

- Prefer the straightforward solution over a clever one. Don't add
  abstraction, indirection, or configurability the task doesn't need.
- If the same logic (or near-identical logic) is needed in more than one
  place, factor it into a shared function rather than copy-pasting it —
  duplicated logic drifts out of sync as one copy gets fixed and the other
  doesn't.
- Don't over-generalize a one-off into a shared helper prematurely, either —
  extract common code once a real second caller exists, not speculatively.

## Comments

Don't comment blindly. Well-named identifiers and straightforward code should
speak for themselves — most lines need no comment at all.

Add a comment only where the logic is genuinely non-obvious: a non-trivial
algorithm, a business rule that isn't derivable from the code, a workaround
for a specific bug or constraint, or a hidden invariant a future reader could
easily violate. Explain *why*, not *what* — if removing the comment wouldn't
leave a reader confused, it shouldn't be there.

## Validating external input

Every boundary where data enters the pipeline (CSV cell, JSON field, JSON
block) needs a validation path that actually runs before you rely on "the
Validator judges it later" — that comment is only true where the check has
been written. When you add or change a reader/importer, trace each field it
produces to the specific downstream check that would catch a bad value; if
none exists, add it rather than assuming the general pattern covers it.

- A missing/wrong-type field silently defaulting is fine for genuinely
  optional data. For any value that becomes a divisor, a multiplier, or feeds
  a running total (case capacity, weight, load counts, dimensions), validate
  the full range — negative and non-finite (NaN/Inf), not only zero. A bug
  caught in the zero case almost always has a negative or NaN sibling that
  needs the identical treatment; fixing one without the other leaves the
  defect in place under a different input.
- A top-level block that is structurally optional (e.g. `"STR"`) but PRESENT
  with the wrong shape (an object instead of an array) must raise an explicit
  error. `contains(key) && is_array()` silently conflates "block is absent"
  with "block is malformed" — check `contains(key)` and the type separately,
  and report the malformed case instead of falling through to "treat as
  absent."
- When adding a `get_or`-style fallback for a new field, check whether the
  fallback value could also be a legitimate value for that field (e.g. `0`
  loads is a real possible value; `-3` loads never is). A fallback that lands
  on a valid-looking value hides the parse failure instead of surfacing it to
  the Validator.
- A field that exists on a record but has no corresponding validation rule
  anywhere in the codebase (grep for it — if the only hits are "read it" and
  "sum it," there is no rule) is a gap, not an implicit pass. Every field that
  flows into a total, a threshold, or a downstream decision needs an owner
  that checks it.

## Ownership and copy safety

A struct that holds non-owning raw pointers or references into another
object's containers (e.g. a joined-line type holding `const Record*` into a
`std::vector` owned by a sibling struct) must not rely on the implicit copy
constructor — copying the pointer's *value* does not make it valid against a
copy of what it points to. Before adding a pointer/reference member to a type
that could plausibly be copied or that lives inside a larger aggregate someone
might copy, settle ownership explicitly:

- delete the copy operations and make the type move-only, or
- write an explicit copy that rebuilds every pointer against the new object's
  own storage.

Don't leave this to the default and assume it will only ever be moved —
check whether anything containing the type is ever copied (including
indirectly, as a member of a larger result struct), not just whether the type
itself is copied directly today.

## Manual memory management

Prefer RAII and the standard containers/smart pointers (`std::vector`,
`std::string`, `std::unique_ptr`, `std::shared_ptr`) over raw `new`/`delete`
so that ownership and cleanup are automatic and this whole category of bug is
structurally impossible. Reach for a raw `new` only when nothing in the
standard library already owns the lifetime you need.

When manual allocation genuinely is necessary:

- Every `new` needs an owner responsible for exactly one matching `delete`
  (array form `new[]` with `delete[]`, never mixed) — no leaks, and no two
  owners that could both try to free the same allocation (no double-free).
- Immediately after `delete`ing a pointer, set it to `nullptr`. A later
  accidental use of a null pointer crashes at the point of misuse; a later
  use of a dangling (already-freed) pointer corrupts memory silently and can
  fail somewhere unrelated, much later, in a way that is far harder to trace
  back to the actual bug.
- Never return, store, or hand off a pointer to memory that has already been
  freed, and never free memory through more than one pointer that references
  it.

## Testing

New code needs tests — new modules, functions, and bug fixes should come with
test coverage, not just a manual check that it works.

- Test the real code path, not a mock of it. A test that stubs out the unit
  under test proves the stub works, not the code — call the actual
  function/class being tested, and reach for a fake or mock only at a true
  external boundary (network, filesystem, clock) that the test genuinely
  cannot use for real.
- Prefer exercising real data over hand-built stand-ins where practical (this
  repo's existing tests load the real product/demand/placeholder fixtures
  rather than reimplementing the logic under test against synthetic input) —
  a reimplementation can drift from the real thing and pass while the real
  thing is broken.
- Keep tests simple: one behavior per test case, a clear name that states
  what's being verified, minimal setup, and assertions that read as the
  expected outcome. A test that needs its own comment to explain what it's
  checking is a sign the test (or the code) needs simplifying.
- The supplied fixtures show what valid data looks like; they do not exercise
  what invalid data looks like, and a suite scoped only to them will not catch
  a malformed-input bug no matter how thorough it is on the happy path.
  Alongside fixture-based tests, add cases for the invalid neighbourhood of
  every external input:
  - a negative and a non-finite (NaN/Inf) value for any numeric field that
    is later divided, multiplied, or accumulated into a total
  - an optional JSON block present with the wrong type (object instead of
    array, string instead of number) rather than simply absent
  - a copy of any result type that holds pointers/references into another
    object, if that type is ever copyable
- When a bug fix addresses one bad-input case (e.g. a field parsed as exactly
  `0`), check whether the same code path has other bad-input cases that share
  the root cause (negative, NaN, wrong type) and cover those in the same
  change — don't fix only the specific value that was reported.
