# 06 — Sistema de temas

## 1. Principio

Un solo LookAndFeel vectorial, dos temas planos de fábrica y cero colores hardcodeados en componentes. Un tema es un documento JSON que describe **solo**:

- Colores sólidos opacos (`#RRGGBB`).
- Geometría entera (bordes, radios, alturas, tamaño de knob, segmentos de medidor).
- Familias y tamaños tipográficos.

No existen degradados, sombras, texturas, radios por esquina, opacidades por token ni imágenes rasterizadas. El esquema JSON rechaza cualquier propiedad adicional, de modo que un token de degradado no puede colarse.

## 2. Archivos e identificadores

| Archivo | `id` | `name` |
|---|---|---|
| `assets/themes/dark-matter.json` | `dark-matter-flat` | `Dark Matter - Flat` |
| `assets/themes/industrial-steel.json` | `industrial-steel-flat` | `Industrial Steel - Flat` |
| `assets/themes/theme.schema.json` | — | JSON Schema 2020-12 |

- El `id` es el valor que se guarda en `uiThemeId` dentro del estado.
- Los nombres de archivo no son el `id`: el `id` viaja en el documento.
- `ThemeManager::darkMatterId` y `ThemeManager::industrialSteelId` contienen los dos IDs de fábrica.
- Un tema nuevo debe usar un `id` que cumpla `^[a-z][a-z0-9]*(-[a-z0-9]+)*$`, entre 3 y 48 caracteres.

## 3. Tokens de color

Los 23 tokens son obligatorios en el esquema y en el parser.

| Token | Dark Matter | Industrial Steel | Uso |
|---|---|---|---|
| `background` | `#0A0B0D` | `#101216` | Chasis del editor. |
| `surface` | `#14161B` | `#181B20` | Cara de slot / panel de módulo. |
| `surfaceAlt` | `#1A1D24` | `#1F2329` | Header, panel de detalle, menús. |
| `border` | `#22262E` | `#2A2F37` | Separadores de 1 px. |
| `borderStrong` | `#333A45` | `#3D434D` | Slot seleccionado, límites de grupo. |
| `textPrimary` | `#E6EAF2` | `#E8EBEF` | Etiquetas y valores principales. |
| `textSecondary` | `#98A2B3` | `#A3ABB8` | Unidades, ayudas, ejes. |
| `textDisabled` | `#5A6373` | `#646C79` | Controles deshabilitados. |
| `accent` | `#00E5FF` | `#38BDF8` | Estado activo, medidores, anillo de gain. |
| `accentSecondary` | `#8B5CF6` | `#A855F7` | Curva de EQ, reducción de ganancia, frecuencia/Q. |
| `warning` | `#FFB74D` | `#F5B54A` | Saturación, avisos. |
| `danger` | `#FF5370` | `#F2545B` | Clip, over, acciones destructivas. |
| `success` | `#2ED47A` | `#34D399` | Validación, lock, estado correcto. |
| `meterLow` | `#2ED47A` | `#34D399` | Zona baja del medidor (−60…−18 dBFS). |
| `meterMid` | `#FFB74D` | `#F5B54A` | Zona media (−18…−6 dBFS). |
| `meterHigh` | `#FF5370` | `#F2545B` | Zona alta (≥ −6 dBFS). |
| `meterBackground` | `#0F1115` | `#0C0E11` | Segmentos apagados / fondo de medidor. |
| `grid` | `#1E232C` | `#232830` | Rejilla de RTA, timeline, ejes. |
| `knobBody` | `#1A1E25` | `#232830` | Cuerpo del knob. |
| `knobRing` | `#00E5FF` | `#38BDF8` | Anillo de posición del knob. |
| `knobPrimary` | `#00E5FF` | `#38BDF8` | Puntero de gain/volumen. |
| `knobSecondary` | `#8B5CF6` | `#A855F7` | Puntero de frecuencia/Q. |
| `selection` | `#123A45` | `#1B3038` | Fondo de slot seleccionado, selección de texto. |

