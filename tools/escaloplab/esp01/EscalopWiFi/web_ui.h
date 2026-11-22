#pragma once

#include <Arduino.h>

#include "config.h" // WIFI_VERSION: the stylesheet's address changes with each version, so browsers never mix old and new

// The module's web pages, in escaloplab.com's look (its globals.css: blue-black #111217 with a matte grain, neon pink
// #FF2E8A, Tektur headings, 28 px panels, round buttons; light theme with ?theme=light). Tektur loads from Google Fonts when the phone has internet; otherwise the
// system font is used (the setup hotspot has no internet).

static const char APP_CSS[] PROGMEM = R"CSS(
:root{--bg:#111217;--surface:#17181e;--s2:#1e1f26;--s3:#282a32;--fg:#f2f2f5;--muted:#a9abb8;--line:rgb(255 255 255/.06);--rose:#ff2e8a;--rose-ink:#ff5ca3;--cer:#2a9df4;--ok:#35c26b;--bad:#ff5a4f;--r:28px;--rs:16px;--glow:0 0 14px rgb(255 46 138/.24);--ease:cubic-bezier(.22,1,.36,1);color-scheme:dark}
html.light{--bg:#f4f4f6;--surface:#fff;--s2:#ebebef;--s3:#dfdfe5;--fg:#111217;--muted:#595b67;--line:rgb(17 18 23/.07);--rose-ink:#c70f62;--ok:#1f9d55;--bad:#d92d20;color-scheme:light}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html{background:var(--bg);-webkit-text-size-adjust:100%}
body{margin:0;color:var(--fg);font:16px/1.55 Geist,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
body::before{content:"";position:fixed;inset:0;z-index:-1;pointer-events:none;opacity:.05;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='160' height='160'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.9' numOctaves='3' stitchTiles='stitch'/%3E%3CfeColorMatrix values='0 0 0 0 0.5 0 0 0 0 0.5 0 0 0 0 0.5 0 0 0 1 0'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)'/%3E%3C/svg%3E")}
h1,h2,h3,.brand,.num{font-family:Tektur,system-ui,sans-serif;letter-spacing:-.02em;margin:0}
h1{font-size:clamp(1.6rem,3vw,2.4rem);line-height:1.05}
h2{font-size:clamp(1.15rem,1.6vw,1.45rem);line-height:1.15;margin-bottom:16px}
h3{font-size:1rem;color:var(--muted);font-weight:500;margin:22px 0 10px}
.wrap{max-width:1600px;margin:0 auto;padding:clamp(16px,3.2vw,40px)}
header{display:flex;align-items:center;gap:14px;padding-bottom:clamp(16px,2.4vw,28px)}
header .grow{flex:1;min-width:0}
.sub{color:var(--muted);font-size:.875rem}
.grid{display:flex;flex-direction:column;gap:clamp(14px,1.6vw,22px)}
.grid .card{margin:0}
.colL,.colR{display:contents}
.o1{order:1}.o2{order:2}.o3{order:3}.o4{order:4}.o5{order:5}.o6{order:6}
@media (min-width:1000px){.grid{display:grid;grid-template-columns:minmax(0,5fr) minmax(0,7fr);align-items:start}
 .colL,.colR{display:flex;flex-direction:column;gap:clamp(14px,1.6vw,22px)}}
.card{background:var(--surface);border:1px solid var(--line);border-radius:var(--r);padding:clamp(20px,2.2vw,32px);margin-bottom:16px}
.row{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.row .grow{flex:1;min-width:0}
.pill{display:inline-flex;align-items:center;gap:6px;border-radius:999px;padding:4px 12px;font-size:.8rem;font-weight:600;background:var(--s2);color:var(--muted)}
.pill::before{content:"";width:7px;height:7px;border-radius:50%;background:currentColor}
.pill.on{color:var(--ok)}.pill.off{color:var(--bad)}.pill.rose{color:var(--rose-ink)}
button,.btn{font:600 .95rem Tektur,system-ui,sans-serif;border:0;border-radius:999px;min-height:46px;padding:0 1.3em;cursor:pointer;background:var(--rose);color:#fff;text-decoration:none;display:inline-flex;align-items:center;justify-content:center;gap:.5em;white-space:nowrap;user-select:none;transition:transform .2s var(--ease),box-shadow .3s var(--ease),background-color .2s}
button:hover:not(:disabled){box-shadow:var(--glow)}
button:active:not(:disabled){transform:scale(.96)}
button.ghost,.btn.ghost{background:var(--s2);color:var(--fg)}
button.ghost:hover:not(:disabled){background:var(--s3);box-shadow:none}
button.danger{background:var(--s2);color:var(--bad)}
button.danger:hover:not(:disabled){box-shadow:none;background:var(--s3)}
button:disabled{opacity:.4;cursor:default}
button svg{width:22px;height:22px;flex:none}
:focus-visible{outline:2px solid var(--rose);outline-offset:3px}
input[type=text],input[type=password],input[type=number]{font:inherit;font-size:max(16px,1em);width:100%;min-height:46px;padding:0 16px;border-radius:var(--rs);border:1px solid var(--line);background:var(--s2);color:var(--fg)}
input[type=file]{font:inherit;color:var(--muted)}
input[type=file]::file-selector-button{font:600 .95rem Tektur,system-ui,sans-serif;border:0;border-radius:999px;min-height:46px;padding:0 1.3em;margin-right:14px;background:var(--s2);color:var(--fg);cursor:pointer}
input[type=range]{accent-color:var(--rose)}
label{display:block;font-size:.875rem;color:var(--muted);margin:12px 0 6px}
.bar{height:8px;border-radius:99px;background:var(--s3);overflow:hidden;margin:12px 0}
.bar i{display:block;height:100%;width:0;background:var(--rose);transition:width .4s var(--ease)}
ul.list{list-style:none;margin:0;padding:0}
ul.list li{display:flex;align-items:center;gap:10px;padding:12px 0;border-top:1px solid var(--line)}
ul.list li:first-child{border-top:0}
ul.list .name{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
ul.list li.folder{color:var(--muted);font-size:.9rem;padding-top:16px}
ul.list li.infolder{padding-left:24px}
ul.list button{min-height:38px;padding:0 1em;font-size:.85rem}
.net{cursor:pointer;border-radius:var(--rs);padding:12px!important}
.net.sel{background:var(--s2);outline:2px solid var(--rose)}
.msg{padding:12px 16px;border-radius:var(--rs);background:var(--s2);margin-top:14px}
.msg.bad{color:var(--bad)}.msg.ok{color:var(--ok)}
.hidden{display:none!important}
small,.small{font-size:.875rem;color:var(--muted)}
a{color:var(--rose-ink)}
footer{text-align:center;padding:8px 0 24px}
/* Printer status */
.state{font-family:Tektur,system-ui,sans-serif;font-size:clamp(1.6rem,2.6vw,2.2rem);line-height:1.1}
/* Manual control */
.ctl{display:flex;flex-wrap:wrap;gap:clamp(18px,2.4vw,36px);align-items:flex-start}
.pad{display:grid;grid-template-columns:repeat(3,68px);grid-template-rows:repeat(3,68px);gap:10px}
.col{display:grid;grid-template-rows:repeat(3,68px);gap:10px;justify-items:center}
.key{width:68px;height:68px;min-height:0;padding:0;border-radius:22px;background:var(--s2);color:var(--fg);flex-direction:column;gap:0;font-size:.72rem}
.key:hover:not(:disabled){background:var(--s3);box-shadow:none}
.key.home{background:var(--rose);color:#fff}
.key.home:hover:not(:disabled){background:var(--rose);box-shadow:var(--glow)}
.axis{font-size:.75rem;color:var(--muted);text-align:center;margin-top:8px;font-family:Tektur,system-ui,sans-serif}
.seg{display:inline-flex;background:var(--s2);border-radius:999px;padding:4px;gap:2px}
.seg button{background:transparent;color:var(--muted);min-height:38px;padding:0 1em}
.seg button:hover:not(:disabled){box-shadow:none;color:var(--fg)}
.seg button.sel{background:var(--rose);color:#fff}
.pos{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin:18px 0 4px}
.pos div,.heater{background:var(--s2);border-radius:var(--rs);padding:12px 16px}
.pos .num{font-size:1.4rem}
.heaters{display:grid;gap:12px}
.heater{display:flex;align-items:center;gap:14px;flex-wrap:wrap}
.heater .num{font-size:2.2rem;line-height:1.1}
.heater .tgt{color:var(--muted);font-size:.9rem}
.heater .grow{flex:1;min-width:120px}
.heater input{width:92px;flex:none;background:var(--surface)}
.heater button{background:var(--s3);min-height:44px;padding:0 1.1em}
.heater button:hover:not(:disabled){background:var(--rose);color:#fff}
.chips{display:flex;flex-wrap:wrap;gap:8px}
.chips button{min-height:38px;padding:0 1em;font-size:.85rem}
.embed .wrap{max-width:none;padding:clamp(16px,2vw,28px)}
.embed header{display:none}
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
    "<title>" title "</title><link rel='stylesheet' href='/app.css?v=" WIFI_VERSION "'>" \
    "<link rel='stylesheet' href='https://fonts.googleapis.com/css2?family=Tektur:wght@500;600;700&family=Geist:wght@400;600&display=swap'></head><body><div class='wrap'>"

// ---------------------------------------------------------------------------------------------------------------
// Setup page, shown through the setup hotspot

// Small line icons for the control pad (24 x 24, current color)
#define ICON_PATH(d) "<svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2.4' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'><path d='" d "'/></svg>"
#define ICON_UP    ICON_PATH("M6 15l6-6 6 6")
#define ICON_DOWN  ICON_PATH("M6 9l6 6 6-6")
#define ICON_LEFT  ICON_PATH("M15 6l-6 6 6 6")
#define ICON_RIGHT ICON_PATH("M9 6l6 6-6 6")
#define ICON_HOME  ICON_PATH("M4 11l8-7 8 7M6 10v9h12v-9")

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

<div class="grid">
<div class="colL">
<div class="card o1">
  <div class="row"><span class="small grow">Printer</span><span class="pill" id="sdPill"></span></div>
  <div class="state" id="state" style="margin:6px 0 4px">Printer</div>
  <div id="job" class="hidden"><div class="small" id="jobName"></div><div class="bar"><i id="jobBar"></i></div></div>
  <div class="row" id="printCtl" style="margin-top:14px"><button class="ghost" data-a="pause">Pause</button><button class="ghost" data-a="resume">Resume</button><button class="danger" data-a="cancel" data-q="Stop this print?">Stop print</button></div>
  <div class="row" style="margin-top:14px"><button class="ghost" data-a="restart" data-q="Restart the printer?">Restart printer</button></div>
</div>

<div class="card o3">
  <h2>Temperature</h2>
  <div class="heaters">
    <div class="heater"><div class="grow"><div class="small">Nozzle</div><span class="num" id="tH">-</span> <span class="tgt" id="tHt"></span></div>
      <div class="row"><input type="number" id="setH" min="0" max="260" step="5" placeholder="&deg;C"><button class="ghost" data-t="hotend">Set</button><button class="ghost" data-t0="hotend">Off</button></div></div>
    <div class="heater"><div class="grow"><div class="small">Bed</div><span class="num" id="tB">-</span> <span class="tgt" id="tBt"></span></div>
      <div class="row"><input type="number" id="setB" min="0" max="110" step="5" placeholder="&deg;C"><button class="ghost" data-t="bed">Set</button><button class="ghost" data-t0="bed">Off</button></div></div>
  </div>
  <h3>Preheat</h3>
  <div class="chips"><button class="ghost pre" data-h="205" data-b="60">PLA 205 / 60</button><button class="ghost pre" data-h="235" data-b="80">PETG 235 / 80</button><button class="ghost pre" data-h="250" data-b="100">ABS 250 / 100</button><button class="ghost pre" data-h="225" data-b="50">TPU 225 / 50</button><button class="danger pre" data-h="0" data-b="0">Cool down</button></div>
  <h3>Part cooling fan</h3>
  <div class="row"><input type="range" id="fan" min="0" max="100" step="10" value="0" class="grow"><span class="num" id="fanV" style="min-width:56px;text-align:right">0%</span></div>
</div>

<div class="card o5">
  <h2>Firmware</h2>
  <div class="small">Printer: <b id="fw">-</b> &middot; Wi-Fi module: <b id="wv">-</b></div>
  <div class="msg" id="upd">Updates are checked every day.</div>
  <div class="bar hidden" id="updBarBox"><i id="updBar"></i></div>
  <div class="row" style="margin-top:14px"><button class="ghost" id="chk">Check for updates</button><button id="inst" class="hidden">Install update</button></div>
</div>

</div>
<div class="colR">
<div class="card o2" id="ctl">
  <div class="row" style="margin-bottom:16px"><h2 class="grow" style="margin:0">Move</h2><div class="seg" id="steps"><button data-s="0.1">0.1</button><button class="sel" data-s="1">1</button><button data-s="10">10</button><button data-s="50">50 mm</button></div></div>
  <div class="small" id="ctlNote"></div>
  <div class="ctl">
    <div><div class="pad">
      <span></span><button class="key mv" data-x="Y" data-d="1" title="Y+">)HTML" ICON_UP R"HTML(Y+</button><span></span>
      <button class="key mv" data-x="X" data-d="-1" title="X-">)HTML" ICON_LEFT R"HTML(X&minus;</button><button class="key home hm" data-h="XY" title="Home X and Y">)HTML" ICON_HOME R"HTML(XY</button><button class="key mv" data-x="X" data-d="1" title="X+">)HTML" ICON_RIGHT R"HTML(X+</button>
      <span></span><button class="key mv" data-x="Y" data-d="-1" title="Y-">)HTML" ICON_DOWN R"HTML(Y&minus;</button><span></span>
    </div><div class="axis">X / Y</div></div>
    <div><div class="col">
      <button class="key mv" data-x="Z" data-d="1" title="Z up">)HTML" ICON_UP R"HTML(Z+</button><button class="key home hm" data-h="Z" title="Home Z">)HTML" ICON_HOME R"HTML(Z</button><button class="key mv" data-x="Z" data-d="-1" title="Z down">)HTML" ICON_DOWN R"HTML(Z&minus;</button>
    </div><div class="axis">Z</div></div>
    <div><div class="col">
      <button class="key ex" data-e="-1" title="Retract">)HTML" ICON_UP R"HTML(Retract</button><span class="small" style="align-self:center" id="eLen">10 mm</span><button class="key ex" data-e="1" title="Extrude">)HTML" ICON_DOWN R"HTML(Extrude</button>
    </div><div class="axis">Extruder</div></div>
  </div>
  <div class="pos"><div><div class="small">X</div><span class="num" id="pX">-</span></div><div><div class="small">Y</div><span class="num" id="pY">-</span></div><div><div class="small">Z</div><span class="num" id="pZ">-</span></div></div>
  <h3>Homing</h3>
  <div class="row"><button class="hm" data-h="">)HTML" ICON_HOME R"HTML(Home all</button><button class="ghost hm" data-h="X">Home X</button><button class="ghost hm" data-h="Y">Home Y</button><button class="ghost hm" data-h="Z">Home Z</button><button class="ghost mo" id="motorsOff">Motors off</button></div>
  <h3>Bed leveling (paper test)</h3>
  <div class="chips"><button class="ghost cn" data-c="0">Front left</button><button class="ghost cn" data-c="1">Front right</button><button class="ghost cn" data-c="2">Back right</button><button class="ghost cn" data-c="3">Back left</button><button class="ghost cn" data-c="4">Center</button></div>
  <div class="small" style="margin-top:8px">The nozzle goes 0.2 mm above that spot. Slide a sheet of paper under it and turn the bed knob until it drags slightly.</div>
</div>

<div class="card o4">
  <h2>Print files</h2>
  <div class="row"><input type="file" id="file" accept=".gcode,.gco,.g" class="grow"><label style="margin:0"><input type="checkbox" id="startNow" checked> Print after upload</label></div>
  <div class="row" style="margin-top:12px"><button id="upBtn">Send to printer</button><span class="small" id="upInfo"></span></div>
  <div class="bar hidden" id="upBarBox"><i id="upBar"></i></div>
  <div class="msg hidden" id="upMsg"></div>
  <div class="row" style="margin-top:22px"><h3 class="grow" style="margin:0">On the SD card</h3><button class="ghost" id="reload" style="min-height:38px">Refresh</button></div>
  <ul class="list" id="files"><li class="small">Loading...</li></ul>
</div>

<div class="card o6">
  <h2>Wi-Fi</h2>
  <div class="small" id="wifi"></div>
  <p class="small">Escalop Workshop finds this printer by itself: open its <b>Printer</b> tab and choose this printer's serial number. Its address is <b id="addr"></b>.</p>
  <div class="row"><button class="danger" id="forget">Forget this Wi-Fi</button></div>
</div>
</div>
</div>
</div>
<script>
const $=id=>document.getElementById(id);let st=null,busy=false;
// Like escaloplab.com: dark unless ?theme=light. Inside Escalop Workshop: its theme color, no header (?embed=1&accent=RRGGBB)
(()=>{const q=new URLSearchParams(location.search),a=q.get('accent');if(q.get('theme')==='light')document.documentElement.classList.add('light');
 if(a&&/^[0-9a-fA-F]{6}$/.test(a)){const r=document.documentElement.style;r.setProperty('--rose','#'+a);r.setProperty('--rose-ink','#'+a)}
 if(q.get('embed'))document.body.classList.add('embed')})();
function esc(s){const d=document.createElement('div');d.textContent=s;return d.innerHTML}
function t(v){return v==null?'-':Math.round(v)+'&deg;'}
function mm(v){return v==null?'-':v.toFixed(v>=100||v<=-100?1:2)}
async function post(url,data){const r=await fetch(url,{method:'POST',body:new URLSearchParams(data||{})});const x=await r.text();if(!r.ok)throw new Error(x||r.status);return x}
async function refresh(){try{st=await(await fetch('/api/status')).json();render()}catch(e){$('conn').textContent='Offline';$('conn').className='pill off'}}
function render(){const p=st.printer,u=st.update;
 $('serial').textContent=st.module.serial?'Serial '+st.module.serial:'';
 $('conn').textContent=p.online?'Online':'No printer';$('conn').className='pill '+(p.online?'on':'off');
 let s=!p.online?'Not answering':p.printing?(p.paused?'Paused':'Printing'):p.busy?'Busy':'Ready';
 if(p.printing&&p.progress!=null)s+=' '+p.progress+'%';$('state').textContent=s;
 $('sdPill').textContent=p.sd?'SD card in':'No SD card';$('sdPill').className='pill '+(p.sd?'on':'off');
 $('job').classList.toggle('hidden',!p.printing);$('jobName').textContent=p.file||'';$('jobBar').style.width=(p.progress||0)+'%';
 $('tH').innerHTML=t(p.hotend[0]);$('tHt').innerHTML=p.hotend[1]?'&rarr; '+t(p.hotend[1]):'off';
 $('tB').innerHTML=t(p.bed[0]);$('tBt').innerHTML=p.bed[1]?'&rarr; '+t(p.bed[1]):'off';
 const ps=p.pos;$('pX').textContent=ps?mm(ps[0]):'-';$('pY').textContent=ps?mm(ps[1]):'-';$('pZ').textContent=ps?mm(ps[2]):'-';
 $('printCtl').classList.toggle('hidden',!p.printing);
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
 $('upBtn').disabled=busy||!p.online||p.printing;
 // Moving is off while printing; heat and fan still work (like on the printer's screen)
 document.querySelectorAll('#ctl button:not(.seg button)').forEach(b=>b.disabled=!p.online||p.printing);
 document.querySelectorAll('[data-t],[data-t0],.pre').forEach(b=>b.disabled=!p.online);$('fan').disabled=!p.online;
 $('ctlNote').textContent=!p.online?'The printer isn\'t answering.':p.printing?'Moving is off while printing.':''}
async function files(){try{const r=await(await fetch('/api/files')).json();const ul=$('files');ul.innerHTML='';
 if(!r.files.length){ul.innerHTML='<li class="small">'+(r.error?esc(r.error):'No print files on the card.')+'</li>';return}
 // Group by folder: the long path when Marlin gives one, else the 8.3 path
 const groups={};r.files.forEach(f=>{const p=(f.long||f.name).replace(/^\/+/,'');const i=p.lastIndexOf('/');
  const dir=i<0?'':p.slice(0,i);f.title=i<0?p:p.slice(i+1);(groups[dir]=groups[dir]||[]).push(f)});
 Object.keys(groups).sort().forEach(dir=>{if(dir){const h=document.createElement('li');h.className='folder';h.innerHTML='&#128193; <b></b>';
  h.querySelector('b').textContent=dir;ul.appendChild(h)}
  groups[dir].forEach(f=>{const li=document.createElement('li');if(dir)li.className='infolder';
  li.innerHTML='<span class="name"></span><span class="small">'+(f.size/1048576).toFixed(1)+' MB</span><button>Print</button><button class="danger">Delete</button>';
  li.querySelector('.name').textContent=f.title;const b=li.querySelectorAll('button');
  b[0].onclick=()=>post('/api/job',{action:'print',name:f.name}).then(refresh).catch(e=>alert(e.message));
  b[1].onclick=()=>{if(confirm('Delete '+f.title+'?'))post('/api/job',{action:'delete',name:f.name}).then(files).catch(e=>alert(e.message))};ul.appendChild(li)})})}catch(e){}}
document.querySelectorAll('[data-a]').forEach(b=>b.onclick=()=>{if(b.dataset.q&&!confirm(b.dataset.q))return;post('/api/control',{action:b.dataset.a}).then(refresh).catch(e=>alert(e.message))});
$('reload').onclick=files;
// Manual control
let step=1;const ctl=(d)=>post('/api/control',d).then(refresh).catch(e=>alert(e.message));
const eLen=()=>Math.min(Math.max(step,1),50);
document.querySelectorAll('#steps [data-s]').forEach(b=>b.onclick=()=>{step=+b.dataset.s;document.querySelectorAll('#steps [data-s]').forEach(x=>x.classList.toggle('sel',x===b));$('eLen').textContent=eLen()+' mm'});
$('eLen').textContent=eLen()+' mm';
document.querySelectorAll('.mv').forEach(b=>b.onclick=()=>ctl({action:'jog',axis:b.dataset.x,dist:(+b.dataset.d)*step}));
document.querySelectorAll('.ex').forEach(b=>b.onclick=()=>ctl({action:'jog',axis:'E',dist:(+b.dataset.e)*eLen()}));
document.querySelectorAll('.hm').forEach(b=>b.onclick=()=>ctl({action:'home',axes:b.dataset.h}));
document.querySelectorAll('.cn').forEach(b=>b.onclick=()=>ctl({action:'corner',spot:b.dataset.c}));
$('motorsOff').onclick=()=>ctl({action:'motors_off'});
document.querySelectorAll('[data-t]').forEach(b=>b.onclick=()=>{const v=$(b.dataset.t==='bed'?'setB':'setH').value;if(v!=='')ctl({action:'temp',tool:b.dataset.t,target:v})});
document.querySelectorAll('[data-t0]').forEach(b=>b.onclick=()=>ctl({action:'temp',tool:b.dataset.t0,target:0}));
document.querySelectorAll('.pre').forEach(b=>b.onclick=()=>ctl({action:'temp',tool:'hotend',target:b.dataset.h}).then(()=>ctl({action:'temp',tool:'bed',target:b.dataset.b})));
$('fan').oninput=()=>{$('fanV').textContent=$('fan').value+'%'};$('fan').onchange=()=>ctl({action:'fan',speed:$('fan').value});
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
