# 03 — Especificación DSP

## 0. Convenciones

- `fs`: frecuencia de muestreo. `x[n]`: entrada. `y[n]`: salida. `N`: número de muestras del bloque.
- Todos los estados internos de filtros y envolventes son `double`; los buffers de audio son `float`.
- `dbToGain(d) = 10^(d/20)`. `gainToDb(g) = 20·log10(max(g, 1e-12))`, con suelo práctico −240 dB.
- Coeficientes biquad normalizados por `a0`; forma directa II transmuesta (TDF2).
- Todo `process` es `noexcept`, sin allocaciones, con `juce::ScopedNoDenormals`.
- `sanitize(x)`: `isfinite(x) ? clamp(x, −16, 16) : 0`.
- Los parámetros se leen por bloque y se suavizan según `docs/02_RT_SAFETY.md`. Los valores por defecto coinciden exactamente con `Source/Core/ModuleCatalog.cpp`.
- `mix` es lineal en dinámica/saturación: `y = (1−m)·dry + m·wet`, `m = mix/100`. En reverb y delay es equal-power: `dry = cos(πm/2)`, `wet = sin(πm/2)`.
- Un módulo inactivo no procesa: el rack lo sustituye por su línea de retardo seca (ver `04_CLASS_MAP.md`).

### 0.1 Tabla de latencias

| Módulo | Latencia declarada (muestras base) | Origen |
|---|---:|---|
| Utility, EQ, Compressor, Multiband, De-Esser, Gate, Transient, Imager, Reverb, Delay | 0 | Procesamiento muestra a muestra sin lookahead |
| Saturator (2 instancias) | 32 cada una | FIR polifásico 8× de 512 taps |
| Mastering | 128 | Oversampling 4× (64) + lookahead (64) |
| **Total del plugin** | **192** | Suma fija, independiente del rack activo |

A 44,1 kHz: `192 / 44100 = 4,354 ms`. La cifra se valida en Fase 5/6 y se congela en ADR-0002.

---

## 1. Utility (`utilA`, `utilB`)

### 1.1 Parámetros

| ID relativo | Rango | Default | Unidad | Smooth |
|---|---|---|---:|---:|
| `active` | bool | `utilA=true`, `utilB=false` | — | — |
| `gain` | −24…+24 | 0 | dB | 20 ms |
| `pan` | −1…+1 | 0 | — | 20 ms |
| `polarityL` | bool | false | — | — |
| `polarityR` | bool | false | — | — |
| `mono` | bool | false | — | — |
| `swap` | bool | false | — | — |

### 1.2 Ley de ganancia y pan

```text
g  = 10^(gainDb/20)
p  = pan
gL = g · (p <= 0 ? 1 : 1 − p)
gR = g · (p >= 0 ? 1 : 1 + p)
```

Es una ley de *balance*: en el centro ambos canales quedan a 0 dB (no hay atenuación de centro). `pan = −1` deja `gR = 0`; `pan = +1` deja `gL = 0`.

Orden de operaciones por muestra:

```text
1. xL = L·gL ; xR = R·gR
2. si mono: m = 0.5·(xL + xR) ; xL = xR = m
3. si polarityL: xL = −xL ; si polarityR: xR = −xR
4. si swap: intercambiar xL/xR
5. L = sanitize(xL) ; R = sanitize(xR)
```

En un buffer mono (`numChannels == 1`), `pan` no altera la señal (gL se usa tal cual); `swap` y `polarityR` no tienen efecto.

### 1.3 Estado

Dos `SmoothedParam` (`gain`, `pan`) y cuatro `ParamRef` (`polarityL`, `polarityR`, `mono`, `swap`). Sin buffers.

### 1.4 Validación

- Identidad: defaults ⇒ salida igual a entrada (tolerancia 0 ulp dentro del rango ±16).
- `pan = −1` ⇒ RMS del canal R = −∞ dB; `pan = +1` ⇒ RMS L = −∞ dB.
- `mono` ⇒ L = R exactamente.
- `swap` + `polarityL/R` conmuta correctamente y es involutivo.
- Cambio de `gain` de −24 a +24 dB en un bloque: sin muestras fuera de `[−16, 16]` y sin discontinuidad > 0,5 dB entre muestras.
- El medidor `outputLevelDb` coincide con el pico real del bloque dentro de 0,1 dB.

---

## 2. Parametric EQ de 8 bandas (`eqA`, `eqB`)

### 2.1 Parámetros

Comunes:

| ID relativo | Rango | Default | Unidad | Smooth |
|---|---|---|---:|---:|
| `active` | bool | `eqA=true`, `eqB=false` | — | — |
| `output` | −12…+12 | 0 | dB | 20 ms |

Por banda `b ∈ {1…8}`:

| ID relativo | Rango | Default | Unidad | Smooth |
|---|---|---|---:|---:|
| `bN.on` | bool | true | — | — |
| `bN.type` | choice | Bell (índice 4) | — | — |
| `bN.freq` | 20…20000 | ver tabla | Hz | 30 ms |
| `bN.gain` | −18…+18 | 0 | dB | 20 ms |
| `bN.q` | 0,1…18 | 0,707 | — | 20 ms |

Tipos (orden congelado): `0 Low Pass`, `1 High Pass`, `2 Band Pass`, `3 Notch`, `4 Bell`, `5 Low Shelf`, `6 High Shelf`.

Frecuencias por defecto:

| Banda | `eqA` | `eqB` |
|---:|---:|---:|
| 1 | 80 Hz | 100 Hz |
| 2 | 160 Hz | 250 Hz |
| 3 | 320 Hz | 500 Hz |
| 4 | 640 Hz | 1 kHz |
| 5 | 1250 Hz | 2 kHz |
| 6 | 2500 Hz | 4 kHz |
| 7 | 5000 Hz | 8 kHz |
| 8 | 10000 Hz | 12 kHz |

### 2.2 Coeficientes RBJ

Con `w0 = 2π·clamp(f, 1, 0,499·fs)/fs`, `cw = cos w0`, `sw = sin w0`, `α = sw/(2Q)`, `A = 10^(G/40)`:

```text
Low Pass:    b0 = (1−cw)/2      b1 = 1−cw          b2 = (1−cw)/2
             a0 = 1+α           a1 = −2cw          a2 = 1−α
High Pass:   b0 = (1+cw)/2      b1 = −(1+cw)       b2 = (1+cw)/2
             a0 = 1+α           a1 = −2cw          a2 = 1−α
Band Pass:   b0 = α             b1 = 0             b2 = −α
             a0 = 1+α           a1 = −2cw          a2 = 1−α
Notch:       b0 = 1             b1 = −2cw          b2 = 1
             a0 = 1+α           a1 = −2cw          a2 = 1−α
Bell:        b0 = 1+αA          b1 = −2cw          b2 = 1−αA
             a0 = 1+α/A         a1 = −2cw          a2 = 1−α/A
```

Shelves (variante `β = √A/Q`):

```text
w0 = 2πf/fs ; cw = cos w0 ; sw = sin w0 ; A = 10^(G/40) ; β = √A/Q

Low Shelf:
a0 = (A+1) + (A−1)·cw + β·sw
b0 = A·((A+1) − (A−1)·cw + β·sw) / a0
b1 = 2A·((A−1) − (A+1)·cw) / a0
b2 = A·((A+1) − (A−1)·cw − β·sw) / a0
a1 = −2·((A−1) + (A+1)·cw) / a0
a2 = ((A+1) + (A−1)·cw − β·sw) / a0

High Shelf:
a0 = (A+1) − (A−1)·cw + β·sw
b0 = A·((A+1) + (A−1)·cw + β·sw) / a0
b1 = −2A·((A−1) + (A+1)·cw) / a0
b2 = A·((A+1) + (A−1)·cw − β·sw) / a0
a1 = 2·((A−1) − (A+1)·cw) / a0
a2 = ((A+1) − (A−1)·cw − β·sw) / a0
```

Si `G = 0`, un shelf produce `b = a` y es transparente. `Q` se limita a `[0,05, 30]` y `f` a `[20, 0,49·fs]` en runtime.

### 2.3 Estructura

Forma directa II transmuesta, doble precisión:

```text
y  = b0·x + z1
z1 = b1·x − a1·y + z2
z2 = b2·x − a2·y
```

Cada banda es un biquad por canal; las 8 bandas se aplican en serie. Los coeficientes se recalculan cada 32 muestras a partir de `current()` de los smoothers; la frecuencia y la Q no se interpolan por muestra. Al cambiar `type` u `on` se resetea el estado de esa banda para evitar transitorios de coeficientes.

Al final: `y = sanitize(y · dbToGain(output))`.

### 2.4 Validación

- Magnitud medida (barrido logarítmico) vs. `|H(e^{jw})|` analítica: error ≤ 0,05 dB en 20 Hz…20 kHz.
- Bell: ganancia en `f0` = `gain` ± 0,05 dB. Shelf: meseta = `gain` ± 0,1 dB.
- Notch: atenuación en `f0` ≤ −60 dB con Q ≥ 1.
- Estabilidad: barrido de 20 Hz a 0,49·fs en 44,1/48/96/192 kHz con todas las Q; ningún polo con `|z| ≥ 1`, ninguna salida NaN/Inf.
- Identidad con todas las ganancias a 0 y `type=Bell`: salida = entrada ± 1e-6.
- Click test: cambio de `freq` de 100 Hz a 10 kHz en un bloque; ninguna derivada mayor que 4× la de la señal.
- Latencia: 0.

---

## 3. Compressor (`compA`, `compB`)

### 3.1 Parámetros

| ID relativo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|
| `active` | bool | false | — | — | — |
| `threshold` | −60…0 | −18 | dB | — | 20 ms |
| `ratio` | 1…20 | 4 | :1 | 4 | 20 ms |
| `attack` | 0,1…200 | 10 | ms | 10 | 20 ms |
| `release` | 10…2000 | 100 | ms | 150 | 20 ms |
| `knee` | 0…40 | 10 | dB | — | 20 ms |
| `detector` | choice | Peak | — | — | — |
| `rmsWindow` | 1…50 | 10 | ms | 10 | 20 ms |
| `makeup` | 0…24 | 0 | dB | — | 20 ms |
| `autoMakeup` | bool | false | — | — | — |
| `mix` | 0…100 | 100 | % | — | 20 ms |
| `scHPF` | 20…500 | 20 | Hz | 100 | 20 ms |
| `link` | 0…100 | 100 | % | — | 20 ms |

### 3.2 Detección

Sidechain por canal: Butterworth de 2º orden a `scHPF`, ganancia unitaria en banda de paso.

Peak:

```text
d_ch = |sc_ch[n]|
e_ch = (d_ch > e_ch) ? αa·e_ch + (1−αa)·d_ch
                     : αr·e_ch + (1−αr)·d_ch
αa = exp(−1/(fs·attack/1000))
αr = exp(−1/(fs·release/1000))
```

RMS:

```text
m_ch = αw·m_ch + (1−αw)·sc_ch²
e_ch = sqrt(m_ch)
αw = exp(−1/(fs·rmsWindow/1000))
```

Link estéreo (`λ = link/100`):

```text
eLink = max(eL, eR)
e_ch' = λ·eLink + (1−λ)·e_ch
```

Con `link = 100` ambos canales comparten exactamente la misma reducción; con `link = 0` son independientes.

### 3.3 Curva estática con knee cuadrático

`L = 20·log10(max(e, 1e-12))`, `T = threshold`, `R = max(ratio, 1)`, `W = knee`.

```text
over = L − T

si W > 0 y 2·|over| <= W:
    t = over + W/2
    GR = (1/R − 1)·t² / (2W)
si over > 0:
    GR = over·(1/R − 1)
en otro caso:
    GR = 0
```

`GR ≤ 0` dB. La transición es continua en `over = ±W/2` y su derivada también.

### 3.4 Suavizado de la reducción

```text
si GR < GRstate: α = αa   (más reducción, ataque)
si GR >= GRstate: α = αr  (menos reducción, release)
GRstate = α·GRstate + (1−α)·GR
```

Se suaviza en dB, no en ganancia lineal, para que el release sea perceptualmente uniforme.

### 3.5 Makeup y salida

