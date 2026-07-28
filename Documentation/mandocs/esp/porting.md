# Portar software a ISD (userspace IR0)

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T1 |
| Status | stable |
| Depends on | userspace, syscalls, tty, vfs |
| Man page | IR0-port (sección 7) |
| Primary sources | `Documentation/USERSPACE.md`, `IR0-userspace/`, TinyCC en `/lib/tcc` |

> **Última verificación:** 2026-07-28

## 1. Resumen

**ISD** es la distribución de software de IR0: BusyBox + runit + herramientas
enlazadas con musl sobre rootfs MINIX. Esta página es una guía práctica para
traer un programa C pequeño (o un flujo BusyBox) al guest.

Fuera de alcance: escritorios glibc completos, ELF dinámico sin musl, o
paridad LTP.

## 2. Qué funciona hoy

| Capacidad | Notas |
|-----------|-------|
| musl / TinyCC estático | `tcc -B/lib/tcc -static -Os …` |
| ash + applets BusyBox | `busybox --list` |
| Archivos | MINIX v1; **nombres ≤14 caracteres** |
| Fuentes del kernel | `/heart/dennis/src` vía virtio-9p (`make run`) |
| Manuales | `man IR0-*` en `/usr/share/man/cat7/` |

## 3. Compilar en el guest

```text
tcc -B/lib/tcc -static -Os -o hello hello.c
./hello
```

Siempre `-B/lib/tcc` y preferí `-static`. Un `tcc a.c -o a` suelto puede
generar ELF dinámico y SEGV.

## 4. Traer un proyecto del host

```text
ls /heart/dennis/src/kernel/main.c
```

Stage1 monta el árbol IR0 del host (tag QEMU `dennis`) como root antes del
login. Si solo ves `hello.c`, falta el dispositivo 9p.

## 5. Expectativas ABI

Orientate al ABI Linux x86-64 / musl que IR0 implementa. Ante divergencias,
usá los contratos en `scripts/linux_abi/`.

## 6. Checklist

1. Compila con TinyCC estático (o musl-gcc en host + inject).
2. Corre bajo ash sin SEGV.
3. No exige APIs solo-glibc.
4. Rutas cortas en disco MINIX (o viví bajo 9p).
5. Documentá un smoke breve si entra al producto.

## 7. Ver también

`man IR0-uspace`, `man IR0-syscalls`, `man IR0-onboard`,
`Documentation/USERSPACE.md`.
