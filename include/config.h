// Configuración de pines, reservorios, tiempos y umbrales de AquaAlerta.
//
//  R1 (PAC + agitador: coagulación, floculación, sedimentación)
//   ├─ bomba de purga ──► lodo al desagüe / riego
//   └─ sin sedimentos ── bomba de transferencia ──► FILTRO ──► R2 (pH + gotas de cloro) ──► uso
#pragma once
#include <stdint.h>

// ---------- Modo de tiempos ----------
// 1 = DEMO: tiempos cortos para Wokwi y la feria (el ciclo completo dura minutos).
// 0 = REAL: tiempos de un aljibe (coagulación ~45 min, contacto de cloro 30 min).
// Se cambia aquí o al compilar: `pio run -e esp32dev_real`.
#ifndef MODO_DEMO
#define MODO_DEMO 1
#endif
// Elige el valor de demo o el real.
#define DEMO_O_REAL(demo, real) (MODO_DEMO ? (demo) : (real))

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
                                   // (pin de arranque: el driver debe tener pull-down en la entrada)
constexpr int PIN_VALVULA_R1 = 15; // electroválvula NC en la entrada de agua cruda a R1
constexpr int PIN_TDS_R1    = 39;  // sensor TDS (sales disueltas) en R1 (ADC1, VN, solo entrada)
constexpr int LCD_ADDR      = 0x27;
constexpr bool RELE_ACTIVO_ALTO = true;  // muchos módulos físicos son activo-bajo

// ---------- Arduino UNO (opcional) ----------
// USAR_UNO=1 (entorno esp32dev_uno): el UNO llena R1 y bate con su propio programa,
// mide el nivel de R1 y R2, y cada ~0,5 s manda por serie la línea
//   N,<cm R1>,<cm R2>,<etapa>      etapa: 0 espera, 1 llenando, 2 batiendo, 3 terminó de batir
// El ESP32 echa el PAC (turbidez × litros) mientras el UNO bate, deja sedimentar, purga,
// pasa el agua al filtro (solo con R1 clara y R2 sin llenar) y clora R2.
// Sus ultrasonidos, flotador, válvula de entrada y agitador no se usan.
#ifndef USAR_UNO
#define USAR_UNO 0
#endif
constexpr bool CON_UNO = USAR_UNO;
constexpr int PIN_UNO_RX = 16;                    // ← TX del UNO (D7) con divisor 1 kΩ / 2 kΩ
constexpr unsigned long UNO_BAUDIOS = 9600;
constexpr unsigned long UNO_SILENCIO_MS = 5000;   // sin mensajes: nivel "sin respuesta"
constexpr float UNO_R1_MIN_PCT = 10.0f;           // R1 con agua (reemplaza al flotador)

// ---------- Recipientes ----------
// Valores DE FÁBRICA. Se cambian sin recompilar desde la página («Recipientes»):
// a mano o con «Está vacío ahora» / «Está lleno ahora», y quedan guardados en
// la flash. Sirve cualquier recipiente de paredes rectas (frasco, balde,
// tanque, aljibe): litros = % de altura × capacidad.
// Ultrasonido: HC-SR04 mide desde ~2 cm (frascos chicos); JSN-SR04T es
// impermeable pero no mide los primeros ~20–25 cm (solo tanques grandes).
constexpr float DIST_RUIDO_CM = 0.5f;  // ruido del ultrasonido: los umbrales de subida nunca bajan de esto
constexpr float GEOM_MIN_ALTURA_CM = 3.0f;  // vacío − lleno mínimo aceptado

