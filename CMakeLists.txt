cmake_minimum_required(VERSION 3.16)
project(order_builder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit build/compile_commands.json so editors (VS Code IntelliSense, clangd)
# pick up the real include paths and flags instead of guessing.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# ---------------------------------------------------------------------------
# Warning flags: zero warnings on both MSVC and GCC.
# ---------------------------------------------------------------------------
add_library(ob_warnings INTERFACE)
if(MSVC)
  target_compile_options(ob_warnings INTERFACE /W4 /WX /utf-8)
else()
  target_compile_options(ob_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

# ---------------------------------------------------------------------------
# ob_core: static library containing all module implementations.
# ---------------------------------------------------------------------------
add_library(ob_core STATIC
  src/importer/importer.cpp
  src/importer/product_importer.cpp
  src/importer/placeholder_importer.cpp
  src/converter/converter.cpp
  src/validator/validator.cpp
  src/joiner/joiner.cpp
  src/reporter/reporter.cpp
  src/pipeline.cpp
  src/params/paramsLoader.cpp
  src/segregation/segregation.cpp
  src/stackRules/stackRules.cpp
  src/bindingConstraint/bindingConstraint.cpp
  src/stackBuilder/stackBuilder.cpp
  src/stackReporter/stackReporter.cpp
  src/trailerSpec/trailerSpec.cpp
  src/palletSpec/palletSpec.cpp
  src/unitLoadMetrics/unitLoadMetrics.cpp
  src/floorBound/floorBound.cpp
  src/floorPlanner/floorPlanner.cpp
  src/demandSelector/demandSelector.cpp
  src/floorReporter/floorReporter.cpp
  src/isoDate/isoDate.cpp
  src/shipCalendar/shipCalendar.cpp
  src/reservationBook/reservationBook.cpp
  src/selectionItem/selectionItem.cpp
  src/floorPattern/floorPattern.cpp
  src/laneClassifier/laneClassifier.cpp
  src/knapsack/knapsack.cpp
  src/patternPacker/patternPacker.cpp
  src/columnGeneration/columnGeneration.cpp
  src/binPacker/binPacker.cpp
  src/selector/selector.cpp
  src/shipDatePlanner/shipDatePlanner.cpp
  src/selectionReporter/selectionReporter.cpp
  src/benchmarkHarness/benchmarkHarness.cpp
  src/selectionParams/selectionParams.cpp
  src/truckCheck/truckCheck.cpp
  src/m5Types/m5Types.cpp
  src/m5Policy/m5Policy.cpp
  src/m5Budget/m5Budget.cpp
  src/m5Ledger/m5Ledger.cpp
  src/m5Physical/m5Physical.cpp
  src/m5Allocation/m5Allocation.cpp
  src/m5Validation/m5Validation.cpp
  src/m5Repair/m5Repair.cpp
  src/m5Methods/m5Methods.cpp
  src/m5Commit/m5Commit.cpp
  src/m5Solver/m5Solver.cpp
  src/m5Orchestrator/m5Orchestrator.cpp
  src/m5Report/m5Report.cpp
  src/m5Integration/m5Integration.cpp
)

target_include_directories(ob_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(ob_core PRIVATE ob_warnings)

# ---------------------------------------------------------------------------
# order_builder: the CLI executable.
# ---------------------------------------------------------------------------
add_executable(order_builder src/main.cpp)
target_link_libraries(order_builder PRIVATE ob_core ob_warnings)

# ---------------------------------------------------------------------------
# ob_tests: unit tests (doctest, single-header, vendored in third_party/).
# ---------------------------------------------------------------------------
enable_testing()

add_executable(ob_tests
  tests/importer/test_importer.cpp
  tests/importer/test_product_importer.cpp
  tests/importer/test_placeholder_importer.cpp
  tests/converter/test_converter.cpp
  tests/validator/test_validator.cpp
  tests/joiner/test_joiner.cpp
  tests/reporter/test_reporter.cpp
  tests/test_end_to_end.cpp
  tests/params/testParamsLoader.cpp
  tests/segregation/testSegregation.cpp
  tests/segregation/testCrossDaySegregation.cpp
  tests/importer/testProductHeaders.cpp
  tests/stackRules/testStackRules.cpp
  tests/bindingConstraint/testBindingConstraint.cpp
  tests/stackBuilder/testStackBuilder.cpp
  tests/stackReporter/testStackReporter.cpp
  tests/testPipelineMilestone2.cpp
  tests/testPipelineMilestone3.cpp
  tests/testCrossDayPipeline.cpp
  tests/testPipelineDeterminism.cpp
  tests/testMilestone2Guarantees.cpp
  tests/testNewExtracts.cpp
  tests/cli/testCli.cpp
  tests/trailerSpec/testTrailerSpec.cpp
  tests/palletSpec/testPalletSpec.cpp
  tests/unitLoadMetrics/testUnitLoadMetrics.cpp
  tests/floorBound/testFloorBound.cpp
  tests/floorPlanner/testFloorPlanner.cpp
  tests/demandSelector/testDemandSelector.cpp
  tests/floorReporter/testFloorReporter.cpp
  tests/acceptance/acceptanceHarness.cpp
  tests/acceptance/testAcceptanceHarness.cpp
  tests/isoDate/testIsoDate.cpp
  tests/shipCalendar/testShipCalendar.cpp
  tests/reservationBook/testReservationBook.cpp
  tests/selectionItem/testSelectionItem.cpp
  tests/selectionItem/testInputTrucks.cpp
  tests/selectionItem/testSelectionProperties.cpp
  tests/floorPattern/testFloorPattern.cpp
  tests/laneClassifier/testLaneClassifier.cpp
  tests/knapsack/testKnapsack.cpp
  tests/patternPacker/testPatternPacker.cpp
  tests/binPacker/testBinPacker.cpp
  tests/selector/testSelector.cpp
  tests/shipDatePlanner/testShipDatePlanner.cpp
  tests/selectionReporter/testSelectionReporter.cpp
  tests/benchmarkHarness/testBenchmarkHarness.cpp
  tests/benchmarkHarness/testM4Benchmark.cpp
  tests/selectionParams/testSelectionParams.cpp
  tests/testPipelineMilestone4.cpp
  tests/m5/testM5Policy.cpp
  tests/m5/testM5Solver.cpp
  tests/m5/testM5Planning.cpp
  tests/m5/testM5Orchestrator.cpp
  tests/m5/testM5Integration.cpp
  tests/m5/testM5RealData.cpp
  tests/m5/testM5Physical.cpp
  tests/m5/testM5Scenarios.cpp
  tests/m5/testM5Routing.cpp
  tests/m5/testM5Coverage.cpp
  tests/m5/testM5Fixtures.cpp
)

# The bin packer's termination test runs the packer on its own thread against a deadline.
find_package(Threads REQUIRED)
target_link_libraries(ob_tests PRIVATE ob_core ob_warnings Threads::Threads)

# Set ON for sanitizer or coverage builds, so timing tests scale their budgets. ASan, TSan and
# Clang's UBSan are detected automatically; GCC's UBSan is not, and needs this.
option(OB_INSTRUMENTED_TESTS "Tests run under sanitizers or other instrumentation" OFF)
if(OB_INSTRUMENTED_TESTS)
  target_compile_definitions(ob_tests PRIVATE OB_INSTRUMENTED_TESTS)
endif()

# The CLI tests run the real executable, so it must be built first and found by path.
add_dependencies(ob_tests order_builder)
target_compile_definitions(ob_tests PRIVATE OB_CLI_PATH="$<TARGET_FILE:order_builder>")
# The M4 benchmark suite writes its report for the client here, beside the build, and beside it
# the diagnosis of what limits each truck's fill.
target_compile_definitions(ob_tests PRIVATE OB_M4_BENCHMARK_REPORT="${CMAKE_BINARY_DIR}/m4BenchmarkReport.txt"
                                            OB_M4_FILL_DIAGNOSIS="${CMAKE_BINARY_DIR}/m4FillDiagnosis.txt"
                                            OB_M4_PROOF_COVERAGE="${CMAKE_BINARY_DIR}/m4ProofCoverage.txt")

# The data-dependent tests open their fixtures by paths relative to the repo
# root (tests/importer/...), so run the binary from there rather than build/.
add_test(NAME ob_tests COMMAND ob_tests
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# The 4-6 August acceptance run on its own; it fails, naming each file, when the data is absent.
add_test(NAME augustAcceptance COMMAND ob_tests -ts=augustAcceptance
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# The M4 benchmark over every extract held. Its presence check is skipped in the full run, so a
# clean clone passes; here --no-skip runs it, so a missing extract fails by name.
add_test(NAME m4Benchmark COMMAND ob_tests -ts=m4Benchmark --no-skip
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# M5's synthetic suite on its own (the m5 test suite inside ob_tests).
add_test(NAME m5Core COMMAND ob_tests -ts=m5
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# M5 on the real 4-6 August extracts; fails, naming each file, when the data is absent.
add_test(NAME m5RealData COMMAND ob_tests -ts=m5RealData --no-skip
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# U-PF-04: the M4 thread test runs clean under --success.
add_test(NAME m4BenchmarkThreadsSuccess
         COMMAND ob_tests "-tc=m4Benchmark: every held extract plans the same at one thread*" --no-skip --success
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# PUB-01..03 (ACC-1) on OR-Library's binpack1.txt (tests/m5/data/orlib/, SHA-256 in README.txt there).
add_test(NAME m5Published COMMAND ob_tests -ts=m5Published --no-skip
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
