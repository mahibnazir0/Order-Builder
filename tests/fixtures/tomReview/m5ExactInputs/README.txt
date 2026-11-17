Exact review inputs requested for M5 R-1a, R-1b and R-5a
Prepared 16 November 2026

These are the original synthetic regression cases from our reviews, not reconstructed customer shipments. The cases start with fixed prebuilt stacks. Each stack is one floor item; the unit_loads_per_stack field is its demand quantity. No restacking or further stacking is required. Floor rotation is allowed under the existing supported row patterns. All items have priority 0 and ship date 6 August 2026. Only one truck is reserved. Do not treat the fixture sizes as actual customer SKU dimensions.

R-1a: 20 A-LARGE items of footprint 98.75 x 40, 20 B-SMALL of 48 x 40. Each item is 50 inches high, weighs 1,000 lb and represents one UL. Trailer 630 x 100 x 108 inches, 45,000 lb payload, 64-UL cap. Expected 25 UL. Witness: five large single rows plus ten rows of two small items, length 600 inches.

R-1b: two 60 x 60 and two 50 x 50 items. Each is 50 inches high, 100 lb and one UL. Trailer FLOOR IS 100 x 100, not 630 x 100; height 108 inches, payload 45,000 lb and cap 64 UL. Expected 2 UL. Witness: two small items side by side, length 50 inches.

R-5a: the fourteen items are listed individually in R-5a.json and in the original exactness-counterexample.json. Preserve product ordering DISTRACTOR, P100...P112. Trailer 630 x 100 x 108 inches, 45,000 lb payload, 64-UL cap. Expected 23 UL. The original feasible witness leaves DISTRACTOR and loads indices 1-13 in 617 inches. The current implementation may choose a different equally valid 23-UL subset; at d028f24 our replay selected 23 in 624 inches. The 617-inch number is a witness, not a requirement that the selector choose that exact layout when its declared tie-break differs.

run_requested_cases.cpp reads the three JSON files and runs the current library. Build the contractor repository at d028f24b9ac04ff17b98489017b38b20dd35d981 first. From its root:

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 6
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I include -I tests -I third_party /path/to/run_requested_cases.cpp build/libob_core.a -pthread -o /path/to/inputs/run_requested_cases

Run the executable from the folder containing R-1a.json, R-1b.json and R-5a.json. results.json records our independently replayed 25 / 2 / 23 results. This does not validate unpublished M5 code or justify leaving the tests disabled after the inputs have arrived.

original_review_probe.cpp is the actual broader probe source from the 10 November review, including the original R-1a and R-1b construction. original_fourteen_stack_probe.cpp plus monotonicity-standard-results.json reproduce all nine fourteen-stack variants; run them from a directory containing that seed file. These original sources need the contractor repository headers and built library.
