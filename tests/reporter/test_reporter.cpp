// NOTE: no doctest main define here — main lives in one implementing TU only.
#include "doctest.h"
#include "reporter.hpp"
#include "importer.hpp"
#include "product_importer.hpp"
#include "placeholder_importer.hpp"

#include <sstream>
#include <algorithm>

using namespace ob;

static const char* DEMAND_PATH  = "tests/importer/Demand-1.json";
static const char* PRODUCT_PATH = "tests/importer/Customer2-Product-Data.csv";
static const char* PH_PATH      = "tests/importer/PlaceHolder-1.json";

namespace {

// Stand-in for the Converter so the Reporter can be tested independently of it.
// CS lines divide by cases-per-unit-load; PAL and DIS are already unit loads.
double pallets_for(const JoinedLine& jl) {
    if (!jl.matched || jl.product == nullptr || jl.str == nullptr) return 0.0;
    const std::string& uom = jl.str->unitofmeas;
    if (uom == "CS") {
        const int cul = jl.product->cases_unit_load;
        return (cul > 0) ? jl.str->trans / cul : 0.0;
    }
    return jl.str->trans;   // PAL and DIS
}

struct Fixture {
    DemandFile             demand;
    ProductLoadResult      products;
    PlaceholderLoadResult  placeholders;
    ProductIndex           index;
    JoinResult             join;
    std::vector<double>    pallets;
    std::vector<double>    weights;

    Fixture() {
        demand       = Importer::load_demand(DEMAND_PATH);
        products     = ProductImporter::load(PRODUCT_PATH);
        placeholders = PlaceholderImporter::load(PH_PATH);
        index        = Joiner::build_index(products.products);
        join         = Joiner::join(demand.str, index);

        pallets.reserve(join.lines.size());
        weights.reserve(join.lines.size());
        for (const auto& jl : join.lines) {
            const double p = pallets_for(jl);
            pallets.push_back(p);
            weights.push_back(jl.product ? p * jl.product->weight_lb : 0.0);
        }
    }
};

} // namespace

TEST_CASE("day summary reproduces the figures Tom reviewed") {
    Fixture f;
    DaySummary d = Reporter::build(f.join, f.placeholders.placeholders,
                                   f.pallets, f.weights, "2026-08-17");

    CHECK(d.total_demand_lines == 24357);
    CHECK(d.matched_lines      == 24357);
    CHECK(d.unmatched_lines    == 0);

    // The hash total Tom asked for on the top-line summary.
    CHECK(d.hash_total == doctest::Approx(8708934.0));

    // Lane split — Tom's ruling was to report both sides plainly.
    CHECK(d.lanes_total            == 371);
    CHECK(d.lanes_with_demand      == 360);
    CHECK(d.lanes_with_placeholder == 189);
    CHECK(d.lanes_demand_only      == 182);
    CHECK(d.lanes_placeholder_only == 11);
    CHECK(d.trucks_requested       == 372);
}

TEST_CASE("pallet-equivalent total matches the independent calculation") {
    Fixture f;
    DaySummary d = Reporter::build(f.join, f.placeholders.placeholders,
                                   f.pallets, f.weights);
    CHECK(d.total_pallet_equiv == doctest::Approx(152911.2).epsilon(0.001));
}

TEST_CASE("lanes are sorted with the largest first") {
    Fixture f;
    DaySummary d = Reporter::build(f.join, f.placeholders.placeholders,
                                   f.pallets, f.weights);
    REQUIRE(d.lanes.size() > 1);
    for (size_t i = 1; i < d.lanes.size(); ++i) {
        REQUIRE(d.lanes[i - 1].pallet_equiv >= d.lanes[i].pallet_equiv);
    }
    // Largest lane in the sample.
    CHECK(d.lanes[0].locfrno   == "2028");
    CHECK(d.lanes[0].loctono   == "2508");
    CHECK(d.lanes[0].ship_cond == "TF");
    CHECK(d.lanes[0].demand_lines == 781);
}

TEST_CASE("lanes present in only one input still appear, with zeroes") {
    Fixture f;
    DaySummary d = Reporter::build(f.join, f.placeholders.placeholders,
                                   f.pallets, f.weights);

    // Placeholder-only lanes: trucks requested but no demand lines.
    const int ph_only = static_cast<int>(std::count_if(
        d.lanes.begin(), d.lanes.end(), [](const LaneSummary& l) {
            return l.has_placeholder && !l.has_demand;
        }));
    CHECK(ph_only == 11);

    for (const auto& l : d.lanes) {
        if (l.has_placeholder && !l.has_demand) {
            REQUIRE(l.demand_lines == 0);
            REQUIRE(l.pallet_equiv == doctest::Approx(0.0));
            REQUIRE(l.trucks_requested > 0);
        }
        if (l.has_demand && !l.has_placeholder) {
            REQUIRE(l.trucks_requested == 0);
            REQUIRE(l.demand_lines > 0);
        }
    }
}

TEST_CASE("hash total counts every line, matched or not") {
    Fixture f;
    JoinResult j = f.join;
    j.lines[0].matched = false;
    j.lines[0].product = nullptr;

    DaySummary d = Reporter::build(j, f.placeholders.placeholders);
    // Unmatched lines still contribute to the integrity check.
    CHECK(d.hash_total == doctest::Approx(8708934.0));
    CHECK(d.unmatched_lines == 1);
}

