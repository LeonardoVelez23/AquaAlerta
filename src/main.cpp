// AquaAlerta – tratamiento de agua en dos reservorios:
//   R1: coagula con PAC, flocula, sedimenta y purga el lodo → bomba + filtro →
//   R2: mide pH y dosifica cloro.
// ODS 6 (agua limpia) · secundario ODS 13 (resiliencia ante sequía).
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "control.h"
#include "pagina.h"
#include "panel.h"
#include "secrets.h"

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
WebServer server(80);

Preferences prefs;
Coagulacion coag;
ControlValvula valvula;
ControlPurga purga;
ControlBomba transfer;
Cloracion cloro;
MediaMovil<MUESTRAS_PROMEDIO> mNivel, mNivel1, mNtu, mNtu2, mPh;

struct {
  float ntuR1 = NAN;        // sedimentos en R1
  bool r1Agua = true;       // flotador de R1
  float nivelR1 = NAN;      // % de R1 (ultrasonido)
  bool r1NivelOk = false;
  float distancia = NAN, nivel = NAN, ph = NAN, ntuR2 = NAN;  // R2
  bool r2Ok = false;
  float litrosR1() const { return r1NivelOk ? nivelR1 / 100 * R1_CAPACIDAD_L : 0; }
  float litrosR2() const { return r2Ok ? nivel / 100 * R2_CAPACIDAD_L : 0; }
} m;
int fallosNivel = FALLOS_NIVEL_ALERTA;  // hasta la primera lectura válida
int fallosNivelR1 = FALLOS_NIVEL_ALERTA;
const char* motivoR1 = "";              // por qué R1 no entrega agua

Diagnostico diag = {VERDE, "Iniciando"};
bool modoAP = false;

// bits de estado: 1 purga, 2 transferencia, 4 goteo, 8 agitador, 16 bomba PAC, 32 válvula R1
struct Punto { uint8_t nivel; uint16_t ntu; uint16_t ntu2; uint8_t ph10; uint8_t estado; };
Punto historial[HISTORIAL_PUNTOS];
int histInicio = 0, histCantidad = 0;

unsigned long tLectura = 0, tPantalla = 0, tHistorial = 0, silencioHasta = 0;
int pantalla = 0;

// ---------- Sensores ----------
float pingCm(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long us = pulseIn(echo, HIGH, 30000);  // ~5 m máximo
  float cm = us / 58.0f;
  return (us == 0 || cm < DIST_MIN_VALIDA_CM || cm > DIST_MAX_VALIDA_CM) ? NAN : cm;
}

// Espera atendiendo la página: la lectura de los ultrasonidos no la congela.
void esperarAtendiendo(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    server.handleClient();
    delay(1);
  }
}

