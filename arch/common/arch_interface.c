#include "arch_interface.h"
#include <arch/common/arch_portable.h>
#include <ir0/oops.h>
#include <string.h>

// Detect MinGW-w64 cross-compilation
#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN32)
    #define MINGW_BUILD 1
#else
    #define MINGW_BUILD 0
#endif

// Implementaciones específicas de arquitectura
void arch_enable_interrupts(void)
{
#if defined(__x86_64__) || defined(__i386__)
    #if MINGW_BUILD
        __asm__ __volatile__("sti" ::: "memory");
    #else
        __asm__ volatile("sti");
    #endif
#elif defined(__aarch64__)
    // ARM64: msr daifclr, #2
    __asm__ volatile("msr daifclr, #2" ::: "memory");
#endif
}

void arch_disable_interrupts(void)
{
#if defined(__x86_64__) || defined(__i386__)
    #if MINGW_BUILD
        __asm__ __volatile__("cli" ::: "memory");
    #else
        __asm__ volatile("cli");
    #endif
#elif defined(__aarch64__)
    // ARM64: msr daifset, #2
    __asm__ volatile("msr daifset, #2" ::: "memory");
#endif
}

uint8_t inb(uint16_t port)
{
#if defined(__x86_64__) || defined(__i386__)
    uint8_t result;
    #if MINGW_BUILD
        __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port) : "memory");
    #else
        asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    #endif
    return result;
#elif defined(__aarch64__)
    // ARM no tiene inb
    return 0;
#endif
}

uintptr_t read_fault_address(void)
{
#if defined(__x86_64__) || defined(__i386__)
    #if MINGW_BUILD && defined(__x86_64__)
        // MinGW-w64 requires explicit 64-bit register constraint
        uint64_t addr64;
        __asm__ __volatile__("mov %%cr2, %q0" : "=r"(addr64) : : "memory");
        return (uintptr_t)addr64;
    #elif defined(__x86_64__)
        uintptr_t addr;
        asm volatile("mov %%cr2, %0" : "=r"(addr));
        return addr;
    #else
        uintptr_t addr;
        asm volatile("mov %%cr2, %0" : "=r"(addr));
        return addr;
    #endif
#elif defined(__aarch64__)
    // ARM64: leer FAR_EL1 register
    uint64_t addr;
    asm volatile("mrs %0, far_el1" : "=r"(addr));
    return addr;
#else
    return 0;
#endif
}

const char *arch_get_name(void)
{
#if defined(__x86_64__)
    return "x86-64 (amd64)";
#elif defined(__i386__)
    return "x86-32 (i386)";
#elif defined(__aarch64__)
    return "ARM64 (aarch64)";
#elif defined(__arm__)
    return "ARM32";
#else
    return "Unknown Architecture";
#endif
}

/**
 * Execute CPUID instruction
 * @leaf: CPUID leaf (eax input)
 * @subleaf: CPUID subleaf (ecx input)
 * @eax: Output for EAX register
 * @ebx: Output for EBX register
 * @ecx: Output for ECX register
 * @edx: Output for EDX register
 */
static inline void cpuid(uint32_t leaf, uint32_t subleaf,
                         uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx)
{
#if defined(__x86_64__) || defined(__i386__)
    #if MINGW_BUILD
        __asm__ __volatile__("cpuid"
                            : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                            : "a"(leaf), "c"(subleaf)
                            : "memory");
    #else
        asm volatile("cpuid"
                    : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                    : "a"(leaf), "c"(subleaf));
    #endif
#else
    /* Non-x86 architectures */
    if (eax) *eax = 0;
    if (ebx) *ebx = 0;
    if (ecx) *ecx = 0;
    if (edx) *edx = 0;
#endif
}

/**
 * Get CPU ID (APIC ID)
 * Returns the current CPU's APIC ID using CPUID
 */
uint32_t arch_get_cpu_id(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t eax, ebx, ecx, edx;
    
    /* Check if CPUID supports leaf 0x1 (basic info) */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 1)
    {
        /* Get CPUID leaf 0x1 - contains APIC ID in bits 31:24 of EBX */
        cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        return (ebx >> 24) & 0xFF;
    }
#endif
    /* Default to 0 if CPUID not available or not x86 */
    return 0;
}

/**
 * Get number of CPUs
 * For now, detects if HT/SMT is enabled and logical cores
 * Full SMP detection would require ACPI MADT parsing
 */
