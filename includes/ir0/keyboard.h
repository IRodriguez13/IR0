/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef _IR0_KEYBOARD_H
#define _IR0_KEYBOARD_H

/* Keyboard buffer functions */
extern char keyboard_buffer_get(void);
extern int keyboard_buffer_has_data(void);

#endif /* _IR0_KEYBOARD_H */