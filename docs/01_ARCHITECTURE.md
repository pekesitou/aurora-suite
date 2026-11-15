# 01 — Arquitectura del sistema

## 1. Objetivo y alcance

AURORA ENGINE es **un único plugin VST3 x64** que contiene un rack modular interno. No es una colección de plugins ni un host. El rack permite insertar, duplicar, eliminar, reordenar y hacer bypass de procesadores en tiempo real, con automatización de todos los parámetros audibles y estado/presets serializables.

Fuera de alcance de la versión 1.0:

- Formatos AU/AAX/Standalone (el núcleo se diseña agnóstico para añadirlos sin reescribir DSP).
- Surround/immersivo (el motor de loudness contempla los pesos de canal, pero el rack es estéreo).
- Edición de audio, grabación, host y gestión de proyectos.
- Modelado físico de hardware (los modelos de saturación son curvas de waveshaping documentadas, no emulaciones circuito a circuito).

## 2. Capas

```text
┌───────────────────────────────────────────────────────────────────────┐
│ UI (message thread)                                                   │
│  PluginEditor → Header / RackView / DetailEditorView / ARIAPanelView  │
│  AuroraLookAndFeel ← ThemeManager ← assets/themes/*.json              │
│  Timer 30–60 Hz: lee atomics y drena FIFOs de visualización            │
└───────────────▲───────────────────────────────┬───────────────────────┘
                │ snapshots sin lock            │ APVTS setters / comandos
┌───────────────┴───────────────────────────────▼───────────────────────┐
│ CORE                                                                  │
│  PluginProcessor (AudioProcessor, APVTS, estado, presets)             │
│  RackEngine (orden, bypass, crossfades, latencia, comandos SPSC)      │
└───────────────▲───────────────────────────────┬───────────────────────┘
                │ audio + métricas atómicas     │ parámetros suavizados
┌───────────────┴───────────────────────────────▼───────────────────────┐
│ DSP (audio thread)                                                    │
│  Utility · EQ · Comp · Multiband · DeEsser · Gate · Transient         │
│  Saturator · Imager · Reverb · Delay · MasteringStage                 │
│  ModuleBase: prepare / reset / process / metadata                     │
└───────────────▲───────────────────────────────┬───────────────────────┘
                │ FIFO SPSC                     │ FIFO SPSC
┌───────────────┴───────────────────────────────▼───────────────────────┐
│ ANALYSIS / METERING                                                   │
│  LoudnessMeter (audio thread, barato, atómico)                        │
│  SpectrumAnalyzer worker (FFT, promediado, peak hold)                 │
│  PhaseScope (puntos + correlación)                                    │
│  ARIA worker (features, diagnóstico, referencia)                      │
└───────────────────────────────────────────────────────────────────────┘
```

Regla de dependencias: `UI → Core → DSP/Metering/AI`. Ningún módulo DSP incluye cabeceras de UI. `ThemeManager` no es visible para el audio thread.

## 3. Modelo de hilos

| Hilo | Origen | Responsabilidad | Prohibido |
|---|---|---|---|
| Audio | Host DAW | `processBlock`, DSP, loudness, escritura en FIFOs | alloc, lock, I/O, excepciones, hilos, logging |
| Message/UI | JUCE | Editor, APVTS, presets, aplicar ARIA, cargar temas | tocar buffers de audio sin FIFO |
| Metering worker | `std::jthread` propio | FFT del espectro, promediado, peak hold | bloquear al audio; escribir parámetros |
| ARIA worker | `std::jthread` propio | Análisis de instantánea, referencia, informe | tocar el buffer de audio del host |
| Host auxiliar | DAW | Automatización, estado | — |

No se usa un thread pool compartido. El worker de metering y el de ARIA son independientes para que un análisis largo de ARIA no congele el espectro. Ambos se crean una sola vez (constructor de `PluginProcessor` o primer `prepareToPlay` desde el hilo de mensajes) y se detienen en el destructor con `request_stop()` + join.

### 3.1 Apagado seguro

1. `request_stop()` a ARIA, luego a metering.
2. `notify_all` de sus condiciones.
3. Join.
4. Solo entonces se liberan buffers y objetos de análisis.

El hilo de audio nunca ve objetos destruidos: los workers solo leen copias/FIFOs; el procesador espera a que terminen antes de destruir el estado compartido.

## 4. Catálogo de instancias (rack de capacidad fija)

El rack **no crea ni destruye objetos en tiempo real**. Se preasignan 16 instancias tipadas en el constructor; insertar = activar una instancia libre, eliminar = desactivar. Así los IDs de parámetros son estables, legibles y automatizables, y no existe asignación dinámica al editar el rack.

