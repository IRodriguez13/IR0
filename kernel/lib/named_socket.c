/* SPDX-License-Identifier: GPL-3.0-only */
/** Path-visible AF_UNIX socket nodes, separate from socket transport state. */

#include <ir0/named_socket.h>
#include <ir0/errno.h>
#include <ir0/path.h>
#include <string.h>

#define NAMED_SOCKET_MAX 64

struct named_socket_entry
{
	char path[256];
	mode_t mode;
	int in_use;
};

static struct named_socket_entry named_sockets[NAMED_SOCKET_MAX];

static struct named_socket_entry *named_socket_find(const char *path)
{
	char norm[256];
	int i;

	if (!path || normalize_path(path, norm, sizeof(norm)) != 0)
		return NULL;
	for (i = 0; i < NAMED_SOCKET_MAX; i++)
	{
		if (named_sockets[i].in_use &&
		    strcmp(named_sockets[i].path, norm) == 0)
			return &named_sockets[i];
	}
	return NULL;
}

int named_socket_create(const char *path, mode_t mode)
{
	char norm[256];
	int i;

	if (!path || path[0] != '/')
		return -EINVAL;
	if (named_socket_find(path))
		return -EADDRINUSE;
	if (normalize_path(path, norm, sizeof(norm)) != 0)
		return -ENAMETOOLONG;
	for (i = 0; i < NAMED_SOCKET_MAX; i++)
	{
		if (!named_sockets[i].in_use)
		{
			strncpy(named_sockets[i].path, norm,
				sizeof(named_sockets[i].path) - 1);
			named_sockets[i].path[sizeof(named_sockets[i].path) - 1] = '\0';
			named_sockets[i].mode = (mode_t)(S_IFSOCK | (mode & 0777));
			named_sockets[i].in_use = 1;
			return 0;
		}
	}
	return -ENOSPC;
}

int named_socket_stat(const char *path, stat_t *buf)
{
	struct named_socket_entry *entry;

	if (!buf)
		return -EINVAL;
	entry = named_socket_find(path);
	if (!entry)
		return -ENOENT;
	memset(buf, 0, sizeof(*buf));
	buf->st_mode = entry->mode;
	buf->st_nlink = 1;
	return 0;
}

int named_socket_unlink(const char *path)
{
	struct named_socket_entry *entry = named_socket_find(path);

	if (!entry)
		return -ENOENT;
	memset(entry, 0, sizeof(*entry));
	return 0;
}