Reglas de uso:

- `accent` y `accentSecondary` no son intercambiables: el primero indica **estado**, el segundo **curva/parámetro espectral**.
- `danger` solo para clip/over y acciones irreversibles. Un simple valor alto usa `warning`.
- `meterLow/Mid/High` se usan exclusivamente en medidores; no como colores de texto.
- Ningún componente puede usar literales de color, ni siquiera `juce::Colours::black` o `transparent` para fondos. La transparencia se usa solo en el propio dibujo vectorial (p. ej. relleno de una curva), nunca para sustituir un token.

## 4. Geometría

| Campo | Dark Matter | Industrial Steel | Rango del esquema |
|---|---:|---:|---|
| `borderWidth` | 1 | 1 | 0…4 px |
| `cornerRadius` | 2 | 0 | 0…12 px |
| `slotHeight` | 96 | 96 | 64…160 px |
| `headerHeight` | 52 | 52 | 32…96 px |
| `knobSize` | 44 | 44 | 24…96 px |
| `meterSegments` | 24 | 20 | 8…64 |

- El radio es único para todos los rectángulos; no hay radios por esquina.
- Un `cornerRadius = 0` es un tema totalmente ortogonal (Industrial Steel).
- `slotHeight` es la altura del slot de rack a escala 1,0.
- `meterSegments` es el número de segmentos del medidor vertical; la UI los reparte uniformemente.

## 5. Tipografía

```json
"typography": {
  "uiFamily": "Inter",
  "monoFamily": "JetBrains Mono",
  "sizes": { "small": 10, "normal": 12, "large": 15, "mono": 11 }
}
```

- `uiFamily` y `monoFamily` son `const` en el esquema: los temas de fábrica solo pueden usar las fuentes embebidas.
- Las fuentes se cargan de `AuroraAssets` (`InterVariable.ttf`, `JetBrainsMono-Variable.ttf`) con `juce::Typeface::createSystemTypefaceFor`. Si el recurso falta, se usa la fuente del sistema y se registra un error de carga de fuentes (no de tema).
- `mono` se usa en todos los valores numéricos tabulares: LUFS, dB, Hz, Q, ms, porcentajes.
- `sizes` debe cumplir `small ≤ normal ≤ large`. El parser corrige una inversión y lo reporta.
- Todos los tamaños se multiplican por `uiScale`; el mínimo efectivo es 9 px. La UI no permite `uiScale < 1,0` en el editor (el estado puede contener 0,8 por compatibilidad, pero se limita al aplicar).

## 6. Documento completo

```json
{
  "$schema": "./theme.schema.json",
  "id": "dark-matter-flat",
  "name": "Dark Matter - Flat",
  "version": 1,
  "colors": { "background": "#0A0B0D", "...": "23 tokens obligatorios" },
  "geometry": {
    "borderWidth": 1,
    "cornerRadius": 2,
    "slotHeight": 96,
    "headerHeight": 52,
    "knobSize": 44,
    "meterSegments": 24
  },
  "typography": {
    "uiFamily": "Inter",
    "monoFamily": "JetBrains Mono",
    "sizes": { "small": 10, "normal": 12, "large": 15, "mono": 11 }
  }
}
```

`$schema` es opcional pero recomendado para edición. El esquema usa `additionalProperties: false` en la raíz, en `colors`, en `geometry`, en `typography` y en `sizes`. Un token desconocido o un degradado declarado como propiedad extra invalidan el documento.

## 7. Carga

`ThemeManager` se construye en el message thread (constructor de `PluginProcessor`) y ejecuta:

```text
1. themes.reserve(3)
2. asset = AuroraAssets::getNamedResource("dark-matter.json", size)
   si asset válido -> parsear y añadir
3. asset = AuroraAssets::getNamedResource("industrial-steel.json", size)
   si asset válido -> parsear y añadir
4. si no existe el id "dark-matter-flat" -> instalar el fallback compilado
5. activeIndex = índice de "dark-matter-flat"
6. si falla el paso 4 y el vector está vacío -> activo = 0 (no puede ocurrir: el paso 4 garantiza un tema)
```

