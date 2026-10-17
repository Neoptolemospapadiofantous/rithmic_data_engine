#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
using namespace lab;
int main(){
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while(std::getline(f,l)){int y,m,d; if(sscanf(l.c_str(),"%d-%d-%d",&y,&m,&d)==3) fomc.insert(y*10000+m*100+d);} }
  paper::VixRegime vix; vix.load("data/regime");
  Series s, es; std::string err; load_raw("data/edge_lab/cache","NQ",s,err); load_raw("data/edge_lab/cache","ES",es,err);
  build_days(s,fomc,vix); build_days(es,fomc,vix);
  Market m(s), mes(es); Evaluator ev(m);
  std::ifstream f("research/ideas/so_cascade.json"); Spec sp=parse_spec(json::parse(f));
  std::vector<std::vector<Trade>> vt;
  for(auto&p:grid(sp)){Variant v; v.p=p; for(auto&c:sp.checks){v.checks.emplace_back(parse(c.when,p),parse(c.dir,p));ev.bind(*v.checks.back().first);ev.bind(*v.checks.back().second);}
    v.stop=parse(sp.stop,p);v.target=parse(sp.target,p);ev.bind(*v.stop);ev.bind(*v.target); auto t=run_variant(ev,s,sp,v,0); for(auto&x:t) x.var=(int)vt.size(); vt.push_back(t);}
  WfOut w=walk(s,vt,1.0,sp.train_months,5);
  MetaData d=meta_data(m,&es,w.trades,1.0);
  auto run=[&](const char* name, GbmParams P, int miny){
    auto fit=[&](const std::vector<std::vector<float>>&X,const std::vector<double>&y){return gbm_fit(X,y,P);};
    auto pr=meta_walk(d,d.y,miny,fit);
    // compare on the common test years 2015+ so every setting is judged on the same trades
    double nb=0,nm=0; int cb=0,cm=0;
    for(size_t i=0;i<d.tr.size();++i){ if(d.year[i]<2015||std::isnan(pr[i])) continue; double x=d.tr[i].pts-1.0; nb+=x; ++cb; if(pr[i]>0){nm+=x;++cm;} }
    printf("%-28s base %4d %+6.2f | meta %4d %+6.2f  (kept %.0f%%)\n",name,cb,nb/cb,cm,nm/cm,100.0*cm/cb);
  };
  run("default 150/60/0.05 y3",{},3);
  run("rounds 75",{75,60,10,0.05},3);
  run("rounds 300",{300,60,10,0.05},3);
  run("min_leaf 30",{150,30,10,0.05},3);
  run("min_leaf 120",{150,120,10,0.05},3);
  run("shrink 0.1",{150,60,10,0.1},3);
  run("bins 5",{150,60,5,0.05},3);
  run("min train years 4",{},4);
}
