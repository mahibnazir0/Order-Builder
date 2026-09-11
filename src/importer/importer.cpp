#include "importer.hpp"
#include "logger.hpp"
#include "json_util.hpp"

#include <fstream>
#include <stdexcept>

namespace ob {

using json = nlohmann::json;

namespace {

STRRecord parse_str(const json& j) {
    STRRecord r;
    r.idpr               = get_or<std::string>(j, "IDPR", "");
    r.bnfpo              = get_or<int>(j, "BNFPO", 0);
    r.locfrno            = get_or<std::string>(j, "LOCFRNO", "");
    r.loctono            = get_or<std::string>(j, "LOCTONO", "");
    r.matnr               = get_or<std::string>(j, "MATNR", "");
    r.datfr_ta           = get_or<std::string>(j, "DATFR_TA", "");
    r.datto_ta           = get_or<std::string>(j, "DATTO_TA", "");
    r.ship_cond          = get_or<std::string>(j, "SHIP_COND", "");
    r.planner_trans      = get_or<std::string>(j, "PLANNER_TRANS", "");
    r.planner_snp        = get_or<std::string>(j, "PLANNER_SNP", "");
    r.planner_trans_nmix = get_or<std::string>(j, "PLANNER_TRANS_NMIX", "");
    r.confirmed_date     = get_or<std::string>(j, "CONFIRMED_DATE", "");
    r.avail_date         = get_or<std::string>(j, "AVAIL_DATE", "");
    r.tprio              = get_or<int>(j, "TPRIO", 0);
    r.trans              = get_or<double>(j, "TRANS", 0.0);
    r.avail_qty          = get_or<int>(j, "AVAIL_QTY", 0);
    r.unitofmeas         = get_or<std::string>(j, "UNITOFMEAS", "");
    r.ctl_date           = get_or<std::string>(j, "CTL_DATE", "");
    r.bstrf              = get_or<int>(j, "BSTRF", 0);
    r.gr_proc_time       = get_or<int>(j, "GR_PROC_TIME", 0);
    r.gi_proc_time       = get_or<int>(j, "GI_PROC_TIME", 0);
    r.pl_deliv_time      = get_or<int>(j, "PL_DELIV_TIME", 0);
    return r;
}

CTLRecord parse_ctl(const json& j) {
    CTLRecord r;
    r.zdate            = get_or<std::string>(j, "ZDATE", "");
    r.zday             = get_or<std::string>(j, "ZDAY", "");
    r.level_load_start = get_or<int>(j, "LEVEL_LOAD_START", 0);
    r.level_load_end   = get_or<int>(j, "LEVEL_LOAD_END", 0);
    r.auto_o2          = get_or<int>(j, "AUTO_O2", 0);
    return r;
}

DNMRecord parse_dnm(const json& j) {
    DNMRecord r;
    r.planner_snp = get_or<std::string>(j, "PLANNER_SNP", "");
    r.locfrno     = get_or<std::string>(j, "LOCFRNO", "");
    return r;
}

} // anonymous namespace

DemandFile Importer::load_demand(const std::string& json_path) {
    std::ifstream in(json_path);
    if (!in) {
        throw std::runtime_error("Cannot open demand file: " + json_path);
    }

    json root;
    try {
        in >> root;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("Demand file is not valid JSON: " + std::string(e.what()));
    }

    DemandFile demand;
    demand.request_id = get_or<std::string>(root, "REQUEST_ID", "");

    // Each block is optional at the top level: absent means empty. Present
    // but the wrong shape (e.g. STR as an object) is malformed input and
    // throws instead — see get_optional_array.
    if (const auto* str = get_optional_array(root, "STR", "Demand file")) {
        demand.str.reserve(str->size());
        for (const auto& item : *str) demand.str.push_back(parse_str(item));
    }
    if (const auto* ctl = get_optional_array(root, "CTL", "Demand file")) {
        for (const auto& item : *ctl) demand.ctl.push_back(parse_ctl(item));
    }
    if (const auto* dnm = get_optional_array(root, "DNM", "Demand file")) {
        for (const auto& item : *dnm) demand.dnm.push_back(parse_dnm(item));
    }

    LOG_INFO("Loaded demand: " + std::to_string(demand.str.size()) + " STR, "
             + std::to_string(demand.ctl.size()) + " CTL, "
             + std::to_string(demand.dnm.size()) + " DNM");

    return demand;
}

} // namespace ob
