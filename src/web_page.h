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
button,.btn{display:inline-block;background:none;border:1px solid var(--line);border-radius:6px;padding:3px 8px;cursor:pointer;font:inherit;text-decoration:none}
button.del{color:var(--bad)}
.stale .tile b{opacity:.4}
#stale{color:var(--warn);font-size:12px;margin:-8px 0 12px}
#msg{color:var(--bad);font-size:12px}
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
<div class="grid stale" id="tiles"></div>
<div id="stale" hidden></div>
<div id="text"></div>
<section>
  <h2>Log files</h2>
  <div class="meta" id="fs"></div>
  <div id="msg" role="status"></div>
  <div class="wrap"><table>
    <thead><tr><th>File</th><th class="n">Size</th><th></th></tr></thead>
    <tbody id="files"></tbody>
  </table></div>
  <pre id="preview" hidden></pre>
</section>
</main>
<script>
const $=id=>document.getElementById(id);
const esc=v=>String(v).replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';');
const fmt=(v,d=1,u='')=>v===null||v===undefined?'—':(typeof v==='number'?v.toFixed(d):v)+u;
const kb=b=>b<1024?b+' B':b<1048576?(b/1024).toFixed(1)+' KB':(b/1048576).toFixed(2)+' MB';
const FIX=['no GPS','no fix','2D','3D','DGPS','RTK float','RTK fixed','static','PPP'];
// The ESP web server handles one request at a time (and blocks while streaming
// a file), so every request has a timeout and polling never overlaps.
async function req(url,opt={}){
  const c=new AbortController(),t=setTimeout(()=>c.abort(),4000);
  try{return await fetch(url,{...opt,signal:c.signal,cache:'no-store'});}
  finally{clearTimeout(t);}
}
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
  $('tiles').innerHTML=rows.map(([k,v])=>`<div class="tile"><span>${k}</span><b title="${esc(v)}">${esc(v)}</b></div>`).join('');
}
// Values stay on screen after the link drops; they are dimmed and labelled as old.
function stale(note){
  $('tiles').classList.toggle('stale',!!note);
  $('stale').hidden=!note;$('stale').textContent=note||'';
}
async function status(){
  try{
    const r=await req('/api/status');
    if(!r.ok)throw new Error('HTTP '+r.status);
    const s=await r.json();
    const l=$('link');l.textContent=s.link;l.className='pill '+s.link;
    $('meta').textContent=`boot #${s.boot} · up ${Math.round(s.uptime_ms/1000)} s · ${s.baud} baud${s.swapped?" (RX/TX swapped)":""} · ${s.rx_packets} pkts · lost ${s.lost_count}× · rows ${s.rows}`;
    tiles(s.t);
    stale(s.link==='CONNECTED'?'':s.link==='LOST'?'No heartbeat from the flight controller: last received values, not live':'Waiting for the flight controller');
    $('text').textContent=s.t.status_text?('FC: '+s.t.status_text):'';
    $('fs').textContent=`LittleFS: ${kb(s.fs_used)} / ${kb(s.fs_total)} · writing ${s.active_file||'— (waiting for FC)'}`;
  }catch(e){
    const l=$('link');l.textContent='ESP OFFLINE';l.className='pill LOST';
    stale('No response from the ESP: last received values, not live');
  }
  setTimeout(status,1000);
}
let shown=null;
async function loadFiles(){
  try{
    const r=await req('/api/files');
    if(!r.ok)return;
    const {files}=await r.json();
    $('files').innerHTML=files.map(f=>{
      const n=encodeURIComponent(f.name),a=esc(f.name);
      return `<tr><td><a href="/download?file=${n}" download>${a}</a>${f.active?'<span class="active">● writing</span>':''}</td>
<td class="n">${kb(f.size)}</td>
<td class="n"><button data-act="view" data-n="${n}">view</button> <a class="btn" href="/download?file=${n}" download>download</a>
${f.active?'':`<button class="del" data-act="del" data-n="${n}">delete</button>`}</td></tr>`}).join('');
  }catch(e){}  // keep the last list while the ESP is unreachable
}
async function pollFiles(){await loadFiles();setTimeout(pollFiles,10000);}
async function preview(n){
  const p=$('preview');p.hidden=false;p.textContent='loading…';shown=n;
  try{
    const r=await req('/view?file='+n);
    p.textContent=await r.text();
  }catch(e){p.textContent='cannot load '+decodeURIComponent(n);}
}
async function del(n){
  const name=decodeURIComponent(n);
  if(!confirm('Delete '+name+'?'))return;
  $('msg').textContent='';
  try{
    const r=await req('/delete?file='+n,{method:'POST'});
    if(!r.ok)$('msg').textContent=`Cannot delete ${name}: ${await r.text()}`;
    else if(shown===n){$('preview').hidden=true;shown=null;}
  }catch(e){$('msg').textContent=`Cannot delete ${name}: no response from the ESP`;}
  loadFiles();
}
$('files').addEventListener('click',e=>{
  const b=e.target.closest('button[data-act]');
  if(!b)return;
  if(b.dataset.act==='view')preview(b.dataset.n);else del(b.dataset.n);
});
status();pollFiles();
</script>
</body>
</html>
)HTML";
