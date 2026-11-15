# 07 — ARIA: análisis determinista y asistencia

## 1. Alcance

ARIA es un asistente de análisis **local, determinista y explicable**. No es un modelo de machine learning, no aprende del usuario, no envía audio a ninguna red y no escribe parámetros desde el hilo de audio.

ARIA responde a una sola pregunta: *dado un fragmento de audio y un perfil de destino, ¿qué desviaciones medibles existen y qué cambios mínimos de parámetros las corrigen?*

No sustituye al oído ni al limitador. No masteriza automáticamente. No aplica cambios sin una acción explícita del usuario.

Esta especificación es normativa para la Fase 6. Cualquier implementación anterior en `Source/AI/` es un prototipo y debe converger a este contrato antes de declarar la fase cerrada.

## 2. Arquitectura y hilos

```text
Audio thread                            Worker ARIA                 Message thread / UI
────────────                            ───────────                 ──────────────────
pushBlock()                             espera snapshot             Analyze / Cancel
  escribe ring activo                     │                           Apply / Undo
  observa freezeRequested                 │                           carga de referencia
  publica snapshot (atomics) ───────────▶ analyze()                   lee report POD
                                          │  features + reglas         │
                                          └─ publica report ─────────▶ pinta informe
```

| Componente | Hilo | Responsabilidad |
|---|---|---|
| `ARIAAnalyzer` | audio (push), worker (análisis) | Captura los últimos 8 s, congela una instantánea y calcula features. |
| `ARIAEngine` | worker | Funciones puras: features → diagnóstico → sugerencias. Sin estado mutable. |
| `ARIAReference` | message/worker | Decodifica un archivo de referencia y calcula sus features. Nunca en audio. |
| `ARIAController` | message | Orquesta Analyze/Cancel, publica el informe y aplica sugerencias vía APVTS. |
| `ARIAPanelView` | message/UI | Presenta el informe; no calcula nada. |

Reglas de hilo:

- El audio solo llama a `pushBlock`. No lee el informe, no espera al worker y no reserva memoria.
- El worker nunca toca el buffer del host ni el APVTS. Solo lee la instantánea congelada y escribe un `ARIAReport` POD.
- La UI nunca lee la instantánea de audio; lee el último informe publicado.
- `applySuggestions` y `undoLastApply` corren exclusivamente en message thread.

## 3. Captura

### 3.1 Formato

| Campo | Valor |
|---|---|
| Canales | 2 (si el host entrega mono, R = L) |
| Tipo | `float` de 32 bits |
| Duración | 8,000 s exactos por buffer |
| Capacidad | `capacityFrames = round(sampleRate × 8.0)` |
| Buffers | 2 (doble buffer), asignados en `prepareToPlay` |
| Memoria | `2 × capacityFrames × 2 canales × 4 B` |
| Escritura | Circular, secuencial, sin allocaciones |
| Tap | Salida del plugin tras safety clamp, antes de la FIFO de medición |

A 192 kHz: `2 × 1.536.000 × 2 × 4 = 24,576 MB`. A 48 kHz: `6,144 MB`.

### 3.2 Armado

La captura está activa desde `prepareToPlay`. No hay botón de armado en v1: mantener 8 s en un ring de dos canales es O(n) y evita que el usuario pulse **Analyze** antes de que exista audio.

Si el host entrega menos de 2 canales, el canal que falta se duplica. Si entrega más, se usan los dos primeros y se descarta el resto.

### 3.3 Congelación

`Analyze` no copia 8 s en el message thread. Publica una petición de congelación con una generación monótona:

```text
message: freezeRequested = true ; requestedGeneration = g
audio:   al inicio del bloque, si freezeRequested.exchange(false):
             frozenIndex  = activeIndex
             frozenLength = min(writePosition, capacityFrames)
             frozenGen    = g
             activeIndex ^= 1
             writePosition = 0
             snapshotReady.store(true, release)
```

- Si el ring no ha dado una vuelta completa, `frozenLength = writePosition` y el informe usa solo ese audio.
- Si ya dio la vuelta, `frozenLength = capacityFrames` y el worker linealiza la instantánea en su propio scratch (el worker sí puede asignar).
- El worker lee el buffer congelado; el audio escribe en el otro. El buffer congelado no se reutiliza hasta que el worker termina y se solicita un nuevo análisis.
- La UI deshabilita **Analyze** mientras `isRunning()`. No hay dos congelaciones simultáneas.

