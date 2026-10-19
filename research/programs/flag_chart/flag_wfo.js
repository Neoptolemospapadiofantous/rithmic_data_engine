// 2026-10-19 flags optimised another way (research/preregistered/2026-10-19_flag_context_wfo.md): A) day context, B) walk-forward settings.
const fs=require('fs'), readline=require('readline');
const atr={}; for(const l of fs.readFileSync(process.argv[3],'utf8').trim().split('\n')){ const [d,a]=l.split(','); atr[+d]=+a; }
const tday=min=>{const s=min*60+6*3600; const d=new Date(s*1000); return d.getUTCFullYear()*10000+(d.getUTCMonth()+1)*100+d.getUTCDate();};
const dayMin=min=>((min%1440)-1080+1440)%1440;
function agg(b,k){ if(k===1) return b; const out=[]; let cur=null; for(const r of b){ const bk=Math.floor(r[0]/k)*k; if(!cur||cur[0]!==bk){cur=[bk,r[1],r[2],r[3],r[4],r[5]]; out.push(cur);} else {cur[2]=Math.max(cur[2],r[2]); cur[3]=Math.min(cur[3],r[3]); cur[4]=r[4]; cur[5]+=r[5];} } return out; }
const SETS=[]; for(const N of [4,6,8]) for(const m of [0.1,0.2,0.3]) for(const F of [3,5]) for(const t of [0.4,0.6]) for(const g of [1,1.5,2]) SETS.push({tf:5,N,m,F,t,g});
for(const N of [10,15,20]) for(const m of [0.2,0.3]) for(const F of [3,5]) for(const t of [0.4,0.6]) for(const g of [1,1.5,2]) SETS.push({tf:1,N,m,F,t,g});
const trades=[];   // {si, d, ym, ctx (2 with+straight, 1 with, 0 against), R, pts}
function runDay(d,rows){ if(!(d in atr)) return; const A=atr[d]; const w=new Date(Date.UTC(Math.floor(d/10000),Math.floor(d/100)%100-1,d%100)).getUTCDay(); if(w==0||w==6) return;
  const ny=rows.filter(r=>{const m=dayMin(r[0]); return m>=930&&m<1320;}); if(ny.length<300) return; const ym=Math.floor(d/100);
  for(const tf of [1,5]){ const b=agg(ny,tf); const open=b[0][1]; const vw=[], hiS=[], loS=[]; let pv=0,vv=0,hi=-1e18,lo=1e18;
    for(const r of b){ const tp=(r[2]+r[3]+r[4])/3; pv+=tp*r[5]; vv+=r[5]; vw.push(vv>0?pv/vv:r[4]); hi=Math.max(hi,r[2]); lo=Math.min(lo,r[3]); hiS.push(hi); loS.push(lo); }
    SETS.forEach((p,si)=>{ if(p.tf!==tf) return; let skip=-1;
      for(let k=p.N+p.F+1;k<b.length;k++){ if(k<=skip) continue; const pe=k-1-p.F, ps=pe-p.N, pole=b[pe][4]-b[ps][4]; if(Math.abs(pole)<p.m*A) continue;
        let path=0; for(let j=ps+1;j<=pe;j++) path+=Math.abs(b[j][4]-b[j-1][4]); if(!path||Math.abs(pole)/path<0.5) continue;
        let fh=-1e18,fl=1e18; for(let j=pe+1;j<k;j++){fh=Math.max(fh,b[j][2]); fl=Math.min(fl,b[j][3]);} if(fh-fl>p.t*Math.abs(pole)) continue;
        const dir=pole>0?1:-1; if(dir>0?b[k][4]<=fh:b[k][4]>=fl) continue;
        const e=b[k][4], st=dir>0?fl-0.25:fh+0.25, risk=dir*(e-st); if(risk<=0.25) continue; skip=k+p.F;
        const tgt=e+dir*p.g*risk; let out=dir*(b[b.length-1][4]-e);
        for(let j=k+1;j<b.length;j++){ const hs=dir>0?b[j][3]<=st:b[j][2]>=st, ht=dir>0?b[j][2]>=tgt:b[j][3]<=tgt; if(hs){out=-risk;break;} if(ht){out=dir*(tgt-e);break;} }
        const withDay=dir*(e-open)>0 && dir*(e-vw[k])>0, straight=hiS[k]>loS[k] && Math.abs(e-open)/(hiS[k]-loS[k])>=0.55;
        trades.push({si,d,ym,ctx:withDay?(straight?2:1):0,R:(out-1)/risk,pts:out-1}); } }); } }
