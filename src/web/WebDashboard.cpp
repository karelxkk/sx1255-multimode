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
  for (const p of list){
    const div = document.createElement('div');
    div.className = 'plugin' + (p.active ? ' active' : '');
    let t = '<b>' + p.name + '</b>';
    if (p.telemetry){
      t += '<br><small>';
      const k = Object.keys(p.telemetry);
      for (let i=0;i<k.length;i++){
        if (i) t += ', ';
        t += k[i] + '=' + p.telemetry[k[i]];
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
drawSpectrum(); drawWaterfall(); refreshStatus(); refreshPlugins(); refreshIq();
setInterval(drawSpectrum, 200);
setInterval(drawWaterfall, 2000);
setInterval(refreshStatus, 500);
setInterval(refreshPlugins, 2000);
setInterval(refreshIq, 2000);
</script>
</body>
</html>
)HTML";
    return html;
}

}  // namespace sx1255::web
