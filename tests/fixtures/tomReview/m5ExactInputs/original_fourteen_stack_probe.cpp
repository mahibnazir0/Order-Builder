#include "m4SyntheticDay.hpp"
#include "benchmarkHarness.hpp"
#include "json.hpp"
#include <fstream>
#include <iostream>
using namespace ob;using namespace m4SyntheticDay;using J=nlohmann::json;
int main(){J seed;std::ifstream("monotonicity-standard-results.json")>>seed;const std::vector<std::pair<double,double>> extras{{100,100},{120,60},{96,60},{80,80},{100,60},{120,48},{140,60},{90,90},{100,80}};
 for(auto d:extras){std::vector<SelectionItem> goods;auto x=item(0,0,1000,0,d.first,d.second);x.productNumber="DISTRACTOR";x.unitLoads=1;x.lineIndices={0};x.heightIn=50;goods.push_back(x);double expected=0;
  for(std::size_t i=0;i<seed["items"].size();++i){auto a=seed["items"][i];auto y=item(0,i+1,1000,0,a["long"],a["short"]);y.unitLoads=a["levels"].get<int>();y.lineIndices.assign(static_cast<std::size_t>(y.unitLoads),i+1);y.heightIn=50*y.unitLoads;y.productNumber="P"+std::to_string(100+i);y.copyIndex=i;goods.push_back(y);expected+=y.unitLoads;}
  auto day=dayOf({group("2027")},goods.size(),goods,{reserved("2027",1)});day.run.trailer.interiorLengthIn=seed["trailer_length"];day.classification=classifyLanes(day.run,day.book,day.items,kRouting);auto settings=selectorSettings();auto plan=selectLoads(day.run,day.book,day.items,day.classification,settings);double got=0;std::vector<std::size_t> chosen;for(auto&t:plan.trucks){got+=t.unitLoads;chosen.insert(chosen.end(),t.items.begin(),t.items.end());}
  std::vector<FloorItem> alternative;for(std::size_t i=1;i<goods.size();++i)alternative.push_back(floorItemFrom(goods[i]));auto layout=arrangeFloor(alternative,day.run.trailer,settings.floor);
  double placedLength=0,placedWeight=0;for(const auto&t:plan.trucks){placedLength+=t.floor.usedLengthIn;placedWeight+=t.weightLb;}auto violations=checkInvariants(day.run,day.book,day.items,plan,64);
  J result={{"selected_floor_length",placedLength},{"selected_weight_lb",placedWeight},{"invariant_violations",violations.size()},{"trailer_length",day.run.trailer.interiorLengthIn},{"trailer_width",day.run.trailer.interiorWidthIn},{"distractor",{d.first,d.second}},{"selected_unit_loads",got},{"selected_indices",chosen},{"known_feasible_unit_loads",expected},{"known_feasible_length",layout.usedLengthIn},{"known_feasible",layout.fitsLength},{"proven",plan.routes[0].proven},{"unproven_because",plan.routes[0].unprovenBecause}};std::cout<<result.dump()<<std::endl;
  if(plan.routes[0].proven&&got<expected&&layout.fitsLength)return 0;
 }
}
