#pragma once

#include <pgmspace.h>

// Single-page UI: live telemetry (polls /api/status) and log file manager.
// Self-contained: the ESP access point has no internet, so no CDN assets.
const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Telemetry</title>
<link rel="icon" href="data:,">
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--muted:#6b7280;--line:#e3e6eb;--ok:#15803d;--bad:#b91c1c;--warn:#b45309;--acc:#2563eb}
@media (prefers-color-scheme:dark){:root{--bg:#0f1218;--card:#181c24;--fg:#e6e9ef;--muted:#8b93a3;--line:#2a303c;--ok:#4ade80;--bad:#f87171;--warn:#fbbf24;--acc:#60a5fa}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:980px;margin:0 auto;padding:16px}
header{display:flex;flex-wrap:wrap;align-items:center;gap:8px 16px;margin-bottom:12px}
h1{font-size:18px;margin:0}
h2{font-size:14px;margin:0 0 8px;color:var(--muted);font-weight:600;text-transform:uppercase;letter-spacing:.04em}
.pill{padding:2px 10px;border-radius:99px;font-weight:600;font-size:12px;border:1px solid currentColor}
.CONNECTED{color:var(--ok)}.LOST{color:var(--bad)}.WAITING{color:var(--warn)}
.meta{color:var(--muted);font-size:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:8px;margin-bottom:16px}
.tile{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:8px 10px}
.tile b{display:block;font-size:20px;font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tile span{color:var(--muted);font-size:12px}
section{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:12px;margin-bottom:16px}
table{width:100%;border-collapse:collapse}
td,th{padding:6px 4px;border-bottom:1px solid var(--line);text-align:left}
th{color:var(--muted);font-weight:600;font-size:12px}
td.n{text-align:right;font-variant-numeric:tabular-nums}
a,button{color:var(--acc)}
button{background:none;border:1px solid var(--line);border-radius:6px;padding:3px 8px;cursor:pointer;font:inherit}
button.del{color:var(--bad)}
.active{font-size:11px;color:var(--ok);margin-left:6px}
pre{margin:8px 0 0;max-height:320px;overflow:auto;font-size:12px;background:var(--bg);padding:8px;border-radius:6px}
#text{color:var(--muted);font-size:12px;margin:-8px 0 16px}
.wrap{overflow-x:auto}
</style>
</head>
<body>
<main>
<header>
  <h1>ESP32 MAVLink Telemetry</h1>
  <span id="link" class="pill WAITING">WAITING</span>
  <span class="meta" id="meta"></span>
</header>
<div class="grid" id="tiles"></div>
<div id="text"></div>
<section>
  <h2>Log files</h2>
  <div class="meta" id="fs"></div>
  <div class="wrap"><table>
    <thead><tr><th>File</th><th class="n">Size</th><th></th></tr></thead>
    <tbody id="files"></tbody>
  </table></div>
  <pre id="preview" hidden></pre>
</section>
</main>
<script>
const $=id=>document.getElementById(id);
const fmt=(v,d=1,u='')=>v===null||v===undefined?'—':(typeof v==='number'?v.toFixed(d):v)+u;
const kb=b=>b<1024?b+' B':b<1048576?(b/1024).toFixed(1)+' KB':(b/1048576).toFixed(2)+' MB';
const FIX=['no GPS','no fix','2D','3D','DGPS','RTK float','RTK fixed','static','PPP'];
function tiles(t){
  const rows=[
    ['Mode',t.mode??'—'],['Armed',t.armed===null?'—':(t.armed?'ARMED':'disarmed')],
    ['Battery',fmt(t.voltage,2,' V')],['Current',fmt(t.current,2,' A')],
    ['Remaining',t.battery_pct===null?'—':t.battery_pct+' %'],['Consumed',t.consumed_mah===null?'—':t.consumed_mah+' mAh'],
    ['Alt (rel)',fmt(t.alt_rel,1,' m')],['Alt (MSL)',fmt(t.alt_msl,1,' m')],
    ['GPS',t.gps_fix===null?'—':(FIX[t.gps_fix]??t.gps_fix)],['Satellites',t.sats??'—'],
    ['HDOP',fmt(t.hdop,2)],['Lat / Lon',t.lat===null?'—':t.lat.toFixed(6)+', '+t.lon.toFixed(6)],
    ['Ground speed',fmt(t.groundspeed,1,' m/s')],['Climb',fmt(t.climb,1,' m/s')],
    ['Heading',t.heading===null?'—':t.heading+'°'],['Throttle',t.throttle===null?'—':t.throttle+' %'],
    ['Roll / Pitch',t.roll===null?'—':t.roll.toFixed(1)+'° / '+t.pitch.toFixed(1)+'°'],['Yaw',fmt(t.yaw,1,'°')],
    ['UTC',t.utc||'—'],
  ];
  $('tiles').innerHTML=rows.map(([k,v])=>`<div class="tile"><span>${k}</span><b title="${v}">${v}</b></div>`).join('');
}
async function status(){
  try{
    const s=await (await fetch('/api/status')).json();
    const l=$('link');l.textContent=s.link;l.className='pill '+s.link;
    $('meta').textContent=`boot #${s.boot} · up ${Math.round(s.uptime_ms/1000)} s · ${s.baud} baud${s.swapped?" (RX/TX swapped)":""} · ${s.rx_packets} pkts · lost ${s.lost_count}× · rows ${s.rows}`;
    tiles(s.t);
    $('text').textContent=s.t.status_text?('FC: '+s.t.status_text):'';
    $('fs').textContent=`LittleFS: ${kb(s.fs_used)} / ${kb(s.fs_total)} · writing ${s.active_file||'— (waiting for FC)'}`;
  }catch(e){const l=$('link');l.textContent='ESP OFFLINE';l.className='pill LOST';}
}
async function files(){
  const r=await (await fetch('/api/files')).json();
  $('files').innerHTML=r.files.map(f=>{
    const n=encodeURIComponent(f.name);
    return `<tr><td><a href="/download?file=${n}">${f.name}</a>${f.active?'<span class="active">● writing</span>':''}</td>
<td class="n">${kb(f.size)}</td>
<td class="n"><button onclick="preview('${n}')">view</button> <a href="/download?file=${n}"><button>download</button></a>
${f.active?'':`<button class="del" onclick="del('${n}')">delete</button>`}</td></tr>`}).join('');
}
async function preview(n){
  const p=$('preview');p.hidden=false;p.textContent='loading…';
  p.textContent=await (await fetch('/view?file='+n)).text();
}
async function del(n){
  const r=await fetch('/delete?file='+n,{method:'POST'});
  if(!r.ok)$('preview').textContent=await r.text();
  files();
}
status();files();
setInterval(status,1000);setInterval(files,10000);
</script>
</body>
</html>
)HTML";
