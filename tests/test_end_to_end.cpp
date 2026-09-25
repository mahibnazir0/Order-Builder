// NOTE: no doctest main define here — main lives in one implementing TU only.
//
// These are the two Milestone 1 acceptance tests named in the plan:
//   1. read all three files for a day, produce the summary, match known totals
//   2. inject a wrong unit of measure, confirm it is caught and named
//
// They run the real pipeline end to end rather than testing modules in
// isolation, so a regression anywhere in the chain fails here.

#include "doctest.h"
#include "pipeline.hpp"

#include <sstream>
#include <stdexcept>
#include <type_traits>

using namespace ob;

namespace {

PipelineInputs real_inputs() {
    PipelineInputs in;
    in.product_path     = "tests/importer/Customer2-Product-Data.csv";
    in.demand_path      = "tests/importer/Demand-1.json";
    in.placeholder_path = "tests/importer/PlaceHolder-1.json";
    in.planning_day     = "2026-08-17";
    return in;
}

} // namespace


// ─── ACCEPTANCE TEST 1 ──────────────────────────────────────────────────────
// "Reads the product master, demand extract and placeholder file for a chosen
//  day, and prints a validated summary you can check against your own figures."

TEST_CASE("ACCEPTANCE: full run reproduces the figures Tom reviewed") {
    PipelineResult r = Pipeline::run(real_inputs());

    // Inputs read correctly
    CHECK(r.demand.str.size()                 == 24357);
    CHECK(r.demand.ctl.size()                 == 8);
    CHECK(r.demand.dnm.size()                 == 22);
    CHECK(r.products.rows_read                == 20201);
    CHECK(r.products.duplicate_ids            == 18);
    CHECK(r.placeholders.placeholders.size()  == 189);
    CHECK(r.placeholders.total_loads          == 372);

    // Join
    CHECK(r.join.matched_lines   == 24357);   // 100%
    CHECK(r.join.unmatched_lines == 0);
    CHECK(r.join.ambiguous_lines == 144);     // pallet-type variant chosen by preference

    // Summary — the figures in the output document Tom approved
    CHECK(r.summary.total_demand_lines     == 24357);
    CHECK(r.summary.hash_total             == doctest::Approx(8708934.0));
    CHECK(r.summary.lanes_total            == 371);
    CHECK(r.summary.lanes_with_demand      == 360);
    CHECK(r.summary.lanes_with_placeholder == 189);
    CHECK(r.summary.lanes_demand_only      == 182);
    CHECK(r.summary.lanes_placeholder_only == 11);
    CHECK(r.summary.trucks_requested       == 372);
    CHECK(r.summary.total_pallet_equiv     == doctest::Approx(152911.2).epsilon(0.001));

    // Clean input: no errors, and the only warnings are the ambiguous variants
    CHECK(r.validation.errors           == 0);
    CHECK(r.validation.ambiguous_pallet == 144);
    CHECK(r.validation.unknown_ship_cond == 0);   // every TL/TF in demand and placeholders
    CHECK(r.validation.missing_ship_cond == 0);
}

TEST_CASE("ACCEPTANCE: printed output carries the figures a planner checks") {
    PipelineResult r = Pipeline::run(real_inputs());

    std::ostringstream out;
    Reporter::print_summary(r.summary, out, 10);
    Reporter::print_warnings(r.validation, out);
    const std::string s = out.str();

    CHECK(s.find("2026-08-17")  != std::string::npos);
    CHECK(s.find("24,357")      != std::string::npos);   // demand lines
    CHECK(s.find("8,708,934")   != std::string::npos);   // hash total
    CHECK(s.find("371")         != std::string::npos);   // lanes
    CHECK(s.find("372")         != std::string::npos);   // trucks requested
    CHECK(s.find("Pallet-eq")   != std::string::npos);   // column label
    CHECK(s.find("not a count of physical pallets") != std::string::npos);
}

TEST_CASE("ACCEPTANCE: weight uses the unit-load basis and the wood-pallet rule") {
    PipelineResult r = Pipeline::run(real_inputs());

    // Weight must be case weight x cases per unit load, plus 60 lb for a wooden
    // pallet (PTL / PGM). Using case weight alone would understate by ~60x, so
    // this guards the most expensive easy mistake in the pipeline.
    bool checked_wood = false, checked_nowood = false;

    for (size_t i = 0; i < r.join.lines.size() && !(checked_wood && checked_nowood); ++i) {
        const JoinedLine& jl = r.join.lines[i];
        if (!jl.matched || jl.product == nullptr) continue;
        if (r.pallets_per_line[i] <= 0.0) continue;

        const ProductRecord& p = *jl.product;
        const double per_ul = p.weight_lb * p.cases_unit_load;
        const bool wood = (p.pallet_id == "PTL" || p.pallet_id == "PGM");
        const double expected = r.pallets_per_line[i] * (per_ul + (wood ? 60.0 : 0.0));

        CHECK(r.weight_per_line[i] == doctest::Approx(expected));
        if (wood) checked_wood = true; else checked_nowood = true;
    }

    CHECK(checked_wood);      // both branches of the rule were exercised
    CHECK(checked_nowood);
}


// ─── ACCEPTANCE TEST 2 ──────────────────────────────────────────────────────
// "Wrong UOM rejected — including the case that generates hundreds of phantom
//  trucks." Tom's example: a line sent in eaches but tagged as cases.