// ---------- R1 (agua cruda) ----------
constexpr float R1_DIST_VACIO_CM = 100.0f;  // sensor → fondo
constexpr float R1_DIST_LLENO_CM = 25.0f;   // sensor → nivel máximo
constexpr float R1_CAPACIDAD_L   = 500.0f;  // litros entre vacío y lleno
// Válvula de entrada: deja entrar agua cruda solo cuando R1 está en reposo
// (no coagula, no purga, no transfiere). Así el agua sin tratar nunca se mezcla
// con la tratada que está pasando a R2. Sin válvula instalada: false, y queda
// la protección por nivel (R1_SUBIDA_TRANSFER).
constexpr bool  USAR_VALVULA_R1 = true;
constexpr float R1_LLENO_MAX    = 95.0f;  // % — la válvula cierra al llegar aquí
constexpr float R1_RELLENAR     = 50.0f;  // % — la válvula abre (lote nuevo) por debajo
constexpr float R1_SUBIDA_VALVULA = 1.0f; // % que sube con la válvula abierta: hay que tratar
// Si con la válvula abierta R1 no sube, no está llegando agua: cierra y reintenta después.
constexpr unsigned long VALVULA_SIN_AGUA_MS  = DEMO_O_REAL(20UL, 5UL * 60UL) * 1000UL;
constexpr unsigned long VALVULA_REINTENTO_MS = DEMO_O_REAL(60UL, 30UL * 60UL) * 1000UL;

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
constexpr float PAC_CAUDAL_ML_MIN = 1000.0f;  // bomba dosificadora de PAC (medir los ml en 1 min)
constexpr float PAC_TANQUE_ML     = 20000.0f; // bidón de solución madre
constexpr float PAC_AVISO_PCT     = 20.0f;    // "Recargar PAC" bajo este %
constexpr float R1_SUBIDA_LOTE    = 5.0f;     // % que sube R1 para contar "agua nueva"
// Con la transferencia encendida R1 debería bajar: si sube esto, está entrando
// agua cruda y se corta la transferencia al momento (R1 vuelve a tratarse).
constexpr float R1_SUBIDA_TRANSFER = 2.0f;
// Agitador: motor DC con paleta, velocidad por PWM (lazo abierto). Calibrar:
// medir las RPM al 100 % de PWM (tacómetro o app del celular) y anotarlas aquí.
constexpr int AGIT_RPM_MAX    = 200;
constexpr int AGIT_RPM_RAPIDA = 150;  // coagulación: 140–160 RPM
constexpr int AGIT_RPM_LENTA  = 40;   // floculación: 40 RPM
constexpr int AGIT_PWM_FREQ   = 1000;
constexpr int AGIT_PWM_BITS   = 8;
constexpr unsigned long COAG_MEDIR_MS  = DEMO_O_REAL(5UL, 30UL) * 1000UL;        // homogeneizar antes de medir
constexpr unsigned long COAG_RAPIDA_MS = DEMO_O_REAL(15UL, 4UL * 60UL) * 1000UL;  // coagulación (3–5 min)
constexpr unsigned long COAG_LENTA_MS  = DEMO_O_REAL(20UL, 12UL * 60UL) * 1000UL; // floculación (10–15 min)
constexpr unsigned long COAG_SEDIM_MS  = DEMO_O_REAL(30UL, 30UL * 60UL) * 1000UL; // sedimentación

// ---------- R1: sedimentos y purga ----------
// El sensor de turbidez va cerca del fondo: mide cuánto sedimento hay.
constexpr float SED_MEDIO_NTU = 50.0f;    // desde aquí hay que purgar
constexpr float SED_ALTO_NTU  = 200.0f;   // mucho sedimento (solo informativo)
constexpr float SED_LIMPIO_NTU = 20.0f;   // la purga termina al bajar de aquí
// La purga va por pulsos: saca lodo, deja asentar, vuelve a medir.
constexpr unsigned long PURGA_PULSO_MS = DEMO_O_REAL(10UL, 30UL) * 1000UL;
constexpr unsigned long PURGA_PAUSA_MS = DEMO_O_REAL(15UL, 5UL * 60UL) * 1000UL;
constexpr int           PURGA_MAX_PULSOS = DEMO_O_REAL(6, 8);  // si no limpia: "Purga sin efecto"
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
// Real: 3 min (da tiempo a que se mezcle con lo que ya había).
constexpr unsigned long FILTRO_TURBIO_MS = DEMO_O_REAL(20UL, 3UL * 60UL) * 1000UL;

