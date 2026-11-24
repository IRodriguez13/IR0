/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — input facade for /dev/events0 provider.
 */

#include <ir0/input.h>
#include <ir0/errno.h>
#include <ir0/input_backend.h>
#include <config.h>
#include <string.h>

void ir0_input_get_caps(struct ir0_input_caps *caps)
{
    if (!caps)
        return;

    memset(caps, 0, sizeof(*caps));
    caps->keyboard = 1;
    caps->mouse = input_mouse_is_available() ? 1 : 0;
    caps->has_events0 = 1;
#if CONFIG_INIT_PS2_CONTROLLER
    caps->keyboard_ps2 = 1;
#endif
    caps->mouse_ps2 = input_mouse_is_available() ? 1 : 0;
    caps->supports_key_up_down = 1;
    caps->supports_mouse_motion = input_mouse_is_available() ? 1 : 0;
}

int ir0_input_inject_event(uint16_t type, uint16_t code, int32_t value)
{
#if CONFIG_TEST_INPUT_INJECT
    input_event(input_i8042_dev(), type, code, value);
    return 0;
#else
    (void)type;
    (void)code;
    (void)value;
    return -ENOSYS;
#endif
}