uint32_t arch_get_cpu_count(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t eax, ebx, ecx, edx;
    
    /* Check if CPUID supports leaf 0x1 */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 1)
    {
        /* Get CPUID leaf 0x1 */
        cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        
        /* Check if HT/SMT is supported */
        if (edx & (1 << 28))  /* HTT bit */
        {
            /* Get number of logical processors per package */
            /* Bits 23:16 of EBX contain maximum number of addressable IDs */
            uint32_t max_logical_cores = (ebx >> 16) & 0xFF;
            if (max_logical_cores > 0)
                return max_logical_cores;
        }
    }
    
    /* Check if CPUID supports leaf 0xB (extended topology) */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0xB)
    {
        /* Try to get logical processor count from leaf 0xB */
        cpuid(0xB, 0, &eax, &ebx, &ecx, &edx);
        if (ebx != 0)
        {
            /* EBX contains number of logical processors at this level */
            return ebx & 0xFFFF;
        }
    }
#endif
    /* Default to 1 for single-core or non-x86 */
    return 1;
}

/**
 * Get CPU vendor string using CPUID
 * @vendor_buf: Buffer to store vendor string (must be at least 13 bytes)
 * Returns: 0 on success, -1 on failure
 */
int arch_get_cpu_vendor(char *vendor_buf)
{
#if defined(__x86_64__) || defined(__i386__)
    if (!vendor_buf)
        return -1;
    
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    
    /* Vendor string is in EBX, EDX, ECX (in that order) */
    memcpy(vendor_buf, &ebx, 4);
    memcpy(vendor_buf + 4, &edx, 4);
    memcpy(vendor_buf + 8, &ecx, 4);
    vendor_buf[12] = '\0';
    
    return 0;
#else
    /* Non-x86: return generic vendor */
    strncpy(vendor_buf, "Unknown", 8);
    return -1;
#endif
}

/**
 * Get CPU family, model, and stepping from CPUID
 * @family: Output for CPU family
 * @model: Output for CPU model
 * @stepping: Output for CPU stepping
 * Returns: 0 on success, -1 on failure
 */
int arch_get_cpu_signature(uint32_t *family, uint32_t *model, uint32_t *stepping)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t eax, ebx, ecx, edx;
    
    /* Check if CPUID supports leaf 0x1 */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 1)
    {
        /* Get CPUID leaf 0x1 - contains family/model/stepping in EAX */
        cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        
        if (family)
        {
            /* Extract family: bits 11:8, extended family: bits 27:20 */
            uint32_t base_family = (eax >> 8) & 0xF;
            uint32_t ext_family = (eax >> 20) & 0xFF;
            *family = (ext_family > 0) ? (base_family + ext_family) : base_family;
        }
        
        if (model)
        {
            /* Extract model: bits 7:4, extended model: bits 19:16 */
            uint32_t base_model = (eax >> 4) & 0xF;
            uint32_t ext_model = (eax >> 16) & 0xF;
            *model = (ext_model > 0) ? ((ext_model << 4) + base_model) : base_model;
        }
        
        if (stepping)
        {
            /* Extract stepping: bits 3:0 */
            *stepping = eax & 0xF;
        }
        
        return 0;
    }
#endif
    /* Fallback values */
    if (family) *family = 0;
    if (model) *model = 0;
    if (stepping) *stepping = 0;
    return -1;
}

void outb(uint16_t port, uint8_t value)
{
#if defined(__x86_64__) || defined(__i386__)
    #if MINGW_BUILD
        __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
    #else
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    #endif
#elif defined(__aarch64__)
    // ARM no tiene puertos I/O - usar MMIO
    // Implementar cuando sea necesario
#endif
}

void cpu_wait(void)
{
#if MINGW_BUILD
    __asm__ __volatile__("hlt" ::: "memory");
#else
    asm volatile("hlt");
#endif
}

// ===============================================================================
// FUNCIONES DE DIVISIÓN 64-BIT (para resolver referencias indefinidas)
// ===============================================================================

// Implementación simple de división unsigned 64-bit
uint64_t __udivdi3(uint64_t a, uint64_t b)
{
    if (b == 0)
    {
        // División por cero - en un kernel real esto debería panic
        panic("Prohibidísimo dividir por cero. Division by zero detected.");
        return 0;
    }

    uint64_t result = 0;
    uint64_t remainder = 0;

    // Algoritmo de división simple
    for (int i = 63; i >= 0; i--)
    {
        remainder = (remainder << 1) | ((a >> i) & 1);
        if (remainder >= b)
        {
            remainder -= b;
            result |= (1ULL << i);
        }
    }

    return result;
}