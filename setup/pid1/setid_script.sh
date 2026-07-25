#!/bin/sh
# Set-user-ID shell script: execve(2) must refuse it (no interpreter path is
# allowed to inherit the set-id bits). Used by make smoke-setuid-exec.
echo SETID_SCRIPT_SHOULD_NOT_RUN
