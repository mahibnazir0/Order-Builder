#include "bindingConstraint.hpp"

#include <cmath>
#include <stdexcept>

using namespace std;

namespace ob {
namespace {

bool isNonNegativeFinite(double value) noexcept { return isfinite(value) && value >= 0.0; }

void requireUsableInputs(const vector<double>& palletsPerLine,
                         const vector<double>& weightPerLine, const TrailerSpec& trailer) {
    if (!isfinite(trailer.weightLimitLb) || trailer.weightLimitLb <= 0.0) {
        throw invalid_argument("bindingConstraint: trailer weightLimitLb must be positive");
    }
    if (trailer.stackPositions <= 0) {
        throw invalid_argument("bindingConstraint: trailer stackPositions must be positive");
    }
    if (palletsPerLine.size() != weightPerLine.size()) {
        throw invalid_argument("bindingConstraint: pallet and weight vectors differ in size");
    }
}

} // namespace

BindingResult assessBinding(const SegregationResult& segregation,
                            const vector<double>& palletsPerLine,
                            const vector<double>& weightPerLine,
                            const TrailerSpec& trailer) {
    requireUsableInputs(palletsPerLine, weightPerLine, trailer);

    BindingResult result;
    result.groups.reserve(segregation.groups.size());
    for (const auto& group : segregation.groups) {
        GroupBinding binding;
        for (const size_t lineIndex : group.lineIndices) {
            if (lineIndex >= palletsPerLine.size()) {
                throw invalid_argument("bindingConstraint: group line index out of range");
            }
            if (!isNonNegativeFinite(palletsPerLine[lineIndex])
                || !isNonNegativeFinite(weightPerLine[lineIndex])) {
                ++result.excludedInvalidLines;
                continue;
            }
            binding.totalPallets += palletsPerLine[lineIndex];
            binding.totalWeightLb += weightPerLine[lineIndex];
        }
        binding.trucksIfWeight = binding.totalWeightLb / trailer.weightLimitLb;
        binding.trucksIfCube = binding.totalPallets / trailer.stackPositions;
        // A tie is cube-bound: floor space is the default limit, weight must clearly exceed it.
        binding.binding = binding.trucksIfWeight > binding.trucksIfCube
            ? BindingConstraint::Weight : BindingConstraint::Cube;
        binding.binding == BindingConstraint::Weight ? ++result.weightBoundGroups
                                                     : ++result.cubeBoundGroups;
        result.groups.push_back(binding);
    }
    return result;
}

} // namespace ob
