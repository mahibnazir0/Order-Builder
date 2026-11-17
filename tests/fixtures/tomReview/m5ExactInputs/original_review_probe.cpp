#include "m4SyntheticDay.hpp"
#include "m4SmallDay.hpp"
#include "benchmarkHarness.hpp"
#include "shipDatePlanner.hpp"
#include "json.hpp"
#include <iostream>
#include <fstream>
#include <functional>

using namespace ob;
using namespace m4SyntheticDay;
using Json=nlohmann::json;

Json summarize(const Day& day,const SelectionPlan& plan){
    Json j;
    j["trucks"]=Json::array();
    for(const auto& t:plan.trucks){
        Json goods=Json::array();
        for(const auto i:t.items){const auto& x=day.items.items[i];goods.push_back({{"index",i},{"priority",x.priority},{"ship_date",x.shipDate},{"long",x.footprintLongIn},{"short",x.footprintShortIn},{"unit_loads",x.unitLoads}});}
        j["trucks"].push_back({{"date",t.shipDate},{"items",goods},{"unit_loads",t.unitLoads},{"weight",t.weightLb},{"floor_length",t.floor.usedLengthIn}});
    }
    j["routes"]=Json::array();
    for(const auto& r:plan.routes)j["routes"].push_back({{"proven",r.proven},{"unproven_because",r.unprovenBecause},{"allowed",r.trucksAllowed},{"built",r.trucksBuilt}});
    j["left_over"]=Json::array();
    for(const auto& l:plan.leftOver)j["left_over"].push_back({{"item",l.itemIndex},{"reason",leftOverReasonName(l.reason)}});
    j["invariant_violations"]=Json::array();
    for(const auto& e:checkInvariants(day.run,day.book,day.items,plan,64))j["invariant_violations"].push_back({{"id",e.invariant},{"detail",e.detail}});
    return j;
}

