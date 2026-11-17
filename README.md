# AquaAlerta – tratamiento de agua en dos reservorios

**ODS 6** (agua limpia y saneamiento) · secundario **ODS 13** (resiliencia ante
sequía y uso responsable del agua).

**Problema:** en las casas rurales el agua llega por horas y a veces turbia. Se
guarda en aljibes o tanques que juntan sedimento, y se usa sin saber si está
desinfectada ni cuánto cloro ponerle.

**Solución:** un ESP32 trata el agua en **dos reservorios**:

```
     R1 · COAGULACIÓN Y SEDIMENTOS                               R2 · AGUA TRATADA
  nivel (ultrasonido) + turbidez (fondo)                        nivel (ultrasonido) + pH
  bomba de PAC + AGITADOR                                                  ▲
  flotador de nivel mínimo                                                 │
       │                                                                   │
       ├── bomba de PURGA ──► lodo al desagüe / riego             bomba dosificadora de cloro
       │                                                                   │
       └── (sin sedimentos) ── bomba de TRANSFERENCIA ──► FILTRO ──────────┘──► uso
```

1. **R1 – coagulación con PAC:** cuando entra agua nueva, el agitador la mezcla,
   el sensor mide la turbidez y el sistema calcula y dosifica el **PAC**
   (policloruro de aluminio). Luego mezcla rápido (coagulación), lento
   (floculación) y deja reposar (sedimentación): las partículas finas se juntan
   en flóculos que caen al fondo.
2. **R1 – purga:** el sensor de turbidez cerca del fondo mide **cuánto
   sedimento hay**. Si hay, la **bomba de purga** lo saca por pulsos.
3. **R1 → filtro → R2:** cuando R1 está **tratada y sin sedimentos**, la bomba de
   transferencia pasa el agua por el **filtro**, que retiene los residuos que
   quedan, hasta R2.
4. **R2 – pH y cloro:** cuando termina de entrar el agua, espera a que se
   mezcle, **mide el pH** y, según el pH y los **litros que entraron**, aplica
   las **gotas de cloro** necesarias. Después cuenta el tiempo de contacto y
   marca **"Agua lista"**.

Avisa con semáforo, buzzer y LCD, y publica una página web en la red local, o
en su **propia red WiFi** si en la casa no hay internet.

## Componentes

| Componente | Función | Pin ESP32 |
|---|---|---|
| Sensor de turbidez SEN0189 cerca del fondo de R1 · en Wokwi potenciómetro `turb` | Cantidad de sedimentos | GPIO 35 |
| Flotador de nivel mínimo en R1 · en Wokwi interruptor deslizante | ¿R1 tiene agua? (protege las bombas) | GPIO 19 |
| JSN-SR04T en R1 · en Wokwi HC-SR04 `ultraR1` | Litros de R1 (para la dosis de PAC) y llegada de agua nueva | TRIG 16 / ECHO 17 |
| Relé + bomba dosificadora de PAC · en Wokwi LED naranja | Coagulante | GPIO 32 |
| Motor DC con paleta + driver/MOSFET (PWM) · en Wokwi LED cian (brillo = velocidad) | Agitador: mezcla rápida y lenta | GPIO 2 |
| Relé + bomba de purga (fondo de R1) · en Wokwi LED azul | Saca el lodo | GPIO 23 |
| Relé + bomba de transferencia + filtro · en Wokwi LED morado | R1 → filtro → R2 | GPIO 33 |
| JSN-SR04T (impermeable) en R2 · en Wokwi HC-SR04 `ultra` | Nivel y litros de R2 | TRIG 5 / ECHO 18 |
| Sensor de pH (módulo PH-4502C + sonda) en R2 · en Wokwi potenciómetro `ph` | pH del agua | GPIO 34 |
| Sensor de turbidez SEN0189 en R2, donde cae el agua filtrada · en Wokwi potenciómetro `turb2` | ¿El filtro deja el agua clara? | GPIO 36 (VP) |
| Relé + bomba dosificadora peristáltica (o electroválvula de goteo) · en Wokwi LED blanco | Gotas de cloro | GPIO 13 |
| LED verde / amarillo / rojo | Semáforo | 26 / 27 / 14 |
| Buzzer | Alarma en rojo | GPIO 25 |
| LCD 16x2 I2C | 6 pantallas: R1, R2+pH, bombas, cloro, coagulación, red | SDA 21 / SCL 22 |
| Pulsador | Silencia 60 s y reinicia fallas | GPIO 4 |

