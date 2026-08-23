#pragma once
// ============================================================================
// demand_types.hpp — Structures for Demand-1.json
//
// Field names and types verified against the real file (24,357 STR records).
// Every STR field is present in every record with no nulls — so no optionals
// are needed. Strings that are empty in the JSON ("") stay as empty strings.
// ============================================================================

#include <string>
#include <vector>

namespace ob {

// One demand line — the "STR" block. 22 fields, all always present.
struct STRRecord {
    std::string idpr;                 // requisition ID       "1332890062"
    int         bnfpo = 0;            // line item number     10, 20, ...
    std::string locfrno;             // origin location      "2190"
    std::string loctono;             // destination          "2508"
    std::string matnr;               // material number      "105521103" (join key)
    std::string datfr_ta;            // ship date from       "2026-08-17"
    std::string datto_ta;            // ship date to         "2026-08-22"
    std::string ship_cond;           // TL | TF
    std::string planner_trans;       // often ""
    std::string planner_snp;         // "S23" — links to DNM block
    std::string planner_trans_nmix;  // often ""
    std::string confirmed_date;      // "2026-08-23"
    std::string avail_date;          // "2026-08-17"
    int         tprio = 0;           // priority 0-11
    double      trans = 0.0;         // quantity (JSON delivers as float)
    int         avail_qty = 0;       // available quantity
    std::string unitofmeas;          // CS | DIS | PAL
    std::string ctl_date;            // "2026-08-19"
    int         bstrf = 0;
    int         gr_proc_time = 0;    // goods receipt days
    int         gi_proc_time = 0;    // goods issue days
    int         pl_deliv_time = 0;   // planned delivery days
};

// Level-load schedule — the "CTL" block. Weekday OR specific date.
struct CTLRecord {
    std::string zdate;               // "2026-06-30" or "" if weekday entry
    std::string zday;                // "MONDAY".."FRIDAY" or "" if date entry
    int         level_load_start = 0;
    int         level_load_end = 0;
    int         auto_o2 = 0;
};

// Do-not-mix pair — the "DNM" block. Planner + origin.
struct DNMRecord {
    std::string planner_snp;         // "S20" — matches STRRecord.planner_snp
    std::string locfrno;             // "2027"
};

// The whole file.
struct DemandFile {
    std::string             request_id;
    std::vector<STRRecord>  str;
    std::vector<CTLRecord>  ctl;
    std::vector<DNMRecord>  dnm;
};

} // namespace ob
