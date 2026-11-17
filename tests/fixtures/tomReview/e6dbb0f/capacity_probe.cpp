#include "m4SyntheticDay.hpp"
#include "benchmarkHarness.hpp"
#include "json.hpp"
#include <iostream>
using namespace ob;using namespace m4SyntheticDay;using J=nlohmann::json;
int main(){J results=J::array();for(double cap:{64.,68.}){
 std::vector<SelectionItem> goods;for(std::size_t i=0;i<34;++i){auto x=stackOf(i,48,36,2,1000,"P"+std::to_string(i));x.heightIn=100;goods.push_back(x);}
 auto day=dayOf({group("2027")},goods.size(),goods,{reserved("2027",1)});auto settings=selectorSettings();settings.maxUnitLoadsPerTruck=cap;
 auto p=selectLoads(day.run,day.book,day.items,day.classification,settings);double ul=0,length=0;for(const auto&t:p.trucks){ul+=t.unitLoads;length+=t.floor.usedLengthIn;}
 results.push_back({{"configured_cap",cap},{"unit_loads",ul},{"length",length},{"proven",p.routes[0].proven},{"invariant_violations",checkInvariants(day.run,day.book,day.items,p,cap).size()}});
}std::cout<<results.dump(2)<<std::endl;}
