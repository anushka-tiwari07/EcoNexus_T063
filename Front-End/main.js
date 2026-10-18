import {auth} from './api.js';
import './profile.js';

const $=id=>document.getElementById(id), n=id=>{const v=$(id).value;return v===''?NaN:+v};
const f=(x,d=2)=>Number.isFinite(x)?x.toLocaleString('en-IN',{maximumFractionDigits:d}):'n/a';
const pages=[['home','Home'],['dashboard','Dashboard'],['calculator','Calculator'],['compliance','Compliance'],['consultancy','Consultancy'],['market','Marketplace'],['invest','Investment'],['profile','Profile'],['reports','Reports']];
$('nav').innerHTML=pages.map(([i,t])=>`<a href="#${i}">${t}</a>`).join('');
function route(){const id=pages.some(p=>p[0]===location.hash.slice(1))?location.hash.slice(1):'home';
 pages.forEach(([i])=>{$(i).hidden=i!==id});
 document.querySelectorAll('#nav a').forEach(a=>a.toggleAttribute('aria-current',a.hash==='#'+id)||a.removeAttribute('aria-current'));
 document.querySelectorAll('#nav a').forEach(a=>{if(a.hash==='#'+id)a.setAttribute('aria-current','page')});
 if(id==='dashboard')drawDash();}
addEventListener('hashchange',route);
$('theme').onclick=()=>{const r=document.documentElement,d=getComputedStyle(r).getPropertyValue('--bg').trim()==='#0f1713';r.dataset.theme=d?'light':'dark'};

/* Auth (talks to backend /api/v1/auth/*) */
const FX=(t,L)=>`<details open class="mt-3"><summary class="font-semibold text-sm">${t}</summary><div class="fx mt-1">${L.join('\n')}</div></details>`;
const showUser=u=>{$('who').textContent=u?`Signed in as ${u.display_name} (${u.email})`:'Not signed in';$('signout').hidden=!u;$('authf').hidden=!!u};
const amsg=(t)=>{$('loginmsg').textContent=t};
async function doAuth(kind){const b={email:$('em').value.trim(),password:$('pw').value};
 if(kind==='register'){b.display_name=$('nm').value.trim();b.organization_name=$('org').value.trim();b.sector=$('sec').value.trim()}
 if(!/^\S+@\S+\.\S+$/.test(b.email)||b.password.length<12||(kind==='register'&&(!b.display_name||b.organization_name.length<2||b.sector.length<2))){amsg('Enter a valid email and a password of 12+ characters'+(kind==='register'?', plus your name, company name and sector.':'.'));return}
 try{const r=await auth[kind](b);$('pw').value='';showUser(r.user);amsg('')}catch(e){amsg(e.message)}}
$('authf').onsubmit=e=>{e.preventDefault();doAuth('login')};
$('signup').onclick=()=>doAuth('register');
$('signout').onclick=async()=>{try{await auth.logout()}catch{}showUser(null)};
auth.me().then(r=>showUser(r.user)).catch(()=>showUser(null));

/* Dashboard (demo) */
let tchart;
function drawDash(){
 $('kpis').innerHTML=[['Total tCO2e','4,310'],['Scope 1','3,680'],['Scope 2','630'],['Output (t)','12,000'],['Intensity','0.359']].map(([a,b])=>`<div class="card"><div class="mut text-xs">${a}</div><div class="kpi">${b}</div></div>`).join('');
 if(tchart)return;
 tchart=new Chart($('trend'),{type:'line',data:{labels:['Jan','Feb','Mar','Apr','May','Jun'],datasets:[{label:'tCO2e (demo)',data:[4100,3950,4310,3800,4200,4050],borderColor:'#2e9e6b',tension:.3}]},options:{maintainAspectRatio:false}});}

