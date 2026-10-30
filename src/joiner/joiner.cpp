#include "joiner.hpp"
#include "logger.hpp"
#include "tolerance.hpp"

#include <algorithm>
#include <cmath>

using namespace std;

namespace ob {

const vector<string>& Joiner::default_pallet_preference() {
    // Ordered by frequency in the product master. Replace with the client's fallback rule
    // once they confirm it (27 Oct).
    static const vector<string> pref = {"TLD", "PTL", "PGM", "GMA"};
    return pref;
}

ProductIndex Joiner::build_index(const vector<ProductRecord>& products) {
    ProductIndex index;
    index.reserve(products.size());
    for (const auto& p : products) {
        index[p.id].push_back(p);
    }
    return index;
}

namespace {

// The client's rule names GMA first among the types that divide a quantity evenly (27 Oct).
const char* const kPalletTypeWhenSeveralDivide = "GMA";

bool fillsWholeUnitLoads(double quantity, int casesUnitLoad) {
    if (casesUnitLoad <= 0) return false;
    const double unitLoads = quantity / static_cast<double>(casesUnitLoad);
    return abs(unitLoads - round(unitLoads)) <= kQuantityEpsilon;
}

// The first candidate in preference order; the first candidate when none is listed, so a
// product is never dropped.
const ProductRecord* firstByPreference(const vector<const ProductRecord*>& candidates,
                                       const vector<string>& preference) {
    for (const string& wanted : preference) {
        for (const ProductRecord* candidate : candidates) {
            if (candidate->pallet_id == wanted) return candidate;
        }
    }
    return candidates.front();
}

} // anonymous namespace

Joiner::PalletTypePick Joiner::choosePalletType(const vector<ProductRecord>& palletTypes,
                                                double quantity,
                                                const vector<string>& preference) {
    if (palletTypes.empty()) return {};
    if (palletTypes.size() == 1) return {&palletTypes.front(), PalletTypeChoice::OnlyType};

    vector<const ProductRecord*> dividing;
    if (isfinite(quantity) && quantity > 0.0) {
        for (const ProductRecord& type : palletTypes) {
            if (fillsWholeUnitLoads(quantity, type.cases_unit_load)) dividing.push_back(&type);
        }
    }
    if (dividing.size() == 1) return {dividing.front(), PalletTypeChoice::DividesEvenly};
    if (dividing.empty()) {
        vector<const ProductRecord*> everyType;
        everyType.reserve(palletTypes.size());
        for (const ProductRecord& type : palletTypes) everyType.push_back(&type);
        return {firstByPreference(everyType, preference), PalletTypeChoice::NoneDivides};
    }
    for (const ProductRecord* type : dividing) {
        if (type->pallet_id == kPalletTypeWhenSeveralDivide) {
            return {type, PalletTypeChoice::SeveralDivide};
        }
    }
    return {firstByPreference(dividing, preference), PalletTypeChoice::SeveralDivide};
}

JoinResult Joiner::join(const vector<STRRecord>& demand,
                        const ProductIndex& index,
                        const vector<string>& pallet_preference) {
    JoinResult result;
    result.lines.reserve(demand.size());

    for (const auto& line : demand) {
        JoinedLine jl;
        jl.str = &line;

        auto it = index.find(line.matnr);
        if (it == index.end() || it->second.empty()) {
            jl.matched = false;
            jl.product = nullptr;
            ++result.unmatched_lines;
            // Record each missing material number once.
            if (find(result.unmatched_matnrs.begin(),
                          result.unmatched_matnrs.end(),
                          line.matnr) == result.unmatched_matnrs.end()) {
                result.unmatched_matnrs.push_back(line.matnr);
            }
        } else {
            const auto& variants = it->second;
            const PalletTypePick pick = choosePalletType(variants, line.trans, pallet_preference);
            jl.matched          = true;
            jl.product          = pick.product;
            jl.palletTypes      = &variants;
            jl.palletTypeChoice = pick.choice;
            ++result.matched_lines;
            if (variants.size() > 1) ++result.multiPalletTypeLines;

            if (jl.palletTypeUnresolved()) {
                ++result.ambiguous_lines;
                if (find(result.ambiguous_matnrs.begin(),
                              result.ambiguous_matnrs.end(),
                              line.matnr) == result.ambiguous_matnrs.end()) {
                    result.ambiguous_matnrs.push_back(line.matnr);
                }
            }
        }

        result.lines.push_back(jl);
    }

    LOG_INFO("Joined demand to products: " + to_string(result.matched_lines)
             + " matched, " + to_string(result.unmatched_lines) + " unmatched, "
             + to_string(result.multiPalletTypeLines) + " on several pallet types, "
             + to_string(result.ambiguous_lines)
             + " of them not settled by the quantity");

    return result;
}

} // namespace ob