```text
GRauto = clamp(−0.5·T·(1 − 1/R), 0, 24)     si autoMakeup, si no 0
g      = 10^((GRstate + makeup + GRauto)/20)
wet    = x·g
y      = (1−m)·x + m·wet,  m = mix/100
```

El dry no pasa por el sidechain HPF; solo el detector lo usa.

### 3.6 Estado

Dos envolventes (L/R), dos estados RMS, dos estados de GR suavizada, dos biquads de sidechain, smoothers de parámetros. Sin memoria dinámica.

### 3.7 Validación

- Curva estática: para una senoide de amplitud constante, `GR` medido vs. fórmula ± 0,1 dB en 20 puntos de `L−T ∈ [−20, +20]` dB y `R ∈ {2, 4, 10, 20}`.
- Attack/release: entrada escalón +12 dB; tiempo para alcanzar el 63 % del GR final dentro de ±10 % del valor nominal (en 1 k…200 ms).
- `ratio = 1` ⇒ `GR = 0` en todo el rango y salida = entrada ± 1e-6.
- `mix = 0` ⇒ salida = entrada exactamente (sin aplicar makeup).
- `link = 100` ⇒ `GRL = GRR` muestra a muestra.
- `scHPF = 500 Hz` con señal de 50 Hz ⇒ el detector no reduce (GR ≈ 0).
- Auto-makeup: valor calculado coincide con la fórmula ± 0,01 dB.
- Sin NaN/Inf con `threshold = −60`, `ratio = 20`, `attack = 0,1`, `release = 10` y entrada de ruido a 0 dBFS.
- Latencia: 0.

---

## 4. Multiband Compressor de 4 bandas (`mbA`)

### 4.1 Parámetros

| ID relativo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|
| `active` | bool | false | — | — | — |
| `x1` | 40…500 | 120 | Hz | 150 | 30 ms |
| `x2` | 200…3000 | 800 | Hz | 800 | 30 ms |
| `x3` | 1000…12000 | 5000 | Hz | 4000 | 30 ms |
| `mix` | 0…100 | 100 | % | — | 20 ms |

Por banda `b ∈ {1…4}`: `bN.threshold` (−60…0, −18 dB), `bN.ratio` (1…20, 2:1, skew 2), `bN.attack` (0,1…200, 10 ms), `bN.release` (10…2000, 100 ms, skew 200), `bN.knee` (0…40, 10 dB), `bN.makeup` (0…24, 0 dB), `bN.mute` (false), `bN.solo` (false).

### 4.2 Crossovers Linkwitz-Riley 4º orden

Un LR4 es el producto de dos Butterworth de 2º orden con `Q = 1/√2 ≈ 0,70710678`:

```text
LP4(f) = LP2(f, Q=0.7071) · LP2(f, Q=0.7071)
HP4(f) = HP2(f, Q=0.7071) · HP2(f, Q=0.7071)
```

Propiedad: `LP4(f) + HP4(f)` es un allpass de magnitud 1. Por eso la suma de las cuatro bandas reconstruye la señal sin cancelación de fase.

Árbol de bandas:

```text
low1  = LP4(x, x1)
high1 = HP4(x, x1)

low2  = LP4(high1, x2)
high2 = HP4(high1, x2)

low3  = LP4(high2, x3)
low4  = HP4(high2, x3)

band1 = low1 ; band2 = low2 ; band3 = low3 ; band4 = low4
```

Cada banda pasa por un compresor idéntico al de la sección 3, sin sidechain HPF, sin link y sin auto-makeup:

```text
threshold, ratio, attack, release, knee, makeup
GR_b = computeGainReductionDb(level_b, threshold_b, ratio_b, knee_b)
g_b  = 10^((smooth(GR_b) + makeup_b)/20)
```

Mute/solo: si hay algún `solo` activo, solo suenan las bandas con solo; las demás se silencian. El mute se aplica con rampa de 5 ms para evitar clicks. Si una banda está muteada y otra en solo, el solo prevalece.

Mezcla final:

```text
wet = Σ_b band_b·g_b
y   = (1−m)·x_delayed + m·wet
```

`x_delayed` es la entrada retrasada por la latencia de grupo del árbol LR4. En la práctica los LR4 son IIR de fase no lineal; para que el dry/wet no cancele, el dry se alinea con un retardo fijo igual a la latencia de grupo agregada a la frecuencia de cruce central (se mide en Fase 5 y se fija como constante de diseño). Con `mix = 100` no se usa el dry.

Los crossovers se recalculan cada 32 muestras con `current()`; las frecuencias se fuerzan a `x1 < x2 < x3` con márgenes del 5 %.

### 4.3 Estado

8 biquads por canal (2 por cada LP/HP de cada cruce, 3 cruces = 12 secciones por canal; se comparten coeficientes entre canales), 4 compresores con sus envolventes, 4 gains de mute/solo suavizados.

### 4.4 Validación

- Reconstrucción: con todos los compresores a `ratio = 1`, `threshold = 0`, `makeup = 0`, `mix = 100`, la suma de bandas = entrada ± 0,1 dB en 20 Hz…20 kHz y ± 1° de fase.
- `x1 < x2 < x3` siempre; un intento de cruzarlos se corrige y se reporta.
- Cada banda atenúa solo su rango: una senoide 100 Hz por debajo de `x1` no modifica las bandas 2–4.
- `GR` por banda ± 0,1 dB respecto a la fórmula.
- `mute` y `solo`: la matriz de estados produce la combinación esperada; sin clicks (derivada acotada).
- Cambio de `x1` de 40 a 500 Hz: sin inestabilidad ni NaN; la suma sigue plana tras 50 ms.
- Latencia: 0 declarada; la alineación dry/wet se resuelve internamente con un retardo fijo, que no se suma a la latencia del plugin.

---

## 5. De-Esser (`dsA`)

### 5.1 Parámetros

| ID relativo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|
| `active` | bool | false | — | — | — |
| `freq` | 3000…10000 | 6000 | Hz | 6000 | 20 ms |
| `threshold` | −60…0 | −24 | dB | — | 20 ms |
| `ratio` | 1…20 | 4 | :1 | 4 | 20 ms |
| `attack` | 0,1…50 | 1 | ms | 1 | 20 ms |
| `release` | 10…500 | 50 | ms | 50 | 20 ms |
| `range` | 0…24 | 12 | dB | — | 20 ms |
| `listen` | bool | false | — | — | — |
| `mix` | 0…100 | 100 | % | — | 20 ms |

