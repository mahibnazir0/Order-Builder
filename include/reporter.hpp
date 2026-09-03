#pragma once
// ============================================================================
// reporter.hpp — Builds and prints the Milestone 1 summary.
//
// Presentation only: no business logic, no file access, no unit conversion.
// Everything it needs is passed in, so it can be tested without a filesystem.
//
// Output has three parts, in the order Tom reviewed and approved:
//   1. Top-line summary   — a health check on the day, including the hash total
//   2. Per-lane table     — one row per LOCFRNO / LOCTONO / SHIP_COND
//   3. Warnings           — anything skipped or flagged, so nothing is silent
//
// TERMINOLOGY
// The pallet figure is a summed PALLET-EQUIVALENT, not a physical pallet count.
// Each demand line converts to a pallet fraction and those fractions are added
// across the lane, so a total like 6,708.9 is a running sum — not one pallet
// split into tenths. Tom queried this; the column is labelled accordingly.
//
// LANES PRESENT IN ONLY ONE INPUT
// Tom's ruling was "just build the trucks" — no separate reconciliation
// section. Lanes appearing in only the demand or only the placeholder file are
// reported in the same table with zeroes in the missing columns.
// ============================================================================

#include "joiner.hpp"
#include "placeholder_types.hpp"
#include "validator.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace ob {

// One row of the per-lane table.
struct LaneSummary {
    std::string locfrno;
    std::string loctono;
    std::string ship_cond;

    int    demand_lines     = 0;
    int    matched_lines    = 0;
    double pallet_equiv     = 0.0;   // summed pallet fractions — see header
    double weight_lb        = 0.0;
    int    trucks_requested = 0;     // from the placeholder file

    bool has_demand      = false;
    bool has_placeholder = false;
};

// The whole day, ready to print.
struct DaySummary {
    std::string planning_day;

    int total_demand_lines = 0;
    int matched_lines      = 0;
    int unmatched_lines    = 0;

    double hash_total   = 0.0;   // sum of every TRANS — Tom's integrity check
    double total_pallet_equiv = 0.0;
    double total_weight_lb    = 0.0;

    int lanes_total          = 0;   // union of both inputs
    int lanes_with_demand    = 0;
    int lanes_with_placeholder = 0;
    int lanes_demand_only    = 0;
    int lanes_placeholder_only = 0;
    int trucks_requested     = 0;

    std::vector<LaneSummary> lanes;   // sorted by pallet_equiv, largest first
};

class Reporter {
public:
    // Aggregate a completed join plus the placeholder file into a day summary.
    //
    // pallets_per_line and weight_per_line are optional and, when supplied,
    // must be parallel to join.lines. They come from the Converter via the
    // caller, so the Reporter holds no conversion logic of its own. Pass empty
    // vectors and the pallet and weight columns report zero.
    //
    // validation is optional. When supplied, a line that carries a
    // Severity::Error issue — or the "zero_dimension" raw-material warning,
    // per Tom's "skip and warn" ruling — is left out of the pallet and weight
    // totals: the Validator caught it, so it must not silently corrupt the
    // summary. The line still counts toward total_demand_lines and hash_total,
    // which are integrity checks against the source file, not derived figures.
    static DaySummary build(const JoinResult& join,
                            const std::vector<PlaceholderRecord>& placeholders,
                            const std::vector<double>& pallets_per_line = {},
                            const std::vector<double>& weight_per_line  = {},
                            const std::string& planning_day = "",
                            const ValidationReport& validation = {});

    // Print the top-line summary and the per-lane table.
    // max_lanes limits how many rows are printed; 0 means all of them.
    static void print_summary(const DaySummary& summary,
                              std::ostream& out,
                              int max_lanes = 0);

    // Print the warning and error section, grouped by rule.
    static void print_warnings(const ValidationReport& report,
                               std::ostream& out,
                               int max_examples_per_rule = 5);
};

} // namespace ob
