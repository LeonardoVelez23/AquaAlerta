# AquaAlerta – tratamiento de agua en dos reservorios

**ODS 6** (agua limpia y saneamiento) · secundario **ODS 13** (resiliencia ante
sequía y uso responsable del agua).

**Problema:** en las casas rurales el agua llega por horas y a veces turbia. Se
guarda en aljibes o tanques donde se desborda, se acaba sin aviso, junta
sedimento y se usa sin saber si está desinfectada.

**Solución:** un ESP32 trata el agua en **dos reservorios**:

```
Origen ──bomba llenado──► R1 SEDIMENTACIÓN ──bomba transferencia──► FILTRO ──► R2 AGUA TRATADA ──► uso
 flotador                 ultrasonido 1                                ▲        ultrasonido 2
                          turbidez 1 (a la altura                gotero de      turbidez 2
                          de la succión)                         cloro          (después del filtro)
```

1. **R1 – sedimentación:** el agua entra y **reposa** para que la tierra baje al
   fondo. Mientras reposa, no se saca agua.
2. **Bomba + filtro:** cuando R1 ya reposó y R2 necesita agua, la bomba pasa el
   agua clara de R1 por un **filtro**, que retiene los residuos que quedan.
3. **R2 – cloración por goteo:** mientras entra agua a R2 se abre un **gotero de
   cloro**, así la dosis es proporcional a los litros. Luego cuenta el
   **tiempo de contacto** antes de marcar el agua como lista.

Avisa con semáforo, buzzer y LCD, y publica una página web en la red local, o
en su **propia red WiFi** si en la casa no hay internet.

## Componentes

| Componente | Función | Pin ESP32 |
|---|---|---|
| JSN-SR04T (impermeable) en R1 · en Wokwi HC-SR04 `ultra` | Nivel R1 | TRIG 5 / ECHO 18 |
| JSN-SR04T en R2 · en Wokwi HC-SR04 `ultra2` | Nivel R2 | TRIG 16 / ECHO 17 |
| Sensor de turbidez SEN0189 en R1 · en Wokwi potenciómetro `turb` | ¿Ya sedimentó? | GPIO 35 |
| Sensor de turbidez SEN0189 en R2 · en Wokwi potenciómetro `turb2` | ¿El filtro funciona? | GPIO 34 |
| Flotador en el origen · en Wokwi interruptor deslizante | ¿Hay agua para llenar R1? | GPIO 19 |
| Relé + bomba de llenado · en Wokwi LED azul | Origen → R1 | GPIO 23 |
| Relé + bomba de transferencia + filtro · en Wokwi LED morado | R1 → filtro → R2 | GPIO 33 |
| Relé + electroválvula de goteo o bomba dosificadora · en Wokwi LED blanco | Cloro en R2 | GPIO 13 |
| LED verde / amarillo / rojo | Semáforo | 26 / 27 / 14 |
| Buzzer | Alarma en rojo | GPIO 25 |
| LCD 16x2 I2C | 5 pantallas: R1, R2, bombas, goteo, red | SDA 21 / SCL 22 |
| Pulsador | Silencia 60 s y reinicia fallas | GPIO 4 |

## Cómo funciona cada etapa

### 1. Llenado de R1 (origen → R1)
- Llena R1 cuando baja del **25 %** y para al **90 %**.
- No arranca si el origen está sin agua. Corta siempre al **95 %** (desborde).
- Si en 1 min R1 no sube 2 %, da la falla "R1 no se llena".

### 2. Sedimentación en R1
- Cada vez que entra agua a R1, sea por la bomba o por otro lado (tanquero,
  lluvia), el sistema lo detecta por la subida del nivel y **reinicia el
  reposo**.
- Tiempo de reposo: `SEDIMENTACION_MS`. En la simulación son 45 s; con agua real,
  **medir** cuánto tarda en aclarar (normalmente 2 h o más).
- Antes de transferir revisa:
  - que R1 esté sobre el **20 %**, porque más abajo está el lodo;
  - que la turbidez de R1 sea **≤ 50 NTU** (si sigue turbia, espera más).

