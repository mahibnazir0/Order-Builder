#include "m4SyntheticDay.hpp"
#include "benchmarkHarness.hpp"
#include "shipDatePlanner.hpp"
#include "json.hpp"
#include <iostream>
#include <random>
#include <functional>
#include <limits>
using namespace ob; using namespace m4SyntheticDay; using J=nlohmann::json;
int main(){
 std::mt19937 rng(120112); const std::vector<std::pair<double,double>> dims{{72,60},{80,48},{120,48},{48,48},{96,40},{60,60},{40,33},{100,50},{110,40},{90,55},{80,60},{100,60}};
 auto settings=selectorSettings();
 for(int trial=0;trial<15000;++trial){
  const unsigned n=11; std::vector<SelectionItem> goods; std::vector<FloorItem> floor;
  double area=0;
  for(unsigned i=0;i<n;++i){auto d=dims[rng()%dims.size()];auto x=item(0,i,1000,0,d.first,d.second); x.productNumber="P"+std::to_string(100+i);x.copyIndex=i;goods.push_back(x);floor.push_back(floorItemFrom(x));area+=d.first*d.second;}
  if(area>63000)continue;
  auto sorted=arrangeFloor(floor,kTrailer,settings.floor);if(sorted.fitsLength)continue;
  const unsigned size=1u<<n,all=size-1;
  struct Row {unsigned mask;double depth;FloorRow layout;};std::vector<std::vector<Row>> rows(n);
  for(unsigned mask=1;mask<size;++mask){if(__builtin_popcount(mask)>4)continue;std::vector<std::size_t> members;for(unsigned i=0;i<n;++i)if(mask&(1u<<i))members.push_back(i);
   for(const auto&r:candidateRows(floor,members,kTrailer,settings.floor))if(r.widthIn<=100.000001){rows[static_cast<unsigned>(__builtin_ctz(mask))].push_back({mask,r.depthIn,r});}
  }
  std::vector<double> dp(size,-1);std::vector<unsigned> choice(size,0);dp[0]=0;
  std::function<double(unsigned)> solve=[&](unsigned mask){if(dp[mask]>=0)return dp[mask];double best=1e20;unsigned first=static_cast<unsigned>(__builtin_ctz(mask));for(const auto&r:rows[first])if((mask&r.mask)==r.mask){double v=r.depth+solve(mask^r.mask);if(v<best){best=v;choice[mask]=r.mask;}}return dp[mask]=best;};
  const double exact=solve(all);if(exact>630.000001)continue;
  auto day=dayOf({group("2027")},n,goods,{reserved("2027",1)});auto plan=selectLoads(day.run,day.book,day.items,day.classification,settings);
  double selected=0;for(const auto&t:plan.trucks)selected+=t.unitLoads;
  if(selected>=n)continue;
  J witness=J::array();unsigned remaining=all;while(remaining){unsigned m=choice[remaining],first=static_cast<unsigned>(__builtin_ctz(m));double best=1e20;const Row* winner=nullptr;for(const auto&r:rows[first])if(r.mask==m&&r.depth<best){best=r.depth;winner=&r;}J placements=J::array();for(const auto&p:winner->layout.placements)placements.push_back({{"item",p.itemIndex},{"turned",p.turned}});witness.push_back({{"pattern",floorPatternName(winner->layout.pattern)},{"depth",best},{"width",winner->layout.widthIn},{"placements",placements}});remaining^=m;}
  J data=J::array();for(const auto&x:goods)data.push_back({{"long",x.footprintLongIn},{"short",x.footprintShortIn},{"height",x.heightIn},{"unit_loads",x.unitLoads},{"weight",x.weightLb}});
  auto dated=planShipDates(day.run,day.book,day.items,kRouting,settings,{true,true},10);
  double datedUnits=0,selectedLength=0;for(const auto&t:dated.plan.trucks)datedUnits+=t.unitLoads;for(const auto&t:plan.trucks)selectedLength+=t.floor.usedLengthIn;
  J out={{"trial",trial},{"items",data},{"sorted_length",sorted.usedLengthIn},{"valid_alternative_length",exact},{"alternative_rows",witness},{"selected_unit_loads",selected},{"selected_length",selectedLength},{"known_feasible_unit_loads",n},{"proven",plan.routes[0].proven},{"unproven_because",plan.routes[0].unprovenBecause},{"invariant_violations",checkInvariants(day.run,day.book,day.items,plan,64).size()},{"date_planner_unit_loads",datedUnits},{"date_planner_proven",dated.plan.routes[0].proven}};
  std::cout<<out.dump(2)<<std::endl;return 0;
 }
 std::cout<<"No counterexample found"<<std::endl;return 1;
}
