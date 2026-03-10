/**
 * IR0 Kernel - Input event queue for /dev/events0 (Linux evdev)
 * Single producer (keyboard IRQ) / single consumer (read syscall)
 */
#include <stddef.h>
#include <ir0/input.h>
#include <ir0/time.h>
#include <drivers/timer/clock_system.h>
#include <string.h>

/* Ring buffer: 64 input_event entries */
#define INPUT_EVENT_QUEUE_SIZE 64
static struct input_event event_queue[INPUT_EVENT_QUEUE_SIZE];
static volatile unsigned int ev_head;
static volatile unsigned int ev_tail;

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
    while (n < count && ev_tail != ev_head)
    {
        buf[n] = event_queue[ev_tail];
        ev_tail = (ev_tail + 1) % INPUT_EVENT_QUEUE_SIZE;
        n++;
    }
    return n;
}

int input_event_has_data(void)
{
    return ev_tail != ev_head;
}