## Cómo funciona cada etapa

### 1. Coagulación y floculación con PAC en R1

Es lo mismo que hace una planta potabilizadora, en pequeño. Se activa cada vez
que R1 sube al menos 5 % (entró agua nueva), al encender el equipo o con el
botón *Tratar R1 ahora*:

| Fase | Agitador | Tiempo (real) | Tiempo (simulación) |
|---|---|---|---|
| Homogeneizar y medir turbidez | 150 RPM | 30 s | 5 s |
| Dosificar PAC | 150 RPM | según los ml | según los ml |
| Coagulación (mezcla rápida) | 140–160 RPM | 3–5 min | 15 s |
| Floculación (mezcla lenta) | 40 RPM | 10–15 min | 20 s |
| Sedimentación (reposo) | quieto | 30 min | 30 s |

**Dosis por dilución:** C1·V1 = C2·V2, despejado como **V1 = C2·V2 / C1**:
- C1 = concentración de la solución madre de PAC (`PAC_STOCK_PPM`, 10 000 ppm = 1 %).
- C2 = dosis en el agua, en ppm, según la turbidez medida.
- V2 = litros de R1, medidos con el ultrasonido.
- Ejemplo: 200 ppm en 500 ml con solución al 1 % → 200 × 500 / 10 000 = **10 ml**.

**Turbidez → dosis** (`PAC_TABLA_NTU` / `PAC_TABLA_PPM`), interpolando entre puntos:

| Turbidez | Dosis de PAC |
|---|---|
| menos de 10 NTU | sin PAC (agua clara) |
| 10 NTU | 20 ppm |
| 20 NTU (época seca) | 30 ppm |
| 100 NTU (lluvia) | 200 ppm (tope) |

> ⚠ **Valores provisionales**, tomados de las notas del equipo. La dosis
> correcta **depende del agua** y se obtiene con una **prueba de jarras**: varios
> vasos con la misma agua, cada uno con una dosis distinta, mismas mezclas y
> reposo; se elige la dosis menor que deja el agua más clara. Con más
> turbidez que la tabla, usa la dosis tope y la página pide hacer la prueba de
> jarras. No hay que pasarse: el PAC de más deja aluminio en el agua.

- En un aljibe real (cientos de litros) conviene una solución madre al 10 %:
  con la del 1 %, 300 L a 200 ppm necesitan 6 L de solución.
- Lleva la cuenta del PAC del bidón ("Recargar PAC" bajo 20 %, "Sin PAC" en
  rojo). Después de rellenar, pulsar *Recargué el PAC*.
- Sin lectura de nivel en R1 no dosifica (no sabe cuántos litros hay): avisa
  "Falla nivel R1" y sigue con la purga y el filtro.
- Mientras coagula, no purga ni transfiere: el agua tiene que estar quieta para
  que caigan los flóculos.

### 2. Sedimentos y purga en R1

| Turbidez en el fondo | Sedimentos | Qué hace |
|---|---|---|
| < 50 NTU | pocos | R1 lista para pasar agua a R2 |
| 50 – 200 NTU | medios | purga |
| ≥ 200 NTU | muchos | purga |

- La purga va por **pulsos**: enciende la bomba 10 s, la apaga 15 s para que el
  lodo se asiente, vuelve a medir y repite si hace falta. Termina cuando el
  fondo baja de **20 NTU**. Así no se bota agua de más.
