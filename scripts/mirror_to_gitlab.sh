#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Push-mirror IR0 to GitLab. GitHub remains the source of truth.
#
# Required env:
#   GITLAB_MIRROR_URL    https://gitlab.example/group/IR0.git
#   GITLAB_MIRROR_TOKEN  PAT with write_repository (never logged)
#
# Optional:
#   GITLAB_MIRROR_ALL=1  git push --mirror (every ref). Default is
#                        master, dev, stable, and tags only.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

url="${GITLAB_MIRROR_URL:-}"
token="${GITLAB_MIRROR_TOKEN:-}"

if [ -z "$url" ] || [ -z "$token" ]; then
	echo "mirror_to_gitlab: skip (GITLAB_MIRROR_URL / GITLAB_MIRROR_TOKEN unset)" >&2
	exit 0
fi

case "$url" in
https://*)
	rest="${url#https://}"
	rest="${rest#*@}"
	pushurl="https://oauth2:${token}@${rest}"
	;;
*)
	echo "mirror_to_gitlab: GITLAB_MIRROR_URL must be https://" >&2
	exit 2
	;;
esac

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
