// The general flag settings found on 2023-10 → 2026-10, tested ONCE on 2010-07 → 2023-09 (never seen by the sweep). NY session.
const fs=require('fs'); const readline=require('readline');
const atr={}; for(const l of fs.readFileSync(process.argv[3],'utf8').trim().split('\n')){ const [d,a]=l.split(','); atr[+d]=+a; }
const tday=min=>{const s=min*60+6*3600; const d=new Date(s*1000); return d.getUTCFullYear()*10000+(d.getUTCMonth()+1)*100+d.getUTCDate();};
const dayMin=min=>((min%1440)-1080+1440)%1440;
function agg(b,k){ if(k===1) return b; const out=[]; let cur=null; for(const r of b){ const bk=Math.floor(r[0]/k)*k; if(!cur||cur[0]!==bk){cur=[bk,r[1],r[2],r[3],r[4]]; out.push(cur);} else {cur[2]=Math.max(cur[2],r[2]); cur[3]=Math.min(cur[3],r[3]); cur[4]=r[4];} } return out; }
const SETS=[ {name:'5m bull  pole 6 >= 0.15 ATR, flag 5 <= 0.6, 1R', tf:5,N:6,mult:0.15,F:5,tight:0.6,tg:1,side:1},
             {name:'5m bull  pole 6 >= 0.2 ATR,  flag 5 <= 0.6, 1R', tf:5,N:6,mult:0.2,F:5,tight:0.6,tg:1,side:1},
             {name:'5m both  pole 6 >= 0.2 ATR,  flag 5 <= 0.6, 1R', tf:5,N:6,mult:0.2,F:5,tight:0.6,tg:1,side:0},
             {name:'1m bull  pole 10 >= 0.3 ATR, flag 3 <= 0.5, 1.5R', tf:1,N:10,mult:0.3,F:3,tight:0.5,tg:1.5,side:1},
             {name:'1m bull  pole 10 >= 0.3 ATR, flag 3 <= 0.5, 2R', tf:1,N:10,mult:0.3,F:3,tight:0.5,tg:2,side:1},
             {name:'5m bear  pole 6 >= 0.2 ATR,  flag 5 <= 0.6, 1R (for comparison)', tf:5,N:6,mult:0.2,F:5,tight:0.6,tg:1,side:-1} ];
const acc=SETS.map(()=>({n:0,w:0,gp:0,gl:0,e:[[0,0],[0,0]]}));
function runDay(d,rows){ if(!(d in atr)) return; const A=atr[d]; const w=new Date(Date.UTC(Math.floor(d/10000),Math.floor(d/100)%100-1,d%100)).getUTCDay(); if(w==0||w==6) return;
  const ny=rows.filter(r=>{const m=dayMin(r[0]); return m>=930&&m<1320;}); if(ny.length<300) return; const era=d<20190101?0:1;
  SETS.forEach((p,si)=>{ const b=agg(ny,p.tf); let skip=-1;
    for(let k=p.N+p.F+1;k<b.length;k++){ if(k<=skip) continue; const pe=k-1-p.F, ps=pe-p.N, pole=b[pe][4]-b[ps][4]; if(Math.abs(pole)<p.mult*A) continue;
      let path=0; for(let j=ps+1;j<=pe;j++) path+=Math.abs(b[j][4]-b[j-1][4]); if(!path||Math.abs(pole)/path<0.5) continue;
      let fh=-1e18,fl=1e18; for(let j=pe+1;j<k;j++){fh=Math.max(fh,b[j][2]); fl=Math.min(fl,b[j][3]);} if(fh-fl>p.tight*Math.abs(pole)) continue;
      const dir=pole>0?1:-1; if(dir>0?b[k][4]<=fh:b[k][4]>=fl) continue; if(p.side&&dir!==p.side) continue;
      const e=b[k][4], st=dir>0?fl-0.25:fh+0.25, risk=dir*(e-st); if(risk<=0.25) continue; skip=k+p.F;
      const tgt=e+dir*p.tg*risk; let out=dir*(b[b.length-1][4]-e);
      for(let j=k+1;j<b.length;j++){ const hs=dir>0?b[j][3]<=st:b[j][2]>=st, ht=dir>0?b[j][2]>=tgt:b[j][3]<=tgt; if(hs){out=-risk;break;} if(ht){out=dir*(tgt-e);break;} }
      const net=out-1, a=acc[si]; a.n++; if(net>0){a.w++; a.gp+=net;} else a.gl-=net; a.e[era][0]+=net; a.e[era][1]++; } }); }
(async()=>{ const rl=readline.createInterface({input:fs.createReadStream(process.argv[2])}); let cur=-1, rows=[];
  for await (const l of rl){ const f=l.split(','); const t=+f[0]; if(t>=1696132800) break; const m=Math.floor(t/60), d=tday(m); if(d!==cur){ if(rows.length) runDay(cur,rows); cur=d; rows=[]; } rows.push([m,+f[1],+f[2],+f[3],+f[4]]); }
  if(rows.length) runDay(cur,rows);
  console.log('HOLDOUT 2010-07 → 2023-09, NY, net of 1 NQ pt (points then were 1,800–16,000, so points are smaller than today):');
  SETS.forEach((p,i)=>{ const a=acc[i]; const pf=a.gl?a.gp/a.gl:0; console.log(`  ${p.name.padEnd(62)} n ${String(a.n).padStart(5)} (${(a.n/13.2).toFixed(0)}/yr) win ${(100*a.w/a.n).toFixed(1)}% PF ${pf.toFixed(2)} net ${((a.gp-a.gl)/a.n).toFixed(2)} pts | 2010-18 ${(a.e[0][0]/a.e[0][1]).toFixed(2)} (${a.e[0][1]}) · 2019-23 ${(a.e[1][0]/a.e[1][1]).toFixed(2)} (${a.e[1][1]})`); }); })();
