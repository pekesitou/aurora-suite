# 05 — Catálogo de parámetros (APVTS)

## 1. Fuente de verdad

`Source/Core/ModuleCatalog.cpp` es la única fuente. `createParameterLayout()` recorre:

1. `catalog().globals` (3 parámetros).
2. `catalog().modules` en orden de `ModuleId`.
3. Para cada módulo, `params` en el orden en que se construyen.

`PluginProcessor` construye:

```cpp
juce::AudioProcessorValueTreeState apvts {
    *this, nullptr, "AURORA", aurora::createParameterLayout()
};
```

- Todos los parámetros usan `juce::ParameterID { id, 1 }`.
- Rango normalizado 0…1 gestionado por APVTS; el DSP lee el valor real desde `std::atomic<float>*`.
- No hay parámetros fuera del catálogo. Un `ParamRef::bind` a un ID inexistente es un error de programación (`jassert` en debug).

## 2. Convención de IDs

```text
{instancia}.{parametro}
{grupoGlobal}.{parametro}
```

- `instancia` ∈ `utilA, utilB, eqA, eqB, compA, compB, mbA, dsA, gateA, transA, satA, satB, imgA, revA, dlyA, masterA`.
- `parametro` en `lowerCamelCase`, ASCII, sin puntos adicionales.
- Bandas: `b1`…`b8` (EQ) y `b1`…`b4` (multibanda), seguidas de `.on`, `.type`, `.freq`…
- Globales: `global.inputTrim`, `global.outputTrim`, `aria.strength`.
- El ID es estable para siempre. Nunca se renombra, reordena ni reutiliza.
- Los IDs son visibles en el host; no se usan abreviaturas ambiguas (`b1.freq` es correcto; `b1f` no).

Total: **238 parámetros**.

| Grupo | Cantidad |
|---|---:|
| Globales + ARIA | 3 |
| Utility ×2 | 14 |
| EQ ×2 | 84 |
| Compressor ×2 | 26 |
| Multiband | 37 |
| De-Esser | 9 |
| Gate | 9 |
| Transient | 6 |
| Saturator ×2 | 16 |
| Imager | 6 |
| Reverb | 11 |
| Delay | 10 |
| Mastering | 7 |
| **Total** | **238** |

## 3. Parámetros globales

| ID | Tipo | Rango | Default | Unidad | Smooth |
|---|---|---|---:|---:|---:|
| `global.inputTrim` | float | −24…+24 | 0 | dB | 20 ms |
| `global.outputTrim` | float | −24…+24 | 0 | dB | 20 ms |
| `aria.strength` | float | 0…100 | 100 | % | — (se aplica en message thread) |

`inputTrim` y `outputTrim` se aplican antes y después del rack, respectivamente. `aria.strength` no afecta al audio directamente: modula el porcentaje de las sugerencias de ARIA al aplicarlas.

## 4. Utility (`utilA`, `utilB`)

Prefix: `utilA.` / `utilB.`

| Sufijo | Tipo | Rango | Default (`A`/`B`) | Unidad | Smooth |
|---|---|---|---:|---:|---:|
| `active` | bool | 0/1 | true / false | — | — |
| `gain` | float | −24…+24 | 0 | dB | 20 ms |
| `pan` | float | −1…+1 | 0 | — | 20 ms |
| `polarityL` | bool | 0/1 | false | — | — |
| `polarityR` | bool | 0/1 | false | — | — |
| `mono` | bool | 0/1 | false | — | — |
| `swap` | bool | 0/1 | false | — | — |

## 5. Parametric EQ (`eqA`, `eqB`)

Prefix: `eqA.` / `eqB.`

Comunes:

| Sufijo | Tipo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|---:|
| `active` | bool | 0/1 | true / false | — | — | — |
| `output` | float | −12…+12 | 0 | dB | — | 20 ms |

Por banda `N = 1…8`:

| Sufijo | Tipo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|---:|
| `bN.on` | bool | 0/1 | true | — | — | — |
| `bN.type` | choice | 7 opciones | Bell (4) | — | — | — |
| `bN.freq` | float | 20…20000 | ver tabla | Hz | 1000 | 30 ms |
| `bN.gain` | float | −18…+18 | 0 | dB | — | 20 ms |
| `bN.q` | float | 0,1…18 | 0,707 | — | 1 | 20 ms |

`bN.type`: `0 Low Pass`, `1 High Pass`, `2 Band Pass`, `3 Notch`, `4 Bell`, `5 Low Shelf`, `6 High Shelf`.

Frecuencias por defecto:

| Banda | `eqA` | `eqB` |
|---:|---:|---:|
| 1 | 80 | 100 |
| 2 | 160 | 250 |
| 3 | 320 | 500 |
| 4 | 640 | 1000 |
| 5 | 1250 | 2000 |
| 6 | 2500 | 4000 |
| 7 | 5000 | 8000 |
| 8 | 10000 | 12000 |

## 6. Compressor (`compA`, `compB`)

Prefix: `compA.` / `compB.`. Defaults idénticos en ambas instancias.

| Sufijo | Tipo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|---:|
| `active` | bool | 0/1 | false | — | — | — |
| `threshold` | float | −60…0 | −18 | dB | — | 20 ms |
| `ratio` | float | 1…20 | 4 | :1 | 4 | 20 ms |
| `attack` | float | 0,1…200 | 10 | ms | 10 | 20 ms |
| `release` | float | 10…2000 | 100 | ms | 150 | 20 ms |
| `knee` | float | 0…40 | 10 | dB | — | 20 ms |
| `detector` | choice | Peak / RMS | Peak (0) | — | — | — |
| `rmsWindow` | float | 1…50 | 10 | ms | 10 | 20 ms |
| `makeup` | float | 0…24 | 0 | dB | — | 20 ms |
| `autoMakeup` | bool | 0/1 | false | — | — | — |
| `mix` | float | 0…100 | 100 | % | — | 20 ms |
| `scHPF` | float | 20…500 | 20 | Hz | 100 | 20 ms |
| `link` | float | 0…100 | 100 | % | — | 20 ms |

`rmsWindow` solo tiene efecto con `detector = RMS`. `autoMakeup` aplica `−0.5·threshold·(1−1/ratio)` limitado a 0…24 dB y se suma a `makeup`.

## 7. Multiband Compressor (`mbA`)

Prefix: `mbA.`

| Sufijo | Tipo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|---:|
| `active` | bool | 0/1 | false | — | — | — |
| `x1` | float | 40…500 | 120 | Hz | 150 | 30 ms |
| `x2` | float | 200…3000 | 800 | Hz | 800 | 30 ms |
| `x3` | float | 1000…12000 | 5000 | Hz | 4000 | 30 ms |
| `mix` | float | 0…100 | 100 | % | — | 20 ms |

Por banda `N = 1…4`:

| Sufijo | Tipo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|---:|
| `bN.threshold` | float | −60…0 | −18 | dB | — | 20 ms |
| `bN.ratio` | float | 1…20 | 2 | :1 | 2 | 20 ms |
| `bN.attack` | float | 0,1…200 | 10 | ms | 10 | 20 ms |
| `bN.release` | float | 10…2000 | 100 | ms | 200 | 20 ms |
| `bN.knee` | float | 0…40 | 10 | dB | — | 20 ms |
| `bN.makeup` | float | 0…24 | 0 | dB | — | 20 ms |
| `bN.mute` | bool | 0/1 | false | — | — | — |
| `bN.solo` | bool | 0/1 | false | — | — | — |

Invariante de runtime: `x1 < x2 < x3`, con un margen mínimo del 5 %. Si la automatización intenta cruzarlos, el motor los corrige y marca diagnóstico.

## 8. De-Esser (`dsA`)

Prefix: `dsA.`

| Sufijo | Tipo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|---:|
| `active` | bool | 0/1 | false | — | — | — |
| `freq` | float | 3000…10000 | 6000 | Hz | 6000 | 20 ms |
| `threshold` | float | −60…0 | −24 | dB | — | 20 ms |
| `ratio` | float | 1…20 | 4 | :1 | 4 | 20 ms |
| `attack` | float | 0,1…50 | 1 | ms | 1 | 20 ms |
| `release` | float | 10…500 | 50 | ms | 50 | 20 ms |
| `range` | float | 0…24 | 12 | dB | — | 20 ms |
| `listen` | bool | 0/1 | false | — | — | — |
| `mix` | float | 0…100 | 100 | % | — | 20 ms |

## 9. Gate / Expander (`gateA`)

Prefix: `gateA.`

| Sufijo | Tipo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|---:|
| `active` | bool | 0/1 | false | — | — | — |
| `threshold` | float | −80…0 | −40 | dB | — | 20 ms |
| `range` | float | −80…0 | −60 | dB | — | 20 ms |
| `attack` | float | 0,05…100 | 1 | ms | 1 | 20 ms |
| `hold` | float | 0…500 | 50 | ms | — | — |
| `release` | float | 5…2000 | 200 | ms | 200 | 20 ms |
| `hysteresis` | float | 0…12 | 3 | dB | — | — |
| `scHPF` | float | 20…500 | 80 | Hz | 100 | 20 ms |
| `mix` | float | 0…100 | 100 | % | — | 20 ms |

