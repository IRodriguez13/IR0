#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Push-mirror IR0 to GitLab. GitHub remains the source of truth.
#
# Default remote URL: https://gitlab.com/IvanR013/IR0.git
# (override with GITLAB_MIRROR_URL).
#
# Auth:
#   GITLAB_MIRROR_TOKEN  PAT with write_repository (CI; never logged)
#   or a configured git remote named "mirror" (local: git push mirror)
#
# Optional:
#   GITLAB_MIRROR_ALL=1  git push --mirror (every ref). Default is
#                        master, dev, stable, and tags only.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

url="${GITLAB_MIRROR_URL:-https://gitlab.com/IvanR013/IR0.git}"
token="${GITLAB_MIRROR_TOKEN:-}"
remote="${GITLAB_MIRROR_REMOTE:-mirror}"

case "$url" in
https://*) ;;
*)
	echo "mirror_to_gitlab: GITLAB_MIRROR_URL must be https://" >&2
	exit 2
	;;
esac

if [ -n "$token" ]; then
	rest="${url#https://}"
	rest="${rest#*@}"
	pushurl="https://oauth2:${token}@${rest}"
elif git remote get-url "$remote" >/dev/null 2>&1; then
	pushurl="$remote"
else
	echo "mirror_to_gitlab: skip (no GITLAB_MIRROR_TOKEN and no remote '$remote')" >&2
	echo "mirror_to_gitlab: run scripts/ensure_gitlab_mirror_remote.sh" >&2
	exit 0
fi

git_push()
{
	git push --prune "$pushurl" "$@"
}

if [ "${GITLAB_MIRROR_ALL:-}" = "1" ]; then
	echo "mirror_to_gitlab: --mirror → GitLab" >&2
	git_push --mirror
	exit 0
fi

specs=()
for b in master dev stable; do
	if git show-ref --verify --quiet "refs/heads/${b}"; then
		specs+=("+refs/heads/${b}:refs/heads/${b}")
	elif git show-ref --verify --quiet "refs/remotes/origin/${b}"; then
		specs+=("+refs/remotes/origin/${b}:refs/heads/${b}")
	fi
done
if git tag | grep -q .; then
	specs+=("+refs/tags/*:refs/tags/*")
fi

if [ "${#specs[@]}" -eq 0 ]; then
	echo "mirror_to_gitlab: no master/dev/stable/tags to push" >&2
	exit 0
fi

echo "mirror_to_gitlab: product refs → GitLab" >&2
git_push "${specs[@]}"
echo "mirror_to_gitlab: ok" >&2