| # | Instance ID | Tipo | Capacidad | Prefijo APVTS | Latencia máx. (muestras base) |
|---:|---|---|---:|---|---:|
| 1 | `utilA` | Utility (gain/pan/polaridad/mono) | 2 | `utilA.` | 0 |
| 2 | `utilB` | Utility | — | `utilB.` | 0 |
| 3 | `eqA` | Parametric EQ 8 bandas | 2 | `eqA.` | 0 |
| 4 | `eqB` | Parametric EQ | — | `eqB.` | 0 |
| 5 | `compA` | Compressor | 2 | `compA.` | 0 |
| 6 | `compB` | Compressor | — | `compB.` | 0 |
| 7 | `mbA` | Multiband compressor 4 bandas | 1 | `mbA.` | 0 |
| 8 | `dsA` | De-Esser | 1 | `dsA.` | 0 |
| 9 | `gateA` | Gate / Expander | 1 | `gateA.` | 0 |
| 10 | `transA` | Transient Shaper | 1 | `transA.` | 0 |
| 11 | `satA` | Saturator | 2 | `satA.` | 32 (8× FIR, provisional) |
| 12 | `satB` | Saturator | — | `satB.` | 32 |
| 13 | `imgA` | Stereo Imager M/S | 1 | `imgA.` | 0 |
| 14 | `revA` | Reverb FDN | 1 | `revA.` | 0 |
| 15 | `dlyA` | Delay estéreo | 1 | `dlyA.` | 0 |
| 16 | `masterA` | Clipper + True Peak Limiter | 1 | `masterA.` | 128 (4× + lookahead, provisional) |

**Latencia total del plugin:** suma de la latencia real de las 16 instancias tras `prepareToPlay`, **independientemente de las que estén activas**. Provisional: `2 × 32 + 128 = 192` muestras (4,35 ms a 44,1 kHz). La cifra exacta se mide en Fase 5/6 y se fija en el ADR-0002 antes de release. No cambia al insertar, quitar, reordenar o hacer bypass.

### 4.1 Por qué instancias tipadas y no slots genéricos

Un VST3 no permite renombrar parámetros de forma fiable en caliente. Un banco genérico `slot3.param07` rompe la experiencia de automatización. La solución comercial es un pool tipado con capacidad fija: los IDs son semánticos (`eqA.b3.freq`), el host muestra nombres útiles y el usuario puede duplicar hasta la capacidad. Al agotarse la capacidad, la UI lo indica y no crea una instancia fantasma. Ver ADR-0001.

## 5. Flujo de señal

```text
Input L/R
  → input trim (smoothed)
  → sanitize (NaN/Inf → 0, denormal guard)
  → para cada instancia en rack.order:
        [inactiva]  → línea de retardo de su latencia (transparente)
        [priming]   → procesa en silencio durante L muestras
        [crossfade] → mezcla 5 ms entre línea seca alineada y salida del módulo
        [activa]    → Module::process (dry/wet interno si aplica, alineado)
  → output trim (smoothed)
  → safety clamp final (±16.0, NaN/Inf → 0)
  → tap de medición: loudness, true peak, espectro, vectorscopio
  → salida al host
```

Los medidores son **tap** posteriores al audio: nunca modifican la señal. El único proceso que garantiza techo es `masterA` cuando está activo; el safety clamp final es una protección numérica, no un limitador de calidad.

## 6. Invariantes de diseño

1. **INV-01:** `processBlock` y `ModuleBase::process` son `noexcept` y libres de asignaciones/locks.
2. **INV-02:** todo buffer de audio, delay, tabla FIR y FFT se asigna en `prepareToPlay` (o constructor), nunca en `process`.
3. **INV-03:** un parámetro del host se lee como `std::atomic<float>` y se suaviza antes de afectar al audio.
4. **INV-04:** la latencia reportada al host es constante durante toda la sesión, aunque cambie el rack.
5. **INV-05:** el bypass y las transiciones estructurales usan crossfade de 5 ms como mínimo.
6. **INV-06:** ningún worker escribe directamente en buffers de audio del host.
7. **INV-07:** el estado se serializa con versión y validación; IDs desconocidos se ignoran con aviso, no rompen el preset.
8. **INV-08:** ningún sample no finito sale del plugin.
9. **INV-09:** el audio thread no espera a la UI ni a ARIA; si una FIFO está llena, se descarta el dato de visualización y se cuenta el drop.
10. **INV-10:** el editor puede cerrarse/abrirse sin afectar al procesamiento.

## 7. RackEngine

### 7.1 Responsabilidades

