# 02 — Seguridad en tiempo real

## 1. Alcance y contrato

Este documento fija las reglas que todo código que se ejecute dentro de `AudioProcessor::processBlock` debe cumplir. Aplica a `PluginProcessor`, `RackEngine`, todos los `ModuleBase::process`, `LoudnessMeter`, las escrituras en FIFOs de metering/ARIA y cualquier callback invocado desde el hilo de audio.

Contrato mínimo de una función de audio:

```cpp
void process (juce::AudioBuffer<float>& buffer) noexcept; // sin alloc, sin lock, sin I/O, sin throw
```

Una función de audio puede:

- Leer `std::atomic<float>` con `memory_order_relaxed`.
- Leer/escribir buffers preasignados de tamaño fijo.
- Escribir en una FIFO SPSC o publicar un índice en un triple buffer.
- Actualizar contadores `std::atomic<uint32_t>` con `relaxed`.
- Calcular DSP en `float`/`double`, con `juce::ScopedNoDenormals` activo.

No puede: reservar memoria, bloquear, esperar, abrir/cerrar archivos, crear hilos, usar `std::function` que capture con heap, construir `juce::String`, loggear, lanzar excepciones, llamar a `juce::MessageManager`, tocar APVTS (más allá de leer los `std::atomic<float>*` ya cacheados) ni destruir objetos compartidos.

## 2. Reglas por categoría

| Categoría | Regla | Verificación |
|---|---|---|
| Memoria | Toda reserva ocurre en constructor o `prepareToPlay`. `process` solo usa capacidad ya reservada. | Test con contador de allocaciones + revisión de código. |
| Locks | Prohibido `std::mutex`, `juce::CriticalSection`, `WaitableEvent`, `spin_lock`. | `grep` en `Source/DSP`, `Source/Metering`, `Source/Core/RackEngine.cpp`. |
| Hilos | Prohibido crear, unir o dormir hilos desde audio. | Revisión de llamadas a `std::thread`, `std::jthread`, `sleep_for`. |
| Excepciones | `processBlock` y todos los `process` son `noexcept`. | El compilador lo impone; no hay `try/catch` en rutas de audio. |
| E/S | Prohibido `File`, `FileOutputStream`, `std::cout`, `juce::Logger`, `DBG` en bucles de audio. | `grep` de esos símbolos. |
| APVTS | El audio solo lee `std::atomic<float>*` cacheados con `load(relaxed)`. | Revisión de `ParamRef`/`SmoothedParam`. |
| UI | El audio nunca notifica ni espera a la UI. | Sin `sendChangeMessage`, `repaint`, `triggerAsyncUpdate` desde audio. |
| Tiempo | Sin bucles dependientes de entrada no acotada; sin `while` sin cota. | Revisión + fuzzing de parámetros. |

`prepareToPlay` corre en el hilo del host fuera del callback y puede asignar. El host garantiza que no hay `processBlock` concurrente durante `prepareToPlay`; aun así, `RackEngine::prepare` reasigna cada buffer antes de publicar el nuevo estado y deja los módulos en estado `reset`.

## 3. Mapa de asignaciones

Todo lo que sigue se reserva en `prepareToPlay` y se reutiliza:

| Recurso | Fórmula / tamaño | Peor caso (192 kHz, 2048) |
|---|---|---|
| Delay estéreo (`dlyA`) | `ceil(fs × 2.05 s) + block + 4` por canal | ≈ 393.600 muestras × 2 × 4 B ≈ 3,15 MB |
| Reverb FDN (`revA`) | 8 líneas × `ceil(fs × 63,7 ms × 1,5) + block` | ≈ 147.000 muestras × 4 B ≈ 0,59 MB |
| Pre-delay reverb | `ceil(fs × 0,2 s)` por canal | ≈ 38.400 × 2 × 4 B ≈ 0,31 MB |
| Captura ARIA | `fs × 8 s × 2 canales` | ≈ 3.072.000 muestras × 4 B ≈ 11,7 MB |
| FFT 4096 + ventanas | `2 × 4096` complejos + 3 × 4096 float | < 200 KB |
| FIFO espectro | 8192 float | 32 KB |
| FIFO vectorscopio | 1024 pares L/R | 8 KB |
| Líneas de latencia del rack | 16 × 192 muestras × 2 canales | 24 KB |
| Oversampling 8× SAT | 2 canales × 512 taps × 8 fases | < 100 KB |
| Oversampling 4× MASTER | 2 canales × 512 taps × 4 fases | < 100 KB |

Presupuesto total previsto: **< 20 MB** con ARIA armada. El editor no asigna buffers de audio; los medidores de UI leen snapshots publicados.

