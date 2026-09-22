# SPDX-License-Identifier: GPL-3.0-only
#
# IR0 release-check gates.
#
# Profile tiers (0.0.1):
#   reference  — PROFILE=minimal (mandatory fresh-clone gate)
#   supported  — CI may add desktop / development (not full cartesian product)
#   experimental — ad-hoc usmang profiles; not release gates
#
# Targets:
#   tooling-check       — fast host checks (no QEMU, no Docker)
#   release-check*      — Tier 1 build + contracts on current tree
#   release-check-boot* — Tier 1.5 ISO + QEMU + guest probes
#   fresh-clone-check   — maintainer CI: git clone IR0+ISD in Docker (simulates third party)
#   ci-local*           — staged pre-push gates on your machine (see ci_local.sh)
#   ci-local-docker     — Docker first-time pack of the working tree (out/ stripped)
#   ci-local-rc         — Docker git clone from GitHub (published trees)

ifndef _IR0_RELEASE_MK
_IR0_RELEASE_MK := 1

RELEASE_CHECK_IMAGE ?= ir0-release-check
RELEASE_CHECK_SCRIPT_REV ?= 12
RELEASE_CHECK_IR0_REF ?= dev
RELEASE_CHECK_ISD_REF ?= dev
RELEASE_CHECK_DOUBLE ?= 0

.PHONY: truth-tests tooling-check fresh-clone-check \
	ci ci-fast ci-docker ci-local ci-local-fast ci-local-boot \
	ci-local-docker ci-local-docker-wip ci-local-rc \
	tui-k tui-u \
	release-check release-check-clean \
	release-check-container release-check-container-local \
	release-check-boot release-check-boot-clean \
	release-check-boot-container release-check-boot-container-local \
	release-check-guest-probes

truth-tests:
	@python3 scripts/test_truth_tooling.py

# Fast gate: unit/contract checks on the developer tree (no fresh clone, no QEMU).
tooling-check:
	@chmod +x scripts/release_check.sh scripts/resolve_isd_root.sh
	@make -s repo-hygiene-guard
	@make -s arch-guard
	@make -s -C tests/host run
	@python3 scripts/test_kernel_manager.py
	@python3 scripts/test_truth_tooling.py
	@make -s isd-contracts
	@make -s check-isd

# --- Local pre-push CI (staged; run before push) ---

ci-local-fast:
	@chmod +x scripts/ci_local.sh
	@CI_LOCAL_STAGE=fast PROFILE="$(ISD_PROFILE)" scripts/ci_local.sh

ci-local-boot:
	@chmod +x scripts/ci_local.sh scripts/release_check_boot.sh \
		scripts/release_check_kmang.sh scripts/release_check_guest_probes.py
	@CI_LOCAL_STAGE=boot PROFILE="$(ISD_PROFILE)" scripts/ci_local.sh

ci-local-docker:
	@chmod +x scripts/ci_local.sh scripts/ci/release-check-fresh.sh \
		scripts/release_check_boot.sh scripts/release_check_kmang.sh \
		scripts/release_check_guest_probes.py scripts/release_check_incremental.sh \
		scripts/release_check_tui.sh
	@CI_LOCAL_STAGE=docker PROFILE="$(ISD_PROFILE)" scripts/ci_local.sh

# Alias of ci-local-docker (same first-pack CLEAN path).
ci-local-docker-wip: ci-local-docker

ci-local-rc:
	@chmod +x scripts/ci_local.sh scripts/ci/release-check-fresh.sh \
		scripts/release_check_boot.sh scripts/release_check_kmang.sh \
		scripts/release_check_guest_probes.py scripts/release_check_tui.sh
	@CI_LOCAL_STAGE=rc PROFILE="$(ISD_PROFILE)" ISD_ARCH="$(ISD_ARCH)" scripts/ci_local.sh

# Recommended before push: fast checks + Docker first-time pack of this tree.
ci-local:
	@chmod +x scripts/ci_local.sh scripts/ci/release-check-fresh.sh \
		scripts/release_check_boot.sh scripts/release_check_kmang.sh \
		scripts/release_check_guest_probes.py scripts/release_check_incremental.sh \
		scripts/release_check_tui.sh
	@CI_LOCAL_STAGE=all PROFILE="$(ISD_PROFILE)" scripts/ci_local.sh

ci: ci-local
ci-fast: ci-local-fast
ci-docker: ci-local-docker

release-check:
	@chmod +x scripts/release_check.sh scripts/resolve_isd_root.sh
	@RELEASE_CHECK_FRESH=0 PROFILE="$(ISD_PROFILE)" ISD_ARCH="$(ISD_ARCH)" \
		scripts/release_check.sh

release-check-clean:
	@chmod +x scripts/release_check.sh scripts/resolve_isd_root.sh
	@RELEASE_CHECK_FRESH=1 PROFILE="$(ISD_PROFILE)" ISD_ARCH="$(ISD_ARCH)" \
		scripts/release_check.sh

release-check-boot:
	@chmod +x scripts/release_check_boot.sh scripts/release_check_kmang.sh \
		scripts/resolve_isd_root.sh scripts/release_check_guest_probes.py
	@PROFILE="$(ISD_PROFILE)" ISD_ARCH="$(ISD_ARCH)" \
		RELEASE_CHECK_GUEST=1 scripts/release_check_boot.sh

