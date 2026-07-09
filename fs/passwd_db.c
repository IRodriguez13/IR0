/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: passwd_db.c
 * Description: IR0 — minimal /etc/passwd and /etc/group reader (VFS-backed)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/passwd.h>
#include <ir0/permissions.h>
#include <ir0/open_flags.h>
#include <ir0/kmem.h>
#include <fs/vfs.h>
#include <string.h>

static unsigned long parse_ulong10(const char *s)
{
	unsigned long v = 0;

	while (s && *s >= '0' && *s <= '9')
	{
		v = v * 10UL + (unsigned long)(*s - '0');
		s++;
	}
	return v;
}

static struct ir0_passwd_entry passwd_tab[IR0_PASSWD_MAX_ENTRIES];
static size_t passwd_count;
static struct ir0_group_entry group_tab[IR0_PASSWD_MAX_ENTRIES];
static size_t group_count;

static char *trim_line(char *s)
{
	char *end;

	while (*s == ' ' || *s == '\t')
		s++;
	end = s + strlen(s);
	while (end > s && (end[-1] == '\n' || end[-1] == '\r' ||
			   end[-1] == ' ' || end[-1] == '\t'))
		end--;
	*end = '\0';
	return s;
}

static int read_file_into_buf(const char *path, char **out, size_t *out_len)
{
	struct vfs_file *f = NULL;
	char *buf;
	size_t cap = 4096;
	size_t len = 0;
	int n;

	if (!path || !out || !out_len)
		return -1;

	buf = kmalloc(cap);
	if (!buf)
		return -1;

	if (vfs_open(path, IR0_O_RDONLY, 0, &f) != 0)
	{
		kfree(buf);
		return -1;
	}

	for (;;)
	{
		if (len + 256 > cap)
		{
			char *grown;
			size_t new_cap = cap * 2;

			grown = kmalloc(new_cap);
			if (!grown)
				break;
			memcpy(grown, buf, len);
			kfree(buf);
			buf = grown;
			cap = new_cap;
		}
		n = vfs_read(f, buf + len, cap - len);
		if (n <= 0)
			break;
		len += (size_t)n;
	}
	vfs_close(f);

	if (len == 0)
	{
		kfree(buf);
		return -1;
	}

	*out = buf;
	*out_len = len;
	return 0;
}

static void seed_builtin_accounts(void)
{
	passwd_count = 0;
	group_count = 0;

	memset(&passwd_tab[passwd_count], 0, sizeof(passwd_tab[0]));
	passwd_tab[passwd_count].uid = ROOT_UID;
	passwd_tab[passwd_count].gid = ROOT_GID;
	strncpy(passwd_tab[passwd_count].name, "root",
		sizeof(passwd_tab[passwd_count].name) - 1);
	passwd_count++;

	memset(&passwd_tab[passwd_count], 0, sizeof(passwd_tab[0]));
	passwd_tab[passwd_count].uid = USER_UID;
	passwd_tab[passwd_count].gid = USER_GID;
	strncpy(passwd_tab[passwd_count].name, "user",
		sizeof(passwd_tab[passwd_count].name) - 1);
	passwd_count++;

	memset(&group_tab[group_count], 0, sizeof(group_tab[0]));
	group_tab[group_count].gid = ROOT_GID;
	strncpy(group_tab[group_count].name, "root",
		sizeof(group_tab[group_count].name) - 1);
	group_count++;

	memset(&group_tab[group_count], 0, sizeof(group_tab[0]));
	group_tab[group_count].gid = USER_GID;
	strncpy(group_tab[group_count].name, "user",
		sizeof(group_tab[group_count].name) - 1);
	group_tab[group_count].members[0] = USER_UID;
	group_tab[group_count].nmembers = 1;
	group_count++;
}

