# Polymorphic Filesystem Guide

## Overview

IR0 Kernel implements a polymorphic filesystem approach where system information and device access are provided through virtual filesystems (`/proc` and `/dev`) instead of dedicated syscalls. This follows the Unix philosophy of "everything is a file" and allows userspace programs to access kernel information using standard file operations.

## Virtual Filesystems

### `/proc` - Process and System Information

The `/proc` filesystem provides kernel and process information through virtual files. All files are read-only and dynamically generated.

#### Available Files

| File | Description | Replaces Syscall |
|------|-------------|------------------|
| `/proc/ps` | Process list | `SYS_PS` (7) |
| `/proc/meminfo` | Memory statistics | - |
| `/proc/netinfo` | Network information | - |
| `/proc/drivers` | Driver list | - |
| `/proc/status` | Current process status | - |
| `/proc/uptime` | System uptime | - |
| `/proc/version` | Kernel version | - |
| `/proc/cpuinfo` | CPU information | - |
| `/proc/loadavg` | System load average | - |
| `/proc/filesystems` | Supported filesystems | - |
| `/proc/cmdline` | Process command line | - |
| `/proc/blockdevices` | Block device list (lsblk) | `SYS_LSBLK` (92) |

### `/dev` - Device Access

The `/dev` filesystem provides access to hardware devices through special device files.

#### Available Devices

| Device | Description | Operations | Replaces Syscall |
|--------|-------------|------------|------------------|
| `/dev/null` | Null device | Read/Write | - |
| `/dev/zero` | Zero device | Read/Write | - |
| `/dev/console` | System console | Read/Write | - |
| `/dev/tty` | Current terminal | Read/Write | - |
| `/dev/kmsg` | Kernel messages | Read/Write | - |
| `/dev/audio` | Audio device | Write (test) | `SYS_AUDIO_TEST` (112) |
| `/dev/mouse` | Mouse device | Read (state) | `SYS_MOUSE_TEST` (113) |
| `/dev/net` | Network device | Read/Write | `SYS_PING` (115), `SYS_IFCONFIG` (116) |
| `/dev/disk` | Disk information (df) | Read | `SYS_DF` (95) |

## Usage Examples

### From Shell (DBG Shell)

```bash
# Process list (replaces 'ps' syscall)
cat /proc/ps

# Disk space (replaces 'df' syscall)
cat /dev/disk

# Block devices (replaces 'lsblk' syscall)
cat /proc/blockdevices

# Drivers list
cat /proc/drivers

# Kernel messages
cat /dev/kmsg

# Memory info
cat /proc/meminfo

# System uptime
cat /proc/uptime
```

### From Userspace Programs

Any userspace program can use the same interface:

```c
#include <ir0/syscall.h>

// Read process list
int fd = open("/proc/ps", O_RDONLY, 0);
char buffer[4096];
read(fd, buffer, sizeof(buffer));
close(fd);

// Read disk information
fd = open("/dev/disk", O_RDONLY, 0);
read(fd, buffer, sizeof(buffer));
close(fd);

// Read block devices
fd = open("/proc/blockdevices", O_RDONLY, 0);
read(fd, buffer, sizeof(buffer));
close(fd);
```

## Implementation Details

### File Descriptor Ranges

- **Regular files**: 3-999 (normal file descriptors)
- **/proc files**: 1000-1999 (special range for procfs)
- **/dev files**: 2000-2999 (special range for devfs)

### File Descriptor Mapping

**/proc files:**
- `1000` = `/proc/meminfo`
- `1001` = `/proc/status` (process-specific)
- `1002` = `/proc/uptime`
- `1003` = `/proc/version`
- `1004` = `/proc/ps`
- `1005` = `/proc/netinfo`
- `1006` = `/proc/drivers`
- `1007` = `/proc/cpuinfo`
- `1008` = `/proc/loadavg`
- `1009` = `/proc/filesystems`
- `1010` = `/proc/cmdline` (process-specific)
- `1011` = `/proc/blockdevices`

**/dev files:**
- `2001` = `/dev/null`
- `2002` = `/dev/zero`
- `2003` = `/dev/console`
- `2004` = `/dev/tty`
- `2005` = `/dev/kmsg`
- `2006` = `/dev/audio`
- `2007` = `/dev/mouse`
- `2008` = `/dev/net`
- `2009` = `/dev/disk`

## Benefits

1. **Uniform Interface**: Same syscalls (`open`, `read`, `write`, `close`) work for everything
2. **No Special Privileges**: Userspace programs can access system information like regular files
3. **Easy to Use**: Programs just use standard file operations
4. **POSIX Compatible**: Follows Unix/Linux conventions
5. **Extensible**: Adding new functionality only requires new virtual files
6. **Reduces Syscall Count**: Fewer syscalls in the dispatcher

## Deprecated Syscalls

The following syscalls are deprecated but maintained for backward compatibility:

- `SYS_PS` (7) → Use `cat /proc/ps`
- `SYS_DF` (95) → Use `cat /dev/disk`
- `SYS_LSBLK` (92) → Use `cat /proc/blockdevices`
- `SYS_AUDIO_TEST` (112) → Use `write()` to `/dev/audio`
- `SYS_MOUSE_TEST` (113) → Use `read()` from `/dev/mouse`

These syscalls now delegate to the virtual filesystem internally, but new code should use the filesystem interface directly.

## See Also

- [DEPRECATION_PLAN.md](DEPRECATION_PLAN.md) - Detailed deprecation plan
- [USERSPACE_POLYMORPHIC.md](USERSPACE_POLYMORPHIC.md) - Userspace compatibility details
- [VIRTUAL_FILESYSTEMS.md](../VIRTUAL_FILESYSTEMS.md) - Complete virtual filesystem documentation