(async()=>{ const rl=readline.createInterface({input:fs.createReadStream(process.argv[2])}); let cur=-1, rows=[];
  for await (const l of rl){ const f=l.split(','); const m=Math.floor(+f[0]/60), d=tday(m); if(d!==cur){ if(rows.length) runDay(cur,rows); cur=d; rows=[]; } rows.push([m,+f[1],+f[2],+f[3],+f[4],+f[5]]); }
  if(rows.length) runDay(cur,rows);
  const era=d=>d<20190101?0:d<20230101?1:2, EN=['2010-18','2019-22','2023-26'];
  const st=a=>{ const n=a.length; if(!n) return {n:0,m:NaN,t:NaN}; let s=0,q=0; for(const v of a){s+=v;q+=v*v;} const m=s/n, sd=Math.sqrt(Math.max(1e-12,q/n-m*m)); return {n,m,t:m/(sd/Math.sqrt(n))}; };
  console.log(`flags over ${SETS.length} settings, NY, 2010-07 → 2026-10: ${trades.length} trade rows`);
  // A. context
  console.log('\n== A. Day context (net R per trade; median over settings, and share of settings where WITH+STRAIGHT beats AGAINST)');
  for(const tf of [5,1]){ const ids=SETS.map((p,i)=>p.tf===tf?i:-1).filter(i=>i>=0); const per={};
    for(const t of trades){ if(SETS[t.si].tf!==tf) continue; const k=t.si+'|'+t.ctx+'|'+era(t.d); (per[k]=per[k]||[]).push(t.R); }
    const med=a=>{ a=a.filter(x=>!isNaN(x)).sort((x,y)=>x-y); return a.length?a[Math.floor(a.length/2)]:NaN; };
    for(const c of [2,1,0]){ const line=[0,1,2].map(e=>{ const ms=ids.map(i=>{const a=per[i+'|'+c+'|'+e]; return a&&a.length>=20?st(a).m:NaN;}); return med(ms); });
      const n=ids.reduce((s,i)=>s+[0,1,2].reduce((q,e)=>q+((per[i+'|'+c+'|'+e]||[]).length),0),0);
      console.log(`  ${tf}m ${['AGAINST the day','WITH the day','WITH + STRAIGHT'][c].padEnd(16)} median net R by era ${line.map(x=>(x>=0?'+':'')+x.toFixed(3)).join(' / ')} | trades ${n} over ${ids.length} settings`); }
    let beat=0, cnt=0; for(const i of ids){ const all=e=>[...(per[i+'|2|'+e]||[])], ag=e=>[...(per[i+'|0|'+e]||[])]; const a=[0,1,2].flatMap(all), b=[0,1,2].flatMap(ag); if(a.length<30||b.length<30) continue; cnt++; if(st(a).m>st(b).m) beat++; }
    console.log(`  ${tf}m: WITH+STRAIGHT beats AGAINST in ${beat}/${cnt} settings (${(100*beat/cnt).toFixed(0)} %)`); }
  // B. walk-forward
  console.log('\n== B. Walk-forward: each month trade the setting (× context) best over the previous 24 months (>= 30 trades)');
  const months=[...new Set(trades.map(t=>t.ym))].sort((a,b)=>a-b); const idx=new Map(); for(const t of trades){ const k=t.ym; if(!idx.has(k)) idx.set(k,[]); idx.get(k).push(t); }
  for(const tf of [5,1]) for(const mode of ['all','with+straight']){ const oos=[]; const picks={};
    for(let mi=24;mi<months.length;mi++){ const ym=months[mi]; const past=months.slice(mi-24,mi); const sc={};
      for(const pm of past) for(const t of idx.get(pm)||[]){ if(SETS[t.si].tf!==tf) continue; if(mode==='with+straight'&&t.ctx!==2) continue; const s=sc[t.si]||(sc[t.si]={n:0,s:0}); s.n++; s.s+=t.R; }
      let best=-1,bv=0; for(const k in sc){ const s=sc[k]; if(s.n<30) continue; const v=s.s/s.n; if(v>bv){bv=v;best=+k;} } if(best<0) continue;   // stand aside if nothing positive
      picks[best]=(picks[best]||0)+1;
      for(const t of idx.get(ym)||[]){ if(t.si!==best) continue; if(mode==='with+straight'&&t.ctx!==2) continue; oos.push(t); } }
    const byE=[0,1,2].map(e=>st(oos.filter(t=>era(t.d)===e).map(t=>t.R))), tot=st(oos.map(t=>t.R)), pts=oos.reduce((s,t)=>s+t.pts,0)/Math.max(1,oos.length);
    const top=Object.entries(picks).sort((a,b)=>b[1]-a[1]).slice(0,2).map(([k,v])=>{const p=SETS[k]; return `pole ${p.N}≥${p.m} flag ${p.F}≤${p.t} ${p.g}R (${v} mo)`;}).join('; ');
    console.log(`  ${tf}m ${mode.padEnd(14)} OOS ${tot.n} trades net ${tot.m>=0?'+':''}${tot.m.toFixed(3)} R (t ${tot.t.toFixed(2)}), ${pts.toFixed(2)} pts | eras ${byE.map((x,e)=>`${EN[e]} ${x.m>=0?'+':''}${x.m.toFixed(3)} (${x.n})`).join(' · ')} | most picked: ${top}`); }
})();
