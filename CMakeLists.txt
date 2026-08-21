cmake_minimum_required(VERSION 3.16)
project(order_builder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
  src/converter/converter.cpp
  src/validator/validator.cpp
  src/joiner/joiner.cpp
  src/reporter/reporter.cpp
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
  tests/converter/test_converter.cpp
  tests/validator/test_validator.cpp
  tests/joiner/test_joiner.cpp
  tests/reporter/test_reporter.cpp
)

target_link_libraries(ob_tests PRIVATE ob_core ob_warnings)

add_test(NAME ob_tests COMMAND ob_tests)
