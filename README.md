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
              [--params <json> --demand-rule <rule>] [--pallets <csv>]
              [--layer-sizes <csv>] [--trailer <code>] [--groups N]
              [--day <YYYY-MM-DD>] [--lanes N] [--debug] [--help]
```

| Flag | Required | Meaning |
|---|---|---|
| `--product <path>` | yes | Product master CSV |
| `--demand <path>` | yes | Demand extract JSON (STR / CTL / DNM blocks) |
| `--placeholder <path>` | yes | Placeholder JSON (trucks requested per lane) |
| `--params <path>` | no | Params JSON (for example `config/orderBuilderParams.json`). Turns on the Milestone 2 report (segregation groups, cube/weight limit per group, and stacks) and the Milestone 3 truck floor. Needs `--demand-rule` |
| `--demand-rule <rule>` | with `--params` | Which ship dates (DATFR_TA) get a floor, each on its own; there is no default. `calendar:YYYY-MM-DD`: the dates the demand file's CTL calendar gives a plan made on that date. `shipDates:YYYY-MM-DD[:YYYY-MM-DD]`: that date, or every date from the first to the last. `wholeExtract`: every line as one floor, a check on the extract rather than a daily floor |
| `--pallets <path>` | with `--params`, for masters without `Pallet_*` columns | Pallet table CSV (`Customer2-Pallet-Data.csv`): weight, height and footprint per pallet type. A product row's own `Pallet_Weight`, `Pallet_Height` and `Pallet_Footprint_*` win over it |
| `--layer-sizes <path>` | no | A product master to borrow `Layer_Length` and `Layer_Width` from, for a master without them; fills only products that have none, matched on ID and Pallet_ID. A file with no layer sizes stops the run |
| `--trailer <code>` | no | Trailer to plan against, by `trailerCode` in the params file; default is the one largest on payload, interior height, volume and stacked floor area (the run stops if no single trailer is) |
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

With `--params` and a demand rule it also prints the Milestone 2 stacks and a truck floor
for each ship date the rule covers. For the 17 August plan, a Monday:

```bash
./build/order_builder.exe --product tests/importer/Customer2-Product-Data.csv --demand tests/importer/Demand-1.json --placeholder tests/importer/PlaceHolder-1.json --params config/orderBuilderParams.json --pallets tests/importer/crossDay/20260929/Product-Data/Customer2-Pallet-Data.csv --demand-rule calendar:2026-08-17 --lanes 6
```

The extract's MONDAY calendar row ships two to three days later, so the report floors 19 and
20 August separately, each beside the placeholders reserved for that day.

File paths on the command line are resolved relative to the current working
directory, so run the tool from the repository root if you are using the relative
paths shown above.

Exit codes:

| Code | Meaning |
|---|---|
| 0 | Ran successfully: every line validated, stacked and counted toward the floor |
| 1 | Validation found errors in the data |
| 2 | Could not run: a missing argument, an unreadable file, an invalid demand rule, a calendar with no usable row for the plan date, or `--demand` and `--placeholder` naming the same file |
| 3 | A line that passed validation is in no stack: it has no unit load (for example a Strength outside 0..10, a zero Weight, or a pallet type whose weight, height and footprint neither the product master nor the pallet table gives), a single pallet is taller than the trailer ceiling, or its quantity is invalid; or demand was supplied but no stack was built |
| 4 | A floor left out a line shipping on its date, or a line has no real ship date (DATFR_TA) |
| 5 | No line of a non-empty extract ships on the rule's dates |
| 6 | No floor was computed because `--params` was not given (the Milestone 1 report is still printed) |

When several apply, the lowest code wins.

The Milestone 2 report states the outcome on its `Result` line, and each line left
out of stacking is logged with its MATNR and the reason. Validation *warnings* do not
affect the exit code. The current sample data has 50 warnings without `--params`
(exit 6) and 52 with it (exit 0): Milestone 2 adds the two
lines whose product exceeds its own CRI limit (`exceeds_own_cri`).

## Test data

The input fixtures are **not in the repository**. They are confidential customer
data and are excluded by `.gitignore`:

- `tests/importer/Demand-1.json`, `tests/importer/Customer2-Product-Data.csv` and
  `tests/importer/PlaceHolder-1.json` (17 Aug)
- `tests/importer/crossDay/<yyyymmdd>/` for 2 and 3 September and 29 September to
  5 October, in the client's own layout: `Demands/`, `PlaceHolder/`, `Product-Data/` and
  (2 and 3 September) `Pushed-Solutions/`. The 5 October master also supplies the layer
  sizes the older masters lack.
- `tests/importer/august/` for the 4-6 August acceptance run: the client's
  `August-4-6-data` folder exactly as shipped (`inputs/` and `results/`). See
  [August acceptance](#august-acceptance).

After cloning, copy them to exactly those paths. Each day's demand file is found
by its `REQUEST_ID`, not its name (`Demand-N.json` and `100-STR-<uuid>.json` both
work), so a mislabelled day fails rather than loading another day's data. The
placeholder file is opened by the exact name the client shipped.

Without them, the tests that open the 17 Aug files directly fail with
`Cannot open demand file: ...` or similar. The cross-day tests are skipped instead,
and two tests fail on purpose, listing every missing file:

- `crossDay: the extract fixtures are present, so the extract tests ran`
- `new extracts: every day's fixtures are present, so the extract tests ran`
- `acceptanceHarness: the Truck Builder solution fixtures are present, so the validity tests ran`
- `acceptanceHarness: the August acceptance files are present, so the August tests ran`

