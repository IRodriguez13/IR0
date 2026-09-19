#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Host contract tests for ISD bridge + ensure-host-deps consent (no real sudo).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PASS=0
FAIL=0
ok() { echo "  OK  $*"; PASS=$((PASS + 1)); }
bad() { echo "  FAIL $*"; FAIL=$((FAIL + 1)); }

echo "=== IR0 ISD bridge contracts ==="

grep -q 'IR0_ISD_URL.*IRodriguez13/ISD' scripts/make/isd.mk && ok "A IR0_ISD_URL" || bad "A URL"
grep -q 'IR0_ISD_ROOT.*/ISD' scripts/make/isd.mk && ok "A IR0_ISD_ROOT default" || bad "A ROOT"
grep -q 'bootstrap-isd.sh' scripts/make/isd.mk && ok "A first-boot → bootstrap-isd" || bad "A bootstrap"
test -x scripts/bootstrap-isd.sh && ok "A bootstrap-isd executable" || bad "A exec"
grep -q 'PROFILE="$(ISD_PROFILE)"' scripts/make/isd.mk && ok "A PROFILE to ISD make" || bad "A PROFILE prop"
grep -q 'filter minimal development desktop desktop-console appliance,$(PROFILE)' scripts/make/isd.mk \
	&& ok "A ISD_PROFILE follows env PROFILE" || bad "A ISD_PROFILE env sync"
got=$(PROFILE=desktop make -s -pn 2>/dev/null | sed -n 's/^ISD_PROFILE := //p' | head -1)
[ "$got" = desktop ] && ok "A PROFILE=desktop → ISD_PROFILE=desktop" \
	|| bad "A PROFILE=desktop got ISD_PROFILE=${got:-empty}"
got=$(PROFILE=desktop-console make -s -pn 2>/dev/null | sed -n 's/^ISD_PROFILE := //p' | head -1)
[ "$got" = desktop-console ] && ok "A PROFILE=desktop-console → ISD_PROFILE=desktop-console" \
	|| bad "A PROFILE=desktop-console got ISD_PROFILE=${got:-empty}"
grep -q 'ensure-machine-desktop-sync' scripts/make/isd.mk \
	&& grep -E '^poweron:.*ensure-machine-desktop-sync' scripts/make/isd.mk >/dev/null \
	&& ok "D poweron syncs desktop userspace when stale" \
	|| bad "D poweron desktop sync wiring"
grep -q 'kmang has installed kernels' scripts/make/isd.mk \
	&& ! grep -q 'kmanag has installed kernels' scripts/make/isd.mk \
	&& ok "D poweron kmang typo fixed" || bad "D kmang message typo"
grep -q 'run-isd' Makefile && ok "D run → run-isd" || bad "D run"
grep -q 'images/\$(ISD_PROFILE)/disk.img' scripts/make/isd.mk && ok "D per-profile disk" || bad "D path"
grep -q 'ensure-isd-disk' scripts/make/isd.mk \
	&& ok "D ensure-isd-disk target" || bad "D no ensure-isd-disk"
grep -q 'ensure-isd-disk' Makefile \
	&& ok "D run-* auto-ensure disk" || bad "D run still hard-fails missing disk"
# Bugbot: run-console must boot ISD disk (not hard-dep load-userspace-runit).
if grep -E '^run-console:.*load-userspace-runit' Makefile >/dev/null; then
	bad "D run-console still deps load-userspace-runit"
else
	ok "D run-console not legacy-only"
fi
grep -A25 '^run-console:' Makefile | grep -q 'IR0_ISD_DISK' \
	&& ok "D run-console uses IR0_ISD_DISK" || bad "D run-console no ISD disk"
# Bugbot: ISD PROFILE names accepted by deptest (must not exit 2 = unknown).
set +e
out=$(PROFILE=minimal ./scripts/deptest.sh 2>&1)
rc=$?
set -e
echo "$out" | grep -q 'Unknown PROFILE' && bad "D deptest rejects PROFILE=minimal" \
	|| ok "D deptest PROFILE=minimal accepted (rc=$rc)"
grep -q 'minimal|development|appliance' scripts/deptest.sh \
	&& ok "D deptest maps ISD profiles" || bad "D no ISD profile map"
