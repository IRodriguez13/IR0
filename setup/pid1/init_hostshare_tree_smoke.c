/**
 * IR0 — virtio-9p deep tree smoke (mkdir/readdir/write-at/rename/unlink)
 */
/* SPDX-License-Identifier: GPL-3.0-only */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static int tag_ok(const char *t)
{
	printf("%s\n", t);
	fflush(stdout);
	return 0;
}

static int fail(const char *t, int err)
{
	printf("%s errno=%d\n", t, err);
	fflush(stdout);
	return 1;
}

int main(void)
{
	int fd;
	ssize_t n;
	char buf[64];
	DIR *d;
	struct dirent *de;
	int saw_b = 0;
	int saw_note = 0;
	struct stat st;

	mkdir("/mnt", 0755);
	mkdir("/mnt/host", 0755);

	if (mount("ir0share", "/mnt/host", "9p", 0, NULL) != 0)
		return fail("HOSTSHARE_TREE_MOUNT_FAIL", errno);
	tag_ok("HOSTSHARE_TREE_MOUNT_OK");

	if (mkdir("/mnt/host/a", 0755) != 0 && errno != EEXIST)
		return fail("HOSTSHARE_TREE_MKDIR_A_FAIL", errno);
	if (mkdir("/mnt/host/a/b", 0755) != 0 && errno != EEXIST)
		return fail("HOSTSHARE_TREE_MKDIR_AB_FAIL", errno);
	tag_ok("HOSTSHARE_TREE_MKDIR_OK");

	fd = open("/mnt/host/a/b/note.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return fail("HOSTSHARE_TREE_CREATE_FAIL", errno);
	n = write(fd, "HELLO", 5);
	close(fd);
	if (n != 5)
		return fail("HOSTSHARE_TREE_WRITE0_FAIL", errno);

	fd = open("/mnt/host/a/b/note.txt", O_WRONLY, 0);
	if (fd < 0)
		return fail("HOSTSHARE_TREE_REOPEN_FAIL", errno);
	if (lseek(fd, 5, SEEK_SET) != 5)
	{
		close(fd);
		return fail("HOSTSHARE_TREE_LSEEK_FAIL", errno);
	}
	n = write(fd, "-WORLD", 6);
	close(fd);
	if (n != 6)
		return fail("HOSTSHARE_TREE_WRITE_AT_FAIL", errno);
	tag_ok("HOSTSHARE_TREE_WRITE_OK");

	fd = open("/mnt/host/a/b/note.txt", O_RDONLY);
	if (fd < 0)
		return fail("HOSTSHARE_TREE_READ_OPEN_FAIL", errno);
	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n != 11 || memcmp(buf, "HELLO-WORLD", 11) != 0)
		return fail("HOSTSHARE_TREE_READ_FAIL", errno);
	tag_ok("HOSTSHARE_TREE_READ_OK");

	d = opendir("/mnt/host/a");
	if (!d)
		return fail("HOSTSHARE_TREE_OPENDIR_FAIL", errno);
	while ((de = readdir(d)) != NULL)
	{
		if (strcmp(de->d_name, "b") == 0)
			saw_b = 1;
	}
	closedir(d);
	if (!saw_b)
		return fail("HOSTSHARE_TREE_READDIR_A_FAIL", 0);

	d = opendir("/mnt/host/a/b");
	if (!d)
		return fail("HOSTSHARE_TREE_OPENDIR_B_FAIL", errno);
	while ((de = readdir(d)) != NULL)
	{
		if (strcmp(de->d_name, "note.txt") == 0)
			saw_note = 1;
	}
	closedir(d);
	if (!saw_note)
		return fail("HOSTSHARE_TREE_READDIR_B_FAIL", 0);
	tag_ok("HOSTSHARE_TREE_READDIR_OK");

	if (rename("/mnt/host/a/b/note.txt", "/mnt/host/a/b/renamed.txt") != 0)
		return fail("HOSTSHARE_TREE_RENAME_FAIL", errno);
	if (stat("/mnt/host/a/b/renamed.txt", &st) != 0)
		return fail("HOSTSHARE_TREE_RENAME_STAT_FAIL", errno);
	tag_ok("HOSTSHARE_TREE_RENAME_OK");

	if (symlink("renamed.txt", "/mnt/host/a/b/link") != 0)
		return fail("HOSTSHARE_SYMLINK_CREATE_FAIL", errno);
	memset(buf, 0, sizeof(buf));
	n = readlink("/mnt/host/a/b/link", buf, sizeof(buf) - 1);
	if (n < 0)
		return fail("HOSTSHARE_SYMLINK_READLINK_FAIL", errno);
	if (n != 11 || memcmp(buf, "renamed.txt", 11) != 0)
		return fail("HOSTSHARE_SYMLINK_TARGET_FAIL", errno);
	if (lstat("/mnt/host/a/b/link", &st) != 0)
		return fail("HOSTSHARE_SYMLINK_LSTAT_FAIL", errno);
	if (!S_ISLNK(st.st_mode))
		return fail("HOSTSHARE_SYMLINK_MODE_FAIL", 0);
	tag_ok("HOSTSHARE_SYMLINK_OK");

	if (unlink("/mnt/host/a/b/link") != 0)
		return fail("HOSTSHARE_SYMLINK_UNLINK_FAIL", errno);
	if (unlink("/mnt/host/a/b/renamed.txt") != 0)
		return fail("HOSTSHARE_TREE_UNLINK_FAIL", errno);
	if (rmdir("/mnt/host/a/b") != 0)
		return fail("HOSTSHARE_TREE_RMDIR_B_FAIL", errno);
	if (rmdir("/mnt/host/a") != 0)
		return fail("HOSTSHARE_TREE_RMDIR_A_FAIL", errno);
	tag_ok("HOSTSHARE_TREE_UNLINK_OK");

	fd = open("/mnt/host/tree_done.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return fail("HOSTSHARE_TREE_DONE_OPEN_FAIL", errno);
	n = write(fd, "HOSTSHARE_TREE_OK\n", 18);
	close(fd);
	if (n != 18)
		return fail("HOSTSHARE_TREE_DONE_WRITE_FAIL", errno);
	if (symlink("tree_done.txt", "/mnt/host/symlink_probe") != 0)
		return fail("HOSTSHARE_SYMLINK_PROBE_FAIL", errno);

	tag_ok("HOSTSHARE_TREE_OK");
	return 0;
}
