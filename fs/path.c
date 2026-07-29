/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: path.c
 * Description: IR0 kernel source/header file
 */

#include <ir0/path.h>
#include <string.h>

int normalize_path(const char *src, char *dest, size_t size)
{
    if (!src || !dest || size == 0)
        return -1;

    // Special case: empty path
    if (*src == '\0')
    {
        if (size < 2)
            return -1;
        dest[0] = '/';
        dest[1] = '\0';
        return 0;
    }

    // Handle absolute vs relative path
    size_t pos = 0;
    if (*src == '/')
    {
        if (size < 1)
            return -1;
        dest[pos++] = '/';
        src++;
    }
    dest[pos] = '\0';

    // Split path into components
    char comp[256];
    size_t comp_len = 0;

    while (*src)
    {
        // Skip multiple slashes
        while (*src == '/')
            src++;

        // End of path
        if (*src == '\0')
            break;

        // Get next component
        comp_len = 0;
        while (*src && *src != '/' && comp_len < sizeof(comp) - 1)
            comp[comp_len++] = *src++;
        comp[comp_len] = '\0';

        // Handle . and ..
        if (strcmp(comp, ".") == 0)
        {
            // Ignore
            continue;
        }
        else if (strcmp(comp, "..") == 0)
        {
            // Go up one level
            if (pos > 1)
            {
                pos--;
                while (pos > 1 && dest[pos - 1] != '/')
                    pos--;
                dest[pos] = '\0';
            }
            continue;
        }

        // Add component to path
        if (pos > 1 || (pos == 1 && dest[0] != '/'))
        {
            if (pos + 1 >= size)
                return -1;
            dest[pos++] = '/';
        }

        if (pos + comp_len >= size)
            return -1;

        strncpy(dest + pos, comp, size - pos - 1);
        dest[size - 1] = '\0';
        pos += comp_len;
    }

    // Special case: path was empty or all components were removed
    if (pos == 0)
    {
        if (size < 2)
            return -1;
        dest[0] = '/';
        pos = 1;
    }

    dest[pos] = '\0';
    return 0;
}

int join_paths(const char *base, const char *rel, char *dest, size_t size)
{
    if (!base || !rel || !dest || size == 0)
        return -1;

    // If rel is absolute, just normalize it
    if (is_absolute_path(rel))
        return normalize_path(rel, dest, size);

    // Combine paths
    char combined[512];
    size_t base_len = strlen(base);
    size_t rel_len = strlen(rel);

    if (base_len + 1 + rel_len >= sizeof(combined))
        return -1;

    strncpy(combined, base, sizeof(combined) - 1);
    combined[sizeof(combined) - 1] = '\0';
    if (base_len > 0 && combined[base_len - 1] != '/')
    {
        combined[base_len] = '/';
        combined[base_len + 1] = '\0';
    }
    strcat(combined, rel);

    // Normalize the combined path
    return normalize_path(combined, dest, size);
}

int is_absolute_path(const char *path)
{
    return path && path[0] == '/';
}

int get_parent_path(const char *path, char *dest, size_t size)
{
    if (!path || !dest || size == 0)
        return -1;

    // Special case: root directory
    if (strcmp(path, "/") == 0)
    {
        if (size < 2)
            return -1;
        dest[0] = '/';
        dest[1] = '\0';
        return 0;
    }

    // Copy path
    char temp[256];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    // Remove trailing slashes
    size_t len = strlen(temp);
    while (len > 1 && temp[len - 1] == '/')
        temp[--len] = '\0';

    // Find last slash
    char *last_slash = strrchr(temp, '/');
    if (!last_slash)
    {
        // No slash found, use current directory
        if (size < 2)
            return -1;
        dest[0] = '.';
        dest[1] = '\0';
        return 0;
    }

    // Special case: path starts with slash
    if (last_slash == temp)
    {
        if (size < 2)
            return -1;
        dest[0] = '/';
        dest[1] = '\0';
        return 0;
    }

    // Get parent directory
    size_t parent_len = last_slash - temp;
    if (parent_len >= size)
        return -1;

    strncpy(dest, temp, parent_len);
    dest[parent_len] = '\0';
    return 0;
}

int ir0_path_apply_root(const char *root, const char *abs_user,
			char *dest, size_t size)
{
	char norm_user[256];
	char norm_root[256];

	if (!abs_user || !dest || size == 0)
		return -1;

	if (normalize_path(abs_user, norm_user, sizeof(norm_user)) != 0)
		return -1;

	if (!root || root[0] == '\0' ||
	    (root[0] == '/' && root[1] == '\0'))
		return normalize_path(norm_user, dest, size);

	if (normalize_path(root, norm_root, sizeof(norm_root)) != 0)
		return -1;

	/* User "/" → the jail root itself. */
	if (norm_user[0] == '/' && norm_user[1] == '\0')
		return normalize_path(norm_root, dest, size);

	/* join_paths ignores base when rel is absolute — strip leading '/'. */
	return join_paths(norm_root, norm_user + 1, dest, size);
}

int ir0_path_under_root(const char *root, const char *cwd)
{
	char norm_root[256];
	char norm_cwd[256];
	size_t rlen;

	if (!cwd || cwd[0] != '/')
		return 0;
	if (!root || root[0] == '\0' ||
	    (root[0] == '/' && root[1] == '\0'))
		return 1;

	if (normalize_path(root, norm_root, sizeof(norm_root)) != 0)
		return 0;
	if (normalize_path(cwd, norm_cwd, sizeof(norm_cwd)) != 0)
		return 0;

	rlen = strlen(norm_root);
	if (strcmp(norm_cwd, norm_root) == 0)
		return 1;
	if (strncmp(norm_cwd, norm_root, rlen) != 0)
		return 0;
	return norm_cwd[rlen] == '/';
}

int ir0_path_getcwd_visible(const char *root, const char *cwd,
			    char *dest, size_t size)
{
	char norm_root[256];
	char norm_cwd[256];
	size_t rlen;
	const char *vis;

	if (!cwd || !dest || size == 0)
		return -1;

	if (normalize_path(cwd, norm_cwd, sizeof(norm_cwd)) != 0)
		return -1;

	if (!root || root[0] == '\0' ||
	    (root[0] == '/' && root[1] == '\0'))
	{
		if (strlen(norm_cwd) >= size)
			return -1;
		strncpy(dest, norm_cwd, size - 1);
		dest[size - 1] = '\0';
		return 0;
	}

	if (normalize_path(root, norm_root, sizeof(norm_root)) != 0)
		return -1;

	if (!ir0_path_under_root(norm_root, norm_cwd))
	{
		if (strlen(norm_cwd) >= size)
			return -1;
		strncpy(dest, norm_cwd, size - 1);
		dest[size - 1] = '\0';
		return 0;
	}

	rlen = strlen(norm_root);
	if (strcmp(norm_cwd, norm_root) == 0)
		vis = "/";
	else
		vis = norm_cwd + rlen; /* starts with '/' */

	if (strlen(vis) >= size)
		return -1;
	strncpy(dest, vis, size - 1);
	dest[size - 1] = '\0';
	return 0;
}