### 3.4 Mínimos y casos límite

| Caso | Respuesta |
|---|---|
| `frozenLength < 3 s` | Informe inválido: «Insuficiente audio; reproduce al menos 3 s». No se aplican reglas. |
| Silencio digital (< −120 dBFS) | Informe válido con features en el suelo; diagnóstico «sin señal». Sin sugerencias. |
| Un solo canal | Se duplica; `correlation = 1`, `sideToMid = −120 dB`. |
| Muestras no finitas | El tap ya está saneado; aun así el worker descarta la instantánea si encuentra un no finito y publica error. |
| Sample rate | 44,1 / 48 / 88,2 / 96 / 176,4 / 192 kHz. Las ventanas son temporales, no de número fijo de muestras. |
| Cancelar | `request_stop` lógico por generación; el worker comprueba cada frame de FFT y cada 4096 muestras. |

## 4. Pipeline de análisis

Todo el cálculo usa `double` en acumuladores y `float` en el almacenamiento. El orden de iteración es fijo: canales 0→1, muestras 0→N−1, bandas 0→30. No hay paralelismo dentro de un informe, para que el resultado sea reproducible.

1. **Linealización**: si el ring dio la vuelta, reconstruir el orden cronológico en un `std::vector<float>` scratch del worker.
2. **Downmix**: `mono[n] = 0.5·(L[n] + R[n])`.
3. **K-weighting** por canal (BS.1770-4): shelf 1681,974 Hz + high-pass 38,135 Hz, en `double`.
4. **Bloques de 100 ms**: potencia media por bloque, sin solape. `p_k`.
5. **Loudness**: momentary (4 bloques), short-term (30 bloques), integrado con gating, LRA.
6. **True peak**: sobremuestreo 4× FIR por canal, pico absoluto en la señal sobremuestreada.
7. **RMS**: sin ponderar, sobre la captura completa y por canal.
8. **FFT**: 4096 puntos, ventana Hann, hop 1024 (75 % de solape), promedio de potencia.
9. **Bandas**: 31 bandas de tercio de octava, centros `20·2^(k/3)` Hz, `k = 0…30`.
10. **Features temporales**: correlación, side/mid, densidad de transitorios.
11. **Reglas**: evaluación en orden fijo, sin aleatoriedad.

La FFT, el sobremuestreo y los scratch se asignan en el worker por análisis. No se reutilizan buffers del audio.

## 5. Features

| Feature | Unidad | Definición |
|---|---|---|
| `integratedLufs` | LUFS | BS.1770-4: bloques de 400 ms, puerta absoluta −70 LUFS, puerta relativa −10 LU; media de potencias. |
| `shortTermLufs` | LUFS | Últimos 3 s (30 bloques de 100 ms). |
| `lra` | LU | EBU Tech 3342: percentiles 10 % y 95 % de los short-term con puerta absoluta −70 LUFS y relativa −20 LU. |
| `truePeakDb` | dBTP | Máximo absoluto tras sobremuestreo 4×; suelo −120 dB. |
| `samplePeakDb` | dBFS | Máximo absoluto de las muestras originales. |
| `rmsDb` | dBFS | RMS sin ponderar de la captura completa. |
| `crestDb` | dB | `samplePeakDb − rmsDb`, acotado a 0…60. |
| `spectralCentroidHz` | Hz | `Σ f·P(f) / Σ P(f)` sobre bins de 20 Hz…20 kHz. |
| `spectralTiltDbPerOct` | dB/oct | Regresión lineal de `bandDb` sobre `log2(f)` para bandas de 100 Hz…10 kHz con `bandDb > −90`. |
| `flatness` | 0…1 | Media geométrica / media aritmética de la potencia espectral (bins 20 Hz…20 kHz). |
| `lowMidDb` | dB | Media de `bandDb` en 200…400 Hz. |
| `sibilanceDb` | dB | Media de `bandDb` en 5…9 kHz. |
| `sibilanceRatioDb` | dB | `sibilanceDb − midDb`, con `midDb` = media 1…4 kHz. |
| `correlation` | −1…1 | `Σ L·R / sqrt(Σ L² · Σ R²)` tras eliminar la componente DC de cada canal. |
| `sideToMidDb` | dB | `20·log10(rms(S) / rms(M))`, `M = 0,5(L+R)`, `S = 0,5(L−R)`; suelo −120. |
| `transientDensityPerSec` | 1/s | Onsets por segundo en la envolvente RMS de 10 ms. |
| `bandsDb[31]` | dB | Energía integrada por banda de tercio de octava. |
| `broadbandDb` | dB | Media de `bandsDb` en 100 Hz…10 kHz con `> −90 dB`; base de comparación de las reglas. |

