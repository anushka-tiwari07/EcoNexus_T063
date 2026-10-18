// Company profile: who the company is, how many credits it holds, how many it needs.
// "Needed" uses the same placeholder rule as the Compliance simulator:
//   benchmark = target intensity x production;  needed = max(0, emissions - benchmark)   (1 credit = 1 tCO2e)
// WHERE THE DATA GOES
//   Company identity (name, sector, state, GSTIN) -> backend, PostgreSQL table `organizations` (PATCH /api/v1/organizations/{id}).
//   Credits held / target / production / emissions / price -> THIS browser only (localStorage, per signed-in user),
//   because no backend table or endpoint exists for them yet (planned with the carbon engine / compliance phases).
import { orgs } from './api.js';

const $ = (id) => document.getElementById(id);
const num = (id) => { const v = $(id).value; return v === '' ? NaN : +v; };
const fmt = (x, d = 2) => Number.isFinite(x) ? x.toLocaleString('en-IN', { maximumFractionDigits: d }) : 'n/a';
const ORG_FIELDS = { pname: 'name', psector: 'sector', pstate: 'state', pgstin: 'gstin' };  // input id -> organizations column
const LOCAL_FIELDS = ['pheld', 'ptgt', 'pprod', 'pem', 'pprice'];
const FIELDS = [...Object.keys(ORG_FIELDS), ...LOCAL_FIELDS];
let orgId = null;
let storeKey = 'eco-nexus:profile:guest';

const read = () => { try { return JSON.parse(localStorage.getItem(storeKey)) || {}; } catch { return {}; } };
const write = () => {
  try { localStorage.setItem(storeKey, JSON.stringify(Object.fromEntries(LOCAL_FIELDS.map((i) => [i, $(i).value])))); return true; }
  catch { return false; }
};

function render() {
  const held = num('pheld'), T = num('ptgt'), Q = num('pprod'), E = num('pem'), price = num('pprice');
  const out = $('pout');
  if (![held, T, Q, E].every(Number.isFinite) || held < 0 || T < 0 || E < 0 || Q <= 0) {
    out.innerHTML = '<div class="warn">Enter credits held, target intensity, production (&gt; 0) and emissions.</div>';
    return;
  }
  const bench = T * Q;
  const need = Math.max(0, E - bench);
  const gap = Math.max(0, need - held);
  const spare = Math.max(0, held - need);
  const cover = need > 0 ? Math.min(100, (held / need) * 100) : 100;
  const status = gap > 0 ? `Short by ${fmt(gap)} credits` : need === 0 ? 'No credits needed on these inputs' : 'Fully covered';
  const cost = gap > 0 && Number.isFinite(price) ? `<tr><td>Est. cost to close the gap</td><td>₹ ${fmt(gap * price, 0)}</td></tr>` : '';
  out.innerHTML = `
    <div class="grid grid-cols-2 gap-2">
      <div><div class="mut text-xs">Credits held</div><div class="kpi">${fmt(held)}</div></div>
      <div><div class="mut text-xs">Credits needed</div><div class="kpi">${fmt(need)}</div></div>
      <div><div class="mut text-xs">Still to buy</div><div class="kpi">${fmt(gap)}</div></div>
      <div><div class="mut text-xs">Spare credits</div><div class="kpi">${fmt(spare)}</div></div>
    </div>
    <p class="mt-3 font-semibold">${status}</p>
    <label for="pcov" class="mt-2">Coverage: ${fmt(cover, 0)}%</label>
    <progress id="pcov" class="w-full" max="100" value="${cover}"></progress>
    <div class="tw mt-2"><table class="w-full">
      <tr><td>Actual intensity</td><td>${fmt(E / Q, 4)} tCO2e/t</td></tr>
      <tr><td>Benchmark emissions</td><td>${fmt(bench)} tCO2e</td></tr>
      ${cost}
    </table></div>
    <details class="mt-3"><summary class="font-semibold text-sm">Formulas with your numbers</summary>
      <div class="fx mt-1">Benchmark = Target × Production = ${fmt(T, 4)} × ${fmt(Q)} = ${fmt(bench)} tCO2e
Needed = max(0, Emissions − Benchmark) = max(0, ${fmt(E)} − ${fmt(bench)}) = ${fmt(need)}
Still to buy = max(0, Needed − Held) = max(0, ${fmt(need)} − ${fmt(held)}) = ${fmt(gap)}</div>
    </details>
    <p class="mut text-xs mt-2">Placeholder rule; credits here are simulated, not registry-issued CCCs.</p>`;
}

function fill(values) { FIELDS.forEach((i) => { if (values[i] !== undefined) $(i).value = values[i]; }); }

$('pf').onsubmit = async (e) => {
  e.preventDefault();
  const local = write();
  render();
  if (!orgId) { $('pmsg').textContent = 'Sign in to save company details to the server. Numbers were kept in this browser.'; return; }
  const patch = {};
  for (const [id, col] of Object.entries(ORG_FIELDS)) {
    const v = $(id).value.trim();
    if (v !== '') patch[col] = col === 'gstin' ? v.toUpperCase() : v;
  }
  try {
    await orgs.update(orgId, patch);
    $('pmsg').textContent = 'Company details saved to the server' + (local ? '; numbers saved in this browser.' : '.');
  } catch (err) {
    const d = Array.isArray(err.details) ? ' ' + err.details.map((x) => `${x.field}: ${x.message}`).join('; ') : '';
    $('pmsg').textContent = (err.status === 403 ? 'Only a company admin can change company details. ' : '') + err.message + d;
  }
};
$('ppull').onclick = () => {  // copy the numbers from the Compliance simulator
  fill({ ptgt: $('tgt').value, pprod: $('cprod').value, pem: $('cem').value, pprice: $('cpr').value });
  render();
};
FIELDS.forEach((i) => $(i).addEventListener('input', render));

fill(read());
render();

// main.js announces the session (login, register, page load, logout) with the Phase 3 session payload.
document.addEventListener('eco:session', async (ev) => {
  const sess = ev.detail;
  if (!sess) { orgId = null; storeKey = 'eco-nexus:profile:guest'; $('pwho').textContent = 'Not signed in: numbers are kept on this browser only.'; return; }
  const { user, memberships } = sess;
  storeKey = 'eco-nexus:profile:' + user.id;
  orgId = memberships.length ? memberships[0].organization_id : null;
  $('pwho').textContent = `Signed in as ${user.display_name} (${user.email})` + (memberships.length ? ` as ${memberships[0].role.replace('_', ' ')}` : '');
  fill(read());
  if (orgId) {
    try {
      const o = await orgs.get(orgId);
      fill({ pname: o.name || '', psector: o.sector || '', pstate: o.state || '', pgstin: o.gstin || '' });
    } catch (err) { $('pmsg').textContent = err.message; }
  }
  render();
});
