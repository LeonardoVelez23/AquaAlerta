// AquaAlerta – tratamiento de agua en dos reservorios:
//   R1 sedimentación → bomba + filtro → R2 con cloración por goteo.
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

ControlBomba llenado({R1_ENCENDER, R1_APAGAR, R1.capacidadL, "R1 no se llena", "Llenado t. max"});
ControlBomba transfer({R2_ENCENDER, R2_APAGAR, R2.capacidadL, "Filtro tapado?", "Transf. t. max"});
Sedimentacion sedimento;
Goteo goteo;

struct Tanque {
  int trig, echo, pinTurb;
  const Reservorio& r;
  MediaMovil<MUESTRAS_PROMEDIO> mNivel, mNtu;
  float distancia = NAN, nivel = NAN, ntu = NAN;
  int fallos = FALLOS_NIVEL_ALERTA;  // hasta la primera lectura válida
  bool ok = false;
  int turbidez = 0;  // 0 limpia, 1 turbia, 2 muy turbia
  float litros() const { return ok ? nivel / 100 * r.capacidadL : 0; }
};
Tanque t1{PIN_TRIG_R1, PIN_ECHO_R1, PIN_TURB_R1, R1};
Tanque t2{PIN_TRIG_R2, PIN_ECHO_R2, PIN_TURB_R2, R2};
bool origen = true;
const char* motivoR1 = "";  // por qué R1 no puede entregar agua

Diagnostico diag = {VERDE, "Iniciando"};
bool modoAP = false;

struct Punto { uint8_t n1, n2; uint16_t ntu1, ntu2; uint8_t estado; };  // bits: llenado, transfer, goteo
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

