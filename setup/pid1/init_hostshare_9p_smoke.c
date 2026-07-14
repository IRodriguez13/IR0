/**
 * IR0 — virtio-9p host share smoke (QEMU -virtfs → /mnt/host)
 * Exercises flat file + one subdirectory path.
 */
/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
	const char *payload = "KTM_HOSTSHARE_OK\n";
	const char *subdir_payload = "HOSTSHARE_9P_SUBDIR_OK\n";
	int fd;
	ssize_t n;

	mkdir("/mnt", 0755);
	mkdir("/mnt/host", 0755);

	if (mount("ir0share", "/mnt/host", "9p", 0, NULL) != 0)
	{
		printf("HOSTSHARE_9P_MOUNT_FAIL errno=%d\n", errno);
		return 2;
	}
	printf("HOSTSHARE_9P_MOUNT_OK\n");

	fd = open("/mnt/host/ktm_hostshare.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		printf("HOSTSHARE_9P_OPEN_FAIL errno=%d\n", errno);
		return 3;
	}
	n = write(fd, payload, strlen(payload));
	close(fd);
	if (n != (ssize_t)strlen(payload))
	{
		printf("HOSTSHARE_9P_WRITE_FAIL n=%zd\n", n);
		return 4;
	}

	fd = open("/mnt/host/subdir/ktm_hostshare.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		printf("HOSTSHARE_9P_SUBDIR_OPEN_FAIL errno=%d\n", errno);
		return 5;
	}
	n = write(fd, subdir_payload, strlen(subdir_payload));
	close(fd);
	if (n != (ssize_t)strlen(subdir_payload))
	{
		printf("HOSTSHARE_9P_SUBDIR_WRITE_FAIL n=%zd\n", n);
		return 6;
	}

	printf("HOSTSHARE_9P_SUBDIR_OK\n");
	printf("HOSTSHARE_9P_OK\n");
	printf("KTM_HOSTSHARE_OK\n");
	fflush(stdout);
	return 0;
}