### 5.2 División de banda

```text
low  = LP4(x, freq)
high = HP4(x, freq)
```

Detección selectiva: `det = BPF(high, freq, Q=1.0)` (RBJ band-pass, 0 dB de pico). Nivel RMS con ventana de 10 ms:

```text
L = 20·log10(sqrt(mean(det²)) + 1e-12)
GR = clamp(computeGainReductionDb(L, threshold, ratio, knee=3 dB), −range, 0)
```

`GR` se suaviza con ataque/release del catálogo. La reducción se aplica **solo a la banda alta**:

```text
y = low + high·10^(GR/20)
```

Con `GR = 0`, `low + high` es un allpass LR4, por lo que el de-esser es transparente.

`listen = true` sustituye la salida por `high·10^(GR/20)`, para oír exactamente lo que se está atenuando.

### 5.3 Estado

Dos LR4 por canal, un BPF de detector por canal, dos envolventes RMS, dos GR suavizadas.

### 5.4 Validación

- `listen = false`, señal de 200 Hz: salida = entrada ± 0,05 dB (la banda alta no contiene energía).
- Ráfaga de 6 kHz a −10 dBFS con `threshold = −24`, `ratio = 4`: reducción en el pico ≥ 6 dB y ≤ `range`.
- `range = 0` ⇒ sin reducción.
- `listen = true` ⇒ solo se oye la banda alta, con la reducción aplicada.
- Con señal por debajo del umbral, `GR = 0` y salida = entrada ± 1e-6.
- Sin clicks al cambiar `freq` (rampa 20 ms) ni al alternar `listen` (crossfade 5 ms).
- Latencia: 0.

---

## 6. Gate / Expander (`gateA`)

### 6.1 Parámetros

| ID relativo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|
| `active` | bool | false | — | — | — |
| `threshold` | −80…0 | −40 | dB | — | 20 ms |
| `range` | −80…0 | −60 | dB | — | 20 ms |
| `attack` | 0,05…100 | 1 | ms | 1 | 20 ms |
| `hold` | 0…500 | 50 | ms | — | — |
| `release` | 5…2000 | 200 | ms | 200 | 20 ms |
| `hysteresis` | 0…12 | 3 | dB | — | — |
| `scHPF` | 20…500 | 80 | Hz | 100 | 20 ms |
| `mix` | 0…100 | 100 | % | — | 20 ms |

### 6.2 Detección y máquina de estados

Detector: Butterworth 2º orden a `scHPF`, envolvente de pico con ataque fijo 0,5 ms y release fijo 20 ms, `L = 20·log10(e + 1e-12)`.

```text
OPEN   si L > threshold
HOLD   si estaba OPEN y L <= threshold ; arranca hold
CLOSED si estaba HOLD, L <= threshold − hysteresis y hold expiró
```

Durante HOLD, un `L > threshold` vuelve a OPEN y reinicia el contador. La histéresis evita chatter en señales cercanas al umbral.

Ganancia objetivo:

```text
targetDb = OPEN o HOLD ? 0 : range
```

`range = 0` ⇒ el gate es transparente (no hay atenuación). `range = −80` ⇒ cierre total.

Suavizado en dB con ataque al abrir y release al cerrar:

```text
α = targetDb > stateDb ? exp(−1/(fs·attack/1000))
                       : exp(−1/(fs·release/1000))
stateDb = α·stateDb + (1−α)·targetDb
g = 10^(stateDb/20)
y = (1−m)·x + m·x·g
```

### 6.3 Estado

Envolvente del detector, biquad de sidechain, `stateDb`, contador de hold en muestras, enum de estado. Sin allocaciones.

### 6.4 Validación

- Apertura: escalón de −30 dBFS con `threshold = −40` ⇒ `g → 1` en ≤ 1,5× attack.
- Cierre: señal a −60 dBFS ⇒ `stateDb → range` en ≤ 1,5× (release + hold).
- Histéresis: señal oscilando entre `threshold` y `threshold − hysteresis/2` no produce chatter (≤ 2 transiciones en 5 s).
- `hold = 500 ms`: el cierre no empieza antes de 500 ms ± 1 muestra tras cruzar el umbral.
- `range = 0` ⇒ salida = entrada.
- `scHPF = 500 Hz` con señal de 50 Hz: el gate no se abre (el detector no ve la señal).
- Ataque de 0,05 ms: sin overshoot > 0,5 dB.
- Latencia: 0.

---

## 7. Transient Shaper (`transA`)

### 7.1 Parámetros

| ID relativo | Rango | Default | Unidad | Smooth |
|---|---|---|---:|---:|
| `active` | bool | false | — | — |
| `attack` | −100…+100 | 0 | % | 20 ms |
| `sustain` | −100…+100 | 0 | % | 20 ms |
| `sensitivity` | 0…100 | 50 | % | 20 ms |
| `output` | −12…+12 | 0 | dB | 20 ms |
| `mix` | 0…100 | 100 | % | 20 ms |

### 7.2 Detección diferencial

Dos envolventes de pico por canal, con la sensibilidad modulando sus tiempos:

```text
s = sensitivity/100
fastAttack  = 2.0 − 1.8·s    [ms]   (2 → 0,2)
fastRelease = 40             [ms]
slowAttack  = 20             [ms]
slowRelease = 400 − 300·s    [ms]   (400 → 100)

ef = peakEnv(|x|, fastAttack, fastRelease)
es = peakEnv(|x|, slowAttack, slowRelease)
```

Índice de transiente:

```text
d = 20·log10((ef + 1e-12)/(es + 1e-12))   [dB], clamp a [−24, +24]
```

`d > 0` en ataques; `d < 0` en colas/decaimiento; `d ≈ 0` en régimen estacionario.

### 7.3 Ley de conformación

```text
gDb = clamp( (attack/100)·0.5·max(d, 0)
           + (sustain/100)·0.5·max(−d, 0), −18, +18 )
```