- Si tras **6 pulsos** sigue turbio, da la falla **"Purga sin efecto"**: puede
  que el agua esté turbia en todo el tanque (no es sedimento) o que la bomba
  esté tapada.
- Con el flotador abajo (R1 sin agua) no funciona ninguna bomba.
- El agua de la purga se puede usar para riego. La página muestra cuántos
  litros se usaron.
- En la simulación los tiempos son cortos; con un reservorio real conviene
  usar 30 s / 5 min y hasta 8 pulsos (`include/config.h`).

### 3. Transferencia (R1 → filtro → R2)
- Rellena R2 cuando baja del **50 %** y para al **90 %**.
- Solo funciona si R1 **ya fue coagulada**, **no tiene sedimentos**, tiene agua y R2 no se está
  clorando (R2 tiene que estar quieto para medir el pH).
- Si con la bomba encendida R2 no sube, da la falla **"Filtro tapado?"**: hay
  que limpiar el filtro o revisar la bomba o la manguera.
- Corta siempre al 95 % (desborde), también en modo manual.
- Si con la bomba encendida el agua de R2 sigue **turbia (> 5 NTU) por más de
  20 s** (en real, 3 min), apaga la bomba con la falla **"Filtro no limpia"**:
  el filtro está roto, saturado o mal colocado.

### 4. pH y gotas de cloro en R2

Cuando termina de entrar agua:

1. **Mezcla:** espera 15 s (en real, 5 min) para que el pH se estabilice.
2. **Revisa la turbidez de R2.** Si pasa de **5 NTU** (OMS), **no clora** y
   avisa "R2 turbia: filtro". El MSP pide filtrar el agua turbia antes de
   clorar, porque el cloro no desinfecta bien el agua turbia. Clora sola en
   cuanto el agua se aclara.
3. **Mide el pH.** El pH decide **si se puede clorar**; la dosis es la del MSP:

   | pH | ¿Clora? | Por qué |
   |---|---|---|
   | menos de 6,5 | sí, 1 gota/L, y avisa "pH bajo (ácido)" | el cloro funciona, pero el agua ácida es corrosiva |
   | 6,5 – 7,5 | sí, 1 gota/L | el cloro funciona mejor (OPS: pH 6,5–7) |
   | 7,5 – 8,0 | sí, 1 gota/L | funciona, algo menos eficaz |
   | más de 8,0 | **no**, avisa "pH alto: corregir" | la OMS pide pH < 8 para desinfectar; echar más cloro no lo arregla |

4. **Gotas = litros que entraron × 1 gota/L** (cloro comercial al 5 %). Por
   ejemplo, 300 L → 300 gotas ≈ 15 ml. Es la misma proporción de la tabla del
   MSP: 20 L → 20 gotas.
5. **Dosifica:** abre la bomba dosificadora el tiempo justo para esas gotas,
   según el caudal `GOTEO_GOTAS_MIN`.
6. **Contacto:** "Clorando: espere" durante 60 s (en real, **30 min**, como
   indica el MSP) y luego **"Agua lista"**.

- Si el pH está sobre 8, el sistema **espera**. Cuando se corrige el pH, clora
  solo.
- Lleva la cuenta del cloro que queda en la botella ("Recargar cloro" bajo
  20 %, "Sin cloro" en rojo). Después de rellenar, pulsar *Recargué el cloro*.
- El botón **Clorar todo R2** sirve, por ejemplo, después de un corte de luz:
  calcula la dosis para todos los litros de R2.
- **Si el cloro no es al 5 %:** gotas/L = 5 ÷ % de la etiqueta (por ejemplo,
  al 2,5 % son 2 gotas/L). Se cambia en `CLORO_GOTAS_POR_LITRO`. Usar cloro sin
  aroma ni detergente.