So a clone without the data never shows a green suite that ran none of the
extract tests. If you know the data is missing and want to run only the unit tests,
exclude those tests yourself with `--test-case-exclude`.

### August acceptance

M3 is checked against Truck Builder's 4-6 August solutions. Copy the client's
`August-4-6-data` folder into `tests/importer/august/` as it is, so that
`tests/importer/august/inputs/Aug4/Demand-Aug5-2.json` and
`tests/importer/august/results/Aug4/raw-json/` exist. The August master has no layer
sizes, so the run borrows them from the 5 October master at
`tests/importer/crossDay/20261005/Product-Data/Customer2-Product-Data.csv`. Then run:

```bash
./build/ob_tests -ts=augustAcceptance
```

or `ctest --test-dir build -R augustAcceptance`. If any file is missing, the run fails
and names every missing path; it never passes with nothing checked. With the data in
place it checks, for each of the three runs: that the client's 454 lane groups and
1,308 loads reproduce, that every shipped line and quantity is counted in the floor, and
that the floor never exceeds the trucks Truck Builder used on the same lane and ship date,
for the volume those trucks shipped (632 lanes and ship dates; a lane group whose loads ship
on two dates is two of them). It pins the client's two worked examples: load 1344793 (60 PAL
of 104578600 is 30 unit loads, 25,284 lb of cargo plus its pallets, on one truck) and load
1343393 (432 DIS and 800 CS are 32 unit loads, on one truck). It also prints the measured
August floor (1,269 trucks on 28 Oct 2026) beside the milestone plan's 1,219. 1,269 is a
measured result, not an approved target: the plan's figure stands until the client confirms
the acceptance criterion after their retest.

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

Current status: every test passes. The M3 validity criterion, that the floor never
exceeds the trucks Truck Builder used on any lane and ship date, holds on all six solved
days (2 and 3 September and 4-6 August) and prints the count on every run. It is not
marked as expected to fail, so any regression turns the suite red.

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
| **Joiner** | Matches each demand line to its product master record on MATNR. Unmatched lines are kept with a null product rather than dropped. Where a product is listed on several pallet types it takes the one whose `Cases_Unit_Load` divides the line's quantity evenly, and flags the lines the quantity does not settle. |
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

**Pallet-type variants — tie-break and fallback awaiting the client.** 18 product IDs
appear more than once in the product master, once per pallet type (TLD / PTL / PGM / GMA).
Same product, same dimensions, same weight, but a different `Cases_Unit_Load` — ID
105553001, for example, is 84 cases per unit load as GMA and 168 as TLD, so the choice
halves or doubles the pallet figure for that line. The demand file carries no pallet-type
field. Following the client's rule (27 Oct 2026), companies ship in full unit loads, so the
Joiner takes the type whose `Cases_Unit_Load` divides the line's quantity evenly: GMA when GMA
does, TLD when only TLD does. Two cases are still to confirm, and every such line is a
warning naming the quantity and each type's unit load:

- `pallet_type_tie`: several types divide the quantity evenly (every TLD quantity of
  105553001 is also a GMA one; 728 CS of 104875007 divides by GMA 104 and TLD 182). GMA is
  taken, as the rule reads. 35 of 24,357 lines on 17 August.
