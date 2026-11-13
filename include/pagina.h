// Página web que sirve el ESP32. Sin librerías externas: funciona sin internet
// (por ejemplo, conectado a la red propia "AquaAlerta").
#pragma once
#include <pgmspace.h>

const char PAGINA[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AquaAlerta</title>
<style>
:root{--bg:#f2f6f9;--card:#fff;--txt:#17252f;--sub:#5b6b77;--agua:#2f8fd8;--lodo:#a07a4a;--cloro:#8b5cf6;--ok:#1f9d55;--warn:#d69e0b;--bad:#d63c3c;--borde:#dbe3ea}
@media (prefers-color-scheme:dark){:root{--bg:#0f1820;--card:#18242e;--txt:#e6eef4;--sub:#93a4b1;--borde:#2a3945}}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--txt)}
header{padding:14px 16px;display:flex;justify-content:space-between;align-items:center;max-width:980px;margin:auto}
h1{font-size:1.25rem;margin:0}small{color:var(--sub)}
main{max-width:980px;margin:auto;padding:0 16px 24px;display:grid;gap:14px}
.card{background:var(--card);border:1px solid var(--borde);border-radius:12px;padding:14px}
#banner{font-weight:600;color:#fff;border-radius:12px;padding:12px 16px}
.VERDE{background:var(--ok)}.AMARILLO{background:var(--warn)}.ROJO{background:var(--bad)}
.flujo{display:grid;grid-template-columns:1fr 110px 1fr;gap:10px;align-items:end}
@media (max-width:560px){.flujo{grid-template-columns:1fr 70px 1fr}}
.tq h3{margin:0 0 6px;font-size:.95rem}.tq p{margin:6px 0 0;font-size:.85rem;color:var(--sub)}
.tanque{position:relative;height:200px;border:3px solid var(--sub);border-top:none;border-radius:0 0 14px 14px;overflow:hidden}
.agua{position:absolute;bottom:0;left:0;right:0;background:var(--agua);transition:height .8s,background .8s}
.lodo{position:absolute;bottom:0;left:0;right:0;height:8%;background:var(--lodo);opacity:.8}
.marca{position:absolute;left:0;right:0;border-top:2px dashed var(--sub);font-size:.65rem;color:var(--sub)}
.pct{position:absolute;inset:0;display:grid;place-items:center;font-size:1.7rem;font-weight:700;text-shadow:0 1px 3px #0008;color:#fff}
.paso{text-align:center;font-size:.75rem;color:var(--sub);padding-bottom:40px}
.paso .ic{font-size:1.4rem;display:block;opacity:.35;transition:opacity .3s}.paso .ic.on{opacity:1}
.kpis{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}
.kpi b{display:block;font-size:1.25rem}.kpi span{color:var(--sub);font-size:.8rem;display:block}
canvas{width:100%;height:200px}
.ctrl{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:8px}
button{border:0;border-radius:8px;padding:9px 14px;font-weight:600;cursor:pointer;background:var(--agua);color:#fff}
button.sec{background:var(--sub)}input{padding:8px;border-radius:8px;border:1px solid var(--borde);width:90px;background:var(--card);color:var(--txt)}
</style></head><body>
<header><h1>💧 AquaAlerta</h1><small id="con">conectando…</small></header>
<main>
 <div id="banner" class="VERDE">Cargando…</div>

 <div class="card"><div class="flujo">
  <div class="tq"><h3>R1 · Sedimentación</h3>
   <div class="tanque"><div class="agua" id="a1" style="height:0"></div><div class="lodo"></div>
    <div class="marca" id="m1">mín. succión</div><div class="pct" id="p1">–</div></div>
   <p><b id="l1">–</b> L · <span id="n1">–</span> NTU<br><span id="s1">–</span></p></div>
  <div class="paso">
   <span class="ic" id="icB">⚙️</span>bomba
   <span class="ic" id="icF">🧽</span>filtro
   <span class="ic" id="icG">💧</span>goteo cloro</div>
  <div class="tq"><h3>R2 · Agua tratada</h3>
   <div class="tanque"><div class="agua" id="a2" style="height:0"></div>
    <div class="marca" id="m2">rellena</div><div class="pct" id="p2">–</div></div>
   <p><b id="l2">–</b> L · <span id="n2">–</span> NTU<br><span id="s2">–</span></p></div>
 </div></div>

 <div class="kpis">
  <div class="card kpi"><span>Llenado (origen → R1)</span><b id="bL">–</b><span id="mL"></span></div>
  <div class="card kpi"><span>Transferencia (R1 → filtro → R2)</span><b id="bT">–</b><span id="mT"></span></div>
  <div class="card kpi"><span>Gotero de cloro</span><b id="bG">–</b><span id="mG"></span></div>
  <div class="card kpi"><span>Origen</span><b id="ori">–</b></div>
 </div>

 <div class="card"><b>🧪 Cloración de R2</b>
  <div class="kpis" style="margin-top:10px">
   <div class="kpi"><span>Último llenado de R2</span><b id="gC">–</b><span id="gCt"></span></div>
   <div class="kpi"><span>Dosis aplicada</span><b id="gD">–</b><span id="gDt"></span></div>
   <div class="kpi"><span>Cloro en el gotero</span><b id="gR">–</b><span id="gRt"></span></div>
   <div class="kpi"><span>Si hay que clorar R2 a mano</span><b id="gM">–</b><span id="gMt"></span></div>
  </div>
  <div class="ctrl"><button class="sec" onclick="enviar('recarga')">Recargué el cloro</button></div>
  <small>El gotero se abre solo mientras pasa agua a R2, así la dosis es proporcional (<span id="gRate"></span>).
   Cloro de uso doméstico (hipoclorito 5–6 %, sin aroma), 2 gotas por litro. Confirme la dosis con la guía del Ministerio de Salud y la etiqueta del producto.</small></div>

 <div class="card"><b>Últimos 30 min</b> <small>— <span style="color:#a07a4a">R1</span> · <span style="color:var(--agua)">R2</span> (%) ·
  franjas: <span style="color:var(--agua)">transferencia</span>, <span style="color:#8b5cf6">goteo</span></small><canvas id="graf"></canvas></div>

 <div class="card"><b>Control</b>
  <div class="ctrl">PIN <input id="pin" type="password" inputmode="numeric"></div>
  <div class="ctrl"><b style="min-width:110px">Llenado R1:</b>
   <button onclick="enviar('l_auto')">Automático</button><button onclick="enviar('l_on')">Encender</button>
   <button class="sec" onclick="enviar('l_off')">Apagar</button></div>
  <div class="ctrl"><b style="min-width:110px">Transferencia:</b>
   <button onclick="enviar('t_auto')">Automático</button><button onclick="enviar('t_on')">Encender</button>
   <button class="sec" onclick="enviar('t_off')">Apagar</button></div>
  <div class="ctrl"><button class="sec" onclick="enviar('reset')">Reiniciar fallas</button><small id="resp"></small></div>
  <small>Protecciones que valen también en manual: corte al 95 %, no transfiere hasta que R1 termine de sedimentar ni por debajo del lodo,
   y si R2 no sube con la bomba encendida avisa «Filtro tapado?».</small></div>

 <div class="card"><b>Métricas</b><div class="kpis" style="margin-top:10px">
  <div class="kpi"><span>Litros que entraron a R1</span><b id="xL">–</b></div>
  <div class="kpi"><span>Litros tratados (a R2)</span><b id="xT">–</b></div>
  <div class="kpi"><span>Turbidez R1 → R2</span><b id="xN">–</b><span id="xNt"></span></div>
  <div class="kpi"><span>Respuesta transferencia</span><b id="xR">–</b></div>
  <div class="kpi"><span>Llenados sin desborde</span><b id="xC">–</b></div>
  <div class="kpi"><span>Litros ahorrados (est.)</span><b id="xA">–</b></div>
 </div></div>
</main>
<script>
const $=id=>document.getElementById(id);
try{$('pin').value=sessionStorage.getItem('pin')||''}catch(e){}
const tt=s=>s>=3600?Math.floor(s/3600)+' h '+Math.floor(s/60%60)+' min':s>=60?Math.floor(s/60)+' min '+(s%60)+' s':s+' s';
const est=b=>b.falla?'FALLA':(b.on?'ENCENDIDA':'apagada');
async function estado(){
 try{
  const d=await (await fetch('/api/estado')).json(),r1=d.r1,r2=d.r2,L=d.llenado,T=d.transfer,G=d.goteo,u=d.umbrales;
  $('con').textContent='en línea · '+new Date().toLocaleTimeString();
  $('banner').className=d.alerta;$('banner').textContent=d.causa;
  $('a1').style.height=(r1.ok?r1.nivel:0)+'%';$('a1').style.background=r1.turbidez?'#7d8f6a':'';
  $('p1').textContent=r1.ok?r1.nivel.toFixed(0)+'%':'¿?';$('l1').textContent=r1.ok?r1.litros:'–';$('n1').textContent=Math.round(r1.ntu);
  $('m1').style.bottom=u.r1_min+'%';
  $('s1').textContent=r1.reposo_ok?(r1.disponible?'✔ sedimentada, lista':r1.motivo):'⏳ sedimentando · faltan '+tt(r1.reposo_falta_s);
  $('a2').style.height=(r2.ok?r2.nivel:0)+'%';$('a2').style.background=r2.turbidez?'#7d8f6a':'';
  $('p2').textContent=r2.ok?r2.nivel.toFixed(0)+'%':'¿?';$('l2').textContent=r2.ok?r2.litros:'–';$('n2').textContent=Math.round(r2.ntu);
  $('m2').style.bottom=u.r2_on+'%';
  $('s2').textContent=G.contacto_ok?'✔ lista para usar':'⏳ cloro actuando · '+tt(G.contacto_falta_s);
  $('icB').classList.toggle('on',T.on);$('icF').classList.toggle('on',T.on);$('icG').classList.toggle('on',G.on);
  $('bL').textContent=est(L);$('mL').textContent=(L.auto?'':'manual · ')+(L.falla||L.motivo);
  $('bT').textContent=est(T);$('mT').textContent=(T.auto?'':'manual · ')+(T.falla||T.motivo);
  $('bG').textContent=G.on?'GOTEANDO':'cerrado';$('mG').textContent=Math.round(G.gotas_total)+' gotas en total';
  $('ori').textContent=d.origen?'con agua':'SIN AGUA';
  $('gC').textContent=Math.round(G.gotas_ciclo)+' gotas';$('gCt').textContent='≈ '+(G.gotas_ciclo/G.gotas_ml).toFixed(1)+' ml';
  $('gD').textContent=G.dosis_pct<0?'–':Math.round(G.dosis_pct)+' %';
  $('gDt').textContent=G.dosis_pct<0?'aún sin ciclo':(Math.abs(G.dosis_pct-100)<=20?'✔ correcta':'⚠ calibrar el gotero');
  $('gR').textContent=Math.round(G.ml_restantes)+' ml';$('gRt').textContent=Math.round(G.pct_restante)+' % de la botella';
  $('gM').textContent=Math.round(G.gotas_r2_completo)+' gotas';$('gMt').textContent='≈ '+Math.round(G.gotas_r2_completo/G.gotas_ml)+' ml para '+r2.litros+' L';
  $('gRate').textContent=G.gotas_min+' gotas/min';
  $('xL').textContent=L.litros+' L';$('xT').textContent=T.litros+' L';
  $('xN').textContent=Math.round(r1.ntu)+' → '+Math.round(r2.ntu)+' NTU';
  $('xNt').textContent=r1.ntu>0?'−'+Math.max(0,Math.round((r1.ntu-r2.ntu)/r1.ntu*100))+' %':'';
  $('xR').textContent=T.respuesta_s<0?'–':T.respuesta_s.toFixed(1)+' s';
  $('xC').textContent=L.completos+T.completos;
  $('xA').textContent=d.linea_base>0?Math.round(L.completos*d.linea_base)+' L':'falta línea base';
 }catch(e){$('con').textContent='sin conexión'}
}
async function historial(){
 try{
  const h=await (await fetch('/api/historial')).json(),c=$('graf'),x=c.getContext('2d');
  const W=c.width=c.clientWidth*devicePixelRatio,H=c.height=c.clientHeight*devicePixelRatio,n=h.datos.length;
  x.clearRect(0,0,W,H);if(n<2)return;
  const px=i=>i/(h.max-1)*W,py=v=>H-v/100*H,w=W/(h.max-1)+1;
  x.fillStyle='#2f8fd833';h.datos.forEach((p,i)=>{if(p[4]&2)x.fillRect(px(i),0,w,H*.88)});
  x.fillStyle='#8b5cf6aa';h.datos.forEach((p,i)=>{if(p[4]&4)x.fillRect(px(i),H*.9,w,H*.1)});
  [[0,'#a07a4a'],[1,'#2f8fd8']].forEach(([k,col])=>{
   x.strokeStyle=col;x.lineWidth=2*devicePixelRatio;x.beginPath();
   h.datos.forEach((p,i)=>i?x.lineTo(px(i),py(p[k])):x.moveTo(px(i),py(p[k])));x.stroke()});
 }catch(e){}
}
async function enviar(accion){
 const pin=$('pin').value;try{sessionStorage.setItem('pin',pin)}catch(e){}
 const r=await fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
  body:'accion='+accion+'&pin='+encodeURIComponent(pin)});
 $('resp').textContent=r.ok?'✔ listo':(r.status==403?'PIN incorrecto':'error');estado();
}
estado();historial();setInterval(estado,2000);setInterval(historial,10000);
</script></body></html>)rawliteral";
