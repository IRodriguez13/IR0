#ifndef _IR0_CONFIG_H
#define _IR0_CONFIG_H

/* 
 * KERNEL_DEBUG_SHELL
 * 1: Use the built-in kernel shell (init_1)
 * 0: Load and execute /bin/init from the filesystem
 */

/* In 1 the kernel will use the built-in kernel shell (init_1), in 0 it will load and execute /bin/init from the filesystem */
#define KERNEL_DEBUG_SHELL 1

#endif /* _IR0_CONFIG_H */
