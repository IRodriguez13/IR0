# SPDX-License-Identifier: GPL-3.0-only
#
# IR0 release-check gates (Tier 1, no QEMU product boot).

ifndef _IR0_RELEASE_MK
_IR0_RELEASE_MK := 1

.PHONY: release-check release-check-clean truth-tests release-check-container

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

release-check-container:
	@chmod +x scripts/ci/release-check-fresh.sh scripts/resolve_isd_root.sh
	@ISD_ROOT="$$(scripts/resolve_isd_root.sh "$(KERNEL_ROOT)")"; \
	docker build --build-arg RELEASE_CHECK_SCRIPT_REV=7 \
		-f scripts/ci/Dockerfile.release-check -t ir0-release-check "$(KERNEL_ROOT)"; \
	docker run --rm \
		-v "$(KERNEL_ROOT):/src/IR0:ro" \
		-v "$$ISD_ROOT:/src/ISD:ro" \
		-e RELEASE_CHECK_LOCAL=1 \
		-e IR0_REF=dev \
		-e ISD_REF=dev \
		-e PROFILE="$(ISD_PROFILE)" \
		-e ISD_ARCH="$(ISD_ARCH)" \
		ir0-release-check

endif
