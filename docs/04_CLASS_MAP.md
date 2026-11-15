# 04 — Mapa de clases y archivos

## 1. Regla de dependencias

```text
UI  →  Core  →  DSP / Metering / AI
```

- `Source/DSP` no incluye cabeceras de `Source/UI` ni de `Source/AI`.
- `Source/Core/ThemeManager.*` no es visible para `RackEngine` ni para ningún `ModuleBase`.
- `Source/AI` puede incluir `DSP/DspUtils.h` y `Core/ModuleCatalog.h`; nunca incluye UI.
- `Source/Metering` no depende de `Core` salvo `ModuleCatalog` si necesita IDs de medidor; por diseño no los necesita.
- Solo `PluginProcessor` conoce a la vez APVTS, RackEngine, meter workers, ARIA y ThemeManager. Es el compositor.

## 2. Inventario de archivos

| Archivo | Estado | Responsabilidad |
|---|---|---|
| `CMakeLists.txt` | Fase 2 | Targets `AuroraEngine`, `AuroraAssets`, `AuroraDSPTests`. |
| `Source/Core/ModuleCatalog.h/.cpp` | **Existente** | Catálogo único de módulos y parámetros; genera APVTS. |
| `Source/Core/ThemeManager.h/.cpp` | **Existente** | Carga y posesión de temas planos; `ChangeBroadcaster`. |
| `Source/Core/PluginProcessor.h/.cpp` | Fase 2 | `juce::AudioProcessor`, APVTS, estado, workers, editor factory. |
| `Source/Core/PluginEditor.h/.cpp` | Fase 2/7 | Editor raíz, layout, timer de UI, listener de tema. |
| `Source/Core/RackEngine.h/.cpp` | Fase 2/4 | Cadena de 16 instancias, estados, latencia, comandos SPSC. |
| `Source/Core/LockFreeUtils.h` | Fase 2 | `SpscQueue<T,N>`, `TripleBuffer<T>`, utilidades de publicación. |
| `Source/Core/StateMigrations.h/.cpp` | Fase 2 | Migración pura de `ValueTree` por versión. |
| `Source/DSP/ModuleBase.h` | **Existente** | Interfaz de todos los procesadores. |
| `Source/DSP/DspUtils.h` | **Existente** | Biquads, one-pole, DC blocker, delay line, envelope, gain computer. |
| `Source/DSP/Utility.h/.cpp` | **Existente** | Módulo Utility. |
| `Source/DSP/ParametricEQ.h/.cpp` | **Existente** | EQ de 8 bandas. |
| `Source/DSP/Compressor.h/.cpp` | Fase 3 | Compresor peak/RMS. |
| `Source/DSP/MultibandComp.h/.cpp` | Fase 5 | Multibanda LR4. |
| `Source/DSP/DeEsser.h/.cpp` | Fase 5 | De-Esser. |
| `Source/DSP/Gate.h/.cpp` | Fase 5 | Gate/expander. |
| `Source/DSP/TransientShaper.h/.cpp` | Fase 5 | Transient shaper. |
| `Source/DSP/Saturator.h/.cpp` | Fase 5 | Waveshaping 8×. |
| `Source/DSP/Imager.h/.cpp` | Fase 5 | Imager M/S. |
| `Source/DSP/Reverb.h/.cpp` | Fase 5 | FDN 8 líneas. |
| `Source/DSP/StereoDelay.h/.cpp` | Fase 5 | Delay estéreo. |
| `Source/DSP/MasteringStage.h/.cpp` | Fase 3/5 | Clipper + true peak limiter. |
| `Source/DSP/ModuleFactory.h/.cpp` | Fase 2 | `std::unique_ptr<ModuleBase> makeModule(ModuleId, APVTS&)`. |
| `Source/Metering/LoudnessMeter.h/.cpp` | Fase 3 | K-weighting, LUFS, true peak, RMS. |
| `Source/Metering/SpectrumAnalyzer.h/.cpp` | Fase 3 | FFT 4096 en worker, triple buffer. |
| `Source/Metering/PhaseScope.h/.cpp` | Fase 5 | Vectorscopio y correlación. |
| `Source/AI/ARIAAnalyzer.h/.cpp` | Fase 6 | Captura 8 s, features deterministas. |
| `Source/AI/ARIAEngine.h/.cpp` | Fase 6 | Reglas, sugerencias, Strength. |
| `Source/AI/ARIAReference.h/.cpp` | Fase 6 | Referencia externa y comparación. |
| `Source/UI/AuroraLookAndFeel.h/.cpp` | Fase 7 | LookAndFeel vectorial basado en `Theme`. |
| `Source/UI/Components/AuroraKnob.h/.cpp` | Fase 7 | Knob plano con anillo y puntero. |
| `Source/UI/Components/LevelMeter.h/.cpp` | Fase 7 | Medidor segmentado. |
| `Source/UI/Components/ThemeSelector.h/.cpp` | Fase 7 | Selector de temas. |
| `Source/UI/RackView.h/.cpp` | Fase 7 | Slots, drag de orden, bypass. |
| `Source/UI/DetailEditorView.h/.cpp` | Fase 7 | Editor del módulo seleccionado. |
| `Source/UI/ARIAPanelView.h/.cpp` | Fase 7 | Analyze, informe, Strength, Apply/Undo. |
| `Tests/CMakeLists.txt` | Fase 2 | Catch2 v3. |
| `Tests/TestMain.cpp` | Fase 2 | `CATCH_CONFIG_MAIN`. |
| `Tests/Core/*.cpp` | Fases 2–6 | Catálogo, estado, rack, temas. |
| `Tests/DSP/*.cpp` | Fases 3–5 | Pruebas numéricas por módulo. |
| `Tests/Metering/*.cpp` | Fases 3–6 | EBU 3341/3342, true peak, FFT. |
| `Tests/AI/*.cpp` | Fase 6 | Determinismo y reglas de ARIA. |
| `assets/themes/*.json` | **Existente** | Dos temas de fábrica + esquema. |
| `assets/fonts/*.ttf` | **Existente** | Inter y JetBrains Mono (OFL). |

