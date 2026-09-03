#include "reporter.hpp"
#include "logger.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <ostream>
#include <sstream>

namespace ob {

namespace {

// Lanes are keyed by the triplet the planner thinks in: origin, destination,
// ship condition. std::map keeps a stable order before we sort for display.
using LaneKey = std::tuple<std::string, std::string, std::string>;

std::string fixed(double v, int places) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(places) << v;
    return os.str();
}

// Thousands separators, done manually so no locale needs installing.
std::string grouped(double v, int places = 0) {
    std::string s = fixed(v, places);
    std::string intpart = s;
    std::string frac;
    const auto dot = s.find('.');
    if (dot != std::string::npos) {
        intpart = s.substr(0, dot);
        frac    = s.substr(dot);
    }
    bool neg = !intpart.empty() && intpart[0] == '-';
    if (neg) intpart.erase(0, 1);

    std::string out;
    int count = 0;
    for (auto it = intpart.rbegin(); it != intpart.rend(); ++it) {
        if (count && count % 3 == 0) out.push_back(',');
        out.push_back(*it);
        ++count;
    }
    std::reverse(out.begin(), out.end());
    if (neg) out.insert(out.begin(), '-');
    return out + frac;
}

void pad_left(std::ostream& out, const std::string& s, size_t width) {
    if (s.size() < width) out << std::string(width - s.size(), ' ');
    out << s;
}
void pad_right(std::ostream& out, const std::string& s, size_t width) {
    out << s;
    if (s.size() < width) out << std::string(width - s.size(), ' ');
}

} // anonymous namespace

DaySummary Reporter::build(const JoinResult& join,
                           const std::vector<PlaceholderRecord>& placeholders,
                           const std::vector<double>& pallets_per_line,
                           const std::vector<double>& weight_per_line,
                           const std::string& planning_day,
                           const ValidationReport& validation) {
    DaySummary day;
    day.planning_day = planning_day;

    const bool have_pallets = (pallets_per_line.size() == join.lines.size());
    const bool have_weight  = (weight_per_line.size()  == join.lines.size());

    // A line the Validator rejected (Severity::Error) or ruled a raw material
    // to skip ("zero_dimension") must not add to the pallet/weight totals —
    // those figures are derived, unlike hash_total below, which is a control
    // total against the source file and intentionally counts everything.
    std::vector<bool> line_excluded(join.lines.size(), false);
    for (const auto& issue : validation.issues) {
        if (issue.line_index < 0
            || static_cast<size_t>(issue.line_index) >= line_excluded.size()) {
            continue;
        }
        if (issue.severity == ValidationIssue::Severity::Error
            || issue.rule == "zero_dimension") {
            line_excluded[static_cast<size_t>(issue.line_index)] = true;
        }
    }

    std::map<LaneKey, LaneSummary> lanes;

    // ── Demand side ─────────────────────────────────────────────────────────
    for (size_t i = 0; i < join.lines.size(); ++i) {
        const JoinedLine& jl = join.lines[i];
        if (jl.str == nullptr) continue;
        const STRRecord& s = *jl.str;

        LaneKey key{s.locfrno, s.loctono, s.ship_cond};
        LaneSummary& lane = lanes[key];
        lane.locfrno   = s.locfrno;
        lane.loctono   = s.loctono;
        lane.ship_cond = s.ship_cond;
        lane.has_demand = true;

        ++lane.demand_lines;
        ++day.total_demand_lines;
        day.hash_total += s.trans;   // every line counts, matched or not

        if (jl.matched) {
            ++lane.matched_lines;
            ++day.matched_lines;
        } else {
            ++day.unmatched_lines;
        }

        if (have_pallets && !line_excluded[i]) lane.pallet_equiv += pallets_per_line[i];
        if (have_weight  && !line_excluded[i]) lane.weight_lb    += weight_per_line[i];
    }

    // ── Placeholder side ────────────────────────────────────────────────────
    for (const auto& p : placeholders) {
        LaneKey key{p.locfrno, p.loctono, p.ship_cond};
        LaneSummary& lane = lanes[key];
        lane.locfrno   = p.locfrno;
        lane.loctono   = p.loctono;
        lane.ship_cond = p.ship_cond;
        lane.has_placeholder = true;
        lane.trucks_requested += p.no_of_loads;
        day.trucks_requested  += p.no_of_loads;
    }

    // ── Roll up ─────────────────────────────────────────────────────────────
    day.lanes.reserve(lanes.size());
    for (auto& kv : lanes) {
        const LaneSummary& lane = kv.second;
        day.total_pallet_equiv += lane.pallet_equiv;
        day.total_weight_lb    += lane.weight_lb;

        if (lane.has_demand)      ++day.lanes_with_demand;
        if (lane.has_placeholder) ++day.lanes_with_placeholder;
        if (lane.has_demand && !lane.has_placeholder) ++day.lanes_demand_only;
        if (!lane.has_demand && lane.has_placeholder) ++day.lanes_placeholder_only;

        day.lanes.push_back(lane);
    }
    day.lanes_total = static_cast<int>(day.lanes.size());

    // Largest lanes first — that is what a planner scans for.
    std::sort(day.lanes.begin(), day.lanes.end(),
              [](const LaneSummary& a, const LaneSummary& b) {
                  if (a.pallet_equiv != b.pallet_equiv) return a.pallet_equiv > b.pallet_equiv;
                  return a.demand_lines > b.demand_lines;
              });

    LOG_INFO("Summary built: " + std::to_string(day.lanes_total) + " lanes, "
             + std::to_string(day.total_demand_lines) + " demand lines, "
             + std::to_string(day.trucks_requested) + " trucks requested");

    return day;
}

