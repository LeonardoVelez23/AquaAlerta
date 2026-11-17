// Configuración de pines, reservorios, tiempos y umbrales de AquaAlerta.
//
//  R1 (PAC + agitador: coagulación, floculación, sedimentación)
//   ├─ bomba de purga ──► lodo al desagüe / riego
//   └─ sin sedimentos ── bomba de transferencia ──► FILTRO ──► R2 (pH + gotas de cloro) ──► uso
#pragma once
#include <stdint.h>

// ---------- Pines (ESP32 DevKit C) ----------
constexpr int PIN_TURB_R1   = 35;  // sensor de sedimentos (turbidez) cerca del fondo de R1 (ADC1)
constexpr int PIN_FLOTADOR_R1 = 19;  // flotador de nivel mínimo en R1 (LOW = hay agua)
constexpr int PIN_TRIG_R2   = 5;   // ultrasonido R2 (JSN-SR04T / HC-SR04)
constexpr int PIN_ECHO_R2   = 18;  // físico: divisor 5 V → 3,3 V
constexpr int PIN_PH_R2     = 34;  // sensor de pH (módulo PH-4502C) en R2 (ADC1)
constexpr int PIN_TURB_R2   = 36;  // turbidez en R2, donde cae el agua filtrada (ADC1, VP)
constexpr int PIN_RELE_PURGA    = 23;  // bomba de purga de lodo (fondo de R1)
constexpr int PIN_RELE_TRANSFER = 33;  // bomba R1 → filtro → R2
constexpr int PIN_RELE_GOTEO    = 13;  // electroválvula / bomba dosificadora de cloro
constexpr int PIN_LED_VERDE = 26;
constexpr int PIN_LED_AMAR  = 27;
constexpr int PIN_LED_ROJO  = 14;
constexpr int PIN_BUZZER    = 25;
constexpr int PIN_BOTON     = 4;   // silenciar alarma y reiniciar fallas
constexpr int PIN_TRIG_R1   = 16;  // ultrasonido R1: litros para calcular la dosis de PAC
constexpr int PIN_ECHO_R1   = 17;  // físico: divisor 5 V → 3,3 V
constexpr int PIN_RELE_PAC  = 32;  // bomba dosificadora de PAC (coagulante) en R1
constexpr int PIN_AGITADOR  = 2;   // PWM → driver/MOSFET del motor del agitador de R1
constexpr int LCD_ADDR      = 0x27;
constexpr bool RELE_ACTIVO_ALTO = true;  // muchos módulos físicos son activo-bajo

// ---------- R1 (agua cruda) ----------
constexpr float R1_DIST_VACIO_CM = 100.0f;  // sensor → fondo
constexpr float R1_DIST_LLENO_CM = 25.0f;   // sensor → nivel máximo
constexpr float R1_CAPACIDAD_L   = 500.0f;  // litros entre vacío y lleno

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