- `attack = +100` y `d = 12 dB` ⇒ +6 dB en el ataque.
- `attack = −100` ⇒ atenúa el ataque hasta −6 dB con el mismo `d`.
- `sustain = +100` y `d = −12 dB` ⇒ +6 dB en la cola.
- Ambos a 0 ⇒ `gDb = 0`.

Suavizado de `gDb` con one-pole de 5 ms; después `output` y `mix` lineal.

### 7.4 Estado

Cuatro envolventes (2 por canal), un `stateDb`, smoothers.

### 7.5 Validación

- Impulso aislado: `attack = +100` ⇒ pico de salida +6 ± 0,5 dB respecto a `attack = 0`.
- Senoide continua: `d ≈ 0` ⇒ salida = entrada ± 0,1 dB con `output = 0`, `mix = 100`.
- Decaimiento exponencial: `sustain = +100` ⇒ cola +6 ± 0,5 dB; `sustain = −100` ⇒ −6 ± 0,5 dB.
- `mix = 0` ⇒ identidad exacta.
- Cambios de parámetro sin clicks (rampa 20 ms).
- Latencia: 0.

---

## 8. Saturator (`satA`, `satB`)

### 8.1 Parámetros

| ID relativo | Rango | Default | Unidad | Smooth |
|---|---|---|---:|---:|
| `active` | bool | false | — | — |
| `model` | choice | Tube (0) | — | — |
| `drive` | 0…24 | 6 | dB | 20 ms |
| `tone` | −12…+12 | 0 | dB | 20 ms |
| `bias` | −0,5…+0,5 | 0,1 | — | 20 ms |
| `autoGain` | bool | true | — | — |
| `output` | −24…+24 | 0 | dB | 20 ms |
| `mix` | 0…100 | 100 | % | 20 ms |

Modelos (orden congelado): `0 Tube`, `1 Tape`, `2 Diode`, `3 Transistor`.

### 8.2 Sobremuestreo 8×

Cadena: upsample 8× por zero-stuffing → FIR polifásico lineal de 512 taps (64 por fase), ventana Kaiser `β = 8`, corte a `0,45·fs_in`, stopband desde `0,55·fs_in` con rechazo ≥ 80 dB → waveshaper → DC blocker → downsample con el mismo FIR.

- Retardo de grupo: `(512−1)/2 = 255,5` muestras a 8× = **31,94 muestras base**, declarado **32**.
- El FIR y las tablas de fase se calculan en `prepare`; no hay allocaciones en `process`.
- El path dry se retrasa 32 muestras para que `mix` no produzca comb filtering.

### 8.3 Curvas de waveshaping

Con `k = 10^(drive/20)`, `b = bias`, `u = k·x + b`:

```text
Tube:       f(u) = tanh(u) − tanh(b)
            f(0) = 0 ; f'(0) = sech²(b) ≈ 1

Tape:       f(u) = clamp(u − u³/3, −2/3, +2/3)
            f(0) = 0 ; f'(0) = 1 − b²

Diode:      f(u) = u >= 0 ? 1 − exp(−u)
                          : −0.5·(1 − exp(2u))
            f(0) = 0 ; f'(0±) = 1 ; asíntotas +1 / −0,5

Transistor: f(u) = u / (1 + |u|^1.5)^(2/3)
            f(0) = 0 ; f'(0) = 1 ; asíntotas ±1
```

Todas las curvas son continuas y con derivada continua a trozos. La asimetría de Tube/Diode y el `bias` generan armónicos pares; Tape/Transistor son predominante impares.

Después del shaper:

```text
y1 = DcBlocker(f(u), 5 Hz)
```

El DC blocker es obligatorio: sin él, el bias y la asimetría de Diode desplazan el nivel medio.

### 8.4 Tone

Split de 1er orden a 700 Hz:

```text
a = 1 − exp(−2π·700/fs)
lp = lp + a·(y1 − lp)
hp = y1 − lp
y2 = lp + hp·10^(tone/20)
```

`tone > 0` realza agudos; `tone < 0` los atenúa. La banda baja queda a ganancia unitaria.

### 8.5 Auto-gain

RMS de entrada y salida con constante de 500 ms, calculado a la frecuencia base (antes/dese pués del bloque de saturación):

```text
gAuto = clamp(sqrt(rmsIn / (rmsOut + 1e-12)), 1/16, 16)   → ±24 dB
gAuto suavizado con 50 ms
```

Si `autoGain = false`, `gAuto = 1`. `output` se aplica siempre después.

### 8.6 Mezcla

```text
dry = retardo32(x)
wet = y2 · gAuto · 10^(output/20)
y   = (1−m)·dry + m·wet
```

### 8.7 Validación

- THD+N crece monótonamente con `drive` para una senoide de 1 kHz a −6 dBFS en los cuatro modelos.
- Rechazo de aliasing: para 1 kHz a −6 dBFS, todas las imágenes > 20 kHz quedan ≥ 70 dB por debajo del fundamental en la salida a 48 kHz.
- `drive = 0`, señal a −40 dBFS ⇒ `|y − x| < 0,5 dB` (zona casi lineal) en Tube y Tape.
- Diode: componente DC de salida < 1e-4 tras 1 s con `bias = 0,5`.
- `mix = 0` ⇒ salida = entrada retrasada 32 muestras, no la entrada directa.
- Latencia medida por correlación cruzada: 32 ± 0 muestras.
- Barrido de `bias` y `drive` en 192 kHz: sin NaN/Inf.

---

## 9. Stereo Imager (`imgA`)

### 9.1 Parámetros

| ID relativo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|
| `active` | bool | false | — | — | — |
| `width` | 0…200 | 100 | % | — | 20 ms |
| `mid` | −12…+12 | 0 | dB | — | 20 ms |
| `side` | −12…+12 | 0 | dB | — | 20 ms |
| `bassMonoFreq` | 20…500 | 120 | Hz | 100 | 20 ms |
| `bassMonoAmount` | 0…100 | 100 | % | — | 20 ms |

### 9.2 Algoritmo

```text
M = 0.5·(L + R)
S = 0.5·(L − R)

S1 = S · (width/100)
S2 = S1 − (bassMonoAmount/100)·LP4(S1, bassMonoFreq)
M2 = M · 10^(mid/20)
S3 = S2 · 10^(side/20)

L' = M2 + S3
R' = M2 − S3
```