/* Calculator */
let last=null,pie;
$('calcf').onsubmit=e=>{e.preventDefault();
 const src=[['Coal','coal','f_coal','t'],['Diesel','dsl','f_dsl','L'],['Natural gas','gas','f_gas','m³'],['Electricity (Scope 2, location-based)','elc','f_elc','kWh']];
 const rows=[],flags=[];let s1=0,s2=0;
 for(const [name,a,fe,u] of src){const A=n(a),F=n(fe);
  if(!Number.isFinite(A)||A<0||!Number.isFinite(F)||F<0){flags.push(`${name}: missing or invalid, excluded and flagged (not zero)`);rows.push([name,NaN,u,F,NaN]);continue}
  const E=A*F/1000;rows.push([name,A,u,F,E]);a==='elc'?s2+=E:s1+=E;}
 const P=n('prod'),tot=s1+s2,inten=P>0?tot/P:NaN;
 if(!(P>0))flags.push('Production must be positive; intensity not computed');
 last={period:$('per').value,fac:$('fac').value,rows,s1,s2,tot,P,inten,flags};
 $('calcout').innerHTML=`<div class="grid grid-cols-2 gap-2"><div><div class="mut text-xs">Scope 1</div><div class="kpi">${f(s1)}</div></div><div><div class="mut text-xs">Scope 2</div><div class="kpi">${f(s2)}</div></div><div><div class="mut text-xs">Total tCO2e</div><div class="kpi">${f(tot)}</div></div><div><div class="mut text-xs">Intensity tCO2e/t</div><div class="kpi">${f(inten,4)}</div></div></div>
 <div class="tw mt-3"><table class="w-full"><tr><th>Source</th><th>Activity</th><th>EF</th><th>tCO2e</th></tr>${rows.map(r=>`<tr><td>${r[0]}</td><td>${f(r[1])} ${r[2]}</td><td>${f(r[3],4)}</td><td>${f(r[4])}</td></tr>`).join('')}</table></div>
 ${FX('Formulas applied',['Emissions per source: E = A × EF ÷ 1000   (A = activity, EF = kgCO2e per unit)',...rows.map(r=>Number.isFinite(r[4])?`  ${r[0]}: ${f(r[1])} ${r[2]} × ${f(r[3],4)} ÷ 1000 = ${f(r[4])} tCO2e`:`  ${r[0]}: input missing → excluded and flagged`),`Scope 1 = sum of fuel sources = ${f(s1)} tCO2e`,`Scope 2 = electricity kWh × grid EF ÷ 1000 = ${f(s2)} tCO2e`,`Total = Scope 1 + Scope 2 = ${f(s1)} + ${f(s2)} = ${f(tot)} tCO2e`,`Intensity = Total ÷ Production = ${f(tot)} ÷ ${f(P)} = ${f(inten,4)} tCO2e/t`,'Note: reporting metric, not the regulated CCTS total. Factors are user-entered placeholders.'])}
 ${flags.length?`<div class="warn mt-2"><b>Data-quality flags</b><ul class="list-disc pl-5">${flags.map(x=>`<li>${x}</li>`).join('')}</ul></div>`:''}
 <div style="height:200px" class="mt-3"><canvas id="pie" role="img" aria-label="Scope split"></canvas></div>`;
 pie=new Chart($('pie'),{type:'doughnut',data:{labels:['Scope 1','Scope 2'],datasets:[{data:[s1,s2],backgroundColor:['#2e9e6b','#e0a526']}]},options:{maintainAspectRatio:false}});};

