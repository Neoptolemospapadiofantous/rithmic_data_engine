#include "../../../../../home/theone/Desktop/rithmic_engine/src/research/meta.hpp"
#include <cstdio>
#include <fstream>
using namespace lab;
int main(){
  Series s; std::string err; if(!load_raw("data/edge_lab/cache","NQ",s,err)){puts(err.c_str());return 1;}
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); build_days(s,fomc,vix);
  Market m(s); Evaluator ev(m);
  std::ifstream f("research/ideas/so_cascade.json"); auto j=json::parse(f); Spec sp=parse_spec(j);
  auto p=grid(sp)[5]; Variant v; v.p=p;
  for(auto&c:sp.checks){v.checks.emplace_back(parse(c.when,p),parse(c.dir,p));ev.bind(*v.checks.back().first);ev.bind(*v.checks.back().second);}
  v.stop=parse(sp.stop,p);v.target=parse(sp.target,p);ev.bind(*v.stop);ev.bind(*v.target);
  auto tr=run_variant(ev,s,sp,v,0);
  ExitSim sim(m); int mis=0; double maxd=0;
  for(auto&t:tr){auto r=sim.run(t,X_BASE); double dd=std::fabs(r.pts-t.pts); if(dd>1e-6){++mis; maxd=std::max(maxd,dd);} }
  printf("trades %zu base mismatch %d maxdiff %g\n",tr.size(),mis,maxd);
  // forced at the thesis bar must reproduce the thesis exit
  int bad=0,fired=0; double sumr=0,sumf=0,sumflat=0;
  for(auto&t:tr){auto r=sim.run(t,X_VWAP); if(!r.thesis) continue; ++fired; auto z=sim.run(t,X_VWAP,r.x); if(std::fabs(z.pts-r.pts)>1e-6) ++bad; sumr+=r.pts;
     auto zf=sim.run(t,X_VWAP,t.flat+1); sumflat+=zf.pts; auto b=sim.run(t,X_BASE); sumf+=b.pts;}
  { std::vector<int> d; std::vector<size_t> fi; std::vector<ExitOut> rr; for(size_t i=0;i<tr.size();++i){auto r=sim.run(tr[i],X_VWAP); rr.push_back(r); if(r.thesis){d.push_back(r.x-tr[i].e); fi.push_back(i);} }
    std::mt19937_64 rng(1); double diff=0; int big=0;
    for(size_t i:fi){ int at=tr[i].e+std::max(1,d[rng()%d.size()]); auto z=sim.run(tr[i],X_VWAP,at); double df=z.pts-rr[i].pts; diff+=df; if(std::fabs(df)>150){ if(big++<5) printf("i=%zu e=%d x=%d at=%d flat=%d stop=%.1f thesis=%.1f forced=%.1f zx=%d\n",i,tr[i].e,rr[i].x,at,tr[i].flat,tr[i].stop,rr[i].pts,z.pts,z.x);} }
    printf("twin diff sum %.0f big %d durs max %d\n",diff,big,*std::max_element(d.begin(),d.end())); }
  printf("vwap fired %d forced-at-same-bar mismatches %d  thesis sum %.0f  no-thesis sum %.0f base sum %.0f\n",fired,bad,sumr,sumflat,sumf);
}
