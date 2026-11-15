# AURORA ENGINE

Suite modular de mezcla y mastering en formato **VST3 de 64 bits**, construida con **C++20**, **JUCE 8**, **CMake** y **APVTS**. El producto se organiza como un rack de slots (estilo Serie 500) con procesadores DSP reales, medición EBU R128 / ITU-R BS.1770-4 y un asistente analítico determinista llamado **ARIA**.

> **Estado actual: FASE 1 — Arquitectura y especificación.**
> No hay todavía binarios ni código DSP. La Fase 2 (proyecto CMake + esqueleto compilable) comienza cuando la especificación de esta fase esté aprobada.

---

## Confirmación de inicio (bootstrap checklist)

1. **Arquitectura C++20 + JUCE 8 + APVTS comprendida.** El núcleo se apoya en `juce::AudioProcessor`, `juce::AudioProcessorValueTreeState`, `std::span`, `std::jthread`, `noexcept`, `constexpr` y RAII. El formato primario es VST3 x64; el núcleo DSP no depende del wrapper.
2. **Seguridad en tiempo real garantizada.** `processBlock` y todos los `process()` de los módulos son `noexcept`, sin asignaciones, sin locks, sin E/S, sin excepciones y sin creación de hilos. Toda la memoria se reserva en `prepareToPlay`. La comunicación usa FIFO SPSC (`juce::AbstractFifo`), buffers de triple índice y `std::atomic`.
3. **Estética Flat Design garantizada.** Chasis `#0A0B0D`, superficies `#14161B`, bordes sólidos de 1 px `#22262E`; sin degradados de fondo, sombras artificiales, neones ni tarjetas flotantes. Los controles son vectoriales y los temas se cargan desde JSON validado.
4. **ARIA desacoplada del hilo de audio.** El análisis se ejecuta en un `std::jthread` secundario sobre una instantánea SPSC del audio. El hilo de audio solo escribe muestras en un ring buffer y contadores atómicos; nunca espera al worker.

---

## Documentación

| Documento | Contenido |
|---|---|
| [`docs/01_ARCHITECTURE.md`](docs/01_ARCHITECTURE.md) | Capas, modelo de hilos, rack, latencia, estado, flujo de señal. |
| [`docs/02_RT_SAFETY.md`](docs/02_RT_SAFETY.md) | Reglas de tiempo real, FIFOs, suavizado, denormales, NaN/Inf y auditoría. |
| [`docs/03_DSP_SPEC.md`](docs/03_DSP_SPEC.md) | Ecuaciones y algoritmos de todos los procesadores y medidores. |
| [`docs/04_CLASS_MAP.md`](docs/04_CLASS_MAP.md) | Contratos de clases, interfaces, responsabilidades y estructura de archivos. |
| [`docs/05_PARAMETERS.md`](docs/05_PARAMETERS.md) | Catálogo APVTS, IDs, rangos, unidades, defaults y estado no automatizable. |
| [`docs/06_THEMES.md`](docs/06_THEMES.md) | Sistema de temas plano, tokens, carga/validación y mapa de componentes. |
| [`docs/07_ARIA.md`](docs/07_ARIA.md) | Métricas, reglas deterministas, Strength %, referencia y privacidad. |
| [`docs/08_TEST_STRATEGY.md`](docs/08_TEST_STRATEGY.md) | Pruebas unitarias, integración, PDC, CPU, DAWs y criterios de aceptación. |
| [`docs/09_ROADMAP.md`](docs/09_ROADMAP.md) | Fases, entregables, gates y riesgos. |
| [`docs/adr/`](docs/adr/) | Decisiones de arquitectura registradas (ADR). |

Los temas de fábrica están en [`assets/themes/`](assets/themes/) y se validan contra [`theme.schema.json`](assets/themes/theme.schema.json). Las fuentes variables Inter y JetBrains Mono se incluyen bajo SIL OFL 1.1 en [`assets/fonts/`](assets/fonts/).

---

## Estructura del repositorio (objetivo)

```text
aurora-suite/
├── CMakeLists.txt                  # Fase 2
├── README.md
├── assets/
│   ├── fonts/                      # InterVariable.ttf, JetBrainsMono-Variable.ttf + OFL
│   ├── themes/                     # dark-matter.json, industrial-steel.json, schema
│   └── profiles/                   # perfiles de loudness de ARIA (Fase 6)
├── Source/
│   ├── Core/                       # PluginProcessor/Editor, RackEngine, ThemeManager
│   ├── DSP/                        # ModuleBase + procesadores
│   ├── AI/                         # ARIAAnalyzer, ARIAEngine, ARIAReference
│   ├── Metering/                   # SpectrumAnalyzer, LoudnessMeter, PhaseScope
│   └── UI/                         # LookAndFeel, componentes, vistas
├── Tests/                          # Catch2
└── docs/                           # esta especificación
```

---

## Toolchain requerido (Fase 2)

| Herramienta | Versión mínima | Nota |
|---|---|---|
| Windows | 10/11 x64 | Plataforma primaria de validación. |
| Visual Studio 2022 | 17.8+ | Carga de trabajo *Desktop development with C++* + Windows 11 SDK. |
| CMake | 3.25+ | `CMakePresets.json` se añadirá en Fase 2. |
| Git | 2.40+ | FetchContent de JUCE y Catch2. |
| JUCE | `8.0.17-security` (pin) | JUCE 9 existe, pero queda fuera del alcance de esta especificación. |
| Catch2 | v3.7+ | Solo tests. |

**Estado del entorno de desarrollo detectado:** no hay `cmake`, `cl`, `ninja` ni JUCE instalados en la máquina actual. Esto **no bloquea la Fase 1**, que es documental, pero es un gate obligatorio antes de la Fase 2. No se declarará ningún build como válido sin compilarlo y ejecutar los tests.

Licencia de JUCE: la distribución del plugin requiere decidir entre **AGPLv3** o **licencia comercial de JUCE**. Esta decisión es de negocio y debe tomarse antes de publicar binarios; se documentará en `docs/LICENSING.md` durante la Fase 2.

---

## Principios no negociables

- Cero código de relleno: cada DSP descrito aquí se implementará con sus ecuaciones y se validará con tests numéricos.
- Cero asignaciones y cero locks en el hilo de audio.
- Latencia de plugin invariante ante cambios de rack (topología con presupuesto de latencia fijo; ver ADR-0002).
- Automatización completa vía APVTS y serialización de estado versionada.
- Un solo LookAndFeel vectorial, dos temas planos de fábrica, sin recursos rasterizados pesados.
- ARIA es determinista, explicable y nunca escribe parámetros desde el hilo de audio.