`width = 0` ⇒ `S3 = 0` ⇒ salida mono exacta. `width = 200` duplica el side (ancho subjetivo ×2). El bass-mono usa el mismo LR4 que el multibanda, aplicado al side, de modo que a 100 % elimina el contenido lateral por debajo de `bassMonoFreq` sin tocar el mid.

La reconstrucción es exacta: con `width = 100`, `mid = side = 0`, `bassMonoAmount = 0`, `L' = L` y `R' = R` ± 1e-7.

### 9.3 Estado

Dos LR4 por canal para el bass-mono (o un par compartido aplicado al side), smoothers. Publica correlación estimada solo como lectura de UI (la fuente autoritativa es `PhaseScope`).

### 9.4 Validación

- Identidad con defaults ± 1e-7.
- `width = 0` ⇒ `L' = R'` exactamente.
- `width = 200` ⇒ `(L'−R') = 2(L−R)` ± 1e-6.
- Bass mono: señal con side de 60 Hz y mid de 1 kHz; a 100 % el side de 60 Hz cae ≥ 24 dB (LR4).
- `mid`/`side` dB aplicados con precisión ± 0,01 dB.
- Sin inversión de fase ni desplazamiento de la imagen central.
- Latencia: 0 (el bass-mono es feed-forward, no añade retardo declarado).

---

## 10. Reverb FDN de 8 líneas (`revA`)

### 10.1 Parámetros

| ID relativo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|
| `active` | bool | false | — | — | — |
| `size` | 0…100 | 50 | % | — | 20 ms |
| `decay` | 0,2…20 | 2 | s | 2 | 30 ms |
| `predelay` | 0…200 | 20 | ms | — | 20 ms |
| `damping` | 0…100 | 40 | % | — | 20 ms |
| `diffusion` | 0…100 | 70 | % | — | 20 ms |
| `width` | 0…200 | 100 | % | — | 20 ms |
| `lowcut` | 20…500 | 80 | Hz | 100 | 20 ms |
| `highcut` | 1000…20000 | 12000 | Hz | 8000 | 20 ms |
| `mix` | 0…100 | 25 | % | — | 20 ms |
| `freeze` | bool | false | — | — | — |

### 10.2 Pre-delay y difusión

Pre-delay entero de `0…200 ms` (`round(fs·ms/1000)`), con lectura fraccional suavizada 20 ms para cambios sin clicks.

Difusión: 4 allpass Schroeder en serie antes de la FDN:

```text
delays = {5.1, 8.3, 12.7, 21.1} ms
gDiff  = 0.7·(diffusion/100)

Allpass Schroeder:
y = −g·x + z
z = x + g·y
```

### 10.3 Líneas FDN

Longitudes base (ms): `{29.7, 37.1, 41.1, 43.7, 47.9, 53.3, 59.9, 63.7}`.

```text
scale = 0.35 + 1.15·(size/100)
L_i   = max(8, round(fs·ms_i·scale/1000))
g_i   = min(0.9995, 10^(−3·L_i/(fs·decay)))      si freeze = false
g_i   = 1.0                                      si freeze = true (entrada muteada)
```

Damping: LP de 1er orden por línea con corte dependiente del parámetro:

```text
fc_damp = 18000·(1 − 0.85·(damping/100))   [Hz]
aDamp   = 1 − exp(−2π·fc_damp/fs)
yDamp_i = yDamp_i + aDamp·(lineOut_i − yDamp_i)
```

Matriz de realimentación: Hadamard 8×8 normalizada por `1/√8`, aplicada con 3 etapas de mariposa (24 sumas/restas por muestra, sin multiplicaciones por matriz).

```text
v = H8 · (yDamp_0 … yDamp_7) / √8
in_i = inputDiffused · (i par ? +1 : −1) / √8
lineIn_i = in_i + g_i·v_i
```

La inyección con signos alternos evita que las 8 líneas arranquen en fase y produce un campo difuso.

### 10.4 Salida

```text
wetL = (1/√8)·Σ_{i par}  lineOut_i
wetR = (1/√8)·Σ_{i impar} lineOut_i
```

Filtros wet: HPF Butterworth 2º orden a `lowcut`, LPF Butterworth 2º orden a `highcut`. Anchura M/S igual que el imager, sin bass-mono:

```text
M = 0.5·(wetL + wetR) ; S = 0.5·(wetL − wetR)
S = S·(width/100)
wetL = M + S ; wetR = M − S
```

Mezcla equal-power:

```text
y = cos(πm/2)·dry + sin(πm/2)·wet,  m = mix/100
```

### 10.5 Freeze

`freeze = true`: entrada al tanque = 0, `g_i = 1`, damping sigue activo (disipa agudos lentamente), DC blocker a la salida del tanque para evitar deriva. El freeze debe ser estable indefinidamente; la energía se limita con `sanitize` y un detector de no-finitos que resetea el tanque.

### 10.6 Validación

- RT60 medido (T30) vs. `decay`: ±15 % para `size = 50`, `damping = 0`, `diffusion = 70`, en 44,1/48/96 kHz.
- Estabilidad: 60 s con `decay = 20`, `size = 100`, `feedback` interno < 1; sin crecimiento ni NaN.
- Freeze: 60 s sin entrada; salida acotada, sin DC > 1e-3, sin oscilación creciente.
- Correlación L/R a 500 ms < 0,3 (campo decorrelacionado).
- `mix = 0` ⇒ identidad exacta (sin retardo, porque el pre-delay solo afecta al wet).
- `lowcut = 500 Hz` ⇒ energía wet < 100 Hz atenuada ≥ 24 dB.
- `highcut = 1 kHz` ⇒ energía wet > 4 kHz atenuada ≥ 24 dB.
- Cambio de `size`: sin clicks audibles (rampa 20 ms) y sin inestabilidad.
- Latencia: 0.

---

## 11. Stereo Delay (`dlyA`)

### 11.1 Parámetros

