# Order Builder

## What this is

Order Builder is a C++17 command-line tool for truck load planning. Milestone 1
reads the three input files for a single planning day, joins the demand lines to
the product master, validates the result, and prints a summary you can check
against your own figures.

Milestone 1 stops there. It does **not** build loads, decide truck counts, apply
do-not-mix or level-load rules, or call Truck Builder. Those begin at Milestone 2.
The truck figure in the summary is the number of trucks *requested* by the
placeholder file — it is read from the input, not calculated.

## Building

Prerequisites:

- CMake 3.16 or newer
- A C++17 compiler
- Ninja (optional, but the commands below use it)

The project builds under MinGW-w64 g++ on Windows and GCC on Ubuntu. There are no
external dependencies to install: doctest and nlohmann/json are single-header
libraries vendored in `third_party/`.

Warnings are errors. CMake applies `-Wall -Wextra -Wpedantic -Werror` on GCC and
Clang, so any warning fails the build.

Windows (MinGW g++):

```bash
cmake -B build -G Ninja
cmake --build build
```

Ubuntu (GCC):

```bash
cmake -B build
cmake --build build
```

The build produces two executables in `build/`: `order_builder` (the CLI) and
`ob_tests` (the test suite). Both link a static library, `ob_core`, which holds
every module implementation.

## Running

```
order_builder --product <csv> --demand <json> --placeholder <json>
              [--params <json>] [--trailer <code>] [--groups N]
              [--day <YYYY-MM-DD>] [--lanes N] [--debug] [--help]
```

| Flag | Required | Meaning |
|---|---|---|
| `--product <path>` | yes | Product master CSV |
| `--demand <path>` | yes | Demand extract JSON (STR / CTL / DNM blocks) |
| `--placeholder <path>` | yes | Placeholder JSON (trucks requested per lane) |
| `--params <path>` | no | Params JSON (for example `config/orderBuilderParams.json`). Turns on the Milestone 2 report: segregation groups, cube/weight limit per group, and stacks |
| `--trailer <code>` | no | Trailer to plan against, by `trailerCode` in the params file; default is the first one listed |
| `--groups N` | no | Print only the N largest groups in the Milestone 2 report; default is all |
| `--day <date>` | no | Planning day, shown in the report header |
| `--lanes N` | no | Print only the N largest lanes; default is all of them |
| `--debug` | no | Verbose logging |
| `--help` | no | Print usage and exit |

Example, run from the repository root:

```bash
./build/order_builder.exe --product tests/importer/Customer2-Product-Data.csv --demand tests/importer/Demand-1.json --placeholder tests/importer/PlaceHolder-1.json --day 2026-08-17 --lanes 6
```

That prints the top-line summary (demand lines, hash total, pallet-equivalents,
weight, lane counts, trucks requested), a per-lane table, and a warnings section.

File paths on the command line are resolved relative to the current working
directory, so run the tool from the repository root if you are using the relative
paths shown above.

Exit codes:

| Code | Meaning |
|---|---|
| 0 | Ran successfully, no validation errors |
| 1 | Ran successfully, but validation found errors in the data |
| 2 | Could not run — a missing argument or an unreadable file |

Validation *warnings* do not affect the exit code. A clean run of the current
sample data exits 0 with 161 warnings.

## Test data

The three input fixtures are **not in the repository**. They are confidential
customer data and are excluded by `.gitignore`:

- `tests/importer/Demand-1.json`
- `tests/importer/Customer2-Product-Data.csv`
- `tests/importer/PlaceHolder-1.json`

After cloning, place your own copies at exactly those three paths. Most of the
test suite reads them directly, so without them a large number of tests fail with
`Cannot open demand file: tests/importer/Demand-1.json` or similar. This is the
single most likely thing to trip up a new clone — if the tests fail immediately
after a clean build, check this first.

## Tests

The suite is doctest-based and lives in one executable. Either of these works:

```bash
ctest --test-dir build --output-on-failure
```

```bash
./build/ob_tests.exe
```

CTest is configured to run the binary from the repository root, so the fixture
paths above resolve either way. Run the executable directly if you want the
per-test-case output, or want to pass doctest flags such as `--test-case=<name>`.

Current status: **80 test cases, all passing** (155,938 assertions). That covers
the module unit tests plus 8 end-to-end acceptance tests in
`tests/test_end_to_end.cpp`, which run the real pipeline against the real files
and assert the exact published figures.

## Project layout

```
order-builder/
  CMakeLists.txt        Build configuration: ob_core, order_builder, ob_tests
  include/              All public headers (flat)
  src/                  Implementations, grouped by module
  tests/                doctest unit tests, plus the end-to-end tests
  third_party/          Vendored single-header libraries
```