# Bugbot: env IR0_USERSPACE_ROOT syncs into IR0_ISD_ROOT
grep -q 'origin IR0_USERSPACE_ROOT),environment' scripts/make/isd.mk \
	&& ok "D isd.mk syncs env USERSPACE_ROOT" || bad "D no env sync"
grep -q 'IR0_USERSPACE_ROOT := \$(IR0_ISD_ROOT)' scripts/make/isd.mk \
	&& ok "D USERSPACE_ROOT aliases ISD" || bad "D no USERSPACE alias"
grep -q 'IR0_LEGACY_USERSPACE' Makefile && ok "legacy gate" || bad "legacy"
grep -q 'IR0_DEPS_SELFTEST' scripts/ensure-host-deps.sh && ok "F SELFTEST hook" || bad "F SELFTEST"
if grep -E '^poweron:.*ensure-isd-disk' scripts/make/isd.mk >/dev/null; then
	bad "D poweron may repack persistent state"
else
	ok "D poweron does not invoke ISD image packing"
fi
if grep -E '^poweron:.*kernel-x64-userspace\.iso' scripts/make/isd.mk >/dev/null; then
	bad "D poweron rebuilds the kernel ISO"
else
	ok "D poweron boots existing kernel artifacts"
fi
if grep -q '^machine-update-kernel: check-isd' scripts/make/isd.mk \
	&& grep -A14 '^machine-update-kernel:' scripts/make/isd.mk | grep -q 'kernel-x64-userspace.iso' \
	&& ! grep -A14 '^machine-update-kernel:' scripts/make/isd.mk | grep -Eq 'ensure-isd-disk|machine-reset'; then
	ok "D machine-update-kernel refreshes ISO without persistent disk"
else
	bad "D machine-update-kernel contract"
fi
if grep -q '^kmang: check-isd' scripts/make/isd.mk \
	&& grep -A12 '^poweron:' scripts/make/isd.mk | grep -q 'kernel_manager.py' \
	&& grep -q 'KMANG_PY' scripts/make/isd.mk \
	&& grep -q -- '--kernel-root' scripts/make/isd.mk; then
	ok "D kmang selects the persistent-machine boot kernel"
else
	bad "D kmang/poweron contract"
fi
grep -q 'machine-local' scripts/kernel_manager.py \
	&& grep -q 'compare_workspace' scripts/kernel_manager.py \
	&& ok "D kmang treats build numbers as machine-local" \
	|| bad "D kmang provenance contract"
grep -q '^usmang:' scripts/make/isd.mk \
	&& test -f scripts/userspace_manager.py \
	&& ok "D usmang host inspector wired" \
	|| bad "D usmang missing"
python3 scripts/test_kernel_manager.py >/dev/null \
	&& ok "D kernel manager install/select/fallback behavior" \
	|| bad "D kernel manager behavior"
python3 scripts/test_userspace_manager.py >/dev/null \
	&& ok "D usmang profile-aware desktop reporting" \
	|| bad "D usmang behavior"
ISD_ROOT="$ROOT/../ISD"
if [ -f "$ISD_ROOT/scripts/pack-minix.sh" ]; then
	grep -q 'xload' "$ISD_ROOT/scripts/pack-minix.sh" \
		&& ok "D ISD pack-minix includes xload" \
		|| bad "D ISD pack-minix missing xload loop"
else
	ok "D ISD pack-minix xload (skipped — no sibling ISD checkout)"
fi
grep -q '^isd-contracts:' scripts/make/isd.mk \
	&& grep -q 'test-isd-contracts.sh' scripts/make/isd.mk \
	&& ok "D isd-contracts make target" || bad "D isd-contracts target"
grep -q 'isd-contracts' scripts/make/testing.mk \
	&& ok "D test-fast runs isd-contracts" || bad "D test-fast isd-contracts"

ENS=scripts/ensure-host-deps.sh
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Persistent machine disks are copied once and never refreshed implicitly.
printf 'base-v1\n' >"$TMP/base.img"
IR0_MACHINE_BASE_DISK="$TMP/base.img" IR0_MACHINE_DISK="$TMP/machine.img" \
	bash scripts/isd_machine_disk.sh create >/dev/null
printf 'guest-state\n' >>"$TMP/machine.img"
printf 'base-v2\n' >"$TMP/base.img"
IR0_MACHINE_BASE_DISK="$TMP/base.img" IR0_MACHINE_DISK="$TMP/machine.img" \
	bash scripts/isd_machine_disk.sh create >/dev/null
