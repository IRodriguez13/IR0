/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_credential_saved_ids.c
 * Description: Focused host regression for Linux setreuid/setregid saved IDs
 */

#include "test_harness.h"
#include <ir0/cred_transition.h>

static int permanent_drop_cannot_regain_root(uint32_t real)
{
	ir0_cred_id_triplet_t ids;

	ids.real = real;
	ids.effective = 0;
	ids.saved = 0;
	if (ir0_cred_setreid(&ids, real, real, 1) != 0)
		return 1;
	if (ids.real != real || ids.effective != real || ids.saved != real)
		return 2;
	if (ir0_cred_setreid(&ids, UINT32_MAX, 0, 0) != -EPERM)
		return 3;
	if (ids.effective != real || ids.saved != real)
		return 4;
	return 0;
}

static int temporary_drop_preserves_saved_root(void)
{
	ir0_cred_id_triplet_t ids;

	ids.real = 1000;
	ids.effective = 0;
	ids.saved = 0;
	if (ir0_cred_setreid(&ids, UINT32_MAX, 1000, 1) != 0)
		return 1;
	if (ids.effective != 1000 || ids.saved != 0)
		return 2;
	if (ir0_cred_setreid(&ids, UINT32_MAX, 0, 0) != 0)
		return 3;
	if (ids.effective != 0 || ids.saved != 0)
		return 4;
	return 0;
}

static int unprivileged_real_id_rejects_saved_id(void)
{
	ir0_cred_id_triplet_t ids;

	ids.real = 1000;
	ids.effective = 1000;
	ids.saved = 0;
	if (ir0_cred_setreid(&ids, 0, UINT32_MAX, 0) != -EPERM)
		return 1;
	if (ids.real != 1000 || ids.effective != 1000 || ids.saved != 0)
		return 2;
	return 0;
}

void test_credential_saved_ids(void)
{
	int ret;

	TEST_BEGIN("credential_saved_ids");
	ret = permanent_drop_cannot_regain_root(1000);
	ASSERT(ret == 0);

	/* The generic transition is shared by setregid. */
	ret = permanent_drop_cannot_regain_root(100);
	ASSERT(ret == 0);

	ret = temporary_drop_preserves_saved_root();
	ASSERT(ret == 0);

	ret = unprivileged_real_id_rejects_saved_id();
	ASSERT(ret == 0);
	TEST_END();
}