release-check-boot-clean:
	@chmod +x scripts/release_check.sh scripts/release_check_boot.sh \
		scripts/release_check_guest_probes.py scripts/resolve_isd_root.sh
	@RELEASE_CHECK_FRESH=1 PROFILE="$(ISD_PROFILE)" ISD_ARCH="$(ISD_ARCH)" \
		scripts/release_check.sh
	@PROFILE="$(ISD_PROFILE)" ISD_ARCH="$(ISD_ARCH)" \
		RELEASE_CHECK_GUEST=1 scripts/release_check_boot.sh

release-check-guest-probes: ensure-isd-disk kernel-x64-userspace.iso
	@chmod +x scripts/release_check_guest_probes.py
	@python3 scripts/release_check_guest_probes.py \
		--iso kernel-x64-userspace.iso --disk "$(IR0_ISD_DISK)"

# --- Docker: default = git clone only (no host tree bind mounts) ---

define RELEASE_CHECK_DOCKER_BUILD
	docker build --build-arg RELEASE_CHECK_SCRIPT_REV=$(RELEASE_CHECK_SCRIPT_REV) \
		-f scripts/ci/Dockerfile.release-check -t $(RELEASE_CHECK_IMAGE) "$(KERNEL_ROOT)"
endef

define RELEASE_CHECK_DOCKER_RUN
	docker run --rm \
		-e IR0_REF=$(RELEASE_CHECK_IR0_REF) \
		-e ISD_REF=$(RELEASE_CHECK_ISD_REF) \
		-e PROFILE="$(ISD_PROFILE)" \
		-e ISD_ARCH="$(ISD_ARCH)" \
		-e RELEASE_CHECK_BOOT=$(1) \
		-e RELEASE_CHECK_GUEST=$(2) \
		-e RELEASE_CHECK_DOUBLE=$(3) \
		-e RELEASE_CHECK_INCREMENTAL=$(4) \
		$(RELEASE_CHECK_IMAGE)
endef

release-check-container:
	@chmod +x scripts/ci/release-check-fresh.sh scripts/resolve_isd_root.sh
	@$(RELEASE_CHECK_DOCKER_BUILD)
	@$(call RELEASE_CHECK_DOCKER_RUN,0,0,0,0)

release-check-boot-container:
	@chmod +x scripts/ci/release-check-fresh.sh scripts/resolve_isd_root.sh \
		scripts/release_check_boot.sh scripts/release_check_kmang.sh \
		scripts/release_check_guest_probes.py
	@$(RELEASE_CHECK_DOCKER_BUILD)
	@$(call RELEASE_CHECK_DOCKER_RUN,1,1,0,1)

# RC/release gate: clone from Git, build, boot, guest probes; optional double-run.
fresh-clone-check:
	@chmod +x scripts/ci/release-check-fresh.sh scripts/resolve_isd_root.sh \
		scripts/release_check_boot.sh scripts/release_check_kmang.sh \
		scripts/release_check_guest_probes.py scripts/release_check_incremental.sh
	@$(RELEASE_CHECK_DOCKER_BUILD)
	@$(call RELEASE_CHECK_DOCKER_RUN,1,1,$(RELEASE_CHECK_DOUBLE),1)


# --- Docker: local WIP only (bind mounts working tree; not a release gate) ---

release-check-container-local:
	@chmod +x scripts/ci/release-check-fresh.sh scripts/resolve_isd_root.sh
	@ISD_ROOT="$$(scripts/resolve_isd_root.sh "$(KERNEL_ROOT)")"; \
	$(RELEASE_CHECK_DOCKER_BUILD); \
	docker run --rm \
		-v "$(KERNEL_ROOT):/src/IR0:ro" \
		-v "$$ISD_ROOT:/src/ISD:ro" \
		-e RELEASE_CHECK_LOCAL=1 \
		-e IR0_REF=$(RELEASE_CHECK_IR0_REF) \
		-e ISD_REF=$(RELEASE_CHECK_ISD_REF) \
		-e PROFILE="$(ISD_PROFILE)" \
		-e ISD_ARCH="$(ISD_ARCH)" \
		-e RELEASE_CHECK_BOOT=0 \
		-e RELEASE_CHECK_GUEST=0 \
		-e RELEASE_CHECK_DOUBLE=0 \
		$(RELEASE_CHECK_IMAGE)

release-check-boot-container-local:
	@chmod +x scripts/ci/release-check-fresh.sh scripts/resolve_isd_root.sh \
		scripts/release_check_boot.sh scripts/release_check_kmang.sh \
		scripts/release_check_guest_probes.py scripts/release_check_incremental.sh
	@ISD_ROOT="$$(scripts/resolve_isd_root.sh "$(KERNEL_ROOT)")"; \
	$(RELEASE_CHECK_DOCKER_BUILD); \
	docker run --rm \
		-v "$(KERNEL_ROOT):/src/IR0:ro" \
		-v "$$ISD_ROOT:/src/ISD:ro" \
		-e RELEASE_CHECK_LOCAL=1 \
		-e IR0_REF=$(RELEASE_CHECK_IR0_REF) \
		-e ISD_REF=$(RELEASE_CHECK_ISD_REF) \
		-e PROFILE="$(ISD_PROFILE)" \
		-e ISD_ARCH="$(ISD_ARCH)" \
		-e RELEASE_CHECK_BOOT=1 \
		-e RELEASE_CHECK_GUEST=1 \
		-e RELEASE_CHECK_DOUBLE=0 \
		-e RELEASE_CHECK_INCREMENTAL=1 \
		$(RELEASE_CHECK_IMAGE)

endif