## 3. `ModuleBase` (existente)

```cpp
class ModuleBase
{
public:
    virtual ~ModuleBase() = default;

    virtual void prepare (double newSampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void process (juce::AudioBuffer<float>& buffer) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void setTempo (double bpm, bool isPlaying) noexcept;
    [[nodiscard]] virtual int latencySamples() const noexcept = 0;
    [[nodiscard]] virtual float gainReductionDb() const noexcept { return 0.0f; }
    [[nodiscard]] virtual float outputLevelDb() const noexcept { return -120.0f; }

    [[nodiscard]] ModuleId moduleId() const noexcept;
    [[nodiscard]] ModuleType moduleType() const noexcept;

protected:
    explicit ModuleBase (ModuleId moduleId);
    ModuleId id;
    ModuleType type;
};
```

Contratos:

| Método | Hilo | Puede asignar | Puede bloquear | Obligatorio |
|---|---|---|---|---|
| `prepare` | message/host | Sí | Sí | Sí |
| `process` | audio | **No** | **No** | Sí, `noexcept` |
| `reset` | audio o message | **No** | **No** | Sí, `noexcept` |
| `setTempo` | audio | **No** | **No** | No |
| `latencySamples` | cualquiera | No | No | Sí, constante tras `prepare` |

`process` recibe el buffer completo del bloque (`0…kMaxBlockSize`). El módulo puede asumir `numChannels ∈ {1,2}`. No debe cambiar el tamaño del buffer ni el número de canales.

Las métricas (`gainReductionDb`, `outputLevelDb`) son `std::atomic<float>` leídos por UI; se actualizan una vez por bloque con `store(relaxed)`.

## 4. `RackEngine`

```cpp
enum class InstanceState : std::uint8_t
{
    inactive = 0, priming, fadeIn, active, fadeOut
};

struct InstanceStatus
{
    ModuleId id;
    InstanceState state;
    float gainReductionDb;
    float outputLevelDb;
    bool prepared;
};

enum class RackCommandType : std::uint8_t
{
    setOrder, activate, deactivate, bypass, resetModule, resetAll
};

struct RackCommand
{
    RackCommandType type;
    ModuleId id;
    bool flag;
    std::array<ModuleId, kNumModules> order;
};

class RackEngine
{
public:
    explicit RackEngine (juce::AudioProcessorValueTreeState& state);
    ~RackEngine();

    RackEngine (const RackEngine&) = delete;
    RackEngine& operator= (const RackEngine&) = delete;

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void releaseResources();

    void process (juce::AudioBuffer<float>& buffer) noexcept;
    void resetAll() noexcept;

    // Message thread. Devuelve false si la cola está llena (la UI reintenta).
    [[nodiscard]] bool submit (const RackCommand& command) noexcept;

    [[nodiscard]] std::array<ModuleId, kNumModules> currentOrder() const noexcept;
    [[nodiscard]] int totalLatencySamples() const noexcept;
    [[nodiscard]] InstanceStatus instanceStatus (ModuleId id) const noexcept;
    [[nodiscard]] std::uint32_t droppedCommands() const noexcept;
    [[nodiscard]] std::uint32_t nonFiniteEvents() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
```

