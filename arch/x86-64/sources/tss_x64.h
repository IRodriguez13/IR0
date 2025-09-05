#pragma once
#include <stdint.h>

// TSS structure for x86-64
typedef struct
{
    uint32_t reserved0;
    uint64_t rsp0; // Kernel stack pointer for Ring 0
    uint64_t rsp1; // Kernel stack pointer for Ring 1
    uint64_t rsp2; // Kernel stack pointer for Ring 2
    uint64_t reserved1;
    uint64_t ist1; // Interrupt stack table 1
    uint64_t ist2; // Interrupt stack table 2
    uint64_t ist3; // Interrupt stack table 3
    uint64_t ist4; // Interrupt stack table 4
    uint64_t ist5; // Interrupt stack table 5
    uint64_t ist6; // Interrupt stack table 6
    uint64_t ist7; // Interrupt stack table 7
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset; // I/O permission bitmap offset
} __attribute__((packed)) tss_t;

void tss_init_x64(void);
void tss_load_x64(void);
void setup_tss();
uint64_t tss_get_address(void);
uint32_t tss_get_size(void);
