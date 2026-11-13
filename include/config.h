// Configuración de pines, reservorios, tiempos y umbrales de AquaAlerta.
//
//  R1 (sedimentos) ── bomba de purga ──► desagüe / riego
//   └─ sin sedimentos ── bomba de transferencia ──► FILTRO ──► R2 (pH + gotas de cloro) ──► uso
#pragma once
#include <stdint.h>

// ---------- Pines (ESP32 DevKit C) ----------
constexpr int PIN_TURB_R1   = 35;  // sensor de sedimentos (turbidez) cerca del fondo de R1 (ADC1)
constexpr int PIN_FLOTADOR_R1 = 19;  // flotador de nivel mínimo en R1 (LOW = hay agua)
constexpr int PIN_TRIG_R2   = 5;   // ultrasonido R2 (JSN-SR04T / HC-SR04)
constexpr int PIN_ECHO_R2   = 18;  // físico: divisor 5 V → 3,3 V
constexpr int PIN_PH_R2     = 34;  // sensor de pH (módulo PH-4502C) en R2 (ADC1)
constexpr int PIN_RELE_PURGA    = 23;  // bomba de purga de lodo (fondo de R1)
constexpr int PIN_RELE_TRANSFER = 33;  // bomba R1 → filtro → R2
constexpr int PIN_RELE_GOTEO    = 13;  // electroválvula / bomba dosificadora de cloro
constexpr int PIN_LED_VERDE = 26;
constexpr int PIN_LED_AMAR  = 27;
constexpr int PIN_LED_ROJO  = 14;
constexpr int PIN_BUZZER    = 25;
constexpr int PIN_BOTON     = 4;   // silenciar alarma y reiniciar fallas
constexpr int LCD_ADDR      = 0x27;
constexpr bool RELE_ACTIVO_ALTO = true;  // muchos módulos físicos son activo-bajo

// ---------- R2 (agua tratada) ----------
// Distancias medidas desde el sensor (en la tapa) hasta el agua.
constexpr float R2_DIST_VACIO_CM = 100.0f;  // sensor → fondo
constexpr float R2_DIST_LLENO_CM = 25.0f;   // sensor → nivel máximo (≥ zona ciega del JSN-SR04T)
constexpr float R2_CAPACIDAD_L   = 500.0f;  // litros entre vacío y lleno

// ---------- Lectura de sensores ----------
constexpr unsigned long INTERVALO_LECTURA_MS = 1000;
constexpr int   PINGS_POR_LECTURA   = 5;     // se usa la mediana (quita ecos falsos)
constexpr int   MUESTRAS_PROMEDIO   = 4;     // promedio móvil
constexpr int   FALLOS_NIVEL_ALERTA = 3;     // lecturas inválidas seguidas → falla
constexpr float DIST_MIN_VALIDA_CM  = 2.0f;
constexpr float DIST_MAX_VALIDA_CM  = 400.0f;

// Turbidez: curva típica del sensor SEN0189 (alimentado a 5 V), V en voltios.
// NTU = -1120.4·V² + 5742.3·V − 4352.9, válida aprox. entre 2,5 y 4,2 V.
constexpr float TURB_DIVISOR = 1.5f;   // divisor 10k/20k en la salida (4,5 V → 3 V)

// pH (módulo PH-4502C): calibrar con soluciones buffer pH 7 y pH 4 y anotar el
// voltaje de cada una (ya corregido por el divisor).
constexpr float PH_DIVISOR = 1.5f;     // divisor 10k/20k en la salida Po
constexpr float PH_V7 = 2.50f;         // voltaje con buffer pH 7
constexpr float PH_V4 = 3.05f;         // voltaje con buffer pH 4

