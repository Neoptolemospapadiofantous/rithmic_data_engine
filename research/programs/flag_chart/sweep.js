// Sweep flag settings on 3 years of NQ: pole bars N, pole size (× ATR), flag bars F, flag tightness, target (1 / 1.5 / 2 R or measured move).
// Score = net points per trade after 1 pt, profit factor, and the two halves (2023-10 → 2024-12, 2025-01 → 2026-10) separately.
const fs=require('fs'); const D=process.argv[2]; const TF=+process.argv[3]; const SESS=process.argv[4];   // '2' NY, 'all'
const tday=min=>{const s=min*60+6*3600; const d=new Date(s*1000); return d.getUTCFullYear()*10000+(d.getUTCMonth()+1)*100+d.getUTCDate();};
const dayMin=min=>((min%1440)-1080+1440)%1440; const sessOf=min=>{const m=dayMin(min); return m<540?0:m<930?1:m<1320?2:-1;};
const atr=JSON.parse(fs.readFileSync(D+'/atr.json')); const keys=Object.keys(atr).map(Number).sort((a,b)=>a-b); const atrOf=d=>{ if(atr[d]) return atr[d]; let b=null; for(const k of keys) if(k<=d) b=k; return b?atr[b]:300; };
const byDay=new Map(); for(const m of JSON.parse(fs.readFileSync(D+'/months.json'))) for(const line of fs.readFileSync(D+'/'+m+'.csv','utf8').trim().split('\n')){ const f=line.split(',').map(Number); const d=tday(f[0]); if(!byDay.has(d)) byDay.set(d,[]); byDay.get(d).push([f[0],f[1]/4,f[2]/4,f[3]/4,f[4]/4]); }
function agg(b,k){ if(k===1) return b; const out=[]; let cur=null; for(const r of b){ const bk=Math.floor(r[0]/k)*k; if(!cur||cur[0]!==bk){cur=[bk,r[1],r[2],r[3],r[4]]; out.push(cur);} else {cur[2]=Math.max(cur[2],r[2]); cur[3]=Math.min(cur[3],r[3]); cur[4]=r[4];} } return out; }
const days=[]; for (const [d,rows] of byDay){ const w=new Date(Date.UTC(Math.floor(d/10000),Math.floor(d/100)%100-1,d%100)).getUTCDay(); if(w==0||w==6) continue;
  const segs=SESS==='all'?[0,1,2]:[+SESS]; for(const se of segs){ const b=agg(rows,TF).filter(r=>sessOf(r[0])===se); if(b.length>40) days.push({d,b,A:atrOf(d)}); } }
const Ns=TF===1?[10,15,20,30]:[3,4,6,8], Ms=[0.1,0.15,0.2,0.3], Fs=TF===1?[3,5,8]:[2,3,5], Ts=[0.3,0.4,0.5,0.6], TG=[1,1.5,2,0];
const res=[];
for(const N of Ns) for(const mult of Ms) for(const F of Fs) for(const tight of Ts) {
  const acc={}; for(const tg of TG) for(const dir of ['bull','bear','both']) acc[tg+dir]={n:0,w:0,gp:0,gl:0,h:[0,0],hn:[0,0]};
  for(const {d,b,A} of days){ let skip=-1; const half=d<20250101?0:1;
    for(let k=N+F+1;k<b.length;k++){ if(k<=skip) continue; const pe=k-1-F, ps=pe-N, pole=b[pe][4]-b[ps][4]; if(Math.abs(pole)<mult*A) continue;
      let path=0; for(let j=ps+1;j<=pe;j++) path+=Math.abs(b[j][4]-b[j-1][4]); if(!path||Math.abs(pole)/path<0.5) continue;
      let fh=-1e18,fl=1e18; for(let j=pe+1;j<k;j++){fh=Math.max(fh,b[j][2]); fl=Math.min(fl,b[j][3]);} if(fh-fl>tight*Math.abs(pole)) continue;
      const dir=pole>0?1:-1; if(dir>0?b[k][4]<=fh:b[k][4]>=fl) continue;
      const e=b[k][4], st=dir>0?fl-0.25:fh+0.25, risk=dir*(e-st); if(risk<=0.25) continue; skip=k+F;
      for(const tg of TG){ const tgt=e+dir*(tg?tg*risk:Math.abs(pole)); let out=dir*(b[b.length-1][4]-e);
        for(let j=k+1;j<b.length;j++){ const hs=dir>0?b[j][3]<=st:b[j][2]>=st, ht=dir>0?b[j][2]>=tgt:b[j][3]<=tgt; if(hs){out=-risk;break;} if(ht){out=dir*(tgt-e);break;} }
        const net=out-1; for(const key of [tg+(dir>0?'bull':'bear'), tg+'both']){ const a=acc[key]; a.n++; if(net>0){a.w++; a.gp+=net;} else a.gl-=net; a.h[half]+=net; a.hn[half]++; } } } }
  for(const tg of TG) for(const dir of ['bull','bear','both']){ const a=acc[tg+dir]; if(a.n<60) continue;
    res.push({N,mult,F,tight,tg,dir,n:a.n,win:a.w/a.n,pf:a.gl?a.gp/a.gl:9,pts:(a.gp-a.gl)/a.n,h0:a.hn[0]?a.h[0]/a.hn[0]:NaN,h1:a.hn[1]?a.h[1]/a.hn[1]:NaN}); } }
fs.writeFileSync(process.argv[5], JSON.stringify(res));
const both=res.filter(r=>r.h0>0&&r.h1>0);
console.log(`TF ${TF}m, session ${SESS}: ${res.length} cells (settings × target × side, n >= 60); positive in BOTH halves: ${both.length}`);
for (const side of ['bull','bear','both']) for (const tg of TG) { const c=res.filter(r=>r.dir===side&&r.tg===tg); const pos=c.filter(r=>r.pts>0).length, bh=c.filter(r=>r.h0>0&&r.h1>0).length;
  const avg=c.reduce((s,r)=>s+r.pts,0)/c.length; console.log(`  ${side.padEnd(5)} target ${tg?tg+'R':'measured'}`.padEnd(30)+` cells ${String(c.length).padStart(3)} | positive ${(100*pos/c.length).toFixed(0).padStart(3)}% | both halves ${(100*bh/c.length).toFixed(0).padStart(3)}% | avg net pts/trade ${avg>=0?'+':''}${avg.toFixed(2)}`); }