- Mantener el orden activo de las 16 instancias y su estado (inactiva, priming, crossfade-in, activa, crossfade-out).
- Aplicar comandos de la UI mediante una cola SPSC de tamaño fijo (`RackCommand`: tipo, instanceId, valor).
- Gestionar las líneas de retardo de latencia por instancia.
- Aplicar fade-out/fade-in de 5 ms al reordenar; crossfade paralelo al activar/desactivar/bypass.
- Reportar latencia total y estado por instancia a la UI mediante atomics.
- Ejecutar `reset()` en los módulos cuando el host lo pida (sin alloc).

### 7.2 Comandos

```cpp
enum class RackCommandType : uint8_t {
    setOrder, activate, deactivate, bypass, resetModule, resetAll
};

struct RackCommand {
    RackCommandType type;
    ModuleId id;
    bool flag;
    std::array<ModuleId, kMaxModules> order;
};
```

La cola es SPSC: UI/message thread escribe, audio thread drena al inicio del bloque. `setOrder` incluye el array completo para no compartir punteros. Capacidad 64 comandos; si se llena, la UI reintenta en el siguiente timer (nunca se bloquea).

### 7.3 Máquina de estados de una instancia

```text
INACTIVE ──activate──▶ PRIMING ──L muestras──▶ FADE_IN (5 ms) ──▶ ACTIVE
   ▲                                                                  │
   └──────────── INACTIVE ◀── FADE_OUT (5 ms) ◀── deactivate/bypass ──┘
```

- **PRIMING:** el módulo procesa la señal actual pero su salida está muteada; la ruta audible sigue siendo la línea de retardo seca. Dura exactamente la latencia del módulo para que su estado interno (FIR, compresor) esté alineado.
- **FADE_IN/OUT:** crossfade lineal de 5 ms entre la línea seca retardada y la salida del módulo. Ambos caminos están alineados en el tiempo.
- **Bypass:** es una desactivación con crossfade; al reactivar se vuelve a hacer priming. No se conserva estado obsoleto indefinidamente.
- **Reordenar:** fade-out global de 5 ms → swap del array de orden → fade-in global de 5 ms. No se procesan dos cadenas a la vez (evita picos de CPU).

### 7.4 Alineación de latencia

Cada instancia tiene una `LatencyLine` preasignada de `L_i` muestras. Si el módulo está activo, su propia latencia (FIR/lookahead) ya retrasa la señal; la línea solo se usa como ruta seca durante bypass/priming. Si está inactivo, toda la señal pasa por la línea. Así la suma de retardos del rack es siempre `Σ L_i` y el PDC no cambia al editar.

Los módulos con mezcla dry/wet interna alinean su rama seca con la húmeda (retardo de su propia latencia) para evitar comb filtering.

## 8. Parámetros y automatización

- APVTS es la única fuente de verdad de parámetros.
- Todos los parámetros audibles tienen ID estable, rango, default, unidad y formato.
- Los parámetros no audibles (tema, orden del rack, modo ARIA, escala UI) se guardan como propiedades del `ValueTree`, no como parámetros del host.
- `getRawParameterValue(id)` se cachea una vez; el audio thread solo hace `load(std::memory_order_relaxed)`.
- La UI usa `SliderAttachment`/`ButtonAttachment` o `ParameterAttachment`; nunca escribe el DSP directamente.
- Al cargar un preset, los valores se aplican por APVTS (message thread) y el rack recibe el orden por comando. El suavizado evita clicks.
- La automatización de parámetros de un módulo inactivo es válida y se conserva; al activarlo se aplica su último valor.

## 9. Estado y presets

```xml
<AURORA stateVersion="1" pluginVersion="0.1.0">
  <PARAMS .../>                       <!-- APVTS -->
  <PROPERTIES rackOrder="utilA,eqA,compA,..."
              uiThemeId="dark-matter-flat"
              uiScale="1.0"
              ariaProfileId="streaming"
              ariaMode="assisted"/>
</AURORA>
```

- `getStateInformation` copia el `ValueTree` y lo serializa a XML binario comprimido (formato JUCE `copyXmlToBinary`). Tamaño típico &lt; 64 KB.
- `setStateInformation` valida `stateVersion`, tamaños y IDs; si falla, mantiene el estado actual y registra un error en message thread. Nunca lanza.
- Migraciones: `StateMigrations` con funciones puras por versión. La v1 es la base.
- Presets de fábrica: XML embebido en `BinaryData`, con categorías (Init, Mixing, Mastering, ARIA profiles) en Fase 9.
- Undo/redo de ARIA: snapshot del `ValueTree` antes de aplicar; el botón Undo restaura el snapshot en message thread.

## 10. Rutas de análisis

### 10.1 Loudness (audio thread)

`LoudnessMeter::process` aplica K-weighting (dos biquads por canal), acumula energía por bloques de 100 ms y calcula:

- Momentary (400 ms), Short-Term (3 s), Integrated (gating 400 ms/75 %), LRA (gating 3 s/1 s).
- True Peak con oversampling 4× y Sample Peak.
- RMS y correlación.

Los resultados se publican en `std::atomic<float>`; la UI los lee a 30 Hz. El cálculo es O(n) y no asigna.

### 10.2 Espectro (worker)

El audio thread hace downmix mono y escribe en una FIFO SPSC de 8192 muestras. El worker:

1. Extrae ventanas de 4096 con 75 % de solape.
2. Aplica Hann o Blackman.
3. FFT real → magnitud → dB.
4. Promediado configurable y peak hold con caída.
5. Publica 2048 bins en un triple buffer lock-free.

La UI dibuja la última trama publicada; si no hay nueva, reutiliza. El EQ usa el mismo analizador para su RTA.

### 10.3 Vectorscopio

El audio thread escribe pares L/R decimados en una FIFO SPSC (1024 puntos) y actualiza la correlación con medias exponenciales. La UI dibuja Lissajous y la barra de correlación. No hay FFT en el audio thread.

### 10.4 ARIA (worker)

El audio thread escribe en un ring buffer de captura de 8 s (estéreo, asignado en `prepareToPlay`) solo cuando la captura está armada. Al pulsar **Analyze**, el worker drena la FIFO, calcula features y publica un `ARIAReport` POD. Ver `07_ARIA.md`.

## 11. UI y temas

- `PluginEditor` es un contenedor con layout proporcional y escala uniforme; tamaño por defecto 1280×800, mínimo 1024×680, máximo 1920×1200.
- Zonas: header (48–56 px), rack (altura fija ~96 px + scroll horizontal), detalle (flexible), columna de medición (280 px), panel ARIA (drawer inferior de 220 px).
- Todo el dibujo es vectorial: rectángulos planos, líneas de 1 px, arcos y texto. Sin imágenes rasterizadas de chasis, sin degradados de fondo, sin sombras.
- `ThemeManager` carga JSON embebido, valida contra esquema y publica un `ChangeBroadcaster`. Los componentes consultan tokens semánticos, no colores hardcodeados.
- Los knobs son componentes custom (`AuroraKnob`): cuerpo plano, anillo de posición de 2 px, puntero de 2 px, marcas; interacción drag vertical, Shift = fino, doble clic = default, rueda = paso.
- Tipografía: Inter (UI) y JetBrains Mono (valores tabulares). Se cargan desde `BinaryData` con `juce::Typeface::createSystemTypefaceFor`; fallback al sistema si faltan.

## 12. Build y targets (Fase 2)

```text
AuroraEngine        → VST3 x64 (primario), AU/Standalone opcionales por flag
AuroraDSPTests      → consola Catch2, sin GUI
AuroraAssets        → juce_add_binary_data (temas, fuentes, presets, perfiles)
```

- CMake ≥ 3.25, C++20, `juce_add_plugin`, `COPY_PLUGIN_AFTER_BUILD` off por defecto.
- Warnings-as-errors en CI; `juce_recommended_warning_flags` + flags extra.
- `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, `JUCE_MODAL_LOOPS_PERMITTED=0`.
- Tests con Catch2 v3 vía FetchContent.
- CI Windows (MSVC) obligatorio; Linux/macOS para sanitizers y tests DSP sin GUI.

## 13. Modos de fallo y respuesta

| Fallo | Respuesta |
|---|---|
| Parámetro NaN/Inf desde automatización | sanitize a default/último valor válido; flag atómico para UI |
| Audio de entrada NaN/Inf | reemplazo por 0 y contador; el bloque continúa |
| FIFO de visualización llena | descartar dato, incrementar `droppedFrames`; nunca bloquear |
| Cola de comandos llena | UI reintenta; el audio no espera |
| Tema JSON corrupto | fallback compilado + mensaje en UI |
| Preset de versión desconocida | rechazar carga, conservar estado, mostrar error |
| Sample rate extremo | `prepareToPlay` reasigna; si falla, silencio seguro |
| Referencia de ARIA ilegible | informe de error en UI; no afecta al audio |
| Worker de ARIA atascado | cancelación por generación; el audio sigue |
| Latencia real > presupuesto | falla el test de Fase 5/6; se ajusta el diseño, no se oculta |

## 14. Decisiones registradas

- ADR-0001: pool tipado de instancias con capacidad fija.
- ADR-0002: latencia invariante con líneas de retardo por instancia.
- ADR-0003: medidores caros fuera del audio thread.
- ADR-0004: ARIA determinista y explicable, sin escritura desde el audio thread.
- ADR-0005: temas JSON embebidos con fallback compilado.
