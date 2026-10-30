# Tooling y Build de IR0

Este documento describe el flujo activo de compilacion, configuracion y validacion.
Esta alineado con el Makefile raiz y menuconfig actuales.

## Targets de Build Principales

- `make ir0`: construye el artefacto por defecto para `ARCH`/`BOARD`.
  - x86-64 → `kernel-x64.iso`
  - `arm64` → `kernel-arm64.bin`
  - `arm64 rpi4` → `build/arm64-rpi4/kernel8.img`
- `make ir0 arm64`: agregado ARM64 para QEMU `virt`.
- `make kernel-x64.bin`: compila y linkea solo el kernel.
- `make kernel-x64.iso`: genera el ISO desde la imagen actual.
- `make all`: mismo artefacto que `ir0`, sin la politica `-Werror`.
- `make clean`: limpia artefactos generados.

## Targets de Ejecucion

- `make run`: GUI en x86-64, serial en ARM64.
- `make run console`: ejecucion serial (`-nographic`).
- `make run debug`: log guest-error/int + monitor QEMU.
- `make run fullscreen`: GTK pantalla completa.
- `make run gdb`: arranca QEMU esperando GDB en localhost:1234.
- `make run arm64 rpi4`: arranca `build/arm64-rpi4/kernel8.img`.

`run` nunca compila. Resuelve el artefacto desde `ARCH`/`BOARD` y falla si no
existe. `DISK=…` adjunta una imagen de usuarios; `DISK=none` evita adjuntarla.
Lista completa de variables: `Documentation/MAKEFILE.md`.

## Flujo de Configuracion

- `make defconfig`: vuelve al baseline.
- `make menuconfig`: configuracion interactiva TUI.
- `make menuconfig-en`: fuerza menuconfig en ingles.
- `make menuconfig-es`: fuerza menuconfig en espanol.
- `python3 scripts/kconfig/menuconfig.py --set ...`: cambios no interactivos.
- `python3 scripts/kconfig/menuconfig.py --preset ...`: aplicacion de preset.

### Areas Clave de Config

- Seleccion de init de drivers (`CONFIG_INIT_*`).
- Seleccion de filesystem (`CONFIG_ENABLE_FS_*`).
- Politica de scheduler (`CONFIG_SCHEDULER_POLICY`).
- Layout de teclado por defecto (`CONFIG_KEYBOARD_LAYOUT`).
- Idioma del menu (`CONFIG_TOOL_MENUCONFIG_LANG`).

## Targets de Validacion

- `make ctr`: gate rapido de merge (kernel + `arch-guard` + `build-matrix-min` + tests host).
- `make agent-fast`: `ctr` + bateria ktest in-kernel.
- `make ktest` / `make kernel-tests`: tests in-kernel en QEMU.
- `make ktm-check`: tests host + guards KTM (classify/manifest/adoption/portabilidad).
- `make build-matrix-min`: matriz `.config` rapida; restaura el `.config` del usuario.
- `make health`: `kernel-analyze` + text budget + memsafe + kernel tests.
- `make arch-guard`: fronteras de arquitectura (`scripts/architecture_guard.py`).
- `make repo-hygiene-guard`: higiene del repositorio.
- `python3 scripts/kernel_export_digest.py kernel-x64.bin`: SHA-256 de simbolos
  globales ordenados (`nm`), comparable entre builds con la misma cadena de
  herramientas (`Documentation/DECOUPLING.md`).

No hay `build-matrix-full`, catalogo QA de smokes ni bundle de release en el
Makefile actual. Nombres historicos en documentos de release no son puntos de
entrada del build.

## Targets de Documentacion

| Target | Que hace |
|--------|----------|
| `make mandocs` | Pregunta idioma, luego todos los capitulos o seleccion; instala |
| `make mandocs-en` | Preguntas en ingles; instala en `~/.local/share/man/man7/` (sin sudo) |
| `make mandocs-es` | Preguntas en espanol; `man IR0-krnl-es` tras instalar |

Sistema completo: `sudo MANDOC_PREFIX=/usr/local make mandocs-en`

Desinstalar: `make mandocs-uninstall` o `MANDOC_LANG=all make mandocs-uninstall`

- `make ai-dev-rules-install`: copia reglas de AI dev a `.cursor/` (gitignored).

## Tooling externo de usuarios

El Makefile del kernel no expone ISD, kernel-manager ni catalogo de maquinas.
`scripts/kernel_manager.py` sigue en el arbol para inspeccion manual, pero no
esta cableado a `make`. Construi imagenes de usuarios con el hermano
[ISD](https://github.com/IRodriguez13/ISD) y adjuntalas con `make run DISK=…`.

**Build `#N` vs tag de release:** `#363` en tooling host es el contador
**local a la maquina** en `.build_number` (incrementa en cada link de
`kernel-x64.bin`). Es independiente de `IR0_VERSION_STRING`. Cuando cambia la
cadena de release, el Makefile resetea `.build_number` a `1`
automáticamente (stamp `.build_number_version`).

## Puntos Fuertes

- Reproducibilidad de config con defconfig y overrides scriptados.
- Combinacion util de matriz de build y tests in-kernel QEMU.
- Menuconfig soporta ambos idiomas y automatizacion por CLI.
- Set de targets chico y predecible; sin acoplamiento ISD ni de producto.

## Puntos Debiles

- La validacion runtime aun es por escenarios, no exhaustiva.
- Algunas features avanzadas siguen con politica MVP.
- La cobertura tipo CI es **local** (`make health`, `make ctr`, `make agent-fast`).
  GitHub Actions ya no corre el workflow pesado `Tests`; `Update LOC` es solo manual.
