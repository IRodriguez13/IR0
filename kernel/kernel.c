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
    print("                 IR0 Kernel v0.0.0 pre-rc1 Boot sequence\n");

    // Initialize logging first
    logging_init();
    log_subsystem_ok("INIT");
    delay_ms(1000);

    // Initialize subsystems
    log_subsystem_ok("GDT_TSS");
    delay_ms(1000);

    // Initialize PS/2 controller
    ps2_init();
    log_subsystem_ok("PS2");
    delay_ms(1000);

    // Initialize keyboard
    extern void keyboard_init(void);
    keyboard_init();
    pic_unmask_irq(1);  // Enable IRQ1 (keyboard)
    log_subsystem_ok("KEYBOARD DRIVER");
    delay_ms(1000);

    // Initialize memory
    extern void simple_alloc_init(void);
    simple_alloc_init();
    log_subsystem_ok("MEMORY SUBS");
    delay_ms(1000);

    // Initialize storage
    ata_init();
    log_subsystem_ok("ATA STORAGE DRIVER");
    delay_ms(1000);

    // Mount filesystem
    extern int vfs_init_with_minix(void);
    vfs_init_with_minix();
    log_subsystem_ok("MINIX FILESYSTEM");
    delay_ms(1000);

    // Initialize process management
    extern void process_init(void);
    process_init();
    log_subsystem_ok("PROCESS SUBS");
    delay_ms(1000);

    // Initialize system clock
    extern int clock_system_init(void);
    clock_system_init();
    log_subsystem_ok("CLOCK SUBS");
    delay_ms(2500);

    // Initialize scheduler
    extern int scheduler_cascade_init(void);
    scheduler_cascade_init();
    log_subsystem_ok("SCHED SUBS");
    delay_ms(2500);

    // Initialize system calls
    extern void syscalls_init(void);
    syscalls_init();
    log_subsystem_ok("SYSCALLS SUBS");
    delay_ms(2500);

    // Set up interrupt handlers
#ifdef __x86_64__
    extern void idt_init64(void);
    extern void idt_load64(void);
    idt_init64();
    idt_load64();
    pic_remap64();
    log_subsystem_ok("INTERRUPTS ON");
    delay_ms(2500);
#endif

    // Start init process (PID 1) and switch to Ring 3
    extern int start_init_process(void);
    extern void init_proc_1(void);
    extern void switch_to_user_mode(void *entry_point);
    
    start_init_process();
    log_subsystem_ok("INIT_PROC1 STARTING");
    delay_ms(2500);

    print("\nSwitching to user mode...\n");
    delay_ms(4500);
    
    __asm__ volatile("sti");
    switch_to_user_mode((void*)init_proc_1);

    // Should never return
    for (;;) __asm__ volatile("hlt");
}