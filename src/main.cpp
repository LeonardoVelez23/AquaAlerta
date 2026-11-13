// AquaAlerta – tratamiento de agua en dos reservorios:
//   R1: mide sedimentos y los purga → bomba + filtro → R2: mide pH y dosifica cloro.
// ODS 6 (agua limpia) · secundario ODS 13 (resiliencia ante sequía).
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "control.h"
#include "pagina.h"
#include "secrets.h"

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
WebServer server(80);

ControlPurga purga;
ControlBomba transfer;
Cloracion cloro;
MediaMovil<MUESTRAS_PROMEDIO> mNivel, mNtu, mPh;

struct {
  float ntuR1 = NAN;        // sedimentos en R1
  bool r1Agua = true;       // flotador de R1
  float distancia = NAN, nivel = NAN, ph = NAN;  // R2
  bool r2Ok = false;
  float litrosR2() const { return r2Ok ? nivel / 100 * R2_CAPACIDAD_L : 0; }
} m;
int fallosNivel = FALLOS_NIVEL_ALERTA;  // hasta la primera lectura válida
const char* motivoR1 = "";              // por qué R1 no entrega agua

Diagnostico diag = {VERDE, "Iniciando"};
bool modoAP = false;

// bits de estado: 1 purga, 2 transferencia, 4 goteo
struct Punto { uint8_t nivel; uint16_t ntu; uint8_t ph10; uint8_t estado; };
Punto historial[HISTORIAL_PUNTOS];
int histInicio = 0, histCantidad = 0;

unsigned long tLectura = 0, tPantalla = 0, tHistorial = 0, silencioHasta = 0;
int pantalla = 0;

// ---------- Sensores ----------
float pingCm() {
  digitalWrite(PIN_TRIG_R2, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG_R2, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG_R2, LOW);
  unsigned long us = pulseIn(PIN_ECHO_R2, HIGH, 30000);  // ~5 m máximo
  float cm = us / 58.0f;
  return (us == 0 || cm < DIST_MIN_VALIDA_CM || cm > DIST_MAX_VALIDA_CM) ? NAN : cm;
}

