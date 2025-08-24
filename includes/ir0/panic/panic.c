#include "panic.h"
#include <ir0/print.h>

// IR0 Advanced Panic Handler
// Convertido a sintaxis Intel para mejor legibilidad

static const char *panic_level_names[] =
{
        "KERNEL BUG",
        "HARDWARE FAULT",
        "OUT OF MEMORY",
        "STACK OVERFLOW",
        "ASSERTION FAILED"
};


// Nos fijamos si estamos en doble panic
static volatile int in_panic = 0;

// Utilizamos un mejor manejo del stack-trace
void panic_advanced(const char *message, panic_level_t level, const char *file, int line)
{
    // Por si de casualidad también falla Panic (es el peor evento posible).
    if (in_panic)
    {
        // corto interrupciones directo de nuevo.
        asm volatile("cli");
        print_error("DOUBLE PANIC! System completely fucked.\n");
        cpu_relax();
        return;
    }

    in_panic = 1;

    // Cortamos las interrupciones inmediatamente.
    asm volatile("cli");

    // Limpio la pantalla
    clear_screen();

    // Header con timestamp pero para timer.
    print_colored("╔═══════════════════════════════════════════════════════════╗\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    print_colored("║                     KERNEL PANIC                          ║\n", VGA_COLOR_WHITE, VGA_COLOR_RED);
    print_colored("╚═══════════════════════════════════════════════════════════╝\n", VGA_COLOR_RED, VGA_COLOR_BLACK);

    print("\n");

    // Panic info básica
    print_colored("Type: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_error(panic_level_names[level]);
    print("\n");

    print_colored("Location: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print(file);
    print(":");
    print_hex_compact(line);
    print("\n");

    print_colored("Message: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_error(message);
    print("\n\n");

    // Me guardo una foto de los registros en el momento del panic
    dump_registers();

    // Stack trace
    dump_stack_trace();

    // informacion de la memoria
    dump_memory_info();

    // Mensaje antes del cpu_relax
    print_colored("\n═══ SYSTEM HALTED ═══\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("Safe to power off or reboot - Es seguro apagar o reiniciar el equipo.\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);

    cpu_relax(); // la cpu a hacer noni para que no haya mas problemas intrackeables.
}

void dump_registers()
{
#ifdef __x86_64__
    // Versión 64-bit
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rsp, rbp;
    uint64_t rflags;
    
    __asm__ volatile (
        "movq %%rax, %0\n"
        "movq %%rbx, %1\n"
        "movq %%rcx, %2\n"
        "movq %%rdx, %3\n"
        "movq %%rsi, %4\n"
        "movq %%rdi, %5\n"
        "movq %%rsp, %6\n"
        "movq %%rbp, %7\n"
        "pushfq\n"
        "popq %8\n"
        : "=m"(rax), "=m"(rbx), "=m"(rcx), "=m"(rdx),
          "=m"(rsi), "=m"(rdi), "=m"(rsp), "=m"(rbp), "=m"(rflags)
        :
        : "memory"
    );
    
    print_colored("--- REGISTER DUMP (64-bit) ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    print("RAX: "); print_hex64(rax); print("  ");
    print("RBX: "); print_hex64(rbx); print("\n");
    // ... resto de registros 64-bit
    
#else
    // Versión 32-bit (tu código actual)
    uint32_t eax, ebx, ecx, edx, esi, edi, esp, ebp;
    uint32_t eflags;
    
    __asm__ volatile (
        "movl %%eax, %0\n"
        "movl %%ebx, %1\n"
        "movl %%ecx, %2\n"
        "movl %%edx, %3\n"
        "movl %%esi, %4\n"
        "movl %%edi, %5\n"
        "movl %%esp, %6\n"
        "movl %%ebp, %7\n"
        "pushfl\n"
        "popl %8\n"
        : "=m"(eax), "=m"(ebx), "=m"(ecx), "=m"(edx),
          "=m"(esi), "=m"(edi), "=m"(esp), "=m"(ebp), "=m"(eflags)
        :
        : "memory"
    );
    
    print_colored("--- REGISTER DUMP (32-bit) ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    
    print("EAX: "); print_hex_compact(eax); print("  ");
    print("EBX: "); print_hex_compact(ebx); print("\n");
    
    print("ECX: "); print_hex_compact(ecx); print("  ");
    print("EDX: "); print_hex_compact(edx); print("\n");
    
    print("ESP: "); print_hex_compact(esp); print("  ");
    print("EBP: "); print_hex_compact(ebp); print("\n");
    
    print("EFLAGS: "); print_hex_compact(eflags); print("\n\n");
#endif
}

void dump_stack_trace()
{
    print_colored("--- STACK TRACE ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);

    #ifdef __x86_64__

        uint64_t *rbp;
        asm volatile("mov %%rbp, %0" : "=r"(rbp));
    #else
    
    uint32_t *ebp;
        asm volatile("movl %%ebp, %0" : "=r"(ebp));
    
       
    
        int frame_count = 0;
    const int max_frames = 10;

    while (ebp && frame_count < max_frames)
    {

        // Validar que ebp esté en rango de memoria válido osea entre 1mb y 1 gb
        if ((uint32_t)ebp < 0x100000 || (uint32_t)ebp > 0x40000000)
        {
            print_warning("Stack trace truncated (invalid frame pointer)\n");
            break;
        }

        uint32_t return_addr = *(ebp + 1);

        print("#");
        print_hex_compact(frame_count);
        print(": ");
        print_hex_compact(return_addr);
        print("\n");

        ebp = (uint32_t *)*ebp; // Siguiente marco de pila
        frame_count++;
    }

    if (frame_count == 0)
    {
        print_warning("No stack trace available\n");
    }

    print("\n");
 #endif
}

void dump_memory_info()
{
    print_colored("--- MEMORY INFO ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);

    // extern uint32_t free_pages_count, total_pages_count;  // Comentado - no existe en esta rama

    // uint32_t used_pages = total_pages_count - free_pages_count;  // Comentado - no existe en esta rama
    // uint32_t mem_usage_percent = (used_pages * 100) / total_pages_count;  // Comentado - no existe en esta rama

    print("Using bump_allocator only\n");
    // print("Total memory: ");
    // print_hex_compact(total_pages_count * 4096);  // Comentado - no existe en esta rama
    // print(" bytes\n");

    // print("Free memory: ");
    // print_hex_compact(free_pages_count * 4096);  // Comentado - no existe en esta rama
    // print(" bytes\n");

    // print("Memory usage: ");
    // print_hex_compact(mem_usage_percent);  // Comentado - no existe en esta rama
    // print("%\n\n");
}

// panic() original como wrapper. Así no tengo que reemplazarlo en cada llamado.
void panic(const char *message)
{
    panic_advanced(message, PANIC_KERNEL_BUG, "unknown", 0);
}

// cpu_relax - halt es una instruccion que corta cualquier ejecucion de la cpu y la incia en modo de bajo consumo, hasta la siguiente interrupcion
// en este caso, la cpu no entra nunca en ninguna interrupción y es eso lo que queremos.

void cpu_relax()
{
    for (;;)
    {
        asm volatile("hlt");
    }
}
