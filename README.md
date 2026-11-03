# Gears3Recomp

Port no oficial de **Gears of War 3** (Xbox 360) a **Android** y **PC** por recompilación estática: el código
PowerPC del juego se traduce a C++ y se compila para ARM64 y x64, con un renderer Vulkan nativo. No es un
emulador. Port de **Jauidev**, hecho con la herramienta de este repositorio que lleva un juego de Xbox 360 del
disco a un APK. Sigue a [GearsRecomp](https://github.com/Jauidev/GearsRecomp), el port de Gears of War.

> **No incluye el juego ni nada que salga de él** (ni el código recompilado ni sus shaders): se generan desde
> tu propia copia del disco de Gears of War 3 para Xbox 360.
> Gears of War y sus marcas pertenecen a Microsoft. Este proyecto no está afiliado a Microsoft ni a Epic Games.

## Estado

- Se juega a **30 fps** (el límite del propio juego) en un POCO X6 Pro (Mali-G615).
- Los shaders que no están en la caché se traducen en segundo plano, para que la partida no se congele.
- Controles táctiles y mandos físicos por Bluetooth o USB (Xbox, PlayStation y la mayoría de mandos
  Android), a la vez.
- Launcher propio, con su fondo y sus colores:
  - **Gráficos y GPU**: se pueden quitar las **sombras** en tiempo real y los **efectos** (desenfoques,
    brillo, rayos de luz, distorsión y partículas más sencillas) para ganar rendimiento.
  - **Último error**: el informe de un cierre se copia con un botón, para enviarlo.
- Probado solo en el comienzo de la campaña y solo en una GPU Mali.

## Instalar en Android

La APK todavía no está publicada; estará en [Releases](../../releases) (arm64, Android 10 o superior, Vulkan).

1. Instala la APK y ábrela.
2. En el launcher elige la ISO del juego o la carpeta del disco extraído. También puedes copiar el disco
   extraído a `/sdcard/Android/data/com.xport.gears_of_war_3/files/game`.

En Adreno, el launcher puede usar Mesa Turnip si el driver del sistema no sirve.

## Compilar

Requisitos (Windows): Android NDK 27.2, Android SDK (build-tools 36, CMake 3.31), MSVC Build Tools 2022,
Python 3 y Git Bash. Todo el código de terceros va incluido en `third_party/`.

Del disco al APK, con todos los pasos (recompilar, ranura, shaders, PC, prueba de arranque, APK):

    py -3 -I tools/port.py <juego.iso|carpeta del disco> <carpeta de trabajo> gears_of_war_3 --label "Gears of War 3"

Con la ranura `games/gears_of_war_3` ya preparada:

    XPORT_GAME=gears_of_war_3 bash tools/build-android.sh xportapp
    XPORT_GAME=gears_of_war_3 bash tools/build-apk.sh

Para PC (PowerShell): `$env:XPORT_GAME='gears_of_war_3'; cmd /c tools\build-host.bat`.

Los shaders que no están en la caché se traducen en tiempo real; en Android hace falta `libdxcompiler.so`
(`tools\build-dxc-android.bat`).

La guía completa está en [docs/GUIA.md](docs/GUIA.md) y la descripción de las herramientas en
[docs/XPORT.md](docs/XPORT.md).

## Estructura

| Carpeta | Qué hay |
| --- | --- |
| `runtime/` | Runtime: kernel, CPU, audio, launcher |
| `rexglue_glue/`, `xenia_glue/` | Hooks de Direct3D y puente con el renderer |
| `third_party/rexglue/` | ReXGlue SDK y el renderer nativo xnative (de Liberty Recompiled) |
| `games/gears_of_war_3/` | La ranura del juego: configuración, arreglos, funciones de D3D y el fondo e icono del launcher |
| `tools/` | XPort: del disco al APK (`port.py`) y utilidades |
| `android/`, `LAUNCHER/` | La app de Android y los recursos del launcher |

## Licencia

[GPL-3.0](LICENSE): el renderer viene de Liberty Recompiled (GPL-3). Cada componente de `third_party/`
conserva su propia licencia.

## Créditos

Port Android: **Jauidev**.

Liberty Recompiled (renderer nativo xnative), ReXGlue SDK, XenonRecomp, XenosRecomp y Unleashed Recompiled
(hedge-dev), Xenia Canary, DirectX Shader Compiler, glslang, smol-v, volk, SMAA, Mesa Turnip, libadrenotools,
libGPUCounters, FFmpeg, libmspack, Zstandard, Snappy, xxHash, unordered_dense, O(1) Heap, spdlog, fmt,
Dear ImGui, stb_image, Khronos Validation Layers y GFXReconstruct.
