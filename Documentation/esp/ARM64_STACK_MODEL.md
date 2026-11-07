# Pila kernel ARM64 y SP_EL1 (producto)

> **Última verificación:** 2026-11-07  
> **Fuente de verdad:** `arch/arm64/asm/entry/vectors.S`, `arch/arm64/sources/arch_switch.c`  
> (`set_current_kernel_stack`), `arch/arm64/asm/boot/entry.S`, `sched/switch/arch_context_switch.c`  
> (`context_prepare_next` → `set_current_kernel_stack`).

## Por qué hay dos punteros de pila

- **SP_EL0**: pila de usuario cuando el hilo corre en EL0.
- **SP_EL1** (reservado si `SPSel` es 1): pila de kernel que usa la entrada por excepción  
  **desde** EL0 hacia EL1. Los vectores en `vectors.S` restan espacio para un marco fijo  
  **antes** de guardar los GPR.

El cambio de contexto cooperativo (`arm64_cpu_switch` en `switch_early.S`) usa el SP  
actual de EL1 (`mov sp, x2`), que debe coincidir con SP_EL1 cuando el kernel corre en  
EL1h con **`SPSel == 1`**.

## Reserva del marco de excepción

La ruta sync desde nivel inferior usa `exc_entry_frame`, que hace:

```text
sub sp, sp, #256
```

La primera excepción desde EL0 hacia EL1 necesita **al menos 256 bytes** de pila libre  
**por debajo** del valor SP_EL1 vigente en la entrada, con **alineación a 16 bytes**.  
`set_current_kernel_stack` programa:

```text
sp_el1 = (kstack_top - 256) & ~0xF
```

con `sp_el1 >= kstack_base` (si no, no se actualiza). Es más estricto que “top − 16”:  
deja margen para el prólogo del vector. `IR0_PROC_KSTACK_SIZE` (`config.h`) es  
suficiente para procesos normales.

La escritura usa `SPSel=0` para que `SP_EL1` sea el banco inactivo. Un  
`msr sp_el1` directo en EL1h sustituiría la pila kernel viva; el epílogo de C  
sacaría LR de la kstack vacía y retornaría a la dirección 0.

## Arranque: SPSel

El firmware puede dejar `SPSel` indefinido en EL1. El *stub* de entrada fuerza  
**`msr spsel, #1`** para que SP_EL1 sea el banco usado con la pila de arranque y  
coincida con el modelo del vector.

## Retorno SVC desde EL0

`arm64_exc_sync_lower` recibe un puntero al marco de 256 bytes. Para SVC, el manejador  
pone el valor de retorno en `frame[0]`; la macro del vector restaura registros y  
hace `eret`. No hay un `msr sp_el1` separado en ese retorno: el marco se reservó de  
SP_EL1 en la entrada.

## sysfs relacionado

Instantánea firmware/boot (tema aparte): [`../sysfs-firmware-ir0.md`](../sysfs-firmware-ir0.md).