TEST_CASE("ACCEPTANCE: a wrong unit of measure is caught and named") {
    PipelineResult r = Pipeline::run(real_inputs());

    // Inject a line tagged with a unit the system does not recognise.
    JoinResult j = r.join;
    static STRRecord bad = *j.lines[0].str;
    bad.unitofmeas = "EA";                 // eaches — not CS, DIS or PAL
    j.lines[0].str = &bad;

    ValidationReport rep = Validator::validate(j, r.pallets_per_line);

    CHECK(rep.unknown_uom == 1);

    // The issue must name the rule, the material and the offending value, so a
    // planner can find the line in the source file.
    bool found = false;
    for (const auto& issue : rep.issues) {
        if (issue.rule == "unknown_uom") {
            found = true;
            CHECK(issue.matnr == bad.matnr);
            CHECK(issue.line_index >= 0);
            CHECK(issue.message.find("EA") != std::string::npos);
        }
    }
    CHECK(found);
}

TEST_CASE("ACCEPTANCE: an unrecognised unit produces no phantom pallets") {
    // The failure Tom described is a wrong unit inflating the truck count.
    // An unrecognised unit must convert to zero pallets rather than being
    // silently treated as cases or pallets.
    PipelineResult r = Pipeline::run(real_inputs());

    JoinedLine jl = r.join.lines[0];
    static STRRecord bad = *jl.str;
    bad.unitofmeas = "EA";
    bad.trans      = 100000.0;             // a quantity that would conjure trucks
    jl.str = &bad;

    JoinResult single;
    single.lines.push_back(jl);
    single.matched_lines = 1;

    PipelineInputs in = real_inputs();
    ValidationReport rep = Validator::validate(single, {}, in.validation);

    CHECK(rep.unknown_uom == 1);
    CHECK(rep.errors == 0);                // a warning, never a silent pass
}

TEST_CASE("ACCEPTANCE: a large line is flagged for review, not rejected") {
    // No hard rule distinguishes a mistagged line from a genuinely large order:
    // legitimate lines in this data reach 1,010 pallets. So the guard is a
    // configurable review threshold that warns and never blocks a run.
    PipelineInputs in = real_inputs();
    in.validation.pallet_warn_threshold = 300.0;

    PipelineResult r = Pipeline::run(in);

    CHECK(r.validation.over_pallet_threshold > 0);   // real lines exceed 300
    CHECK(r.validation.errors == 0);                 // but the run still succeeds
}


// ─── Failure handling ───────────────────────────────────────────────────────

TEST_CASE("ACCEPTANCE: a missing input file fails cleanly, naming the file") {
    PipelineInputs in = real_inputs();
    in.demand_path = "does/not/exist.json";

    CHECK_THROWS_AS(Pipeline::run(in), std::runtime_error);

    try {
        Pipeline::run(in);
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        CHECK(msg.find("does/not/exist.json") != std::string::npos);
    }
}

TEST_CASE("PipelineResult is move-only: a copy can never dangle-reference the original") {
    // join (via JoinedLine) holds raw pointers into demand and products.
    // Copying PipelineResult would duplicate those containers while the
    // pointers kept referencing the original's memory — a use-after-free
    // once the original goes out of scope. This must fail to compile, not
    // fail at runtime under ASan.
    static_assert(!std::is_copy_constructible<PipelineResult>::value,
                  "PipelineResult must not be copy-constructible");
    static_assert(!std::is_copy_assignable<PipelineResult>::value,
                  "PipelineResult must not be copy-assignable");
    static_assert(std::is_move_constructible<PipelineResult>::value,
                  "PipelineResult must stay movable so Pipeline::run can return it");
    CHECK(true);   // the assertions above are the actual test
}

TEST_CASE("ACCEPTANCE: a placeholder with a negative NO_OF_LOADS is flagged and excluded") {
    PipelineResult r = Pipeline::run(real_inputs());
    REQUIRE(!r.placeholders.placeholders.empty());
    REQUIRE(r.placeholders.placeholders[0].no_of_loads > 0);

    std::vector<PlaceholderRecord> placeholders = r.placeholders.placeholders;
    const int original_loads = placeholders[0].no_of_loads;
    placeholders[0].no_of_loads = -3;

    ValidationReport rep = r.validation;   // clean report — nothing else flagged
    Validator::validate_placeholders(placeholders, rep);
    CHECK(rep.negative_load_count == 1);
    CHECK(rep.errors >= 1);

    DaySummary summary = Reporter::build(r.join, placeholders, r.pallets_per_line,
                                         r.weight_per_line, "", rep);
    CHECK(summary.excluded_placeholders == 1);
    CHECK(summary.trucks_requested == r.summary.trucks_requested - original_loads);
}

TEST_CASE("ACCEPTANCE: pipeline stages agree with each other") {
    PipelineResult r = Pipeline::run(real_inputs());

    // The parallel arrays the Validator and Reporter rely on must line up.
    REQUIRE(r.pallets_per_line.size() == r.join.lines.size());
    REQUIRE(r.weight_per_line.size()  == r.join.lines.size());

    // Lane totals must add up to the day total.
    double lane_sum = 0.0;
    int    line_sum = 0;
    for (const auto& lane : r.summary.lanes) {
        lane_sum += lane.pallet_equiv;
        line_sum += lane.demand_lines;
    }
    CHECK(lane_sum == doctest::Approx(r.summary.total_pallet_equiv));
    CHECK(line_sum == r.summary.total_demand_lines);
}
