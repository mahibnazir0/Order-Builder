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
#include <string>
#include <vector>

namespace ob {

struct ValidationConfig {
    // Lines resolving to more than this many pallets are flagged for review.
    // Not an error: legitimate lines in the sample reach 1,010 pallets.
    double pallet_warn_threshold = 300.0;

    // Unit-of-measure codes seen in the demand file. Anything else is a warning.
    std::vector<std::string> allowed_uom = {"CS", "DIS", "PAL"};
};

struct ValidationIssue {
    enum class Severity { Warning, Error };

    Severity    severity = Severity::Warning;
    std::string rule;        // short rule name, for grouping in the report
    std::string message;     // human-readable detail
    std::string matnr;       // material number, where applicable
    int         line_index = -1;  // position in the demand file, for traceability
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;
    int errors   = 0;
    int warnings = 0;

    // Per-rule counts, so the summary can say "12 lines skipped: raw material"
    // without the caller walking the whole list.
    int missing_fields      = 0;
    int unmatched_product   = 0;
    int unknown_uom         = 0;
    int non_positive_qty    = 0;
    int zero_dimension      = 0;   // Tom's ruling: skip and warn
    int blank_uom_product   = 0;
    int ambiguous_pallet    = 0;   // variant chosen by preference, not by data
    int over_pallet_threshold = 0; // the phantom-truck warning
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
};

} // namespace ob