| ID relativo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|
| `active` | bool | false | — | — | — |
| `sync` | bool | true | — | — | — |
| `division` | choice | `1/4` (índice 2) | — | — | — |
| `timeL` | 1…2000 | 350 | ms | — | 30 ms |
| `timeR` | 1…2000 | 500 | ms | — | 30 ms |
| `feedback` | 0…95 | 35 | % | — | 20 ms |
| `pingpong` | bool | false | — | — | — |
| `lowcut` | 20…2000 | 120 | Hz | 200 | 20 ms |
| `highcut` | 1000…20000 | 8000 | Hz | 8000 | 20 ms |
| `mix` | 0…100 | 25 | % | — | 20 ms |

Divisiones (orden congelado): `1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4 D, 1/8 D, 1/4 T, 1/8 T, Free`.

### 11.2 Tiempo de retardo

Con `sync = true` y tempo `BPM` del host:

```text
beats:  1/1 = 4 ; 1/2 = 2 ; 1/4 = 1 ; 1/8 = 0.5 ; 1/16 = 0.25 ; 1/32 = 0.125
        D (dotted) = beats·1.5 ; T (triplet) = beats·(2/3)
ms = 60000·beats / BPM
```

Con `sync = false` o `division = Free`: `ms = timeL` / `timeR`.

El tiempo se suaviza con one-pole de 30 ms y se lee con interpolación lineal:

```text
d = clamp(dSamples, 0, capacity−2)
i0 = floor(d) ; frac = d − i0
y = buf[i0] + frac·(buf[i0+1] − buf[i0])
```

Si el host no reporta tempo válido (`BPM ≤ 0`), se usa 120 BPM y se marca el flag de diagnóstico.

### 11.3 Enrutamiento y feedback

Normal (ping-pong off):

```text
readL = lineL(dL) ; readR = lineR(dR)
fbL = filtered(readL)·g ; fbR = filtered(readR)·g
lineLIn = xL + fbL ; lineRIn = xR + fbR
g = clamp(feedback/100, 0, 0.95)
```

Ping-pong:

```text
lineLIn = 0.5·(xL + xR) + fb·readR
lineRIn = fb·readL
```

El filtro del lazo es HPF 2º orden a `lowcut` seguido de LPF 2º orden a `highcut`. El feedback nunca alcanza 1,0.

### 11.4 Mezcla

Equal-power sobre el dry (sin retardo) y el wet. El delay no añade latencia al plugin.

### 11.5 Validación

- Impulso: primer eco en `round(ms·fs/1000)` ± 1 muestra.
- `feedback = 95 %`: 20 repeticiones decrecen según `0.95^n` ± 0,5 dB; sin crecimiento.
- Ping-pong: primer eco alterna canales; con entrada mono el balance L/R del wet es simétrico.
- Cambio de `division` a tempo constante: el tiempo converge en ≤ 100 ms sin clicks.
- `lowcut = 2 kHz`, `highcut = 2 kHz`: el lazo se vuelve inestable por diseño; se fuerza `highcut > lowcut·1,05` y se reporta.
- BPM inválido ⇒ 120 BPM, sin excepción.
- Latencia: 0.

---

## 12. Mastering: Clipper + True Peak Limiter (`masterA`)

### 12.1 Parámetros

| ID relativo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|
| `active` | bool | true | — | — | — |
| `clipperOn` | bool | true | — | — | — |
| `clipperMode` | choice | Soft (0) | — | — | — |
| `drive` | 0…12 | 0 | dB | — | 20 ms |
| `ceiling` | −12…0 | −0,3 | dBTP | — | 20 ms |
| `limiterOn` | bool | true | — | — | — |
| `release` | 10…1000 | 100 | ms | 100 | 20 ms |

### 12.2 Oversampling y latencia

Cadena a 4×: FIR polifásico de 512 taps (128 por fase), Kaiser `β = 8`, corte a `0,45·fs`, stopband ≥ 80 dB. Retardo de grupo `255,5` muestras a 4× = `63,875` muestras base → **64**.

Lookahead del limitador: `256` muestras a 4× = **64** base. Latencia total declarada: **128** muestras base.

### 12.3 Clipper

Se ejecuta a 4×, antes del limitador. `c = 10^(ceiling/20)`.

```text
u = x·10^(drive/20)

Soft:  y = c·tanh(u/c)
Hard:  y = clamp(u, −c, +c)
```

Soft empieza a comprimir antes del techo (THD gradual); Hard recorta en seco. `drive = 0` ⇒ `u = x` y Soft todavía curva los picos por encima de `c`. `clipperOn = false` deja pasar la señal sin recorte.

### 12.4 Limitador con lookahead

Ventana `W = 256` muestras a 4×. Máximo deslizante de `|x|` con deque monótona de capacidad `W+1` (implementación con `std::array` e índices; sin allocaciones).

```text
peak[n]   = max |x[n−W+1 … n]|
target[n] = min(1, c / (peak[n] + 1e-12))

release: αr = exp(−1/(fs4·release/1000))
gRel[n] = min(target[n], αr·gRel[n−1] + (1−αr)·target[n])

attack: la ganancia aplicada en n es una rampa lineal desde gRel[n−W]
        hasta gRel[n] a lo largo de W muestras (lookahead)
y[n] = x[n]·gRamp[n]
```

Si `limiterOn = false`, `g = 1`. `ceiling` se aplica también como clamp final a 4× para garantizar que ningún pico inter-muestra supere el techo.

### 12.5 Detección de true peak

El detector de picos trabaja sobre la señal a 4× (no sobre la base), por lo que el máximo medido es una estimación de true peak conforme a ITU-R BS.1770-4. El valor se publica en `dBTP = 20·log10(max|x4x|)`.

### 12.6 Validación

- Senoide 997 Hz a 0 dBFS, `ceiling = −0,3`, `limiterOn = true`: true peak de salida ≤ −0,3 dBTP (tolerancia 0,0/+0,02 dB).
- Sin limitador y con clipper Soft a `drive = 0`: salida ≤ `ceiling` en la base, pero puede superar levemente entre muestras; se reporta como advertencia.
- Latencia por correlación: 128 ± 0 muestras.
- Release 100 ms: recuperación al 63 % en 100 ± 10 ms tras un pico aislado.
- Sin pumping audible: tren de picos a 1 kHz/100 ms con release 300 ms; la ganancia no oscila más de 0,5 dB entre picos.
- `clipperOn = false`, `limiterOn = false` ⇒ salida = entrada ± 1e-6 (solo latencia).
- Barrido de `ceiling` de 0 a −12 dBTP: sin overshoot en 10⁶ muestras de ruido y senos.
- Aliasing del clipper: imágenes > 20 kHz ≥ 70 dB por debajo del fundamental con `drive = 12`.