/* Compliance */
$('cf').onsubmit=e=>{e.preventDefault();
 const T=n('tgt'),Q=n('cprod'),E=n('cem'),pr=n('cpr'),fee=n('cfee')||0,ab=n('aba');
 if(![T,Q,E,pr,ab].every(Number.isFinite)||Q<=0){$('cout').innerHTML='<div class="warn">Enter valid positive values.</div>';return}
 const B=T*Q,sh=Math.max(0,E-B),su=Math.max(0,B-E),buy=sh*pr+(sh>0?fee:0),abt=sh*ab;
 $('cout').innerHTML=`<table class="w-full"><tr><td>Actual intensity</td><td>${f(E/Q,4)} tCO2e/t</td></tr><tr><td>Target intensity</td><td>${f(T,4)}</td></tr><tr><td>Benchmark emissions</td><td>${f(B)} tCO2e</td></tr><tr><td>Preliminary shortfall</td><td>${f(sh)}</td></tr><tr><td>Preliminary surplus</td><td>${f(su)}</td></tr><tr><td>Credit purchase cost (est.)</td><td>₹ ${f(buy,0)}</td></tr><tr><td>Abatement cost (est.)</td><td>₹ ${f(abt,0)}</td></tr><tr><td><b>Cheaper (on these inputs)</b></td><td><b>${sh===0?'No shortfall':buy<=abt?'Buy credits':'Abate'}</b></td></tr></table>
 ${FX('Formulas applied',[`Benchmark = Target × Production = ${f(T,4)} × ${f(Q)} = ${f(B)} tCO2e`,`Shortfall = max(0, Emissions − Benchmark) = max(0, ${f(E)} − ${f(B)}) = ${f(sh)}`,`Surplus = max(0, Benchmark − Emissions) = ${f(su)}`,`Purchase cost = Shortfall × Price + Fee = ${f(sh)} × ${f(pr)} + ${f(sh>0?fee:0)} = ₹${f(buy,0)}`,`Abatement cost = Shortfall × Cost per t = ${f(sh)} × ${f(ab)} = ₹${f(abt,0)}`])}<p class="text-xs mut mt-2">Unresolved: official sector methodology, adjustments, obligated-entity status, credit eligibility. Abatement ignores energy savings to avoid double counting.</p>`;};

/* CSV import preview */
$('csv').onchange=async e=>{const t=await e.target.files[0]?.text();if(!t)return;
 const L=t.trim().split(/\r?\n/),H=L[0].split(',').map(s=>s.trim().toLowerCase()),req=['facility','period','source','quantity','unit'];
 const miss=req.filter(r=>!H.includes(r));if(miss.length){$('csvout').innerHTML=`<div class="warn">Missing columns: ${miss.join(', ')}</div>`;return}
 const seen=new Set(),errs=[];let ok=0;
 L.slice(1).forEach((l,i)=>{const c=l.split(','),r=Object.fromEntries(H.map((h,j)=>[h,(c[j]||'').trim()])),k=[r.facility,r.period,r.source].join('|');
  if(!(+r.quantity>=0)||r.quantity==='')errs.push(`Row ${i+2}: invalid quantity`);else if(seen.has(k))errs.push(`Row ${i+2}: duplicate ${k}`);else{seen.add(k);ok++}});
 $('csvout').innerHTML=`<p>${ok} valid row(s), ${errs.length} error(s). Nothing was uploaded.</p>${errs.length?`<ul class="list-disc pl-5">${errs.map(x=>`<li>${x}</li>`).join('')}</ul>`:''}`;};

/* Simulated order book: price-time priority, in-memory */
let seq=0,book=[],trades=[],bal={cash:5000000,cr:2000};
const add=(o,s,q,p)=>book.push({id:++seq,o,s,q,p});
add('Sim Co X','sell',300,1020);add('Sim Co Y','sell',200,1050);add('Sim Co Z','buy',250,980);add('Sim Co W','buy',150,950);
const reserved=()=>({cash:book.filter(b=>b.o==='You'&&b.s==='buy').reduce((a,b)=>a+b.q*b.p,0),cr:book.filter(b=>b.o==='You'&&b.s==='sell').reduce((a,b)=>a+b.q,0)});
function match(o){
 const opp=b=>b.s!==o.s&&(o.s==='buy'?b.p<=o.p:b.p>=o.p);
 while(o.q>0){const c=book.filter(b=>b.id!==o.id&&opp(b)).sort((a,b)=>(o.s==='buy'?a.p-b.p:b.p-a.p)||a.id-b.id)[0];if(!c)break;
  const q=Math.min(o.q,c.q);o.q-=q;c.q-=q;trades.unshift({id:'SIM-'+(trades.length+1),side:o.s,q,p:c.p,cp:c.o===o.o?'self':c.o});
  if(o.o==='You'||c.o==='You'){const buyer=o.s==='buy'?o.o:c.o;if(buyer==='You'){bal.cash-=q*c.p;bal.cr+=q}else{bal.cash+=q*c.p;bal.cr-=q}}
  if(c.q===0)book=book.filter(b=>b!==c);}
 if(o.q===0)book=book.filter(b=>b!==o);}
