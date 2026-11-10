<!-- IR0 AI dev rule: ir0-scale-within-patterns -->
<!-- alwaysApply: true -->
<!-- description: IR0 sigue creciendo, pero todo código nuevo entra con los patrones del núcleo común y con gates automáticos -->

# IR0 — Crecer dentro de los patrones

IR0 no congela el alcance: se siguen agregando subsistemas, drivers e ISAs. La
condición es que **todo código nuevo entre ya con los patrones correctos** y que
esos patrones los haga cumplir un gate, no la memoria de quien revisa.

Complementa `ir0-api-public-private.md`, `kernel-architecture-rigor.md`,
`ir0-arm64-rpi5-x86-regression-gate.md` e `ir0-bugfix-red-green-proof.md`.

## Orden de trabajo

1. **Núcleo común primero.** Antes de abrir un frente nuevo, el camino crítico que
   va a usar tiene que estar en el modelo correcto: bloqueo en pila de kernel con
   wait queues (nada de polling para despertar tareas), locking real en
   estructuras compartidas, validación de usuario dentro de la operación, señales
   y switch al modelo Linux.
2. **Abstraído de la ISA.** El núcleo común no conoce registros, segmentos, frames
   de hardware ni `#if` por ISA. Cada ISA implementa hooks chicos detrás de una
   fachada con nombre simple (`switch_to`, `switch_mm`, `flush_icache_range`).
3. **Recién entonces, crecer.** Un subsistema o driver nuevo se apoya en esas
   fachadas; si necesita algo que el núcleo no ofrece, primero se agrega al núcleo
   (con su prueba), después se usa.

## Condiciones de entrada para código nuevo

Un subsistema, driver o syscall nuevo entra a `dev` sólo si:

- **API mínima:** una función pública por comportamiento, nombre simple; lo demás
  `static` o privado del subsistema. La operación valida sus argumentos por dentro.
- **Sin ISA en código portable:** ni registros, ni `#if` por ISA/config dentro de
  caminos críticos; la configuración elige archivos o stubs de header.
- **Diagnóstico aparte:** nada de `klog`/KTM en línea en código de política; un
  hook de diagnóstico del subsistema en su propio archivo.
- **Dominio correcto:** el archivo vive en el directorio de su subsistema; las
  APIs públicas en `includes/ir0/`; cada archivo incluye sólo lo que usa, una vez.
- **Pruebas en ambas ISAs:** selftest KTAP (o ktest/host test) que corre en
  x86_64 y arm64; si hay ABI de Linux involucrada, el caso de conformidad
  correspondiente.
- **Gates verdes:** `make test-fast`, `arch-guard`, `kernel-tests`, `ctr`,
  `ir0 arm64` y los selftests de ambas ISAs.

## Los patrones se hacen cumplir con guards

Cada criterio que se repite en una revisión se convierte en un chequeo de
`scripts/architecture_guard.py` (o del guard que corresponda) con su auto-test de
violación inyectada: nombres retirados, registros fuera de `arch/`, `#if` en
caminos críticos, includes duplicados o tardíos, headers que sólo reexportan.
Un hallazgo de revisión que no tiene guard es deuda: se anota y se automatiza.

## Medir, no suponer

- **Conformidad:** un subconjunto de LTP y los selftests KTAP comparan el
  comportamiento con Linux en ambas ISAs.
- **Fuzzing acotado de syscalls:** corre con tiempo y memoria limitados y cada
  hallazgo se convierte en un selftest rojo antes del fix.

## SMP y optimización

SMP y la optimización del scheduler llegan **después** de que el núcleo común
cumpla lo anterior. Todo lo multi-core vive en `smp/`; el núcleo del scheduler
usa interfaces simples (por ejemplo el lock del runqueue) que `smp/` implementa.
