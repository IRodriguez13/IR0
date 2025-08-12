;
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

global switch_task

switch_task:
    cli
    
    ; Cargar parámetros
    mov eax, [esp + 4]              ; old_task  
    mov ebx, [esp + 8]              ; new_task
    
    ; Guardar estado del proceso actual
    pushfd                          ; Guardar flags
    pusha                           ; Guardar todos los registros generales
    
    ; Guardar ESP y EBP en la estructura del proceso actual  
    mov [eax + 4], esp              ; old_task->esp
    mov [eax + 8], ebp              ; old_task->ebp
    
    ; Cambiar espacio de direcciones
    mov ecx, [ebx + 16]             ; new_task->cr3
    mov cr3, ecx                    ; Cargar nuevo page directory
    
    ; Restaurar estado del nuevo proceso
    mov esp, [ebx + 4]              ; new_task->esp
    mov ebp, [ebx + 8]              ; new_task->ebp
    
    ; Restaurar registros y flags
    popa                            ; Restaurar registros generales
    popfd                           ; Restaurar flags
    
    sti
    ret                             ; ⚠️  CAMBIO: usar 'ret' en lugar de 'jmp'