`hold` y `hysteresis` se leen por bloque sin smoother; sus cambios no producen clicks porque solo afectan a la máquina de estados.

## 10. Transient Shaper (`transA`)

Prefix: `transA.`

| Sufijo | Tipo | Rango | Default | Unidad | Smooth |
|---|---|---|---:|---:|---:|
| `active` | bool | 0/1 | false | — | — |
| `attack` | float | −100…+100 | 0 | % | 20 ms |
| `sustain` | float | −100…+100 | 0 | % | 20 ms |
| `sensitivity` | float | 0…100 | 50 | % | 20 ms |
| `output` | float | −12…+12 | 0 | dB | 20 ms |
| `mix` | float | 0…100 | 100 | % | 20 ms |

## 11. Saturator (`satA`, `satB`)

Prefix: `satA.` / `satB.`. Defaults idénticos.

| Sufijo | Tipo | Rango | Default | Unidad | Smooth |
|---|---|---|---:|---:|---:|
| `active` | bool | 0/1 | false | — | — |
| `model` | choice | Tube / Tape / Diode / Transistor | Tube (0) | — | — |
| `drive` | float | 0…24 | 6 | dB | 20 ms |
| `tone` | float | −12…+12 | 0 | dB | 20 ms |
| `bias` | float | −0,5…+0,5 | 0,1 | — | 20 ms |
| `autoGain` | bool | 0/1 | true | — | — |
| `output` | float | −24…+24 | 0 | dB | 20 ms |
| `mix` | float | 0…100 | 100 | % | 20 ms |

## 12. Stereo Imager (`imgA`)

Prefix: `imgA.`

| Sufijo | Tipo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|---:|
| `active` | bool | 0/1 | false | — | — | — |
| `width` | float | 0…200 | 100 | % | — | 20 ms |
| `mid` | float | −12…+12 | 0 | dB | — | 20 ms |
| `side` | float | −12…+12 | 0 | dB | — | 20 ms |
| `bassMonoFreq` | float | 20…500 | 120 | Hz | 100 | 20 ms |
| `bassMonoAmount` | float | 0…100 | 100 | % | — | 20 ms |

## 13. Reverb (`revA`)

Prefix: `revA.`

| Sufijo | Tipo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|---:|
| `active` | bool | 0/1 | false | — | — | — |
| `size` | float | 0…100 | 50 | % | — | 20 ms |
| `decay` | float | 0,2…20 | 2 | s | 2 | 30 ms |
| `predelay` | float | 0…200 | 20 | ms | — | 20 ms |
| `damping` | float | 0…100 | 40 | % | — | 20 ms |
| `diffusion` | float | 0…100 | 70 | % | — | 20 ms |
| `width` | float | 0…200 | 100 | % | — | 20 ms |
| `lowcut` | float | 20…500 | 80 | Hz | 100 | 20 ms |
| `highcut` | float | 1000…20000 | 12000 | Hz | 8000 | 20 ms |
| `mix` | float | 0…100 | 25 | % | — | 20 ms |
| `freeze` | bool | 0/1 | false | — | — | — |

`lowcut` y `highcut` se corrigen en runtime para que `highcut > lowcut·1,05`.

## 14. Stereo Delay (`dlyA`)

Prefix: `dlyA.`

| Sufijo | Tipo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|---:|
| `active` | bool | 0/1 | false | — | — | — |
| `sync` | bool | 0/1 | true | — | — | — |
| `division` | choice | 11 opciones | `1/4` (2) | — | — | — |
| `timeL` | float | 1…2000 | 350 | ms | — | 30 ms |
| `timeR` | float | 1…2000 | 500 | ms | — | 30 ms |
| `feedback` | float | 0…95 | 35 | % | — | 20 ms |
| `pingpong` | bool | 0/1 | false | — | — | — |
| `lowcut` | float | 20…2000 | 120 | Hz | 200 | 20 ms |
| `highcut` | float | 1000…20000 | 8000 | Hz | 8000 | 20 ms |
| `mix` | float | 0…100 | 25 | % | — | 20 ms |

`division`: `1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4 D, 1/8 D, 1/4 T, 1/8 T, Free`.

`timeL`/`timeR` solo tienen efecto con `sync = false` o `division = Free`.

## 15. Mastering (`masterA`)

Prefix: `masterA.`

