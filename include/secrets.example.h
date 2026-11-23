// Copiar como include/secrets.h y completar. secrets.h no se sube a git.
#pragma once

// WiFi de la casa. Si no conecta en 10 s, el ESP32 crea su propia red (AP).
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""

// Red propia del ESP32 (contraseña de 8 caracteres o más).
#define AP_SSID       "AquaAlerta"
#define AP_PASSWORD   "cambiar123"

// PIN que pide la página para mover la bomba. Con los valores de fábrica
// ("1234" / "cambiar123") la página y el monitor serial muestran un aviso.
// La página va por HTTP sin cifrar: usarla solo en la red de la casa o la del ESP32.
// ThingSpeak: Write API Key del canal (pestaña "API Keys"). Vacío = no envía datos.
// Solo envía cuando el ESP32 está conectado al WiFi de arriba (con internet).
#define THINGSPEAK_WRITE_KEY ""
// Número del canal y Read API Key: la página los usa para mostrar lo que llegó a
// ThingSpeak (vista "ThingSpeak" del dashboard). La Read Key queda visible en la
// página: solo permite leer el canal, no escribir.
#define THINGSPEAK_CHANNEL_ID 0
#define THINGSPEAK_READ_KEY ""

#define PIN_WEB       "1234"