**Fuentes de la dosis y del pH:**
- MSP Ecuador, *Guía de Agua Segura* (2020), p. 43: cloro comercial al 5 % → 1
  gota por litro, 20 L → 20 gotas; esperar 30 min. En la p. 42 cita a la OPS:
  cloro libre residual ≥ 0,5 mg/L, 30 min de contacto, pH 6,5–7, turbiedad ≤ 1 UNT.
  <https://www.salud.gob.ec/wp-content/uploads/2020/12/Guia-Agua-Segura.pdf>
- OMS, *Guías para la calidad del agua potable*: cloro libre residual ≥ 0,5 mg/L
  tras 30 min a **pH < 8,0**; turbiedad < 5 NTU; máximo 5 mg/L por sabor.

### Límite del sensor de turbidez (importante para la evaluación)

El SEN0189 es un sensor barato pensado para agua bastante turbia. Cerca del
agua limpia su curva cambia ~3,7 NTU por milivoltio, así que **no distingue de
forma fiable 0 de 5 NTU**: detecta bien el agua **visiblemente turbia** y un
filtro que falla, pero no certifica el límite de la OMS. Para eso:

1. **Calibrar:** con agua embotellada en R2, anotar el voltaje en
   `TURB_R2_V_LIMPIA`; ese voltaje queda como 0 NTU.
2. **Verificar con un tubo de turbidez:** es un tubo transparente de ~60 cm con
   un disco blanco y negro en el fondo (se puede hacer con un tubo de PVC
   transparente). Se llena con agua de R2 mirando desde arriba: si se ve el
   disco con el tubo lleno, el agua está bajo ~5 NTU. Es el método de campo que
   usan programas de agua de la OMS y ONG.
3. Para una medida exacta, se necesita un turbidímetro de laboratorio. Se puede
   pedir a un laboratorio de la universidad o a la empresa de agua.

## Semáforo

| Color | Cuándo |
|---|---|
| 🟢 Verde | "Agua lista": R2 clorada y con el tiempo de contacto cumplido |
| 🟡 Amarillo | R2 turbia: filtro, pH alto: corregir, pH bajo (ácido), recargar cloro, recargar PAC, falla nivel R1, midiendo R1, dosificando PAC, coagulando, floculando, sedimentando, purgando lodo, R1 sin agua, R1 con lodo, midiendo pH, aplicando cloro, clorando: espere, R2 nivel bajo |
| 🔴 Rojo + buzzer | Falla del sensor de R2, "Filtro tapado?", "Filtro no limpia", "Purga sin efecto", R2 ≥ 95 %, R2 ≤ 10 %, sin cloro, sin PAC |

## Página web

Al arrancar intenta conectarse al WiFi de `include/secrets.h`. Si no puede en
10 s, crea la red **AquaAlerta** (contraseña en `secrets.h`) y la página queda en
<http://192.168.4.1>. La IP se ve en el LCD. No usa librerías de internet.

- R1 con su nivel, sus litros y la capa de lodo, R2 con su nivel, y los íconos
  PAC → agitador → purga → bomba → filtro, que se encienden cuando trabajan.
- Tarjeta de coagulación: etapa y tiempo restante, turbidez medida, dosis en
  ppm, ml de PAC aplicados, RPM del agitador y PAC que queda en el bidón.
- Barra de pH (0–14) con la zona verde, la tabla de pH, las gotas del agua
  por clorar, la última dosis (pH, gotas/L, gotas, ml) y el cloro restante.
- Turbidez de R2 (apta para clorar o no) y turbidez R1 → R2, que muestra cuánto limpia el filtro.
- Gráfico de los últimos 30 min: nivel de R2, sedimentos de R1, turbidez de R2, pH, y franjas
  de agitador/PAC, purga, transferencia y goteo.
- Control con PIN: purga y transferencia (automático / encender / apagar),
  reiniciar fallas, *Tratar R1 ahora*, *Recargué el PAC*, *Clorar todo R2* y *Recargué el cloro*.