`BinaryData.h` lo genera el target `AuroraAssets` de `juce_add_binary_data`. El `.cpp` lo incluye con `#if __has_include(<BinaryData.h>)`. Si el archivo no existe en el include path (por ejemplo un target de tests que no enlaza `AuroraAssets`), el módulo compila y usa el fallback compilado; `isUsingEmbeddedAssets()` devuelve `false`. El integrador debe:

- Añadir `AuroraAssets` como dependencia del target del plugin.
- Añadir el directorio de generación de `BinaryData.h` al include path.
- Declarar los dos JSON en `juce_add_binary_data(AuroraAssets SOURCES assets/themes/dark-matter.json assets/themes/industrial-steel.json ...)`.

No se inventa ningún símbolo de BinaryData: si el namespace o la firma no coinciden, el fallback compilado mantiene el plugin funcional.

## 8. Validación

El parser aplica estas reglas, en orden:

| Regla | Fallo |
|---|---|
| JSON parseable y raíz objeto | Documento rechazado por completo. |
| `id` string, regex, longitud 3…48 | Documento rechazado por completo. |
| `id` no duplicado | Documento ignorado; se conserva el primero. |
| `name` string no vacío | Se usa el `id` como nombre; se registra error. |
| `version` entero 1…1000 | Se usa 1; se registra error. |
| `colors` objeto | Se conservan los 23 valores del fallback compilado; se registra error. |
| Cada color string `#RRGGBB` | Ese token usa el valor compilado de Dark Matter; se registra error. |
| `geometry` objeto | Se conservan los 6 valores compilados; se registra error. |
| Cada geometría entera en rango | Ese campo usa el valor compilado; se registra error. |
| `typography` objeto | Se conservan familias y tamaños compilados; se registra error. |
| `uiFamily`/`monoFamily` string no vacío | Se conserva el valor compilado; se registra error. |
| Cada tamaño numérico 8…40 | Ese tamaño usa el valor compilado; se registra error. |
| `small ≤ normal ≤ large` | Se corrige el orden; se registra error. |

- La validación de color es estricta: exactamente 7 caracteres, `#` y 6 dígitos hexadecimales. No se aceptan `#RGB`, `#RRGGBBAA`, nombres CSS ni `rgb()`.
- Un error de campo **no** invalida el tema: se aplica el fallback de Dark Matter para ese campo y el tema sigue seleccionable. Esto evita que un JSON con un typo deje la UI sin colores.
- Los errores se guardan en `ThemeManager::getLoadErrors()` (máximo 64) y se muestran en el panel de diagnóstico con el nombre del recurso y la ruta del campo.
- El esquema es la referencia para autores y CI; el parser es la última línea de defensa en runtime.

## 9. Fallback

```text
1. JSON dark-matter válido            -> tema cargado
2. JSON dark-matter inválido en un campo -> ese campo = valor compilado Dark Matter
3. JSON dark-matter irrecuperable     -> tema compilado Dark Matter
4. JSON industrial-steel inválido     -> mismo tratamiento; si el id no llega a existir,
                                          el selector muestra solo Dark Matter
5. Sin BinaryData                    -> ambos temas compilados; el selector muestra
                                          "Dark Matter - Flat" únicamente
```

- El fallback compilado es exactamente el contenido de `dark-matter.json` (mismos 23 colores, misma geometría y tipografía).
- El fallback nunca es un color “de emergencia” arbitrario: es un tema completo y coherente.
- `getActiveTheme()` nunca devuelve null ni un tema a medio construir.

## 10. Hilos, ownership y notificaciones