// ---------- TDS: sales y minerales disueltos en el agua de la fuente (R1) ----------
// Ni el filtro de arena/carbón ni el cloro quitan las sales: si la fuente es
// salobre (p. ej. un estero), tratarla gasta PAC y cloro en agua que no sirve.
// La sonda va en R1, lejos del pH de R2 (dos sondas en el mismo tanque se interfieren).
// Módulo tipo Gravity TDS alimentado a 3,3 V: salida 0–2,3 V, directo al pin.
// Se usa la lectura del agua cruda de cada lote (antes del PAC, que suma sales).
// OMS (aceptabilidad por sabor): < 600 mg/L bueno, > 1000 mg/L inaceptable.
constexpr float TDS_BUENO_PPM = 600.0f;
constexpr float TDS_MAX_PPM   = 1000.0f;  // más: aviso "Agua salobre"
constexpr float TDS_TEMP_C    = 25.0f;    // sin sensor de temperatura se asume 25 °C
// Calibración: con una solución patrón (p. ej. 342 ppm de NaCl), K = patrón / lectura.
constexpr float TDS_K         = 1.0f;
// true: con agua salobre no coagula ni pasa agua a R2 (ahorra insumos).
// false: solo avisa. Por defecto avisa, hasta calibrar el sensor.
constexpr bool  TDS_BLOQUEA   = false;

// ---------- Transferencia (R1 → filtro → R2) ----------
constexpr float R2_ENCENDER = 50.0f;   // % — auto: rellena R2 por debajo
constexpr float R2_APAGAR   = 90.0f;   // % — ...hasta aquí
constexpr float NIVEL_SEGURIDAD = 95.0f;  // % — corte forzado, incluso en manual
constexpr float NIVEL_CRITICO   = 10.0f;  // % — alerta roja: R2 casi vacío
constexpr float SUBIDA_MINIMA   = 2.0f;   // % que debe subir R2 para "sí pasa agua"
constexpr unsigned long BOMBA_SIN_SUBIR_MS    = DEMO_O_REAL(60UL, 3UL * 60UL) * 1000UL;
constexpr unsigned long BOMBA_MAX_MS          = DEMO_O_REAL(10UL, 60UL) * 60UL * 1000UL;
constexpr unsigned long BOMBA_MIN_APAGADA_MS  = DEMO_O_REAL(10UL, 30UL) * 1000UL;

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
// Real: 5 min (mezcla antes de medir pH) y 30 min de contacto (MSP).
constexpr unsigned long MEZCLA_MS         = DEMO_O_REAL(15UL, 5UL * 60UL) * 1000UL;
constexpr unsigned long CLORO_CONTACTO_MS = DEMO_O_REAL(60UL, 30UL * 60UL) * 1000UL;

// ---------- Interfaz ----------
constexpr unsigned long CAMBIO_PANTALLA_MS   = 3000;
constexpr unsigned long SILENCIO_MS          = 60000;
constexpr unsigned long INTERVALO_HISTORIAL_MS = 10000;  // un punto cada 10 s
constexpr int           HISTORIAL_PUNTOS     = 180;      // 30 min
constexpr unsigned long WIFI_ESPERA_MS       = 10000;    // luego crea su propia red
constexpr uint32_t      WATCHDOG_MS          = 20000;
// ThingSpeak (cuenta gratis): como máximo un envío cada 15 s. Ver include/nube.h.
constexpr unsigned long THINGSPEAK_INTERVALO_MS = DEMO_O_REAL(20UL, 60UL) * 1000UL;
// Respaldo en flash (NVS): insumos, litros por clorar y estado de los lotes
// sobreviven a un corte de luz. Los contadores se guardan como máximo cada
// minuto; los cambios de etapa, al momento.
constexpr unsigned long RESPALDO_MIN_MS = 60000;
