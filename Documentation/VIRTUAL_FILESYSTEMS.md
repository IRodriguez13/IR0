# Virtual Filesystems in IR0 Kernel

IR0 Kernel implements several virtual filesystems that provide access to kernel information and devices through the standard file interface. These filesystems follow the Unix philosophy of "everything is a file."

## Available Virtual Filesystems

### 1. `/proc` - Process Information Filesystem

The `/proc` filesystem provides access to kernel and process information through virtual files. All files are read-only and dynamically generated.

#### System Information Files

- **`/proc/meminfo`** - Memory usage statistics
  - Shows total and used physical memory
  - Displays heap allocator statistics
  - Includes frame allocation information

- **`/proc/ps`** - Process list
  - Lists all running processes
  - Shows PID, state, and process names
  - Similar to the `ps` command output

- **`/proc/netinfo`** - Network information
  - Displays network interface information
  - Shows ARP cache entries
  - Network configuration details

- **`/proc/drivers`** - Driver information
  - Lists all registered kernel drivers
  - Shows driver names, versions, and languages (C/C++/Rust)
  - Driver status information

- **`/proc/status`** - Current process status
  - Information about the current process
  - Process state, PID, memory usage
  - Equivalent to `/proc/self/status`

- **`/proc/uptime`** - System uptime
  - System uptime in seconds
  - Time since kernel boot

- **`/proc/version`** - Kernel version
  - Kernel version string
  - Build information (date, time, user, host, compiler)

- **`/proc/cpuinfo`** - CPU information
  - Processor details
  - Architecture information

- **`/proc/loadavg`** - System load average
  - System load statistics
  - CPU utilization metrics

- **`/proc/filesystems`** - Supported filesystems
  - Lists all registered filesystems
  - Shows virtual filesystems (nodev) and physical filesystems
  - Format: `nodev <name>` for virtual filesystems, `<name>` for physical

- **`/proc/cmdline`** - Current process command line
  - Command line arguments for the current process
  - Equivalent to `/proc/self/cmdline`

#### Process-Specific Files

- **`/proc/[pid]/status`** - Process status by PID
  - Detailed information about a specific process
  - Replace `[pid]` with the process ID

- **`/proc/[pid]/cmdline`** - Process command line by PID
  - Command line arguments for a specific process
  - Replace `[pid]` with the process ID

#### Usage Examples

```bash
# View memory information
cat /proc/meminfo

# List all processes
cat /proc/ps

# Check system uptime
cat /proc/uptime

# View kernel version
cat /proc/version

# Get current process status
cat /proc/status

# Get process status by PID
cat /proc/1/status
```

---

### 2. `/dev` - Device Filesystem

The `/dev` filesystem provides access to hardware devices through special device files. This implements the Unix "everything is a file" philosophy for device access.

#### Standard Device Nodes

- **`/dev/null`** - Null device
  - **Read**: Always returns EOF (end of file)
  - **Write**: Accepts all data and discards it
  - **Use case**: Redirecting output to discard it (`> /dev/null`)

- **`/dev/zero`** - Zero device
  - **Read**: Returns zeros (null bytes)
  - **Write**: Accepts data but doesn't store it
  - **Use case**: Creating zero-filled buffers

- **`/dev/console`** - System console
  - **Read**: Console input (not fully implemented)
  - **Write**: Outputs to VGA text mode display
  - **Permissions**: 0620 (read/write for owner and group)

- **`/dev/tty`** - Current terminal
  - **Read/Write**: Same as `/dev/console`
  - **Use case**: Terminal device access
  - **Permissions**: 0620

- **`/dev/kmsg`** - Kernel message buffer
  - **Read**: Reads from circular kernel message buffer
  - **Write**: Writes to kernel message buffer
  - **Use case**: Kernel logging and message retrieval
  - **Permissions**: 0600 (read/write for owner only)
  - **Buffer size**: 4KB circular buffer

