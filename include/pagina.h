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
.flujo{display:grid;grid-template-columns:1fr 100px 1fr;gap:10px;align-items:end}
@media (max-width:560px){.flujo{grid-template-columns:1fr 64px 1fr}}
.tq h3{margin:0 0 6px;font-size:.95rem}.tq p{margin:6px 0 0;font-size:.85rem;color:var(--sub)}
.tanque{position:relative;height:200px;border:3px solid var(--sub);border-top:none;border-radius:0 0 14px 14px;overflow:hidden}
.agua{position:absolute;bottom:0;left:0;right:0;background:var(--agua);transition:height .8s}
.lodo{position:absolute;bottom:0;left:0;right:0;background:var(--lodo);transition:height .8s}
.pct{position:absolute;inset:0;display:grid;place-items:center;font-size:1.6rem;font-weight:700;text-shadow:0 1px 3px #0008;color:#fff;text-align:center}
.paso{text-align:center;font-size:.72rem;color:var(--sub);padding-bottom:40px}
.paso .ic{font-size:1.35rem;display:block;opacity:.3;transition:opacity .3s}.paso .ic.on{opacity:1}
.barra{height:10px;border-radius:5px;background:linear-gradient(90deg,#d63c3c 0%,#d69e0b 30%,#1f9d55 46%,#1f9d55 54%,#d69e0b 57%,#d63c3c 75%);position:relative;margin:10px 0 2px}
.barra i{position:absolute;top:-4px;width:4px;height:18px;background:var(--txt);border-radius:2px;transition:left .8s}
.kpis{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}
.kpi b{display:block;font-size:1.25rem}.kpi span{color:var(--sub);font-size:.8rem;display:block}
canvas{width:100%;height:200px}
.ctrl{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:8px}
button{border:0;border-radius:8px;padding:9px 14px;font-weight:600;cursor:pointer;background:var(--agua);color:#fff}
button.sec{background:var(--sub)}input{padding:8px;border-radius:8px;border:1px solid var(--borde);width:90px;background:var(--card);color:var(--txt)}
table{border-collapse:collapse;font-size:.85rem;margin-top:6px}td,th{padding:3px 10px 3px 0;text-align:left}
</style></head><body>
<header><h1>💧 AquaAlerta</h1><small id="con">conectando…</small></header>
<main>
 <div id="banner" class="VERDE">Cargando…</div>

 <div class="card"><div class="flujo">
  <div class="tq"><h3>R1 · Coagulación y sedimentos</h3>
   <div class="tanque"><div class="agua" id="a1" style="height:85%"></div><div class="lodo" id="lodo1" style="height:0"></div>
    <div class="pct" id="p1">–</div></div>
   <p><b id="l1">–</b> L · <b id="sed">–</b> · <span id="n1">–</span> NTU<br><span id="s1">–</span></p></div>
  <div class="paso">
   <span class="ic" id="icC">🧪</span>PAC
   <span class="ic" id="icM">🌀</span>agitador
   <span class="ic" id="icP">🟤</span>purga
   <span class="ic" id="icB">⚙️</span>bomba
   <span class="ic" id="icF">🧽</span>filtro</div>
  <div class="tq"><h3>R2 · Agua tratada</h3>
   <div class="tanque"><div class="agua" id="a2" style="height:0"></div><div class="pct" id="p2">–</div></div>
   <p><b id="l2">–</b> L · pH <b id="ph">–</b> · <span id="n2">–</span> NTU<br><span id="s2">–</span></p></div>
 </div></div>

 <div class="kpis">
  <div class="card kpi"><span>Coagulación en R1</span><b id="bC">–</b><span id="mC"></span></div>
  <div class="card kpi"><span>Bomba de purga (lodo de R1)</span><b id="bP">–</b><span id="mP"></span></div>
  <div class="card kpi"><span>Bomba R1 → filtro → R2</span><b id="bT">–</b><span id="mT"></span></div>
  <div class="card kpi"><span>Gotero de cloro</span><b id="bG">–</b><span id="mG"></span></div>
 </div>

 <div class="card"><b>🌀 Coagulación con PAC en R1</b>
  <div class="kpis" style="margin-top:12px">
   <div class="kpi"><span>Etapa</span><b id="kE">–</b><span id="kEt"></span></div>
   <div class="kpi"><span>Turbidez medida (agua mezclada)</span><b id="kN">–</b><span id="kNt"></span></div>
   <div class="kpi"><span>Dosis de PAC</span><b id="kD">–</b><span id="kDt"></span></div>
   <div class="kpi"><span>PAC aplicado</span><b id="kA">–</b><span id="kAt"></span></div>
   <div class="kpi"><span>Agitador</span><b id="kR">–</b><span id="kRt"></span></div>
   <div class="kpi"><span>PAC en el bidón</span><b id="kB">–</b><span id="kBt"></span></div>
  </div>
  <div class="ctrl"><button onclick="enviar('tratar')">Tratar R1 ahora</button>
   <button class="sec" onclick="enviar('pac_recarga')">Recargué el PAC</button></div>
  <small>Cada vez que entra agua a R1: agita y mide la turbidez, calcula la dosis con
   C1·V1 = C2·V2 (V1 = ppm × litros ÷ concentración de la solución madre), mezcla rápido
   (coagulación), mezcla lento (floculación) y deja reposar (sedimentación). Después la
   bomba de purga saca el lodo. La tabla turbidez → ppm sale de la prueba de jarras.</small></div>

 <div class="card"><b>🧪 pH y cloro en R2</b>
  <div class="barra"><i id="phMarca" style="left:50%"></i></div>
  <small style="display:flex;justify-content:space-between"><span>0 ácido</span><span>7 neutro</span><span>14 básico</span></small>
  <div class="kpis" style="margin-top:12px">
   <div class="kpi"><span>Turbidez R2 (después del filtro)</span><b id="tB">–</b><span id="tT"></span></div>
   <div class="kpi"><span>pH actual</span><b id="phB">–</b><span id="phT"></span></div>
   <div class="kpi"><span>Agua por clorar</span><b id="cPend">–</b><span id="cPendT"></span></div>
   <div class="kpi"><span>Última dosis</span><b id="cDosis">–</b><span id="cDosisT"></span></div>
   <div class="kpi"><span>Cloro en el gotero</span><b id="cRest">–</b><span id="cRestT"></span></div>
  </div>
  <table><tr><th>pH</th><th>¿Clora?</th><th>Nota</th></tr>
   <tr><td id="t1"></td><td id="t1g"></td><td>agua ácida (corrosiva): avisa</td></tr>
   <tr><td id="t2"></td><td id="t2g"></td><td>el cloro funciona mejor</td></tr>
   <tr><td id="t3"></td><td id="t3g"></td><td>funciona, algo menos eficaz</td></tr>
   <tr><td id="t4"></td><td>no</td><td>el cloro no desinfecta bien: corregir pH</td></tr></table>
  <div class="ctrl"><button onclick="enviar('clorar')">Clorar todo R2</button>
   <button class="sec" onclick="enviar('recarga')">Recargué el cloro</button></div>
  <small>Antes de clorar se exige agua clara: menos de 5 NTU (OMS; ideal menos de 1 NTU). Con R2 turbia no se clora: hay que revisar el filtro.<br>
   Dosis del MSP Ecuador (Guía de Agua Segura): cloro comercial al 5 %, 1 gota por litro y esperar 30 min.
   La OMS pide pH menor a 8 para que el cloro desinfecte; con pH alto se corrige el pH, no se echa más cloro.</small></div>

 <div class="card"><b>Últimos 30 min</b> <small>— <span style="color:var(--agua)">nivel R2 %</span> ·
  <span style="color:#a07a4a">- - sedimentos R1</span> · <span style="color:#1f9d55">·· turbidez R2</span> · <span style="color:#8b5cf6">pH</span> ·
  franjas: <span style="color:#0e9aa7">agitador/PAC</span>, <span style="color:#a07a4a">purga</span>, <span style="color:var(--agua)">transferencia</span>, <span style="color:#8b5cf6">goteo</span></small>
  <canvas id="graf"></canvas></div>

 <div class="card"><b>Control</b>
  <div class="ctrl">PIN <input id="pin" type="password" inputmode="numeric"></div>
  <div class="ctrl"><b style="min-width:110px">Purga R1:</b>
   <button onclick="enviar('p_auto')">Automático</button><button onclick="enviar('p_on')">Encender</button>
   <button class="sec" onclick="enviar('p_off')">Apagar</button></div>
  <div class="ctrl"><b style="min-width:110px">Transferencia:</b>
   <button onclick="enviar('t_auto')">Automático</button><button onclick="enviar('t_on')">Encender</button>
   <button class="sec" onclick="enviar('t_off')">Apagar</button></div>
  <div class="ctrl"><button class="sec" onclick="enviar('reset')">Reiniciar fallas</button><small id="resp"></small></div>
  <small>Protecciones: no pasa agua a R2 mientras R1 se esté coagulando, tenga sedimentos, esté sin agua o R2 se esté clorando;
   corte al 95 %; si R2 no sube con la bomba encendida avisa «Filtro tapado?».</small></div>

 <div class="card"><b>Métricas</b><div class="kpis" style="margin-top:10px">
  <div class="kpi"><span>Lotes coagulados</span><b id="xC">–</b><span id="xCt"></span></div>
  <div class="kpi"><span>Purgas de lodo</span><b id="xP">–</b><span id="xPt"></span></div>
  <div class="kpi"><span>Agua usada en purga</span><b id="xPl">–</b><span>reutilizable para riego</span></div>
  <div class="kpi"><span>Litros tratados (a R2)</span><b id="xT">–</b></div>
  <div class="kpi"><span>Turbidez R1 → R2 (filtro)</span><b id="xF">–</b><span id="xFt"></span></div>
  <div class="kpi"><span>Dosis aplicadas</span><b id="xD">–</b><span id="xDt"></span></div>
  <div class="kpi"><span>Respuesta transferencia</span><b id="xR">–</b></div>
 </div></div>
</main>
<script>
const $=id=>document.getElementById(id);
try{$('pin').value=sessionStorage.getItem('pin')||''}catch(e){}
let tabla=false;
async function estado(){
 try{
  const d=await (await fetch('/api/estado')).json(),r1=d.r1,K=d.coag,P=d.purga,T=d.transfer,r2=d.r2,C=d.cloro,k=d.cfg;
  $('con').textContent='en línea · '+new Date().toLocaleTimeString();
  $('banner').className=d.alerta;$('banner').textContent=d.causa;
  // R1
  $('a1').style.height=!r1.agua?'6%':(r1.nivel_ok?Math.max(6,r1.nivel)+'%':'85%');
  $('l1').textContent=r1.nivel_ok?r1.litros:'¿?';
  $('lodo1').style.height=Math.min(40,r1.ntu/300*40)+'%';
  $('p1').textContent=['🙂','😐','🟤'][r1.sedimento];
  $('sed').textContent='Sedimentos: '+['pocos','medios','muchos'][r1.sedimento];$('n1').textContent=r1.ntu;
  $('s1').textContent=!r1.agua?'⚠ R1 sin agua':K.ocupado?K.estado+(K.falta_s>0?' · '+K.falta_s+' s':''):P.estado=='PULSO'?'Purgando lodo '+P.pulso+'/'+P.max_pulsos:
   P.estado=='PAUSA'?'Asentando '+P.pulso+'/'+P.max_pulsos:(r1.disponible?'✔ sin sedimentos, lista':r1.motivo);
  // R2
  $('a2').style.height=(r2.ok?r2.nivel:0)+'%';$('p2').textContent=r2.ok?r2.nivel.toFixed(0)+'%':'¿?';
  $('l2').textContent=r2.ok?r2.litros:'–';$('ph').textContent=r2.ph.toFixed(1);$('n2').textContent=r2.ntu.toFixed(1);
  $('a2').style.background=r2.clara?'':'#7d8f6a';
  $('tB').textContent=r2.ntu.toFixed(1)+' NTU';
  $('tT').textContent=!r2.clara?'⚠ turbia: no se clora, revisar filtro':(r2.ntu<=k.turb_ideal?'✔ muy clara':'✔ apta para clorar');
  $('xF').textContent=r1.ntu+' → '+r2.ntu.toFixed(1)+' NTU';
  $('xFt').textContent=r1.ntu>0?'−'+Math.max(0,Math.round((r1.ntu-r2.ntu)/r1.ntu*100))+' %':'';
  $('s2').textContent=C.estado+(C.falta_s>0?' · '+C.falta_s+' s':'');
  $('icC').classList.toggle('on',K.pac_on);$('icM').classList.toggle('on',K.rpm>0);
  $('icP').classList.toggle('on',P.on);$('icB').classList.toggle('on',T.on);$('icF').classList.toggle('on',T.on);
  // coagulación
  $('bC').textContent=K.estado;$('mC').textContent=K.ocupado&&K.falta_s>0?'faltan '+K.falta_s+' s':(K.pendiente?'agua nueva por tratar':'');
  $('kE').textContent=K.estado;$('kEt').textContent=K.falta_s>0?'faltan '+K.falta_s+' s':'';
  $('kN').textContent=K.ntu_medida<0?'–':K.ntu_medida+' NTU';
  $('kNt').textContent=K.fuera_tabla?'⚠ fuera de la tabla: hacer prueba de jarras':(K.sin_pac?'clara (< '+k.coag_min+' NTU): sin PAC':'');
  $('kD').textContent=K.ppm>0?K.ppm+' ppm':'–';
  $('kDt').textContent=K.sin_nivel?'⚠ sin nivel de R1: no se dosificó':(K.ppm>0?'para '+K.litros+' L de agua':'');
  $('kA').textContent=K.ml_objetivo>0?Math.round(K.ml_aplicados)+' / '+Math.round(K.ml_objetivo)+' ml':'–';
  $('kAt').textContent='solución madre '+(k.pac_stock/10000)+' % ('+k.pac_stock+' ppm)';
  $('kR').textContent=K.rpm>0?K.rpm+' RPM':'quieto';
  $('kRt').textContent=K.rpm==k.rpm_rapida?'mezcla rápida (coagulación)':K.rpm==k.rpm_lenta?'mezcla lenta (floculación)':'';
  $('kB').textContent=Math.round(K.ml_restantes)+' ml';$('kBt').textContent=Math.round(K.pct_restante)+' % del bidón';
  // bombas
  $('bP').textContent=P.falla?'FALLA':(P.on?'ENCENDIDA':'apagada');$('mP').textContent=(P.auto?'':'manual · ')+P.motivo;
  $('bT').textContent=T.falla?'FALLA':(T.on?'ENCENDIDA':'apagada');$('mT').textContent=(T.auto?'':'manual · ')+(T.falla||T.motivo);
  $('bG').textContent=C.goteo?'GOTEANDO':'cerrado';$('mG').textContent=C.goteo?Math.round(C.aplicadas)+' / '+Math.round(C.objetivo)+' gotas':C.estado;
  // pH y cloro
  $('phMarca').style.left='calc('+(r2.ph/14*100)+'% - 2px)';
  $('phB').textContent=r2.ph.toFixed(2);
  $('phT').textContent=['⚠ ácido: clora, pero corregir','✔ óptimo','✔ aceptable','⚠ alto: no clora, corregir'][r2.rango_ph];
  $('cPend').textContent=Math.round(C.pendientes_l)+' L';
  $('cPendT').textContent=C.pendientes_l>0&&r2.gotas_l>0?'≈ '+Math.round(C.pendientes_l*r2.gotas_l)+' gotas':'';
  $('cDosis').textContent=C.dosis?Math.round(C.objetivo)+' gotas':'–';
  $('cDosisT').textContent=C.dosis?'pH '+C.ph_dosis.toFixed(1)+' · '+C.gotas_l_usadas+' gotas/L · ≈ '+(C.objetivo/C.gotas_ml).toFixed(1)+' ml':'';
  $('cRest').textContent=Math.round(C.ml_restantes)+' ml';$('cRestT').textContent=Math.round(C.pct_restante)+' % de la botella';
  if(!tabla){tabla=true;
   const g='sí, '+k.gotas_l+' gota/L';
   $('t1').textContent='menos de '+k.ph_min;$('t1g').textContent=g;$('t2').textContent=k.ph_min+' – '+k.ph_opt;$('t2g').textContent=g;
   $('t3').textContent=k.ph_opt+' – '+k.ph_max;$('t3g').textContent=g;$('t4').textContent='más de '+k.ph_max;}
  // métricas
  $('xC').textContent=K.lotes;$('xCt').textContent=Math.round(K.ml_total)+' ml de PAC usados';
  $('xP').textContent=P.purgas;$('xPt').textContent=P.ntu_antes<0?'':'última: '+P.ntu_antes+' → '+P.ntu_despues+' NTU';
  $('xPl').textContent=P.litros+' L';$('xT').textContent=T.litros+' L';
  $('xD').textContent=C.dosis;$('xDt').textContent=Math.round(C.total)+' gotas ≈ '+Math.round(C.total/C.gotas_ml)+' ml';
  $('xR').textContent=T.respuesta_s<0?'–':T.respuesta_s.toFixed(1)+' s';
 }catch(e){$('con').textContent='sin conexión'}
}
async function historial(){
 try{
  const h=await (await fetch('/api/historial')).json(),c=$('graf'),x=c.getContext('2d');
  const W=c.width=c.clientWidth*devicePixelRatio,H=c.height=c.clientHeight*devicePixelRatio,n=h.datos.length;
  x.clearRect(0,0,W,H);if(n<2)return;
  const px=i=>i/(h.max-1)*W,w=W/(h.max-1)+1;
  // franjas: agitador/PAC y purga arriba, transferencia al medio, goteo abajo
  [[24,'#0e9aa7aa',0,.08],[1,'#a07a4a99',.08,.08],[2,'#2f8fd833',.16,.76],[4,'#8b5cf6aa',.92,.08]].forEach(([b,col,y,alto])=>{x.fillStyle=col;
   h.datos.forEach((p,i)=>{if(p[3]&b)x.fillRect(px(i),y*H,w,alto*H)})});
  const linea=(f,col,dash)=>{x.strokeStyle=col;x.lineWidth=2*devicePixelRatio;x.setLineDash(dash);x.beginPath();
   h.datos.forEach((p,i)=>{const y=H-f(p)*H;i?x.lineTo(px(i),y):x.moveTo(px(i),y)});x.stroke();x.setLineDash([])};
  linea(p=>p[0]/100,'#2f8fd8',[]);
  linea(p=>Math.min(p[1],300)/300,'#a07a4a',[6,4]);
  linea(p=>Math.min(p[4],300)/300,'#1f9d55',[2,3]);
  linea(p=>p[2]/140,'#8b5cf6',[]);
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
