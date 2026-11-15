# 08 — Estrategia de pruebas

## 1. Principios

1. **Todo DSP se prueba con señal, no con inspección visual.** Una ecuación sin test numérico no está implementada.
2. **Los tests son deterministas.** Semillas fijas, sin red, sin reloj, sin aleatoriedad no controlada, sin dependencia del estado del host.
3. **El hilo de audio se prueba como contrato de tiempo real**, no solo como resultado. Un test que solo compara muestras no detecta una asignación o un lock.
4. **Los gates son binarios.** Una fase no se cierra con «casi pasa» ni con tolerancias relajadas sin ADR.
5. **La suite corre en CI desde la Fase 2.** No se acumulan tests para el final.

## 2. Herramientas y niveles

| Nivel | Herramienta | Qué cubre |
|---|---|---|
| Unitario DSP | Catch2 v3.7+ | Cada procesador, ecuación y estado. |
| Unitario de dominio | Catch2 | Catálogo, estado, temas, parser JSON, reglas ARIA. |
| Integración | Catch2 + `juce::AudioPluginInstance` | `PluginProcessor` completo, rack, PDC, automatización. |
| Tiempo real | Instrumentación propia | Allocaciones, locks, latencia de bloque, workers. |
| Compatibilidad | `pluginval` + DAWs reales | Escaneo, automatización, estado, PDC, estabilidad. |
| Sanitizers | ASan, UBSan, TSan (Linux) | Memoria, UB, carreras. |
| Rendimiento | `PerformanceCounter` / `steady_clock` | CPU por escenario, memoria, arranque. |

No se usa `juce::UnitTest` para la suite principal: Catch2 permite fixtures, secciones, generators y reporting uniforme en CI.

## 3. Estructura

```text
Tests/
├── CMakeLists.txt
├── main.cpp                       # Catch2 session
├── Support/
│   ├── SignalGenerators.h/.cpp    # senos, ruido, impulsos, sweeps, multitonos
│   ├── TestHost.h/.cpp            # preparación de processor, bloques, automatización
│   ├── Golden.h/.cpp              # comparación numérica y snapshots
│   └── RtProbe.h/.cpp             # alloc/lock/tiempo
├── DSP/
│   ├── UtilityTests.cpp
│   ├── EqTests.cpp
│   ├── CompressorTests.cpp
│   ├── MultibandTests.cpp
│   ├── DeEsserTests.cpp
│   ├── GateTests.cpp
│   ├── TransientTests.cpp
│   ├── SaturatorTests.cpp
│   ├── ImagerTests.cpp
│   ├── ReverbTests.cpp
│   ├── DelayTests.cpp
│   └── MasteringTests.cpp
├── Metering/
│   ├── LoudnessEbuTests.cpp
│   ├── TruePeakTests.cpp
│   ├── SpectrumTests.cpp
│   └── PhaseScopeTests.cpp
├── Core/
│   ├── CatalogTests.cpp
│   ├── RackTests.cpp
│   ├── StateTests.cpp
│   ├── ThemeTests.cpp
│   └── RtSafetyTests.cpp
├── AI/
│   ├── AriaCaptureTests.cpp
│   ├── AriaFeatureTests.cpp
│   ├── AriaRuleTests.cpp
│   └── AriaApplyTests.cpp
└── Compatibility/
    ├── PluginVal.md
    └── DawMatrix.md
```

Convención de nombres:

```cpp
TEST_CASE ("dsp/eq/identity", "[dsp][eq]")
TEST_CASE ("rt/process/no-allocations", "[rt][core]")
TEST_CASE ("ebu/3341/integrated-minus23", "[metering][ebu]")
```

Los tags permiten ejecutar subconjuntos: `[dsp]`, `[rt]`, `[ebu]`, `[ai]`, `[slow]`, `[compat]`.

## 4. Señales y fixtures

Generadores propios, sin archivos de audio externos salvo los casos EBU:

| Señal | Uso |
|---|---|
| Senoide 1 kHz a −23, −33, −36 dBFS | EBU Tech 3341. |
| Ruido rosa calibrado | Respuesta tonal, bandas, tilt. |
| Ruido blanco con semilla fija | Estabilidad, NaN, denormales. |
| Impulso unitario | Latencia, respuesta al impulso, PDC. |
| Sweep logarítmico 20 Hz…0,49·fs | Aliasing, estabilidad, respuesta. |
| Multitono (p. ej. 100 Hz + 1 kHz + 8 kHz) | Intermodulación, saturación. |
| Silencio digital | Denormales, CPU, freeze. |
| Rampa de ganancia | Clicks, suavizado. |
| Bloques de 0, 1, 64, 128, 256, 512, 1024, 2048 | Tamaño variable del host. |

Reglas:

- El PRNG de tests es explícito y con semilla por caso (`std::mt19937` o xorshift documentado). Nunca `rand()`.
- Los goldens se guardan como arrays de `float` o CSV pequeño, no como audio comprimido.
- Los tests comparan con tolerancia declarada; no se usa igualdad exacta salvo en latencia y valores discretos.
- No se descargan fixtures en tiempo de test. Los archivos EBU se versionan o se generan analíticamente.

## 5. Tests DSP por módulo

Tolerancias por defecto: ganancia ±0,01 dB, frecuencia ±0,5 %, THD ±0,5 dB, latencia exacta en muestras. Cada módulo incluye, como mínimo:

| Módulo | Casos obligatorios |
|---|---|
| Utility | Identidad con defaults; gain ±24 dB; pan ley −3 dB; polaridad; mono; swap; rampa sin clicks. |
| EQ | Identidad con 8 bandas en 0 dB; respuesta en `f0` ±0,1 dB; RBJ contra coeficientes de referencia; barrido de Q y frecuencia; estabilidad en 44,1…192 kHz; 8 bandas simultáneas. |
| Compressor | Curva estática con knee; umbral; ratio; ataque/release al 63 %; detector peak vs RMS; stereo link 0/100; auto-makeup; mix dry/wet alineado. |
| Multiband | Suma de bandas plana ±0,1 dB con compresión a 0; aislamiento de banda; crossover 40…12 kHz; mute/solo; latencia declarada; sin cancelación en dry/wet. |
| De-Esser | Reducción ≥6 dB en ráfaga de 6 kHz; reducción ≤`range`; frecuencia 3…10 kHz; listen; mix; no afecta 1 kHz. |
| Gate | Umbral; histéresis sin chatter; hold; attack/release; range; sidechain HPF; señal por debajo del umbral. |
| Transient | Attack +100/−100; sustain +100/−100; sensibilidad; unidad con 0; output; mix; sin pre-eco. |
| Saturator | THD monótona con drive en 4 modelos; aliasing ≥70 dB; auto-gain; DC en Diode; bias; tone; latencia 32 ±0. |
| Imager | Identidad ±1e-7; width 0 mono exacto; width 200; mid/side ±12 dB; bass mono ≥24 dB; correlación; sin desplazamiento de fase. |
| Reverb | RT60 vs decay ±10 %; predelay; damping; difusión; width; freeze sin fuga; lowcut/highcut; sin DC ni NaN; cola finita. |
| Delay | Tiempo 1…2000 ms; sync a BPM; división; feedback 0…95 %; ping-pong; lowcut/highcut; máxima memoria; sin runaway. |
| Mastering | Curva del clipper; techo del limitador; true peak ≤ ceiling + 0,1 dB; lookahead 128; release; oversampling 4×; clipper off; limiter off; drive 0…12 dB. |

### 5.1 Pruebas transversales

