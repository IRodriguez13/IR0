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

global switch_task

switch_task:
    cli
    mov eax, [esp + 4]              ; old_task
    mov ebx, [esp + 8]              ; new_task
    
    ; Guardar contexto ANTES de modificar registros
    pushfd                          ; Guardar flags
    pusha                           ; Guardar todos los registros
    
    mov [eax + 4], esp              ; old_task->esp (ahora sí es correcto)
    mov [eax + 8], ebp              ; old_task->ebp
    
    ; Cambiar espacio de memoria
    mov ecx, [ebx + 16]             ; new_task->cr3
    mov cr3, ecx
    
    ; Restaurar contexto nuevo
    mov esp, [ebx + 4]              ; new_task->esp
    mov ebp, [ebx + 8]              ; new_task->ebp
    
    popa                            ; Restaurar registros
    popfd                           ; Restaurar flags
    sti
    
    jmp dword [ebx + 12]            ; new_task->eip