Si una reserva falla, el módulo queda marcado como no preparado, su `process` escribe silencio y `PluginProcessor` publica un flag atómico de error. Nunca se intenta reservar de nuevo desde audio.

## 4. FIFO SPSC

Se usa `juce::AbstractFifo` (productor único, consumidor único). El productor nunca espera: si no cabe el bloque completo, escribe lo que quepa, cuenta el descarte y retorna.

```cpp
class SpscFloatFifo
{
public:
    explicit SpscFloatFifo (int capacity)
        : storage (static_cast<size_t> (capacity)), fifo (capacity) {}

    int write (const float* source, int numSamples) noexcept
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

        std::copy (source, source + size1, storage.begin() + start1);
        std::copy (source + size1, source + size1 + size2, storage.begin() + start2);
        fifo.finishedWrite (size1 + size2);
        return size1 + size2; // < numSamples => el llamador cuenta el drop
    }

    int read (float* dest, int maxSamples) noexcept
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead (maxSamples, start1, size1, start2, size2);
        std::copy (storage.begin() + start1, storage.begin() + start1 + size1, dest);
        std::copy (storage.begin() + start2, storage.begin() + start2 + size2, dest + size1);
        fifo.finishedRead (size1 + size2);
        return size1 + size2;
    }

private:
    std::vector<float> storage;
    juce::AbstractFifo fifo;
};
```

Reglas:

- La capacidad se fija en `prepare`. `write` no llama a `resize`.
- El consumidor puede drenar todo el bloque; el productor solo escribe.
- `write` devuelve el número realmente escrito. La diferencia se acumula en `droppedSamples` (atómico) y se muestra en diagnóstico.
- `AbstractFifo` ya usa atomics internos con las barreras necesarias; no se envuelve en otro lock.
- Los FIFOs de visualización son *lossy* por diseño (INV-09). Los de comandos son *lossless* mientras quepan; si se llenan, la UI reintenta.

### 4.1 Cola de comandos del rack

```cpp
struct RackCommand
{
    RackCommandType type;
    ModuleId id;
    bool flag;
    std::array<ModuleId, kNumModules> order;
};

static_assert (std::is_trivially_copyable_v<RackCommand>);
```

Un `SpscQueue<RackCommand, 64>` con `AbstractFifo` + `std::array<RackCommand, 64>`. El audio drena al inicio del bloque con un máximo de 64 comandos. Un `setOrder` trae el array completo; nunca se comparten punteros a `std::vector`.

## 5. Triple buffer

Para publicar snapshots grandes (espectro de 2048 bins, informe ARIA, estado de medidores) se usa un triple buffer de tres ranuras: una propiedad del productor, una del consumidor y una intermedia compartida. No hay locks ni espera; el consumidor siempre obtiene la última publicación completa.

```cpp
template <typename Payload>
class TripleBuffer
{
public:
    Payload& beginWrite() noexcept { return slots[static_cast<size_t> (writing)]; }

    void publish() noexcept
    {
        const auto previous = shared.exchange (writing | freshBit, std::memory_order_acq_rel);
        writing = previous & indexMask;
    }

    // Devuelve nullptr si no hay publicación nueva.
    const Payload* tryReadLatest() noexcept
    {
        if ((shared.load (std::memory_order_acquire) & freshBit) == 0)
            return nullptr;

        const auto previous = shared.exchange (reading, std::memory_order_acq_rel);
        reading = previous & indexMask;
        return &slots[static_cast<size_t> (reading)];
    }

private:
    static constexpr uint32_t freshBit = 4;
    static constexpr uint32_t indexMask = 3;

    std::array<Payload, 3> slots {};
    std::atomic<uint32_t> shared { 0 }; // índice intermedio + flag fresh
    uint32_t writing = 1;               // propiedad del productor
    uint32_t reading = 2;               // propiedad del consumidor
};
```

- `Payload` debe ser trivialmente copiable/ asignable y de tamaño fijo. Nada de `std::vector` dentro del payload.
- El productor puede publicar dos veces antes de que el consumidor lea: la ranura intermedia se sobrescribe y el consumidor recibe la última versión. Es aceptable para visualización.
- Los índices son propiedad exclusiva de un hilo; solo `shared` es atómico. No se usan referencias al payload fuera de la ventana de lectura del consumidor.
- El consumidor (UI) copia o dibuja inmediatamente; el productor puede reutilizar la ranura en la siguiente publicación.

## 6. Suavizado de parámetros

`SmoothedParam` encapsula `juce::SmoothedValue<float, Linear>` y un `std::atomic<float>*`:

