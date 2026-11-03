#pragma once

// The phone control page (ControlServer, phone control on): one self-contained page a phone's
// browser opens at http://<this PC>:9123/?key=..., talking to the control API with that key.
// It wears the desk's theme (its colours, key corners and typeface, from /api/state, following a
// change). MIX: every input, bus and output with its meter, fader and mute (S on inputs).
// SCENES: scenes (the active one lit) and macros. PADS: banks A-D, held while the finger is down.
namespace PhonePage
{
    inline const char* const html = R"PAGE(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<meta name="theme-color" content="#16181b"><title>Faider</title>
<style>
:root{--bg:#16181b;--panel:#23262b;--well:#1b1d20;--rim:#33373e;--ink:#e8e9eb;--dim:#8b9099;--key:#2d3137;--keyink:#e8e9eb;
 --lit:#3fd16b;--litink:#0d1a10;--mute:#e0524f;--muteink:#fff;--solo:#e8c33c;--soloink:#1a1405;--glass:#0c0d0f;--r:8px;--font:system-ui,Segoe UI,Roboto,sans-serif}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg)}
body{color:var(--ink);font:600 14px/1.2 var(--font);text-transform:uppercase;letter-spacing:.04em;user-select:none;-webkit-user-select:none}
header{position:sticky;top:0;z-index:2;background:var(--bg);padding:12px 14px 10px;border-bottom:1px solid var(--rim)}
.top{display:flex;align-items:center;gap:10px;height:22px}.top b{font-size:16px;letter-spacing:.12em}
.dot{width:9px;height:9px;border-radius:50%;background:var(--mute);flex:none}.dot.on{background:var(--lit)}
.scene{margin-left:auto;color:var(--dim);font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:55%}
.row,.seg{display:flex;gap:6px}
.seg{margin-top:10px}.seg button{flex:1 1 0;min-width:0}
button{font:inherit;font-size:13px;color:var(--keyink);background:var(--key);border:1px solid var(--rim);border-radius:var(--r);
 height:44px;padding:0 8px;margin:0;display:flex;align-items:center;justify-content:center;text-align:center;line-height:1.15;
 text-transform:uppercase;letter-spacing:.05em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
button.on{background:var(--lit);color:var(--litink);border-color:var(--lit)}
button.m.on{background:var(--mute);border-color:var(--mute);color:var(--muteink)}button.s.on{background:var(--solo);border-color:var(--solo);color:var(--soloink)}
main{padding:4px 14px 30px}
h2{font-size:11px;color:var(--dim);margin:16px 2px 8px;letter-spacing:.14em;font-weight:700}
.strip{background:var(--panel);border:1px solid var(--rim);border-radius:calc(var(--r) + 2px);padding:10px 12px 6px;margin-bottom:8px}
.strip .row{align-items:center;height:36px}
.name{flex:1 1 auto;min-width:0;overflow:hidden;white-space:nowrap;text-overflow:ellipsis}
.db{flex:none;width:56px;text-align:right;color:var(--dim);font-variant-numeric:tabular-nums;font-size:12px}
.strip button{flex:none;width:44px;height:36px;padding:0}
.meter{height:5px;background:var(--glass);border-radius:3px;margin:8px 0 2px;overflow:hidden}
.meter i{display:block;height:100%;width:0;background:var(--lit)}
input[type=range]{display:block;width:100%;height:36px;-webkit-appearance:none;appearance:none;background:transparent;margin:0}
input[type=range]::-webkit-slider-runnable-track{height:6px;background:var(--glass);border-radius:3px}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:30px;height:30px;margin-top:-12px;border-radius:min(var(--r),8px);background:var(--key);border:2px solid var(--keyink)}
input[type=range]::-moz-range-track{height:6px;background:var(--glass);border-radius:3px}
input[type=range]::-moz-range-thumb{width:28px;height:28px;border-radius:min(var(--r),8px);background:var(--key);border:2px solid var(--keyink)}
.off{opacity:.45}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
.grid button{height:52px;white-space:normal}
.pads{grid-template-columns:repeat(4,minmax(0,1fr))}
.pads button{height:auto;aspect-ratio:1/1;padding:4px;font-size:11px;white-space:normal;overflow-wrap:anywhere}
.pads button span{display:-webkit-box;-webkit-line-clamp:3;-webkit-box-orient:vertical;overflow:hidden}
.pads button.empty{opacity:.3}.pads button.down{background:var(--lit);color:var(--litink)}
.banks{margin:10px 0}
.page{display:none}.page.show{display:block}
.msg{grid-column:1/-1;color:var(--dim);text-align:center;margin:24px 0;text-transform:none;font-weight:500}
</style></head><body>
<header><div class="top"><span class="dot" id="dot"></span><b>FAIDER</b><span class="scene" id="scene"></span></div>
<div class="seg" id="nav"><button data-p="mix" class="on">MIX</button><button data-p="scenes">SCENES</button><button data-p="pads">PADS</button></div></header>
<main><div class="page show" id="mix"></div><div class="page" id="scenes"></div>
<div class="page" id="pads"><div class="seg banks" id="banks"></div><div class="grid pads" id="padgrid"></div></div></main>
<script>
const key=new URLSearchParams(location.search).get('key')||'';
const H={'X-Faider':'1','X-Faider-Key':key};
const $=s=>document.querySelector(s), E=(t,c,x)=>{const e=document.createElement(t);if(c)e.className=c;if(x!==undefined)e.textContent=x;return e};
const enc=s=>encodeURIComponent(s);
async function get(p){const r=await fetch(p,{headers:{'X-Faider-Key':key},cache:'no-store'});if(!r.ok)throw r.status;return r.json()}
function post(p){return fetch(p,{method:'POST',headers:H}).catch(()=>{})}
let state=null,shape='',themeShown='',bank=0,strips={};
document.querySelectorAll('#nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('#nav button').forEach(x=>x.classList.toggle('on',x===b));
 document.querySelectorAll('.page').forEach(p=>p.classList.toggle('show',p.id===b.dataset.p))});
// Dark or light writing for a colour, whichever reads.
function inkOn(hex){const n=parseInt(hex.slice(1),16),r=n>>16&255,g=n>>8&255,b=n&255;return (0.299*r+0.587*g+0.114*b)>150?'#141414':'#ffffff'}
function applyTheme(t){
 const s=JSON.stringify(t);if(!t||s===themeShown)return;themeShown=s;const v=document.documentElement.style;
 const set={'--bg':t.background,'--panel':t.panel,'--well':t.panelDark,'--rim':t.rim,'--ink':t.legend,'--dim':t.legendDim,'--key':t.key,'--keyink':t.keyText,
  '--lit':t.lit,'--litink':inkOn(t.lit),'--mute':t.mute,'--muteink':inkOn(t.mute),'--solo':t.solo,'--soloink':inkOn(t.solo),'--glass':t.glass,'--r':Math.max(2,t.radius)+'px'};
 for(const k in set)v.setProperty(k,set[k]);
 v.setProperty('--font',(t.font?'"'+t.font+'",':'')+'system-ui,Segoe UI,Roboto,sans-serif');
 document.querySelector('meta[name=theme-color]').content=t.background}
function dbText(v){return v<=-60?'-INF':(v>0?'+':'')+v.toFixed(1)}
// A pad's name as its key shows it (as on the desk): words, without catalogue numbers or a stock library's uploader.
function shortName(n){let w=n.split(/[-_. ]+/).filter(Boolean);if(w.length>=3&&/^\d{5,}$/.test(w[w.length-1]))w.shift();
 w=w.filter(x=>!/^\d+$/.test(x));let out='';for(const x of w){if((out+' '+x).trim().length>24)break;out=(out+' '+x).trim()}return out||n.slice(0,24)}
function strip(kind,s){
 const el=E('div','strip'),row=E('div','row'),nm=E('span','name',s.name),db=E('span','db');
 const meter=E('div','meter'),bar=E('i');meter.append(bar);
 const fader=E('input');fader.type='range';fader.min=-60;fader.max=12;fader.step=0.5;
 let last=0,busy=false;
 fader.addEventListener('pointerdown',()=>busy=true);
 fader.addEventListener('pointerup',()=>setTimeout(()=>busy=false,600));
 fader.oninput=()=>{db.textContent=dbText(+fader.value);const t=Date.now();if(t-last>60){last=t;post(`/api/${kind}/${enc(s.name)}/fader?value=${fader.value}`)}};
 fader.onchange=()=>post(`/api/${kind}/${enc(s.name)}/fader?value=${fader.value}`);
 row.append(nm,db);
 let so=null;
 if(kind==='input'){so=E('button','s','S');so.onclick=()=>{so.classList.toggle('on');post(`/api/input/${enc(s.name)}/solo?value=${so.classList.contains('on')?1:0}`)};row.append(so)}
 const m=E('button','m','M');m.onclick=()=>{m.classList.toggle('on');post(`/api/${kind}/${enc(s.name)}/mute?value=${m.classList.contains('on')?1:0}`)};
 row.append(m);el.append(row,meter,fader);
 strips[kind+':'+s.name]={el,fader,db,bar,m,so,busy:()=>busy};
 return el}
function build(){
 strips={};const mix=$('#mix');mix.innerHTML='';
 for(const [kind,list,title] of [['input',state.inputs,'INPUTS'],['bus',state.buses,'BUSES'],['output',state.outputs,'OUTPUTS']]){
  if(!list.length)continue;mix.append(E('h2','',title));for(const s of list)mix.append(strip(kind,s))}
 const sc=$('#scenes');sc.innerHTML='';
 for(const [title,list,api,empty] of [['SCENES',state.scenes,'scene','No scenes yet.'],['MACROS',state.macros,'macro','No macros yet.']]){
  sc.append(E('h2','',title));const g=E('div','grid');sc.append(g);
  if(!list.length)g.append(E('div','msg',empty));
  for(const n of list){const b=E('button','',n);if(api==='scene')b.dataset.scene=n;b.onclick=()=>post(`/api/${api}/${enc(n)}`);g.append(b)}}
 buildPads()}
function buildPads(){
 const banks=$('#banks');banks.innerHTML='';
 ['A','B','C','D'].forEach((l,i)=>{const b=E('button',i===bank?'on':'','BANK '+l);b.onclick=()=>{bank=i;buildPads()};banks.append(b)});
 const grid=$('#padgrid');grid.innerHTML='';const named={};for(const p of state.pads)named[p.pad]=p.name;
 for(let i=1;i<=16;i++){const n=bank*16+i,b=E('button',named[n]?'':'empty');b.append(E('span','',named[n]?shortName(named[n]):('ABCD'[bank]+i)));
  if(named[n]){const down=e=>{e.preventDefault();b.classList.add('down');post(`/api/pad/${n}?value=1`)},
   up=()=>{if(!b.classList.contains('down'))return;b.classList.remove('down');post(`/api/pad/${n}?value=0`)};
   b.addEventListener('pointerdown',down);b.addEventListener('pointerup',up);b.addEventListener('pointercancel',up);b.addEventListener('pointerleave',up)}
  grid.append(b)}}
function update(){
 for(const [kind,list] of [['input',state.inputs],['bus',state.buses],['output',state.outputs]])for(const s of list){
  const v=strips[kind+':'+s.name];if(!v)continue;v.el.classList.toggle('off',!s.on);v.m.classList.toggle('on',s.mute);
  if(v.so)v.so.classList.toggle('on',s.solo);if(!v.busy()){v.fader.value=Math.max(-60,s.fader);v.db.textContent=dbText(s.fader)}}
 document.querySelectorAll('[data-scene]').forEach(b=>b.classList.toggle('on',b.dataset.scene===state.activeScene));
 $('#scene').textContent=state.activeScene||''}
async function poll(){
 try{state=await get('/api/state');$('#dot').classList.add('on');applyTheme(state.theme);
  const s=JSON.stringify([state.inputs.map(x=>x.name),state.buses.map(x=>x.name),state.outputs.map(x=>x.name),state.scenes,state.macros,state.pads]);
  if(s!==shape){shape=s;build()}update()}
 catch(e){$('#dot').classList.remove('on');if(e===403&&!state)$('#mix').innerHTML='<div class="msg">Phone control is off, or this link is old. Scan the code again in Faider.</div>';
  if(e===429){shape='';$('#mix').innerHTML='<div class="msg">Too many wrong keys &mdash; wait a minute.</div>'}}
 setTimeout(poll,1000)}
async function meters(){
 try{const m=await get('/api/meters');for(const k in m){const v=strips[k];if(v)v.bar.style.width=Math.max(0,Math.min(100,(Math.max(m[k][0],m[k][1])+60)/72*100))+'%'}}catch(e){}
 setTimeout(meters,120)}
poll();meters();
</script></body></html>)PAGE";
}
