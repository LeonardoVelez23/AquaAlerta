# Recomendaciones recibidas el 19 de noviembre de 2026

> **Importante:** estas son **recomendaciones que nos dieron hoy, 19-nov-2026**, a una semana de la jornada
> presencial del TerriTech Hackathon CITIC 2026 (26-nov-2026). **Todavía no están implementadas.**
> Debajo de cada una está nuestro análisis, si conviene aplicarla y con qué prioridad.

## Lista tal como nos la dieron

1. Control PID para la concentración del químico vertido en R1 (PAC).
2. Control PID para la velocidad y tiempo de agitación; revisar qué método conviene más.
3. Sensores para interfaces, control de la sedimentación.
4. Gestión de la válvula de salida al recipiente de filtrado (por la sedimentación restante).
5. Filtrado por nanopartículas de plátano.
6. Revisar otras opciones, pros y contras de la desinfección por cloro.

## Resumen

| # | Recomendación | ¿Conviene? | Prioridad |
|---|---|---|---|
| 1 | PID para la dosis de PAC | **No como PID**; sí un ajuste entre lotes | Media |
| 2 | PID para la velocidad y el tiempo de agitación | Solo un PI de velocidad, y solo con sensor de RPM | Baja |
| 3 | Sensores de interfaz y control de la sedimentación | **Sí**: barato y mejora mucho el proceso | **Alta** |
| 4 | Gestión de la válvula de salida hacia el filtro | **Sí**: ya está a medias | **Alta** |
| 5 | Filtrado con nanopartículas de plátano | No para esta versión | — |
| 6 | Alternativas al cloro | Sí, como análisis para el pitch | Alta (solo documentación) |

## 1. PID para la dosis de PAC → no conviene como PID

Un PID corrige de forma continua un error que se mide al instante. La coagulación no funciona así:

- **Retardo enorme:** el efecto del PAC se ve después de la agitación y la sedimentación (≈ 45 min en modo real).
- **No se puede retroceder:** si se pasa de PAC, no se puede quitar. La sobredosis vuelve a enturbiar el agua y deja más aluminio.
- **Es un proceso por lotes:** se dosifica una vez por lote, no en flujo continuo.

Hoy el firmware calcula la dosis antes de dosificar: tabla de turbidez a ppm (`PAC_TABLA_NTU` / `PAC_TABLA_PPM`
en `include/config.h`), multiplicada por los litros. Eso es **control anticipativo (feedforward)**, que es lo correcto aquí.

**Propuesta:** agregar un **ajuste entre lotes (run-to-run)**. Si después de sedimentar R1 sigue turbio, el
siguiente lote usa unos ppm más; si queda claro con margen, prueba con menos. El equipo aprende la dosis de cada
fuente de agua. Ante el jurado, es más defendible explicar por qué no usamos PID.

## 2. PID para la agitación → solo un PI de velocidad, y opcional

- **Tiempo:** no se controla con PID. Es parte de la receta (coagulación 4 min, floculación 12 min:
  `COAG_RAPIDA_MS`, `COAG_LENTA_MS`) y se ajusta con la prueba de jarras.
- **Velocidad:** hoy el PWM va en lazo abierto, así que las RPM cambian con el voltaje y la carga. Un **PI** (sin la D,
  que amplifica el ruido) tiene sentido **solo si agregamos un sensor de RPM**: Hall con imán en el eje, u óptico de
  ranura, de 1 a 3 USD.
- **Más útil y gratis:** una **rampa suave** al pasar de la agitación rápida a la lenta, porque un cambio brusco rompe los
  flóculos recién formados. Además, calibrar una vez la tabla de PWM a RPM con una app de tacómetro en el celular.

## 3. Control de la sedimentación → la más valiosa

Hoy la sedimentación dura un **tiempo fijo** (`COAG_SEDIM_MS`, 30 min).

- **Terminar por turbidez:** que la sedimentación acabe cuando la turbidez de R1 baje del umbral **y se estabilice**, con
  un tiempo mínimo y uno máximo de seguridad. Si el sensor está a la altura de la salida, solo hay que cambiar el firmware.
- **Detectar la interfaz entre el lodo y el agua clara:** 2 o 3 pares de LED IR y fotodiodo a distintas alturas de R1
  (menos de 1 USD cada par). Así se sabe dónde está el lodo y cuánto purgar. El ultrasonido no sirve para eso.

