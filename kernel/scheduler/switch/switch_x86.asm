; ===============================================================================
; switch_task - Cambio de contexto entre procesos
; ===============================================================================
;
; Como toda funcion que usa instrucciones privilegiadas, va en asm. En este caso cli y sti 
; El tema aca es: primero cargamos las structs del alto nivel en eax y ebx respectivamente, despues
; despues "guardamos" los datos de las tareas de la struct 1 y usamos el cr3 para que apunte 
; al directorio de paginacion de la segunda tarea.
; despues cargamos esp y ebp la segunda tarea. y saltamos ahí en el alto nivel. 
;

; kernel/scheduler/switch/switch.asm - CORREGIDO
BITS 32
global switch_task
switch_task:
    cli
    mov eax, [esp + 4]  ; old_task
    mov ebx, [esp + 8]  ; new_task
    
    ; Guardar TODO el contexto
    pushfd              ; EFLAGS
    pusha              ; Todos los registros de propósito general
    
    ; Guardar ESP en old_task
    mov [eax + 4], esp
    
    ; Cambiar a new_task
    mov esp, [ebx + 4]  ; Cargar nuevo stack
    
    ; Solo cambiar CR3 si no es 0 (kernel page directory)
    mov ecx, [ebx + 16] ; Cargar CR3
    cmp ecx, 0          ; ¿Es 0 (kernel page directory)?
    je skip_cr3_change  ; Si es 0, no cambiar CR3
    mov cr3, ecx        ; Cambiar espacio de memoria
skip_cr3_change:
    
    ; Restaurar contexto de new_task
    popa               ; Restaurar registros
    popfd             ; Restaurar flags
    
    sti
    ret               ; Retornar al EIP que estaba en el stack