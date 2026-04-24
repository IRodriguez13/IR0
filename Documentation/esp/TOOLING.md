# Tooling y Build de IR0

Este documento describe el flujo activo de compilacion, configuracion y validacion.
Esta alineado con el Makefile y menuconfig actuales.

## Targets de Build Principales

- `make -j4`: build completo, link e ISO.
- `make kernel-x64.bin`: compila y linkea kernel sin ISO.
- `make kernel-x64.iso`: genera ISO desde binario actual.
- `make clean`: limpia artefactos de build.

## Targets de Ejecucion

- `make run`: perfil QEMU con GUI y hardware base de IR0.
- `make run-console`: ejecucion orientada a consola/serial.
- `make run-debug`: perfil con foco en debugging.
- `make run-gdb`: arranca QEMU esperando GDB en localhost.

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

- `make build-matrix-min`: matriz rapida de perfiles.
- `make build-matrix-full`: matriz extendida con guards.
- `make runtime-net-check`: smoke runtime en QEMU para red.
- `make scale-readiness-gate`: gate de estabilizacion.
- `make arch-guard`: checks de limites arquitectonicos.

## Test Harness de Host

- `make -C tests/host`: compila tests de host.
- `make -C tests/host run`: ejecuta suite de host.

## Puntos Fuertes

- Reproducibilidad de config con defconfig y overrides scriptados.
- Combinacion util de matriz de build y smoke runtime.
- Menuconfig soporta idiomas y automatizacion por CLI.

## Puntos Debiles

- La validacion runtime aun es por escenarios, no exhaustiva.
- Algunas features avanzadas siguen con politica MVP.
- La cobertura tipo CI depende de disciplina de ejecucion local.
