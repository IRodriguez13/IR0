# IR0 Kernel Manager (kmang)

| Field | Value |
|-------|-------|
| Version | 0.1 |
| Fase IR0 | T1 (herramientas host para maquinas ISD persistentes) |
| Estado | stable |
| Depende de | userspace, boot, onboarding |
| Pagina man | IR0-kmang (seccion 7) |
| Fuentes principales | `scripts/kernel_manager.py`, `scripts/make/isd.mk`, `scripts/test_kernel_manager.py`, `scripts/test-isd-contracts.sh` |

> **Ultima verificacion:** 2026-09-20

## 1. Vision general

**kmang** (`make kmang`) es la TUI interactiva y la CLI del **catalogo de ISO kernel**
por maquina persistente. Cada maquina bajo `IR0-machines/<arch>/<profile>/<nombre>/`
guarda ISOs versionados, un symlink **Default** (`kernel-current.iso`) y un
**Fallback** opcional (`kernel-fallback.iso`).

`make poweron` arranca el **Default** del catalogo cuando `resolve` tiene exito — no
el ISO suelto del workspace hasta enrollarlo (`i` en la TUI, o
`make kernel-manager-install`).

Los numeros de build `#N` / `-buildN` son contadores **locales a la maquina host**
(`.build_number` en el arbol del kernel). No son IDs globales de release. Compare hosts
por SHA-256, `ir0_build_number` en el ELF y metadata git al instalar — no solo por `#N`.

**Fuera de alcance:** versionado userspace/rootfs (`make usmang`), pruebas KTM in-guest
(`Documentation/KTM.md`), y `make run-isd` (siempre ISO workspace).

## 2. Arquitectura interna

| Componente | Ruta | Rol |
|------------|------|-----|
| Store | `KernelStore` en `scripts/kernel_manager.py` | CRUD, verify, resolve |
| Imagenes | `<machine-dir>/kernels/<id>.iso` | Copias enrolladas |
| Metadata | `<machine-dir>/kernels/<id>.json` | formato 3: sha256, embedded_build, arch, git |
| Default | `<machine-dir>/kernel-current.iso` | Symlink al ISO seleccionado |
| Fallback | `<machine-dir>/kernel-fallback.iso` | Default anterior tras re-seleccion |
| Lock | `<machine-dir>/.kernel-manager.lock` | flock exclusivo |
| Workspace | `kernel-x64-userspace.iso` | ISO construido; no lo bootea `poweron` sin enroll |

**Id de catalogo:** `{IR0_VERSION_STRING}-build{N}` donde `N` sale de `nm ir0_build_number`
dentro del ELF del kernel — nunca del nombre de archivo solo.

**Capas de verificacion:**

1. ISO con `/boot/kernel-x64.bin`, `/boot/kernel-arm64.bin` o `/boot/kernel.bin` extraible.
2. ELF `e_machine` coincide con `--arch` del catalogo cuando esta configurado.
3. Build embebido coincide con sufijo `-buildN` cuando existe.
4. SHA-256 en disco y coherencia de metadata format/id/arch.

Estados de salud: `verified`, `checksum-only`, `legacy-unverified`, o motivos de fallo.

## 3. Flujo de datos

**Enroll del ISO workspace (TUI `i` o `install-workspace`):**

```text
  make kernel-x64-userspace.iso
       → source_identity(version, #N embebido)
       → copia a kernels/<version-buildN>.iso (+ fsync)
       → escribe .json (commit git, builder@host, sha256)
       → symlink atomico kernel-current.iso
       → Default anterior → kernel-fallback.iso
```

**Seleccion + arranque (`Enter` o `b`) en perfiles desktop:**

```text
  Enter → selecciona Default → t = solo terminal | x = X directo | q = quedarse
       → inyecta etc/ir0-session (one-shot) → make poweron
```

**Arranque maquina persistente (`make poweron`):**

```text
  kernel_manager resolve
       → verify Default; si falla, Fallback
       → catalogo vacio: ISO workspace
       → enrollado pero todo corrupto: exit 2 (no boot silencioso de workspace)
       → qemu -cdrom <ISO resuelto> -drive disk.img
```

**Rebuild sin enroll (`make machine-update-kernel`):**

```text
  solo reconstruye ISO workspace
       → poweron sigue con el Default viejo hasta kmang i / kernel-manager-install
```

## 4. Responsabilidades