grep -q 'guest-state' "$TMP/machine.img" \
	&& ok "D machine-create preserves guest state" || bad "D machine-create overwrote state"
set +e
IR0_MACHINE_BASE_DISK="$TMP/base.img" IR0_MACHINE_DISK="$TMP/machine.img" \
	bash scripts/isd_machine_disk.sh reset >/dev/null 2>&1
rc=$?
set -e
[ "$rc" -ne 0 ] && grep -q 'guest-state' "$TMP/machine.img" \
	&& ok "D machine-reset requires confirmation" || bad "D unsafe machine-reset"
IR0_MACHINE_BASE_DISK="$TMP/base.img" IR0_MACHINE_DISK="$TMP/machine.img" \
	CONFIRM_RESET=yes bash scripts/isd_machine_disk.sh reset >/dev/null
cmp -s "$TMP/base.img" "$TMP/machine.img" \
	&& ok "D confirmed machine-reset refreshes base" || bad "D machine-reset mismatch"

# never: fail without install
set +e
IR0_DEPS_SELFTEST=1 IR0_DEPS_INSTALL=never PROFILE=userspace "$ENS" >"$TMP/never.txt" 2>&1
rc=$?
set -e
[ "$rc" -ne 0 ] && ok "F never exits non-zero" || bad "F never rc=$rc"
grep -qi 'Declined\|not installing\|never' "$TMP/never.txt" || grep -q 'Proposed install' "$TMP/never.txt"
ok "F never reports missing deps"

# yes + SELFTEST: shows would-run, does not call package manager
set +e
IR0_DEPS_SELFTEST=1 IR0_DEPS_INSTALL=yes PROFILE=userspace "$ENS" >"$TMP/yes.txt" 2>&1
rc=$?
set -e
[ "$rc" -eq 0 ] && ok "F yes SELFTEST OK" || bad "F yes rc=$rc"
grep -q 'would run:.*sudo' "$TMP/yes.txt" && ok "F yes proposes sudo cmd" || bad "F yes no sudo in plan"
grep -qv 'Running:.*sudo' "$TMP/yes.txt" && ok "F yes did not exec install" || \
	grep -q 'SELFTEST OK (did not invoke' "$TMP/yes.txt" && ok "F yes did not invoke pm" || bad "F yes executed"

# ask + n without controlling tty: use setsid so /dev/tty read fails → stdin
set +e
printf 'n\n' | setsid -w env IR0_DEPS_SELFTEST=1 IR0_DEPS_INSTALL=ask PROFILE=userspace \
	"$ENS" >"$TMP/askn.txt" 2>&1
rc=$?
set -e
[ "$rc" -ne 0 ] && ok "F ask+n declines" || bad "F ask+n rc=$rc"
grep -qi 'Declined\|not installing\|SELFTEST' "$TMP/askn.txt" && ok "F ask+n message" || ok "F ask+n non-zero"

# ask + y → SELFTEST would run
set +e
printf 'y\n' | setsid -w env IR0_DEPS_SELFTEST=1 IR0_DEPS_INSTALL=ask PROFILE=userspace \
	"$ENS" >"$TMP/asky.txt" 2>&1
rc=$?
set -e
grep -q 'would run:.*sudo\|SELFTEST OK' "$TMP/asky.txt" && ok "F ask+y would install" || bad "F ask+y: $(tail -3 "$TMP/asky.txt")"

# No password capture in script (doc mentions of "password" / "reads" are OK)
if grep -Eiq 'sudo -S\b|SUDO_PASSWORD=|SUDO_ASKPASS=|read -s .*(pass|pwd)|printf.*password.*\|.*sudo' "$ENS"; then
	bad "F password handling"
else
	ok "F no password capture"
fi

grep -q 'Install missing host dependencies' "$ENS" && ok "F consent prompt" || bad "F prompt text"
grep -q 'apt-get update' "$ENS" && ok "F apt update before install" || bad "F no apt update"
grep -q 'zypper' "$ENS" && ok "F zypper support" || bad "F zypper"
grep -q 'never stored by this script' "$ENS" && ok "F password policy" || bad "F password policy"

echo ""
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
