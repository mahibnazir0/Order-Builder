# Order Builder

A freight load-planning tool: imports product, demand, and placeholder data,
validates it, joins it, and reports per-lane summaries.

Status: **Milestone 1 -- skeleton only.** No module logic is implemented yet;
see the `TODO(mahib)` / `TODO(dev2)` markers throughout `include/` and `src/`
for the work split.

## Layout

```
order-builder/
  CMakeLists.txt       Build config: ob_core, order_builder, ob_tests
  include/              Public headers (types.hpp, logger.hpp, per-module headers)
  src/                   Module implementations + main.cpp
  tests/                 doctest-based unit tests, one placeholder per module
  third_party/           Vendored single-header libs (doctest.h, json.hpp) -- empty for now
  data/test/              Sample input JSON for local testing -- empty for now
```

## Modules

| Module    | Header                              | Owner (Milestone 1 stub) |
|-----------|--------------------------------------|---------------------------|
| Importer  | `include/importer/importer.hpp`     | product/placeholder: mahib, demand: dev2 |
| Converter | `include/converter/converter.hpp`   | Saif |
| Validator | `include/validator/validator.hpp`   | mahib |
| Joiner    | `include/joiner/joiner.hpp`         | Saif  |
| Reporter  | `include/reporter/reporter.hpp`     | mahib |

## Prerequisites before building

Drop these into `third_party/` (not included in this skeleton):

- `third_party/doctest.h` -- [doctest](https://github.com/doctest/doctest) single-header test framework
- `third_party/json.hpp` -- [nlohmann/json](https://github.com/nlohmann/json) single-header JSON library

Drop sample input files into `data/test/` (Tom's sample JSON).

## Build (Ubuntu / GCC)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Build (Windows / MSVC)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## Running

```bash
./build/order_builder --product data/test/products.json --demand data/test/demand.json --placeholder data/test/placeholder.json --day 2026-08-22 --debug
```

Milestone 1's `main.cpp` only parses arguments and prints usage -- it does
not call into any module yet.
