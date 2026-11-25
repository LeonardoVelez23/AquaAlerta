// AquaAlerta – Arduino UNO: llenado de R1 y batido.
// Trabaja junto al ESP32 (firmware compilado con el entorno esp32dev_uno):
//   UNO  : mide el nivel de R1 y R2, llena R1 con la bomba de entrada y bate el agua
//          por un tiempo.
//   ESP32: mide la turbidez y, mientras el UNO bate, echa el PAC (dosis según turbidez
//          y litros). Luego deja sedimentar, purga, pasa el agua al filtro (solo si R2
//          no está lleno) y clora R2.
// Cada 0,5 s el UNO manda al ESP32 la línea:
//   N,<cm R1>,<cm R2>,<etapa>      (-1 si un sensor no responde)
//   etapa: 0 espera, 1 llenando, 2 batiendo, 3 terminó (espera que el ESP32 vacíe R1)
//
// Conexión al ESP32: UNO D7 (TX) → divisor 1 kΩ / 2 kΩ → GPIO16 del ESP32, y GND común.
// Si el equipo ya tiene su propio programa, basta con copiar enviarAlEsp() y llamarla
// cada 500 ms con sus distancias y su etapa.
#include <SoftwareSerial.h>

// ---------- Pines ----------
const int TRIG_R1 = 2, ECHO_R1 = 3;   // ultrasonido de R1
const int TRIG_R2 = 4, ECHO_R2 = 5;   // ultrasonido de R2 (tanque final)
const int ESP_RX = 6, ESP_TX = 7;     // D7 → ESP32 (D6 no se usa)
const int RELE_BOMBA_ENTRADA = 8;     // bomba que llena R1
const int RELE_MOTOR = 10;            // motor del batidor
const int RELE_LIBRE_1 = 9;           // relés sin uso (la bomba de PAC la maneja el ESP32)
const int RELE_LIBRE_2 = 11;
const bool RELE_ACTIVO_ALTO = false;  // la mayoría de módulos de relé son activo-bajo

// ---------- Recipiente R1 (medir con regla) ----------
const float R1_VACIO_CM = 25.0;  // distancia sensor–agua con R1 vacío
const float R1_LLENO_CM = 6.0;   // distancia con R1 lleno (deja margen bajo el sensor)

// ---------- Tiempos ----------
const unsigned long T_BATIDO_MS = 60000UL;        // motor del batidor (el ESP32 echa el PAC en este tiempo)
const unsigned long T_LLENADO_MAX_MS = 180000UL;  // seguridad: corta la bomba si tarda más
const unsigned long T_REINTENTO_MS = 60000UL;     // espera tras un llenado fallido

SoftwareSerial esp(ESP_RX, ESP_TX);

int etapa = 0;
unsigned long desde = 0, tEnvio = 0, esperarHasta = 0;
float cmR1 = -1, cmR2 = -1;

float medirCm(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long us = pulseIn(echo, HIGH, 30000UL);
  if (us == 0) return -1;
  float cm = us / 58.0;
  return (cm < 2 || cm > 400) ? -1 : cm;
}

void rele(int pin, bool on) { digitalWrite(pin, on == RELE_ACTIVO_ALTO ? HIGH : LOW); }

void apagarTodo() {
  rele(RELE_BOMBA_ENTRADA, false);
  rele(RELE_MOTOR, false);
}

void cambiar(int e) {
  etapa = e;
  desde = millis();
  Serial.print("Etapa ");
  Serial.println(e);
}

void enviarAlEsp(float r1, float r2, int e) {
  esp.print("N,");
  esp.print(r1, 1);
  esp.print(',');
  esp.print(r2, 1);
  esp.print(',');
  esp.println(e);
}

bool r1Vacio() { return cmR1 > 0 && cmR1 >= R1_VACIO_CM - 2; }
bool r1Lleno() { return cmR1 > 0 && cmR1 <= R1_LLENO_CM; }

void setup() {
  Serial.begin(9600);
  esp.begin(9600);
  const int reles[] = {RELE_BOMBA_ENTRADA, RELE_MOTOR, RELE_LIBRE_1, RELE_LIBRE_2};
  for (int p : reles) {
    pinMode(p, OUTPUT);
    rele(p, false);  // primero: todo apagado
  }
  pinMode(TRIG_R1, OUTPUT);
  pinMode(ECHO_R1, INPUT);
  pinMode(TRIG_R2, OUTPUT);
  pinMode(ECHO_R2, INPUT);
  Serial.println("AquaAlerta UNO");
}

void loop() {
  unsigned long ahora = millis();
  cmR1 = medirCm(TRIG_R1, ECHO_R1);
  delay(30);  // deja apagar el eco antes del otro sensor
  cmR2 = medirCm(TRIG_R2, ECHO_R2);

  switch (etapa) {
    case 0:  // espera: R1 vacío → llenar
      apagarTodo();
      if (r1Vacio() && (long)(ahora - esperarHasta) >= 0) {
        rele(RELE_BOMBA_ENTRADA, true);
        cambiar(1);
      }
      break;
    case 1:  // llenando R1
      if (r1Lleno()) {
        rele(RELE_BOMBA_ENTRADA, false);
        rele(RELE_MOTOR, true);  // el ESP32 ve la etapa 2 y echa el PAC
        cambiar(2);
      } else if (cmR1 < 0 || ahora - desde > T_LLENADO_MAX_MS) {
        // sensor sin respuesta o no se llena (sin agua, manguera suelta): corta
        apagarTodo();
        esperarHasta = ahora + T_REINTENTO_MS;
        Serial.println("Llenado cortado por seguridad");
        cambiar(0);
      }
      break;
    case 2:  // batiendo (el ESP32 echa el PAC)
      if (ahora - desde >= T_BATIDO_MS) {
        rele(RELE_MOTOR, false);
        cambiar(3);
      }
      break;
    case 3:  // terminó: el ESP32 deja sedimentar y pasa el agua al filtro
      apagarTodo();
      if (r1Vacio()) cambiar(0);
      break;
  }

  if (ahora - tEnvio >= 500) {
    tEnvio = ahora;
    enviarAlEsp(cmR1, cmR2, etapa);
  }
  delay(50);
}