function drawBook(){
 const agg=s=>Object.entries(book.filter(b=>b.s===s).reduce((m,b)=>(m[b.p]=(m[b.p]||0)+b.q,m),{})).map(([p,q])=>[+p,q]).sort((a,b)=>s==='buy'?b[0]-a[0]:a[0]-b[0]);
 const B=agg('buy'),A=agg('sell'),tb=(r)=>`<tr><th>Price</th><th>Qty</th></tr>`+(r.map(x=>`<tr><td>${f(x[0])}</td><td>${f(x[1],0)}</td></tr>`).join('')||'<tr><td colspan=2 class="mut">Empty</td></tr>');
 $('bids').innerHTML=tb(B);$('asks').innerHTML=tb(A);
 $('bbo').textContent=`Best bid ${B[0]?f(B[0][0]):'—'} · Best ask ${A[0]?f(A[0][0]):'—'} (simulated)`;
 const r=reserved();$('bal').textContent=`Sim cash ₹${f(bal.cash,0)} (reserved ${f(r.cash,0)}) · Sim credits ${f(bal.cr,0)} (reserved ${f(r.cr,0)})`;
 $('trades').innerHTML=`<tr><th>Receipt</th><th>Taker side</th><th>Qty</th><th>Price</th><th>Counterparty</th></tr>`+(trades.map(t=>`<tr><td>${t.id}</td><td>${t.side}</td><td>${t.q}</td><td>${f(t.p)}</td><td>${t.cp}</td></tr>`).join('')||'<tr><td colspan=5 class="mut">No trades yet</td></tr>');}
$('of').onsubmit=e=>{e.preventDefault();const s=$('side').value,q=n('oq'),p=n('op'),r=reserved();
 if(!(q>0&&Number.isInteger(q)&&p>0)){$('omsg').textContent='Enter a whole positive quantity and price.';return}
 if(s==='buy'&&q*p>bal.cash-r.cash){$('omsg').textContent='Rejected: insufficient simulated cash (no double spending).';return}
 if(s==='sell'&&q>bal.cr-r.cr){$('omsg').textContent='Rejected: insufficient simulated credits (no overselling).';return}
 const o={id:++seq,o:'You',s,q,p};book.push(o);match(o);$('omsg').textContent='Order processed (simulated).';drawBook();};
drawBook();