- **Identidad global**: todos los módulos inactivos ⇒ salida = entrada retardada 192 muestras, residual < −120 dBFS.
- **Latencia**: impulso a través de cada módulo activo y de todo el rack; latencia total exacta 192 en 44,1/48/96/192 kHz.
- **PDC**: activar, desactivar, reordenar y hacer bypass sin cambio de latencia reportada; null test tras cada operación.
- **Bloques variables**: 0, 1, 64, 257, 2048 muestras; el resultado debe ser idéntico al procesar en bloques de 2048 (salvo módulos con estado dependiente del bloque, que deben documentarlo).
- **Sample rate**: 44,1/48/88,2/96/176,4/192 kHz; sin NaN/Inf, sin coeficientes inestables.
- **Suavizado**: salto de −24 a +24 dB en un bloque; sin discontinuidad >0,5 dB entre muestras.
- **Automatización**: rampa de parámetro sample-accurate desde el host; el resultado no depende del tamaño de bloque.

## 6. Medición y EBU

### 6.1 Loudness (EBU Tech 3341)

| Caso | Señal | Esperado | Tolerancia |
|---|---|---:|---:|
| 1 | Senoide 1 kHz estéreo −23 dBFS | −23,0 LUFS | ±0,1 LU |
| 2 | Senoide 1 kHz estéreo −33 dBFS | −33,0 LUFS | ±0,1 LU |
| 3 | Senoide 1 kHz estéreo −36 dBFS | −36,0 LUFS | ±0,1 LU |
| 4 | Silencio | −70,0 LUFS (suelo) | ±0,1 LU |
| 5 | Ruido rosa calibrado | valor de referencia | ±0,1 LU |
| 6 | Cambio de nivel de 10 dB | integrado estable | ±0,1 LU |

Se comprueban momentary, short-term e integrado. El gating absoluto (−70 LUFS) y relativo (−10 LU) se testean por separado con señales que cruzan el umbral.

### 6.2 LRA (EBU Tech 3342)

- Señales de test de 3342 con LRA nominal 5, 10 y 20 LU.
- Tolerancia ±1 LU.
- Test de señal estacionaria: LRA ≈ 0 LU (±0,5 LU).
- Test de silencio intercalado: no contamina el integrado ni el LRA.

### 6.3 True peak

- Senoides de 997 Hz a distintas fases y niveles cerca de 0 dBFS, 44,1/48/96/192 kHz.
- Tolerancia ±0,2 dB respecto a una implementación de referencia 4×.
- Inter-sample peaks conocidos (p. ej. fase 45° a −0,1 dBFS) deben superar el sample peak.
- El limitador de `masterA` no debe superar `ceiling + 0,1 dB` medido con el mismo detector.

### 6.4 Espectro y vectorscopio

- FFT 4096: bin de 1 kHz correcto ±1 bin; fuga de Hann verificada con dos tonos; promediado estable.
- 31 bandas de tercio de octava: suma monótona; senoide de 1 kHz en la banda central ±1 banda.
- Vectorscopio: correlación +1 (mono), −1 (invertido), 0 (señales ortogonales); sin NaN.
- FIFO llena: se descarta el excedente y `droppedSamples` aumenta; el audio no se bloquea.

## 7. ARIA

| Test | Criterio |
|---|---|
| Captura | 8 s exactos; ring con y sin vuelta; 3 s mínimo; 2,9 s inválido. |
| Congelación | El worker lee el buffer congelado; el audio sigue escribiendo en el otro; no hay copia en audio. |
| Determinismo | Misma instantánea + perfil ⇒ informe byte a byte idéntico en el mismo binario. |
| Loudness | Coincide con `LoudnessMeter` ±0,1 LU (integrado/short-term) y ±1 LU (LRA). |
| True peak | ±0,2 dB respecto al detector de medición. |
| Bandas | 31 bandas; 1 kHz en la banda correcta. |
| Reglas | Frontera por debajo/por encima de cada umbral; golden de parámetros; sin reglas contradictorias. |
| Strength | 0 % no escribe; 50 % interpola continuos; 100 % aplica objetivo; dos aplicaciones = una. |
| Undo | Restaura el `ValueTree` completo, incluidos parámetros ajenos. |
| Referencia | WAV 10 s estable; archivo corrupto ⇒ error de UI, sin crash; no altera audio. |
| Hilos | Worker bloqueado/cancelado no cambia latencia ni audio; sin allocaciones en `pushBlock`. |
| Privacidad | El informe serializado no contiene rutas absolutas ni muestras. |

