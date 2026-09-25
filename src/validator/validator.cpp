#include "validator.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace ob {

namespace {

void add(ValidationReport& rep,
         ValidationIssue::Severity sev,
         const std::string& rule,
         const std::string& message,
         const std::string& matnr,
         int index,
         int placeholder_index = -1) {
    ValidationIssue issue;
    issue.severity          = sev;
    issue.rule              = rule;
    issue.message           = message;
    issue.matnr             = matnr;
    issue.line_index        = index;
    issue.placeholder_index = placeholder_index;
    rep.issues.push_back(issue);

    if (sev == ValidationIssue::Severity::Error) ++rep.errors;
    else                                          ++rep.warnings;
}

bool isValidDimension(double inches) {
    return std::isfinite(inches) && inches >= 0.0;
}

bool is_blank(const std::string& s) {
    return s.find_first_not_of(" \t\r\n") == std::string::npos;
}

// The UoMs the Converter turns into pallets and weight. Any other code converts to 0.0
// pallets and is already reported as unknown_uom.
bool converted_to_weight(const std::string& uom) {
    return uom == "CS" || uom == "PAL" || uom == "DIS";
}

bool is_allowed(const std::string& value, const std::vector<std::string>& allowed) {
    return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
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
            || is_blank(s.datfr_ta) || is_blank(s.unitofmeas) || is_blank(s.ship_cond)) {
            ++rep.missing_fields;
            add(rep, ValidationIssue::Severity::Error, "missing_fields",
                "Demand line is missing a mandatory field", s.matnr, idx);
            continue;   // nothing further is meaningful for this line
        }

        // ── Quantity ────────────────────────────────────────────────────────
        if (!std::isfinite(s.trans) || s.trans <= 0.0) {
            ++rep.non_positive_qty;
            add(rep, ValidationIssue::Severity::Error, "non_positive_qty",
                "Demand quantity is not a finite, positive number", s.matnr, idx);
        } else if (s.trans > kMaxDemandQuantity) {
            ++rep.excessive_quantity;
            add(rep, ValidationIssue::Severity::Error, "excessive_quantity",
                "Demand quantity exceeds "
                    + std::to_string(static_cast<long long>(kMaxDemandQuantity)),
                s.matnr, idx);
        }

        // ── Unit of measure ─────────────────────────────────────────────────
        if (!is_allowed(s.unitofmeas, config.allowed_uom)) {
            ++rep.unknown_uom;
            add(rep, ValidationIssue::Severity::Warning, "unknown_uom",
                "Unit of measure '" + s.unitofmeas + "' is outside the expected set",
                s.matnr, idx);
        }

