#pragma once
// ============================================================================
// joiner.hpp — Matches demand lines to their product master record.
//
// Join key is MATNR (string) -> ProductRecord.id (string).
// Measured on the real files: 24,357 of 24,357 lines match (100%).
//
// THE PALLET-TYPE PROBLEM
// 18 product IDs appear more than once in the master — once per pallet type
// (TLD / PTL / PGM / GMA). Same product, same dimensions, same weight, but a
// DIFFERENT Cases_Unit_Load. Example: ID 105553001 is 84 cases per unit load
// as GMA and 168 as TLD. Picking the wrong variant doubles or halves the
// pallet count for that line.
//
// The demand file carries no pallet-type field, so the correct variant cannot
// be derived from the input. Until Tom supplies the rule, the joiner applies a
// configurable preference order and FLAGS every ambiguous match so the count
// is never hidden. 144 of 24,357 lines (0.59%) are affected in the sample.
// ============================================================================

#include "demand_types.hpp"
#include "product_types.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace ob {

// All master rows sharing one ID. Usually one row; 18 IDs have two.
using ProductIndex = std::unordered_map<std::string, std::vector<ProductRecord>>;

struct JoinedLine {
    const STRRecord*     str     = nullptr;  // always set
    const ProductRecord* product = nullptr;  // null when unmatched
    bool matched   = false;
    bool ambiguous = false;   // several pallet-type variants existed
};

struct JoinResult {
    std::vector<JoinedLine>  lines;
    int matched_lines   = 0;
    int unmatched_lines = 0;
    int ambiguous_lines = 0;   // resolved by preference order, not by the data
    std::vector<std::string> unmatched_matnrs;
    std::vector<std::string> ambiguous_matnrs;
};

class Joiner {
public:
    // Preference order when a product has several pallet-type variants.
    // TLD first because it is the most common in the master (9,130 rows).
    // This is a documented placeholder for Tom's rule, not a business decision.
    static const std::vector<std::string>& default_pallet_preference();

    // Group master rows by ID. Rows sharing an ID are kept together rather
    // than de-duplicated, so the join can see that a choice is being made.
    static ProductIndex build_index(const std::vector<ProductRecord>& products);

    // Match every demand line. Unmatched lines are kept in the result with
    // product == nullptr; they are never silently dropped.
    static JoinResult join(const std::vector<STRRecord>& demand,
                           const ProductIndex& index,
                           const std::vector<std::string>& pallet_preference
                               = default_pallet_preference());
};

} // namespace ob
