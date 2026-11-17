#pragma once
// ============================================================================
// m5UnitLoads.hpp - unit-load counts and the per-slot cap (ED-20, R-9, EC-43/44).
//
// A cap counts the component unit loads a truck carries, summed over every source
// line in every whole stack it holds: never stacks, rows or pattern copies. The cap
// is a whole number held as a signed 64-bit integer, from 1 to 2^63 - 1. M4 may deal
// a line out in fractions finer than 0.01, so an atom's quantity is counted in whole
// hundredths of a unit load (D3), rounded up when it is not already on that step: the
// count never understates a load. Every sum and product is checked, never wrapped or
// clamped, and the comparison with the cap is exact.
// ============================================================================

#include "json.hpp"
#include "m5Types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ob {
namespace m5 {

struct UnitLoadCap {
    std::optional<std::int64_t> value;  // empty when `error` is set
    std::string error;
};

// Exact text: decimal digits only, from 1 to 2^63 - 1. No sign, point, exponent, space or text.
UnitLoadCap unitLoadCapFromText(const std::string& text);

// A number read as a double: finite, whole, from 1 up to but excluding 2^63.
UnitLoadCap unitLoadCapFromNumber(double value);

// A named field of a configuration block: missing, null, text and fractional values are errors; an
// integer is taken exactly, so 2^63 - 1 is representable and 2^63 is refused.
UnitLoadCap unitLoadCapFromJson(const nlohmann::json& block, const std::string& field);

// A unit-load quantity in whole hundredths, rounded up unless it is within 1e-9 (relative) of a
// hundredth: finite, from 0 and small enough for hundredths to be exact in a double (about 9e13 unit
// loads). Empty otherwise.
std::optional<std::int64_t> hundredthsOfUnitLoads(double quantity);

// The cap in hundredths, saturating at the largest 64-bit value: no total can exceed that, so a cap
// above it never binds, and nothing wraps.
std::int64_t capInHundredths(std::int64_t cap);

// Empty when the result does not fit a signed 64-bit count.
std::optional<std::int64_t> addCounts(std::int64_t left, std::int64_t right);
std::optional<std::int64_t> multiplyCounts(std::int64_t left, std::int64_t right);

// The atom's unit loads in hundredths: its components' quantities summed with multiplicity, then counted
// as above. Empty for a negative or non-finite component or a total out of range.
std::optional<std::int64_t> unitLoadHundredthsOfAtom(const Atom& atom);

// The checked sum over the named atoms (indices into input.atoms).
std::optional<std::int64_t> unitLoadHundredthsOfAtoms(const M5LaneDateInput& input, const std::vector<std::size_t>& atoms);

// "12.50", from hundredths, for findings.
std::string unitLoadsText(std::int64_t hundredths);

} // namespace m5
} // namespace ob