        // ── Ship condition ──────────────────────────────────────────────────
        if (!is_allowed(s.ship_cond, config.allowed_ship_cond)) {
            ++rep.unknown_ship_cond;
            add(rep, ValidationIssue::Severity::Warning, "unknown_ship_cond",
                "Ship condition '" + s.ship_cond + "' is outside the expected set",
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

        // Cases_Unit_Load is the divisor for CS lines and a factor in every unit-load
        // weight, so a 0 corrupts CS, PAL and DIS lines alike: CS cannot convert to
        // pallets, and PAL/DIS report a weight that omits the cargo entirely. The
        // Converter returns 0.0 rather than dividing by zero, so it has to be
        // surfaced here or the line silently reports as empty.
        if (converted_to_weight(s.unitofmeas) && p.cases_unit_load == 0) {
            ++rep.zero_unit_load;
            add(rep, ValidationIssue::Severity::Error, "zero_unit_load",
                "Product's Cases_Unit_Load is 0, cannot compute unit-load weight or convert cases to pallets",
                s.matnr, idx);
        }

        // A negative Cases_Unit_Load or a negative/non-finite Weight is bad
        // master data that corrupts every downstream figure without raising
        // anything on its own — a -10 divides trans into -10 pallets, and a
        // NaN weight propagates into a NaN total. Both need to be caught here
        // at the source rather than checking every place they get used.
        if (p.cases_unit_load < 0) {
            ++rep.negative_unit_load;
            add(rep, ValidationIssue::Severity::Error, "negative_unit_load",
                "Product's Cases_Unit_Load is negative", s.matnr, idx);
        }
        if (!std::isfinite(p.weight_lb) || p.weight_lb < 0.0 || p.weight_lb > kMaxCaseWeightLb) {
            ++rep.invalid_weight;
            add(rep, ValidationIssue::Severity::Error, "invalid_weight",
                "Product's Weight is not a finite number between 0 and "
                    + std::to_string(static_cast<long long>(kMaxCaseWeightLb)) + " lb",
                s.matnr, idx);
        }
        if (converted_to_weight(s.unitofmeas)
            && (p.cases_layer <= 0 || p.layers_unit_load <= 0)) {
            ++rep.invalid_layer_data;
            add(rep, ValidationIssue::Severity::Error, "invalid_layer_data",
                "Product's Cases_Layer or Layers_Unit_Load is not a positive integer", s.matnr, idx);
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
        if (!isValidDimension(p.length_in) || !isValidDimension(p.width_in)
            || !isValidDimension(p.height_in)) {
            ++rep.invalid_dimension;
            add(rep, ValidationIssue::Severity::Error, "invalid_dimension",
                "Product has a negative or non-finite dimension", s.matnr, idx);
        }
        if (p.strength < 0 || p.strength > 10) {
            ++rep.invalid_strength;
            add(rep, ValidationIssue::Severity::Warning, "invalid_strength",
                "Product's Strength is unreadable or outside 0..10", s.matnr, idx);
        }

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

        // Converter::pallet_has_wood only recognises the same set the Joiner
        // already treats as the known pallet types. Anything else (blank, a
        // typo, a new type not yet added here) silently reads as "not wood"
        // and understates weight by kWoodPalletWeightLb with nothing to flag
        // it — checked against Joiner's list rather than a second literal set
        // that could drift out of sync with it.
        const auto& known_pallet_types = Joiner::default_pallet_preference();
        if (std::find(known_pallet_types.begin(), known_pallet_types.end(), p.pallet_id)
                == known_pallet_types.end()) {
            ++rep.unrecognized_pallet_id;
            add(rep, ValidationIssue::Severity::Warning, "unrecognized_pallet_id",
                "Product's Pallet_ID '" + p.pallet_id + "' is not one of the known types",
                s.matnr, idx);
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

void Validator::validate_do_not_mix(const std::vector<DNMRecord>& pairs,
                                    const std::vector<STRRecord>& demandLines,
                                    ValidationReport& rep) {
    std::unordered_set<std::string> doNotMixSites;
    for (size_t i = 0; i < pairs.size(); ++i) {
        const DNMRecord& pair = pairs[i];
        if (is_blank(pair.planner_snp) || is_blank(pair.locfrno)) {
            ++rep.invalid_do_not_mix_pair;
            add(rep, ValidationIssue::Severity::Error, "invalid_do_not_mix_pair",
                "Do-not-mix entry " + std::to_string(i) + " is missing PLANNER_SNP or LOCFRNO",
                "", -1, -1);
        } else {
            doNotMixSites.insert(pair.locfrno);
        }
    }
    if (doNotMixSites.empty()) return;

    for (size_t i = 0; i < demandLines.size(); ++i) {
        const STRRecord& line = demandLines[i];
        if (is_blank(line.planner_snp) && doNotMixSites.count(line.locfrno) != 0) {
            ++rep.blankPlannerAtDoNotMixSite;
            add(rep, ValidationIssue::Severity::Warning, "blank_planner_at_do_not_mix_site",
                "Demand line has no PLANNER_SNP at do-not-mix site " + line.locfrno
                    + ", do-not-mix cannot be applied to it",
                line.matnr, static_cast<int>(i));
        }
    }
}

void Validator::validate_placeholders(const std::vector<PlaceholderRecord>& placeholders,
                                      ValidationReport& rep,
                                      const ValidationConfig& config) {
    for (size_t i = 0; i < placeholders.size(); ++i) {
        const PlaceholderRecord& p = placeholders[i];
        const int idx = static_cast<int>(i);

        // Blank LOCFRNO/LOCTONO would collapse into a bogus empty-string lane
        // key and silently merge with any other blank-keyed placeholder.
        if (is_blank(p.locfrno) || is_blank(p.loctono)) {
            ++rep.missing_lane_identifier;
            add(rep, ValidationIssue::Severity::Error, "missing_lane_identifier",
                "Placeholder is missing LOCFRNO or LOCTONO", "", -1, idx);
        }

        // SHIP_COND is part of the lane key, so a blank one would file the trucks under a
        // bogus lane that no demand line can match.
        if (is_blank(p.ship_cond)) {
            ++rep.missing_ship_cond;
            add(rep, ValidationIssue::Severity::Error, "missing_ship_cond",
                "Placeholder is missing SHIP_COND for lane " + p.locfrno + "->" + p.loctono,
                "", -1, idx);
        } else if (!is_allowed(p.ship_cond, config.allowed_ship_cond)) {
            ++rep.unknown_ship_cond;
            add(rep, ValidationIssue::Severity::Warning, "unknown_ship_cond",
                "Ship condition '" + p.ship_cond + "' is outside the expected set for lane "
                    + p.locfrno + "->" + p.loctono, "", -1, idx);
        }

        if (p.no_of_loads < 0) {
            ++rep.negative_load_count;
            add(rep, ValidationIssue::Severity::Error, "negative_load_count",
                "Placeholder NO_OF_LOADS is missing, malformed or negative for lane " + p.locfrno
                    + "->" + p.loctono, "", -1, idx);
        }

        if (p.no_of_loads > kMaxLoadsPerPlaceholder) {
            ++rep.excessive_load_count;
            add(rep, ValidationIssue::Severity::Error, "excessive_load_count",
                "Placeholder NO_OF_LOADS exceeds " + std::to_string(kMaxLoadsPerPlaceholder)
                    + " for lane " + p.locfrno + "->" + p.loctono, "", -1, idx);
        }
    }
}

std::vector<bool> Validator::excludedLineFlags(const ValidationReport& report, size_t lineCount) {
    std::vector<bool> excluded(lineCount, false);
    for (const auto& issue : report.issues) {
        if (issue.line_index < 0 || static_cast<size_t>(issue.line_index) >= lineCount) continue;
        if (issue.severity == ValidationIssue::Severity::Error || issue.rule == "zero_dimension") {
            excluded[static_cast<size_t>(issue.line_index)] = true;
        }
    }
    return excluded;
}

} // namespace ob
