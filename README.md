# AquaAlerta – tratamiento de agua en dos reservorios

**ODS 6** (agua limpia y saneamiento) · secundario **ODS 13** (resiliencia ante
sequía y uso responsable del agua).

**Problema:** en las casas rurales el agua llega por horas y a veces turbia. Se
guarda en aljibes o tanques que juntan sedimento, y se usa sin saber si está
desinfectada ni cuánto cloro ponerle.

**Solución:** un ESP32 trata el agua en **dos reservorios**:

```
            R1 · SEDIMENTOS                                      R2 · AGUA TRATADA
  sensor de sedimentos (fondo)                                  nivel (ultrasonido) + pH
  flotador de nivel mínimo                                                 ▲
       │                                                                   │
       ├── bomba de PURGA ──► lodo al desagüe / riego             bomba dosificadora de cloro
       │                                                                   │
       └── (sin sedimentos) ── bomba de TRANSFERENCIA ──► FILTRO ──────────┘──► uso
```

1. **R1 – sedimentos:** un sensor de turbidez cerca del fondo mide **cuánto
   sedimento hay**. Si hay, la **bomba de purga** lo saca por pulsos.
2. **R1 → filtro → R2:** cuando R1 está **sin sedimentos**, la bomba de
   transferencia pasa el agua por el **filtro**, que retiene los residuos que
   quedan, hasta R2.
3. **R2 – pH y cloro:** cuando termina de entrar el agua, espera a que se
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
| Relé + bomba de purga (fondo de R1) · en Wokwi LED azul | Saca el lodo | GPIO 23 |
| Relé + bomba de transferencia + filtro · en Wokwi LED morado | R1 → filtro → R2 | GPIO 33 |
| JSN-SR04T (impermeable) en R2 · en Wokwi HC-SR04 `ultra` | Nivel y litros de R2 | TRIG 5 / ECHO 18 |
| Sensor de pH (módulo PH-4502C + sonda) en R2 · en Wokwi potenciómetro `ph` | pH del agua | GPIO 34 |
| Relé + bomba dosificadora peristáltica (o electroválvula de goteo) · en Wokwi LED blanco | Gotas de cloro | GPIO 13 |
| LED verde / amarillo / rojo | Semáforo | 26 / 27 / 14 |
| Buzzer | Alarma en rojo | GPIO 25 |
| LCD 16x2 I2C | 5 pantallas: R1, R2+pH, bombas, cloro, red | SDA 21 / SCL 22 |
| Pulsador | Silencia 60 s y reinicia fallas | GPIO 4 |

## Cómo funciona cada etapa

### 1. Sedimentos y purga en R1

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

### 2. Transferencia (R1 → filtro → R2)
- Rellena R2 cuando baja del **50 %** y para al **90 %**.
- Solo funciona si R1 **no tiene sedimentos**, tiene agua y R2 no se está
  clorando (R2 tiene que estar quieto para medir el pH).
- Si con la bomba encendida R2 no sube, da la falla **"Filtro tapado?"**: hay
  que limpiar el filtro o revisar la bomba o la manguera.
- Corta siempre al 95 % (desborde), también en modo manual.

### 3. pH y gotas de cloro en R2

Cuando termina de entrar agua:

1. **Mezcla:** espera 15 s (en real, 5 min) para que el pH se estabilice.
2. **Mide el pH** y elige la dosis:

   | pH | Gotas por litro | Por qué |
   |---|---|---|
   | menos de 6,5 | **no clora**, avisa "pH bajo: corregir" | agua ácida, corrosiva |
   | 6,5 – 7,5 | **2** (normal) | el cloro funciona mejor |
   | 7,5 – 8,0 | **2,5** (aumentada) | el cloro pierde algo de efecto |
   | más de 8,0 | **no clora**, avisa "pH alto: corregir" | el cloro casi no desinfecta; más cloro no lo arregla |

3. **Gotas = litros que entraron × gotas por litro.** Por ejemplo, 300 L con pH
   7,2 → 600 gotas ≈ 30 ml.
4. **Dosifica:** abre la bomba dosificadora el tiempo justo para esas gotas,
   según el caudal `GOTEO_GOTAS_MIN`.
5. **Contacto:** "Clorando: espere" durante 60 s (en real, 30 min) y luego
   **"Agua lista"**.

- Si el pH está fuera de rango, el sistema **espera**. Cuando se corrige el pH,
  clora solo.
- Lleva la cuenta del cloro que queda en la botella ("Recargar cloro" bajo
  20 %, "Sin cloro" en rojo). Después de rellenar, pulsar *Recargué el cloro*.
- El botón **Clorar todo R2** sirve, por ejemplo, después de un corte de luz:
  calcula la dosis para todos los litros de R2.