| Path | Contents |
|---|---|
| `include/` | `importer.hpp`, `product_importer.hpp`, `placeholder_importer.hpp`, `joiner.hpp`, `converter.hpp`, `validator.hpp`, `reporter.hpp`, `pipeline.hpp`, `logger.hpp`, and the data-structure headers `demand_types.hpp`, `product_types.hpp`, `placeholder_types.hpp` |
| `src/importer/` | `importer.cpp` (demand JSON), `product_importer.cpp` (product CSV), `placeholder_importer.cpp` (placeholder JSON) |
| `src/joiner/` | `joiner.cpp` |
| `src/converter/` | `converter.cpp` |
| `src/validator/` | `validator.cpp` |
| `src/reporter/` | `reporter.cpp` |
| `src/pipeline.cpp` | The whole run, wired together |
| `src/main.cpp` | CLI entry point: argument parsing only |
| `tests/` | One test file per module under `tests/<module>/`, plus `tests/test_end_to_end.cpp` |
| `third_party/` | `doctest.h`, `json.hpp` |

`include/types.hpp` is a signpost only. Every structure it once declared has moved
to its own module header and the file now declares nothing; it is kept so a reader
following an old reference is told where each type went.

## Modules

| Module | Responsibility |
|---|---|
| **Importer** | Three readers — demand JSON, product master CSV, placeholder JSON. They load and nothing more: a missing or malformed field becomes an empty string or zero, and the Validator decides whether that matters. They throw only when a file cannot be opened or parsed at all. |
| **Joiner** | Matches each demand line to its product master record on MATNR. Unmatched lines are kept with a null product rather than dropped. Where a product has several pallet-type variants it chooses by a documented preference order and flags the line as ambiguous. |
| **Converter** | Pure arithmetic, and the only place unit conversion happens: inches to centimetres, demand quantity to pallet-equivalents, pallet-equivalents to pounds. It never throws, never logs and never rejects a line — a quantity it cannot convert comes back as 0.0. |
| **Validator** | The only module that judges. It walks the joined data and produces a report of warnings and errors, each naming the rule, the material and the offending value. Nothing is rejected silently. |
| **Reporter** | Presentation only — no business logic, no file access, no unit conversion. Aggregates the joined data and the placeholder file into a day summary and prints the three output sections. |
| **Pipeline** | Runs the stages in order: read three files, join, convert, validate, summarise. `main()` does nothing but parse arguments and call it, so the end-to-end tests exercise the real pipeline rather than a shell command. |

One note on terminology used throughout the output: the pallet figure is a summed
**pallet-equivalent**, not a count of physical pallets. Each demand line converts
to a pallet fraction and those fractions are added across the lane, so a lane
total such as 6,708.9 is a running sum rather than one pallet split into tenths.
Fractions are summed, not rounded; rounding is available as a parameter but is off
by default.

## Known open items

**Pallet-type variants — awaiting a selection rule.** 18 product IDs appear more
than once in the product master, once per pallet type (TLD / PTL / PGM / GMA).
Same product, same dimensions, same weight, but a different `Cases_Unit_Load` —
ID 105553001, for example, is 84 cases per unit load as GMA and 168 as TLD, so the
choice of variant halves or doubles the pallet figure for that line. The demand
file carries no pallet-type field, so the correct variant cannot be derived from
the input. The Joiner therefore applies a documented preference order and flags
every affected line: 144 of 24,357 lines (0.59%) in the current sample. They
appear in the warnings section as `ambiguous_pallet_type`. A selection rule would
replace the preference order.

**Large-line check — a review threshold, not a hard guard.** A line sent in eaches
but tagged as cases inflates the pallet count and conjures trucks that do not
exist. No signal in the supplied data separates that case from a genuinely large
order: TRANS exceeds AVAIL_QTY on every line, so availability is a forecast rather
than a stock check; BSTRF is zero throughout; TRANS is always a whole number, so
eaches and cases look identical; and legitimate lines reach 1,010 pallets. A fixed
ceiling would reject real orders. It is therefore a configurable warning
threshold, defaulting to 300 pallets, which flags lines for review and never
blocks a run. They appear in the warnings section as `large_line` — 17 lines in
the current sample. A business rule would replace the threshold.

**Wood pallet weight.** Weight assumes 60 lb for a physical wood pallet (PTL and
PGM; TLD and GMA add nothing). That figure is a documented assumption rather than
a supplied one, and is a named parameter so it can be corrected in one place.