- kmang **debe** rechazar traversal de ids, mismatch build/id, mismatch arch/ELF,
  y colision de id con bytes distintos.
- kmang **debe** mantener Default/Fallback bootables via `resolve()` o fallar en claro.
- kmang **debe** rotular `#N` como local en ayuda TUI y JSON de `compare`.
- El operador **debe** ejecutar `i` tras rebuild si quiere que `poweron` use la imagen nueva.
- `machine-update-kernel` **no debe** auto-enrollar (contrato de preservacion del disco).

## 5. Limites del subsistema

- kmang **no** repacka el rootfs completo — solo `etc/ir0-session`
  opcional antes de un boot pedido desde la TUI.
- kmang **no** reemplaza gestion de paquetes ISD (`userspace_manager.py`).
- kmang **no** lanza QEMU; `poweron` consume solo la salida de `resolve`.
- Tests host: `scripts/test_kernel_manager.py`; gate en `make isd-contracts`.

## 6. Relacion con otros subsistemas

| Subsistema | Relacion |
|------------|----------|
| `scripts/make/isd.mk` | `KMANG_PY`, `make kmang`, `kernel-manager-*`, `poweron` |
| ISD / userspace | Disco rootfs separado del catalogo kernel |
| `make usmang` | Inspector host complementario para userland ISD |
| Boot / version | `IR0_VERSION_STRING` prefija ids de catalogo |
| KTM | Plano de prueba in-guest independiente |

Ver tambien: `IR0-userspace`, `IR0-onboarding`, `Documentation/TOOLING.md`.

## 7. Mapas visuales

```text
  Arbol workspace                   Store maquina persistente
  ---------------                   -------------------------
  kernel-x64-userspace.iso          kernels/
  .build_number (N local)    i/r     ├── 0.0.1-pre-rc3-build42.iso
                                    ├── 0.0.1-pre-rc3-build42.json
                                    kernel-current.iso ──► build42.iso  (Default)
                                    kernel-fallback.iso ─► build41.iso  (Fallback)
                                    disk.img              (kmang no lo toca)

  make poweron ──► resolve() ──► qemu -cdrom ISO Default
```

## 8. Invariantes importantes

| Invariante | Motivo |
|------------|--------|
| Default ≠ Workspace sin enroll | Evita boot silencioso de builds locales no revisados |
| Fallback sobrevive a select | Rollback con Enter sobre entrada anterior verificada |
| `ir0_build_number` es SoT de `#N` | Sufijo id debe coincidir con simbolo ELF |
| SHA-256 es SoT entre hosts | Dos `build42` pueden tener bytes distintos |
| No borrar Current/Fallback | Evita catalogo vacio e irrecoverable |
| Lock de escritor unico | Segunda TUI/CLI devuelve exit 2 |

## 9. Depuracion

| Sintoma | Comprobar |
|---------|-----------|
| `poweron` arranca kernel viejo | `make kmang` → linea compare; `i` o `kernel-manager-install` |
| kernels instalados pero ninguno verifica | `KMANG_PY verify`; re-enroll o Fallback |
| `identity mismatch` / `collision` | Rebuild cambio bytes; nuevo `-buildN` o borrar entrada |
| otro kernel manager activo | Cerrar TUI; lock stale solo si no hay proceso |
| Olvidaste una tecla TUI | **`h`**, **`?`** o **F1** dentro de `make kmang`; leyenda compacta abajo |

**Leyenda TUI (completa):** overlay agrupado con `h`/`?`/F1; ↑↓ si el terminal es bajo.
Copia no interactiva: `python3 scripts/kernel_manager.py … help`.

**CLI:**

```bash
make kernel-manager-list
make kmang-cli
make kernel-manager-install
python3 scripts/kernel_manager.py --machine-dir … compare
python3 scripts/kernel_manager.py --machine-dir … info ID --json
python3 scripts/kernel_manager.py --machine-dir … help
```

**Tests:** `python3 scripts/test_kernel_manager.py` (tambien `make kmang-test`, `make isd-contracts`).

## 10. Roadmap

- Perfiles arm64 de catalogo cuando existan maquinas persistentes arm64 en producto
  (verify ya extrae payloads arm64; camino principal sigue siendo x86_64).
- Prompt opcional post `machine-update-kernel` (hoy manual a proposito).
- Manifiesto `IR0-system` unificando SHA Default + version ISD.

## Build

```bash
make sync-mandocs
make man TOPIC=kmang
```
