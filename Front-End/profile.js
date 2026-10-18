// Company profile: who the company is, how many credits it holds, how many it needs.
// "Needed" uses the same placeholder rule as the Compliance simulator:
//   benchmark = target intensity x production;  needed = max(0, emissions - benchmark)   (1 credit = 1 tCO2e)
// Details are saved in THIS browser only (localStorage, per signed-in user). No backend endpoint exists yet.
import { auth } from './api.js';

const $ = (id) => document.getElementById(id);
const num = (id) => { const v = $(id).value; return v === '' ? NaN : +v; };
const fmt = (x, d = 2) => Number.isFinite(x) ? x.toLocaleString('en-IN', { maximumFractionDigits: d }) : 'n/a';
const FIELDS = ['pname', 'psector', 'pstate', 'pgstin', 'pheld', 'ptgt', 'pprod', 'pem', 'pprice'];
let storeKey = 'eco-nexus:profile:guest';

const read = () => { try { return JSON.parse(localStorage.getItem(storeKey)) || {}; } catch { return {}; } };
const write = () => {
  try { localStorage.setItem(storeKey, JSON.stringify(Object.fromEntries(FIELDS.map((i) => [i, $(i).value])))); return true; }
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

$('pf').onsubmit = (e) => {
  e.preventDefault();
  $('pmsg').textContent = write() ? 'Saved in this browser.' : 'Could not save (browser storage unavailable).';
  render();
};
$('ppull').onclick = () => {  // copy the numbers from the Compliance simulator
  fill({ ptgt: $('tgt').value, pprod: $('cprod').value, pem: $('cem').value, pprice: $('cpr').value });
  render();
};
FIELDS.forEach((i) => $(i).addEventListener('input', render));

fill(read());
render();
auth.me().then(({ user }) => {
  storeKey = 'eco-nexus:profile:' + user.id;
  $('pwho').textContent = `Signed in as ${user.display_name} (${user.email})`;
  fill(read());
  render();
}).catch(() => { $('pwho').textContent = 'Not signed in: details are kept on this browser only.'; });
