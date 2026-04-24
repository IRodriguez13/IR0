/**
 * IR0 Kernel - Input event queue for /dev/events0 (Linux evdev)
 * Single producer (keyboard IRQ) / single consumer (read syscall)
 */
#include <stddef.h>
#include <ir0/input.h>
#include <ir0/time.h>
#include <drivers/timer/clock_system.h>
#include <string.h>
#include <arch/common/arch_portable.h>

/* Ring buffer: 64 input_event entries */
#define INPUT_EVENT_QUEUE_SIZE 64
static struct input_event event_queue[INPUT_EVENT_QUEUE_SIZE];
static volatile unsigned int ev_head;
static volatile unsigned int ev_tail;

static inline uint64_t input_events_irq_save(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
#else
    arch_disable_interrupts();
    return 0;
#endif
}

static inline void input_events_irq_restore(uint64_t flags)
{
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
#else
    (void)flags;
    arch_enable_interrupts();
#endif
}

/* Called from keyboard IRQ handler - must be fast, no blocking */
void input_event_push(uint16_t type, uint16_t code, int32_t value)
{
    unsigned int next = (ev_head + 1) % INPUT_EVENT_QUEUE_SIZE;
    if (next == ev_tail)
        return;  /* Buffer full, drop event */

    uint64_t ms = clock_get_uptime_milliseconds();
    event_queue[ev_head].time.tv_sec = (time_t)(ms / 1000);
    event_queue[ev_head].time.tv_usec = (suseconds_t)((ms % 1000) * 1000);
    event_queue[ev_head].type = type;
    event_queue[ev_head].code = code;
    event_queue[ev_head].value = value;

    ev_head = next;
}

/* Returns number of events copied, 0 if none. Called from process context. */
size_t input_event_read(struct input_event *buf, size_t count)
{
    size_t n = 0;
    uint64_t irq_flags;

    if (!buf || count == 0)
        return 0;

    irq_flags = input_events_irq_save();
    while (n < count && ev_tail != ev_head)
    {
        buf[n] = event_queue[ev_tail];
        ev_tail = (ev_tail + 1) % INPUT_EVENT_QUEUE_SIZE;
        n++;
    }
    input_events_irq_restore(irq_flags);
    return n;
}

int input_event_has_data(void)
{
    return ev_tail != ev_head;
}
