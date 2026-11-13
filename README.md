# AquaAlerta – nivel y turbidez del agua en un tanque

**ODS 6** (agua limpia y saneamiento) · secundario **ODS 13** (resiliencia ante
sequía y uso responsable del agua).

**Problema:** en muchas casas rurales el tanque o la cisterna se desborda o se
vacía sin aviso, y nadie sabe si el agua está turbia.

**Solución:** un ESP32 mide el nivel (ultrasonido) y la turbidez, enciende y
apaga una bomba sola con protecciones, avisa con semáforo y buzzer, y publica
una página web en la red local, o en su **propia red WiFi** si en la casa no hay
internet.

```
Origen (cisterna/pozo) ──bomba──► Tanque
      │ flotador                   │ JSN-SR04T (tapa) + sensor de turbidez
      └──────────────► ESP32 ◄─────┘
                         ├─ relé → bomba        ├─ LEDs + buzzer + LCD
                         └─ página web (LAN o red "AquaAlerta")
```

## Componentes

| Componente | Función | Pin ESP32 |
|---|---|---|
| JSN-SR04T (impermeable) · en Wokwi HC-SR04 | Nivel por ultrasonido | TRIG 5 / ECHO 18 |
| Sensor de turbidez SEN0189 · en Wokwi potenciómetro | Turbidez (NTU) | GPIO 35 |
| Flotador en el origen · en Wokwi interruptor deslizante | ¿Hay agua para bombear? | GPIO 19 |
| Módulo relé + bomba 5–12 V · en Wokwi LED azul | Llenado | GPIO 23 |
| Módulo relé + bomba sumergible de fondo + filtro de sedimentos · en Wokwi LED morado | Filtrado (recirculación) | GPIO 33 |
| LED verde / amarillo / rojo | Semáforo | 26 / 27 / 14 |
| Buzzer | Alarma en rojo | GPIO 25 |
| LCD 16x2 I2C | Nivel, bomba, IP | SDA 21 / SCL 22 |
| Pulsador | Silencia 60 s y reinicia falla de bomba | GPIO 4 |

## Semáforo

| Color | Cuándo |
|---|---|
| 🟢 Verde | Todo normal |
| 🟡 Amarillo | Nivel bajo (≤ 25 %), agua turbia (≥ 50 NTU), origen sin agua |
| 🔴 Rojo + buzzer | Tanque casi vacío (≤ 10 %), riesgo de desborde (≥ 95 %), agua muy turbia (≥ 200 NTU), falla de bomba o del sensor de nivel |

## Control de la bomba

- **Automático con histéresis:** enciende bajo 25 % y apaga al llegar a 90 %.
  No se prende y apaga a cada rato.
- **Manual** desde la web (pide PIN).
- **Protecciones que valen siempre, también en manual:**
  - corte forzado al **95 %** → no hay desborde;
  - **origen sin agua** → no arranca (evita que la bomba trabaje en seco);
  - si en **1 min** el nivel no sube 2 % → falla "Bomba no llena" (bomba en
    seco, tubo roto o filtro tapado). Queda bloqueada hasta reiniciar con el
    botón o la web;
  - **tiempo máximo** encendida (10 min) y **reposo mínimo** entre arranques (10 s);
  - sin lectura del sensor de nivel → apagada.

Los tiempos están acortados para la simulación. En un tanque real conviene
usar 3 min, 60 min y 30 s (`include/config.h`).

## Filtrado de sedimentos

Una bomba pequeña en el **fondo del tanque** saca el agua con sedimento, la
pasa por un **filtro de sedimentos** (cartucho o filtro casero de arena y grava)
y la **devuelve** al tanque por el lado opuesto, para que el agua circule.

- **Automático:** filtra cuando la turbidez llega a **50 NTU** y sigue hasta
  bajar a **20 NTU**. También filtra un rato **después de cada llenado**, porque
  el agua que entra levanta el sedimento del fondo.
- **Protecciones:**
  - no filtra con el tanque bajo **20 %** (la bomba quedaría en seco o
    chuparía lodo);
  - no filtra mientras la bomba de llenado está encendida;
  - si en **2 min** la turbidez no baja al menos **20 %**, avisa
    **"Limpiar filtro"** (filtro tapado o saturado) y se bloquea hasta
    reiniciar con el botón o con *Ya limpié el filtro* en la web;
  - tiene un **tiempo máximo** encendida y un reposo mínimo entre arranques.
- **Métricas:** ciclos, litros filtrados (tiempo × caudal
  `FILTRO_CAUDAL_LPM`) y **cuánto bajó la turbidez** en el último ciclo
  (ej. 120 → 18 NTU = −85 %).

En Wokwi, para simular el filtrado gira el potenciómetro de turbidez hacia
"limpia" mientras el LED morado está encendido. Si no lo giras, a los 2 min
aparece "Limpiar filtro".

## Cloración: gotas de cloro según los litros

El sensor de nivel ya sabe cuántos litros hay, así que el sistema calcula el
cloro necesario:

- **Agua nueva sin clorar:** los litros que entraron con la bomba desde la
  última vez que se pulsó *Ya puse el cloro*.
- **Todo el tanque:** para cuando el tanque se llenó por otro lado (tanquero,
  lluvia).
- Dosis: **2 gotas por litro** de cloro doméstico (hipoclorito 5–6 %, sin
  aroma). Se muestra en gotas y en ml (20 gotas ≈ 1 ml), por ejemplo
  500 L → 1000 gotas ≈ 50 ml.
- Tras pulsar *Ya puse el cloro* cuenta **30 min** de espera antes de tomar el agua.
- Si el agua está turbia, dice **"filtre primero"**, porque el cloro no
  desinfecta bien el agua turbia.
- La dosis se cambia en `CLORO_GOTAS_POR_LITRO` (`include/config.h`). Hay que
  confirmarla con la guía del Ministerio de Salud y la concentración que dice la
  etiqueta del cloro.

En el LCD también aparecen dos pantallas nuevas: estado del filtro y cloro.

## Página web

Al arrancar intenta conectarse al WiFi de `include/secrets.h`. Si no puede en
10 s, crea la red **AquaAlerta** (contraseña en `secrets.h`) y la página queda en
<http://192.168.4.1>. La IP se ve en el LCD. No usa librerías de internet, así que
funciona sin conexión.

- Tanque animado, bomba, turbidez, origen y modo.
- Gráfico de los últimos 30 min (nivel + franjas cuando la bomba estuvo encendida).
- Botones Automático / Encender / Apagar / Reiniciar falla (con PIN).
- Métricas del proyecto.
- API JSON: `GET /api/estado`, `GET /api/historial`, `POST /api/control`
  (`accion=auto|on|off|reset|f_auto|f_on|f_off|f_reset|clorado`, `pin=…`).

## Cómo se mide (evaluación)

| Métrica | Cómo obtenerla |
|---|---|
| **Error del nivel** | Llenar el tanque a 5–6 alturas, medir con regla graduada y anotar lo que dice la página. Reportar error promedio y máximo (cm o %). |
| **Litros no desperdiciados** | 1) **Línea base sin el sistema:** cronometrar cuánto dura un desborde y medir el caudal de llenado (litros que llena un balde en X s) → litros por desborde. 2) Ponerlo en `LITROS_DESBORDE_LINEA_BASE`. 3) La página multiplica por los *llenados sin desborde*. |
| **Tiempo de respuesta de la bomba** | Lo mide el ESP32: desde que se enciende el relé hasta que el nivel sube 2 %. Aparece en la página. |
| Turbidez (cualitativa) | Comparar con muestras: agua limpia, y agua con 1, 2 y 5 cucharadas de tierra por litro. |

> **Importante:** el sensor de turbidez indica si el agua está **turbia**, no si
> es **potable**. No detecta bacterias, cloro ni metales.

## Simular en Wokwi (Antigravity / VS Code)

1. `pio run`
2. `F1` → **Wokwi: Start Simulator**
3. Abrir <http://localhost:8180> (Wokwi reenvía el puerto 80 del ESP32).
4. Probar:
   - Clic en el sensor ultrasónico → subir la **distancia** a ~105 cm (nivel ~15 %)
     → amarillo y la bomba (LED azul) enciende.
   - Como en la simulación el agua no sube sola, **bajar la distancia** poco a
     poco (llenado). Si no lo haces en 1 min → falla "Bomba no llena".
   - Distancia ≤ 30 cm (≥ 95 %) → corte por desborde y rojo.
   - Interruptor deslizante a la derecha → "Origen sin agua", la bomba no arranca.
   - Potenciómetro hacia abajo → agua turbia / muy turbia (al máximo = limpia).
   - PIN por defecto: `1234`.

## Armado físico

- **JSN-SR04T** en la tapa, centrado, lejos de la entrada de agua. Su zona ciega
  es de ~20–25 cm: el nivel máximo debe quedar al menos a esa distancia
  (`DIST_LLENO_CM`). Medir `DIST_VACIO_CM` con el tanque vacío.
- **ECHO a 5 V** → divisor 1 kΩ / 2 kΩ antes del GPIO 18.
- **Turbidez:** salida de hasta 4,5 V → divisor 10 kΩ / 20 kΩ (`TURB_DIVISOR = 1.5`).
  La parte superior del sensor no es sumergible.
- **Bomba con fuente propia** (5–12 V), **GND común** con el ESP32, módulo relé
  con optoacoplador y diodo. Si el relé es activo-bajo: `RELE_ACTIVO_ALTO = false`.
- Cambiar `AP_PASSWORD` y `PIN_WEB` en `include/secrets.h`.

## Estructura

```
include/config.h    pines, tanque, umbrales y tiempos
include/control.h   lógica pura: nivel, turbidez, semáforo y control de bomba
include/pagina.h    página web (HTML/CSS/JS embebido)
src/main.cpp        sensores, relé, LCD, WiFi/AP y servidor web
diagram.json        circuito de Wokwi
```