### 5.1 Detección de transitorios

1. Envolvente RMS en ventanas de 10 ms sin solape.
2. Un onset ocurre si `env[k] > env[k−1] + 6 dB` y `env[k]` supera el mínimo de los 30 ms anteriores en 3 dB.
3. Refractario: 50 ms desde el onset anterior.
4. `transientDensityPerSec = onsets / (N / fs)`.

Es una medida de densidad, no un detector musical. Se usa solo como diagnóstico; ninguna regla lo usa para ecualizar.

## 6. Perfiles

Un perfil fija objetivos, no mediciones. El motor es el mismo para todos.

| `id` | Nombre | `targetIntegratedLufs` | `maxTruePeakDb` | `maxLra` |
|---|---|---:|---:|---:|
| `streaming` | Streaming | −14,0 | −1,0 | 12,0 |
| `podcast` | Podcast / voz | −16,0 | −1,0 | 10,0 |
| `club` | Club / bass music | −8,0 | −0,3 | 8,0 |
| `mastering` | Mastering neutro | −9,0 | −1,0 | 8,0 |
| `custom` | Personalizado | usuario | usuario | usuario |

- El perfil por defecto es `streaming`.
- `custom` parte de `streaming`; los campos no definidos conservan el valor del perfil base.
- El perfil se guarda como propiedad `ariaProfileId` del estado, no como parámetro del host.
- Los perfiles de fábrica se embeberán en `assets/profiles/` en Fase 6. Mientras no exista el asset, se usa la tabla compilada.
- Un `id` desconocido cae a `streaming` con aviso en el informe; nunca invalida el preset.

Formato previsto de cada perfil:

```json
{
  "id": "streaming",
  "name": "Streaming",
  "targetIntegratedLufs": -14.0,
  "maxTruePeakDb": -1.0,
  "maxLra": 12.0,
  "version": 1
}
```

## 7. Reglas deterministas

### 7.1 Contrato

- Las reglas se evalúan **siempre** en el orden R01…R10. No hay aleatoriedad, reloj, estado del usuario ni dependencia del orden de mensajes.
- Cada regla produce cero o más `ARIASuggestion` con: `parameterId`, `targetValue`, `baselineValue`, `ruleId`, `reason`, `severity`.
- `targetValue` es un valor absoluto en unidades del catálogo, no un delta aplicado al valor actual.
- `baselineValue` es el valor del parámetro en el momento del análisis. Se guarda para que Apply sea idempotente y Undo exacto.
- Un parámetro solo puede aparecer una vez en el informe. Si dos reglas lo tocan, gana la regla de menor número, salvo que la tabla indique lo contrario.
- Máximo 32 cambios por informe. Si se excede, se descartan las reglas de mayor número y el informe indica `truncated = true`.
- Si no se dispara ninguna regla, el informe es válido y `suggestions` está vacío.

### 7.2 Catálogo

`broadbandDb` es la referencia tonal. `x` es la desviación medida. Todos los `clamp` usan los límites reales de `ModuleCatalog`.