### 3. Transferencia (R1 → filtro → R2)
- Rellena R2 cuando baja del **50 %** y para al **90 %**.
- No funciona mientras R1 se llena, sedimenta o está bajo. La página dice el motivo.
- Si con la bomba encendida R2 no sube, da la falla **"Filtro tapado?"**: hay
  que limpiar el filtro o revisar la bomba o la manguera.
- La turbidez de R2 permite verificar el filtro: si R2 sale turbia, aparece
  "R2 turbia:filtro".

### 4. Cloración por goteo en R2
- El gotero se abre **solo mientras la bomba de transferencia está encendida**.
- **Calibración:** la dosis es de 2 gotas por litro de cloro doméstico
  (hipoclorito 5–6 %, sin aroma). Si la bomba pasa 10 L/min, el gotero debe dar
  **20 gotas/min**: contar las gotas de 1 min y ajustar la llave del gotero.
  Poner el caudal real en `CAUDAL_TRANSFER_LPM`.
- Al terminar cada transferencia compara las gotas aplicadas con las que hacían
  falta según los litros que **midió** el sensor de R2. Si la diferencia pasa
  ±20 %, aparece "Revisar goteo".
- Lleva la cuenta del cloro que queda en la botella del gotero. Avisa
  "Recargar cloro" bajo el 20 % y "Sin cloro" en rojo. Después de rellenar,
  pulsar *Recargué el cloro* en la web.
- **Tiempo de contacto:** después de clorar, R2 muestra "Clorando: espere" hasta
  que pasan 30 min (60 s en la simulación). Luego cambia a **"Agua lista"**.
- La página también da las gotas necesarias para clorar R2 a mano, por si el
  gotero falla.
- La dosis se cambia en `CLORO_GOTAS_POR_LITRO`. Hay que **confirmarla con la
  guía del Ministerio de Salud** y el porcentaje de la etiqueta del cloro.

## Semáforo

| Color | Cuándo |
|---|---|
| 🟢 Verde | "Agua lista": R2 con agua tratada y tiempo de contacto cumplido |
| 🟡 Amarillo | R2 turbia, recargar cloro, revisar goteo, clorando (esperar), origen sin agua, R2 bajo 50 % |
| 🔴 Rojo + buzzer | Falla de sensor de nivel, falla de bomba o "Filtro tapado?", desborde de R1 o R2 (≥ 95 %), R2 casi vacío (≤ 10 %), R2 muy turbia, sin cloro |

## Página web

Al arrancar intenta conectarse al WiFi de `include/secrets.h`. Si no puede en
10 s, crea la red **AquaAlerta** (contraseña en `secrets.h`) y la página queda en
<http://192.168.4.1>. La IP se ve en el LCD. No usa librerías de internet.

- Los dos reservorios animados (R1 con lodo en el fondo y línea de succión
  mínima), y los íconos bomba → filtro → goteo, que se encienden cuando
  trabajan.
- Estado de cada bomba, del gotero y de la sedimentación (cuánto falta).
- Cloración: gotas del último llenado, dosis aplicada (%), cloro restante y
  gotas para clorar a mano.
- Gráfico de los últimos 30 min: nivel de R1 y R2, franjas de transferencia y
  de goteo.
- Control con PIN: llenado y transferencia (automático / encender / apagar),
  reiniciar fallas y *Recargué el cloro*.
- API JSON: `GET /api/estado`, `GET /api/historial`, `POST /api/control`
  (`accion=l_auto|l_on|l_off|t_auto|t_on|t_off|reset|recarga`, `pin=…`).

## Cómo se mide (evaluación)

| Métrica | Cómo obtenerla |
|---|---|
| **Error del nivel** | Llenar cada reservorio a 5–6 alturas, medir con regla y comparar con la página. Reportar error promedio y máximo. |
| **Reducción de turbidez** | La página muestra *Turbidez R1 → R2* (ej. 180 → 12 NTU = −93 %). Probar con agua con 1, 2 y 5 cucharadas de tierra por litro. |
| **Precisión de la dosis de cloro** | *Dosis aplicada* (%) de cada ciclo: gotas reales vs. 2 gotas × litros medidos. Lo ideal es entre 80 y 120 %. Con un kit de cloro residual (piscina) se puede verificar en R2. |
| **Litros no desperdiciados** | Línea base sin el sistema: duración de un desborde × caudal. Ponerla en `LITROS_DESBORDE_LINEA_BASE`; la página la multiplica por los llenados sin desborde. |
| **Tiempo de respuesta** | Lo mide el ESP32: desde que enciende la transferencia hasta que R2 sube 2 %. |

