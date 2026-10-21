#include "sx1255/web/WebDashboard.hpp"

namespace sx1255::web {

const std::string& dashboardHtml()
{
    static const std::string html = R"HTML(<!DOCTYPE html>
<html lang="cs">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>sx1255-multimode</title>
<style>
  :root { color-scheme: dark; }
  body { margin:0; background:#0d1117; color:#c9d1d9; font:14px/1.45 system-ui,sans-serif; }
  header { padding:10px 16px; border-bottom:1px solid #21262d; display:flex; flex-wrap:wrap; gap:18px; align-items:center; }
  .stat { color:#58a6ff; }
  main { padding:12px; display:grid; gap:12px; grid-template-columns:1fr; }
  .panel { background:#161b22; border:1px solid #21262d; border-radius:8px; padding:10px; }
  canvas { width:100%; height:220px; display:block; background:#000; }
  h2 { margin:0 0 8px; font-size:14px; font-weight:600; color:#8b949e; text-transform:uppercase; letter-spacing:.04em; }
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(150px,1fr)); gap:8px; }
  .cell { background:#0d1117; border:1px solid #21262d; border-radius:6px; padding:8px 10px; }
  .cell b { display:block; font-size:20px; color:#58a6ff; }
  .cell span { color:#8b949e; font-size:12px; }
  #plugins { display:flex; flex-wrap:wrap; gap:8px; }
  .plugin { border:1px solid #21262d; border-radius:6px; padding:8px 10px; min-width:160px; }
  .plugin.active { border-color:#2ea043; }
  .plugin small { color:#8b949e; }
  .diag-table { width:100%; border-collapse:collapse; font:12px/1.35 ui-monospace,monospace; }
  .diag-table th, .diag-table td { padding:4px 6px; border-bottom:1px solid #21262d; text-align:right; }
  .diag-table th:first-child, .diag-table td:first-child { text-align:left; }
  .diag-table th { color:#8b949e; font-weight:600; position:sticky; top:0; background:#161b22; }
  .diag-wrap { max-height:320px; overflow:auto; border:1px solid #21262d; border-radius:6px; }
  .ok { color:#3fb950; } .bad { color:#f85149; }
  button { background:#21262d; color:#c9d1d9; border:1px solid #30363d; border-radius:6px; padding:5px 10px; cursor:pointer; }
  button:hover { border-color:#58a6ff; }
  .ber-hero { display:grid; grid-template-columns:minmax(180px,260px) 1fr; gap:18px;
              align-items:center; padding:16px 20px; margin-bottom:12px;
              background:linear-gradient(135deg,#0d1117,#161b22);
              border:2px solid #30363d; border-radius:10px; }
  .ber-value { display:flex; align-items:baseline; gap:6px; line-height:1; }
  .ber-value b { font:700 64px/1 ui-monospace,monospace; letter-spacing:-.04em; }
  .ber-value span { font-size:22px; color:#8b949e; }
  .ber-caption { margin-top:8px; color:#8b949e; font-weight:600;
                 text-transform:uppercase; letter-spacing:.05em; }
  .ber-side { display:grid; gap:8px; }
  .ber-state { font-size:20px; font-weight:700; }
  .ber-average { color:#c9d1d9; }
  .ber-average b { font:700 22px/1 ui-monospace,monospace; }
  .ber-meter { height:14px; border-radius:999px; overflow:hidden; background:#21262d;
               border:1px solid #30363d; }
  .ber-meter div { height:100%; width:0; transition:width .25s ease, background .25s ease; }
  .ber-help { color:#8b949e; font-size:12px; }
  .ber-good { border-color:#238636; }
  .ber-good .ber-value b, .ber-good .ber-state { color:#3fb950; }
  .ber-good .ber-meter div { background:#3fb950; }
  .ber-warn { border-color:#9e6a03; }
  .ber-warn .ber-value b, .ber-warn .ber-state { color:#d29922; }
  .ber-warn .ber-meter div { background:#d29922; }
  .ber-bad { border-color:#da3633; }
  .ber-bad .ber-value b, .ber-bad .ber-state { color:#f85149; }
  .ber-bad .ber-meter div { background:#f85149; }
  .ber-unknown .ber-value b, .ber-unknown .ber-state { color:#8b949e; }
  @media (max-width:620px) {
    .ber-hero { grid-template-columns:1fr; }
    .ber-value b { font-size:52px; }
  }
</style>
</head>
<body>
<header>
  <div>stav <span class="stat" id="state">-</span></div>
  <div>mód <span class="stat" id="mode">-</span></div>
  <div>síť <span class="stat" id="netmode">-</span></div>
  <div>RSSI <span class="stat" id="rssi">-</span> dBm</div>
  <div>uptime <span class="stat" id="uptime">-</span> s</div>
  <div id="iq"></div>
</header>
<main>
  <div class="panel">
    <h2>Spektrum / waterfall</h2>
    <canvas id="spectrum"></canvas>
    <canvas id="waterfall" style="height:180px"></canvas>
  </div>
  <div class="panel">
    <h2>Detekce</h2>
    <div class="grid">
      <div class="cell"><b id="fm">-</b><span>FM detekcí</span></div>
      <div class="cell"><b id="dstar">-</b><span>D-Star detekcí</span></div>
      <div class="cell"><b id="m17">-</b><span>M17 detekcí</span></div>
      <div class="cell"><b id="falsepos">-</b><span>false positives</span></div>
    </div>
  </div>
  <div class="panel">
    <h2>Pluginy</h2>
    <div id="plugins"></div>
  </div>
  <div class="panel">
    <h2>M17 Diagnostics</h2>
    <div id="m17-ber-hero" class="ber-hero ber-unknown">
      <div>
        <div class="ber-value"><b id="m17-ber-big">-</b><span>%</span></div>
        <div class="ber-caption">BER posledního rámce</div>
      </div>
      <div class="ber-side">
        <div id="m17-ber-state" class="ber-state">čekám na rámec</div>
        <div class="ber-average">průměr 100 rámců: <b id="m17-ber-average">-</b> %</div>
        <div class="ber-meter"><div id="m17-ber-bar"></div></div>
        <div class="ber-help">Kanálové chyby před FEC. Nižší hodnota je lepší.</div>
      </div>
    </div>
    <div class="grid">
      <div class="cell"><b id="m17-afc">-</b><span>AFC correction [Hz]</span></div>
      <div class="cell"><b id="m17-distance">-</b><span>sync distance</span></div>
      <div class="cell"><b id="m17-confidence">-</b><span>sync confidence</span></div>
      <div class="cell"><b id="m17-hits">-</b><span>sync hits / misses</span></div>
      <div class="cell"><b id="m17-level">-</b><span>average symbol level</span></div>
      <div class="cell"><b id="m17-rssi">-</b><span>RSSI at sync [dBFS]</span></div>
      <div class="cell"><b id="m17-sync-detected">-</b><span>sync detected</span></div>
      <div class="cell"><b id="m17-lsf">-</b><span>LSF ok / fail</span></div>
      <div class="cell"><b id="m17-crc">-</b><span>CRC ok / fail</span></div>
      <div class="cell"><b id="m17-golay">-</b><span>Golay ok / fail</span></div>
      <div class="cell"><b id="m17-viterbi">-</b><span>Viterbi ok / fail</span></div>
      <div class="cell"><b id="m17-frame">-</b><span>frame ok / fail</span></div>
    </div>
    <div style="height:8px"></div>
    <button id="m17-reset" type="button">Reset M17 statistik</button>
    <div style="height:8px"></div>
    <div class="diag-wrap">
      <table class="diag-table">
        <thead><tr><th>čas</th><th>RSSI</th><th>AFC Hz</th><th>sync dist</th><th>BER %</th><th>typ</th><th>výsledek</th></tr></thead>
        <tbody id="m17-candidates"></tbody>
      </table>
    </div>
  </div>
</main>
<script>
'use strict';
const $ = function(id){ return document.getElementById(id); };
async function getJSON(path){
  try {
    const r = await fetch(path, {cache:'no-store'});
    if (!r.ok) return null;
    return await r.json();
  } catch (e) { return null; }
}
function colorFor(db){
  const v = Math.max(-120, Math.min(-20, db));
  const t = (v + 120) / 100;
  const r = Math.round(255 * Math.min(1, t * 3));
  const g = Math.round(255 * Math.min(1, Math.max(0, (t - 0.15) * 2.4)));
  const b = Math.round(255 * Math.min(1, Math.max(0, 1 - t * 1.4)));
  return 'rgb(' + r + ',' + g + ',' + b + ')';
}
async function drawSpectrum(){
  const s = await getJSON('/api/spectrum');
  if (!s || !s.spectrum) return;
  const c = $('spectrum');
  const dpr = window.devicePixelRatio || 1;
  c.width = c.clientWidth * dpr; c.height = c.clientHeight * dpr;
  const ctx = c.getContext('2d');
  ctx.scale(dpr, dpr);
  const W = c.clientWidth, H = c.clientHeight;
  ctx.fillStyle = '#000'; ctx.fillRect(0,0,W,H);
  const n = s.spectrum.length;
  const lo = -120, hi = -20;
  function y(v){ return H - (Math.max(lo, Math.min(hi, v)) - lo) * H / (hi - lo); }
  ctx.strokeStyle = '#1f6feb'; ctx.lineWidth = 1.4;
  ctx.beginPath();
  for (let i=0;i<n;i++){ const x = i * W / (n-1); const v = s.spectrum[i]; if(i===0) ctx.moveTo(x,y(v)); else ctx.lineTo(x,y(v)); }
  ctx.stroke();
  if (s.peak_hold){
    ctx.strokeStyle = 'rgba(255,255,255,0.25)'; ctx.lineWidth = 1;
    ctx.beginPath();
    for (let i=0;i<n;i++){ const x = i * W / (n-1); const v = s.peak_hold[i]; if(i===0) ctx.moveTo(x,y(v)); else ctx.lineTo(x,y(v)); }
    ctx.stroke();
  }
  ctx.strokeStyle = '#2ea043'; ctx.setLineDash([5,4]);
  ctx.beginPath(); ctx.moveTo(0,y(s.noise_floor_db)); ctx.lineTo(W,y(s.noise_floor_db)); ctx.stroke();
  ctx.setLineDash([]);
}
let wfCanvas = null, wfBins = 0, wfH = 0, wfSeq = 0;
async function drawWaterfall(){
  const w = await getJSON(wfSeq ? '/api/waterfall?since=' + wfSeq : '/api/waterfall');
  if (!w || !w.bins || !w.max_rows) return;
  const cols = w.bins, maxRows = w.max_rows;
  if (!wfCanvas || wfBins !== cols || wfH !== maxRows){
    wfCanvas = document.createElement('canvas');
    wfCanvas.width = cols; wfCanvas.height = maxRows;
    wfBins = cols; wfH = maxRows;
    const g = wfCanvas.getContext('2d');
    g.fillStyle = '#000';
    g.fillRect(0, 0, cols, maxRows);
  }
  const g = wfCanvas.getContext('2d');
  const rows = w.data || [];
  const n = Math.min(rows.length, wfH);
  if (n > 0){
    // Scroll the existing history up, keep newest rows at the bottom.
    g.drawImage(wfCanvas, 0, n, wfBins, wfH - n, 0, 0, wfBins, wfH - n);
    g.fillStyle = '#000';
    g.fillRect(0, wfH - n, wfBins, n);
    for (let i = 0; i < rows.length; i++){
      const y = wfH - rows.length + i;
      if (y < 0 || y >= wfH) continue;
      const row = rows[i];
      const img = g.createImageData(wfBins, 1);
      for (let x = 0; x < wfBins; x++){
        const db = (row && x < row.length) ? row[x] : -120;
        const t = Math.max(0, Math.min(1, (db + 120) / 100));
        img.data[x * 4]     = Math.round(255 * Math.min(1, t * 3));
        img.data[x * 4 + 1] = Math.round(255 * Math.min(1, Math.max(0, (t - 0.15) * 2.4)));
        img.data[x * 4 + 2] = Math.round(255 * Math.min(1, Math.max(0, 1 - t * 1.4)));
        img.data[x * 4 + 3] = 255;
      }
      g.putImageData(img, 0, y);
    }
  }
  if (typeof w.seq === 'number') wfSeq = w.seq;
  const c = $('waterfall');
  const dpr = window.devicePixelRatio || 1;
  const W = c.clientWidth, H = c.clientHeight;
  c.width = W * dpr; c.height = H * dpr;
  const ctx = c.getContext('2d');
  ctx.scale(dpr, dpr);
  ctx.imageSmoothingEnabled = false;
  ctx.drawImage(wfCanvas, 0, 0, wfBins, wfH, 0, 0, W, H);
}
async function refreshStatus(){
  const s = await getJSON('/api/status');
  if (!s) return;
  $('state').textContent = s.state || '-';
  $('mode').textContent = s.mode || '-';
  $('netmode').textContent = s.network_mode || '-';
  $('rssi').textContent = s.rssi;
  $('uptime').textContent = s.uptime;
  $('fm').textContent = s.stats ? s.stats.fm_detected : '-';
  $('dstar').textContent = s.stats ? s.stats.dstar_detected : '-';
  $('m17').textContent = s.stats ? s.stats.m17_detected : '-';
  $('falsepos').textContent = s.stats ? s.stats.false_positives : '-';
}
async function refreshPlugins(){
  const list = await getJSON('/api/plugins');
  if (!list) return;
  const box = $('plugins'); box.innerHTML = '';
  function fmtMetric(key, value){
    if (typeof value === 'number' && key.indexOf('confidence') !== -1)
      return value.toFixed(6);
    return value;
  }
  for (const p of list){
    const div = document.createElement('div');
    div.className = 'plugin' + (p.active ? ' active' : '');
    let t = '<b>' + p.name + '</b>';
    if (p.telemetry){
      t += '<br><small>';
      const k = Object.keys(p.telemetry);
      for (let i=0;i<k.length;i++){
        if (i) t += ', ';
        t += k[i] + '=' + fmtMetric(k[i], p.telemetry[k[i]]);
      }
      t += '</small>';
    }
    div.innerHTML = t;
    box.appendChild(div);
  }
}
async function refreshIq(){
  const s = await getJSON('/api/iq');
  if (!s) return;
  $('iq').textContent = 'IQ: ' + (s.recording ? 'nahrávám' : (s.replay ? 'replay' : 'živě')) +
    ' · ' + s.captured_samples + ' vzorků · ' + s.file;
}
function fixed(value, digits){
  return (typeof value === 'number' && Number.isFinite(value)) ? value.toFixed(digits) : '-';
}
function updateBerHero(last, average){
  const hero = $('m17-ber-hero');
  const valid = typeof last === 'number' && Number.isFinite(last) && last >= 0;
  hero.classList.remove('ber-good', 'ber-warn', 'ber-bad', 'ber-unknown');
  if (!valid){
    hero.classList.add('ber-unknown');
    $('m17-ber-big').textContent = '-';
    $('m17-ber-average').textContent = '-';
    $('m17-ber-state').textContent = 'čekám na rámec';
    $('m17-ber-bar').style.width = '0%';
    return;
  }
  let cls = 'ber-bad';
  let text = 'kritický';
  if (last < 2){ cls = 'ber-good'; text = 'výborný'; }
  else if (last < 5){ cls = 'ber-warn'; text = 'použitelný'; }
  hero.classList.add(cls);
  $('m17-ber-big').textContent = fixed(last, 2);
  $('m17-ber-average').textContent = fixed(average, 2);
  $('m17-ber-state').textContent = text;
  $('m17-ber-bar').style.width = Math.max(2, Math.min(100, last * 10)) + '%';
}
async function refreshM17Diagnostics(){
  const d = await getJSON('/api/m17/diagnostics');
  if (!d || d.error) return;
  $('m17-afc').textContent = fixed(d.afc_correction_hz, 1);
  $('m17-distance').textContent = fixed(d.sync_distance, 2);
  $('m17-confidence').textContent = fixed(d.sync_confidence, 3);
  $('m17-hits').textContent = String(d.sync_hits || 0) + ' / ' + String(d.sync_misses || 0);
  $('m17-level').textContent = fixed(d.average_symbol_level, 2);
  $('m17-rssi').textContent = fixed(d.rssi_at_sync, 1);
  $('m17-sync-detected').textContent = String(d.sync_detected || 0);
  $('m17-lsf').textContent = String(d.lsf_ok || 0) + ' / ' + String(d.lsf_fail || 0);
  $('m17-crc').textContent = String(d.crc_ok || 0) + ' / ' + String(d.crc_fail || 0);
  $('m17-golay').textContent = String(d.golay_ok || 0) + ' / ' + String(d.golay_fail || 0);
  $('m17-viterbi').textContent = String(d.viterbi_ok || 0) + ' / ' + String(d.viterbi_fail || 0);
  $('m17-frame').textContent = String(d.frame_ok || 0) + ' / ' + String(d.frame_fail || 0);
  updateBerHero(d.last_frame_ber_percent, d.average_ber_percent);
  const body = $('m17-candidates');
  body.innerHTML = '';
  const rows = (d.candidates || []).slice().reverse();
  for (const c of rows){
    const tr = document.createElement('tr');
    const cells = [
      c.timestamp, fixed(c.rssi_dbfs, 1), fixed(c.afc_correction_hz, 0),
      fixed(c.sync_distance, 2), fixed(c.ber_percent, 2), c.sync_type,
    ];
    for (const value of cells){
      const td = document.createElement('td'); td.textContent = value; tr.appendChild(td);
    }
    const result = document.createElement('td');
    result.textContent = c.result;
    result.className = c.result === 'M17' ? 'ok' : 'bad';
    tr.appendChild(result);
    body.appendChild(tr);
  }
}
$('m17-reset').addEventListener('click', async function(){
  await fetch('/api/m17/diagnostics/reset', {method:'POST', cache:'no-store'});
  refreshM17Diagnostics();
});
drawSpectrum(); drawWaterfall(); refreshStatus(); refreshPlugins(); refreshIq(); refreshM17Diagnostics();
setInterval(drawSpectrum, 200);
setInterval(drawWaterfall, 2000);
setInterval(refreshStatus, 500);
setInterval(refreshPlugins, 2000);
setInterval(refreshIq, 2000);
setInterval(refreshM17Diagnostics, 500);
</script>
</body>
</html>
)HTML";
    return html;
}

}  // namespace sx1255::web
