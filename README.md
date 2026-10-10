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
              [--params <json>] [--pallets <csv>] [--trailer <code>] [--groups N]
              [--day <YYYY-MM-DD>] [--lanes N] [--debug] [--help]
```

| Flag | Required | Meaning |
|---|---|---|
| `--product <path>` | yes | Product master CSV |
| `--demand <path>` | yes | Demand extract JSON (STR / CTL / DNM blocks) |
| `--placeholder <path>` | yes | Placeholder JSON (trucks requested per lane) |
| `--params <path>` | no | Params JSON (for example `config/orderBuilderParams.json`). Turns on the Milestone 2 report: segregation groups, cube/weight limit per group, and stacks |
| `--pallets <path>` | with `--params`, for masters without `Pallet_*` columns | Pallet table CSV (`Customer2-Pallet-Data.csv`): weight, height and footprint per pallet type. A product row's own `Pallet_Weight`, `Pallet_Height` and `Pallet_Footprint_*` win over it |
| `--trailer <code>` | no | Trailer to plan against, by `trailerCode` in the params file; default is the one largest on payload, interior height, stack positions and depth (the run stops if no single trailer is) |
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
| 0 | Ran successfully and covered all of the demand |
| 1 | Ran, but the result is incomplete: validation found errors in the data, or (with `--params`) a line that passed validation is in no stack — it has no unit load (for example a Strength outside 0..10, a zero Weight, or a pallet type whose weight, height and footprint neither the product master nor the pallet table gives), a single pallet is taller than the trailer ceiling, or its quantity is invalid — or demand was supplied but no stack was built |
| 2 | Could not run — a missing argument or an unreadable file |

The Milestone 2 report states the outcome on its `Result` line, and each line left
out of stacking is logged with its MATNR and the reason. Validation *warnings* do not
affect the exit code. A clean run of the current
sample data exits 0 with 161 warnings, or 163 with `--params`: Milestone 2 adds the two
lines whose product exceeds its own CRI limit (`exceeds_own_cri`).

## Test data

The input fixtures are **not in the repository**. They are confidential customer
data and are excluded by `.gitignore`:

- `tests/importer/Demand-1.json`, `tests/importer/Customer2-Product-Data.csv` and
  `tests/importer/PlaceHolder-1.json` (17 Aug)
- `tests/importer/crossDay/<yyyymmdd>/` for 2 and 3 September, in the client's own
  layout: `Demands/`, `PlaceHolder/`, `Product-Data/` and `Pushed-Solutions/`

After cloning, copy them to exactly those paths. Each day's demand file is found
by its `REQUEST_ID`, not its name (`Demand-N.json` and `100-STR-<uuid>.json` both
work), so a mislabelled day fails rather than loading another day's data. The
placeholder file is opened by the exact name the client shipped.

Without them, the tests that open the 17 Aug files directly fail with
`Cannot open demand file: ...` or similar. The cross-day tests are skipped instead,
and two tests fail on purpose, listing every missing file:

- `crossDay: the extract fixtures are present, so the extract tests ran`
- `acceptanceHarness: the Truck Builder solution fixtures are present, so the validity tests ran`

So a clone without the data never shows a green suite that ran none of the
extract tests. If you know the data is missing and want to run only the unit tests,
exclude those two tests yourself with `--test-case-exclude`.

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

Current status: every test passes except one, which fails on purpose.
`acceptanceHarness: the floor never exceeds Truck Builder's achieved loads` is
the M3 validity criterion, and it fails until the floor is a valid lower bound on
the September solutions. It prints the number of lane-days exceeded on every run.
It is not marked as expected to fail, so the suite does not report green while the
defect is open.

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

**Wood pallet weight.** The Milestone 1 totals assume 60 lb for a physical wood
pallet (PTL and PGM; TLD and GMA add nothing), so the published M1 figures stay
reproducible. Milestone 2 does not use that figure: every stack, CRI check, group
weight and cube/weight decision weighs the pallet from the supplied data — the
product row's `Pallet_*` columns, or the pallet table given with `--pallets` — so a
client with different pallets needs no code or config change.
