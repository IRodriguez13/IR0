/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * AAPCS64 handlers return through x30, so no return slot sits below the
 * sigframe and the handler SP stays 16-byte aligned.
 */
#define IR0_ARCH_SIGFRAME_RETURN_SLOT 0UL
