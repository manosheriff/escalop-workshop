#pragma once

#include <Arduino.h>

// The module's web pages, in Escaloplab's look (escaloplab.com: neon pink #FF2E8A, Tektur headings,
// light #F4F4F6 / dark #111217). Tektur loads from Google Fonts when the phone has internet; otherwise the
// system font is used (the setup hotspot has no internet).

static const char APP_CSS[] PROGMEM = R"CSS(
:root{--bg:#f4f4f6;--surface:#fff;--fg:#111217;--muted:#595b67;--line:#e3e3e8;--rose:#ff2e8a;--rose-ink:#c70f62;--ok:#1f9d55;--bad:#d92d20;color-scheme:light}
@media (prefers-color-scheme:dark){:root{--bg:#111217;--surface:#17181e;--fg:#f2f2f5;--muted:#a9abb8;--line:#2a2b33;--rose-ink:#ff5ca3;color-scheme:dark}}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.55 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
h1,h2,.brand{font-family:Tektur,system-ui,sans-serif;letter-spacing:.01em;margin:0}
h1{font-size:22px;line-height:1.15}
h2{font-size:17px;margin-bottom:10px}
.wrap{max-width:720px;margin:0 auto;padding:16px}
header{display:flex;align-items:center;gap:12px;padding:8px 0 16px}
header .grow{flex:1;min-width:0}
.sub{color:var(--muted);font-size:13px}
.card{background:var(--surface);border:1px solid var(--line);border-radius:20px;padding:18px;margin-bottom:14px}
.row{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.row .grow{flex:1;min-width:0}
.pill{display:inline-block;border-radius:999px;padding:2px 10px;font-size:12px;font-weight:600;border:1px solid var(--line);color:var(--muted)}
.pill.on{color:var(--ok);border-color:currentColor}.pill.off{color:var(--bad);border-color:currentColor}.pill.rose{color:var(--rose-ink);border-color:currentColor}
button,.btn{font:inherit;font-weight:600;border:0;border-radius:999px;padding:9px 16px;cursor:pointer;background:var(--rose);color:#fff;text-decoration:none;display:inline-block}
button.ghost,.btn.ghost{background:transparent;color:var(--fg);border:1px solid var(--line)}
button.danger{background:transparent;color:var(--bad);border:1px solid currentColor}
button:disabled{opacity:.45;cursor:default}
button:focus-visible,input:focus-visible,a:focus-visible{outline:2px solid var(--rose);outline-offset:2px}
input[type=text],input[type=password]{font:inherit;width:100%;padding:10px 14px;border-radius:14px;border:1px solid var(--line);background:var(--bg);color:var(--fg)}
label{display:block;font-size:13px;color:var(--muted);margin:10px 0 4px}
.temps{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:12px 0}
.temp{background:var(--bg);border-radius:14px;padding:10px 12px}
.temp b{font-family:Tektur,system-ui,sans-serif;font-size:22px}
.bar{height:8px;border-radius:99px;background:var(--line);overflow:hidden;margin:10px 0}
.bar i{display:block;height:100%;width:0;background:var(--rose);transition:width .4s}
ul.list{list-style:none;margin:0;padding:0}
ul.list li{display:flex;align-items:center;gap:10px;padding:10px 0;border-top:1px solid var(--line)}
ul.list li:first-child{border-top:0}
ul.list .name{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
ul.list li.folder{color:var(--muted);font-size:14px;padding-top:14px}
ul.list li.infolder{padding-left:22px}
.net{cursor:pointer;border-radius:12px;padding:10px 8px!important}
.net.sel{background:var(--bg);outline:2px solid var(--rose)}
.msg{padding:10px 14px;border-radius:14px;background:var(--bg);margin-top:12px}
.msg.bad{color:var(--bad)}.msg.ok{color:var(--ok)}
.hidden{display:none!important}
small,.small{font-size:13px;color:var(--muted)}
a{color:var(--rose-ink)}
footer{text-align:center;padding:8px 0 24px}
@media (min-width:900px){.cards{display:grid;grid-template-columns:1fr 1fr;gap:14px;align-items:start}.cards .card{margin-bottom:0}}
.embed .wrap{max-width:none;padding:20px 28px}
.embed header{display:none}
.embed footer{text-align:left;padding-top:16px}
)CSS";

// Three neon chevrons, as on escaloplab.com
#define LOGO_SVG \
    "<svg width='40' height='40' viewBox='30.5 30 139 139' aria-hidden='true'>" \
    "<g fill='none' stroke-linecap='round' stroke-linejoin='round'>" \
    "<polyline points='46,86 100,48 154,86' stroke='#ff2e8a' stroke-width='15'/><polyline points='46,86 100,48 154,86' stroke='#ffb8d6' stroke-width='3.5'/>" \
    "<polyline points='46,119 100,81 154,119' stroke='#ff2e8a' stroke-width='15'/><polyline points='46,119 100,81 154,119' stroke='#ffb8d6' stroke-width='3.5'/>" \
    "<polyline points='46,152 100,114 154,152' stroke='#ff2e8a' stroke-width='15'/><polyline points='46,152 100,114 154,152' stroke='#ffb8d6' stroke-width='3.5'/>" \
    "</g></svg>"

#define PAGE_HEAD(title) \
    "<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>" \
    "<title>" title "</title><link rel='stylesheet' href='/app.css'>" \
    "<link rel='stylesheet' href='https://fonts.googleapis.com/css2?family=Tektur:wght@500;700&display=swap'></head><body><div class='wrap'>"

// ---------------------------------------------------------------------------------------------------------------
// Setup page, shown through the setup hotspot

static const char PAGE_SETUP[] PROGMEM = PAGE_HEAD("Wi-Fi setup") R"HTML(
<header>)HTML" LOGO_SVG R"HTML(<div class="grow"><h1>Wi-Fi setup</h1><div class="sub">Escaloplab v80</div></div></header>
<div class="card" id="pick">
  <h2>Choose your Wi-Fi</h2>
  <p class="small">Only 2.4 GHz networks work. If yours isn't listed, wait a moment or tap Search again.</p>
  <ul class="list" id="nets"><li class="small">Searching...</li></ul>
  <div class="row" style="margin-top:8px"><button class="ghost" id="again" type="button">Search again</button>
  <button class="ghost" id="other" type="button">Other network</button></div>
  <form id="form" class="hidden" autocomplete="off">
    <div id="ssidBox" class="hidden"><label for="ssid">Network name</label><input type="text" id="ssid" maxlength="32"></div>
    <label for="pass">Password</label><input type="password" id="pass" maxlength="64">
    <div class="row" style="margin-top:6px"><label style="margin:0"><input type="checkbox" id="show"> Show password</label></div>
    <div class="row" style="margin-top:14px"><button type="submit" id="save">Connect</button></div>
  </form>
</div>
<div class="card hidden" id="status"><h2 id="stTitle">Connecting...</h2><p id="stText" class="small"></p>
  <div class="row"><button class="ghost hidden" id="retry" type="button">Try again</button></div></div>
<footer class="small">You can also set up Wi-Fi on the printer: Menu &gt; Wi-Fi.</footer>
</div>
<script>
const $=id=>document.getElementById(id);let sel=null,manual=false,poll=null;
function esc(s){const d=document.createElement('div');d.textContent=s;return d.innerHTML}
function bars(r){return r>-60?'&#9679;&#9679;&#9679;':r>-75?'&#9679;&#9679;&#9675;':'&#9679;&#9675;&#9675;'}
async function scan(){$('nets').innerHTML='<li class="small">Searching...</li>';
 for(let i=0;i<15;i++){try{const r=await(await fetch('/api/scan')).json();if(!r.scanning){show(r.networks);return}}catch(e){}await new Promise(r=>setTimeout(r,1500))}
 $('nets').innerHTML='<li class="small">No networks found. Tap Search again.</li>'}
function show(list){if(!list.length){$('nets').innerHTML='<li class="small">No networks found. Tap Search again.</li>';return}
 $('nets').innerHTML='';list.forEach(n=>{const li=document.createElement('li');li.className='net';
 li.innerHTML='<span class="name">'+esc(n.ssid)+'</span><span class="small">'+(n.secure?'&#128274; ':'')+bars(n.rssi)+'</span>';
 li.onclick=()=>{document.querySelectorAll('.net').forEach(x=>x.classList.remove('sel'));li.classList.add('sel');sel=n;manual=false;
 $('ssidBox').classList.add('hidden');$('form').classList.remove('hidden');$('pass').disabled=!n.secure;$('pass').value='';if(n.secure)$('pass').focus()};
 $('nets').appendChild(li)})}
$('again').onclick=scan;
$('other').onclick=()=>{manual=true;sel=null;document.querySelectorAll('.net').forEach(x=>x.classList.remove('sel'));
 $('ssidBox').classList.remove('hidden');$('form').classList.remove('hidden');$('pass').disabled=false;$('ssid').focus()};
$('show').onchange=e=>{$('pass').type=e.target.checked?'text':'password'};
$('form').onsubmit=async e=>{e.preventDefault();const ssid=manual?$('ssid').value.trim():(sel&&sel.ssid);if(!ssid)return;
 const body=new URLSearchParams({ssid,pass:$('pass').disabled?'':$('pass').value});
 $('pick').classList.add('hidden');$('status').classList.remove('hidden');$('retry').classList.add('hidden');
 $('stTitle').textContent='Connecting to '+ssid+'...';$('stText').textContent='This takes up to 30 seconds. The printer\'s screen shows the result too.';
 try{await fetch('/api/setup',{method:'POST',body})}catch(e){}
 clearInterval(poll);poll=setInterval(check,1500)};
async function check(){try{const s=await(await fetch('/api/setup/state')).json();
 if(s.result==='ok'){clearInterval(poll);$('stTitle').textContent='Connected!';
  $('stText').innerHTML='Your printer is on '+esc(s.ssid)+'. Reconnect your phone to your Wi-Fi, then open <b>http://'+esc(s.host)+'.local</b> or <b>http://'+esc(s.ip)+'</b>. In Escalop Workshop, use that address as an OctoPrint host to send prints.'}
 else if(s.result==='badpw'||s.result==='fail'){clearInterval(poll);$('stTitle').textContent=s.result==='badpw'?'Wrong password':'Couldn\'t connect';
  $('stText').textContent=s.result==='badpw'?'Check the password and try again.':'The network didn\'t answer. Is it a 2.4 GHz network, and in range?';$('retry').classList.remove('hidden')}
 }catch(e){$('stText').textContent='Your phone lost the setup Wi-Fi while the printer connected. That is normal: look at the printer\'s screen for the result.'}}
$('retry').onclick=()=>{$('status').classList.add('hidden');$('pick').classList.remove('hidden')};
scan();
</script></body></html>)HTML";

// ---------------------------------------------------------------------------------------------------------------
// The printer's page on the home network

static const char PAGE_MAIN[] PROGMEM = PAGE_HEAD("Escaloplab v80") R"HTML(
<header>)HTML" LOGO_SVG R"HTML(<div class="grow"><h1>Escaloplab v80</h1><div class="sub" id="serial"></div></div><span class="pill" id="conn">...</span></header>

<div class="cards">
<div class="card">
  <div class="row"><h2 class="grow" id="state" style="margin:0">Printer</h2><span class="pill" id="sdPill"></span></div>
  <div id="job" class="hidden"><div class="small" id="jobName"></div><div class="bar"><i id="jobBar"></i></div></div>
  <div class="temps"><div class="temp"><div class="small">Nozzle</div><b id="tH">-</b> <span class="small" id="tHt"></span></div>
  <div class="temp"><div class="small">Bed</div><b id="tB">-</b> <span class="small" id="tBt"></span></div></div>
  <div class="row" id="printCtl"><button class="ghost" data-a="pause">Pause</button><button class="ghost" data-a="resume">Resume</button><button class="danger" data-a="cancel" data-q="Stop this print?">Stop print</button></div>
  <div class="row" id="idleCtl"><button class="ghost" data-a="preheat_pla">Preheat PLA</button><button class="ghost" data-a="preheat_petg">Preheat PETG</button><button class="ghost" data-a="cooldown">Cool down</button><button class="ghost" data-a="home">Home</button><button class="ghost" data-a="restart" data-q="Restart the printer?">Restart printer</button></div>
</div>

<div class="card">
  <h2>Print files</h2>
  <div class="row"><input type="file" id="file" accept=".gcode,.gco,.g" class="grow"><label style="margin:0"><input type="checkbox" id="startNow" checked> Print after upload</label></div>
  <div class="row" style="margin-top:10px"><button id="upBtn">Send to printer</button><span class="small" id="upInfo"></span></div>
  <div class="bar hidden" id="upBarBox"><i id="upBar"></i></div>
  <div class="msg hidden" id="upMsg"></div>
  <h2 style="margin-top:16px">On the SD card</h2>
  <ul class="list" id="files"><li class="small">Loading...</li></ul>
  <div class="row" style="margin-top:8px"><button class="ghost" id="reload">Refresh</button></div>
</div>

<div class="card">
  <h2>Firmware</h2>
  <div class="small">Printer: <b id="fw">-</b> &middot; Wi-Fi module: <b id="wv">-</b></div>
  <div class="msg" id="upd">Updates are checked every day.</div>
  <div class="bar hidden" id="updBarBox"><i id="updBar"></i></div>
  <div class="row" style="margin-top:10px"><button class="ghost" id="chk">Check for updates</button><button id="inst" class="hidden">Install update</button></div>
</div>

<div class="card">
  <h2>Wi-Fi</h2>
  <div class="small" id="wifi"></div>
  <p class="small">Escalop Workshop finds this printer by itself: open its <b>Printer</b> tab and choose this printer's serial number. Its address is <b id="addr"></b>.</p>
  <div class="row"><button class="danger" id="forget">Forget this Wi-Fi</button></div>
</div>
</div>
<footer class="small"><a href="/log">Log</a> &middot; <a href="/update">Wi-Fi module software</a></footer>
</div>
<script>
const $=id=>document.getElementById(id);let st=null,busy=false;
// Inside Escalop Workshop's Printer tab: its theme color, no own header (?embed=1&accent=RRGGBB)
(()=>{const q=new URLSearchParams(location.search),a=q.get('accent');if(a&&/^[0-9a-fA-F]{6}$/.test(a)){const r=document.documentElement.style;r.setProperty('--rose','#'+a);r.setProperty('--rose-ink','#'+a)}
 if(q.get('embed'))document.body.classList.add('embed')})();
function esc(s){const d=document.createElement('div');d.textContent=s;return d.innerHTML}
function t(v){return v==null?'-':Math.round(v)+'&deg;'}
async function post(url,data){const r=await fetch(url,{method:'POST',body:new URLSearchParams(data||{})});const x=await r.text();if(!r.ok)throw new Error(x||r.status);return x}
async function refresh(){try{st=await(await fetch('/api/status')).json();render()}catch(e){$('conn').textContent='Offline';$('conn').className='pill off'}}
function render(){const p=st.printer,u=st.update;
 $('serial').textContent=st.module.serial?'Serial '+st.module.serial:'';
 $('conn').textContent=p.online?'Online':'No printer';$('conn').className='pill '+(p.online?'on':'off');
 let s=!p.online?'Printer not answering':p.printing?(p.paused?'Paused':'Printing'):p.busy?'Busy':'Ready';
 if(p.printing&&p.progress!=null)s+=' '+p.progress+'%';$('state').textContent=s;
 $('sdPill').textContent=p.sd?'SD card in':'No SD card';$('sdPill').className='pill '+(p.sd?'':'off');
 $('job').classList.toggle('hidden',!p.printing);$('jobName').textContent=p.file||'';$('jobBar').style.width=(p.progress||0)+'%';
 $('tH').innerHTML=t(p.hotend[0]);$('tHt').innerHTML=p.hotend[1]?'/ '+t(p.hotend[1]):'';
 $('tB').innerHTML=t(p.bed[0]);$('tBt').innerHTML=p.bed[1]?'/ '+t(p.bed[1]):'';
 $('printCtl').classList.toggle('hidden',!p.printing);$('idleCtl').classList.toggle('hidden',p.printing||!p.online);
 document.querySelector('[data-a=pause]').classList.toggle('hidden',p.paused);document.querySelector('[data-a=resume]').classList.toggle('hidden',!p.paused);
 $('fw').textContent=p.fw||'unknown';$('wv').textContent=st.module.version;
 let m='Up to date. Updates are checked every day.';
 if(u.state==='checking')m='Checking for updates...';else if(u.state==='downloading')m='Downloading new firmware... '+u.progress+'%';
 else if(u.state==='installing')m='Installing firmware '+u.progress+'%. Don\'t turn the printer off.';
 else if(u.available)m='New firmware '+u.available+' is ready. It installs in about a minute; the printer restarts.';
 if(u.error&&(u.state==='failed'||!u.available))m+=' ('+u.error+')';$('upd').textContent=m;
 $('updBarBox').classList.toggle('hidden',!(u.state==='downloading'||u.state==='installing'));$('updBar').style.width=u.progress+'%';
 $('inst').classList.toggle('hidden',!u.available||u.state==='installing');$('inst').disabled=p.printing||p.busy;
 const w=st.wifi;$('wifi').innerHTML='Connected to <b>'+esc(w.ssid)+'</b> &middot; '+esc(w.ip)+' &middot; signal '+w.rssi+' dBm';
 $('addr').textContent=w.host+'.local';
 $('upBtn').disabled=busy||!p.online||p.printing}
async function files(){try{const r=await(await fetch('/api/files')).json();const ul=$('files');ul.innerHTML='';
 if(!r.files.length){ul.innerHTML='<li class="small">'+(r.error?esc(r.error):'No print files on the card.')+'</li>';return}
 // Group by folder: the long path when Marlin gives one, else the 8.3 path
 const groups={};r.files.forEach(f=>{const p=(f.long||f.name).replace(/^\/+/,'');const i=p.lastIndexOf('/');
  const dir=i<0?'':p.slice(0,i);f.title=i<0?p:p.slice(i+1);(groups[dir]=groups[dir]||[]).push(f)});
 Object.keys(groups).sort().forEach(dir=>{if(dir){const h=document.createElement('li');h.className='folder';h.innerHTML='&#128193; <b></b>';
  h.querySelector('b').textContent=dir;ul.appendChild(h)}
  groups[dir].forEach(f=>{const li=document.createElement('li');if(dir)li.className='infolder';
  li.innerHTML='<span class="name"></span><span class="small">'+(f.size/1048576).toFixed(1)+' MB</span><button class="ghost">Print</button><button class="danger">Delete</button>';
  li.querySelector('.name').textContent=f.title;const b=li.querySelectorAll('button');
  b[0].onclick=()=>post('/api/job',{action:'print',name:f.name}).then(refresh).catch(e=>alert(e.message));
  b[1].onclick=()=>{if(confirm('Delete '+f.title+'?'))post('/api/job',{action:'delete',name:f.name}).then(files).catch(e=>alert(e.message))};ul.appendChild(li)})})}catch(e){}}
document.querySelectorAll('[data-a]').forEach(b=>b.onclick=()=>{if(b.dataset.q&&!confirm(b.dataset.q))return;post('/api/control',{action:b.dataset.a}).then(refresh).catch(e=>alert(e.message))});
$('reload').onclick=files;
$('chk').onclick=()=>post('/api/update/check').then(()=>{$('upd').textContent='Checking. The Wi-Fi module restarts for this; the page is back in about 30 seconds.'});
$('inst').onclick=()=>{if(confirm('Install the new printer firmware now? The printer restarts.'))post('/api/update/install').then(refresh).catch(e=>alert(e.message))};
$('forget').onclick=()=>{if(confirm('Forget this Wi-Fi? The printer turns its setup hotspot on.'))post('/api/wifi/forget')};
$('upBtn').onclick=()=>{const f=$('file').files[0];if(!f)return;const fd=new FormData();fd.append('print',$('startNow').checked?'true':'false');fd.append('file',f,f.name);
 const x=new XMLHttpRequest();busy=true;render();$('upBarBox').classList.remove('hidden');$('upMsg').classList.add('hidden');const t0=Date.now();
 x.upload.onprogress=e=>{if(!e.lengthComputable)return;$('upBar').style.width=(100*e.loaded/e.total)+'%';const s=(Date.now()-t0)/1000;
 $('upInfo').textContent=Math.round(100*e.loaded/e.total)+'% · '+(e.loaded/1024/Math.max(s,.1)).toFixed(0)+' KB/s'};
 x.onloadend=()=>{busy=false;$('upBarBox').classList.add('hidden');$('upInfo').textContent='';const ok=x.status>=200&&x.status<300;
 $('upMsg').textContent=ok?'Sent to the printer.':(x.responseText||'Upload failed');$('upMsg').className='msg '+(ok?'ok':'bad');files();refresh()};
 x.open('POST','/api/files/local');x.send(fd)};
refresh();files();setInterval(refresh,2000);
</script></body></html>)HTML";

// ---------------------------------------------------------------------------------------------------------------
// Installing the Wi-Fi module's own software by hand (normally it updates itself from GitHub)

static const char PAGE_MODULE_UPDATE[] PROGMEM = PAGE_HEAD("Wi-Fi module software") R"HTML(
<header>)HTML" LOGO_SVG R"HTML(<div class="grow"><h1>Wi-Fi module software</h1><div class="sub">Normally this updates itself every day.</div></div></header>
<div class="card"><p class="small">Only use this if Escaloplab support asked you to. Choose the <b>EscalopV80_WiFi_x.y.z.bin.gz</b> file.</p>
<form method="POST" action="/update" enctype="multipart/form-data"><input type="file" name="bin" accept=".gz,.bin"><div class="row" style="margin-top:12px"><button>Install</button><a class="btn ghost" href="/">Back</a></div></form></div>
</div></body></html>)HTML";
