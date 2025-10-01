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
// delay_ms is already available from print.h

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

    // Initialize logging first
    logging_init();
    log_subsystem_ok("INIT");
    delay_ms(2500);

    // Initialize subsystems
    log_subsystem_ok("GDT_TSS");
    delay_ms(2500);

    // Initialize PS/2 controller
    ps2_init();
    log_subsystem_ok("PS2");
    delay_ms(2500);

    // Initialize keyboard
    extern void keyboard_init(void);
    keyboard_init();
    pic_unmask_irq(1);  // Enable IRQ1 (keyboard)
    log_subsystem_ok("KEYBOARD");
    delay_ms(2500);

    // Initialize memory
    extern void simple_alloc_init(void);
    simple_alloc_init();
    log_subsystem_ok("MEMORY");
    delay_ms(2500);

    // Initialize storage
    ata_init();
    log_subsystem_ok("STORAGE");
    delay_ms(2500);

    // Mount filesystem
    extern int vfs_init_with_minix(void);
    vfs_init_with_minix();
    log_subsystem_ok("FILESYSTEM");
    delay_ms(2500);

    // Initialize process management
    extern void process_init(void);
    process_init();
    log_subsystem_ok("PROCESS");
    delay_ms(2500);

    // Initialize system clock
    extern int clock_system_init(void);
    clock_system_init();
    log_subsystem_ok("CLOCK");
    delay_ms(2500);

    // Initialize scheduler
    extern int scheduler_cascade_init(void);
    scheduler_cascade_init();
    log_subsystem_ok("SCHEDULER");
    delay_ms(2500);

    // Initialize system calls
    extern void syscalls_init(void);
    syscalls_init();
    log_subsystem_ok("SYSCALLS");
    delay_ms(2500);

    // Set up interrupt handlers
#ifdef __x86_64__
    extern void idt_init64(void);
    extern void idt_load64(void);
    idt_init64();
    idt_load64();
    pic_remap64();
    log_subsystem_ok("INTERRUPTS");
    delay_ms(2500);
#endif

    // Start init process (PID 1) and switch to Ring 3
    extern int start_init_process(void);
    extern void init_proc_1(void);
    extern void switch_to_user_mode(void *entry_point);
    
    start_init_process();
    log_subsystem_ok("INIT_PROCESS");
    delay_ms(2500);

    print("\nSwitching to user mode...\n");
    delay_ms(2500);
    
    __asm__ volatile("sti");
    switch_to_user_mode((void*)init_proc_1);

    // Should never return
    for (;;) __asm__ volatile("hlt");
}