int main(){
    Json results=Json::object();
    {
        std::vector<SelectionItem> goods;
        for(size_t i=0;i<20;++i){auto x=item(0,0,1000,0,98.75,40);x.productNumber="A-LARGE";x.copyIndex=i;goods.push_back(x);}
        for(size_t i=0;i<20;++i){auto x=item(0,1,1000,0,48,40);x.productNumber="B-SMALL";x.copyIndex=i;goods.push_back(x);}
        auto day=dayOf({group("2027")},2,goods,{reserved("2027",1)});
        const auto result=selectLoads(day.run,day.book,day.items,day.classification,selectorSettings());
        auto summary=summarize(day,result);
        std::vector<FloorItem> alternative;
        for(size_t i=0;i<5;++i)alternative.push_back(floorItemFrom(day.items.items[i]));
        for(size_t i=20;i<40;++i)alternative.push_back(floorItemFrom(day.items.items[i]));
        const auto layout=arrangeFloor(alternative,day.run.trailer,selectorSettings().floor);
        summary["feasible_alternative_items"]=alternative.size();
        summary["feasible_alternative_floor_fits"]=layout.fitsLength && layout.unplaceableItems.empty();
        summary["feasible_alternative_used_length"]=layout.usedLengthIn;
        results["standard_53ft_single_truck_exactness"]=summary;
    }
    {
        std::vector<SelectionItem> items{item(0,0,100,0,60,60),item(0,1,100,0,60,60),item(0,2,100,0,50,50),item(0,3,100,0,50,50)};
        for(size_t i=0;i<items.size();++i){items[i].productNumber="P"+std::to_string(i);items[i].copyIndex=i;}
        auto day=dayOf({group("2027")},4,items,{reserved("2027",1)});
        day.run.trailer.interiorLengthIn=100;
        day.run.trailer.interiorWidthIn=100;
        day.classification=classifyLanes(day.run,day.book,day.items,kRouting);
        const auto settings=selectorSettings();
        const auto result=selectLoads(day.run,day.book,day.items,day.classification,settings);
        auto summary=summarize(day,result);
        std::vector<FloorItem> alternative;
        for(const auto& x:day.items.items)if(x.footprintLongIn==50)alternative.push_back(floorItemFrom(x));
        const auto layout=arrangeFloor(alternative,day.run.trailer,settings.floor);
        summary["feasible_alternative_items"]=alternative.size();
        summary["feasible_alternative_floor_fits"]=layout.fitsLength && layout.unplaceableItems.empty();
        summary["feasible_alternative_used_length"]=layout.usedLengthIn;
        results["single_truck_exactness_after_floor_repair"]=summary;
    }
    {
        m4SmallDay::SmallDay inputs("review-mixed-dates-switch-off");
        Json demand;{std::ifstream f(inputs.inputs.demand_path);f>>demand;}
        auto first=m4SmallDay::SmallDay::lineJson("GOOD",8,"TL");first["IDPR"]="DUE";
        auto future=first;future["IDPR"]="FUTURE";future["DATFR_TA"]="2026-08-08";future["DATTO_TA"]="2026-08-10";
        demand["STR"]=Json::array({first,future});{std::ofstream f(inputs.inputs.demand_path);f<<demand.dump(2);}
        Json params;{std::ifstream f(inputs.inputs.paramsPath);f>>params;}
        params["selection"]["carryUnshippedForward"]=false;params["selection"]["pullForward"]=false;
        inputs.inputs.paramsPath=(inputs.directory/"params.json").string();{std::ofstream f(inputs.inputs.paramsPath);f<<params.dump(2);}
        std::filesystem::create_directories("../probe-data");
        std::filesystem::copy(inputs.directory,"../probe-data/mixed-dates-switch-off",std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
        std::ofstream pipelineLog("../pipeline-probe.log");
        auto* previousOutput=std::cout.rdbuf(pipelineLog.rdbuf());
        auto run=Pipeline::run(inputs.inputs);
        std::cout.rdbuf(previousOutput);
        Json summary={{"outcome",static_cast<int>(runOutcome(run))},{"pull_forward_to_fill",run.selectionParams.shipDates.pullForward},
          {"mixed_date_stacks",run.selectionItems.itemsMixingShipDates},{"violations",run.selectionViolations.size()}};
        summary["shipped_lines"]=Json::array();
        for(const auto& t:run.selectionPlan.trucks)for(const auto i:t.items)for(const auto l:run.selectionItems.items[i].lineIndices)
            summary["shipped_lines"].push_back({{"id",run.join.lines[l].str->idpr},{"due",run.join.lines[l].str->datfr_ta},{"truck_date",t.shipDate}});
        results["pull_forward_disabled_mixed_stack"]=summary;
    }
    {
        auto old=item(0,0,1000,11,50,42);old.shipDate="2026-08-05";old.timing=ShipTiming::Overdue;
        auto urgent=item(0,1,1000,0,50,42);urgent.shipDate="2026-08-06";
        std::vector<SelectionItem> goods(30,old);goods.insert(goods.end(),30,urgent);
        auto day=dayOf({group("2027")},2,goods,{reserved("2027",1)});
        auto p=planShipDates(day.run,day.book,day.items,kRouting,selectorSettings(),{true,false},10);
        results["backlog_priority_vs_new_demand"]=summarize(day,p.plan);
    }
    {
        std::vector<SelectionItem> goods{item(0,0,1000,0,50,42)};
        for(size_t i=0;i<29;++i)goods.push_back(item(0,0,1000,11,50,42));
        for(size_t i=0;i<20;++i)goods.push_back(item(1,1,1000,0,50,42));
        auto day=dayOf({group("2027",SplitReason::DoNotMix,"A"),group("2027",SplitReason::DoNotMix,"B")},2,goods,{reserved("2027",1)});
        auto p=planShipDates(day.run,day.book,day.items,kRouting,selectorSettings(),{false,false},10);
        auto summary=summarize(day,p.plan);
        std::vector<FloorItem> alternative;
        for(const auto& item:day.items.items)if(item.groupIndex==1)alternative.push_back(floorItemFrom(item));
        const auto layout=arrangeFloor(alternative,day.run.trailer,selectorSettings().floor);
        summary["feasible_priority_0_alternative"]=alternative.size();
        summary["alternative_floor_fits"]=layout.fitsLength && layout.unplaceableItems.empty();
        results["priority_across_segregated_groups"]=summary;
    }
    {
        // Group A's due goods have room in their own truck; future group B must not steal a
        // truck from currently due group C. The classifier says future filler never decides it.
        std::vector<SelectionItem> goods;
        for(size_t i=0;i<10;++i)goods.push_back(item(0,0,1000,0,50,42));
        for(size_t i=0;i<1;++i)goods.push_back(item(1,1,1000,0,50,42));
        for(size_t i=0;i<30;++i){auto x=item(0,0,1000,0,50,42);x.shipDate="2026-08-08";x.timing=ShipTiming::Future;goods.push_back(x);}
        auto day=dayOf({group("2027",SplitReason::DoNotMix,"A"),group("2027",SplitReason::DoNotMix,"B")},2,goods,{reserved("2027",1)});
        auto p=planShipDates(day.run,day.book,day.items,kRouting,selectorSettings(),{false,true},10);
        results["future_filler_shared_reservation"]=summarize(day,p.plan);
    }
    {
        std::vector<SelectionItem> goods;
        for(size_t i=0;i<12;++i){auto x=item(0,0,1000,0,60,60);x.productNumber="A-SQUARE";x.copyIndex=i;goods.push_back(x);}
        for(size_t i=0;i<11;++i){auto x=item(1,1,1000,0,48,40);x.productNumber="B-STANDARD";x.copyIndex=i;goods.push_back(x);}
        auto day=dayOf({group("2027",SplitReason::DoNotMix,"A"),group("2027",SplitReason::DoNotMix,"B")},2,goods,{reserved("2027",1)});
        const auto result=selectLoads(day.run,day.book,day.items,day.classification,selectorSettings());
        auto summary=summarize(day,result);std::vector<FloorItem> alternative;
        for(const auto& x:day.items.items)if(x.groupIndex==1)alternative.push_back(floorItemFrom(x));
        const auto layout=arrangeFloor(alternative,day.run.trailer,selectorSettings().floor);
        summary["alternative_group_B_unit_loads"]=11;summary["alternative_floor_fits"]=layout.fitsLength && layout.unplaceableItems.empty();
        summary["alternative_length"]=layout.usedLengthIn;
        results["group_reservation_estimate_ignores_floor"]=summary;
    }
    {
        auto old=item(0,0,1000,11,50,42);old.shipDate="2026-08-05";old.timing=ShipTiming::Overdue;
        auto urgent=item(0,1,1000,0,50,42);urgent.shipDate="2026-08-06";
        std::vector<SelectionItem> goods(30,old);goods.insert(goods.end(),30,urgent);
        auto day=dayOf({group("2027")},2,goods,{reserved("2027",1)});
        auto routing=kRouting;routing.pastDueOrder=PastDueOrder::PriorityFirst;
        auto planned=planShipDates(day.run,day.book,day.items,routing,selectorSettings(),{true,false},10);
        results["priority_first_configured"]=summarize(day,planned.plan);
    }
    std::cout<<results.dump(2)<<'\n';
}
