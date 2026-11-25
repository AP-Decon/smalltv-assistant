#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmallTV Assistant</title>
<style>
:root{--bg:#0f1115;--card:#1a1d24;--fg:#e8eaf0;--mut:#8b91a0;--acc:#2ad4d4}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:520px;margin:auto;padding:16px}h1{font-size:20px;margin:4px 0 2px}small,.mut{color:var(--mut)}
section{background:var(--card);border-radius:12px;padding:14px;margin:12px 0}h2{font-size:15px;margin:0 0 10px}
label{display:block;margin:8px 0 3px;color:var(--mut);font-size:13px}
input,select,textarea{width:100%;padding:9px;border-radius:8px;border:1px solid #333a46;background:#11141a;color:var(--fg);font:inherit}
input[type=checkbox]{width:auto;margin-right:6px}input[type=color]{height:40px;padding:2px}
button{padding:10px 14px;border:0;border-radius:8px;background:var(--acc);color:#001;font-weight:600;font:inherit;cursor:pointer}
button.sec{background:#2a2f3a;color:var(--fg)}.row{display:flex;gap:8px}.row>*{flex:1}
.styles button{background:#2a2f3a;color:var(--fg)}.styles button.on{background:var(--acc);color:#001}
.stk{display:flex;flex-wrap:wrap;gap:6px;margin-top:10px}.stk button{background:#2a2f3a;font-size:24px;padding:6px 9px}
.note{border-left:3px solid var(--acc);padding:4px 8px;margin:6px 0}.note b{display:block}
#msg{position:fixed;bottom:12px;left:50%;transform:translateX(-50%);background:#2a2f3a;padding:8px 14px;border-radius:8px;display:none}
</style></head><body><main>
<h1>SmallTV Assistant</h1><small id="info">loading...</small>

<section><h2>Style</h2>
<div class="row styles"><button data-s="face">Face</button><button data-s="mascot">Mascot</button><button data-s="cards">Cards</button></div>
</section>

<section><h2>Family chat</h2>
<label>This cube's name (shown on everyone's cubes)</label><input id="name" maxlength="20">
<label>Family code - the same on every cube and phone</label>
<div class="row"><input id="familyTopic" placeholder="leave empty to switch family chat off"><button class="sec" style="flex:0 0 auto" onclick="genCode()">New</button></div>
<button onclick="save()" style="margin-top:8px">Save</button>
<label>Send to the family</label>
<div class="row"><input id="f_msg" placeholder="Type a message"><button style="flex:0 0 auto" onclick="famSend()">Send</button></div>
<div id="stk" class="stk"></div>
<small id="chatlink"></small>
</section>

<section><h2>Send a test message</h2>
<label>Title</label><input id="t_title" value="Hello!">
<label>Message</label><textarea id="t_msg" rows="2">Your little assistant is working.</textarea>
<label>Mood</label><select id="t_mood"></select>
<div class="row" style="margin-top:10px"><button onclick="sendTest()">Send</button><button class="sec" onclick="api('/api/dismiss','POST')">Dismiss</button></div>
</section>

<section><h2>Notifications (ntfy)</h2>
<label>Server</label><input id="ntfyServer" placeholder="ntfy.sh">
<label>Port</label><input id="ntfyPort" type="number">
<label>Topic (make it long and random - anyone who knows it can post)</label><input id="ntfyTopic" placeholder="e.g. alan-cube-7f3k9q2x">
<label>Access token (optional, for private servers)</label><input id="ntfyToken" type="password">
<small id="ntfyState"></small>
</section>

<section><h2>Display</h2>
<label>Brightness <span id="bv"></span>%</label><input id="brightness" type="range" min="5" max="100">
<label>Night brightness %</label><input id="nightBrightness" type="number" min="0" max="100">
<div class="row"><div><label>Night from (hour)</label><input id="nightStart" type="number" min="0" max="23"></div>
<div><label>until (hour)</label><input id="nightEnd" type="number" min="0" max="23"></div></div>
<label>Show each message for (seconds)</label><input id="showSecs" type="number" min="3" max="600">
<label>Repeat last message for (minutes, 0 = off)</label><input id="keepMins" type="number" min="0" max="240">
<label>Eye colour</label><input id="eyeHex" type="color">
<label>Time zone (POSIX)</label><input id="tz">
<label><input type="checkbox" id="hour12">12-hour clock</label>
<label><input type="checkbox" id="bgr">BGR colour order (untick if red and blue are swapped)</label>
<label><input type="checkbox" id="invert">Invert panel (toggle if colours look like a negative)</label>
<div class="row" style="margin-top:10px"><button onclick="save()">Save</button><button class="sec" onclick="api('/api/test','POST')">Colour test</button></div>
</section>

<section><h2>Wi-Fi</h2><small>To move the cube to a different network: press this, then join the <b>SmallTV-Assistant</b> network and choose the new one.</small><br><br>
<button class="sec" onclick="if(confirm('Forget Wi-Fi and restart into setup mode?'))api('/api/forget-wifi','POST')">Forget Wi-Fi</button></section>

<section><h2>Recent</h2><div id="hist" class="mut">Nothing yet</div></section>
<section><h2>Firmware</h2><small id="fw"></small><br><br><a href="/update" style="color:var(--acc)">Upload new firmware (.bin)</a></section>
</main><div id="msg"></div>
<script>
const $=id=>document.getElementById(id);
const F=['name','familyTopic','ntfyServer','ntfyPort','ntfyTopic','ntfyToken','brightness','nightBrightness','nightStart','nightEnd','showSecs','keepMins','eyeHex','tz'];
const B=['hour12','bgr','invert'];
function toast(t){const m=$('msg');m.textContent=t;m.style.display='block';setTimeout(()=>m.style.display='none',1800)}
async function api(u,m='GET',body){const r=await fetch(u,{method:m,headers:{'Content-Type':'application/json'},body:body?JSON.stringify(body):undefined});toast(r.ok?'Done':'Error '+r.status);return r.ok?r.json().catch(()=>({})):{}}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
let filled=false;
function render(s,fill){
 if(fill||!filled){F.forEach(k=>$(k).value=s.config[k]??'');B.forEach(k=>$(k).checked=!!s.config[k]);filled=true}
 $('bv').textContent=$('brightness').value;
 document.querySelectorAll('.styles button').forEach(b=>b.classList.toggle('on',b.dataset.s===s.config.style));
 $('info').textContent=s.ip+'  -  '+(s.time||'')+'  -  free memory '+s.heap+' bytes';
 $('ntfyState').textContent=s.config.ntfyTopic?(s.ntfy?'Connected to ntfy':'Not connected to ntfy (yet)'):'No topic set - local /notify only';
 $('fw').textContent='Version '+s.version;
 $('hist').innerHTML=s.history.length?s.history.map(n=>'<div class="note"><b>'+(n.family?'&#128106; ':'')+esc(n.title||'(no title)')+'</b>'+esc(n.body)+' <small>['+n.mood+']</small></div>').join(''):'Nothing yet';
 if(!$('stk').children.length&&s.stickers)s.stickers.forEach(k=>{const b=document.createElement('button');b.textContent=k.emoji;b.title=k.name;b.onclick=()=>famSend(k.name);$('stk').appendChild(b)});
 $('chatlink').textContent=s.config.familyTopic?'Phone chat link: add #'+s.config.familyTopic+' to the end of your chat page address':'';
 if(!$('t_mood').options.length)s.moods.forEach(m=>$('t_mood').add(new Option(m,m)));
}
async function load(){try{render(await (await fetch('/api/state')).json())}catch(e){}}
async function save(){const c={};F.forEach(k=>c[k]=$(k).value);B.forEach(k=>c[k]=$(k).checked);render(await api('/api/config','POST',c),true)}
async function famSend(sticker){const m=$('f_msg').value;if(!m&&!sticker)return;await api('/api/family','POST',{message:m,sticker:sticker});$('f_msg').value=''}
function genCode(){const a='abcdefghjkmnpqrstuvwxyz23456789';let c='fam-';const r=crypto.getRandomValues(new Uint8Array(20));r.forEach(x=>c+=a[x%a.length]);$('familyTopic').value=c}
async function sendTest(){await api('/notify','POST',{title:$('t_title').value,message:$('t_msg').value,mood:$('t_mood').value})}
document.querySelectorAll('.styles button').forEach(b=>b.onclick=async()=>{render(await api('/api/style?s='+b.dataset.s,'POST'))});
$('brightness').oninput=()=>$('bv').textContent=$('brightness').value;
load();setInterval(load,5000);
</script></body></html>)HTML";
