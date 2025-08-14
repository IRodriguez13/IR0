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
    mov eax, [esp + 4] ; old_task  
    mov ebx, [esp + 8] ; new_task
    
    ; Validar punteros
    test eax, eax
    jz   .no_old_task
    test ebx, ebx
    jz   .error
    
    ; Guardar estado
    pushfd
    pusha
    
    ; Guardar ESP/EBP en old_task
    mov [eax + 4], esp ; old_task->esp
    mov [eax + 8], ebp ; old_task->ebp
    
.no_old_task:
    ; Cargar nuevo estado
    mov esp, [ebx + 4]  ; new_task->esp
    mov ebp, [ebx + 8]  ; new_task->ebp
    mov ecx, [ebx + 16] ; new_task->cr3
    mov cr3, ecx
    
    ; Restaurar contexto
    popa
    popfd
    sti
    
    ; CRÍTICO: Saltar al EIP de la nueva tarea
    jmp [ebx + 12] ; new_task->eip
    
.error:
    sti
    ret