| Sufijo | Tipo | Rango | Default | Unidad | Skew | Smooth |
|---|---|---|---:|---:|---:|---:|
| `active` | bool | 0/1 | true | — | — | — |
| `clipperOn` | bool | 0/1 | true | — | — | — |
| `clipperMode` | choice | Soft / Hard | Soft (0) | — | — | — |
| `drive` | float | 0…12 | 0 | dB | — | 20 ms |
| `ceiling` | float | −12…0 | −0,3 | dBTP | — | 20 ms |
| `limiterOn` | bool | 0/1 | true | — | — | — |
| `release` | float | 10…1000 | 100 | ms | 100 | 20 ms |

`ceiling` es el techo del clipper y del limitador. La UI debe mostrar el valor con un decimal y la unidad `dBTP`.

## 16. Formato de valores

`ModuleCatalog` instala `withStringFromValueFunction` para floats:

| Unidad | Formato |
|---|---|
| `Hz` | `< 1000`: `123.4 Hz`; `≥ 1000`: `1.23 kHz` |
| `%` | `45.0 %` |
| `dB`, `dBTP`, `ms`, `s`, `:1` | `valor + " " + unidad`, 2 decimales |
| sin unidad | 2 decimales |

Los `AudioParameterChoice` muestran el texto de la opción. Los `AudioParameterBool` muestran `On`/`Off` (el nombre accesible es el del parámetro).

## 17. Propiedades no automatizables

Se guardan en el `ValueTree` de estado, no como parámetros del host:

| Propiedad | Tipo | Default | Uso |
|---|---|---|---|
| `rackOrder` | string | orden por defecto | CSV de Instance IDs. |
| `uiThemeId` | string | `dark-matter-flat` | Tema activo. |
| `uiScale` | float | 1,0 | Escala 0,8…2,0. |
| `ariaProfileId` | string | `streaming` | Perfil de ARIA. |
| `ariaMode` | string | `assisted` | `manual` / `assisted`. |
| `ariaReferencePath` | string | vacío | Ruta de referencia (no se usa en audio). |
| `lastSelectedModule` | string | `eqA` | Estado de UI. |
| `editorWidth`, `editorHeight` | int | 1280, 800 | Tamaño recordado. |

Ninguna de estas propiedades se lee en `processBlock`. `rackOrder` llega al audio por `RackCommand::setOrder`.

## 18. Suavizado y automation

- Todo parámetro continuo audible tiene `smoothingMs > 0`, salvo los que no afectan a la ganancia muestra a muestra (`hold`, `hysteresis`).
- Los parámetros discretos (bool/choice) se aplican en el límite de bloque o subtrama y, si cambian la topología de un filtro, fuerzan `reset()` de ese estado.
- La automatización de un módulo inactivo se conserva en APVTS y se aplica al activarlo. No se descarta.
- `SmoothedParam::updateTarget()` corre una vez por bloque; un salto de automatización se convierte en una rampa de 20–30 ms, nunca en un escalón.
- El rango de automatización es el rango del catálogo. No hay parámetros ocultos que cambien el sonido sin ID.

## 19. Compatibilidad y versionado

Reglas de por vida:

1. Un ID existente nunca cambia de nombre, tipo, unidad ni orden de opciones.
2. Los rangos no se estrechan. Se pueden ampliar en una versión mayor, con migración documentada.
3. Los defaults pueden cambiar solo en una versión mayor y con `StateMigrations` que preserve presets antiguos.
4. Las nuevas opciones de un `choice` se añaden al final.
5. Los nuevos parámetros se añaden al final de su módulo (o al final de `globals`), nunca en medio.
6. Un parámetro eliminado se conserva como *deprecated* al menos una versión mayor, leyéndose pero sin efecto, para no romper automatizaciones.
7. `getStateInformation` escribe `stateVersion` y `pluginVersion`; `setStateInformation` rechaza versiones futuras desconocidas sin lanzar.
8. Un preset con IDs desconocidos carga el resto y reporta un aviso; no falla en bloque.

## 20. Matriz de verificación del catálogo

| Test | Criterio |
|---|---|
| `catalog count` | `modules.size() == 16`, `globals.size() == 3`. |
| `unique ids` | 238 IDs únicos. |
| `id format` | Regex `^[a-z][a-zA-Z0-9]*\.[a-zA-Z0-9.]+$`. |
| `range` | `min <= default <= max` en todos los floats. |
| `step` | `step >= 0`; si `> 0`, divide el rango. |
| `skew` | `skewCentre == 0` o dentro de `(min, max)`. |
| `choices` | `defaultValue` entero en `[0, choices.size()−1]`. |
| `smoothing` | Todo float audible tiene `smoothingMs ∈ {20, 30}`. |
| `layout` | `createParameterLayout()` produce exactamente 238 parámetros. |
| `round-trip` | Guardar y restaurar estado conserva los 238 valores ± 1e-6. |
| `automation` | Todos los parámetros son `isAutomatable() == true`. |