// Mediana de varios pings: un eco falso (pared, gota) no mueve el resultado.
float distanciaMediana() {
  float v[PINGS_POR_LECTURA];
  int n = 0;
  for (int i = 0; i < PINGS_POR_LECTURA; i++) {
    float d = pingCm();
    if (!isnan(d)) v[n++] = d;
    delay(30);  // deja apagar los ecos del ping anterior
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

  float d = distanciaMediana();
  if (isnan(d)) {
    if (++fallosNivel == FALLOS_NIVEL_ALERTA) Serial.println("R2: sensor de nivel sin respuesta");
  } else {
    fallosNivel = 0;
    m.distancia = d;
    m.nivel = mNivel.agregar(nivelDesdeDistancia(d));
  }
  m.r2Ok = fallosNivel < FALLOS_NIVEL_ALERTA;
  m.ph = mPh.agregar(phDesdeVoltaje(voltios(PIN_PH_R2, PH_DIVISOR)));
}

// ¿R1 puede entregar agua a R2?
bool r1Disponible() {
  if (!m.r1Agua) { motivoR1 = "R1 sin agua"; return false; }
  if (!purga.sinSedimentos(m.ntuR1)) { motivoR1 = "R1 con sedimentos"; return false; }
  if (cloro.ocupado()) { motivoR1 = "R2 clorando"; return false; }
  motivoR1 = "";
  return true;
}

// ---------- Salidas ----------
void aplicarRele(int pin, bool on) { digitalWrite(pin, on == RELE_ACTIVO_ALTO ? HIGH : LOW); }

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
  char a[17], b[17];
  switch (pantalla) {
    case 0:
      snprintf(a, sizeof a, "R1 sed %-5s%4.0f", textoSedimento(m.ntuR1), m.ntuR1);
      if (purga.estado == EstadoPurga::PULSO) snprintf(b, sizeof b, "Purgando %d/%d", purga.pulso, PURGA_MAX_PULSOS);
      else if (purga.estado == EstadoPurga::PAUSA) snprintf(b, sizeof b, "Asentando %d/%d", purga.pulso, PURGA_MAX_PULSOS);
      else snprintf(b, sizeof b, "%s", m.r1Agua ? purga.motivo : "R1 sin agua");
      break;
    case 1:
      if (m.r2Ok) snprintf(a, sizeof a, "R2 %3.0f%% %5.0fL", m.nivel, m.litrosR2());
      else snprintf(a, sizeof a, "R2 nivel: ERROR");
      static const char* rango[] = {"acido", "optimo", "acept.", "ALTO"};
      snprintf(b, sizeof b, "pH %4.1f %s", m.ph, rango[rangoPh(m.ph)]);
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
    default:
      snprintf(a, sizeof a, "%s", modoAP ? "Red: " AP_SSID : "WiFi conectado");
      snprintf(b, sizeof b, "%s", (modoAP ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str());
  }
  lcd.clear();
  lcd.print(a);
  lcd.setCursor(0, 1);
  lcd.print(b);
}

// ---------- Historial ----------
void guardarHistorial() {
  Punto p = {(uint8_t)(m.r2Ok ? lroundf(m.nivel) : 0), (uint16_t)lroundf(isnan(m.ntuR1) ? 0 : m.ntuR1),
             (uint8_t)lroundf(isnan(m.ph) ? 0 : m.ph * 10),
             (uint8_t)(purga.encendida | transfer.encendida << 1 | cloro.goteoAbierto << 2)};
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

void apiEstado() {
  unsigned long ahora = millis();
  String j;
  j.reserve(1400);
  char buf[480];
  auto add = [&](const char* fmt, auto... args) {
    snprintf(buf, sizeof buf, fmt, args...);
    j += buf;
  };
  add("{\"alerta\":\"%s\",\"causa\":\"%s\",\"uptime_s\":%lu,",
      nombreAlerta(diag.alerta), diag.causa, ahora / 1000);
  add("\"r1\":{\"agua\":%s,\"ntu\":%.0f,\"sedimento\":%d,\"disponible\":%s,\"motivo\":\"%s\"},",
      tf(m.r1Agua), isnan(m.ntuR1) ? 0 : m.ntuR1, nivelSedimento(m.ntuR1), tf(motivoR1[0] == 0), motivoR1);
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
  add("\"r2\":{\"ok\":%s,\"nivel\":%.1f,\"litros\":%.0f,\"ph\":%.2f,\"gotas_l\":%.1f,\"rango_ph\":%d},",
      tf(m.r2Ok), m.r2Ok ? m.nivel : 0, m.litrosR2(), isnan(m.ph) ? 0 : m.ph, gotasPorLitro(m.ph),
      rangoPh(isnan(m.ph) ? 7 : m.ph));
  add("\"cloro\":{\"estado\":\"%s\",\"goteo\":%s,\"pendientes_l\":%.0f,\"objetivo\":%.0f,\"aplicadas\":%.0f,"
      "\"total\":%.0f,\"ph_dosis\":%.2f,\"gotas_l_usadas\":%.1f,\"dosis\":%lu,\"falta_s\":%lu,"
      "\"ml_restantes\":%.0f,\"pct_restante\":%.0f,\"gotas_ml\":%.0f},",
      textoCloro(cloro.estado), tf(cloro.goteoAbierto), cloro.litrosPendientes, cloro.gotasObjetivo,
      cloro.gotasAplicadas, cloro.gotasTotal, cloro.phDosis, cloro.gotasPorLitroUsadas, cloro.dosis,
      cloro.faltaMs(ahora) / 1000, cloro.mlRestantes(), cloro.pctRestante(), GOTAS_POR_ML);
  add("\"cfg\":{\"sed_medio\":%.0f,\"sed_limpio\":%.0f,\"ph_min\":%.1f,\"ph_opt\":%.1f,\"ph_max\":%.1f,"
      "\"gotas_l\":%.1f,\"r2_on\":%.0f,\"r2_off\":%.0f}}",
      SED_MEDIO_NTU, SED_LIMPIO_NTU, PH_MIN, PH_OPTIMO_MAX, PH_MAX, CLORO_GOTAS_POR_LITRO,
      R2_ENCENDER, R2_APAGAR);
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
    j += '[' + String(p.nivel) + ',' + String(p.ntu) + ',' + String(p.ph10) + ',' + String(p.estado) + ']';
  }
  j += "]}";
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
  for (int p : {PIN_RELE_PURGA, PIN_RELE_TRANSFER, PIN_RELE_GOTEO}) {
    pinMode(p, OUTPUT);
    aplicarRele(p, false);  // primero: todo apagado
  }
  pinMode(PIN_TRIG_R2, OUTPUT);
  pinMode(PIN_ECHO_R2, INPUT);
  pinMode(PIN_FLOTADOR_R1, INPUT_PULLUP);
  pinMode(PIN_BOTON, INPUT_PULLUP);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMAR, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  analogSetPinAttenuation(PIN_TURB_R1, ADC_11db);
  analogSetPinAttenuation(PIN_PH_R2, ADC_11db);

  if (esp_reset_reason() == ESP_RST_TASK_WDT) Serial.println("Aviso: reinicio por watchdog");
  lcd.init();
  lcd.backlight();
  lcd.print("AquaAlerta ODS6");
  lcd.setCursor(0, 1);
  lcd.print("Conectando...");
  Serial.println("AquaAlerta iniciada");

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

    // 1) R1: purga de sedimentos
    bool antes = purga.encendida;
    aplicarRele(PIN_RELE_PURGA, purga.actualizar(m.ntuR1, m.r1Agua, transfer.encendida, t));
    if (purga.encendida != antes) Serial.printf("Purga: %s (%s)\n", purga.encendida ? "ENCENDIDA" : "apagada", purga.motivo);

    // 2) Transferencia R1 → filtro → R2 (solo sin sedimentos)
    antes = transfer.encendida;
    Falla fallaAntes = transfer.falla;
    float litrosAntes = transfer.litrosBombeados;
    bool disp = r1Disponible();
    aplicarRele(PIN_RELE_TRANSFER, transfer.actualizar({m.nivel, m.r2Ok, disp, motivoR1, t}));
    if (transfer.encendida != antes)
      Serial.printf("Transferencia: %s (%s)\n", transfer.encendida ? "ENCENDIDA" : "apagada", transfer.motivo);
    if (transfer.falla != fallaAntes && transfer.falla != Falla::NINGUNA)
      Serial.printf("Transferencia: FALLA -> %s\n", textoFalla(transfer.falla));
    if (antes && !transfer.encendida) cloro.aguaNueva(transfer.litrosBombeados - litrosAntes, t);

    // 3) R2: pH → gotas de cloro
    EstadoCloro cAntes = cloro.estado;
    aplicarRele(PIN_RELE_GOTEO, cloro.actualizar(m.ph, transfer.encendida, t));
    if (cloro.estado != cAntes) {
      Serial.printf("Cloro: %s", textoCloro(cloro.estado));
      if (cloro.estado == EstadoCloro::DOSIFICANDO)
        Serial.printf(" -> pH %.1f, %.1f gotas/L x %.0f L = %.0f gotas", cloro.phDosis,
                      cloro.gotasPorLitroUsadas, cloro.litrosPendientes, cloro.gotasObjetivo);
      Serial.println();
    }

    Diagnostico d = diagnosticar({m.r2Ok, m.nivel, m.ph, m.r1Agua, m.ntuR1, &purga, &transfer, &cloro});
    if (d.alerta != diag.alerta || strcmp(d.causa, diag.causa) != 0)
      Serial.printf("Estado: %s (%s)\n", nombreAlerta(d.alerta), d.causa);
    diag = d;
    actualizarLeds();
  }

  if (ahora - tHistorial >= INTERVALO_HISTORIAL_MS || tHistorial == 0) {
    tHistorial = ahora;
    guardarHistorial();
    Serial.printf("{\"ntu_r1\":%.0f,\"r1_agua\":%d,\"r2\":%.1f,\"ph\":%.2f,\"purga\":%d,\"transfer\":%d,"
                  "\"goteo\":%d,\"estado\":\"%s\"}\n",
                  m.ntuR1, m.r1Agua, m.nivel, m.ph, purga.encendida, transfer.encendida,
                  cloro.goteoAbierto, diag.causa);
  }

  if (ahora - tPantalla >= CAMBIO_PANTALLA_MS) {
    tPantalla = ahora;
    pantalla = (pantalla + 1) % 5;
    mostrarLcd(ahora);
  }

  actualizarBuzzer(ahora);
}
