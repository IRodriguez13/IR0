// kernel.c -  kernel initialization routine
#include <ir0/print.h>
#include <ir0/logging.h>
#include <ir0/panic/panic.h>
#include <string.h>
#include <drivers/IO/ps2.h>
#include <drivers/storage/ata.h>
#include <interrupt/arch/pic.h>
#include <memory/allocator.h>

// Main kernel entry point from boot
// Simple delay function
static void kernel_delay(void) {
    for (volatile int i = 0; i < 10000000; i++);
}

void kmain_x64(void)
{
    // CRITICAL: Initialize GDT and TSS FIRST
    extern void gdt_install(void);
    extern void setup_tss(void);
    gdt_install();
    setup_tss();
    
    // Banner
    print("IR0 Kernel v0.0.1\n");
    print("Booting...\n\n");

    // Initialize subsystems with Linux-style messages
    print("Initializing GDT and TSS...                    [ OK ]\n");
    kernel_delay();

    extern void logging_init(void);
    print("Starting logging subsystem...                  ");
    logging_init();
    print("[ OK ]\n");
    kernel_delay();

    print("Initializing PS/2 controller...                ");
    ps2_init();
    print("[ OK ]\n");
    kernel_delay();

    extern void keyboard_init(void);
    print("Loading keyboard driver...                     ");
    keyboard_init();
    pic_unmask_irq(1);
    print("[ OK ]\n");
    kernel_delay();

    extern void simple_alloc_init(void);
    print("Initializing memory allocator...               ");
    simple_alloc_init();
    print("[ OK ]\n");
    kernel_delay();

    print("Detecting storage devices...                   ");
    ata_init();
    print("[ OK ]\n");
    kernel_delay();

    extern int vfs_init_with_minix(void);
    print("Mounting filesystem...                         ");
    vfs_init_with_minix();
    print("[ OK ]\n");
    kernel_delay();

    extern void process_init(void);
    print("Initializing process management...             ");
    process_init();
    print("[ OK ]\n");
    kernel_delay();

    extern int clock_system_init(void);
    print("Starting system clock...                       ");
    clock_system_init();
    print("[ OK ]\n");
    kernel_delay();

    extern int scheduler_cascade_init(void);
    print("Loading task scheduler...                      ");
    scheduler_cascade_init();
    print("[ OK ]\n");
    kernel_delay();

    extern void syscalls_init(void);
    print("Registering system calls...                    ");
    syscalls_init();
    print("[ OK ]\n");
    kernel_delay();

#ifdef __x86_64__
    extern void idt_init64(void);
    extern void idt_load64(void);
    print("Setting up interrupt handlers...               ");
    idt_init64();
    idt_load64();
    pic_remap64();
    print("[ OK ]\n");
    kernel_delay();
#endif

    // Start init process (PID 1) and switch to Ring 3
    extern int start_init_process(void);
    extern void init_proc_1(void);
    extern void switch_to_user_mode(void *entry_point);
    
    print("Creating init process...                       ");
    start_init_process();
    print("[ OK ]\n");
    kernel_delay();

    print("\nSwitching to user mode...\n");
    kernel_delay();
    
    __asm__ volatile("sti");
    switch_to_user_mode((void*)init_proc_1);

    // Should never return
    panic("Kernel halted unexpectedly");
}