```cpp
struct SmoothedParam
{
    std::atomic<float>* source = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoother;
    float lastTarget = 0.0f;

    void updateTarget() noexcept; // una vez por bloque, lee relaxed
    float next() noexcept;        // una muestra
    void snap (float) noexcept;   // reset / prepare, sin rampa
};
```

Reglas:

- `updateTarget()` se llama una vez por bloque, no por muestra.
- Los coeficientes de filtros se recalculan cada 32 muestras leyendo `current()`; no se interpola el coeficiente directamente (evita inestabilidad en `f→0`).
- Los parámetros discretos (tipo de filtro, modelo, división) no se suavizan: se aplican en el límite de subtrama y fuerzan `reset()` del estado del filtro para evitar transitorios.
- Tiempos de rampa por defecto (alineados con `ModuleCatalog`):

| Parámetro | Rampa |
|---|---:|
| Gains, pan, mix, makeup, ratios, thresholds | 20 ms |
| Frecuencias de EQ y crossover | 30 ms |
| Tiempo de delay | 30 ms |
| Decay de reverb | 30 ms |
| Bypass / activación de instancia | 5 ms (crossfade, no smoother) |
| Cambio de modelo de saturación | 5 ms de crossfade entre curvas |

- Un salto de parámetro desde la UI nunca produce un click: el peor caso es una rampa de 20–30 ms.
- `snap()` solo se usa en `prepare`, `reset`, restauración de estado y al activar un módulo tras priming.

## 7. Denormales

- `juce::ScopedNoDenormals noDenormals;` como primera instrucción de `processBlock` y de cada `process` que pueda invocarse fuera de él (tests).
- Las redes de retroalimentación (reverb, delay, filtros) incluyen *flush* explícito cuando el estado cae por debajo de `1e-30`:

```cpp
if (std::abs (z) < 1.0e-30) z = 0.0;
```

- No se inyecta dither ni ruido DC: el coste de CPU se controla con FTZ/DAZ, no con una señal espuria.
- Test obligatorio: entrada de silencio durante 10 s con reverb y delay al 95 % de feedback; el tiempo de CPU por bloque no debe crecer más de un 10 % respecto al mismo test con señal.
- En MSVC se compila con `/fp:fast` solo en los targets DSP si los tests numéricos lo permiten; si no, `/fp:precise` y FTZ explícito.

## 8. NaN, Inf y estabilidad numérica

1. **Entrada**: al inicio de `processBlock` se sanea cada muestra: `std::isfinite(x) ? clamp(x, -16, 16) : 0`. Se cuenta el evento en un atómico.
2. **Parámetros**: `SmoothedParam::updateTarget` descarta valores no finitos y conserva el último válido. Un parámetro fuera de rango se limita al rango del catálogo.
3. **Estado interno**: cada filtro biquad comprueba `std::isfinite(y)`; si falla, `reset()` y devuelve 0. Los acumuladores de envolvente hacen lo mismo.
4. **División por cero**: prohibida. Se usa `1 / max(|x|, eps)` con `eps = 1e-12` o `1e-20` según la magnitud.
5. **Salida**: safety clamp final `±16.0` y saneado. INV-08: ningún sample no finito sale del plugin.
6. **Estado cargado**: `setStateInformation` valida cada valor contra el catálogo antes de aplicarlo; un preset corrupto no puede inyectar NaN.
7. **Recuperación**: tras un evento no finito, el módulo afectado se resetea y se marca `hadNonFinite` para diagnóstico. El audio continúa; nunca se lanza.

## 9. Ciclo de vida de hilos

```text
PluginProcessor ctor (message thread)
  ├─ crea APVTS, catálogo, RackEngine
  ├─ crea ThemeManager (message thread)
  ├─ arranca SpectrumAnalyzer worker (jthread)
  └─ arranca ARIA worker (jthread)

prepareToPlay (host thread, sin audio concurrente)
  ├─ dimensiona buffers de módulos y líneas de latencia
  ├─ dimensiona FIFOs y captura ARIA
  └─ publica sampleRate/maxBlock en atomics antes de habilitar process

processBlock (audio thread)
  ├─ lee parámetros, procesa rack
  ├─ escribe FIFOs / captura
  └─ publica métricas atómicas

~PluginProcessor (message thread)
  ├─ pide stop a ARIA y luego a metering
  ├─ notify_all de sus condiciones
  ├─ join de ambos jthread
  └─ libera buffers y objetos de análisis
```

- Los workers usan `std::jthread` con `std::stop_token` y `std::condition_variable_any::wait(lock, token, pred)`.
- El destructor nunca se ejecuta en el hilo de audio; el host lo garantiza.
- `PluginEditor` puede abrirse y cerrarse en cualquier momento: se suscribe a los atomics/triple buffers de metering y se desuscribe en su destructor. Nunca es dueño de buffers de audio.
- `ThemeManager` vive en el message thread y no se consulta desde audio.
- `prepareToPlay` no se llama concurrentemente con `releaseResources` por el host; aun así, los workers se pausan con un flag atómico durante el redimensionado.

