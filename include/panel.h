// Panel de red (fase 2): varios aljibes en un mapa. Solo el nodo 1 es real
// (lee /api/estado de este ESP32); los demás son SIMULADOS y así se rotulan.
// Sirve para mostrar en el pitch cómo escalaría AquaAlerta en el territorio.
// Sin librerías externas: funciona sin internet. Abrir en http://<ip>/panel
// (o como archivo local con ?esp=192.168.4.1).
#pragma once
#include <pgmspace.h>

const char PANEL[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Red AquaAlerta</title>
<style>
:root{--bg:#f2f6f9;--card:#fff;--txt:#17252f;--sub:#5b6b77;--agua:#2f8fd8;--lodo:#a07a4a;--cloro:#8b5cf6;--ok:#1f9d55;--warn:#d69e0b;--bad:#d63c3c;--gris:#8795a1;--borde:#dbe3ea;--mapa:#e8f0e4;--rio:#9cc8ea;--via:#cfd6cc}
@media (prefers-color-scheme:dark){:root{--bg:#0f1820;--card:#18242e;--txt:#e6eef4;--sub:#93a4b1;--borde:#2a3945;--mapa:#1b2a24;--rio:#24527a;--via:#34443d}}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--txt)}
header{padding:14px 16px;display:flex;justify-content:space-between;align-items:center;gap:10px;max-width:1100px;margin:auto}
h1{font-size:1.25rem;margin:0}h2{font-size:1rem;margin:0 0 10px}small{color:var(--sub)}
a{color:var(--agua);font-weight:600;text-decoration:none}
main{max-width:1100px;margin:auto;padding:0 16px 24px;display:grid;gap:14px}
.card{background:var(--card);border:1px solid var(--borde);border-radius:12px;padding:14px;min-width:0}
.aviso{border-left:5px solid var(--cloro);font-size:.9rem;line-height:1.45}
.aviso label{display:block;margin-top:6px;color:var(--sub);cursor:pointer}
.tag{display:inline-block;font-size:.68rem;font-weight:700;letter-spacing:.04em;padding:2px 6px;border-radius:5px;vertical-align:middle;color:#fff}
.vivo{background:var(--ok)}.sim{background:var(--cloro)}
.kpis{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.kpi b{display:block;font-size:1.4rem}.kpi span{color:var(--sub);font-size:.8rem;display:block}
.dos{display:grid;grid-template-columns:1.2fr 1fr;gap:14px}
@media (max-width:760px){.dos{grid-template-columns:1fr}}
svg{width:100%;height:auto;display:block;border-radius:10px}
.nodo{cursor:pointer}.nodo text{font-size:11px;font-weight:700;fill:#fff;pointer-events:none}
.nodo .nom{fill:var(--txt);font-weight:600;font-size:10px}
.sel circle.p{stroke:var(--txt);stroke-width:3}
@keyframes pulso{0%{r:11;opacity:.7}100%{r:24;opacity:0}}
.onda{animation:pulso 1.8s ease-out infinite;fill:none;stroke:var(--ok);stroke-width:2}
.est{display:inline-block;padding:3px 9px;border-radius:7px;color:#fff;font-weight:600;font-size:.85rem}
.VERDE{background:var(--ok)}.AMARILLO{background:var(--warn)}.ROJO{background:var(--bad)}.GRIS{background:var(--gris)}
.vals{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin:12px 0}
.vals div{border:1px solid var(--borde);border-radius:8px;padding:8px}
.vals span{display:block;font-size:.75rem;color:var(--sub)}.vals b{font-size:1.1rem}
.graf{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}
.graf div span{font-size:.75rem;color:var(--sub)}canvas{width:100%;height:56px;display:block}
.tabla{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:.85rem;white-space:nowrap}
th,td{padding:7px 10px 7px 0;text-align:left;border-bottom:1px solid var(--borde)}th{color:var(--sub);font-weight:600}
tbody tr{cursor:pointer}tbody tr:hover{background:var(--bg)}
.bar{display:inline-block;width:46px;height:7px;border-radius:4px;background:var(--borde);vertical-align:middle;margin-right:5px;overflow:hidden}
.bar i{display:block;height:100%}
</style></head><body>
<header><div><h1>💧 Red AquaAlerta</h1><small>Aljibes rurales · Calceta, Manabí</small></div>
<a href="/" id="lnk">Panel del prototipo ›</a></header>
<main>
 <div class="card aviso"><b>Demostración de fase 2.</b> Solo el aljibe <span class="tag vivo">EN VIVO</span> es el
  prototipo real conectado ahora. Los marcados <span class="tag sim">SIMULADO</span> tienen datos de ejemplo
  para mostrar cómo se vería la red al instalar AquaAlerta en más aljibes.
  <label><input type="checkbox" id="soloReal"> Mostrar solo el aljibe real</label></div>

 <div class="card kpis">
  <div class="kpi"><span>Aljibes en línea</span><b id="kOn">–</b></div>
  <div class="kpi"><span>Agua segura (verde)</span><b id="kOk">–</b></div>
  <div class="kpi"><span>Con aviso / alarma</span><b id="kAl">–</b></div>
  <div class="kpi"><span>Litros tratados</span><b id="kL">–</b></div>
  <div class="kpi"><span>Personas abastecidas por 1 día</span><b id="kP">–</b><span>a 20 L por persona</span></div>
  <div class="kpi"><span>Insumos por reponer</span><b id="kI">–</b><span id="kIt"></span></div>
 </div>

 <div class="dos">
  <div class="card"><h2>Mapa de aljibes</h2>
   <svg id="mapa" viewBox="0 0 400 260" role="img" aria-label="Mapa esquemático de los aljibes">
    <rect width="400" height="260" fill="var(--mapa)"/>
    <path d="M-10 40 C60 60 110 20 170 70 S260 160 330 150 S390 200 410 230" fill="none" stroke="var(--rio)" stroke-width="14" stroke-linecap="round"/>
    <text x="345" y="185" font-size="10" fill="var(--sub)" font-style="italic">río</text>
    <path d="M200 130 L120 60 M200 130 L320 90 M200 130 L250 215 M200 130 L60 170 M320 90 L300 30" stroke="var(--via)" stroke-width="4" fill="none"/>
    <circle cx="200" cy="130" r="30" fill="var(--via)" opacity=".55"/>
    <text x="200" y="98" text-anchor="middle" font-size="11" fill="var(--sub)" font-weight="600">Calceta</text>
    <g id="nodos"></g>
   </svg>
   <small>Esquema, no a escala. Toca un aljibe para ver el detalle.</small></div>
  <div class="card" id="det"></div>
 </div>

 <div class="card"><h2>Todos los aljibes</h2><div class="tabla"><table>
  <thead><tr><th>Aljibe</th><th>Estado</th><th>Turbidez R1</th><th>TDS</th><th>pH</th><th>Turbidez R2</th><th>R2</th><th>PAC</th><th>Cloro</th><th>Visto</th></tr></thead>
  <tbody id="filas"></tbody></table></div></div>
</main>
<script>
const q=new URLSearchParams(location.search).get('esp');
const BASE=q?(q.startsWith('http')?q:'http://'+q):'';
if(BASE)document.getElementById('lnk').href=BASE+'/';
const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const MAXH=180;  // 30 min a un punto cada 10 s, igual que el historial del ESP32

// Nombres y posiciones: cámbienlos por los sitios reales cuando se instalen.
const N=[
 {id:1,nom:'Aljibe 1 · Prototipo',sec:'Demostración en la feria',x:200,y:130,real:true},
 {id:2,nom:'Aljibe 2',sec:'Sector norte',x:120,y:60,tipo:'ok'},
 {id:3,nom:'Aljibe 3',sec:'Sector este',x:320,y:90,tipo:'pac'},
 {id:4,nom:'Aljibe 4',sec:'Sector sur',x:250,y:215,tipo:'filtro'},
 {id:5,nom:'Aljibe 5',sec:'Sector oeste',x:60,y:170,tipo:'off'},
 {id:6,nom:'Aljibe 6',sec:'Estero (agua salobre)',x:300,y:30,tipo:'coag'}];

// Casos simulados: cada uno muestra una situación distinta que detecta AquaAlerta.
const BASES={
 ok:    {alerta:'VERDE',causa:'Agua lista',ntu1:18,tds:310,ph:7.0,ntu2:1.2,r2:82,pac:64,cloro:71,litros:940},
 pac:   {alerta:'AMARILLO',causa:'Recargar PAC',ntu1:35,tds:420,ph:6.9,ntu2:2.1,r2:66,pac:12,cloro:48,litros:610},
 filtro:{alerta:'ROJO',causa:'Filtro no limpia',ntu1:22,tds:380,ph:7.2,ntu2:14,r2:40,pac:55,cloro:60,litros:380},
 off:   {alerta:'VERDE',causa:'Agua lista',ntu1:20,tds:290,ph:7.1,ntu2:1.5,r2:74,pac:58,cloro:66,litros:520},
 coag:  {alerta:'AMARILLO',causa:'Agua salobre',ntu1:160,tds:1450,ph:7.1,ntu2:0.9,r2:55,pac:70,cloro:80,litros:720}};

const lim=(v,a,b)=>Math.max(a,Math.min(b,v));
// Ruido alrededor del valor base del caso (vuelve hacia él: no se desvía con el tiempo).
const ruido=(v,b,a)=>v+(b-v)*.08+(Math.random()-.5)*a;
function paso(s,t){
 const b=BASES[t];
 s.ntu1=lim(ruido(s.ntu1,b.ntu1,b.ntu1*.06+1),0,3000);
 s.tds=lim(ruido(s.tds,b.tds,b.tds*.02),0,5000);
 s.ph=lim(ruido(s.ph,b.ph,.06),5.5,9);
 s.ntu2=lim(ruido(s.ntu2,b.ntu2,t==='filtro'?1.2:.15),0,50);
 s.r2=lim(ruido(s.r2,b.r2,.8),8,92);
 if(t==='ok'&&Math.random()<.05)s.litros+=Math.round(5+Math.random()*10);
}
function guardar(n){
 n.h.push({ntu1:n.s.ntu1,ntu2:n.s.ntu2,ph:n.s.ph,r2:n.s.r2,tds:n.s.tds});
 if(n.h.length>MAXH)n.h.shift();
}
const ahora=Date.now();
N.forEach(n=>{
 n.h=[];
 if(n.real){n.s=null;n.online=false;n.visto=0;return;}
 n.s=Object.assign({},BASES[n.tipo]);
 for(let i=0;i<MAXH;i++){paso(n.s,n.tipo);guardar(n);}
 n.online=n.tipo!=='off';
 n.visto=n.online?ahora:ahora-3*3600e3-17*60e3;
});

function leer(k){try{return localStorage.getItem(k)}catch(e){return null}}
function escribir(k,v){try{localStorage.setItem(k,v)}catch(e){}}
let sel=1;
$('soloReal').checked=leer('soloReal')==='1';
$('soloReal').onchange=e=>{escribir('soloReal',e.target.checked?'1':'0');if(e.target.checked)sel=1;pintar();};
const visibles=()=>N.filter(n=>n.real||!$('soloReal').checked);

function hace(ms){
 if(!ms)return 'nunca';
 const s=Math.round((Date.now()-ms)/1000);
 if(s<10)return 'ahora';if(s<60)return 'hace '+s+' s';
 const m=Math.round(s/60);if(m<60)return 'hace '+m+' min';
 const h=Math.floor(m/60);return 'hace '+h+' h '+(m%60)+' min';
}
const color=n=>!n.online||!n.s?'GRIS':n.s.alerta;
const causa=n=>!n.s?'Sin datos todavía':(!n.online?'Sin conexión':n.s.causa);
const css=v=>getComputedStyle(document.documentElement).getPropertyValue(v).trim();
const COL={VERDE:'--ok',AMARILLO:'--warn',ROJO:'--bad',GRIS:'--gris'};
const f=(v,d)=>v==null||isNaN(v)?'–':(+v).toFixed(d);
const barra=(p,c)=>p==null?'–':`<span class="bar"><i style="width:${lim(p,0,100)}%;background:var(${p<20?'--bad':c})"></i></span>${Math.round(p)}%`;
const etiqueta=n=>n.real?'<span class="tag vivo">EN VIVO</span>':'<span class="tag sim">SIMULADO</span>';

async function leerReal(){
 const n=N[0];
 try{
  const c=new AbortController();const to=setTimeout(()=>c.abort(),3000);
  const d=await (await fetch(BASE+'/api/estado',{signal:c.signal})).json();clearTimeout(to);
  n.s={alerta:d.alerta,causa:d.causa,ntu1:d.r1.ntu,tds:d.r1.tds,ph:d.r2.ph,ntu2:d.r2.ntu,r2:d.r2.nivel,
       pac:d.coag.pct_restante,cloro:d.cloro.pct_restante,litros:d.transfer.litros};
  n.online=true;n.visto=Date.now();
 }catch(e){n.online=false;}
}
async function histReal(){
 try{
  const d=await (await fetch(BASE+'/api/historial')).json();
  // cada punto: [nivel R2 %, NTU R1, pH×10, bits de estado, NTU R2, TDS R1]
  N[0].h=d.datos.slice(-MAXH).map(p=>({r2:p[0],ntu1:p[1],ph:p[2]/10,ntu2:p[4],tds:p[5]}));
 }catch(e){}
}

function pintar(){
 const vs=visibles();
 const on=vs.filter(n=>n.online&&n.s);
 $('kOn').textContent=on.length+' / '+vs.length;
 $('kOk').textContent=on.filter(n=>n.s.alerta==='VERDE').length;
 $('kAl').textContent=on.filter(n=>n.s.alerta==='AMARILLO').length+' / '+on.filter(n=>n.s.alerta==='ROJO').length;
 const L=vs.reduce((a,n)=>a+(n.s?n.s.litros||0:0),0);
 $('kL').textContent=Math.round(L).toLocaleString('es');
 $('kP').textContent=Math.floor(L/20).toLocaleString('es');
 const rep=vs.filter(n=>n.s&&(n.s.pac<20||n.s.cloro<20));
 $('kI').textContent=rep.length;
 $('kIt').textContent=rep.map(n=>'#'+n.id+(n.s.pac<20?' PAC':'')+(n.s.cloro<20?' cloro':'')).join(', ');

 let g='';
 vs.forEach(n=>{
  const c=css(COL[color(n)]);
  g+=`<g class="nodo${n.id===sel?' sel':''}" data-id="${n.id}">`+
     (n.real&&n.online?`<circle class="onda" cx="${n.x}" cy="${n.y}" r="11"/>`:'')+
     `<circle class="p" cx="${n.x}" cy="${n.y}" r="11" fill="${c}"${n.online?'':' stroke-dasharray="3 2" stroke="var(--sub)" stroke-width="2"'}/>`+
     `<text x="${n.x}" y="${n.y+4}" text-anchor="middle">${n.id}</text>`+
     `<text class="nom" x="${n.x}" y="${n.y+25}" text-anchor="middle">${n.real?'EN VIVO':'simulado'}</text></g>`;
 });
 $('nodos').innerHTML=g;

 $('filas').innerHTML=vs.map(n=>{const s=n.s||{};return `<tr data-id="${n.id}"><td><b>${esc(n.nom)}</b> ${etiqueta(n)}<br><small>${esc(n.sec)}</small></td>`+
  `<td><span class="est ${color(n)}">${esc(causa(n))}</span></td><td>${f(s.ntu1,0)} NTU</td><td>${f(s.tds,0)} ppm</td><td>${f(s.ph,1)}</td><td>${f(s.ntu2,1)} NTU</td>`+
  `<td>${barra(s.r2,'--agua')}</td><td>${barra(s.pac,'--lodo')}</td><td>${barra(s.cloro,'--cloro')}</td><td>${hace(n.visto)}</td></tr>`}).join('');

 detalle();
}

function detalle(){
 const n=N.find(x=>x.id===sel)||N[0],s=n.s||{};
 $('det').innerHTML=`<h2>${esc(n.nom)} ${etiqueta(n)}</h2>`+
  `<small>${esc(n.sec)} · ${hace(n.visto)}</small><p><span class="est ${color(n)}">${esc(causa(n))}</span></p>`+
  `<div class="vals"><div><span>Turbidez R1 (sedimentos)</span><b>${f(s.ntu1,0)} NTU</b></div>`+
  `<div><span>Turbidez R2 (tras el filtro)</span><b>${f(s.ntu2,1)} NTU</b></div>`+
  `<div><span>Sales disueltas (TDS) R1</span><b>${f(s.tds,0)} ppm</b></div>`+
  `<div><span>pH en R2</span><b>${f(s.ph,1)}</b></div><div><span>Nivel R2</span><b>${f(s.r2,0)} %</b></div>`+
  `<div><span>PAC / cloro</span><b>${f(s.pac,0)} % / ${f(s.cloro,0)} %</b></div>`+
  `<div><span>Litros tratados</span><b>${f(s.litros,0)} L</b></div></div>`+
  `<div class="graf"><div><span>Turbidez R1 · 30 min</span><canvas id="g1"></canvas></div>`+
  `<div><span>Turbidez R2 · 30 min</span><canvas id="g2"></canvas></div>`+
  `<div><span>pH · 30 min</span><canvas id="g3"></canvas></div><div><span>Nivel R2 · 30 min</span><canvas id="g4"></canvas></div></div>`+
  (n.real?`<p><a href="${BASE}/">Abrir el panel completo del prototipo ›</a></p>`:
          `<p><small>Datos de ejemplo: este aljibe todavía no existe.</small></p>`);
 const h=n.h;
 linea($('g1'),h.map(p=>p.ntu1),css('--lodo'));
 linea($('g2'),h.map(p=>p.ntu2),css('--cloro'),0);
 linea($('g3'),h.map(p=>p.ph),css('--ok'),5,9);
 linea($('g4'),h.map(p=>p.r2),css('--agua'),0,100);
}

function linea(cv,v,col,mn,mx){
 const r=devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;
 cv.width=w*r;cv.height=h*r;const c=cv.getContext('2d');c.scale(r,r);
 c.fillStyle=css('--bg');c.fillRect(0,0,w,h);
 if(v.length<2){c.fillStyle=css('--sub');c.font='11px system-ui';c.fillText('sin historial',6,h/2+4);return;}
 const lo=mn!=null?mn:Math.min(...v),hi=mx!=null?mx:Math.max(...v),sp=(hi-lo)||1;
 c.strokeStyle=col;c.lineWidth=2;c.beginPath();
 v.forEach((y,i)=>{const X=i/(v.length-1)*w,Y=h-3-(lim(y,lo,hi)-lo)/sp*(h-6);i?c.lineTo(X,Y):c.moveTo(X,Y)});
 c.stroke();
}

function elegir(e){const t=e.target.closest('[data-id]');if(t){sel=+t.dataset.id;pintar();}}
$('nodos').addEventListener('click',elegir);$('filas').addEventListener('click',elegir);

let ciclo=0;
async function tick(){
 await leerReal();
 if(ciclo%5===0)await histReal();
 N.forEach(n=>{if(!n.real&&n.online){paso(n.s,n.tipo);n.visto=Date.now();if(ciclo%5===0)guardar(n);}});
 ciclo++;pintar();
}
tick();setInterval(tick,2000);
</script></body></html>)rawliteral";