- `ThemeManager` es message-thread-only. No hay locks, ni atomics, ni acceso desde audio.
- `PluginProcessor` es dueño de `ThemeManager` mediante `std::unique_ptr`.
- `AuroraEditor` se registra como `juce::ChangeListener` y llama a `setTheme(id)` desde el selector.
- `setTheme`:
  1. Busca el id.
  2. Si no existe, registra error, devuelve `false` y **no** cambia nada.
  3. Si existe y es distinto del activo, actualiza `activeIndex` y llama a `sendChangeMessage()`.
  4. Si es el mismo, devuelve `true` sin notificar (evita repintados innecesarios).
- `sendChangeMessage()` es asíncrono; el editor aplica el tema en el siguiente `changeListenerCallback`, en message thread.
- Al cargar un preset con `uiThemeId`, el processor llama a `setTheme` en message thread después de restaurar APVTS. Si el tema no existe, mantiene el actual y avisa.
- El tema no se serializa en APVTS; va en las propiedades del `ValueTree`.

## 11. Mapa de componentes

| Componente | Tokens |
|---|---|
| Chasis del editor | `background` |
| Header | `surfaceAlt`, `border`, `textPrimary`, `textSecondary` |
| Slot de rack | `surface`, `border`, `borderStrong` (selección), `selection` (fondo seleccionado), `accent` (activo), `textDisabled` (bypass) |
| Etiqueta de módulo | `textPrimary`, `textSecondary` (subtítulo) |
| Knob | `knobBody`, `knobRing`, `knobPrimary` (gain), `knobSecondary` (freq/Q), `textPrimary` (valor), `textSecondary` (unidad) |
| Slider horizontal | `surfaceAlt` (surco), `accent` (relleno), `knobBody` (tirador) |
| Botón | `surfaceAlt`, `border`, `textPrimary`, `accent` (activo), `danger` (destructivo) |
| Combo / menú | `surfaceAlt`, `border`, `textPrimary`, `selection` (item resaltado) |
| Medidor de nivel | `meterBackground`, `meterLow`, `meterMid`, `meterHigh`, `textSecondary` (escala) |
| Medidor de GR | `accentSecondary`, `meterBackground`, `grid` |
| RTA | `grid`, `accent`, `accentSecondary` (curva), `textSecondary` (ejes) |
| Vectorscopio | `grid`, `accent`, `meterHigh` (clip) |
| Curva de EQ | `accentSecondary`, nodos `accent`, rejilla `grid` |
| Panel ARIA | `surfaceAlt`, `accent` (Analyze), `warning` (conflicto), `success` (aplicado), `danger` (error) |
| Barra de estado | `surfaceAlt`, `textSecondary`, `danger` (drop/non-finite), `warning` (CPU alta) |
| Tooltip | `surfaceAlt`, `border`, `textPrimary` |
| Foco de teclado | `accent` (borde 2 px) + `borderStrong` (offset 1 px) |

## 12. Accesibilidad

Requisitos verificables en CI (test de contraste WCAG 2.1 sobre los JSON):

| Par | Umbral | Dark Matter | Industrial Steel |
|---|---|---:|---:|
| `textPrimary` sobre `background` | ≥ 4,5:1 | ≈ 16,3:1 | ≈ 15,8:1 |
| `textSecondary` sobre `surface` | ≥ 4,5:1 | ≈ 7,2:1 | ≈ 7,8:1 |
| `textDisabled` sobre `surface` | ≥ 3,0:1 (informativo) | ≈ 3,1:1 | ≈ 3,4:1 |
| `accent` sobre `surface` | ≥ 3,0:1 (gráfico) | ≈ 12,0:1 | ≈ 8,4:1 |
| `accentSecondary` sobre `surface` | ≥ 3,0:1 (gráfico) | ≈ 4,4:1 | ≈ 4,6:1 |
| `warning` sobre `surface` | ≥ 3,0:1 | ≈ 10,7:1 | ≈ 10,0:1 |
| `danger` sobre `surface` | ≥ 4,5:1 (texto de error) | ≈ 5,9:1 | ≈ 5,3:1 |
| `success` sobre `surface` | ≥ 4,5:1 | ≈ 9,6:1 | ≈ 9,0:1 |
| `meterLow` sobre `meterBackground` | ≥ 3,0:1 | ≈ 9,2:1 | ≈ 9,5:1 |

