#!/bin/sh
# Recovery smoke helper — run fsck.ir0 then mount-root-rw (honest tags).
set -e
fsck.ir0
mount-root-rw
echo RECOVERY_FSCK_REMOUNT_DONE