## 8. Catálogo, estado y temas

### 8.1 Catálogo y APVTS

- `createParameterLayout()` produce exactamente **238** parámetros y **238** IDs únicos.
- Cada `ParamSpec` tiene rango finito, default dentro de rango, unidad y `step` coherente.
- Los IDs siguen `{instancia}.{param}` y son estables; un test fija la lista completa y falla si cambia sin actualizar la versión de estado.
- `rawParam` no devuelve `nullptr` para ningún ID del catálogo.
- Round-trip de estado: guardar/restaurar conserva los 238 valores ±1e-6.
- Estado corrupto: XML inválido, versión desconocida, IDs desconocidos, valores fuera de rango, NaN/Inf. Se conserva el estado anterior y se registra error; nunca se lanza.
- Migración v1→v2 (cuando exista): fixtures de estado antiguo.

### 8.2 Temas

- Los dos JSON de fábrica validan contra `theme.schema.json` (2020-12).
- El parser rechaza: JSON no objeto, `id` ausente/inválido, color sin `#`, color de 3/8 dígitos, token desconocido, propiedad adicional, rango de geometría fuera de límites.
- Fallback por campo: un color inválido usa el valor compilado de Dark Matter y registra error; el resto del tema se conserva.
- `setTheme` con ID desconocido no cambia el tema activo y registra error.
- `getThemeIds()` devuelve exactamente los IDs de fábrica, sin duplicados.
- Cambio de tema: `ChangeBroadcaster` notifica una vez; ningún componente conserva colores del tema anterior.
- Contraste: los pares texto/fondo de `06_THEMES.md` cumplen 4,5:1 (texto normal) y 3:1 (texto grande/controles) según WCAG 2.1 AA.
- Sin degradados: el esquema no tiene tokens de gradiente y el LookAndFeel no usa `ColourGradient`.

## 9. Integración, rack y tiempo real

### 9.1 Rack

- 16 instancias, orden por defecto con `masterA` al final.
- Activar/desactivar/bypass/reordenar: crossfade de 5 ms, sin clicks, sin cambio de latencia.
- `RackCommand` llena la cola: la UI reintenta; el audio no espera.
- `setOrder` con array completo; sin punteros compartidos.
- Reset de módulo y reset global sin allocaciones.
- Editor abierto/cerrado 100 veces bajo carga: el audio no se interrumpe.

### 9.2 Instrumentación RT

`RtProbe` se activa alrededor de `processBlock` en el harness de tests:

```cpp
RtProbe probe;
processor.processBlock (buffer, midi);
REQUIRE (probe.allocations() == 0);
REQUIRE (probe.locks() == 0);
```

| Comprobación | Método | Criterio |
|---|---|---|
| Allocaciones | Interceptar `operator new`/`delete` con flag por hilo | 0 tras `prepareToPlay`. |
| Locks | Revisión + TSan; detector de `CriticalSection` en builds de test | 0 en audio. |
| I/O | `grep` de `File`, `Logger`, `DBG`, `std::cout` en rutas de audio | 0. |
| Hilos | `grep` de `std::thread`, `std::jthread`, `sleep_for` en audio | 0. |
| Excepciones | Compilación `noexcept`; sin `try/catch` en audio | 0. |
| NaN/Inf | Inyección de NaN, Inf, −Inf, denormal en entrada y parámetros | Salida finita; contador > 0. |
| Denormales | Silencio 10 s con reverb/delay a 95 % feedback | CPU no crece >10 % vs señal. |
| FIFO llena | Llenar FIFO y medir bloque | Sin espera; drop contado. |
| Worker atascado | Bloquear worker y procesar 10 s | Latencia y audio intactos. |
| Presupuesto | Medir P50/P95/P99 de `processBlock` | P99 dentro del presupuesto de 02. |