Reglas adicionales:

1. **Nunca color como único portador de información.** Activo = color + borde/icono; clip = color + texto “CLIP”; solo/mute = color + letra `S`/`M`; error = color + icono + texto.
2. **Foco visible**: borde de 2 px `accent` con 1 px de separación `borderStrong`, en ambos temas y sobre cualquier superficie.
3. **Área de interacción mínima**: 24×24 px a escala 1,0; el knob de 44 px cumple.
4. **Texto mínimo**: 10 px a escala 1,0; los valores numéricos usan `mono` (11 px).
5. **Escala**: la UI admite 100–200 %. A 200 % no se recorta texto; los paneles usan layout proporcional y scroll donde corresponde.
6. **Sin parpadeos** por encima de 3 Hz; los medidores actualizan a 30–60 Hz con suavizado visual, no parpadeo.
7. **Unidades siempre visibles** en los valores (dB, Hz, LUFS, dBTP, %), no solo el número.
8. **Contraste de medidores**: los tres colores de zona se distinguen entre sí y del fondo; además hay lectura numérica.
9. **Daltonismo**: `meterLow`/`meterMid`/`meterHigh` no son el único indicador de nivel; la altura del segmento y el número lo son.

## 13. Añadir un tema

1. Copiar `dark-matter.json` a `assets/themes/<nombre>.json`.
2. Cambiar `id` y `name`; mantener `version: 1`.
3. Rellenar los 23 tokens y la geometría.
4. Validar contra `theme.schema.json` (CI o `python -m jsonschema`).
5. Añadir el archivo a `juce_add_binary_data(AuroraAssets ...)`.
6. Añadir su id a la lista de temas de fábrica solo si se quiere que `ThemeManager` lo cargue en el constructor.
7. Ejecutar el test de contraste y el de carga de temas.
8. Actualizar la tabla de la sección 3 si el tema es de fábrica.

No se requieren cambios en `ThemeManager` para un tema nuevo si se añade al `BinaryData`: el constructor lo carga y `getThemeIds()` lo expone.

## 14. Pruebas

| Test | Criterio |
|---|---|
| `theme.schema` | Ambos JSON validan contra `theme.schema.json` con `additionalProperties: false`. |
| `theme.load` | `getThemeIds()` contiene los dos ids de fábrica. |
| `theme.active` | El tema activo inicial es `dark-matter-flat`. |
| `theme.fallback` | Un JSON sin `colors` conserva los 23 colores compilados y reporta 1 error. |
| `theme.badHex` | `#12345` y `#GGGGGG` usan el valor compilado y reportan error. |
| `theme.badId` | `Dark_Matter` y `dark--matter` se rechazan por completo. |
| `theme.duplicate` | Un id repetido no reemplaza al primero. |
| `theme.set` | `setTheme` desconocido devuelve `false` y no emite cambio. |
| `theme.broadcast` | `setTheme` distinto emite exactamente un `ChangeBroadcaster`. |
| `theme.contrast` | Todos los pares de la tabla de accesibilidad cumplen el umbral. |
| `theme.geometry` | Valores fuera de rango usan el fallback y reportan error. |
| `theme.noGradient` | El esquema rechaza propiedades como `gradient`, `shadow` o `opacity`. |
| `theme.thread` | `getLoadErrors()` no crece sin límite (máx. 64). |

## 15. Prohibiciones explícitas

- Degradados (`linear-gradient`, `radial-gradient`, cualquier par de colores interpolado).
- Sombras, biseles, relieves, `dropShadow`, `innerShadow`.
- Texturas, ruido, imágenes rasterizadas de chasis.
- Neones, halos, `glow`, blur.
- Tarjetas flotantes con elevación.
- Colores con alfa en el JSON.
- Tokens nuevos sin actualizar el esquema y el parser a la vez.
- Lectura del tema desde el hilo de audio.
