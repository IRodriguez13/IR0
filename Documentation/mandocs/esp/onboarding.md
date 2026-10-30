# Onboarding IR0 — Del clone al primer parche

| Campo | Valor |
|-------|-------|
| Versión | 0.2 |
| Fase IR0 | T0–T1 |
| Estado | stable |
| Depende de | boot, multi-arch |
| Página man | IR0-onboarding (sección 7) |
| Fuentes principales | `README.md`, `SETUP.md`, `CONTRIBUTING.md`, `scripts/deptest.sh`, `Makefile`, `scripts/run_qemu.sh`, `Documentation/USERSPACE.md` |

## 1. Visión general

Este capítulo es para **nuevos contribuidores**. Cubre clonar IR0, chequear el
host, construir el kernel, correrlo en QEMU, leer man pages y aterrizar un
primer cambio pequeño.

Fuera de alcance: reescribir el kernel, flashear appliance ARM, o reclamar ABI
Linux completa. La profundidad de subsistemas está en otras páginas `IR0-*`.
Las imágenes de usuarios las construye el repositorio hermano **ISD**, no este
Makefile.

## 2. Arquitectura interna (superficie de entrada)

| Pieza | Rol |
|-------|-----|
| `make check-env` / `deptest` | Diagnóstico host (required / optional / unusable) |
| `make defconfig` / `ir0` / `run` | Primer boot oficial (x86-64 QEMU) |
| `make menuconfig` | TUI de configuración (variantes inglés / español) |
| `make sync-mandocs` / `make man TOPIC=` | Docs sin pelear MANPATH |
| `make ctr` | Gate rápido de merge → `CTR_OK` |
| `make agent-fast` | `ctr` + batería ktest en el kernel |
| `CONFIG_BOOT_LOG_HOSTSHARE` | Dump opt-in del ring log → 9p `ir0-boot.log` |

`make run` solo lanza un artefacto ya compilado. No compila ni invoca ISD.

## 3. Flujo de datos — primer boot

```text
  git clone IR0 → cd IR0
       → make check-env
       → make defconfig
       → make ir0        # kernel-x64.iso
       → make run        # QEMU → banner kernel → handoff /sbin/init
```

`make ir0` construye un ISO solo-kernel. Para tener shell hace falta una imagen
de usuarios construida por el hermano **ISD**; adjuntala con `make run DISK=…`.
Sin `/sbin/init` el kernel hace panic en el handoff (no hay shell in-kernel).
Ver `Documentation/esp/USERSPACE.md`.

ARM64 usa goals posicionales:

```bash
make ir0 arm64
make run arm64
make ir0 arm64 rpi4
make run arm64 rpi4
```

### Boot log opcional en el host

Activá `CONFIG_BOOT_LOG_HOSTSHARE=y` en `make menuconfig` y adjuntá un share 9p
con tag `ir0share`:

```bash
make run IR0_QEMU_EXTRA="-fsdev local,id=ir0fs,path=$PWD,security_model=none \
  -device virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on"
```

El guest escribe `ir0-boot.log` en el directorio compartido y emite el tag
serial `BOOT_LOG_HOSTSHARE_OK`.

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
| CONTRIBUTING.md | Estilo, commit, gates CTR |

## 7. Mapas visuales

```text
  Nuevo contribuidor
       │
       ├─► check-env ──► defconfig ──► ir0 ──► run
       │                                      │
       │                                      └─► (opcional) DISK=… / boot log 9p
       │
       └─► sync-mandocs ──► man TOPIC=onboarding|boot|mm|…
                              └─► primer bug ──► ctr
```

## 8. Invariantes importantes

1. Primer boot oficial = **desktop x86-64 bajo QEMU**.
2. Las boards ARM64 son objetivos de laboratorio; `rpi5` sigue siendo stub de
   compilación.
3. Boot log hostshare es **opt-in** (`CONFIG_BOOT_LOG_HOSTSHARE`); `make run`
   queda limpio por defecto.
4. Las facades en `includes/ir0/` **no** están todas documentadas — ver INDEX.

## 9. Consejos de depuración — primer bug

1. Arrancá con `make run` (o `make run console`) y mirá el banner serial
   `IR0 kernel …` (luego ARCH / HYPERVISOR / PLATFORM).
2. `make man TOPIC=boot` — buscá `ir0_boot_serial_ready` /
   `includes/ir0/boot_log.h`.
3. Grep el símbolo; abrí el `.c`; leé un call site en `kmain`.
4. Un cambio de **un archivo**, luego corré el gate rápido:

```bash
make ctr
# esperar CTR_OK
```

Sumá la batería in-kernel cuando toques rutas de QEMU/ktest:

```bash
make agent-fast
```

## 10. Manuales en guest

Los manuales del host los instala este árbol:

```bash
make sync-mandocs
make man TOPIC=onboarding
```

Las páginas en el guest (`/usr/share/man/cat7/`) pertenecen al rootfs de
**ISD**, no al Makefile del kernel. IR0 ya no expone `prepare-guest-mandocs`,
`first-boot` ni `load-userspace-runit`.

## 11. Roadmap futuro

- Más recetas de “primer bug” por subsistema (mm, vfs, net).
- Cobertura mandoc más amplia de `includes/ir0/` (backlog honesto — no hecho).
