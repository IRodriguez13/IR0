#ifndef _IR0_FCNTL_H
#define _IR0_FCNTL_H

/* File access modes */
#define O_RDONLY    0x0000  /* Read only */
#define O_WRONLY    0x0001  /* Write only */
#define O_RDWR      0x0002  /* Read and write */
#define O_ACCMODE   0x0003  /* Mask for file access modes */

/* File status flags */
#define O_APPEND    0x0008  /* Append mode */
#define O_CREAT     0x0100  /* Create if nonexistent */
#define O_EXCL      0x0200  /* Error if file exists */
#define O_TRUNC     0x0400  /* Truncate to zero length */
#define O_DIRECTORY 0x0200000  /* Must be a directory */

/* Seek types */
#define SEEK_SET    0       /* Set file offset to offset */
#define SEEK_CUR    1       /* Set file offset to current plus offset */
#define SEEK_END    2       /* Set file offset to EOF plus offset */

#endif /* _IR0_FCNTL_H */