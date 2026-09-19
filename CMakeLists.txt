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
)

target_link_libraries(ob_tests PRIVATE ob_core ob_warnings)

# The data-dependent tests open their fixtures by paths relative to the repo
# root (tests/importer/...), so run the binary from there rather than build/.
add_test(NAME ob_tests COMMAND ob_tests
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