- API JSON: `GET /api/estado`, `GET /api/historial`, `POST /api/control`
  (`accion=p_auto|p_on|p_off|t_auto|t_on|t_off|reset|clorar|recarga|tratar|pac_recarga`, `pin=…`).

## Cómo se mide (evaluación)

| Métrica | Cómo obtenerla |
|---|---|
| **Eficacia del PAC** | Prueba de jarras: turbidez del agua cruda y después de 30 min de reposo, con y sin PAC. La página muestra la turbidez medida, la dosis y los ml usados por lote. |
| **Eficacia de la purga** | La página muestra la turbidez del fondo antes y después de cada purga (ej. 180 → 15 NTU), y los litros usados. Probar con agua con 1, 2 y 5 cucharadas de tierra por litro. |
| **Eficacia del filtro** | Turbidez R1 → R2 en la página. Verificar el agua de R2 con el tubo de turbidez (¿se ve el disco con el tubo lleno?). |
| **Error del nivel de R2** | Llenar R2 a 5–6 alturas, medir con regla y comparar con la página. Reportar error promedio y máximo. |
| **Exactitud del pH** | Medir el mismo vaso con el sensor y con tiras de pH o un pH-metro de referencia. |
| **Dosis de cloro** | Comparar las gotas calculadas con las que sacó la bomba (medir los ml en una probeta). Con un kit de cloro residual (de piscina) se puede verificar el cloro en R2. |
| **Tiempo de respuesta** | Lo mide el ESP32: desde que enciende la transferencia hasta que R2 sube 2 %. |

> **Importante:** el sensor de turbidez mide **sedimentos y turbidez**, y el de
> pH mide la **acidez**. Ninguno detecta bacterias ni metales. El sistema
> ayuda a desinfectar con la dosis correcta, pero no reemplaza un análisis de
> laboratorio del agua.

## Simular en Wokwi (Antigravity / VS Code)

