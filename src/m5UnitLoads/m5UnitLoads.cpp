#include "m5UnitLoads.hpp"

#include <cmath>
#include <limits>

using namespace std;

namespace ob {
namespace m5 {

namespace {

constexpr double kStepTolerance = 1e-9;
constexpr double kTwoToThe53 = 9007199254740992.0;
constexpr double kTwoToThe63 = 9223372036854775808.0;

UnitLoadCap refused(const string& why) {
    UnitLoadCap cap;
    cap.error = "the unit-load cap " + why + ": it must be a whole number from 1 to 9223372036854775807";
    return cap;
}

}  // namespace

UnitLoadCap unitLoadCapFromText(const string& text) {
    if (text.empty()) return refused("is empty");
    int64_t value = 0;
    for (const char digit : text) {
        if (digit < '0' || digit > '9') return refused("'" + text + "' is not made of digits only");
        const optional<int64_t> shifted = multiplyCounts(value, 10);
        const optional<int64_t> grown = shifted ? addCounts(*shifted, digit - '0') : nullopt;
        if (!grown) return refused("'" + text + "' is past 2^63 - 1");
        value = *grown;
    }
    if (value < 1) return refused("'" + text + "' is not above zero");
    UnitLoadCap cap;
    cap.value = value;
    return cap;
}

UnitLoadCap unitLoadCapFromNumber(double value) {
    if (!isfinite(value)) return refused("is not finite");
    if (value < 1.0) return refused("is not above zero");
    if (value >= kTwoToThe63) return refused("is past 2^63 - 1");
    if (value != floor(value)) return refused("is not a whole number");
    UnitLoadCap cap;
    cap.value = static_cast<int64_t>(value);
    return cap;
}

UnitLoadCap unitLoadCapFromJson(const nlohmann::json& block, const string& field) {
    if (!block.is_object() || !block.contains(field) || block.at(field).is_null()) return refused("'" + field + "' is missing");
    const nlohmann::json& entry = block.at(field);
    if (entry.is_number_unsigned()) {
        const uint64_t value = entry.get<uint64_t>();
        if (value > static_cast<uint64_t>(numeric_limits<int64_t>::max())) return refused("'" + field + "' is past 2^63 - 1");
        if (value == 0) return refused("'" + field + "' is not above zero");
        UnitLoadCap cap;
        cap.value = static_cast<int64_t>(value);
        return cap;
    }
    if (entry.is_number_integer()) return refused("'" + field + "' is not above zero");
    if (entry.is_number_float()) return unitLoadCapFromNumber(entry.get<double>());
    return refused("'" + field + "' is not a number");
}

optional<int64_t> hundredthsOfUnitLoads(double quantity) {
    if (!isfinite(quantity) || quantity < 0.0) return nullopt;
    const double scaled = quantity * 100.0;
    if (scaled > kTwoToThe53) return nullopt;
    const double nearest = nearbyint(scaled);
    if (fabs(scaled - nearest) <= kStepTolerance * max(1.0, scaled)) return static_cast<int64_t>(nearest);
    return static_cast<int64_t>(ceil(scaled));
}

int64_t capInHundredths(int64_t cap) {
    const optional<int64_t> scaled = multiplyCounts(cap, 100);
    return scaled ? *scaled : numeric_limits<int64_t>::max();
}

optional<int64_t> addCounts(int64_t left, int64_t right) {
    if (right > 0 && left > numeric_limits<int64_t>::max() - right) return nullopt;
    if (right < 0 && left < numeric_limits<int64_t>::min() - right) return nullopt;
    return left + right;
}

optional<int64_t> multiplyCounts(int64_t left, int64_t right) {
    if (left == 0 || right == 0) return int64_t{0};
    const int64_t most = numeric_limits<int64_t>::max();
    const int64_t least = numeric_limits<int64_t>::min();
    bool overflows = false;
    if (left > 0) {
        overflows = right > 0 ? left > most / right : right < least / left;
    } else {
        overflows = right > 0 ? left < least / right : left < most / right;
    }
    if (overflows) return nullopt;
    return left * right;
}

optional<int64_t> unitLoadHundredthsOfAtom(const Atom& atom) {
    double total = 0.0;
    for (const Component& component : atom.components) {
        if (!isfinite(component.unitLoads) || component.unitLoads < 0.0) return nullopt;
        total += component.unitLoads;
    }
    return hundredthsOfUnitLoads(total);
}

optional<int64_t> unitLoadHundredthsOfAtoms(const M5LaneDateInput& input, const vector<size_t>& atoms) {
    int64_t total = 0;
    for (const size_t index : atoms) {
        if (index >= input.atoms.size()) return nullopt;
        const optional<int64_t> atomLoads = unitLoadHundredthsOfAtom(input.atoms[index]);
        const optional<int64_t> grown = atomLoads ? addCounts(total, *atomLoads) : nullopt;
        if (!grown) return nullopt;
        total = *grown;
    }
    return total;
}

string unitLoadsText(int64_t hundredths) {
    const string cents = to_string(hundredths % 100);
    return to_string(hundredths / 100) + "." + (cents.size() < 2 ? "0" : "") + cents;
}

} // namespace m5
} // namespace ob