Responsabilidades y contratos:

- Construye las 16 instancias con `makeModule(id, state)` una sola vez; no hay `new` en audio.
- `prepare` reserva las `LatencyLine` de cada instancia y llama a `prepare` de todos los módulos, activos o no.
- `process` drena hasta 64 comandos, aplica crossfades de 5 ms, ejecuta los módulos activos en orden y escribe en las líneas de retardo de los inactivos.
- `totalLatencySamples()` es la suma de `latencySamples()` de las 16 instancias, **constante** durante la sesión.
- La cola de comandos es SPSC (`SpscQueue<RackCommand, 64>`); `submit` nunca bloquea.
- `InstanceStatus` se publica en un array de atomics; la UI lo lee a 30 Hz.
- `bypass` se implementa como `deactivate` + estado de bypass recordado; al reactivar se hace priming.
- `resetModule` resetea estados internos sin asignar.
- El motor no conoce el tema ni el editor.

## 5. `PluginProcessor`

```cpp
class AuroraProcessor final : public juce::AudioProcessor
{
public:
    AuroraProcessor();
    ~AuroraProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() noexcept;
    ThemeManager& themeManager() noexcept;          // message thread
    ARIAController& ariaController() noexcept;      // message thread + worker
    MeteringHub& metering() noexcept;

private:
    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<ThemeManager> themes;
    std::unique_ptr<RackEngine> rack;
    std::unique_ptr<MeteringHub> meters;
    std::unique_ptr<ARIAController> aria;

    std::jthread meteringWorker;
    std::jthread ariaWorker;
    std::atomic<bool> prepared { false };
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
};
```

Contratos:

- `processBlock`: `noexcept`, `ScopedNoDenormals`, sanitize de entrada, `inputTrim`, rack, `outputTrim`, clamp final, tap de medición.
- No toca `ThemeManager`, no crea `String`, no notifica a la UI.
- `prepareToPlay`: dimensiona rack y medidores, arranca workers si no lo están, publica `sampleRate`/`blockSize` en atomics.
- `getStateInformation`: copia el `ValueTree` en message thread y serializa con `copyXmlToBinary`.
- `setStateInformation`: valida versión, aplica `StateMigrations`, restaura por APVTS; nunca lanza.
- `createEditor` devuelve `new AuroraEditor(*this)`; el editor no es dueño de nada del processor salvo referencias.
- El destructor para workers antes de liberar `meters`/`aria`.

## 6. `PluginEditor`

```cpp
class AuroraEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           private juce::ChangeListener
{
public:
    explicit AuroraEditor (AuroraProcessor&);
    ~AuroraEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;                 // 30–60 Hz
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    AuroraProcessor& processor;
    AuroraLookAndFeel lookAndFeel;
    std::unique_ptr<HeaderBar> header;
    std::unique_ptr<RackView> rackView;
    std::unique_ptr<DetailEditorView> detailView;
    std::unique_ptr<MeteringColumn> meters;
    std::unique_ptr<ARIAPanelView> ariaPanel;
    int selectedModuleIndex = 0;
};
```

- Escucha `ThemeManager` y reconstruye estilos al recibir el cambio; no aplica el tema al audio.
- Timer de UI: drena snapshots de medidores y actualiza valores; nunca bloquea al audio.
- Tamaño 1280×800 por defecto, mínimo 1024×680, máximo 1920×1200, con escala uniforme.
- El editor se destruye sin afectar al procesamiento (INV-10).

## 7. `ThemeManager` (existente)