## 10. Auditoría

Checklist por release (se firma en `docs/08_TEST_STRATEGY.md`):

| # | Comprobación | Método |
|---:|---|---|
| A1 | Cero allocaciones en `processBlock` | `operator new` interceptado en tests; contador debe ser 0 tras `prepare`. |
| A2 | Cero locks en rutas de audio | Revisión + `grep` de `mutex`, `CriticalSection`, `ScopedLock`. |
| A3 | Cero E/S y logging | `grep` de `File`, `Logger`, `DBG`, `std::cout`. |
| A4 | Sin `std::function` en audio | `grep` + revisión de callbacks. |
| A5 | Sin hilos creados en audio | Revisión de `std::thread`/`jthread`. |
| A6 | Todos los buffers dimensionados en `prepare` | Inspección de miembros y `setSize`. |
| A7 | `ScopedNoDenormals` presente | Instrumentación de test de CPU con silencio. |
| A8 | Sanitize de entrada/salida | Test con NaN/Inf/denormal inyectados. |
| A9 | Coeficientes finitos | Test de barrido de parámetros en 192 kHz. |
| A10 | Sin espera a workers | Test de worker bloqueado; el audio debe seguir. |
| A11 | FIFO llena no bloquea | Test que llena la FIFO y mide tiempo de bloque. |
| A12 | Latencia constante | Test de PDC tras reordenar/activar/desactivar. |
| A13 | Estado inválido no rompe | Fuzz de XML/JSON de estado y temas. |
| A14 | Editor cerrado no afecta audio | Test de ciclo abrir/cerrar 100 veces bajo carga. |
| A15 | Sin carreras de datos | ThreadSanitizer en Linux en los tests de estrés. |

### 10.1 Presupuesto de CPU

Medición con `juce::PerformanceCounter` o `std::chrono::steady_clock` fuera del callback, acumulando el tiempo de `processBlock` durante 10 s. Objetivo provisional (se ratifica en Fase 8):

| Escenario | Presupuesto |
|---|---|
| 48 kHz / 256 muestras, rack completo activo | < 25 % de un núcleo |
| 96 kHz / 128 muestras, rack completo | < 40 % |
| 192 kHz / 64 muestras, rack completo | < 65 % |
| 192 kHz / 2048 muestras, rack completo | < 30 % |

Si un escenario excede el presupuesto, se optimiza el módulo culpable; no se sube el presupuesto sin ADR.

## 11. Antipatrones prohibidos

1. **Reservar en el audio thread** “solo la primera vez”. La primera vez es un glitch.
2. **Usar `std::vector::push_back`** dentro de `process` aunque la capacidad “casi siempre” alcance.
3. **Copiar `AudioBuffer`** (copia profunda) al pasar por un módulo.
4. **`std::shared_ptr` copiado** en audio (incremento atómico y posible destrucción).
5. **`std::function`** como callback de proceso; usar plantillas o funciones libres.
6. **`juce::String`** para formatear valores en audio; el formateo es de UI.
7. **Locks “breves”** o spinlocks; cualquier espera es un glitch potencial.
8. **`try/catch`** en audio; las excepciones no son un mecanismo de control de flujo RT.
9. **`new`/`delete`** de módulos al insertar en el rack; el pool es fijo.
10. **Leer APVTS por ID** en cada bloque (`getRawParameterValue("eqA.b1.freq")`); cachear el puntero.
11. **`getParameter()` + `convertFrom0to1`** en audio; usar el valor atómico normalizado del APVTS.
12. **Interpolar coeficientes de filtro** en lugar de frecuencia/Q; puede desestabilizar el filtro.
13. **Resetear filtros en cada bloque** para “evitar clicks”; provoca discontinuidades.
14. **`while (!fifo.isFull())`** o esperas activas.
15. **Ignorar `droppedSamples`**; el descarte debe ser visible en diagnóstico.
16. **Confiar en `denormal` del hardware**; usar `ScopedNoDenormals` y flush explícito.
17. **Publicar punteros a memoria del audio thread** sin triple buffer/FIFO.
18. **Escribir parámetros desde ARIA o desde el worker**; solo el message thread aplica cambios vía APVTS.
19. **Destruir objetos compartidos con workers sin join previo**.
20. **Asumir que `processBlock` siempre recibe el mismo tamaño de bloque**; el buffer debe soportar `0..maxBlockSize`.