// Mediana de varios pings: un eco falso (pared, gota) no mueve el resultado.
float distanciaMediana(int trig, int echo) {
  float v[PINGS_POR_LECTURA];
  int n = 0;
  for (int i = 0; i < PINGS_POR_LECTURA; i++) {
    float d = pingCm(trig, echo);
    if (!isnan(d)) v[n++] = d;
    delay(30);  // deja apagar los ecos del ping anterior
  }
  if (n < PINGS_POR_LECTURA / 2 + 1) return NAN;
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { float t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
  return v[n / 2];
}

void leerTanque(Tanque& t, const char* nombre) {
  float d = distanciaMediana(t.trig, t.echo);
  if (isnan(d)) {
    if (++t.fallos == FALLOS_NIVEL_ALERTA) Serial.printf("%s: sensor de nivel sin respuesta\n", nombre);
  } else {
    t.fallos = 0;
    t.distancia = d;
    t.nivel = t.mNivel.agregar(nivelDesdeDistancia(d, t.r));
  }
  t.ok = t.fallos < FALLOS_NIVEL_ALERTA;

  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(t.pinTurb);
  float volt = mv / 16.0f / 1000.0f * TURB_DIVISOR;
  t.ntu = t.mNtu.agregar(ntuDesdeVoltaje(volt));
  t.turbidez = nivelTurbidez(t.ntu, t.turbidez);
}

void leerSensores() {
  leerTanque(t1, "R1");
  leerTanque(t2, "R2");
  origen = digitalRead(PIN_ORIGEN) == LOW;
}

// ¿R1 puede entregar agua a R2? (reposo cumplido, sobre el lodo y clara)
bool r1Disponible(unsigned long ahora) {
  if (!t1.ok) { motivoR1 = "Sin lectura R1"; return false; }
  if (llenado.encendida) { motivoR1 = "R1 llenandose"; return false; }
  if (!sedimento.lista(ahora)) { motivoR1 = "R1 sedimentando"; return false; }
  if (t1.nivel <= R1_NIVEL_MIN_TRANSFER) { motivoR1 = "R1 bajo (lodo)"; return false; }
  if (!isnan(t1.ntu) && t1.ntu > R1_NTU_MAX_TRANSFER) { motivoR1 = "R1 aun turbia"; return false; }
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

void mmss(char* buf, size_t n, unsigned long ms) {
  unsigned long s = ms / 1000;
  if (s >= 3600) snprintf(buf, n, "%luh%02lu", s / 3600, s / 60 % 60);
  else snprintf(buf, n, "%lu:%02lu", s / 60, s % 60);
}

void mostrarLcd(unsigned long ahora) {
  char a[17], b[17], t[10];
  switch (pantalla) {
    case 0:
      if (t1.ok) snprintf(a, sizeof a, "R1 %3.0f%% %5.0fL", t1.nivel, t1.litros());
      else snprintf(a, sizeof a, "R1 nivel: ERROR");
      if (sedimento.lista(ahora)) snprintf(b, sizeof b, "Reposado %4.0fNTU", t1.ntu);
      else { mmss(t, sizeof t, sedimento.faltaMs(ahora)); snprintf(b, sizeof b, "Sediment. %s", t); }
      break;
    case 1:
      if (t2.ok) snprintf(a, sizeof a, "R2 %3.0f%% %5.0fL", t2.nivel, t2.litros());
      else snprintf(a, sizeof a, "R2 nivel: ERROR");
      snprintf(b, sizeof b, "Turb R2 %4.0fNTU", t2.ntu);
      break;
    case 2:
      snprintf(a, sizeof a, "Llen %s Tr %s", llenado.encendida ? "ON " : "OFF", transfer.encendida ? "ON" : "OFF");
      snprintf(b, sizeof b, "%s", diag.causa);
      break;
    case 3:
      snprintf(a, sizeof a, "Goteo %s %3.0f%%", goteo.abierto ? "ON " : "OFF", goteo.pctRestante());
      if (goteo.contactoListo(ahora)) snprintf(b, sizeof b, "R2 lista p/usar");
      else { mmss(t, sizeof t, goteo.contactoFaltaMs(ahora)); snprintf(b, sizeof b, "Contacto %s", t); }
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
uint16_t ntuEntero(float v) { return (uint16_t)lroundf(isnan(v) ? 0 : v); }

void guardarHistorial() {
  Punto p = {(uint8_t)(t1.ok ? lroundf(t1.nivel) : 0), (uint8_t)(t2.ok ? lroundf(t2.nivel) : 0),
             ntuEntero(t1.ntu), ntuEntero(t2.ntu),
             (uint8_t)(llenado.encendida | transfer.encendida << 1 | goteo.abierto << 2)};
  if (histCantidad == HISTORIAL_PUNTOS) histInicio = (histInicio + 1) % HISTORIAL_PUNTOS;
  else histCantidad++;
  historial[(histInicio + histCantidad - 1) % HISTORIAL_PUNTOS] = p;
}

// ---------- Web ----------
const char* nombreAlerta(Alerta a) { return a == ROJO ? "ROJO" : (a == AMARILLO ? "AMARILLO" : "VERDE"); }
const char* tf(bool b) { return b ? "true" : "false"; }

void apiEstado() {
  unsigned long ahora = millis();
  String j;
  j.reserve(1400);
  char buf[420];
  auto add = [&](const char* fmt, auto... args) {
    snprintf(buf, sizeof buf, fmt, args...);
    j += buf;
  };
  add("{\"alerta\":\"%s\",\"causa\":\"%s\",\"uptime_s\":%lu,\"origen\":%s,",
      nombreAlerta(diag.alerta), diag.causa, ahora / 1000, tf(origen));
  add("\"r1\":{\"ok\":%s,\"nivel\":%.1f,\"litros\":%.0f,\"ntu\":%.0f,\"turbidez\":%d,"
      "\"reposo_ok\":%s,\"reposo_falta_s\":%lu,\"disponible\":%s,\"motivo\":\"%s\"},",
      tf(t1.ok), t1.ok ? t1.nivel : 0, t1.litros(), isnan(t1.ntu) ? 0 : t1.ntu, t1.turbidez,
      tf(sedimento.lista(ahora)), sedimento.faltaMs(ahora) / 1000, tf(motivoR1[0] == 0), motivoR1);
  add("\"r2\":{\"ok\":%s,\"nivel\":%.1f,\"litros\":%.0f,\"ntu\":%.0f,\"turbidez\":%d},",
      tf(t2.ok), t2.ok ? t2.nivel : 0, t2.litros(), isnan(t2.ntu) ? 0 : t2.ntu, t2.turbidez);
  for (int i = 0; i < 2; i++) {
    const ControlBomba& b = i ? transfer : llenado;
    add("\"%s\":{\"on\":%s,\"auto\":%s,\"motivo\":\"%s\",\"falla\":\"%s\",\"ciclos\":%lu,\"seg\":%lu,"
        "\"litros\":%.0f,\"respuesta_s\":%.1f,\"completos\":%lu,\"cortes\":%lu},",
        i ? "transfer" : "llenado", tf(b.encendida), tf(b.automatico), b.motivo, b.textoFalla(),
        b.ciclos, b.msEncendidaTotal(ahora) / 1000, b.litrosBombeados, b.ultimaRespuestaS,
        b.llenadosCompletos, b.cortesSeguridad);
  }
  add("\"goteo\":{\"on\":%s,\"gotas_total\":%.0f,\"gotas_ciclo\":%.0f,\"dosis_pct\":%.0f,"
      "\"ml_restantes\":%.0f,\"pct_restante\":%.0f,\"contacto_ok\":%s,\"contacto_falta_s\":%lu,"
      "\"gotas_min\":%.0f,\"gotas_litro\":%.1f,\"gotas_ml\":%.0f,\"gotas_r2_completo\":%.0f},",
      tf(goteo.abierto), goteo.gotasTotal, goteo.gotasCiclo, goteo.dosisPct, goteo.mlRestantes(),
      goteo.pctRestante(), tf(goteo.contactoListo(ahora)), goteo.contactoFaltaMs(ahora) / 1000,
      GOTEO_GOTAS_MIN, CLORO_GOTAS_POR_LITRO, GOTAS_POR_ML, t2.litros() * CLORO_GOTAS_POR_LITRO);
  add("\"umbrales\":{\"r1_on\":%.0f,\"r1_off\":%.0f,\"r1_min\":%.0f,\"r2_on\":%.0f,\"r2_off\":%.0f},"
      "\"linea_base\":%.0f}",
      R1_ENCENDER, R1_APAGAR, R1_NIVEL_MIN_TRANSFER, R2_ENCENDER, R2_APAGAR, LITROS_DESBORDE_LINEA_BASE);
  server.send(200, "application/json", j);
}

void apiHistorial() {
  String j;
  j.reserve(histCantidad * 22 + 80);
  j = "{\"intervalo_s\":" + String(INTERVALO_HISTORIAL_MS / 1000) + ",\"max\":" +
      String(HISTORIAL_PUNTOS) + ",\"datos\":[";
  for (int i = 0; i < histCantidad; i++) {
    const Punto& p = historial[(histInicio + i) % HISTORIAL_PUNTOS];
    if (i) j += ',';
    j += '[' + String(p.n1) + ',' + String(p.n2) + ',' + String(p.ntu1) + ',' + String(p.ntu2) +
         ',' + String(p.estado) + ']';
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
  if (a == "l_auto") llenado.modoAutomatico();
  else if (a == "l_on") llenado.manual(true);
  else if (a == "l_off") llenado.manual(false);
  else if (a == "t_auto") transfer.modoAutomatico();
  else if (a == "t_on") transfer.manual(true);
  else if (a == "t_off") transfer.manual(false);
  else if (a == "reset") { llenado.reiniciarFalla(); transfer.reiniciarFalla(); }
  else if (a == "recarga") goteo.recargado();
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
      if (llenado.falla != Falla::NINGUNA || transfer.falla != Falla::NINGUNA) {
        llenado.reiniciarFalla();
        transfer.reiniciarFalla();
        Serial.println("Boton: fallas de bombas reiniciadas");
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

void registrar(const char* nombre, const ControlBomba& b, bool antes, Falla fallaAntes) {
  if (b.encendida != antes) Serial.printf("%s: %s (%s)\n", nombre, b.encendida ? "ENCENDIDA" : "apagada", b.motivo);
  if (b.falla != fallaAntes && b.falla != Falla::NINGUNA) Serial.printf("%s: FALLA -> %s\n", nombre, b.textoFalla());
}

// ---------- Programa ----------
void setup() {
  Serial.begin(115200);
  for (int p : {PIN_RELE_LLENADO, PIN_RELE_TRANSFER, PIN_RELE_GOTEO}) {
    pinMode(p, OUTPUT);
    aplicarRele(p, false);  // primero: todo apagado
  }
  pinMode(PIN_TRIG_R1, OUTPUT);
  pinMode(PIN_ECHO_R1, INPUT);
  pinMode(PIN_TRIG_R2, OUTPUT);
  pinMode(PIN_ECHO_R2, INPUT);
  pinMode(PIN_ORIGEN, INPUT_PULLUP);
  pinMode(PIN_BOTON, INPUT_PULLUP);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMAR, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  analogSetPinAttenuation(PIN_TURB_R1, ADC_11db);
  analogSetPinAttenuation(PIN_TURB_R2, ADC_11db);

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

    // 1) Llenado: origen → R1
    bool antes = llenado.encendida;
    Falla fallaAntes = llenado.falla;
    aplicarRele(PIN_RELE_LLENADO, llenado.actualizar({t1.nivel, t1.ok, origen, "Origen sin agua", t}));
    registrar("Llenado", llenado, antes, fallaAntes);

    // 2) Sedimentación en R1 (cualquier entrada de agua reinicia el reposo)
    sedimento.actualizar(t1.nivel, t1.ok, llenado.encendida, t);

    // 3) Transferencia: R1 → filtro → R2
    antes = transfer.encendida;
    fallaAntes = transfer.falla;
    float litrosAntes = transfer.litrosBombeados;
    bool disp = r1Disponible(t);
    aplicarRele(PIN_RELE_TRANSFER, transfer.actualizar({t2.nivel, t2.ok, disp, motivoR1, t}));
    registrar("Transferencia", transfer, antes, fallaAntes);

    // 4) Goteo de cloro: abierto mientras entra agua a R2
    bool goteoAntes = goteo.abierto;
    aplicarRele(PIN_RELE_GOTEO, goteo.actualizar(transfer.encendida, t));
    if (antes && !transfer.encendida) {
      goteo.cerrarCiclo(transfer.litrosBombeados - litrosAntes);
      Serial.printf("Goteo: %.0f gotas para %.0f L (dosis %.0f %%)\n", goteo.gotasCiclo,
                    transfer.litrosBombeados - litrosAntes, goteo.dosisPct);
    }
    if (goteo.abierto != goteoAntes) Serial.printf("Goteo: %s\n", goteo.abierto ? "ABIERTO" : "cerrado");

    Diagnostico d = diagnosticar({t1.ok, t2.ok, t1.nivel, t2.nivel, t2.turbidez, origen,
                                  &llenado, &transfer, &goteo, t});
    if (d.alerta != diag.alerta || strcmp(d.causa, diag.causa) != 0)
      Serial.printf("Estado: %s (%s)\n", nombreAlerta(d.alerta), d.causa);
    diag = d;
    actualizarLeds();
  }

  if (ahora - tHistorial >= INTERVALO_HISTORIAL_MS || tHistorial == 0) {
    tHistorial = ahora;
    guardarHistorial();
    Serial.printf("{\"r1\":%.1f,\"r2\":%.1f,\"ntu1\":%.0f,\"ntu2\":%.0f,\"llenado\":%d,\"transfer\":%d,"
                  "\"goteo\":%d,\"estado\":\"%s\"}\n",
                  t1.nivel, t2.nivel, t1.ntu, t2.ntu, llenado.encendida, transfer.encendida,
                  goteo.abierto, diag.causa);
  }

  if (ahora - tPantalla >= CAMBIO_PANTALLA_MS) {
    tPantalla = ahora;
    pantalla = (pantalla + 1) % 5;
    mostrarLcd(ahora);
  }

  actualizarBuzzer(ahora);
}
