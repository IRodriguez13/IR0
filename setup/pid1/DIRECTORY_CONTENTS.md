Init Process Binary Directory
==============================

This directory is intended to hold the compiled Init process binary (PID 1).

Expected Contents:
------------------
- init: The compiled Init binary executable (not versioned; see .gitignore)
  * Built in-tree: make build-init-smoke (nostdlib ring-3) or make build-init-musl
  * Or compiled from an external repository and copied here
  * Must be a valid static ELF binary for x86-64 architecture
  * Will be copied to /sbin/init on the virtual disk

Usage:
------
1. Compile the Init process in its own repository
2. Copy the compiled binary to this directory as 'init'
3. Use 'make load-init' to load it into the virtual disk image
4. Use 'make remove-init' to remove it from the virtual disk

Note:
-----
- The Init binary is not automatically included in kernel builds
- The binary must be manually copied here after compilation
- The load-init command requires root privileges (mounts filesystem)