- **La tabla de dosis está en `include/config.h` y hay que validarla** con la
  guía del Ministerio de Salud y el porcentaje que indica la etiqueta del cloro
  (hipoclorito 5–6 %, sin aroma).

## Semáforo

| Color | Cuándo |
|---|---|
| 🟢 Verde | "Agua lista": R2 clorada y con el tiempo de contacto cumplido |
| 🟡 Amarillo | pH bajo/alto: corregir, recargar cloro, purgando lodo, R1 sin agua, R1 con lodo, midiendo pH, aplicando cloro, clorando: espere, R2 nivel bajo |
| 🔴 Rojo + buzzer | Falla del sensor de R2, "Filtro tapado?", "Purga sin efecto", R2 ≥ 95 %, R2 ≤ 10 %, sin cloro |

## Página web

Al arrancar intenta conectarse al WiFi de `include/secrets.h`. Si no puede en
10 s, crea la red **AquaAlerta** (contraseña en `secrets.h`) y la página queda en
<http://192.168.4.1>. La IP se ve en el LCD. No usa librerías de internet.

- R1 con la capa de lodo según los sedimentos, R2 con su nivel, y los íconos
  purga → bomba → filtro, que se encienden cuando trabajan.
- Barra de pH (0–14) con la zona verde, la tabla de dosis, las gotas del agua
  por clorar, la última dosis (pH, gotas/L, gotas, ml) y el cloro restante.
- Gráfico de los últimos 30 min: nivel de R2, sedimentos de R1, pH, y franjas
  de purga, transferencia y goteo.
- Control con PIN: purga y transferencia (automático / encender / apagar),
  reiniciar fallas, *Clorar todo R2* y *Recargué el cloro*.
- API JSON: `GET /api/estado`, `GET /api/historial`, `POST /api/control`
  (`accion=p_auto|p_on|p_off|t_auto|t_on|t_off|reset|clorar|recarga`, `pin=…`).

## Cómo se mide (evaluación)

| Métrica | Cómo obtenerla |
|---|---|
| **Eficacia de la purga** | La página muestra la turbidez del fondo antes y después de cada purga (ej. 180 → 15 NTU), y los litros usados. Probar con agua con 1, 2 y 5 cucharadas de tierra por litro. |
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
4. Recorrido sugerido. Al inicio R1 está sin sedimentos, R2 al ~27 % y el pH ≈ 7,1:
   - Arranca la **transferencia (LED morado)**. Como en la simulación el agua
     no sube sola, baja poco a poco la distancia de `ultra` (R2 se llena). Con
     ≤ 32 cm (90 %) se apaga.
   - R2: **"Midiendo pH"** 15 s → **"Aplicando cloro" (LED blanco)** con las
     gotas calculadas → **"Clorando: espere"** 60 s → **"Agua lista"** (verde).
   - Potenciómetro `ph` casi al máximo → pH bajo; casi al mínimo → pH alto: no
     clora y pide corregir. Llévalo al centro y clora solo.
   - Potenciómetro `turb` hacia abajo → sedimentos → se detiene la
     transferencia y arranca la **purga (LED azul)** por pulsos. Súbelo durante
     una pausa (agua limpia) y la purga termina. Si no lo subes, tras 6 pulsos
     aparece "Purga sin efecto".
   - Interruptor deslizante → "R1 sin agua": ninguna bomba arranca.
   - PIN por defecto: `1234`.

## Armado físico

- **Sensor de sedimentos (turbidez)** en R1, a ~5 cm del fondo y al lado de la
  succión de la purga. La parte superior del sensor no es sumergible: montarlo
  en un tubo sellado con la punta hacia abajo. Salida de hasta 4,5 V → divisor
  10 kΩ / 20 kΩ.
- **Bomba de purga** con la succión en el punto más bajo del fondo (ideal: fondo
  en pendiente o cónico). La salida va a desagüe o a riego.
- **Bomba de transferencia** con la succión a 10–15 cm **sobre el fondo**, para
  no tomar lodo. Instalar el **filtro** en la manguera hacia R2 (cartucho de
  sedimentos 50 → 5 micras, o filtro casero de grava, arena y algodón).
- **Flotador** en R1 a la altura de la succión de transferencia: si el agua baja
  de ahí, ninguna bomba funciona.
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
include/config.h    pines, umbrales de sedimentos, tabla pH → gotas, tiempos
include/control.h   lógica pura: purga, transferencia, cloración por pH y semáforo
include/pagina.h    página web (HTML/CSS/JS embebido)
src/main.cpp        sensores, relés, LCD, WiFi/AP y servidor web
diagram.json        circuito de Wokwi
```
