#pragma once

#include "json.hpp"
#include "pipeline.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

// A small M4 day written to files and run through the real pipeline, for tests that need the
// whole run: products, demand, placeholders and the shipped params.
namespace m4SmallDay {

inline const std::string kShippedParamsPath = "config/orderBuilderParams.json";

// A day of two products on one lane, planned on Tuesday 4 Aug for 6-7 Aug, with two trucks
// reserved on 6 Aug. Written to a folder of its own and removed with it. `dnm` is the DNM
// block's entries, comma-separated.
struct SmallDay {
    std::filesystem::path directory;
    ob::PipelineInputs inputs;

    explicit SmallDay(const std::string& name, const std::string& extraLines = "",
                      const std::string& extraPlaceholders = "", const std::string& dnm = "") {
        directory = std::filesystem::temp_directory_path() / ("obM4Pipeline_" + name);
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        const std::string product = (directory / "products.csv").string();
        const std::string demand = (directory / "demand.json").string();
        const std::string placeholder = (directory / "placeholder.json").string();
        const std::string pallets = (directory / "pallets.csv").string();
        std::ofstream(pallets) << "ID,Footprint_Length,Footprint_Width,Height,Weight\nTLD,48,40,0.1,1\n";
        std::ofstream(product) << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
                                  "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
                                  "GOOD,Good,10,10,10,5,CS,20,4,2,8,TLD\n"
                                  "OTHER,Other,10,10,10,5,CS,30,4,2,8,TLD\n";
        std::ofstream(demand) << R"({"REQUEST_ID":"#STR_PA4400_20260804100000#","DNM":[)" << dnm << "],"
                                 R"("CTL":[{"ZDATE":"","ZDAY":"TUESDAY","LEVEL_LOAD_START":2,"LEVEL_LOAD_END":3,"AUTO_O2":2}],)"
                                 R"("STR":[)"
                              << line("GOOD", 240, "TL") << "," << line("OTHER", 160, "TL") << extraLines << "]}";
        std::ofstream(placeholder) << R"({"PHOLDER":[{"LOCFRNO":"1","LOCTONO":"2","SHIP_COND":"TL","DATFR_TA":"2026-08-06",)"
                                      R"("DATTO_TA":"2026-08-08","ZZNA_EQUIP_SIZE":"53F","NO_OF_LOADS":2,"EBELN":""})"
                                   << extraPlaceholders << "]}";
        inputs.product_path = product;
        inputs.demand_path = demand;
        inputs.placeholder_path = placeholder;
        inputs.palletPath = pallets;
        inputs.paramsPath = kShippedParamsPath;
        inputs.demandSelector = ob::parseDemandSelector("calendar:2026-08-04");
        inputs.runSelection = true;
    }
    ~SmallDay() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    SmallDay(const SmallDay&) = delete;
    SmallDay& operator=(const SmallDay&) = delete;

    static nlohmann::json lineJson(const std::string& matnr, int cases, const std::string& shipCondition) {
        return {{"IDPR", "1"},          {"BNFPO", 10},           {"LOCFRNO", "1"},        {"LOCTONO", "2"},
                {"MATNR", matnr},       {"DATFR_TA", "2026-08-06"}, {"DATTO_TA", "2026-08-08"},
                {"SHIP_COND", shipCondition}, {"PLANNER_SNP", "S01"}, {"AVAIL_DATE", "2026-08-04"},
                {"TPRIO", 0},           {"AVAIL_QTY", 0},        {"TRANS", cases},        {"UNITOFMEAS", "CS"}};
    }
    static std::string line(const std::string& matnr, int cases, const std::string& shipCondition) {
        return lineJson(matnr, cases, shipCondition).dump();
    }
    static nlohmann::json placeholderJson(const std::string& origin, int loads) {
        return {{"LOCFRNO", origin},        {"LOCTONO", "2"},  {"SHIP_COND", "TL"},
                {"DATFR_TA", "2026-08-06"}, {"DATTO_TA", "2026-08-08"},
                {"ZZNA_EQUIP_SIZE", "53F"}, {"NO_OF_LOADS", loads}, {"EBELN", ""}};
    }
};

} // namespace m4SmallDay
