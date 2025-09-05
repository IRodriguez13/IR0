#include <stdint.h>
#include <string.h>
#include "tss_x64.h"
#include "gdt.h"

extern tss_t kernel_tss;

struct gdt_table_struct gdt_table;

struct gdtr gdt_descriptor;

void gdt_flush(struct gdtr *gdtr)
{
    asm volatile(
        "lgdt (%0)\n"
        "mov $0x28, %%ax\n" // TSS selector (index 5 << 3)
        "ltr %%ax\n"
        :
        : "r"(gdtr)
        : "rax", "memory");
}

void update_gdt_tss(uint64_t tss_addr)
{
    struct gdt_tss_entry *e = &gdt_table.tss_entry;

    uint16_t limit = sizeof(tss_t) - 1;

    e->limit = limit;
    e->base_low = tss_addr & 0xFFFF;
    e->base_mid = (tss_addr >> 16) & 0xFF;
    e->access = 0x89; // Present, Available TSS
    e->gran = 0x00;
    e->base_high = (tss_addr >> 24) & 0xFF;
    e->base_long = (tss_addr >> 32) & 0xFFFFFFFF;
    e->reserved = 0;
}

void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    struct gdt_entry *e = &gdt_table.entries[index];
    e->limit = limit & 0xFFFF;
    e->base_low = base & 0xFFFF;
    e->base_mid = (base >> 16) & 0xFF;
    e->access = access;
    e->gran = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    e->base_high = (base >> 24) & 0xFF;
}

void gdt_set_tss(uint64_t base, uint16_t limit)
{
    struct gdt_tss_entry *e = &gdt_table.tss_entry;
    e->limit = limit;
    e->base_low = base & 0xFFFF;
    e->base_mid = (base >> 16) & 0xFF;
    e->access = 0x89; // Present, Available TSS
    e->gran = 0x00;
    e->base_high = (base >> 24) & 0xFF;
    e->base_long = (base >> 32) & 0xFFFFFFFF;
    e->reserved = 0;
}

void gdt_install()
{
    gdt_set_entry(0, 0, 0, 0x00, 0x00); // Null
    gdt_set_entry(1, 0, 0, 0x9A, 0x20); // Kernel code
    gdt_set_entry(2, 0, 0, 0x92, 0x00); // Kernel data
    gdt_set_entry(3, 0, 0, 0xFA, 0x20); // User code
    gdt_set_entry(4, 0, 0, 0xF2, 0x00); // User data
    gdt_set_tss((uint64_t)&kernel_tss, sizeof(tss_t) - 1);

    gdt_descriptor.size = sizeof(gdt_table) - 1;
    gdt_descriptor.offset = (uint64_t)&gdt_table;

    gdt_flush(&gdt_descriptor);
}
