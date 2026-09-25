#pragma once
// ============================================================================
// validator.hpp — Checks joined demand for problems worth a human's attention.
//
// The Validator judges; the readers only load. Nothing here rejects a line
// silently: every problem becomes an entry in the report.
//
// ON THE PHANTOM-TRUCK GUARD
// Tom's example is a line sent in eaches but tagged as cases, which inflates
// the pallet count and conjures trucks that do not exist. We could not find a
// hard rule for it in the supplied data:
//   - TRANS exceeds AVAIL_QTY on 100% of lines, so availability is a forecast,
//     not a stock check.
//   - BSTRF (lot-size multiple) is zero on every line.
//   - TRANS is always a whole number, so eaches and cases look identical.
//   - Legitimate lines reach 1,010 pallets (~34 truckloads), so a fixed pallet
//     ceiling would reject real orders. 42 lines exceed 200 pallets.
// It is therefore implemented as a CONFIGURABLE WARNING, not an error, with a
// documented default. Tom's business rule should replace the threshold.
// ============================================================================

#include "joiner.hpp"
#include "placeholder_types.hpp"
#include <string>
#include <vector>

namespace ob {

// Upper bound for one placeholder's NO_OF_LOADS. The real file holds 1 to 13, so 1000 is far
// above any genuine request while keeping the truck totals well inside long long.
constexpr int kMaxLoadsPerPlaceholder = 1000;

// Upper bound for one demand line's TRANS. The real file peaks at 16,560; the bound keeps
// the derived pallets, weight and hash totals finite.
constexpr double kMaxDemandQuantity = 1'000'000.0;

// Upper bound for a product's per-case Weight. The real file peaks at 1,323 lb; the bound
// keeps weight_lb * Cases_Unit_Load * TRANS finite.
constexpr double kMaxCaseWeightLb = 10'000.0;

struct ValidationConfig {
    // Lines resolving to more than this many pallets are flagged for review.
    // Not an error: legitimate lines in the sample reach 1,010 pallets.
    double pallet_warn_threshold = 300.0;

    // Unit-of-measure codes seen in the demand file. Anything else is a warning.
    std::vector<std::string> allowed_uom = {"CS", "DIS", "PAL"};

    // Ship conditions seen in the demand and placeholder files. Anything else is a warning.
    std::vector<std::string> allowed_ship_cond = {"TL", "TF"};
};

struct ValidationIssue {
    enum class Severity { Warning, Error };

    Severity    severity = Severity::Warning;
    std::string rule;        // short rule name, for grouping in the report
    std::string message;     // human-readable detail
    std::string matnr;       // material number, where applicable
    int         line_index = -1;  // position in the demand file, for traceability

    // Position in placeholders[], for issues raised by validate_placeholders.
    // -1 for every demand-line issue above; kept as a separate index space
    // (rather than reusing line_index) so the two can never be confused when
    // the Reporter decides what to exclude from which total.
    int placeholder_index = -1;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;
    int errors   = 0;
    int warnings = 0;

    // Per-rule counts, so the summary can say "12 lines skipped: raw material"
    // without the caller walking the whole list.
    int missing_fields      = 0;
    int unmatched_product   = 0;
    int unknown_ship_cond   = 0;   // SHIP_COND outside allowed_ship_cond (demand or placeholder)
    int unknown_uom         = 0;
    int non_positive_qty    = 0;
    int excessive_quantity  = 0;   // TRANS above kMaxDemandQuantity
    int zero_unit_load      = 0;   // CS, PAL or DIS line whose product has Cases_Unit_Load == 0
    int negative_unit_load  = 0;   // product's Cases_Unit_Load is negative
    int invalid_weight      = 0;   // product's Weight is negative, non-finite or above kMaxCaseWeightLb
    int invalid_layer_data  = 0;   // product's Cases_Layer or Layers_Unit_Load is zero, negative or unreadable
    int invalid_dimension   = 0;   // product's Length/Width/Height is negative or non-finite
    int invalid_strength    = 0;   // product's Strength is unreadable or outside 0..10
    int zero_dimension      = 0;   // Tom's ruling: skip and warn
    int blank_uom_product   = 0;
    int unrecognized_pallet_id = 0; // not one of Joiner's known pallet types
    int ambiguous_pallet    = 0;   // variant chosen by preference, not by data
    int over_pallet_threshold = 0; // the phantom-truck warning

    // From validate_placeholders, not the demand join above.
    int missing_ship_cond       = 0; // placeholder has a blank SHIP_COND
    int negative_load_count     = 0; // placeholder's NO_OF_LOADS is missing, malformed or negative
    int excessive_load_count    = 0; // placeholder's NO_OF_LOADS exceeds kMaxLoadsPerPlaceholder
    int missing_lane_identifier = 0; // placeholder has a blank LOCFRNO or LOCTONO
    int invalid_do_not_mix_pair = 0; // DNM entry has a blank PLANNER_SNP or LOCFRNO
    int blankPlannerAtDoNotMixSite = 0; // demand line with a blank PLANNER_SNP at a DNM LOCFRNO
};

class Validator {
public:
    // Validate a completed join.
    //
    // pallets_per_line is optional and, when supplied, must be parallel to
    // join.lines. It is computed by the caller (via Converter) so the Validator
    // stays free of unit-conversion logic. Pass an empty vector to skip the
    // pallet-threshold check.
    static ValidationReport validate(const JoinResult& join,
                                     const std::vector<double>& pallets_per_line = {},
                                     const ValidationConfig& config = {});

    // Checks the placeholder file's own fields, which validate() above never
    // sees: a negative NO_OF_LOADS or a blank lane identifier would otherwise
    // reach the truck-count totals with nothing having looked at it. Appends
    // to `report` (usually the same report validate() produced) rather than
    // returning a separate one, so the CLI's single error/warning count and
    // print_warnings grouping cover placeholders too.
    static void validate_placeholders(const std::vector<PlaceholderRecord>& placeholders,
                                      ValidationReport& report,
                                      const ValidationConfig& config = {});

    // Checks the DNM block, which segregate() consumes without judging: an
    // entry with a blank PLANNER_SNP is silently dropped and one with a blank
    // LOCFRNO can never match, so that planner's demand would be mixed into
    // the normal lane group with nothing on record. Raised as an Error with
    // no line or placeholder index, so it is reported without excluding
    // anything from the totals. Appends to `report` like validate_placeholders.
    //
    // Also warns on each demand line with a blank PLANNER_SNP at a do-not-mix
    // LOCFRNO: a missing or wrong-type PLANNER_SNP loads as blank, which can
    // never match a pair, so the line would be mixed into the normal group
    // unnoticed. A warning, not an error, since blank planners are real data.
    // `demandLines` must be parallel to the joined lines so line_index lines up.
    static void validate_do_not_mix(const std::vector<DNMRecord>& pairs,
                                    const std::vector<STRRecord>& demandLines,
                                    ValidationReport& report);

    // Which demand lines must be kept out of derived totals: any line carrying an
    // Error, or Tom's "skip and warn" zero_dimension ruling. Parallel to the joined
    // lines; issues pointing outside [0, lineCount) are ignored.
    static std::vector<bool> excludedLineFlags(const ValidationReport& report, size_t lineCount);
};

} // namespace ob