// Mediana de varios pings: un eco falso (pared, gota) no mueve el resultado.
float distanciaMediana(int trig, int echo) {
  float v[PINGS_POR_LECTURA];
  int n = 0;
  for (int i = 0; i < PINGS_POR_LECTURA; i++) {
    float d = pingCm(trig, echo);
    if (!isnan(d)) v[n++] = d;
    esperarAtendiendo(30);  // deja apagar los ecos del ping anterior
  }
  if (n < PINGS_POR_LECTURA / 2 + 1) return NAN;
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { float t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
  return v[n / 2];
}

float voltios(int pin, float divisor) {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(pin);
  return mv / 16.0f / 1000.0f * divisor;
}

void leerSensores() {
  m.ntuR1 = mNtu.agregar(ntuDesdeVoltaje(voltios(PIN_TURB_R1, TURB_DIVISOR)));
  m.r1Agua = digitalRead(PIN_FLOTADOR_R1) == LOW;
  float d1 = distanciaMediana(PIN_TRIG_R1, PIN_ECHO_R1);
  if (isnan(d1)) {
    if (++fallosNivelR1 == FALLOS_NIVEL_ALERTA) Serial.println("R1: sensor de nivel sin respuesta");
  } else {
    fallosNivelR1 = 0;
    m.nivelR1 = mNivel1.agregar(nivelR1DesdeDistancia(d1));
  }
  m.r1NivelOk = fallosNivelR1 < FALLOS_NIVEL_ALERTA;

  float d = distanciaMediana(PIN_TRIG_R2, PIN_ECHO_R2);
  if (isnan(d)) {
    if (++fallosNivel == FALLOS_NIVEL_ALERTA) Serial.println("R2: sensor de nivel sin respuesta");
  } else {
    fallosNivel = 0;
    m.distancia = d;
    m.nivel = mNivel.agregar(nivelDesdeDistancia(d));
  }
  m.r2Ok = fallosNivel < FALLOS_NIVEL_ALERTA;
  m.ph = mPh.agregar(phDesdeVoltaje(voltios(PIN_PH_R2, PH_DIVISOR)));
  // Calibrado con agua embotellada: ese voltaje queda como 0 NTU (tope de la curva, 4,2 V).
  float v2 = voltios(PIN_TURB_R2, TURB_DIVISOR) + (4.2f - TURB_R2_V_LIMPIA);
  m.ntuR2 = mNtu2.agregar(ntuDesdeVoltaje(v2));
}

// ¿R1 puede entregar agua a R2?
bool r1Disponible() {
  if (!m.r1Agua) { motivoR1 = "R1 sin agua"; return false; }
  if (valvula.abierta) { motivoR1 = "Llenando R1"; return false; }
  if (!coag.lista()) { motivoR1 = coag.estado == EstadoCoag::LISTA ? "Agua por tratar" : textoCoag(coag.estado); return false; }
  if (!purga.sinSedimentos(m.ntuR1)) { motivoR1 = "R1 con sedimentos"; return false; }
  if (cloro.ocupado()) { motivoR1 = "R2 clorando"; return false; }
  motivoR1 = "";
  return true;
}

// ---------- Salidas ----------
void aplicarRele(int pin, bool on) { digitalWrite(pin, on == RELE_ACTIVO_ALTO ? HIGH : LOW); }
void aplicarAgitador(int rpm) { ledcWrite(PIN_AGITADOR, rpmAPwm(rpm)); }

void actualizarLeds() {
  digitalWrite(PIN_LED_VERDE, diag.alerta == VERDE);
  digitalWrite(PIN_LED_AMAR, diag.alerta == AMARILLO);
  digitalWrite(PIN_LED_ROJO, diag.alerta == ROJO);
}

void actualizarBuzzer(unsigned long ahora) {
  if (diag.alerta != ROJO || ahora < silencioHasta) { noTone(PIN_BUZZER); return; }
  if ((ahora / 400) % 2 == 0) tone(PIN_BUZZER, 1800);
  else noTone(PIN_BUZZER);
}

const char* textoSedimento(float ntu) {
  static const char* t[] = {"poco", "medio", "alto"};
  return t[nivelSedimento(ntu)];
}

void mostrarLcd(unsigned long ahora) {
  char a[24], b[24];  // el LCD muestra 16: se cortan abajo
  switch (pantalla) {
    case 0:
      snprintf(a, sizeof a, "R1 sed %-5s%4.0f", textoSedimento(m.ntuR1), m.ntuR1);
      if (purga.estado == EstadoPurga::PULSO) snprintf(b, sizeof b, "Purgando %d/%d", purga.pulso, PURGA_MAX_PULSOS);
      else if (purga.estado == EstadoPurga::PAUSA) snprintf(b, sizeof b, "Asentando %d/%d", purga.pulso, PURGA_MAX_PULSOS);
      else snprintf(b, sizeof b, "%s", !m.r1Agua ? "R1 sin agua" : (valvula.abierta ? "Llenando R1" : purga.motivo));
      break;
    case 1:
      if (m.r2Ok) snprintf(a, sizeof a, "R2 %3.0f%% %5.0fL", m.nivel, m.litrosR2());
      else snprintf(a, sizeof a, "R2 nivel: ERROR");
      snprintf(b, sizeof b, "pH%4.1f %5.0fNTU", m.ph, m.ntuR2);
      break;
    case 2:
      snprintf(a, sizeof a, "Pur %s Tr %s", purga.encendida ? "ON " : "OFF", transfer.encendida ? "ON" : "OFF");
      snprintf(b, sizeof b, "%s", diag.causa);
      break;
    case 3:
      snprintf(a, sizeof a, "%s", textoCloro(cloro.estado));
      if (cloro.estado == EstadoCloro::DOSIFICANDO)
        snprintf(b, sizeof b, "%.0f/%.0f gotas", cloro.gotasAplicadas, cloro.gotasObjetivo);
      else if (cloro.faltaMs(ahora) > 0)
        snprintf(b, sizeof b, "Faltan %lus", cloro.faltaMs(ahora) / 1000);
      else snprintf(b, sizeof b, "Cloro %3.0f%%", cloro.pctRestante());
      break;
    case 4:
      snprintf(a, sizeof a, "%s", textoCoag(coag.estado));
      if (coag.estado == EstadoCoag::DOSIFICANDO)
        snprintf(b, sizeof b, "PAC %.0f/%.0fml", coag.mlAplicados, coag.mlObjetivo);
      else if (coag.faltaMs(ahora) > 0)
        snprintf(b, sizeof b, "%3drpm falta%3lus", coag.rpm, coag.faltaMs(ahora) / 1000);
      else if (m.r1NivelOk)
        snprintf(b, sizeof b, "R1 %3.0f%% PAC%3.0f%%", m.nivelR1, coag.pctRestante());
      else snprintf(b, sizeof b, "R1 nivel: ERROR");
      break;
    default:
      snprintf(a, sizeof a, "%s", modoAP ? "Red: " AP_SSID : "WiFi conectado");
      snprintf(b, sizeof b, "%s", (modoAP ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str());
  }
  a[16] = b[16] = 0;
  lcd.clear();
  lcd.print(a);
  lcd.setCursor(0, 1);
  lcd.print(b);
}

// ---------- Historial ----------
void guardarHistorial() {
  Punto p = {(uint8_t)(m.r2Ok ? lroundf(m.nivel) : 0), (uint16_t)lroundf(isnan(m.ntuR1) ? 0 : m.ntuR1),
             (uint16_t)lroundf(isnan(m.ntuR2) ? 0 : m.ntuR2), (uint8_t)lroundf(isnan(m.ph) ? 0 : m.ph * 10),
             (uint8_t)(purga.encendida | transfer.encendida << 1 | cloro.goteoAbierto << 2 |
                       (coag.rpm > 0) << 3 | coag.bombaPac << 4 | valvula.abierta << 5)};
  if (histCantidad == HISTORIAL_PUNTOS) histInicio = (histInicio + 1) % HISTORIAL_PUNTOS;
  else histCantidad++;
  historial[(histInicio + histCantidad - 1) % HISTORIAL_PUNTOS] = p;
}

// ---------- Web ----------
const char* nombreAlerta(Alerta a) { return a == ROJO ? "ROJO" : (a == AMARILLO ? "AMARILLO" : "VERDE"); }
const char* tf(bool b) { return b ? "true" : "false"; }
const char* nombrePurga(EstadoPurga e) {
  return e == EstadoPurga::PULSO ? "PULSO" : (e == EstadoPurga::PAUSA ? "PAUSA" : "LIMPIO");
}

// Claves de fábrica: cualquiera en la red podría mover las bombas.
bool clavesPorDefecto() { return strcmp(PIN_WEB, "1234") == 0 || strcmp(AP_PASSWORD, "cambiar123") == 0; }

// ---------- Respaldo en flash (sobrevive a cortes de luz) ----------
constexpr uint16_t RESPALDO_VERSION = 1;
struct Respaldo {
  uint16_t version;
  RespaldoCoag coag;
  RespaldoCloro cloro;
};
Respaldo guardado = {};
unsigned long tGuardado = 0;

Respaldo respaldoActual() { return {RESPALDO_VERSION, coag.respaldo(), cloro.respaldo()}; }

void cargarRespaldo(unsigned long ahora) {
  prefs.begin("aqua", false);
  Respaldo r;
  if (prefs.getBytesLength("estado") != sizeof r || prefs.getBytes("estado", &r, sizeof r) != sizeof r ||
      r.version != RESPALDO_VERSION) {
    Serial.println("Respaldo: sin datos guardados, insumos llenos");
    guardado = respaldoActual();
    return;
  }
  coag.restaurar(r.coag);
  cloro.restaurar(r.cloro, ahora);
  guardado = r;
  Serial.printf("Respaldo: PAC %.0f ml, cloro %.0f ml, %.0f L por clorar, R1 %s, R2 %s\n", coag.mlRestantes(),
                cloro.mlRestantes(), cloro.litrosPendientes, coag.lista() ? "tratada" : "por tratar",
                textoCloro(cloro.estado));
}

// Campo a campo y con tolerancia (no memcmp: el struct tiene relleno y los
// floats varían en el último decimal mientras dosifica).
bool casiIgual(float a, float b, float tol = 0.5f) { return fabsf(a - b) < tol; }
bool mismaEtapa(const Respaldo& a, const Respaldo& b) {
  return casiIgual(a.coag.mlTotal, b.coag.mlTotal) && casiIgual(a.coag.mlRecarga, b.coag.mlRecarga) &&
         casiIgual(a.coag.mlPrevios, b.coag.mlPrevios) && a.coag.lotes == b.coag.lotes &&
         a.coag.tratada == b.coag.tratada && casiIgual(a.cloro.gotasTotal, b.cloro.gotasTotal) &&
         casiIgual(a.cloro.gotasRecarga, b.cloro.gotasRecarga) &&
         casiIgual(a.cloro.litrosPendientes, b.cloro.litrosPendientes) &&
         casiIgual(a.cloro.litrosInicioLlenado, b.cloro.litrosInicioLlenado) && a.cloro.dosis == b.cloro.dosis &&
         a.cloro.fase == b.cloro.fase;
}

// Guarda al momento los cambios de etapa; el nivel base de R1 (que baja de a
// poco al transferir) como máximo una vez por minuto, para cuidar la flash.
void guardarRespaldo(unsigned long ahora) {
  Respaldo r = respaldoActual();
  if (mismaEtapa(r, guardado)) {
    if (casiIgual(r.coag.base, guardado.coag.base) || ahora - tGuardado < RESPALDO_MIN_MS) return;
  }
  prefs.putBytes("estado", &r, sizeof r);
  guardado = r;
  tGuardado = ahora;
}

void apiEstado() {
  unsigned long ahora = millis();
  String j;
  j.reserve(2000);
  char buf[480];
  auto add = [&](const char* fmt, auto... args) {
    snprintf(buf, sizeof buf, fmt, args...);
    j += buf;
  };
  add("{\"alerta\":\"%s\",\"causa\":\"%s\",\"uptime_s\":%lu,",
      nombreAlerta(diag.alerta), diag.causa, ahora / 1000);
  add("\"r1\":{\"agua\":%s,\"ntu\":%.0f,\"sedimento\":%d,\"disponible\":%s,\"motivo\":\"%s\","
      "\"nivel_ok\":%s,\"nivel\":%.1f,\"litros\":%.0f,\"valvula\":%s,\"valvula_motivo\":\"%s\","
      "\"llenados\":%lu},",
      tf(m.r1Agua), isnan(m.ntuR1) ? 0 : m.ntuR1, nivelSedimento(m.ntuR1), tf(motivoR1[0] == 0), motivoR1,
      tf(m.r1NivelOk), m.r1NivelOk ? m.nivelR1 : 0, m.litrosR1(), tf(valvula.abierta), valvula.motivo,
      valvula.llenados);
  add("\"coag\":{\"estado\":\"%s\",\"pendiente\":%s,\"ocupado\":%s,\"falta_s\":%lu,\"rpm\":%d,\"pac_on\":%s,"
      "\"ntu_medida\":%.0f,\"ppm\":%.0f,\"litros\":%.0f,\"ml_objetivo\":%.0f,\"ml_aplicados\":%.0f,"
      "\"sin_pac\":%s,\"sin_nivel\":%s,\"fuera_tabla\":%s,\"lotes\":%lu,\"ml_total\":%.0f,"
      "\"ml_restantes\":%.0f,\"pct_restante\":%.0f},",
      textoCoag(coag.estado), tf(coag.pendiente()), tf(coag.ocupado()), coag.faltaMs(ahora) / 1000, coag.rpm,
      tf(coag.bombaPac), coag.ntuMedida, coag.ppm, coag.litrosLote, coag.mlObjetivo, coag.mlAplicados,
      tf(coag.sinPac), tf(coag.sinNivel), tf(coag.fueraTabla), coag.lotes, coag.mlTotal, coag.mlRestantes(),
      coag.pctRestante());
  add("\"purga\":{\"on\":%s,\"auto\":%s,\"estado\":\"%s\",\"pulso\":%d,\"max_pulsos\":%d,\"motivo\":\"%s\","
      "\"falla\":%s,\"purgas\":%lu,\"seg\":%lu,\"litros\":%.0f,\"ntu_antes\":%.0f,\"ntu_despues\":%.0f},",
      tf(purga.encendida), tf(purga.automatico), nombrePurga(purga.estado), purga.pulso, PURGA_MAX_PULSOS,
      purga.motivo, tf(purga.falla), purga.purgas, purga.msTotal(ahora) / 1000, purga.litrosPurgados(ahora),
      purga.ntuAntes, purga.ntuDespues);
  add("\"transfer\":{\"on\":%s,\"auto\":%s,\"motivo\":\"%s\",\"falla\":\"%s\",\"ciclos\":%lu,\"seg\":%lu,"
      "\"litros\":%.0f,\"respuesta_s\":%.1f,\"completos\":%lu,\"cortes\":%lu},",
      tf(transfer.encendida), tf(transfer.automatico), transfer.motivo, textoFalla(transfer.falla),
      transfer.ciclos, transfer.msEncendidaTotal(ahora) / 1000, transfer.litrosBombeados,
      transfer.ultimaRespuestaS, transfer.llenadosCompletos, transfer.cortesSeguridad);
  add("\"r2\":{\"ok\":%s,\"nivel\":%.1f,\"litros\":%.0f,\"ph\":%.2f,\"gotas_l\":%.1f,\"rango_ph\":%d,"
      "\"ntu\":%.1f,\"clara\":%s},",
      tf(m.r2Ok), m.r2Ok ? m.nivel : 0, m.litrosR2(), isnan(m.ph) ? 0 : m.ph, gotasPorLitro(m.ph),
      rangoPh(isnan(m.ph) ? 7 : m.ph), isnan(m.ntuR2) ? 0 : m.ntuR2, tf(claraParaClorar(m.ntuR2)));
  add("\"cloro\":{\"estado\":\"%s\",\"goteo\":%s,\"pendientes_l\":%.0f,\"objetivo\":%.0f,\"aplicadas\":%.0f,"
      "\"total\":%.0f,\"ph_dosis\":%.2f,\"gotas_l_usadas\":%.1f,\"dosis\":%lu,\"falta_s\":%lu,"
      "\"ml_restantes\":%.0f,\"pct_restante\":%.0f,\"gotas_ml\":%.0f},",
      textoCloro(cloro.estado), tf(cloro.goteoAbierto), cloro.litrosPendientes, cloro.gotasObjetivo,
      cloro.gotasAplicadas, cloro.gotasTotal, cloro.phDosis, cloro.gotasPorLitroUsadas, cloro.dosis,
      cloro.faltaMs(ahora) / 1000, cloro.mlRestantes(), cloro.pctRestante(), GOTAS_POR_ML);
  add("\"cfg\":{\"sed_medio\":%.0f,\"sed_limpio\":%.0f,\"ph_min\":%.1f,\"ph_opt\":%.1f,\"ph_max\":%.1f,"
      "\"gotas_l\":%.1f,\"turb_max\":%.0f,\"turb_ideal\":%.0f,\"r2_on\":%.0f,\"r2_off\":%.0f,"
      "\"coag_min\":%.0f,\"pac_stock\":%.0f,\"rpm_rapida\":%d,\"rpm_lenta\":%d,\"modo_demo\":%s,"
      "\"claves_defecto\":%s}}",
      SED_MEDIO_NTU, SED_LIMPIO_NTU, PH_MIN, PH_OPTIMO_MAX, PH_MAX, CLORO_GOTAS_POR_LITRO,
      TURB_R2_MAX_CLORO, TURB_R2_IDEAL, R2_ENCENDER, R2_APAGAR, COAG_MIN_NTU, PAC_STOCK_PPM,
      AGIT_RPM_RAPIDA, AGIT_RPM_LENTA, tf(MODO_DEMO), tf(clavesPorDefecto()));
  server.sendHeader("Access-Control-Allow-Origin", "*");  // panel abierto como archivo local
  server.send(200, "application/json", j);
}

void apiHistorial() {
  String j;
  j.reserve(histCantidad * 18 + 80);
  j = "{\"intervalo_s\":" + String(INTERVALO_HISTORIAL_MS / 1000) + ",\"max\":" +
      String(HISTORIAL_PUNTOS) + ",\"datos\":[";
  for (int i = 0; i < histCantidad; i++) {
    const Punto& p = historial[(histInicio + i) % HISTORIAL_PUNTOS];
    if (i) j += ',';
    j += '[' + String(p.nivel) + ',' + String(p.ntu) + ',' + String(p.ph10) + ',' + String(p.estado) + ',' +
         String(p.ntu2) + ']';
  }
  j += "]}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", j);
}

void apiControl() {
  if (server.arg("pin") != PIN_WEB) {
    server.send(403, "application/json", "{\"ok\":false,\"error\":\"PIN\"}");
    return;
  }
  String a = server.arg("accion");
  unsigned long ahora = millis();
  if (a == "p_auto") purga.modoAutomatico();
  else if (a == "p_on") purga.manual(true);
  else if (a == "p_off") purga.manual(false);
  else if (a == "t_auto") transfer.modoAutomatico();
  else if (a == "t_on") transfer.manual(true);
  else if (a == "t_off") transfer.manual(false);
  else if (a == "reset") { purga.reiniciarFalla(); transfer.reiniciarFalla(); }
  else if (a == "recarga") cloro.recargado(ahora);
  else if (a == "clorar") cloro.clorarTodo(m.litrosR2(), ahora);
  else if (a == "tratar") coag.iniciar();
  else if (a == "pac_recarga") coag.recargado();
  else { server.send(400, "application/json", "{\"ok\":false}"); return; }
  Serial.printf("Web: accion '%s'\n", a.c_str());
  server.send(200, "application/json", "{\"ok\":true}");
}

void iniciarRed() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("WiFi: conectando a \"%s\"", WIFI_SSID);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_ESPERA_MS) {
    delay(250);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi: OK, abrir http://%s\n", WiFi.localIP().toString().c_str());
  } else {
    // Sin WiFi en casa: el ESP32 crea su propia red y la página sigue disponible.
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    modoAP = true;
    Serial.printf("\nWiFi: sin red, creada \"%s\" -> http://%s\n", AP_SSID,
                  WiFi.softAPIP().toString().c_str());
  }
  server.on("/", [] { server.send_P(200, "text/html; charset=utf-8", PAGINA); });
  server.on("/panel", [] { server.send_P(200, "text/html; charset=utf-8", PANEL); });
  server.on("/api/estado", apiEstado);
  server.on("/api/historial", apiHistorial);
  server.on("/api/control", HTTP_POST, apiControl);
  server.onNotFound([] { server.send(404, "text/plain", "No encontrado"); });
  server.begin();
}