static void parse_passwd_buf(char *buf, size_t len)
{
	char *line;
	char *cursor = buf;

	(void)len;
	passwd_count = 0;

	for (line = cursor; line && *line; )
	{
		char *nl = strchr(line, '\n');
		struct ir0_passwd_entry ent;
		char *fields[7];
		int nf = 0;
		char *tok;
		char *field_save = NULL;

		if (nl)
		{
			*nl = '\0';
			cursor = nl + 1;
		}
		else
			cursor = NULL;

		line = trim_line(line);
		if (!line[0] || line[0] == '#')
		{
			line = cursor;
			continue;
		}

		tok = strtok_r(line, ":", &field_save);
		while (tok && nf < 7)
		{
			fields[nf++] = tok;
			tok = strtok_r(NULL, ":", &field_save);
		}
		if (nf < 3)
		{
			line = cursor;
			continue;
		}

		memset(&ent, 0, sizeof(ent));
		strncpy(ent.name, fields[0], sizeof(ent.name) - 1);
		ent.uid = (uid_t)parse_ulong10(fields[2]);
		ent.gid = (gid_t)parse_ulong10(fields[3]);

		if (passwd_count < IR0_PASSWD_MAX_ENTRIES)
			passwd_tab[passwd_count++] = ent;

		line = cursor;
	}
}

static void parse_group_buf(char *buf, size_t len)
{
	char *line;
	char *cursor = buf;

	(void)len;
	group_count = 0;

	for (line = cursor; line && *line; )
	{
		char *nl = strchr(line, '\n');
		struct ir0_group_entry ent;
		char *fields[4];
		int nf = 0;
		char *tok;
		char *field_save = NULL;

		if (nl)
		{
			*nl = '\0';
			cursor = nl + 1;
		}
		else
			cursor = NULL;

		line = trim_line(line);
		if (!line[0] || line[0] == '#')
		{
			line = cursor;
			continue;
		}

		tok = strtok_r(line, ":", &field_save);
		while (tok && nf < 4)
		{
			fields[nf++] = tok;
			tok = strtok_r(NULL, ":", &field_save);
		}
		if (nf < 3)
		{
			line = cursor;
			continue;
		}

		memset(&ent, 0, sizeof(ent));
		strncpy(ent.name, fields[0], sizeof(ent.name) - 1);
		ent.gid = (gid_t)parse_ulong10(fields[2]);
		if (nf >= 4 && fields[3] && fields[3][0])
		{
			char *mem_save = NULL;
			char *mem = strtok_r(fields[3], ",", &mem_save);

			while (mem && ent.nmembers < IR0_PASSWD_MAX_ENTRIES)
			{
				ent.members[ent.nmembers++] =
					(gid_t)parse_ulong10(mem);
				mem = strtok_r(NULL, ",", &mem_save);
			}
		}

		if (group_count < IR0_PASSWD_MAX_ENTRIES)
			group_tab[group_count++] = ent;

		line = cursor;
	}
}

void ir0_passwd_reload(void)
{
	char *pbuf = NULL;
	char *gbuf = NULL;
	size_t plen = 0;
	size_t glen = 0;

	seed_builtin_accounts();

	if (read_file_into_buf("/etc/passwd", &pbuf, &plen) == 0)
	{
		parse_passwd_buf(pbuf, plen);
		kfree(pbuf);
	}

	if (read_file_into_buf("/etc/group", &gbuf, &glen) == 0)
	{
		parse_group_buf(gbuf, glen);
		kfree(gbuf);
	}
}

int ir0_passwd_lookup_uid(uid_t uid, struct ir0_passwd_entry *out)
{
	size_t i;

	for (i = 0; i < passwd_count; i++)
	{
		if (passwd_tab[i].uid == uid)
		{
			if (out)
				*out = passwd_tab[i];
			return 0;
		}
	}
	return -1;
}

int ir0_passwd_lookup_name(const char *name, struct ir0_passwd_entry *out)
{
	size_t i;

	if (!name)
		return -1;

	for (i = 0; i < passwd_count; i++)
	{
		if (strcmp(passwd_tab[i].name, name) == 0)
		{
			if (out)
				*out = passwd_tab[i];
			return 0;
		}
	}
	return -1;
}

int ir0_group_lookup_gid(gid_t gid, struct ir0_group_entry *out)
{
	size_t i;

	for (i = 0; i < group_count; i++)
	{
		if (group_tab[i].gid == gid)
		{
			if (out)
				*out = group_tab[i];
			return 0;
		}
	}
	return -1;
}
