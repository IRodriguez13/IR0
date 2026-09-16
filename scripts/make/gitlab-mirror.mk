# SPDX-License-Identifier: GPL-3.0-only
#
# GitLab is a read-replica of GitHub (remote name: mirror).
#   git push mirror              # current branch
#   make gitlab-remote           # add/refresh remotes on IR0 + ../ISD
#   make push-mirror             # master / dev / stable + tags
#
# Public URLs:
#   https://gitlab.com/IvanR013/IR0.git
#   https://gitlab.com/IvanR013/ISD.git

ifndef _IR0_GITLAB_MIRROR_MK
_IR0_GITLAB_MIRROR_MK := 1

GITLAB_IR0_MIRROR ?= https://gitlab.com/IvanR013/IR0.git
GITLAB_ISD_MIRROR ?= https://gitlab.com/IvanR013/ISD.git
GITLAB_MIRROR_REMOTE ?= mirror
export GITLAB_IR0_MIRROR GITLAB_ISD_MIRROR GITLAB_MIRROR_REMOTE
export GITLAB_MIRROR_URL ?= $(GITLAB_IR0_MIRROR)

.PHONY: gitlab-remote push-mirror

gitlab-remote:
	@chmod +x scripts/ensure_gitlab_mirror_remote.sh
	@scripts/ensure_gitlab_mirror_remote.sh

push-mirror: gitlab-remote
	@chmod +x scripts/mirror_to_gitlab.sh
	@scripts/mirror_to_gitlab.sh

endif