1. `pio run`
2. `F1` → **Wokwi: Start Simulator**
3. Abrir <http://localhost:8180> (Wokwi reenvía el puerto 80 del ESP32).
4. Recorrido sugerido. Al inicio R1 está al ~20 % (≈ 100 L) y sin sedimentos, R2 al ~27 % y el pH ≈ 7,1:
   - Al encender, R1 se trata: **agitador (LED cian)** 5 s midiendo; como el
     agua está clara, no usa PAC y queda "R1 tratada".
   - Para ver el PAC: pon `turb` cerca de la mitad (≈ 60 NTU) y pulsa
     *Tratar R1 ahora* (o baja la distancia de `ultraR1` 5 cm: entra agua
     nueva). Verás **"Dosificando PAC" (LED naranja)** con los ml calculados,
     **"Coagulando"** (LED cian fuerte, 150 RPM), **"Floculando"** (LED cian
     tenue, 40 RPM) y **"Sedimentando"**. Después arranca la purga porque el
     fondo sigue turbio: sube `turb` para que termine.
   - Arranca la **transferencia (LED morado)**. Como en la simulación el agua
     no sube sola, baja poco a poco la distancia de `ultra` (R2 se llena). Con
     ≤ 32 cm (90 %) se apaga.
   - R2: **"Midiendo pH"** 15 s → **"Aplicando cloro" (LED blanco)** con las
     gotas calculadas → **"Clorando: espere"** 60 s → **"Agua lista"** (verde).
   - Potenciómetro `ph` casi al mínimo → pH alto: no clora y pide corregir.
     Llévalo al centro y clora solo. Casi al máximo → pH bajo: clora, pero avisa.
   - Potenciómetro `turb` hacia abajo → sedimentos → se detiene la
     transferencia y arranca la **purga (LED azul)** por pulsos. Súbelo durante
     una pausa (agua limpia) y la purga termina. Si no lo subes, tras 6 pulsos
     aparece "Purga sin efecto".
   - Potenciómetro `turb2` hacia abajo → R2 turbia: no clora ("R2 turbia:
     filtro"). Si lo haces con la transferencia encendida, a los 20 s aparece
     "Filtro no limpia".
   - Interruptor deslizante → "R1 sin agua": ninguna bomba arranca.
   - PIN por defecto: `1234`.

## Armado físico

- **Sensor de sedimentos (turbidez)** en R1, a ~5 cm del fondo y al lado de la
  succión de la purga. La parte superior del sensor no es sumergible: montarlo
  en un tubo sellado con la punta hacia abajo. Salida de hasta 4,5 V → divisor
  10 kΩ / 20 kΩ.
- **Agitador:** motor DC de 12 V con una paleta (o un motor de batidora), controlado
  por PWM desde el GPIO 2 con un MOSFET logic-level (IRLZ44N) o un driver L298N,
  con diodo en el motor. El PWM no mide las RPM: medir las RPM al 100 % (tacómetro
  o app del celular con una marca en el eje) y ponerlas en `AGIT_RPM_MAX`.
- **Bomba de PAC:** peristáltica o de diafragma de 12 V con la manguera en el
  bidón de solución madre. Medir los ml que saca en 1 min y ponerlos en
  `PAC_CAUDAL_ML_MIN`. Que caiga cerca del agitador.
- **JSN-SR04T en R1**, en la tapa, igual que el de R2 (`R1_DIST_*`, `R1_CAPACIDAD_L`).
  ECHO 5 V → divisor 1 kΩ / 2 kΩ → GPIO 17.
- **Bomba de purga** con la succión en el punto más bajo del fondo (ideal: fondo
  en pendiente o cónico). La salida va a desagüe o a riego.
- **Bomba de transferencia** con la succión a 10–15 cm **sobre el fondo**, para
  no tomar lodo. Instalar el **filtro** en la manguera hacia R2 (cartucho de
  sedimentos 50 → 5 micras, o filtro casero de grava, arena y algodón).
- **Flotador** en R1 a la altura de la succión de transferencia: si el agua baja
  de ahí, ninguna bomba funciona.
- **Sensor de turbidez de R2** donde cae el agua del filtro, sumergido sin
  tocar el fondo, protegido de la luz directa (la luz altera la lectura).
  Divisor 10 kΩ / 20 kΩ → GPIO 36.
- **Sensor de pH:** sonda siempre húmeda (guardarla en solución de
  almacenamiento), **calibrar con buffers pH 7 y pH 4** y anotar los voltajes en
  `PH_V7` y `PH_V4`. Salida Po → divisor 10 kΩ / 20 kΩ → GPIO 34. Medir lejos de
  la entrada del cloro.
- **Dosificador:** bomba peristáltica de 12 V (~30 ml/min) con la manguera en la
  botella de cloro. Medir cuántos ml saca en 1 min y poner `ml × 20` en
  `GOTEO_GOTAS_MIN`. Que gotee sobre la entrada de agua de R2 para que se mezcle.
- **JSN-SR04T** en la tapa de R2, centrado. Zona ciega de ~20–25 cm
  (`R2_DIST_LLENO_CM`). **ECHO a 5 V** → divisor 1 kΩ / 2 kΩ.
- **Bombas con fuente propia**, GND común con el ESP32, relés con optoacoplador
  y diodo. Si son activo-bajo: `RELE_ACTIVO_ALTO = false`.
- Cambiar `AP_PASSWORD` y `PIN_WEB` en `include/secrets.h`.

## Estructura

```
include/config.h    pines, dosis de PAC, umbrales de sedimentos, dosis de cloro, rangos de pH, tiempos
include/control.h   lógica pura: coagulación, purga, transferencia, cloración por pH y semáforo
include/pagina.h    página web (HTML/CSS/JS embebido)
src/main.cpp        sensores, relés, LCD, WiFi/AP y servidor web
diagram.json        circuito de Wokwi
```