| ID | Condición | Cambios (valor objetivo) | Motivo |
|---|---|---|---|
| R01 | `lowMidDb − broadbandDb > +2,0 dB` | `eqA.active=1`, `eqA.b3.on=1`, `eqA.b3.type=Bell(4)`, `eqA.b3.freq=300`, `eqA.b3.q=1,0`, `eqA.b3.gain = −clamp(0,5, 4,0, 0,5·(lowMidDb − broadbandDb))` | Exceso de 200–400 Hz. |
| R02 | `tilt < −3,5 dB/oct` | `eqA.active=1`, `eqA.b8.on=1`, `eqA.b8.type=High Shelf(6)`, `eqA.b8.freq=8000`, `eqA.b8.gain = clamp(0,5, 4,0, −0,5·tilt)` | Balance oscuro. |
| R03 | `tilt > +3,5 dB/oct` | `eqA.active=1`, `eqA.b8.on=1`, `eqA.b8.type=High Shelf(6)`, `eqA.b8.freq=8000`, `eqA.b8.gain = −clamp(0,5, 3,0, 0,5·tilt)` | Balance brillante. |
| R04 | `crestDb > 16,0` | `compA.active=1`, `compA.threshold = clamp(−30, −10, −18 − 0,5·(crestDb − 16))`, `compA.ratio=2,5`, `compA.attack=20`, `compA.release=150`, `compA.knee=10`, `compA.makeup=0`, `compA.autoMakeup=0`, `compA.mix=100` | Crest alto: dinámica sin controlar. |
| R05 | `sibilanceRatioDb > +3,0 dB` | `dsA.active=1`, `dsA.freq=6500`, `dsA.threshold = clamp(−40, −16, shortTermLufs − 6)`, `dsA.ratio=4`, `dsA.attack=1`, `dsA.release=50`, `dsA.range = clamp(2, 12, 2·(sibilanceRatioDb − 3))`, `dsA.mix=100` | Sibilancia elevada respecto a 1–4 kHz. |
| R06 | `correlation < 0,15` | `imgA.active=1`, `imgA.width = clamp(0, 100, 80 + 100·correlation)`, `imgA.bassMonoFreq=120`, `imgA.bassMonoAmount=100` | Imagen estéreo inestable o fuera de fase. |
| R07 | `truePeakDb > maxTruePeakDb` | `masterA.active=1`, `masterA.limiterOn=1`, `masterA.ceiling = maxTruePeakDb` | Techo excedido. |
| R08 | `integratedLufs < target − 1,0` | `masterA.active=1`, `masterA.clipperOn=1`, `masterA.drive = clamp(0, 6, (target − integratedLufs) − 1,0)`, `masterA.ceiling = maxTruePeakDb` | Loudness por debajo del objetivo. |
| R09 | `integratedLufs > target + 1,0` | `global.outputTrim = clamp(−12, 0, target − integratedLufs)` | Loudness por encima del objetivo. |
| R10 | `lra > maxLra` y R04 no disparada | `compA.active=1`, `compA.threshold = clamp(−30, −10, shortTermLufs − 4)`, `compA.ratio=2,0`, `compA.attack=30`, `compA.release=300`, `compA.knee=10`, `compA.autoMakeup=1`, `compA.mix=100` | Rango dinámico excesivo para el perfil. |

Precisiones:

- R08 no se aplica si `truePeakDb > maxTruePeakDb` y R07 ya está activa; en ese caso el limitador debe resolver el techo antes de añadir drive. El informe muestra ambas como diagnóstico, pero solo R07 propone cambios.
- R09 nunca propone subir gain: si la señal está por encima del objetivo, solo atenúa.
- R02 y R03 son mutuamente excluyentes.
- R04 y R10 son mutuamente excluyentes; R04 tiene prioridad.
- R01 y R02/R03 pueden coexistir: bandas distintas del mismo EQ.
- Si `eqA` ya tiene una banda 3 o 8 activa con un valor manual distinto, R01/R02/R03 siguen proponiendo el valor calculado; el usuario ve el `baselineValue` y decide. No se respeta el ajuste manual como “bloqueo” en v1.

### 7.3 Explicabilidad

Cada sugerencia incluye texto con la medición y el umbral:

```text
R01: 200–400 Hz está 3,2 dB sobre la media (umbral +2,0 dB).
     EQ A · banda 3 · Bell 300 Hz · −1,6 dB.
```

El informe incluye además los features medidos, el perfil, la versión del conjunto de reglas y la duración analizada. La UI nunca muestra una sugerencia sin su `ruleId` y su razón.

## 8. Strength y aplicación

`aria.strength` (0…100 %, default 100) escala la aplicación, no el análisis. El informe siempre describe el objetivo al 100 %.

| Strength | Comportamiento |
|---:|---|
| 0 | No escribe nada. Solo informe. |
| 1…100 | Continuos: `v = baseline + (strength/100)·(target − baseline)`. |
| 1…100 | Booleanos, choices y activaciones: se aplica el valor objetivo completo. |