```cpp
struct Theme
{
    juce::String id, name;
    int version = 1;
    juce::Colour background, surface, surfaceAlt, border, borderStrong;
    juce::Colour textPrimary, textSecondary, textDisabled;
    juce::Colour accent, accentSecondary, warning, danger, success;
    juce::Colour meterLow, meterMid, meterHigh, meterBackground;
    juce::Colour grid, knobBody, knobRing, knobPrimary, knobSecondary, selection;
    int borderWidth = 1, cornerRadius = 2, slotHeight = 96, headerHeight = 52;
    int knobSize = 44, meterSegments = 24;
    juce::String uiFamily = "Inter", monoFamily = "JetBrains Mono";
    float fontSizeSmall = 10.0f, fontSizeNormal = 12.0f, fontSizeLarge = 15.0f, fontSizeMono = 11.0f;
};

class ThemeManager final : public juce::ChangeBroadcaster
{
public:
    static constexpr const char* darkMatterId = "dark-matter-flat";
    static constexpr const char* industrialSteelId = "industrial-steel-flat";

    ThemeManager();
    ~ThemeManager() override = default;

    [[nodiscard]] const Theme& getActiveTheme() const noexcept;
    [[nodiscard]] juce::String getActiveThemeId() const;
    [[nodiscard]] juce::StringArray getThemeIds() const;
    [[nodiscard]] const Theme* getTheme (const juce::String& id) const noexcept;
    [[nodiscard]] bool hasTheme (const juce::String& id) const noexcept;
    bool setTheme (const juce::String& id);
    [[nodiscard]] bool isUsingEmbeddedAssets() const noexcept;
    [[nodiscard]] const juce::StringArray& getLoadErrors() const noexcept;
    [[nodiscard]] juce::String getLoadErrorsAsText() const;
};
```

- Constructor de message thread; carga `dark-matter.json` e `industrial-steel.json` desde `AuroraAssets` (`BinaryData.h`).
- `getActiveTheme()` devuelve una referencia estable: el vector de temas no se modifica después del constructor.
- `setTheme` valida el id, actualiza `activeIndex` y llama a `sendChangeMessage()` solo si cambió.
- Fallback compilado de Dark Matter si el JSON falla; fallback por campo para valores inválidos.
- Sin locks; solo message thread.

## 8. Metering

```cpp
struct LoudnessSnapshot
{
    float momentaryLufs = -70.0f;
    float shortTermLufs = -70.0f;
    float integratedLufs = -70.0f;
    float lra = 0.0f;
    float truePeakDb = -120.0f;
    float samplePeakDb = -120.0f;
    float rmsDb = -120.0f;
    std::uint32_t validBlocks = 0;
};

class LoudnessMeter
{
public:
    void prepare (double sampleRate, int numChannels);
    void process (const juce::AudioBuffer<float>& buffer) noexcept;
    void reset() noexcept;
    [[nodiscard]] LoudnessSnapshot snapshot() const noexcept;

private:
    std::array<KWeighting, 2> filters;
    std::vector<double> blockEnergy;   // reservado en prepare, tamaño fijo por canción
    // ...
};

class SpectrumAnalyzer
{
public:
    void prepare (double sampleRate, int fftSize = 4096, int hopSize = 1024);
    void start();
    void stop();
    void pushBlock (const juce::AudioBuffer<float>& buffer) noexcept; // audio thread
    bool readLatest (std::span<float> destinationDb) noexcept;         // UI
};

class PhaseScope
{
public:
    void prepare (double sampleRate, int decimation = 4);
    void pushBlock (const juce::AudioBuffer<float>& buffer) noexcept;  // audio thread
    bool readLatest (std::span<float> xy) noexcept;                    // UI
    [[nodiscard]] float correlation() const noexcept;
};
```

- `LoudnessMeter` corre en el audio thread; coste O(n), sin allocaciones. `blockEnergy` se reserva en `prepare` con capacidad para 10 minutos; si se llena, se descarta el bloque más antiguo o se detiene la integración (decisión de Fase 3, documentada).
- `SpectrumAnalyzer` recibe audio por FIFO, hace FFT en su `jthread` y publica 2048 bins en `TripleBuffer`.
- `PhaseScope` escribe puntos decimados en FIFO; el worker no es necesario para la correlación (se actualiza con exponenciales en audio), pero el dibujo se hace en UI.
- `MeteringHub` agrupa las tres instancias y expone `snapshot()` a la UI.