/* Investment */
let icf;
const npv=(cf,r)=>cf.reduce((a,c,t)=>a+c/Math.pow(1+r,t),0);
function irr(cf){let lo=-.99,hi=1;if(npv(cf,lo)*npv(cf,hi)>0)return NaN;for(let i=0;i<200;i++){const m=(lo+hi)/2;npv(cf,lo)*npv(cf,m)<=0?hi=m:lo=m}return(lo+hi)/2}
function model(p){const cf=[-p.capex];let gen=[];for(let t=1;t<=p.life;t++){const g=p.kw*8760*p.cuf/100*Math.pow(1-p.deg/100,t-1);gen.push(g);cf.push(g*p.tar-p.opex)}return{cf,gen}}
$('if').onsubmit=e=>{e.preventDefault();
 const p={kw:n('kw'),cuf:n('cuf'),capex:n('capex'),opex:n('opex'),tar:n('tar'),life:Math.round(n('life')),deg:n('deg'),dr:n('dr'),gef:n('gef')};
 if(Object.values(p).some(v=>!Number.isFinite(v)||v<0)||p.life<1||p.capex<=0){$('iout').innerHTML='<div class="warn">Enter valid non-negative values.</div>';return}
 const {cf,gen}=model(p),r=p.dr/100;let cum=0,pay=NaN;const cc=cf.map((c,t)=>{const prev=cum;cum+=c;if(!Number.isFinite(pay)&&cum>=0&&t>0)pay=t-1+(-prev/c);return cum});
 const I=irr(cf);
 $('iout').innerHTML=`<table class="w-full"><tr><td>Year-1 generation</td><td>${f(gen[0],0)} kWh</td></tr><tr><td>Year-1 avoided emissions</td><td>${f(gen[0]*p.gef/1000)} tCO2e</td></tr><tr><td>Lifetime avoided emissions</td><td>${f(gen.reduce((a,b)=>a+b,0)*p.gef/1000,0)} tCO2e</td></tr><tr><td>Payback</td><td>${Number.isFinite(pay)?f(pay,1)+' yrs':'not within lifetime'}</td></tr><tr><td>NPV @ ${p.dr}%</td><td>₹ ${f(npv(cf,r),0)}</td></tr><tr><td>IRR</td><td>${Number.isFinite(I)?f(I*100,2)+'%':'n/a (no sign change)'}</td></tr></table>${FX('Formulas applied',[`Generation_t = kW × 8760 × CUF × (1 − deg)^(t−1)`,`  Year 1 = ${f(p.kw,0)} × 8760 × ${p.cuf}% = ${f(gen[0],0)} kWh`,`Cash flow_t = Generation_t × Avoided cost − Opex   (t=0: −Capex)`,`  Year 1 = ${f(gen[0],0)} × ${p.tar} − ${f(p.opex,0)} = ₹${f(cf[1],0)}`,`NPV = Σ CF_t ÷ (1 + r)^t, r = ${p.dr}% → ₹${f(npv(cf,r),0)}`,`IRR = r where NPV(r) = 0 (bisection) → ${Number.isFinite(I)?f(I*100,2)+'%':'n/a'}`,`Payback = first year cumulative CF ≥ 0 (linear interpolation)`,`Avoided emissions = Generation × Grid EF ÷ 1000 = ${f(gen[0]*p.gef/1000)} tCO2e (year 1)`])}<p class="text-xs mut mt-2">Avoided cost treated as pre-tax savings; no incentives, tax or credit revenue modeled.</p>`;
 if(icf)icf.destroy();
 icf=new Chart($('icf'),{type:'line',data:{labels:cc.map((_,i)=>i),datasets:[{label:'Cumulative ₹',data:cc,borderColor:'#2e9e6b'}]},options:{maintainAspectRatio:false}});
 const vars=[-20,-10,0,10,20];
 $('sens').innerHTML=`<tr><th>Δ tariff \\ Δ capex</th>${vars.map(v=>`<th>${v>0?'+':''}${v}%</th>`).join('')}</tr>`+vars.map(a=>`<tr><th>${a>0?'+':''}${a}%</th>${vars.map(b=>{const m=model({...p,tar:p.tar*(1+a/100),capex:p.capex*(1+b/100)});return`<td>${f(npv(m.cf,r),0)}</td>`}).join('')}</tr>`).join('');};

/* Reports */
$('csvx').onclick=()=>{if(!last){$('rmsg').textContent='Run the emissions calculator first.';return}
 const q=v=>Number.isFinite(v)?v:'MISSING';
 const csv=['facility,period,source,activity,unit,ef_kgco2e_per_unit,tco2e',...last.rows.map(r=>[last.fac,last.period,r[0].replace(/,/g,' '),q(r[1]),r[2],q(r[3]),q(r[4])].join(',')),`TOTAL,${last.period},,,,,${last.tot}`].join('\n');
 const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([csv],{type:'text/csv'}));a.download='emissions-demo.csv';a.click();$('rmsg').textContent='CSV downloaded (demo data, unverified factors).'};
$('prt').onclick=()=>print();
route();