- **`/dev/audio`** - Audio device
  - **Read**: Audio input (not fully implemented)
  - **Write**: Sends audio data to Sound Blaster driver
  - **IOCTL**: Audio control (volume, play, stop)
  - **Permissions**: 0660

- **`/dev/mouse`** - Mouse device
  - **Read**: Returns mouse state (x, y, buttons)
  - **IOCTL**: Mouse control (get state, set sensitivity)
  - **Permissions**: 0660
  - **Format**: Returns 3 integers (x, y, button state)

- **`/dev/net`** - Network device
  - **Read**: Network status (not fully implemented)
  - **Write**: Network commands (ping, ifconfig)
  - **IOCTL**: Network control (send ping, get/set config)
  - **Permissions**: 0660

- **`/dev/disk`** - Disk device
  - **Read**: Reads from disk at offset
  - **Write**: Writes to disk at offset
  - **IOCTL**: Disk control (read/write sector, get geometry)
  - **Permissions**: 0660

#### Usage Examples

```bash
# Discard output
command > /dev/null

# Create zero-filled file
dd if=/dev/zero of=zeros.bin bs=1024 count=1

# Write to console
echo "Hello" > /dev/console

# Read kernel messages
cat /dev/kmsg

# Send audio data
cat audio.raw > /dev/audio
```

---

### 3. RAMFS - RAM Filesystem

RAMFS is a simple in-memory filesystem for boot files and temporary data. All data is stored in kernel memory and is lost on reboot.

#### Features

- In-memory storage (no persistence)
- Simple file-based interface
- Maximum 64 files
- Maximum 4KB per file
- Maximum 255 characters per filename

#### Usage

RAMFS is typically mounted at boot time for temporary file storage. Files can be created, read, and written like any regular filesystem.

```bash
# Mount RAMFS (typically done at boot)
mount -t ramfs none /tmp

# Create files in RAMFS
touch /tmp/tempfile.txt
echo "data" > /tmp/tempfile.txt
```

---

### 4. TMPFS - Temporary Filesystem

TMPFS is similar to RAMFS but optimized for temporary directories like `/tmp`. It provides a more structured inode-based filesystem.

#### Features

- In-memory storage with inode structure
- Directory support
- Maximum 128 files
- Maximum 64KB per file
- Maximum 255 characters per filename
- Maximum 32 directories
- Supports directory hierarchy

#### Usage

TMPFS is typically mounted for temporary storage:

```bash
# Mount TMPFS (typically done at boot)
mount -t tmpfs none /tmp

# Create files and directories
mkdir /tmp/mydir
touch /tmp/mydir/file.txt
```

---

## Filesystem Type Summary

| Filesystem | Type | Storage | Mount Point | Purpose |
|-----------|------|---------|-------------|---------|
| `proc` | Virtual | Dynamic | `/proc` | Kernel/process information |
| `devfs` | Virtual | Dynamic | `/dev` | Device access |
| `ramfs` | Virtual | Memory | Any | Simple temporary storage |
| `tmpfs` | Virtual | Memory | Any | Structured temporary storage |
| `minix` | Physical | Disk | Any | Persistent disk storage |

## Implementation Notes

- All virtual filesystems are implemented as part of the VFS (Virtual File System) layer
- Virtual filesystems don't require actual disk storage
- Files in `/proc` and `/dev` are dynamically generated on access
- RAMFS and TMPFS store data in kernel memory only
- All virtual filesystems support standard file operations (open, read, write, stat, etc.)

## Viewing Available Filesystems

To see all registered filesystems (virtual and physical):

```bash
cat /proc/filesystems
```

This will show output like:
```
nodev proc
nodev devfs
nodev ramfs
nodev tmpfs
minix
```

The `nodev` prefix indicates virtual filesystems that don't require a device.

---

For more information about the VFS implementation, see the kernel source code in `fs/` directory.