## 9. ARIA

```cpp
struct ARIAFeatures
{
    float integratedLufs = -70.0f;
    float shortTermLufs = -70.0f;
    float lufsRange = 0.0f;
    float truePeakDb = -120.0f;
    float crestFactorDb = 0.0f;
    float spectralCentroidHz = 0.0f;
    float spectralTiltDbPerOct = 0.0f;
    float flatness = 0.0f;
    float sibilanceRatioDb = 0.0f;
    float lowMidExcessDb = 0.0f;
    float correlation = 0.0f;
    float sideToMidDb = -120.0f;
    float transientDensityPerSec = 0.0f;
    std::array<float, 24> bandEnergyDb {};
};

struct ARIAReport
{
    static constexpr int reportVersion = 1;
    ARIAFeatures features;
    std::array<ARIASuggestion, 16> suggestions {};
    int numSuggestions = 0;
    std::array<ARIADiagnostic, 8> diagnostics {};
    int numDiagnostics = 0;
    std::uint32_t generation = 0;
    double sampleRate = 0.0;
    bool valid = false;
};

class ARIAAnalyzer
{
public:
    void prepare (double sampleRate, int numChannels, double captureSeconds = 8.0);
    void arm() noexcept;                              // message thread
    void freeze() noexcept;                           // message thread, swap de buffer
    void pushBlock (const juce::AudioBuffer<float>&) noexcept; // audio thread
    [[nodiscard]] std::uint32_t frozenGeneration() const noexcept;
    [[nodiscard]] bool hasFrozenCapture() const noexcept;
};

class ARIAEngine
{
public:
    [[nodiscard]] ARIAReport analyze (const float* const* channels,
                                      int numChannels, int numSamples,
                                      double sampleRate,
                                      const ARIAProfile& profile,
                                      std::stop_token token) const noexcept;

    [[nodiscard]] std::array<ARIAAppliedChange, 16>
        materialize (const ARIAReport& report, float strengthPercent) const noexcept;
};

class ARIAReference
{
public:
    bool loadFile (const juce::File&, juce::AudioFormatManager&); // message thread
    void clear();
    [[nodiscard]] bool isLoaded() const noexcept;
    [[nodiscard]] const ARIAFeatures& features() const noexcept;
};

class ARIAController
{
public:
    void requestAnalysis() noexcept;          // message thread -> worker
    void cancel() noexcept;
    bool readReport (ARIAReport& destination) noexcept; // message thread
    void applySuggestions (float strengthPercent);      // message thread, APVTS + undo
    void undoLastApply();                              // message thread
};
```

- `ARIAAnalyzer` usa doble buffer de captura de 8 s; el audio escribe en el activo y `freeze()` intercambia índices atómicamente sin copiar. El worker lee el buffer congelado.
- `ARIAEngine::analyze` es pura y determinista: mismas muestras + mismo perfil ⇒ mismo informe.
- `materialize` no escribe parámetros; devuelve una lista de cambios (`ParamID`, valor, motivo, regla). `ARIAController::applySuggestions` los aplica en message thread vía APVTS, con snapshot para undo.
- `ARIAReference` decodifica el archivo en message thread/worker de referencia; nunca en audio.
- Ningún objeto de ARIA se consulta desde `processBlock`, salvo `ARIAnalyzer::pushBlock`.

## 10. UI

| Clase | Archivo | Contrato |
|---|---|---|
| `AuroraLookAndFeel` | `UI/AuroraLookAndFeel.*` | `setTheme(const Theme&)`; dibuja knobs, sliders, botones, combos, tabs sin gradientes. |
| `AuroraKnob` | `UI/Components/AuroraKnob.*` | `Slider` + drag vertical, Shift fino, doble clic default, rueda paso, tooltip con unidad. |
| `LevelMeter` | `UI/Components/LevelMeter.*` | Segmentos según `Theme::meterSegments`; colores low/mid/high; peak hold 1,5 s. |
| `ThemeSelector` | `UI/Components/ThemeSelector.*` | Combo con `getThemeIds()`; aplica `setTheme` y persiste `uiThemeId`. |
| `RackView` | `UI/RackView.*` | Slots de `slotHeight`, drag para reordenar, bypass, selección; envía `RackCommand`. |
| `DetailEditorView` | `UI/DetailEditorView.*` | Parámetros del módulo seleccionado con attachments; sin lógica DSP. |
| `ARIAPanelView` | `UI/ARIAPanelView.*` | Analyze/Cancel, informe, Strength, Apply/Undo, perfil, referencia. |
| `MeteringColumn` | `UI/MeteringColumn.*` | LUFS M/S/I, LRA, TP, espectro, vectorscopio; lee snapshots. |