El test de allocaciones debe ejecutarse también con el editor abierto, con ARIA analizando y con el tema cambiando, para cubrir rutas cruzadas.

### 9.3 PDC y latencia

| Test | Criterio |
|---|---|
| Latencia reportada | `getLatencySamples() == 192` en 44,1/48/96/192 kHz. |
| Impulso | Pico en la muestra 192 ±0 en todos los módulos inactivos. |
| Activación | Activar `satA`, `satB`, `masterA` uno a uno: latencia constante. |
| Reordenar | Cambiar el orden 100 veces: latencia constante, sin clicks. |
| Bypass | Bypass de cada módulo: latencia constante, null test tras crossfade. |
| Render offline | El render offline produce la misma latencia y el mismo resultado que tiempo real. |

## 10. Rendimiento y memoria

### 10.1 CPU

| Escenario | Presupuesto |
|---|---:|
| 48 kHz / 256, rack completo | < 25 % de un núcleo |
| 96 kHz / 128, rack completo | < 40 % |
| 192 kHz / 64, rack completo | < 65 % |
| 192 kHz / 2048, rack completo | < 30 % |

- Medición en una máquina de referencia documentada (CPU, RAM, OS, build Release).
- 10 s de audio por escenario; se reportan P50/P95/P99, no solo la media.
- Un escenario que excede el presupuesto bloquea la fase; se optimiza el módulo culpable, no se sube el umbral sin ADR.
- Se mide también el peor caso con ARIA analizando y el editor abierto.

### 10.2 Memoria

- Medición con allocator instrumentado en `prepareToPlay` a 192 kHz / 2048.
- Presupuesto: **< 40 MB** con captura ARIA doble (24,576 MB solo ARIA), delays, reverb, FFT y oversampling.
- Sin fugas: `juce::LeakedObjectDetector` + ASan; 0 fugas al destruir el processor 100 veces.
- El editor no asigna buffers de audio; abrir/cerrar no cambia la memoria del processor.

### 10.3 Arranque

- `prepareToPlay` a 192 kHz / 2048 < 250 ms en la máquina de referencia.
- Escaneo del plugin en el host < 2 s (sin cargar editor).
- Crear/destruir 50 instancias en un proyecto vacío sin fugas ni crecimiento de memoria.

## 11. Compatibilidad

### 11.1 pluginval

Ejecución obligatoria antes de cada release:

```text
pluginval --strictness-level 10 --validate-in-process <ruta al VST3>
```

Cubre: escaneo, parámetros, automatización, estado, canales, sample rates, blocks, editor, PDC, hilos. Cualquier fallo es bloqueante.

### 11.2 Matriz de DAWs

| DAW | Versión mínima | Plataforma | Qué se valida |
|---|---|---|---|
| REAPER | 7.x | Windows | Escaneo, PDC, automatización, render offline, cambio de SR. |
| Cubase | 13/14 | Windows | Escaneo VST3, PDC, automatización, estado, múltiples instancias. |
| Studio One | 6/7 | Windows | PDC, render, cambio de buffer, editor. |
| Ableton Live | 12 | Windows | Automatización, PDC, estado, cambio de SR. |
| Bitwig Studio | 5.x | Windows | PDC, modulación/automatización, múltiples instancias. |
| FL Studio | 21 | Windows | Escaneo, PDC, render, estado. |
| Nuendo | 13/14 | Windows | PDC, render, interoperabilidad con Cubase. |

Por DAW se prueba: insertar, abrir/cerrar editor 20 veces, automatizar 10 parámetros, guardar/cargar proyecto, render offline, cambiar 44,1↔48↔96 kHz, cambiar buffer 64↔2048, 8 instancias simultáneas, quitar el plugin con audio sonando.

La matriz completa se ejecuta en Fase 8 y en cada release candidate; en CI solo se automatiza REAPER si es viable. Los resultados se registran en `Tests/Compatibility/DawMatrix.md`.

