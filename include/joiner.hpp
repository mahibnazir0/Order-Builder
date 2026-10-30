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
// The demand file carries no pallet-type field. The client's rule (27 Oct):
// companies ship in full unit loads, so take the type whose Cases_Unit_Load
// divides the quantity evenly, GMA when GMA does and TLD when only TLD does.
// Where several types divide evenly (every TLD quantity of 105553001 is also
// a GMA one) GMA is taken as that rule says; where none does, the preference
// order below decides. The client has still to confirm both cases, so every
// such line is FLAGGED: 36 of 24,357 lines on 17 August.
// ============================================================================

#include "demand_types.hpp"
#include "product_types.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace ob {

// All master rows sharing one ID. Usually one row; 18 IDs have two.
using ProductIndex = std::unordered_map<std::string, std::vector<ProductRecord>>;

// How a matched line's pallet type was chosen. The last two are the cases the client has
// still to confirm (27 Oct), so the Validator warns on them.
enum class PalletTypeChoice {
    OnlyType,          // the master lists the product on one pallet type
    DividesEvenly,     // exactly one type's Cases_Unit_Load divides the quantity evenly
    SeveralDivide,     // more than one does: GMA if it is one of them, else the preference order
    NoneDivides,       // none does, or the quantity is not a positive number: the preference order
};

struct JoinedLine {
    const STRRecord*     str     = nullptr;  // always set
    const ProductRecord* product = nullptr;  // null when unmatched
    // Every row the master lists for the product, the chosen one included; null when
    // unmatched. Points into the same ProductIndex entry as product.
    const std::vector<ProductRecord>* palletTypes = nullptr;
    bool matched   = false;
    PalletTypeChoice palletTypeChoice = PalletTypeChoice::OnlyType;

    bool palletTypeUnresolved() const {
        return palletTypeChoice == PalletTypeChoice::SeveralDivide
            || palletTypeChoice == PalletTypeChoice::NoneDivides;
    }
};

struct JoinResult {
    std::vector<JoinedLine>  lines;
    int matched_lines   = 0;
    int unmatched_lines = 0;
    // Lines on a product with several pallet types, and those the quantity did not settle
    // (several types or none divide it evenly).
    int multiPalletTypeLines = 0;
    int ambiguous_lines = 0;
    std::vector<std::string> unmatched_matnrs;
    std::vector<std::string> ambiguous_matnrs;
};

class Joiner {
public:
    // Which pallet type decides when the quantity divides evenly by none of a product's types,
    // or by several of which none is GMA. TLD first because it is the most common in the
    // master (9,130 rows). A documented placeholder until the client confirms the fallback.
    static const std::vector<std::string>& default_pallet_preference();

    // Group master rows by ID. Rows sharing an ID are kept together rather
    // than de-duplicated, so the join can see that a choice is being made.
    static ProductIndex build_index(const std::vector<ProductRecord>& products);

    // Match every demand line. Unmatched lines are kept in the result with
    // product == nullptr; they are never silently dropped. A product with several
    // pallet types is matched by its line's quantity (choosePalletType).
    static JoinResult join(const std::vector<STRRecord>& demand,
                           const ProductIndex& index,
                           const std::vector<std::string>& pallet_preference
                               = default_pallet_preference());

    struct PalletTypePick {
        const ProductRecord* product = nullptr;
        PalletTypeChoice choice = PalletTypeChoice::OnlyType;
    };

    // The pallet type a quantity ships on, from a product's master rows: the one whose
    // Cases_Unit_Load divides it evenly; GMA when several do and GMA is one; otherwise the
    // first type in preference order among the candidates, falling back to the first row.
    // Returns a null product only for an empty list.
    static PalletTypePick choosePalletType(const std::vector<ProductRecord>& palletTypes,
                                           double quantity,
                                           const std::vector<std::string>& preference
                                               = default_pallet_preference());
};

} // namespace ob
