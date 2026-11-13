// Configuración de pines, reservorios, tiempos y umbrales de AquaAlerta.
//
//  Origen ─bomba llenado─► R1 (sedimentación) ─bomba transferencia─► FILTRO ─► R2 (cloración) ─► uso
//                                                      gotero de cloro ─────────┘
#pragma once
#include <stdint.h>

// ---------- Pines (ESP32 DevKit C) ----------
constexpr int PIN_TRIG_R1   = 5;   // ultrasonido R1 (JSN-SR04T / HC-SR04)
constexpr int PIN_ECHO_R1   = 18;  // físico: divisor 5 V → 3,3 V
constexpr int PIN_TRIG_R2   = 16;  // ultrasonido R2
constexpr int PIN_ECHO_R2   = 17;  // físico: divisor 5 V → 3,3 V
constexpr int PIN_TURB_R1   = 35;  // turbidez R1, a la altura de la succión (ADC1)
constexpr int PIN_TURB_R2   = 34;  // turbidez R2, después del filtro (ADC1)
constexpr int PIN_ORIGEN    = 19;  // flotador del origen (LOW = hay agua)
constexpr int PIN_RELE_LLENADO  = 23;  // bomba origen → R1
constexpr int PIN_RELE_TRANSFER = 33;  // bomba R1 → filtro → R2
constexpr int PIN_RELE_GOTEO    = 13;  // electroválvula / bomba dosificadora del cloro
constexpr int PIN_LED_VERDE = 26;
constexpr int PIN_LED_AMAR  = 27;
constexpr int PIN_LED_ROJO  = 14;
constexpr int PIN_BUZZER    = 25;
constexpr int PIN_BOTON     = 4;   // silenciar alarma y reiniciar fallas
constexpr int LCD_ADDR      = 0x27;
constexpr bool RELE_ACTIVO_ALTO = true;  // muchos módulos físicos son activo-bajo

// ---------- Reservorios ----------
// Distancias medidas desde el sensor (en la tapa) hasta el agua.
struct Reservorio {
  float distVacioCm;  // sensor → fondo
  float distLlenoCm;  // sensor → nivel máximo (≥ zona ciega del JSN-SR04T, ~25 cm)
  float capacidadL;   // litros entre vacío y lleno
};
constexpr Reservorio R1 = {120.0f, 25.0f, 1000.0f};  // sedimentación
constexpr Reservorio R2 = {100.0f, 25.0f, 500.0f};   // agua tratada

// ---------- Lectura de sensores ----------
constexpr unsigned long INTERVALO_LECTURA_MS = 1000;
constexpr int   PINGS_POR_LECTURA   = 5;     // se usa la mediana (quita ecos falsos)
constexpr int   MUESTRAS_PROMEDIO   = 4;     // promedio móvil (oleaje al bombear)
constexpr int   FALLOS_NIVEL_ALERTA = 3;     // lecturas inválidas seguidas → falla
constexpr float DIST_MIN_VALIDA_CM  = 2.0f;
constexpr float DIST_MAX_VALIDA_CM  = 400.0f;

// Turbidez: curva típica del sensor SEN0189 (alimentado a 5 V), V en voltios.
// NTU = -1120.4·V² + 5742.3·V − 4352.9, válida aprox. entre 2,5 y 4,2 V.
constexpr float TURB_DIVISOR = 1.5f;   // divisor 10k/20k en la salida (4,5 V → 3 V)
constexpr float TURB_PRECAUCION_NTU = 50.0f;   // agua algo turbia
constexpr float TURB_ALTA_NTU       = 200.0f;  // agua muy turbia
constexpr float TURB_HISTERESIS_NTU = 20.0f;