> **Importante:** el sensor de turbidez indica si el agua está **turbia**, no si
> es **potable**. No detecta bacterias, cloro ni metales. El cloro desinfecta
> bien solo agua **clara**: por eso primero se sedimenta y se filtra.

## Simular en Wokwi (Antigravity / VS Code)

1. `pio run`
2. `F1` → **Wokwi: Start Simulator**
3. Abrir <http://localhost:8180> (Wokwi reenvía el puerto 80 del ESP32).
4. Recorrido sugerido. Al inicio R1 está al ~63 % y R2 al ~27 %:
   - Durante 45 s R1 está **sedimentando**: la transferencia espera.
   - Luego se encienden la **transferencia (LED morado)** y el **goteo (LED
     blanco)**. Como en la simulación el agua no sube sola, baja poco a poco la
     distancia de `ultra2` (R2 se llena). Si no lo haces en 1 min, aparece
     "Filtro tapado?".
   - Con `ultra2` en ≤ 32 cm (R2 ≥ 90 %) la transferencia y el
     goteo se apagan. Se ve la dosis y empieza el tiempo de contacto ("Clorando:
     espere"). A los 60 s el estado cambia a **"Agua lista"** (verde).
   - Sube la distancia de `ultra` (R1) a ~105 cm → R1 baja al 15 % → se enciende
     el **llenado (LED azul)**. Al bajarla otra vez (llenando), el reposo se
     reinicia.
   - Potenciómetro `turb` hacia abajo → R1 turbia → la transferencia espera.
     Potenciómetro `turb2` hacia abajo → R2 turbia → "R2 turbia:filtro".
   - Interruptor deslizante → "Origen sin agua".
   - PIN por defecto: `1234`.

## Armado físico

- **Succión de la bomba de transferencia:** a 10–15 cm **sobre el fondo** de R1
  (nunca en el fondo), para no levantar el lodo. Ese nivel corresponde a
  `R1_NIVEL_MIN_TRANSFER`. Purgar el lodo de R1 de vez en cuando con una llave
  en el fondo.
- **Filtro** en la manguera entre la bomba y R2: cartucho de sedimentos
  (50 → 5 micras) o filtro casero de grava, arena y algodón. Limpiarlo o
  cambiarlo cuando aparezca "Filtro tapado?".
- **Gotero:** botella con cloro y llave reguladora, con una electroválvula de
  12 V normalmente cerrada que maneja el relé del GPIO 13, o una bomba
  peristáltica pequeña. Que gotee sobre el chorro de entrada a R2 para que se
  mezcle.
- **JSN-SR04T** en cada tapa, centrado y lejos de la entrada de agua. Zona ciega
  de ~20–25 cm (`distLlenoCm`). Medir `distVacioCm` con cada reservorio vacío.
  **ECHO a 5 V:** usar un divisor de 1 kΩ / 2 kΩ antes de cada GPIO.
- **Turbidez:** salida de hasta 4,5 V → divisor 10 kΩ / 20 kΩ. El sensor de R1
  va a la altura de la succión. La parte superior del sensor no es sumergible.
- **Bombas y válvula con fuente propia**, GND común con el ESP32, relés con
  optoacoplador y diodo. Si son activo-bajo: `RELE_ACTIVO_ALTO = false`.
- Cambiar `AP_PASSWORD` y `PIN_WEB` en `include/secrets.h`.

## Estructura

```
include/config.h    pines, reservorios, umbrales, tiempos y dosis de cloro
include/control.h   lógica pura: bombas, sedimentación, goteo y semáforo
include/pagina.h    página web (HTML/CSS/JS embebido)
src/main.cpp        sensores, relés, LCD, WiFi/AP y servidor web
diagram.json        circuito de Wokwi
```