- `baseline` es el valor capturado al analizar, no el valor actual al aplicar. Por eso aplicar dos veces el mismo informe es idempotente.
- Un cambio continuo se limita al rango del catálogo y se redondea al `step` del parámetro.
- Los cambios se aplican en una sola transacción del message thread: se copia el `ValueTree` completo a un snapshot de undo, se escriben todos los parámetros y se publica una notificación.
- Si el usuario cancela a mitad, no se escribe nada; la transacción es atómica desde el punto de vista de la UI.
- Undo restaura el snapshot completo, no solo los parámetros de ARIA.
- No hay auto-apply, ni apply al cerrar, ni apply desde el worker.

## 9. Referencia

`ARIAReference` permite comparar la mezcla con un archivo externo.

- Formatos: los que ofrezca `juce::AudioFormatManager` en la build (WAV, AIFF, FLAC; MP3/AAC solo si se habilitan).
- Decodificación en message thread o worker de referencia; nunca en audio.
- Máximo 10 minutos de archivo; se analizan los primeros 8 s con el mismo pipeline. Si dura menos de 3 s, se rechaza.
- El archivo se remuestrea offline al sample rate del proyecto con un resampler de calidad alta; el resultado debe ser determinista para el mismo archivo y rate.
- La comparación muestra deltas de features (`mix − reference`) y un diagnóstico textual.
- En v1 la referencia **no** modifica las reglas ni genera sugerencias automáticas. Es un panel de comparación. Cualquier modo “match reference” se especificará en una versión posterior del conjunto de reglas.
- El estado guarda `ariaReferencePath` y `ariaReferenceHash` (SHA-256) como propiedades; nunca guarda muestras de audio.
- Si el archivo no existe al restaurar, el panel muestra «referencia no disponible» y el audio no se ve afectado.

## 10. Privacidad y límites

- Todo el análisis es local. No hay telemetría, red, cuentas ni subida de audio.
- El informe no contiene rutas absolutas ni nombres de archivo; la referencia se muestra por nombre.
- Los logs de diagnóstico no incluyen muestras de audio ni features completos.
- ARIA no lee el estado del host ni otros plugins.
- ARIA no modifica el bypass del rack ni el orden; solo propone parámetros del catálogo.
- Un informe es una fotografía de 8 s. No es un análisis de canción completa. La UI lo indica explícitamente.
- ARIA no promete cumplimiento normativo; las métricas se calculan según BS.1770-4/EBU R128, pero la certificación depende de la cadena y del medidor externo.

## 11. Criterios de aceptación (Fase 6)

1. **Determinismo**: dos análisis de la misma instantánea, perfil y `ruleSetVersion` producen el mismo informe byte a byte en el mismo binario.
2. **Independencia del hilo**: el audio no bloquea con el worker ocupado, cancelado o atascado; el test de worker bloqueado no altera la latencia ni el audio.
3. **Congelación**: con 8 s exactos, el worker analiza exactamente `capacityFrames`; con 3 s, analiza 3 s; con 2,9 s, informe inválido.
4. **Loudness**: las features de loudness coinciden con el medidor `LoudnessMeter` del plugin dentro de ±0,1 LU para integrado, ±0,1 LU para short-term y ±1 LU para LRA.
5. **True peak**: ±0,2 dB respecto a una referencia de sobremuestreo 4× validada.
6. **Bandas**: 31 bandas, suma de potencias monótona; una senoide de 1 kHz cae en la banda de 1 kHz ±1 banda.
7. **Reglas**: cada regla tiene un test de frontera (justo por debajo, justo por encima) y un golden de parámetros esperados.
8. **Idempotencia**: aplicar el mismo informe dos veces con Strength 50 deja los mismos valores que aplicarlo una vez.
9. **Undo**: undo restaura exactamente el `ValueTree` previo, incluidos parámetros ajenos a ARIA.
10. **Privacidad**: el informe serializado no contiene rutas absolutas ni audio.
11. **Referencia**: cargar un WAV de 10 s produce features estables y no altera el audio; un archivo corrupto produce error de UI, no un crash.
12. **Memoria**: la captura doble no supera 25 MB a 192 kHz; el análisis no reserva en audio.