// ---------- R1: coagulación y floculación con PAC ----------
// Ciclo por lote de agua nueva:
//   homogeneizar y medir turbidez → dosificar PAC → mezcla rápida (coagulación)
//   → mezcla lenta (floculación) → reposo (sedimentación) → purga del lodo.
// Dosis por dilución: C1·V1 = C2·V2 → V1 = C2·V2 / C1
//   C1 = solución madre de PAC, C2 = dosis en el agua, V2 = litros de R1.
//   Ej.: 200 ppm en 500 ml con solución al 1 % (10 000 ppm) → 10 ml de PAC.
constexpr float PAC_STOCK_PPM = 10000.0f;  // solución madre al 1 % (en un aljibe real conviene al 10 %)
constexpr float COAG_MIN_NTU  = 10.0f;     // agua más clara que esto: no se usa PAC
// Tabla turbidez → dosis. PROVISIONAL, de las notas del equipo (seca: 20 NTU →
// 20–40 ppm). Reemplazar con los resultados de la prueba de jarras del agua real.
// Sobre el último punto se usa la dosis tope y se avisa (hacer prueba de jarras).
constexpr int   PAC_PUNTOS = 3;
constexpr float PAC_TABLA_NTU[PAC_PUNTOS] = {10.0f, 20.0f, 100.0f};
constexpr float PAC_TABLA_PPM[PAC_PUNTOS] = {20.0f, 30.0f, 200.0f};
constexpr float PAC_CAUDAL_ML_MIN = 1000.0f;  // bomba dosificadora de PAC (medir)
constexpr float PAC_TANQUE_ML     = 20000.0f; // bidón de solución madre
constexpr float PAC_AVISO_PCT     = 20.0f;    // "Recargar PAC" bajo este %
constexpr float R1_SUBIDA_LOTE    = 5.0f;     // % que sube R1 para contar "agua nueva"
// Agitador: motor DC con paleta, velocidad por PWM (lazo abierto). Calibrar:
// medir las RPM al 100 % de PWM (tacómetro o app del celular) y anotarlas aquí.
constexpr int AGIT_RPM_MAX    = 200;
constexpr int AGIT_RPM_RAPIDA = 150;  // coagulación: 140–160 RPM
constexpr int AGIT_RPM_LENTA  = 40;   // floculación: 40 RPM
constexpr int AGIT_PWM_FREQ   = 1000;
constexpr int AGIT_PWM_BITS   = 8;
// Simulación: 5 s, 15 s, 20 s y 30 s. Real: 30 s, 3–5 min, 10–15 min y 30 min.
constexpr unsigned long COAG_MEDIR_MS  = 5UL * 1000UL;   // homogeneizar antes de medir
constexpr unsigned long COAG_RAPIDA_MS = 15UL * 1000UL;  // coagulación
constexpr unsigned long COAG_LENTA_MS  = 20UL * 1000UL;  // floculación
constexpr unsigned long COAG_SEDIM_MS  = 30UL * 1000UL;  // sedimentación

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

// ---------- Turbidez en R2 (control del filtro) ----------
// OMS: para que el cloro desinfecte, turbiedad < 5 NTU (ideal < 1 NTU, OPS).
// El MSP pide filtrar el agua turbia ANTES de clorar: con R2 turbia no se clora.
constexpr float TURB_R2_MAX_CLORO = 5.0f;  // NTU — más: no clora, revisar filtro
constexpr float TURB_R2_IDEAL     = 1.0f;  // NTU — solo informativo
// LÍMITE DEL SENSOR: el SEN0189 cambia ~3,7 NTU por mV cerca del agua limpia,
// así que no distingue de forma fiable 0 de 5 NTU: detecta agua visiblemente
// turbia. Calibración: medir el voltaje (ya × divisor) con agua embotellada en
// R2 y anotarlo aquí; la lectura se corre para que ese voltaje sea 0 NTU.
// Verificar los 5 NTU a mano con un tubo de turbidez (ver README).
constexpr float TURB_R2_V_LIMPIA = 4.20f;
// Si el agua que entra a R2 sigue turbia este tiempo con la bomba encendida:
// falla "Filtro no limpia" (filtro roto, saturado o mal instalado).
// Simulación: 20 s. Real: 3 min (da tiempo a que se mezcle con lo que ya había).
constexpr unsigned long FILTRO_TURBIO_MS = 20UL * 1000UL;

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
// Dosis: MSP Ecuador, "Guía de Agua Segura" (2020), dosis domiciliaria:
//   cloro comercial al 5 % → 1 gota por litro (20 L → 20 gotas), esperar 30 min.
//   Con otra concentración: gotas/L = 5 / % del producto (ej. 2,5 % → 2 gotas/L).
// pH: la OPS (citada en la misma guía) pide pH 6,5–7 y la OMS pH < 8,0 para que
// el cloro desinfecte. Ninguna de las dos sube la dosis según el pH: el pH decide
// SI se puede clorar, no cuántas gotas. Con pH > 8 echar más cloro no lo
// arregla y además pasaría el máximo de 5 mg/L de la OMS.
constexpr float CLORO_GOTAS_POR_LITRO = 1.0f;  // cloro comercial al 5 % (MSP)
constexpr float PH_MIN        = 6.5f;  // más ácido: sí clora, pero avisa (agua corrosiva)
constexpr float PH_OPTIMO_MAX = 7.5f;  // 6,5–7,5: el cloro funciona mejor
constexpr float PH_MAX        = 8.0f;  // más básico: NO clora, pide corregir el pH (OMS)
constexpr float GOTAS_POR_ML = 20.0f;  // gotero estándar
// Ritmo del dosificador abierto (medir: ml que saca en 1 min × 20).
// Bomba peristáltica pequeña ≈ 30 ml/min = 600 gotas/min (300 L → 300 gotas en 30 s).
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