## 12. Fuzzing, propiedades y sanitizers

- **Estado**: fuzz de XML/JSON con bytes aleatorios, truncados, profundidad excesiva, IDs gigantes. Nunca crash; error controlado.
- **Temas**: fuzz de JSON y tipos; el parser nunca lanza y siempre conserva un tema válido.
- **Parámetros**: barrido aleatorio de los 238 parámetros con semilla fija; salida finita en 44,1…192 kHz.
- **Audio**: entrada con NaN/Inf/denormales/amplitud 1e6; salida finita y clamp.
- **ASan/UBSan**: suite completa en Linux, build Debug.
- **TSan**: tests de rack, FIFO, workers y captura ARIA.
- **Valgrind** (Linux, opcional): fugas y accesos inválidos en tests lentos.

## 13. CI

| Job | Disparador | Contenido |
|---|---|---|
| `windows-debug` | Cada push/PR | Build MSVC Debug, tests unitarios y de integración. |
| `windows-release` | Cada push/PR | Build Release, tests, pluginval (strictness 10). |
| `linux-asan-ubsan` | Cada push/PR | Tests DSP/AI sin GUI con ASan+UBSan. |
| `linux-tsan` | Nightly | Tests de hilos, rack y ARIA. |
| `bench` | Nightly | Matriz CPU, memoria, arranque; publica CSV. |
| `daw-matrix` | Release candidate | Matriz manual/automatizada de DAWs. |

- Warnings-as-errors en todos los jobs.
- Un test que falla se reproduce con la misma semilla y se adjunta el log.
- No se permite `[skip ci]` en cambios de DSP, estado o temas.
- Los artefactos (logs, CSV, informes pluginval) se conservan 90 días.

## 14. Gates por fase

| Fase | Gate de pruebas |
|---|---|
| 2 — Esqueleto | Compila; 238 parámetros; round-trip de estado; temas cargan; null test. |
| 3 — Medición | EBU 3341/3342; true peak; FIFO; espectro; sin allocaciones en audio. |
| 4 — Rack | PDC constante; comandos; crossfades; worker atascado; TSan. |
| 5 — DSP | Todos los tests de módulo; aliasing; latencia 192; barrido 192 kHz. |
| 6 — ARIA | Determinismo; reglas; Strength; Undo; captura; referencia. |
| 7 — UI | Temas; contraste; editor 100 ciclos; sin allocaciones con editor abierto. |
| 8 — Hardening | pluginval 10; matriz DAW; CPU/memoria; ASan/UBSan/TSan; fuzz. |
| 9 — Release | Suite completa verde; presets; migraciones; checklist A1–A15 firmado. |

### 14.1 Checklist de release

1. Suite completa en Windows Debug y Release.
2. ASan/UBSan y TSan verdes.
3. pluginval strictness 10 sin fallos.
4. Matriz DAW completa sin fallos bloqueantes.
5. CPU y memoria dentro de presupuesto en la máquina de referencia.
6. Auditoría A1–A15 de `02_RT_SAFETY.md` firmada.
7. Goldens de DSP y ARIA actualizados solo con justificación.
8. Sin warnings del compilador.
9. Estado v1 round-trip verificado en todas las DAWs de la matriz.
10. Temas de fábrica validados contra el esquema y contraste WCAG AA.

## 15. Política de tests frágiles y regresiones

- Un test que falla de forma intermitente se marca `[flaky]`, se investiga y se corrige o se elimina en la misma semana. No se ignora.
- Un bug corregido añade un test de regresión con el caso mínimo reproducible.
- Los cambios de golden requieren: motivo, diff numérico y aprobación en revisión.
- No se relajan tolerancias para pasar un test. Si el diseño no puede cumplirlas, se cambia el diseño o se registra un ADR.
- Los tests lentos (`[slow]`) corren en nightly, no en cada PR, pero deben pasar antes de un release candidate.