void Reporter::print_summary(const DaySummary& day, std::ostream& out, int max_lanes) {
    out << "\n";
    out << "================================================================\n";
    out << " ORDER BUILDER - MILESTONE 1 SUMMARY\n";
    if (!day.planning_day.empty()) out << " Planning day: " << day.planning_day << "\n";
    out << "================================================================\n\n";

    out << "  Demand lines              " << grouped(day.total_demand_lines) << "\n";
    out << "  Matched to product master " << grouped(day.matched_lines);
    if (day.total_demand_lines > 0) {
        const double pct = 100.0 * day.matched_lines / day.total_demand_lines;
        out << "  (" << fixed(pct, 1) << "%)";
    }
    out << "\n";
    out << "  Unmatched lines           " << grouped(day.unmatched_lines) << "\n";
    out << "  Hash total (all demand)   " << grouped(day.hash_total) << " units\n";
    out << "  Total pallet-equivalents  " << grouped(day.total_pallet_equiv, 1) << "\n";
    out << "  Total weight              " << grouped(day.total_weight_lb) << " lb\n";
    out << "\n";
    out << "  Lanes                     " << grouped(day.lanes_total) << "\n";
    out << "    with demand             " << grouped(day.lanes_with_demand) << "\n";
    out << "    with placeholder        " << grouped(day.lanes_with_placeholder) << "\n";
    out << "    demand only             " << grouped(day.lanes_demand_only) << "\n";
    out << "    placeholder only        " << grouped(day.lanes_placeholder_only) << "\n";
    out << "  Trucks requested          " << grouped(day.trucks_requested) << "\n";
    out << "\n";

    // ── Per-lane table ──────────────────────────────────────────────────────
    out << "----------------------------------------------------------------\n";
    out << " PER-LANE SUMMARY";
    const int shown = (max_lanes > 0 && max_lanes < static_cast<int>(day.lanes.size()))
                          ? max_lanes : static_cast<int>(day.lanes.size());
    if (shown < static_cast<int>(day.lanes.size())) {
        out << "  (top " << shown << " of " << day.lanes.size() << " by volume)";
    }
    out << "\n";
    out << "----------------------------------------------------------------\n";

    pad_right(out, "From",   8);
    pad_right(out, "To",     8);
    pad_right(out, "SC",     4);
    pad_left (out, "Lines",  8);
    pad_left (out, "Match",  8);
    pad_left (out, "Pallet-eq", 12);
    pad_left (out, "Weight(lb)", 14);
    pad_left (out, "Trucks", 8);
    out << "\n";

    for (int i = 0; i < shown; ++i) {
        const LaneSummary& l = day.lanes[static_cast<size_t>(i)];
        pad_right(out, l.locfrno,   8);
        pad_right(out, l.loctono,   8);
        pad_right(out, l.ship_cond, 4);
        pad_left (out, grouped(l.demand_lines),      8);
        pad_left (out, grouped(l.matched_lines),     8);
        pad_left (out, grouped(l.pallet_equiv, 1),  12);
        pad_left (out, grouped(l.weight_lb),        14);
        pad_left (out, grouped(l.trucks_requested),  8);
        out << "\n";
    }

    out << "\n  Pallet-eq is a summed pallet-equivalent: each demand line is\n";
    out << "  converted to a pallet fraction and the fractions are added across\n";
    out << "  the lane. It is not a count of physical pallets.\n\n";
}

void Reporter::print_warnings(const ValidationReport& report,
                              std::ostream& out,
                              int max_examples_per_rule) {
    out << "----------------------------------------------------------------\n";
    out << " WARNINGS AND REJECTIONS\n";
    out << "----------------------------------------------------------------\n";

    if (report.issues.empty()) {
        out << "  Nothing flagged. All demand lines passed validation.\n\n";
        return;
    }

    out << "  " << report.errors << " error(s), " << report.warnings << " warning(s)\n\n";

    // Group by rule so a planner sees the shape before the detail.
    std::map<std::string, std::vector<const ValidationIssue*>> by_rule;
    for (const auto& issue : report.issues) by_rule[issue.rule].push_back(&issue);

    for (const auto& kv : by_rule) {
        const auto& rule  = kv.first;
        const auto& items = kv.second;
        out << "  " << rule << "  (" << items.size() << ")\n";

        const int limit = (max_examples_per_rule > 0)
                              ? std::min<int>(max_examples_per_rule,
                                              static_cast<int>(items.size()))
                              : static_cast<int>(items.size());
        for (int i = 0; i < limit; ++i) {
            const ValidationIssue& issue = *items[static_cast<size_t>(i)];
            out << "      line " << issue.line_index;
            if (!issue.matnr.empty()) out << ", material " << issue.matnr;
            out << ": " << issue.message << "\n";
        }
        if (limit < static_cast<int>(items.size())) {
            out << "      ... and " << (items.size() - static_cast<size_t>(limit))
                << " more\n";
        }
        out << "\n";
    }
}

} // namespace ob