TEST_CASE("pallet and weight columns are zero when no figures are supplied") {
    Fixture f;
    DaySummary d = Reporter::build(f.join, f.placeholders.placeholders);
    CHECK(d.total_pallet_equiv == doctest::Approx(0.0));
    CHECK(d.total_weight_lb    == doctest::Approx(0.0));
    // Everything else still works.
    CHECK(d.total_demand_lines == 24357);
    CHECK(d.lanes_total        == 371);
}

TEST_CASE("a line the Validator flags as an error is excluded from pallet and weight totals") {
    Fixture f;
    REQUIRE(f.pallets[0] > 0.0);   // the excluded line must actually contribute something

    ValidationReport rep;
    ValidationIssue issue;
    issue.severity   = ValidationIssue::Severity::Error;
    issue.rule       = "non_positive_qty";
    issue.message    = "test";
    issue.line_index = 0;
    rep.issues.push_back(issue);
    ++rep.errors;

    DaySummary with_error = Reporter::build(f.join, f.placeholders.placeholders,
                                            f.pallets, f.weights, "", rep);
    DaySummary without_error = Reporter::build(f.join, f.placeholders.placeholders,
                                               f.pallets, f.weights);

    CHECK(with_error.total_pallet_equiv
          == doctest::Approx(without_error.total_pallet_equiv - f.pallets[0]));
    CHECK(with_error.total_weight_lb
          == doctest::Approx(without_error.total_weight_lb - f.weights[0]));

    // total_demand_lines and hash_total are integrity checks against the
    // source file, not derived figures — the errored line still counts there.
    CHECK(with_error.total_demand_lines == without_error.total_demand_lines);
    CHECK(with_error.hash_total         == doctest::Approx(without_error.hash_total));
}

TEST_CASE("a zero_dimension warning also excludes its line, per Tom's skip-and-warn ruling") {
    Fixture f;
    REQUIRE(f.pallets[0] > 0.0);

    ValidationReport rep;
    ValidationIssue issue;
    issue.severity   = ValidationIssue::Severity::Warning;
    issue.rule       = "zero_dimension";
    issue.message    = "test";
    issue.line_index = 0;
    rep.issues.push_back(issue);
    ++rep.warnings;

    DaySummary with_flag = Reporter::build(f.join, f.placeholders.placeholders,
                                           f.pallets, f.weights, "", rep);
    DaySummary without_flag = Reporter::build(f.join, f.placeholders.placeholders,
                                              f.pallets, f.weights);

    CHECK(with_flag.total_pallet_equiv
          == doctest::Approx(without_flag.total_pallet_equiv - f.pallets[0]));
}

TEST_CASE("an ordinary warning does not exclude its line from the totals") {
    Fixture f;
    ValidationReport rep;
    ValidationIssue issue;
    issue.severity   = ValidationIssue::Severity::Warning;
    issue.rule       = "ambiguous_pallet_type";
    issue.message    = "test";
    issue.line_index = 0;
    rep.issues.push_back(issue);
    ++rep.warnings;

    DaySummary with_flag = Reporter::build(f.join, f.placeholders.placeholders,
                                           f.pallets, f.weights, "", rep);
    DaySummary without_flag = Reporter::build(f.join, f.placeholders.placeholders,
                                              f.pallets, f.weights);

    CHECK(with_flag.total_pallet_equiv == doctest::Approx(without_flag.total_pallet_equiv));
}

TEST_CASE("printed summary contains the key figures and the pallet-eq note") {
    Fixture f;
    DaySummary d = Reporter::build(f.join, f.placeholders.placeholders,
                                   f.pallets, f.weights, "2026-08-17");
    std::ostringstream out;
    Reporter::print_summary(d, out, 8);
    const std::string s = out.str();

    CHECK(s.find("2026-08-17")     != std::string::npos);
    CHECK(s.find("24,357")         != std::string::npos);  // demand lines
    CHECK(s.find("8,708,934")      != std::string::npos);  // hash total
    CHECK(s.find("Pallet-eq")      != std::string::npos);  // column label
    CHECK(s.find("not a count of physical pallets") != std::string::npos);
    CHECK(s.find("2028")           != std::string::npos);  // top lane
}

TEST_CASE("printed summary honours the lane limit") {
    Fixture f;
    DaySummary d = Reporter::build(f.join, f.placeholders.placeholders,
                                   f.pallets, f.weights);
    std::ostringstream limited, full;
    Reporter::print_summary(d, limited, 5);
    Reporter::print_summary(d, full, 0);

    CHECK(limited.str().size() < full.str().size());
    CHECK(limited.str().find("top 5 of 371") != std::string::npos);
}

TEST_CASE("warning section groups by rule and caps examples") {
    ValidationReport rep;
    for (int i = 0; i < 12; ++i) {
        ValidationIssue issue;
        issue.severity   = ValidationIssue::Severity::Warning;
        issue.rule       = "ambiguous_pallet_type";
        issue.message    = "variant chosen by preference";
        issue.matnr      = "105553001";
        issue.line_index = i;
        rep.issues.push_back(issue);
        ++rep.warnings;
    }

    std::ostringstream out;
    Reporter::print_warnings(rep, out, 3);
    const std::string s = out.str();

    CHECK(s.find("ambiguous_pallet_type") != std::string::npos);
    CHECK(s.find("(12)")                  != std::string::npos);  // group count
    CHECK(s.find("and 9 more")            != std::string::npos);  // 12 - 3 shown
}

TEST_CASE("clean validation prints a clear all-good message") {
    ValidationReport rep;   // no issues
    std::ostringstream out;
    Reporter::print_warnings(rep, out);
    CHECK(out.str().find("Nothing flagged") != std::string::npos);
}