---

## 13. Medición

### 13.1 K-weighting (ITU-R BS.1770-4)

Dos etapas en serie por canal, doble precisión.

Etapa 1 — shelf de agudos (+4 dB por encima de ~1,7 kHz):

```text
f0 = 1681.974450955533 Hz ; G = 3.999843853973347 dB ; Q = 0.7071752369554196

Coeficientes a 48 kHz (b normalizados):
b = [ 1.53512485958697, −2.69169618940638, 1.19839281085285 ]
a = [ 1.0,              −1.69065929318241, 0.73248077421585 ]
```

Etapa 2 — high-pass a 38 Hz:

```text
f0 = 38.13547087602444 Hz ; Q = 0.5003270373238773

Coeficientes a 48 kHz:
b = [ 1.0, −2.0, 1.0 ]
a = [ 1.0, −1.99004745483398, 0.99007225036621 ]
```

Para otras frecuencias de muestreo se recalcula el mismo prototipo analógico con transformada bilineal y prewarping en `f0`; los coeficientes de 48 kHz se usan como referencia de test. La ganancia global de la etapa se compensa para que una senoide de 1 kHz a −23 dBFS mida exactamente −23 LUFS (constante `−0.691` incluida en la fórmula de loudness).

### 13.2 Loudness (EBU R128 / BS.1770-4)

Por canal `c`, con peso `G_c = 1.0` para L/R (1.41 para surround, fuera de alcance):

```text
z_c = media de x_K² sobre la ventana
L_K = −0.691 + 10·log10( Σ_c G_c·z_c )
```

| Medida | Ventana | Hop | Gating |
|---|---:|---:|---|
| Momentary | 400 ms | 100 ms | ninguno |
| Short-Term | 3 s | 100 ms | ninguno |
| Integrated | 400 ms | 100 ms | absoluto −70 LUFS + relativo −10 LU |
| LRA | 3 s | 1 s | absoluto −70 LUFS + relativo −20 LU |

Integrated:

```text
1. Calcular l_i para todos los bloques de 400 ms.
2. Absoluto: conservar l_i > −70 LUFS.
3. L_ungated = −0.691 + 10·log10(media de z_i de los conservados).
4. Relativo: conservar l_i > L_ungated − 10.
5. Integrated = −0.691 + 10·log10(media de z_i de los conservados en 2 y 4).
```

LRA: percentiles P10 y P95 de los `l_i` de 3 s que pasan ambos gates; `LRA = P95 − P10`. Se publica 0 si hay menos de 2 bloques válidos.

Se publican también RMS (ventana 400 ms), sample peak y true peak. Todos los valores se escriben en `std::atomic<float>`; la UI los lee a 30–60 Hz.

### 13.3 True peak

Oversampling 4× con FIR polifásico de 48 taps por fase según el Anexo 2 de BS.1770-4. `TP_dB = 20·log10(max|x_4x|)` sobre la ventana de medición. El medidor del plugin es independiente del oversampling de `masterA`; puede medir la salida ya limitada.

### 13.4 FFT 4096 (analizador de espectro)

- Ventana `N = 4096`, hop `1024` (75 % de solape).
- Hann: `w[n] = 0.5 − 0.5·cos(2πn/(N−1))`.
- Blackman: `w[n] = 0.42 − 0.5·cos(2πn/(N−1)) + 0.08·cos(4πn/(N−1))`.
- FFT real → magnitud corregida: `mag[k] = 2·|X[k]| / Σw[n]`.
- `dB[k] = 20·log10(max(mag[k], 1e-12))`.
- Promediado exponencial: `s[k] ← α·s[k] + (1−α)·|X[k]|²`, `α = exp(−hop/(fs·τ))`, `τ = 200 ms`.
- Peak hold: `p[k] ← max(s[k], p[k] − fall·Δt)`, `fall = 12 dB/s`.
- Se publican 2048 bins (0…fs/2) en un triple buffer. La UI aplica el eje logarítmico.

Validación: senoide de 1 kHz → máximo en el bin `round(1000·N/fs)` (±1 bin) y nivel ±0,1 dB tras corrección de ganancia coherente. Ruido rosa: pendiente −3 dB/octava ±0,5 dB entre 100 Hz y 10 kHz.

### 13.5 Vectorscopio

El audio thread escribe pares L/R decimados (factor 4) en una FIFO SPSC de 1024 puntos y actualiza:

```text
a = 1 − exp(−1/(fs/4·0.3))          // ventana ~300 ms
num  += a·(L·R − num)
denL += a·(L² − denL)
denR += a·(R² − denR)
ρ = clamp(num / sqrt(denL·denR + 1e-12), −1, +1)
```

Display: rotación 45° para el goniómetro:

```text
x = (L − R)/√2
y = (L + R)/√2
```

Validación: `L = R` ⇒ ρ = +1 y traza vertical; `L = −R` ⇒ ρ = −1 y traza vertical invertida; señales descorrelacionadas ⇒ ρ ≈ 0 y nube circular. Sin FFT en el audio thread.

---

## 14. Criterios globales de aceptación DSP

| Criterio | Umbral |
|---|---|
| Samples no finitos a la salida | 0 en 10⁸ muestras con fuzz de parámetros |
| Crecimiento de estado en filtros estables | `|y| ≤ 16` tras 60 s |
| Error de identidad en módulos transparentes | ≤ 1e-6 |
| Error de ganancia estática | ≤ 0,1 dB |
| Latencia declarada | exacta, verificada por correlación |
| Cambio de parámetro audible | sin click (derivada acotada a 4× la señal) |
| Aliasing en saturación/clipper | ≥ 70 dB por debajo del fundamental en banda audible |
| RT60 de reverb | ±15 % del valor nominal |
| GR de compresores | ±0,1 dB respecto a la curva |
| True peak | ≤ ceiling + 0,02 dB |
| Loudness | ±0,1 LU en las señales de EBU Tech 3341 |
