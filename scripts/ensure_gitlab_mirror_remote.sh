#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Add or refresh git remote "mirror" → GitLab (GitHub stays origin).
#
#   git push mirror          # current branch
#   make push-mirror         # master / dev / stable + tags
#
# Env:
#   GITLAB_IR0_MIRROR  default https://gitlab.com/IvanR013/IR0.git
#   GITLAB_ISD_MIRROR  default https://gitlab.com/IvanR013/ISD.git
#   IR0_ISD_ROOT       sibling ISD tree (default ../ISD)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IR0_URL="${GITLAB_IR0_MIRROR:-https://gitlab.com/IvanR013/IR0.git}"
ISD_URL="${GITLAB_ISD_MIRROR:-https://gitlab.com/IvanR013/ISD.git}"
ISD_ROOT="${IR0_ISD_ROOT:-$ROOT/../ISD}"
if [ ! -d "$ISD_ROOT/.git" ] && [ -d "${HOME:-}/ISD/.git" ]; then
	ISD_ROOT="$HOME/ISD"
fi
REMOTE="${GITLAB_MIRROR_REMOTE:-mirror}"

ensure()
{
	local dir="$1" url="$2" label="$3"

	if [ ! -d "$dir/.git" ]; then
		echo "gitlab-remote: skip $label (no git tree at $dir)" >&2
		return 0
	fi
	if git -C "$dir" remote get-url "$REMOTE" >/dev/null 2>&1; then
		git -C "$dir" remote set-url "$REMOTE" "$url"
	else
		git -C "$dir" remote add "$REMOTE" "$url"
	fi
	echo "gitlab-remote: $label $REMOTE → $(git -C "$dir" remote get-url "$REMOTE")" >&2
}

ensure "$ROOT" "$IR0_URL" "IR0"
ensure "$ISD_ROOT" "$ISD_URL" "ISD"
echo "push: git push $REMOTE          # current branch" >&2
echo "      git push $REMOTE master   # product tip" >&2