// ---------- Bombas (valen para las dos) ----------
constexpr float NIVEL_SEGURIDAD  = 95.0f;  // % — corte forzado del destino, incluso en manual
constexpr float NIVEL_CRITICO    = 10.0f;  // % — alerta roja: R2 casi vacío
constexpr float SUBIDA_MINIMA    = 2.0f;   // % que debe subir el destino para "sí llena"
// Valores cortos para la simulación; en reservorios reales: 3 min, 60 min y 30 s.
constexpr unsigned long BOMBA_SIN_SUBIR_MS    = 60UL * 1000UL;
constexpr unsigned long BOMBA_MAX_MS          = 10UL * 60UL * 1000UL;
constexpr unsigned long BOMBA_MIN_APAGADA_MS  = 10UL * 1000UL;

// Llenado de R1 (origen → R1)
constexpr float R1_ENCENDER = 25.0f;   // % — auto: llena R1 por debajo
constexpr float R1_APAGAR   = 90.0f;   // % — ...hasta aquí

// ---------- Sedimentación en R1 ----------
// Tras entrar agua a R1 se deja reposar para que el sedimento baje al fondo.
// Simulación: 45 s. Real: 2 h o más (medir cuánto tarda en aclarar su agua).
constexpr unsigned long SEDIMENTACION_MS = 45UL * 1000UL;
constexpr float R1_NIVEL_MIN_TRANSFER = 20.0f;  // % — más abajo está el lodo: no se succiona
constexpr float R1_NTU_MAX_TRANSFER   = 50.0f;  // si el agua de R1 sigue turbia, se espera

// Transferencia (R1 → filtro → R2)
constexpr float R2_ENCENDER = 50.0f;   // % — auto: rellena R2 por debajo
constexpr float R2_APAGAR   = 90.0f;   // % — ...hasta aquí
constexpr float CAUDAL_TRANSFER_LPM = 10.0f;  // litros/min reales de la bomba con filtro (medir)

// ---------- Cloración por goteo en R2 ----------
// Guía común con cloro de uso doméstico (hipoclorito de sodio 5–6 %, sin aroma):
// 2 gotas por litro y 30 min de contacto antes de usar. Verificar con la guía
// del Ministerio de Salud y el % de la etiqueta.
constexpr float CLORO_GOTAS_POR_LITRO = 2.0f;
constexpr float GOTAS_POR_ML          = 20.0f;  // gotero estándar
// El gotero se abre mientras pasa agua a R2. Calibrarlo para que dé:
//   CAUDAL_TRANSFER_LPM × CLORO_GOTAS_POR_LITRO gotas/min  (10 L/min → 20 gotas/min)
constexpr float GOTEO_GOTAS_MIN    = CAUDAL_TRANSFER_LPM * CLORO_GOTAS_POR_LITRO;
constexpr float GOTERO_CAPACIDAD_ML = 1000.0f;  // botella de cloro del gotero
constexpr float GOTERO_AVISO_PCT    = 20.0f;    // avisa "recargar cloro" bajo este %
constexpr float DOSIS_TOLERANCIA_PCT = 20.0f;   // dosis real vs. necesaria: ±20 % está bien
// Simulación: 60 s. Real: 30 min.
constexpr unsigned long CLORO_CONTACTO_MS = 60UL * 1000UL;

// ---------- Métricas ----------
// Litros que se perdían en cada desborde ANTES del sistema (línea base medida a
// mano: duración del desborde × caudal de llenado). Reemplazar por el valor real.
constexpr float LITROS_DESBORDE_LINEA_BASE = 0.0f;

// ---------- Interfaz ----------
constexpr unsigned long CAMBIO_PANTALLA_MS   = 3000;
constexpr unsigned long SILENCIO_MS          = 60000;
constexpr unsigned long INTERVALO_HISTORIAL_MS = 10000;  // un punto cada 10 s
constexpr int           HISTORIAL_PUNTOS     = 180;      // 30 min
constexpr unsigned long WIFI_ESPERA_MS       = 10000;    // luego crea su propia red
constexpr uint32_t      WATCHDOG_MS          = 20000;
