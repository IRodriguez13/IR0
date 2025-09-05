#pragma once
// GDT entry (8 bytes)
struct gdt_entry
{
    uint16_t limit;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t gran;
    uint8_t base_high;
} __attribute__((packed));

// TSS descriptor (16 bytes + 8 extra)
struct gdt_tss_entry
{
    uint16_t limit;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t gran;
    uint8_t base_high;
    uint32_t base_long;
    uint32_t reserved;
} __attribute__((packed));

// GDTR structure
struct gdtr
{
    uint16_t size;
    uint64_t offset;
} __attribute__((packed));

// GDT table
struct gdt_table_struct
{
    struct gdt_entry entries[5];
    struct gdt_tss_entry tss_entry;
} __attribute__((packed));

extern struct gdt_table_struct gdt_table;

extern struct gdtr gdt_descriptor;

void update_gdt_tss(uint64_t tss_addr);

void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

void gdt_install();

void gdt_set_tss(uint64_t base, uint16_t limit);