Ahorra tiempo en lotes con agua fácil y da un dato medible para el póster.

## 4. Válvula de salida hacia el filtro → completar lo que hay

Hoy el paso al filtro **ya solo ocurre si R1 está por debajo del umbral de turbidez**, y hay una falla «agua turbia» que lo
detiene. Falta:

- **Nivel mínimo:** detener el vaciado antes de llegar a la capa de lodo, dejando un margen en el fondo. Se usa
  el sensor de nivel que ya existe.
- **Corte durante el vaciado:** si la turbidez sube mientras sale el agua, se está arrastrando lodo y hay que cerrar.
- **Salida elevada:** la toma debe estar unos centímetros sobre el fondo de R1.
- **Válvula en vez de bomba:** como la maqueta es en cascada, el agua cae por gravedad. Una válvula solenoide
  reemplaza a la bomba de transferencia con el mismo relé (`PIN_RELE_TRANSFER`), es más barata y no consume
  energía bombeando.

## 5. Filtrado con nanopartículas de plátano → no para esta versión

Se revisó como información:

- **Lo que sabemos:** la cáscara sirve sobre todo como **adsorbente de metales pesados**, y no tenemos datos de metales
  en el agua de Bolívar. Como coagulante, solo funcionó a pH 4 o con agua muy turbia. Hecha con plata o cobre,
  requiere laboratorio.
- **Problemas para el sistema:** cruda suelta materia orgánica, consume el cloro de R2 y sube el TDS.
- **Si se usara:** biocarbón de cáscara entre la arena y el carbón activado (ver `docs/cascara-platano-uso.png`).
- **Para el pitch:** como mucho, línea futura.

Fuentes: [Applied Water Science, 2024](https://link.springer.com/article/10.1007/s13201-024-02173-8);
[Ingeniería e Investigación, EPN, 2019](https://revistas.unal.edu.co/index.php/ingeinv/article/download/69703/70326);
[tesis ESPAM sobre cáscara de plátano](https://repositorio.espam.edu.ec/items/3a529f77-8a1c-4848-bf8d-3d0365ece59a/full).

## 6. Alternativas al cloro → mantener el cloro, llevar la comparación preparada

| Método | A favor | En contra |
|---|---|---|
| **Cloro (actual)** | Barato, avalado por el MSP. **Deja cloro residual que protege el agua guardada en el aljibe** | Sabor; forma subproductos con materia orgánica; necesita pH < 8 y agua clara; poco eficaz contra *Cryptosporidium* |
| **UV-C** | Sin químicos ni sabor; rápido; elimina *Cryptosporidium* | **Sin residual** (el agua se puede recontaminar en el aljibe); necesita agua muy clara y electricidad; la lámpara se cambia |
| **SODIS** (botellas PET al sol, 6 h) | Gratis y validado por la OMS | Poco volumen, lento, depende del clima |
| **Hervir** | Muy eficaz | Gas o leña en cada uso; no deja residual |
| **Ozono** | Muy potente | Caro, sin residual, genera un gas peligroso |
| **Iones de plata o cobre** | Deja residual | Lento; hay que vigilar los límites de metales |
| **Electrocloración** (cloro hecho en sitio con sal y electricidad) | No hay que comprar cloro | Electrodos y control más complejos |

**Argumento para el jurado:** el agua se guarda en aljibes y solo el cloro protege el agua *después* del tratamiento.
Línea futura: UV más una dosis baja de cloro (dos barreras).

## Plan propuesto para esta semana

1. **#3 y #4 en el firmware:** sedimentación por turbidez, nivel mínimo y corte durante el vaciado. Usan sensores que ya
   tenemos.
2. **#1 como ajuste entre lotes**, con la justificación de por qué no es PID.
3. **#2 solo la rampa suave** (el PI de velocidad, si compramos el sensor Hall).
4. **#6 como diapositiva de respaldo** o tabla en la ficha técnica.
5. **#5** solo se menciona como línea futura.

| Recomendación | Estado |
|---|---|
| 1 · Ajuste de PAC entre lotes | Pendiente |
| 2 · Rampa de agitación / PI de velocidad | Pendiente |
| 3 · Sedimentación por turbidez / sensores de interfaz | Pendiente |
| 4 · Nivel mínimo y corte en la salida hacia el filtro | Pendiente |
| 5 · Plátano | Descartado para esta versión |
| 6 · Comparación de desinfección | Pendiente (documentación) |
