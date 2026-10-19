// 2026-10-19 (founder: "45% WR with good risk profile and accounts we can blow up = winning scenario"): the Vest 5-account operation
// (account_5aday rules: $150 eval / $80 funded risk, room-capped, blown accounts replaced) on SYNTHETIC independent trades at a given
// win rate, 1.5R target, with and without a 0.06R cost. Shows the win rate a stream needs; real streams are in account_5aday.cpp.
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
int main(){ std::mt19937 g(3); std::uniform_real_distribution<double> U(0,1);
 const double WR[]={0.40,0.42,0.43,0.45,0.48,0.50}; const int TPD[]={1,3,5};
 printf("WR   trades/day | cost 0  : net$/yr losing%%  | cost 0.06R (2 pts on a 32-pt stop): net$/yr losing%%\n");
 for(double wr:WR) for(int n:TPD){ printf("%.0f%%  %d          ", wr*100, n);
  for(double cost:{0.0,0.0625}){ std::vector<double> yr; for(int y=0;y<4000;y++){ struct A{double b; bool f;}; std::vector<A> ac; double fees=0,paid=0;
    for(int d=0;d<252;d++){ while(ac.size()<5){ac.push_back({0,false}); fees+=40;}
      std::vector<double> tr(n); for(auto&x:tr) x=(U(g)<wr?1.5:-1.0)-cost;   // same trades on every account
      for(auto&a:ac){ double dp=0, rk=a.f?80:150; for(double r:tr){ double room=std::min(150+dp,300+a.b), risk=std::min(rk,room); if(risk<20)break; double pl=r*risk; dp+=pl; a.b+=pl; if(!a.f&&a.b>=500)break; if(a.b<=-300+1e-9||dp<=-150+1e-9)break; }
        if(!a.f&&a.b>=500){a.f=true;a.b=0;} else if(a.f&&a.b>=500){paid+=0.8*(a.b-250);a.b=250;} }
      ac.erase(std::remove_if(ac.begin(),ac.end(),[](const A&a){return a.b<=-300+1e-9;}),ac.end()); }
    yr.push_back(paid-fees);} double m=0; int l=0; for(double x:yr){m+=x; if(x<0)l++;} printf("| %+8.0f  %5.1f%%   ", m/yr.size(), 100.0*l/yr.size()); }
  printf("\n"); } }
