// Copiar como include/secrets.h y completar. secrets.h no se sube a git.
#pragma once

// WiFi de la casa. Si no conecta en 10 s, el ESP32 crea su propia red (AP).
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""

// Red propia del ESP32 (contraseña de 8 caracteres o más).
#define AP_SSID       "AquaAlerta"
#define AP_PASSWORD   "cambiar123"

// PIN que pide la página para mover la bomba.
#define PIN_WEB       "1234"