// ---------- Botón: silenciar alarma + reiniciar fallas ----------
void manejarBoton(unsigned long ahora) {
  static bool antes = false;
  static unsigned long tCambio = 0;
  bool presionado = digitalRead(PIN_BOTON) == LOW;
  if (presionado != antes && ahora - tCambio > 50) {
    tCambio = ahora;
    antes = presionado;
    if (presionado) {
      silencioHasta = ahora + SILENCIO_MS;
      if (purga.falla || transfer.falla != Falla::NINGUNA) {
        purga.reiniciarFalla();
        transfer.reiniciarFalla();
        Serial.println("Boton: fallas reiniciadas");
      } else {
        Serial.println("Boton: alarma silenciada 60 s");
      }
    }
  }
}

void iniciarWatchdog() {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = WATCHDOG_MS;
  cfg.trigger_panic = true;
  if (esp_task_wdt_init(&cfg) == ESP_ERR_INVALID_STATE) esp_task_wdt_reconfigure(&cfg);
  esp_task_wdt_add(NULL);
}

// ---------- Programa ----------
void setup() {
  Serial.begin(115200);
  for (int p : {PIN_RELE_PURGA, PIN_RELE_TRANSFER, PIN_RELE_GOTEO, PIN_RELE_PAC, PIN_VALVULA_R1}) {
    pinMode(p, OUTPUT);
    aplicarRele(p, false);  // primero: todo apagado
  }
  ledcAttach(PIN_AGITADOR, AGIT_PWM_FREQ, AGIT_PWM_BITS);
  aplicarAgitador(0);
  pinMode(PIN_TRIG_R1, OUTPUT);
  pinMode(PIN_ECHO_R1, INPUT);
  pinMode(PIN_TRIG_R2, OUTPUT);
  pinMode(PIN_ECHO_R2, INPUT);
  pinMode(PIN_FLOTADOR_R1, INPUT_PULLUP);
  pinMode(PIN_BOTON, INPUT_PULLUP);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMAR, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  analogSetPinAttenuation(PIN_TURB_R1, ADC_11db);
  analogSetPinAttenuation(PIN_PH_R2, ADC_11db);
  analogSetPinAttenuation(PIN_TURB_R2, ADC_11db);

  if (esp_reset_reason() == ESP_RST_TASK_WDT) Serial.println("Aviso: reinicio por watchdog");
  lcd.init();
  lcd.backlight();
  lcd.print("AquaAlerta ODS6");
  lcd.setCursor(0, 1);
  lcd.print("Conectando...");
  Serial.printf("AquaAlerta iniciada (modo %s)\n", MODO_DEMO ? "DEMO: tiempos cortos" : "REAL");
  if (clavesPorDefecto()) Serial.println("AVISO: PIN_WEB o AP_PASSWORD de fabrica, cambiarlos en secrets.h");
  cargarRespaldo(millis());

  iniciarRed();
  iniciarWatchdog();
}

