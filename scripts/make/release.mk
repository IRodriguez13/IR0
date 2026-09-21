# SPDX-License-Identifier: GPL-3.0-only
#
# IR0 release-check gates (Tier 1 build/contracts; Tier 1.5 optional QEMU boot).

ifndef _IR0_RELEASE_MK
_IR0_RELEASE_MK := 1

.PHONY: release-check release-check-clean truth-tests \
	release-check-container release-check-boot release-check-boot-clean \
	release-check-boot-container

truth-tests:
	@python3 scripts/test_truth_tooling.py

release-check:
	@chmod +x scripts/release_check.sh scripts/resolve_isd_root.sh
	@RELEASE_CHECK_FRESH=0 PROFILE="$(ISD_PROFILE)" ISD_ARCH="$(ISD_ARCH)" \
		scripts/release_check.sh

release-check-clean:
	@chmod +x scripts/release_check.sh scripts/resolve_isd_root.sh
	@RELEASE_CHECK_FRESH=1 PROFILE="$(ISD_PROFILE)" ISD_ARCH="$(ISD_ARCH)" \
		scripts/release_check.sh

release-check-boot:
	@chmod +x scripts/release_check_boot.sh scripts/resolve_isd_root.sh
	@PROFILE="$(ISD_PROFILE)" ISD_ARCH="$(ISD_ARCH)" scripts/release_check_boot.sh

release-check-boot-clean:
	@chmod +x scripts/release_check.sh scripts/release_check_boot.sh scripts/resolve_isd_root.sh
	@RELEASE_CHECK_FRESH=1 PROFILE="$(ISD_PROFILE)" ISD_ARCH="$(ISD_ARCH)" \
		scripts/release_check.sh
	@PROFILE="$(ISD_PROFILE)" ISD_ARCH="$(ISD_ARCH)" scripts/release_check_boot.sh

release-check-container:
	@chmod +x scripts/ci/release-check-fresh.sh scripts/resolve_isd_root.sh
	@ISD_ROOT="$$(scripts/resolve_isd_root.sh "$(KERNEL_ROOT)")"; \
	docker build --build-arg RELEASE_CHECK_SCRIPT_REV=8 \
		-f scripts/ci/Dockerfile.release-check -t ir0-release-check "$(KERNEL_ROOT)"; \
	docker run --rm \
		-v "$(KERNEL_ROOT):/src/IR0:ro" \
		-v "$$ISD_ROOT:/src/ISD:ro" \
		-e RELEASE_CHECK_LOCAL=1 \
		-e IR0_REF=dev \
		-e ISD_REF=dev \
		-e PROFILE="$(ISD_PROFILE)" \
		-e ISD_ARCH="$(ISD_ARCH)" \
		-e RELEASE_CHECK_BOOT=0 \
		ir0-release-check

release-check-boot-container:
	@chmod +x scripts/ci/release-check-fresh.sh scripts/resolve_isd_root.sh
	@ISD_ROOT="$$(scripts/resolve_isd_root.sh "$(KERNEL_ROOT)")"; \
	docker build --build-arg RELEASE_CHECK_SCRIPT_REV=8 \
		-f scripts/ci/Dockerfile.release-check -t ir0-release-check "$(KERNEL_ROOT)"; \
	docker run --rm \
		-v "$(KERNEL_ROOT):/src/IR0:ro" \
		-v "$$ISD_ROOT:/src/ISD:ro" \
		-e RELEASE_CHECK_LOCAL=1 \
		-e IR0_REF=dev \
		-e ISD_REF=dev \
		-e PROFILE="$(ISD_PROFILE)" \
		-e ISD_ARCH="$(ISD_ARCH)" \
		-e RELEASE_CHECK_BOOT=1 \
		ir0-release-check

endif
