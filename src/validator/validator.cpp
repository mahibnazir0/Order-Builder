#include "validator.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cmath>

namespace ob {

namespace {

void add(ValidationReport& rep,
         ValidationIssue::Severity sev,
         const std::string& rule,
         const std::string& message,
         const std::string& matnr,
         int index) {
    ValidationIssue issue;
    issue.severity   = sev;
    issue.rule       = rule;
    issue.message    = message;
    issue.matnr      = matnr;
    issue.line_index = index;
    rep.issues.push_back(issue);

    if (sev == ValidationIssue::Severity::Error) ++rep.errors;
    else                                          ++rep.warnings;
}

bool is_blank(const std::string& s) {
    return s.find_first_not_of(" \t\r\n") == std::string::npos;
}

bool uom_allowed(const std::string& uom, const std::vector<std::string>& allowed) {
    return std::find(allowed.begin(), allowed.end(), uom) != allowed.end();
}

} // anonymous namespace

ValidationReport Validator::validate(const JoinResult& join,
                                     const std::vector<double>& pallets_per_line,
                                     const ValidationConfig& config) {
    ValidationReport rep;
    const bool have_pallets = (pallets_per_line.size() == join.lines.size());

    for (size_t i = 0; i < join.lines.size(); ++i) {
        const JoinedLine& jl = join.lines[i];
        const int idx = static_cast<int>(i);
        if (jl.str == nullptr) continue;          // defensive; should never happen
        const STRRecord& s = *jl.str;

        // ── Mandatory fields ────────────────────────────────────────────────
        // A line missing any of these cannot be planned at all.
        if (is_blank(s.matnr) || is_blank(s.locfrno) || is_blank(s.loctono)
            || is_blank(s.datfr_ta) || is_blank(s.unitofmeas)) {
            ++rep.missing_fields;
            add(rep, ValidationIssue::Severity::Error, "missing_fields",
                "Demand line is missing a mandatory field", s.matnr, idx);
            continue;   // nothing further is meaningful for this line
        }

        // ── Quantity ────────────────────────────────────────────────────────
        if (s.trans <= 0.0) {
            ++rep.non_positive_qty;
            add(rep, ValidationIssue::Severity::Error, "non_positive_qty",
                "Demand quantity is zero or negative", s.matnr, idx);
        }

        // ── Unit of measure ─────────────────────────────────────────────────
        if (!uom_allowed(s.unitofmeas, config.allowed_uom)) {
            ++rep.unknown_uom;
            add(rep, ValidationIssue::Severity::Warning, "unknown_uom",
                "Unit of measure '" + s.unitofmeas + "' is outside the expected set",
                s.matnr, idx);
        }

        // ── Join outcome ────────────────────────────────────────────────────
        if (!jl.matched || jl.product == nullptr) {
            ++rep.unmatched_product;
            add(rep, ValidationIssue::Severity::Error, "unmatched_product",
                "Material number is not in the product master", s.matnr, idx);
            continue;   // product-dependent checks below need a product
        }

        const ProductRecord& p = *jl.product;

        // A CS line with no Cases_Unit_Load has no way to convert to pallets:
        // the Converter returns 0.0 for it rather than dividing by zero, and
        // that has to be surfaced here or the line silently reports as empty.
        if (s.unitofmeas == "CS" && p.cases_unit_load == 0) {
            ++rep.zero_unit_load;
            add(rep, ValidationIssue::Severity::Error, "zero_unit_load",
                "Product's Cases_Unit_Load is 0, cannot convert cases to pallets",
                s.matnr, idx);
        }

        // Pallet-type variant was chosen by preference order, not by the data.
        if (jl.ambiguous) {
            ++rep.ambiguous_pallet;
            add(rep, ValidationIssue::Severity::Warning, "ambiguous_pallet_type",
                "Product has several pallet-type variants; '" + p.pallet_id
                    + "' was chosen by preference order",
                s.matnr, idx);
        }

        // ── Product data quality ────────────────────────────────────────────
        // Tom's ruling: zero-dimension rows are raw materials — skip and warn.
        if (p.length_in == 0.0 || p.width_in == 0.0 || p.height_in == 0.0) {
            ++rep.zero_dimension;
            add(rep, ValidationIssue::Severity::Warning, "zero_dimension",
                "Product has a zero dimension (raw material), line skipped",
                s.matnr, idx);
        }

        if (is_blank(p.uom)) {
            ++rep.blank_uom_product;
            add(rep, ValidationIssue::Severity::Warning, "blank_uom_product",
                "Product master row has no unit of measure", s.matnr, idx);
        }

        // ── Phantom-truck warning ───────────────────────────────────────────
        // See the header: no hard rule exists in the supplied data, so this is
        // a configurable threshold for human review, never an automatic reject.
        if (have_pallets && pallets_per_line[i] > config.pallet_warn_threshold) {
            ++rep.over_pallet_threshold;
            add(rep, ValidationIssue::Severity::Warning, "large_line",
                "Line resolves to " + std::to_string(std::llround(pallets_per_line[i]))
                    + " pallets, above the review threshold, check the unit of measure",
                s.matnr, idx);
        }
    }

    LOG_INFO("Validation: " + std::to_string(rep.errors) + " errors, "
             + std::to_string(rep.warnings) + " warnings across "
             + std::to_string(join.lines.size()) + " demand lines");

    return rep;
}

} // namespace ob
