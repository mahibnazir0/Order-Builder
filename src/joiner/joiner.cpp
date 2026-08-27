#include "joiner.hpp"
#include "logger.hpp"

#include <algorithm>

namespace ob {

const std::vector<std::string>& Joiner::default_pallet_preference() {
    // Ordered by frequency in the product master. Replace with Tom's rule
    // once he supplies the four configuration names and how demand selects one.
    static const std::vector<std::string> pref = {"TLD", "PTL", "PGM", "GMA"};
    return pref;
}

ProductIndex Joiner::build_index(const std::vector<ProductRecord>& products) {
    ProductIndex index;
    index.reserve(products.size());
    for (const auto& p : products) {
        index[p.id].push_back(p);
    }
    return index;
}

namespace {

// Choose one variant when a product ID has several pallet types.
// Walk the preference order and take the first match; if none of the preferred
// types are present, fall back to the first row so a product is never dropped.
const ProductRecord* choose_variant(const std::vector<ProductRecord>& variants,
                                    const std::vector<std::string>& preference) {
    if (variants.empty()) return nullptr;
    if (variants.size() == 1) return &variants[0];

    for (const auto& want : preference) {
        for (const auto& v : variants) {
            if (v.pallet_id == want) return &v;
        }
    }
    return &variants[0];   // no preferred type present — keep the line
}

} // anonymous namespace

JoinResult Joiner::join(const std::vector<STRRecord>& demand,
                        const ProductIndex& index,
                        const std::vector<std::string>& pallet_preference) {
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
            if (std::find(result.unmatched_matnrs.begin(),
                          result.unmatched_matnrs.end(),
                          line.matnr) == result.unmatched_matnrs.end()) {
                result.unmatched_matnrs.push_back(line.matnr);
            }
        } else {
            const auto& variants = it->second;
            jl.matched   = true;
            jl.ambiguous = (variants.size() > 1);
            jl.product   = choose_variant(variants, pallet_preference);
            ++result.matched_lines;

            if (jl.ambiguous) {
                ++result.ambiguous_lines;
                if (std::find(result.ambiguous_matnrs.begin(),
                              result.ambiguous_matnrs.end(),
                              line.matnr) == result.ambiguous_matnrs.end()) {
                    result.ambiguous_matnrs.push_back(line.matnr);
                }
            }
        }

        result.lines.push_back(jl);
    }

    LOG_INFO("Joined demand to products: " + std::to_string(result.matched_lines)
             + " matched, " + std::to_string(result.unmatched_lines) + " unmatched, "
             + std::to_string(result.ambiguous_lines) + " ambiguous (pallet-type variant chosen by preference)");

    return result;
}

} // namespace ob