- `pallet_type_no_fit`: no type divides it (5 DIS of 104725105 against TLD 33 and GMA 15;
  25 CS of 105553001). The preference order TLD, PTL, PGM, GMA decides. 1 line on 17 August.

On every extract supplied, the single type that divides evenly is the one the old
preference order already chose; only the tie-break to GMA moved any figure.

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

**Units of measure.** A demand quantity counts the product's base unit, the master's
"case", whatever its unit of measure calls it (CS, DIS or PAL), and is divided by
`Cases_Unit_Load` to give pallet-equivalents (client, 27 Oct 2026). The client's 16 Oct
example: 18 DIS make one unit load. For a PAL product with `Cases_Unit_Load` 2, two PAL stack
into one unit load, so 60 PAL is 30 unit loads; read as 60, two of Truck Builder's August
loads would be over the 45,000 lb limit. Converted figures are cargo: each unit load's pallet
weight is added on top. A demand line whose unit differs from its product's unit in the
master is an `uom_mismatch` error and is left out of every total.

**Known limitation: no configured unit conversions.** The client's system can convert a
demand unit that differs from the product's unit through configured conversions. Order
Builder has no such configuration: it rejects every mismatch, which is stricter. No line on
any extract supplied so far mismatches, so nothing is lost on them, but a future extract with
a mismatched line would have it left out and reported rather than converted.

**Floor per ship date.** The floor is calculated separately for each ship date (DATFR_TA,
the scheduled ship date; DATTO_TA is the scheduled arrival date), from the volume that ships
on it, and set beside that day's placeholders lane by lane (client, 24 and 27 Oct 2026). A
run covering Saturday and Sunday gets two floors, never one accumulated over both. Which
dates a run covers comes from the upstream calendar, the demand file's CTL block: the row
dated the plan date if there is one, otherwise the plan's weekday row, read the same way for
every weekday (`--demand-rule calendar:YYYY-MM-DD`). The dates can also be given directly
(`shipDates:`). Lines shipping after the run's last date are future demand the planner can
see; they count toward none of its floors, and pulling them forward onto an earlier truck is
a later stage's decision. The report counts them, the lines due before the first date, and
placeholders for dates the run does not cover.

**Placeholders outside a run's dates (open policy point).** A placeholder whose ship date
falls outside the run's calendar window is counted and reported ("Reserved, other dates" in
section A of the floor report) but is set against none of the run's daily floors. The Aug5
run, for example, has 52 trucks booked for 9 Aug while its calendar window is 7-8 Aug. This
is the current, provisional behaviour: how such reservations should be treated is still open
with the client (28 Oct 2026).

**Pallet weight.** Every milestone weighs a pallet the same way, from the item
master first (client ruling, 13 Oct 2026): the product row's `Pallet_*` columns, then
the pallet table given with `--pallets`, then a built-in fallback that copies the
client's own table (PTL and PGM 65 lb, TLD 1 lb, GMA 0.1 lb) for a master with no
pallet columns, such as 17 August. A client with different pallets needs no code or
config change. A negative or non-finite pallet weight is an `invalid_pallet_weight`
error that excludes the line.

**Truck floor capacity.** The Milestone 3 floor takes a trailer's capacity from its floor
area and volume, not from a count of pallet positions (client, 12 Oct 2026): a truck holds at
most interior length x width x ceiling of footprint x unit-load height, or its payload in
weight. `stackPositions` is still read for the Milestone 2 cube-or-weight split.

**Unit-load footprint.** A stack covers the larger of its pallet and its layer (client,
15 Oct 2026). TLD and GMA have no real pallet, so they cover their layer (`Layer_Length` x
`Layer_Width` in the master); PTL and PGM cover the larger of 48 x 40 and the layer on each
side. Masters before 29 Sep have no layer columns: pass `--layer-sizes` with a later master to
borrow them by ID and Pallet_ID. Without a layer size, the pallet's footprint stands in and
the floor report counts those lines.

**Deck height.** The shipped params count the pallet deck toward unit-load height
(`floorDeckHeight: Included`, client ruling, 12 Oct 2026), so a PTL or PGM load
carries its 6 in wood deck against the 108 in ceiling. The 0.1 in TLD/GMA deck is a
placeholder and adds nothing.