void loop() {
  esp_task_wdt_reset();
  server.handleClient();
  unsigned long ahora = millis();
  manejarBoton(ahora);

  if (ahora - tLectura >= INTERVALO_LECTURA_MS || tLectura == 0) {
    tLectura = ahora;
    leerSensores();
    unsigned long t = millis();

    // Se cortó la luz mientras entraba agua a R2: suma los litros que entraron.
    if (cloro.llenadoInterrumpido() && m.r2Ok) {
      float litros = m.litrosR2() - cloro.litrosInicioLlenado();
      Serial.printf("Respaldo: llenado de R2 interrumpido, entraron %.0f L\n", litros > 0 ? litros : 0);
      cloro.aguaNueva(litros, t);
    }

    // 0) R1: coagulación con PAC (agita, dosifica, flocula, deja sedimentar)
    EstadoCoag coAntes = coag.estado;
    bool r1Ocupado = purga.encendida || purga.estado != EstadoPurga::LIMPIO || transfer.encendida ||
                     valvula.abierta;
    coag.actualizar(m.ntuR1, m.nivelR1, m.r1NivelOk, m.r1Agua, r1Ocupado, transfer.encendida, t);
    aplicarRele(PIN_RELE_PAC, coag.bombaPac);
    aplicarAgitador(coag.rpm);
    if (coag.estado != coAntes) {
      Serial.printf("Coagulacion: %s", textoCoag(coag.estado));
      if (coag.estado == EstadoCoag::DOSIFICANDO)
        Serial.printf(" -> %.0f NTU, %.0f ppm x %.0f L = %.0f ml de PAC", coag.ntuMedida, coag.ppm,
                      coag.litrosLote, coag.mlObjetivo);
      Serial.println();
    }

    // 0b) Válvula de entrada: deja entrar agua cruda solo con R1 en reposo
    bool vAntes = valvula.abierta;
    bool r2EsperaAgua = coag.lista() && m.r2Ok && m.nivel <= R2_ENCENDER;
    if (valvula.actualizar(m.nivelR1, m.r1NivelOk, coag.ocupado() || purga.encendida ||
                           purga.estado != EstadoPurga::LIMPIO || transfer.encendida, r2EsperaAgua, t)) {
      coag.iniciar();  // entró agua cruda: R1 se vuelve a tratar
    }
    aplicarRele(PIN_VALVULA_R1, valvula.abierta);
    if (valvula.abierta != vAntes) Serial.printf("Valvula R1: %s (%s)\n", valvula.abierta ? "ABIERTA" : "cerrada", valvula.motivo);

    // 1) R1: purga de sedimentos (espera a que termine la coagulación)
    bool antes = purga.encendida;
    aplicarRele(PIN_RELE_PURGA, purga.actualizar(m.ntuR1, m.r1Agua,
                                                 transfer.encendida || coag.ocupado() || coag.pendiente(), t));
    if (purga.encendida != antes) Serial.printf("Purga: %s (%s)\n", purga.encendida ? "ENCENDIDA" : "apagada", purga.motivo);

    // 2) Transferencia R1 → filtro → R2 (solo sin sedimentos)
    antes = transfer.encendida;
    Falla fallaAntes = transfer.falla;
    float litrosAntes = transfer.litrosBombeados;
    bool disp = r1Disponible();
    aplicarRele(PIN_RELE_TRANSFER,
                transfer.actualizar({m.nivel, m.r2Ok, disp, motivoR1, !claraParaClorar(m.ntuR2), t}));
    if (transfer.encendida && !antes) cloro.inicioLlenado(m.litrosR2());
    if (transfer.encendida != antes)
      Serial.printf("Transferencia: %s (%s)\n", transfer.encendida ? "ENCENDIDA" : "apagada", transfer.motivo);
    if (transfer.falla != fallaAntes && transfer.falla != Falla::NINGUNA)
      Serial.printf("Transferencia: FALLA -> %s\n", textoFalla(transfer.falla));
    if (antes && !transfer.encendida) cloro.aguaNueva(transfer.litrosBombeados - litrosAntes, t);

    // 3) R2: pH → gotas de cloro
    EstadoCloro cAntes = cloro.estado;
    aplicarRele(PIN_RELE_GOTEO, cloro.actualizar(m.ph, m.ntuR2, transfer.encendida, t));
    if (cloro.estado != cAntes) {
      Serial.printf("Cloro: %s", textoCloro(cloro.estado));
      if (cloro.estado == EstadoCloro::DOSIFICANDO)
        Serial.printf(" -> pH %.1f, %.1f gotas/L x %.0f L = %.0f gotas", cloro.phDosis,
                      cloro.gotasPorLitroUsadas, cloro.litrosPendientes, cloro.gotasObjetivo);
      Serial.println();
    }

    Diagnostico d = diagnosticar({m.r2Ok, m.nivel, m.ph, m.ntuR2, m.r1Agua, m.ntuR1, m.r1NivelOk, &coag, &purga,
                                  &transfer, &cloro});
    if (d.alerta != diag.alerta || strcmp(d.causa, diag.causa) != 0)
      Serial.printf("Estado: %s (%s)\n", nombreAlerta(d.alerta), d.causa);
    diag = d;
    actualizarLeds();
    guardarRespaldo(t);
  }

  if (ahora - tHistorial >= INTERVALO_HISTORIAL_MS || tHistorial == 0) {
    tHistorial = ahora;
    guardarHistorial();
    Serial.printf("{\"ntu_r1\":%.0f,\"r1_agua\":%d,\"r1\":%.1f,\"coag\":\"%s\",\"rpm\":%d,\"valvula\":%d,"
                  "\"r2\":%.1f,\"ph\":%.2f,\"ntu_r2\":%.1f,\"purga\":%d,\"transfer\":%d,\"goteo\":%d,"
                  "\"estado\":\"%s\"}\n",
                  m.ntuR1, m.r1Agua, m.nivelR1, textoCoag(coag.estado), coag.rpm, valvula.abierta, m.nivel, m.ph,
                  m.ntuR2, purga.encendida, transfer.encendida, cloro.goteoAbierto, diag.causa);
  }

  if (ahora - tPantalla >= CAMBIO_PANTALLA_MS) {
    tPantalla = ahora;
    pantalla = (pantalla + 1) % 6;
    mostrarLcd(ahora);
  }

  actualizarBuzzer(ahora);
}