Todos los componentes leen colores de `Theme`, nunca literales. Las fuentes se cargan de `BinaryData` con `juce::Typeface::createSystemTypefaceFor`; fallback a la fuente del sistema si faltan.

## 11. Ownership

```text
AuroraProcessor
├── AudioProcessorValueTreeState (por valor)
├── ThemeManager (unique_ptr)          [message thread]
├── RackEngine (unique_ptr)
│   └── 16 × ModuleBase (unique_ptr)   [audio tras prepare]
├── MeteringHub (unique_ptr)
│   ├── LoudnessMeter                  [audio]
│   ├── SpectrumAnalyzer               [audio push / worker]
│   └── PhaseScope                     [audio push / UI draw]
├── ARIAController (unique_ptr)
│   ├── ARIAAnalyzer (captura)         [audio push / worker read]
│   ├── ARIAEngine (puro)              [worker]
│   └── ARIAReference (features)       [message thread]
├── meteringWorker (jthread)
└── ariaWorker (jthread)

AuroraEditor (host-owned vía createEditor)
├── AuroraLookAndFeel
├── HeaderBar / RackView / DetailEditorView / MeteringColumn / ARIAPanelView
└── referencias a AuroraProcessor (no ownership)
```

- `PluginProcessor` no es dueño del editor.
- Los workers no son dueños de buffers de audio del host; solo leen FIFOs o buffers congelados propiedad del processor.
- `ThemeManager` no es dueño de fuentes ni de componentes; solo de los `Theme`.
- El orden de destrucción es: editor → workers (join) → medidores → ARIA → rack → temas → APVTS.

## 12. Acceso a parámetros

Patrón obligatorio en constructores de módulos (message thread):

```cpp
ParametricEQ::ParametricEQ (ModuleId id, juce::AudioProcessorValueTreeState& state)
    : ModuleBase (id)
{
    const auto prefix = instancePrefix (id);
    outputGain.bind (state, prefix + ".output");
    // ...
}
```

- `bind` cachea `state.getRawParameterValue(id)`; no se vuelve a buscar por string.
- En audio: `updateTarget()` una vez por bloque; `next()` por muestra.
- Un parámetro inexistente en el catálogo es un error de programación: `jassert` en debug, silencio/valor por defecto en release.
- Los `AudioProcessorParameter` se crean exclusivamente desde `createParameterLayout()`; ningún módulo añade parámetros por su cuenta.

## 13. Diagnóstico y errores

```cpp
struct Diagnostics
{
    std::atomic<std::uint32_t> nonFiniteInput { 0 };
    std::atomic<std::uint32_t> nonFiniteOutput { 0 };
    std::atomic<std::uint32_t> droppedSpectrumSamples { 0 };
    std::atomic<std::uint32_t> droppedAriaSamples { 0 };
    std::atomic<std::uint32_t> droppedRackCommands { 0 };
    std::atomic<std::uint32_t> allocationFailures { 0 };
    std::atomic<std::uint32_t> ariaGeneration { 0 };
    std::atomic<bool> prepared { false };
};
```

La UI muestra estos contadores en el panel de diagnóstico (Fase 7). No hay logging en audio; los errores de estado/tema se registran en message thread en `ThemeManager::getLoadErrors()` o en un `juce::StringArray` del processor.

## 14. Compatibilidad de firma y estilo

- Namespace único `aurora`; sub-namespace `aurora::dsp` para utilidades matemáticas.
- `#pragma once` en todas las cabeceras.
- `[[nodiscard]]` en getters puros; `noexcept` en todo lo que corre en audio.
- Sin `using namespace` en cabeceras.
- `std::span` para vistas de buffers de tamaño fijo; `juce::AudioBuffer<float>&` para el bloque del host.
- `std::array` para capacidad fija; `std::vector` solo para datos dimensionados en `prepare`.
- Nada de RTTI en audio; `dynamic_cast` solo en inicialización si es imprescindible (preferir factory tipada).
