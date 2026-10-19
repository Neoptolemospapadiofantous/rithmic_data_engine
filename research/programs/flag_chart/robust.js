const fs=require('fs'); const res=JSON.parse(fs.readFileSync(process.argv[2])); const side=process.argv[3];
const key=r=>[r.N,r.mult,r.F,r.tight,r.tg,r.dir].join('|'); const M=new Map(res.map(r=>[key(r),r]));
const dims={N:[...new Set(res.map(r=>r.N))].sort((a,b)=>a-b),mult:[0.1,0.15,0.2,0.3],F:[...new Set(res.map(r=>r.F))].sort((a,b)=>a-b),tight:[0.3,0.4,0.5,0.6]};
const out=[]; for(const r of res){ if(r.dir!==side||r.n<100) continue; let nb=[]; for(const k of Object.keys(dims)){ const v=dims[k], i=v.indexOf(r[k]); for(const j of [i-1,i+1]) if(j>=0&&j<v.length){ const q={...r,[k]:v[j]}; const x=M.get(key(q)); if(x) nb.push(x); } }
  const nbpos=nb.filter(x=>x.pts>0).length/Math.max(1,nb.length), nbavg=nb.reduce((s,x)=>s+x.pts,0)/Math.max(1,nb.length);
  out.push({...r,nbpos,nbavg,nnb:nb.length}); }
const robust=out.filter(r=>r.h0>0&&r.h1>0&&r.nbpos>=0.75).sort((a,b)=>b.nbavg-a.nbavg);
console.log(`${process.argv[2].split('/').pop()} ${side}: ${out.length} cells with n>=100; robust (both halves + >=75% of neighbours positive): ${robust.length}`);
for(const r of robust.slice(0,8)) console.log(`  pole ${r.N} bars >= ${r.mult} ATR, flag ${r.F} bars <= ${r.tight}x pole, target ${r.tg?r.tg+'R':'measured'} | n ${r.n} (${(r.n/3).toFixed(0)}/yr) win ${(100*r.win).toFixed(0)}% PF ${r.pf.toFixed(2)} net ${r.pts>=0?'+':''}${r.pts.toFixed(2)} pts | halves ${r.h0.toFixed(2)} / ${r.h1.toFixed(2)} | neighbours ${(100*r.nbpos).toFixed(0)}% positive, avg ${r.nbavg.toFixed(2)}`);
