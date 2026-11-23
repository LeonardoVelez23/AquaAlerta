// Envío de las mediciones a ThingSpeak cuando el ESP32 tiene internet.
//
// Corre en una tarea propia (núcleo 0) para que una conexión lenta no frene el
// control de las bombas (loop, núcleo 1). El loop deja una "muestra" cada 10 s
// con publicar(); la tarea manda la más reciente cada THINGSPEAK_INTERVALO_MS.
// Sin clave (THINGSPEAK_WRITE_KEY vacío) o sin internet (modo AP) no hace nada.
//
// Campos del canal (configurarlos con estos nombres en ThingSpeak):
//   field1 Turbidez R1 (NTU)   field2 Turbidez R2 (NTU)   field3 pH R2
//   field4 TDS R1 (ppm)        field5 Nivel R2 (%)        field6 Litros tratados
//   field7 PAC restante (%)    field8 Alerta (0 verde, 1 amarillo, 2 rojo)
//   status: "Lote N · <causa del estado>"
#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"

namespace nube {

struct Muestra {
  float ntuR1, ntuR2, ph, tds, nivelR2, litros, pac;
  int alerta;
  uint32_t lote;
  char causa[48];
};

// Lo que muestra la página (/api/estado → "nube").
struct Estado {
  bool activa = false;           // hay clave configurada
  uint32_t envios = 0, errores = 0;
  unsigned long ultimoOk = 0;    // millis() del último envío aceptado
  int ultimoCodigo = 0;          // código HTTP del último intento (<0: sin conexión)
};

inline Estado estado;
inline portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
inline Muestra muestra;
inline bool hayMuestra = false;
inline const char* clave = "";

// Llamar desde el loop: guarda la última muestra (copia protegida entre núcleos).
inline void publicar(const Muestra& s) {
  portENTER_CRITICAL(&mux);
  muestra = s;
  hayMuestra = true;
  portEXIT_CRITICAL(&mux);
}

inline String codificar(const String& s) {
  String r;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.') r += (char)c;
    else { r += '%'; r += hex[c >> 4]; r += hex[c & 15]; }
  }
  return r;
}

inline void tarea(void*) {
  WiFiClientSecure cliente;
  cliente.setInsecure();  // no valida el certificado: suficiente para el prototipo
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(THINGSPEAK_INTERVALO_MS));
    if (WiFi.getMode() != WIFI_STA || WiFi.status() != WL_CONNECTED) continue;  // sin internet

    Muestra s;
    bool hay;
    portENTER_CRITICAL(&mux);
    s = muestra;
    hay = hayMuestra;
    portEXIT_CRITICAL(&mux);
    if (!hay) continue;

    String cuerpo = String("api_key=") + clave;
    auto campo = [&](int n, float v, int dec) {
      if (!isnan(v)) cuerpo += "&field" + String(n) + "=" + String(v, dec);
    };
    campo(1, s.ntuR1, 0);
    campo(2, s.ntuR2, 1);
    campo(3, s.ph, 2);
    campo(4, s.tds, 0);
    campo(5, s.nivelR2, 1);
    campo(6, s.litros, 0);
    campo(7, s.pac, 0);
    campo(8, s.alerta, 0);
    cuerpo += "&status=" + codificar(String("Lote ") + s.lote + " · " + s.causa);

    HTTPClient http;
    http.setTimeout(8000);
    int codigo = -1;
    String resp;
    if (http.begin(cliente, "https://api.thingspeak.com/update")) {
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      codigo = http.POST(cuerpo);
      if (codigo > 0) resp = http.getString();
      http.end();
    }
    estado.ultimoCodigo = codigo;
    // ThingSpeak responde con el número de la entrada, o "0" si la rechazó (por ejemplo, muy seguido).
    if (codigo == 200 && resp.toInt() > 0) {
      estado.envios++;
      estado.ultimoOk = millis();
    } else {
      estado.errores++;
      Serial.printf("ThingSpeak: envio fallido (HTTP %d, respuesta '%s')\n", codigo, resp.c_str());
    }
  }
}

inline void iniciar(const char* k) {
  clave = k;
  if (!k || !*k) {
    Serial.println("ThingSpeak: sin clave (THINGSPEAK_WRITE_KEY), no se envian datos");
    return;
  }
  estado.activa = true;
  xTaskCreatePinnedToCore(tarea, "nube", 10240, nullptr, 1, nullptr, 0);
  Serial.printf("ThingSpeak: envio cada %lu s cuando haya internet\n", THINGSPEAK_INTERVALO_MS / 1000);
}

}  // namespace nube
