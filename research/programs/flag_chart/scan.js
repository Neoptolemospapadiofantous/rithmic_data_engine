const fs=require('fs'); const src=fs.readFileSync(process.argv[2],'utf8'); const D=process.argv[3];
const grab=n=>{const i=src.indexOf(n); let j=i,d=0; for(;j<src.length;j++){ if(src[j]=='{')d++; if(src[j]=='}'){d--; if(d==0)break;} } return src.slice(i,j+1);};
eval(grab('function detect(')); eval(grab('function agg('));
const tday=min=>{const s=min*60+6*3600; const d=new Date(s*1000); return d.getUTCFullYear()*10000+(d.getUTCMonth()+1)*100+d.getUTCDate();};
const dayMin=min=>((min%1440)-1080+1440)%1440; const sessOf=min=>{const m=dayMin(min); return m<540?0:m<930?1:m<1320?2:-1;};
const atr=JSON.parse(fs.readFileSync(D+'/atr.json')); const keys=Object.keys(atr).map(Number).sort((a,b)=>a-b);
const atrOf=d=>{ if(atr[d]) return atr[d]; let b=null; for(const k of keys) if(k<=d) b=k; return b?atr[b]:300; };
const months=JSON.parse(fs.readFileSync(D+'/months.json'));
const byDay=new Map(); for(const m of months){ for(const line of fs.readFileSync(D+'/'+m+'.csv','utf8').trim().split('\n')){ const f=line.split(',').map(Number); const d=tday(f[0]); if(!byDay.has(d)) byDay.set(d,[]); byDay.get(d).push([f[0],f[1]/4,f[2]/4,f[3]/4,f[4]/4]); } }
const SN=['Asia','London','NY'];
for (const tf of [1,5]) { const p={N:tf==1?15:6,mult:0.3,F:tf==1?5:3,tight:0.4}; const G={};
  const add=(g,f)=>{const x=G[g]||(G[g]={n:0,t:0,s:0,R:0,net:0,pts:0}); x.n++; if(f.res==='target')x.t++; if(f.res==='stopped')x.s++; x.R+=f.R; x.net+=f.net; x.pts+=f.R*f.risk-1;};
  for (const [d,rows] of byDay) { const w=new Date(Date.UTC(Math.floor(d/10000),Math.floor(d/100)%100-1,d%100)).getUTCDay(); if(w==0||w==6) continue;
    for (const se of [0,1,2]) { const b=agg(rows,tf).filter(r=>sessOf(r[0])===se); if(b.length<p.N+p.F+5) continue;
      for (const f of detect(b,atrOf(d),p)) { const ty=f.dir>0?'bull':'bear'; add('ALL '+ty,f); add(SN[se]+' '+ty,f); add(SN[se]+' all',f); add('year '+String(d).slice(0,4)+' '+ty,f); } } }
  console.log(`\n=== ${tf}-minute chart (pole ${p.N} bars >= ${p.mult} ATR, flag ${p.F} bars <= ${p.tight} x pole; target = measured move, stop beyond the flag)`);
  console.log('group                 flags  /yr  target-first stopped  gross R  net R(1pt)  net pts/trade');
  for (const g of Object.keys(G).sort()) { const x=G[g]; console.log(`${g.padEnd(20)} ${String(x.n).padStart(6)} ${(x.n/3.0).toFixed(0).padStart(4)}   ${(100*x.t/x.n).toFixed(1).padStart(6)}%   ${(100*x.s/x.n).toFixed(1).padStart(6)}%  ${(x.R/x.n>=0?'+':'')+(x.R/x.n).toFixed(3)}   ${(x.net/x.n>=0?'+':'')+(x.net/x.n).toFixed(3)}     ${(x.pts/x.n>=0?'+':'')+(x.pts/x.n).toFixed(2)}`); } }
