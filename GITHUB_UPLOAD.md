# Qué subir a GitHub cuando el proyecto esté terminado

Este archivo es una guía de publicación. No forma parte del producto y puedes
borrarlo después de subir el repositorio.

## 1. Qué debe entrar en el repositorio

Sube el código fuente y los recursos, no los artefactos de compilación:

```text
CMakeLists.txt
CMakePresets.json              (si se añade)
README.md
LICENSE                        (si se decide publicar con licencia)
.github/                       (CI, plantillas de issues, si se añaden)
Source/                        Todo el código C++ (Core, DSP, AI, Metering, UI)
Tests/                         Pruebas Catch2 y CMakeLists de tests
assets/
  themes/                      JSON de temas + theme.schema.json
  fonts/                       Inter, JetBrains Mono y sus licencias OFL
  profiles/                    Perfiles de ARIA (si se añaden)
docs/                          Arquitectura, DSP, RT safety, roadmap, ADRs
external/JUCE/                 Como submódulo Git, NO como carpeta copiada
```

`external/JUCE` está fijado al tag `8.0.17-security`. Lo correcto es registrarlo
como submódulo para que el repositorio no pese cientos de MB:

```powershell
git submodule add https://github.com/juce-framework/JUCE.git external/JUCE
git -C external/JUCE checkout 8.0.17-security
```

Si prefieres no usar submódulos, documenta en el README que hay que clonar JUCE
manualmente en `external/JUCE` con ese tag, y añade `external/` al `.gitignore`.

## 2. Qué NO debe subirse

```text
build/                         Carpeta de compilación de CMake
out/                           Salidas de Visual Studio / CLion
.vs/  .idea/  .vscode/         Configuración local del IDE (opcional ignorar)
CMakeUserPresets.json          Rutas absolutas de tu máquina
*.user  *.suo  *.sln.DotSettings.user
*.obj  *.pdb  *.ilk  *.exp  *.lib
*.dll  *.exe  *.vst3           Binarios: van a Releases, no al código
pluginval.exe                  Herramienta externa
external/JUCE/                 Si NO se usa submódulo
```

No subas tampoco presets de usuario, capturas temporales, logs ni la carpeta
`instrumentos`/`instrucciones.txt` si contienen notas personales.

## 3. Binarios: usa Releases

Cuando el VST3 compile y pase las pruebas, crea un ZIP con:

```text
Aurora Engine.vst3/            Plugin VST3 x64 completo
Aurora Engine.exe              Standalone (opcional)
README-INSTALL.txt             Instrucciones de instalación
LICENSE.txt                    Licencia del plugin
```

Súbelo como *Release* etiquetada, por ejemplo `v0.1.0`, en lugar de hacer commit
de los binarios. En Windows el VST3 se instala en:

```text
C:\Program Files\Common Files\VST3\Aurora Engine.vst3
```

## 4. Antes de publicar: comprobaciones

```powershell
# Configurar y compilar
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DAURORA_BUILD_TESTS=ON
cmake --build build

# Tests
ctest --test-dir build --output-on-failure

# Validación del plugin (si tienes pluginval)
pluginval.exe --strictness-level 8 --validate-in-process "build/artefacts/VST3/Aurora Engine.vst3"

# Estado de Git: revisa que no haya binarios ni basura
git status
```

Comprueba también:

- El plugin abre en al menos dos DAWs (Reaper + otro).
- Guardar/cargar preset restaura parámetros, orden del rack y tema.
- El VST3 aparece con nombre, fabricante y versión correctos.
- No hay warnings del compilador en `Source/`.
- `README.md` explica cómo compilar desde cero.

## 5. Secuencia recomendada de subida

```powershell
# Desde la raíz del proyecto
git init
git add .gitignore CMakeLists.txt README.md Source Tests assets docs
git add external/JUCE        # solo si lo has añadido como submódulo
git commit -m "Aurora Engine: plugin VST3 modular de mezcla y mastering"

git branch -M main
git remote add origin https://github.com/TU_USUARIO/aurora-engine.git
git push -u origin main
```

Después, en GitHub:

1. Verifica que `Source/`, `assets/` y `docs/` se ven completos.
2. Crea el Release `v0.1.0` y adjunta el ZIP del VST3.
3. Añade una descripción corta, requisitos (Windows 10/11 x64, VST3) y una nota
   sobre el estado del proyecto.
4. Si el repositorio es público, revisa las licencias de JUCE, Inter y JetBrains
   Mono antes de distribuir binarios.

## 6. Archivos que deben existir al publicar

- [ ] `CMakeLists.txt` funcional desde una copia limpia.
- [ ] `Source/` completo y compilando.
- [ ] `assets/themes/*.json` y `assets/fonts/` con sus licencias.
- [ ] `docs/` actualizado (como mínimo arquitectura, DSP y roadmap).
- [ ] `Tests/` con pruebas ejecutables.
- [ ] `README.md` con compilación, instalación y estado real.
- [ ] `.gitignore` que excluya `build/`, binarios y configuración local.
- [ ] Release con el VST3 x64 empaquetado.
