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
