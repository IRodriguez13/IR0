# Onboarding IR0 — Del clone al primer parche

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T0–T1 |
| Estado | stable |
| Depende de | boot, multi-arch |
| Página man | IR0-onboarding (sección 7) |
| Fuentes principales | `README.md`, `SETUP.md`, `CONTRIBUTING.md`, `scripts/deptest.sh`, `scripts/make/profiles.mk`, `scripts/make/hostshare-boot.mk`, `includes/ir0/boot_log_hostshare.h` |

## 1. Visión general

Este capítulo es para **nuevos contribuidores**. Cubre clonar IR0, chequear el
host, construir la imagen desktop soportada, leer man pages, consultar un boot
log opcional en el host, y aterrizar un primer cambio pequeño.

Fuera de alcance: reescribir el kernel, flashear appliance ARM, o reclamar ABI
Linux completa. La profundidad de subsistemas está en otras páginas `IR0-*`.

## 2. Arquitectura interna (superficie de entrada)

| Pieza | Rol |
|-------|-----|
| `make check-env` / `deptest` | Diagnóstico host (required / optional / unusable) |
| `make defconfig` / `first-boot` / `run` | Primer boot oficial (x86-64 + BusyBox) |
| `make help-profiles` | Perfiles + techos honestos |
| `make sync-mandocs` / `make man TOPIC=` | Docs sin pelear MANPATH |
| `make pre-submit` | Gate local → `PRE_SUBMIT_OK` |
| `CONFIG_BOOT_LOG_HOSTSHARE` | Dump opt-in del ring log → virtio-9p `ir0-boot.log` |

## 3. Flujo de datos — primer boot

```text
  git clone IR0 → cd IR0
       → make check-env
       → make defconfig
       → make first-boot   # clona ../IR0-userspace + rootfs BusyBox + ISO
       → make run          # QEMU → getty → BusyBox ash
```

Sin `/sbin/init` en `disk.img` el kernel hace panic en el handoff (no hay shell in-kernel).
Ver `Documentation/esp/USERSPACE.md`.

Boot log opcional en el **host**:

```text
  make run-bootlog
       → activa BOOT_LOG_HOSTSHARE
       → QEMU -virtfs mount_tag=ir0share
       → guest escribe build/hostshare/ir0-boot.log
       → tag serial BOOT_LOG_HOSTSHARE_OK
```

## 4. Responsabilidades

- Contribuidor: correr `check-env` antes de reportar “no compila”.
- Mantenedor: Makefile raíz fino; recetas en `scripts/make/*.mk`.
- Kernel: nunca panic si no hay 9p; tag SKIP si está opt-in sin dispositivo.

## 5. Límites del subsistema

- No meter smokes one-shot de agente en el Makefile raíz.
- No reclamar imágenes RPi flasheables (lab UART / stub).
- Mandocs documentan lo **implementado**; aspiracional solo en §10.

## 6. Relaciones con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| IR0-boot | Pipeline de arranque / banner serial |
| IR0-multi-arch | Techos ARM lab |
| filesystems / virtio-9p | Share usado por el dump de boot log |
| CONTRIBUTING.md | Estilo, commit, pre-submit |

## 7. Mapas visuales

```text
  Nuevo contribuidor
       │
       ├─► check-env ──► defconfig ──► first-boot ──► run
       │                                      │
       │                                      └─► (opcional) run-bootlog
       │                                                └─► host ir0-boot.log
       │
       └─► sync-mandocs ──► man TOPIC=onboarding|boot|mm|…
                              └─► primer bug ──► pre-submit
```

## 8. Invariantes importantes

1. Primer boot oficial = **desktop x86-64 bajo QEMU**.
2. `hub-rpi4` = lab UART; `watch-rpi5-stub` = stub compile Planned.
3. Boot log hostshare es **opt-in**; `make run` sigue limpio.
4. Las facades en `includes/ir0/` **no** están todas documentadas — ver INDEX.

## 9. Consejos de depuración — primer bug

1. Arrancá con `make run` y mirá el banner serial `IR0 kernel …` (luego ARCH / HYPERVISOR / PLATFORM).
2. `make man TOPIC=boot` — buscá `ir0_boot_serial_ready` / `boot_log.h`.
3. Grep el símbolo; abrí el `.c`; leé un call site en `kmain`.
4. Opcional: `make run-bootlog` y abrí `build/hostshare/ir0-boot.log`.
5. Un cambio de **un archivo**, luego:

```bash
make pre-submit
# esperar PRE_SUBMIT_OK
```

## 10. Manuales en guest

Tras `make first-boot` / `load-userspace-runit`, el disco de producto incluye
BusyBox `man` y páginas IR0 pre-renderizadas en `/usr/share/man/cat7/`:

```text
man IR0-boot
man IR0-onboard
man -w IR0-tty
```

Rebuild en host: `make prepare-guest-mandocs` (requiere `mandoc` en el host).
Instalación completa EN/ES en el host: `make sync-mandocs`.

## 11. Roadmap futuro

- Más recetas “primer bug” por subsistema.
- Más cobertura mandoc de `includes/ir0/` (backlog honesto).

Ver también: `README.md`, `SETUP.md`, `CONTRIBUTING.md`, `Documentation/USERSPACE.md`,
`make help-bootlog`.