// ---------- R1: sedimentos y purga ----------
// El sensor de turbidez va cerca del fondo: mide cuánto sedimento hay.
constexpr float SED_MEDIO_NTU = 50.0f;    // desde aquí hay que purgar
constexpr float SED_ALTO_NTU  = 200.0f;   // mucho sedimento (solo informativo)
constexpr float SED_LIMPIO_NTU = 20.0f;   // la purga termina al bajar de aquí
// La purga va por pulsos: saca lodo, deja asentar, vuelve a medir.
// Valores cortos para la simulación; en un reservorio real: 30 s, 5 min, 8.
constexpr unsigned long PURGA_PULSO_MS = 10UL * 1000UL;
constexpr unsigned long PURGA_PAUSA_MS = 15UL * 1000UL;
constexpr int           PURGA_MAX_PULSOS = 6;  // si no limpia: "Purga sin efecto"
constexpr float PURGA_CAUDAL_LPM = 15.0f;  // litros/min de la bomba de purga (medir)

// ---------- Transferencia (R1 → filtro → R2) ----------
constexpr float R2_ENCENDER = 50.0f;   // % — auto: rellena R2 por debajo
constexpr float R2_APAGAR   = 90.0f;   // % — ...hasta aquí
constexpr float NIVEL_SEGURIDAD = 95.0f;  // % — corte forzado, incluso en manual
constexpr float NIVEL_CRITICO   = 10.0f;  // % — alerta roja: R2 casi vacío
constexpr float SUBIDA_MINIMA   = 2.0f;   // % que debe subir R2 para "sí pasa agua"
// Simulación; en real: 3 min, 60 min y 30 s.
constexpr unsigned long BOMBA_SIN_SUBIR_MS    = 60UL * 1000UL;
constexpr unsigned long BOMBA_MAX_MS          = 10UL * 60UL * 1000UL;
constexpr unsigned long BOMBA_MIN_APAGADA_MS  = 10UL * 1000UL;

// ---------- Cloración en R2 según el pH ----------
// Cloro de uso doméstico (hipoclorito de sodio 5–6 %, sin aroma).
// El cloro desinfecta bien con pH 6,5–7,5; sobre 8 pierde mucho efecto y no se
// compensa echando más cloro: hay que corregir el pH.
// VALORES A VALIDAR con la guía del Ministerio de Salud y la etiqueta del cloro.
constexpr float PH_MIN       = 6.5f;   // más ácido: no se dosifica, corregir pH
constexpr float PH_OPTIMO_MAX = 7.5f;  // 6,5–7,5 → dosis normal
constexpr float PH_MAX       = 8.0f;   // 7,5–8,0 → dosis aumentada; más: corregir pH
constexpr float GOTAS_L_NORMAL    = 2.0f;  // gotas por litro con pH óptimo
constexpr float GOTAS_L_AUMENTADA = 2.5f;  // gotas por litro con pH 7,5–8,0
constexpr float GOTAS_POR_ML = 20.0f;      // gotero estándar
// Ritmo del dosificador abierto (medir: ml que saca en 1 min × 20).
// Bomba peristáltica pequeña ≈ 30 ml/min = 600 gotas/min (300 L → 600 gotas en 1 min).
// Un gotero por gravedad da ~60–120 gotas/min: sirve, pero tarda mucho más.
constexpr float GOTEO_GOTAS_MIN = 600.0f;
constexpr float GOTERO_CAPACIDAD_ML = 1000.0f;  // botella de cloro
constexpr float GOTERO_AVISO_PCT    = 20.0f;    // "Recargar cloro" bajo este %
// Simulación: 15 s y 60 s. Real: 5 min (mezcla antes de medir pH) y 30 min (contacto).
constexpr unsigned long MEZCLA_MS         = 15UL * 1000UL;
constexpr unsigned long CLORO_CONTACTO_MS = 60UL * 1000UL;

// ---------- Interfaz ----------
constexpr unsigned long CAMBIO_PANTALLA_MS   = 3000;
constexpr unsigned long SILENCIO_MS          = 60000;
constexpr unsigned long INTERVALO_HISTORIAL_MS = 10000;  // un punto cada 10 s
constexpr int           HISTORIAL_PUNTOS     = 180;      // 30 min
constexpr unsigned long WIFI_ESPERA_MS       = 10000;    // luego crea su propia red
constexpr uint32_t      WATCHDOG_MS          = 20000;
