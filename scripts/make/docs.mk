# SPDX-License-Identifier: GPL-3.0-only
#
# Documentation / man pages — thin Makefile entry.
# Source of truth: Documentation/mandocs/{en,esp}/ → scripts/build_mandocs.py
#
#   make sync-mandocs         # rebuild+install → ~/.local/share/man (no sudo)
#   make man TOPIC=boot       # view without fighting MANPATH
#   make mandocs-en / -es
#

.PHONY: sync-mandocs help-docs man mandocs mandocs-en mandocs-es \
	mandocs-uninstall mandocs-view ai-dev-rules-install

MANDOC_SYNC_LANG ?= all
MANDOC_PREFIX ?= $(HOME)/.local
# TOPIC = chapter slug (boot, vfs, net, multi-arch, …) → man IR0-$(TOPIC)
TOPIC ?= boot
MAN_LANG ?= en

sync-mandocs:
	@echo "  MANDOC  sync from Documentation/mandocs (lang=$(MANDOC_SYNC_LANG))"
	@python3 $(KERNEL_ROOT)/scripts/build_mandocs.py --lang $(MANDOC_SYNC_LANG) --yes \
		--prefix "$(MANDOC_PREFIX)"
	@echo ""
	@echo "✓ sync-mandocs OK (user install, no sudo)"
	@echo "  Prefix: $(MANDOC_PREFIX)/share/man"
	@echo "  Try:    man IR0-boot"
	@echo "  Or:     make man TOPIC=boot"
	@echo "  If man cannot find pages, either use make man, or:"
	@echo "          export MANPATH=$(MANDOC_PREFIX)/share/man:\$$MANPATH"

# Open a chapter without requiring the user to set MANPATH.
# Prefers installed pages under MANDOC_PREFIX; falls back to built tree / source render.
man:
	@slug="$(TOPIC)"; \
	case "$$slug" in \
		IR0-*) page="$$slug" ;; \
		*) page="IR0-$$slug" ;; \
	esac; \
	pref="$(MANDOC_PREFIX)/share/man"; \
	built="$(KERNEL_ROOT)/Documentation/mandocs/build/$(MAN_LANG)"; \
	if [ -f "$$pref/man7/$$page.7" ]; then \
		echo "  MAN     $$page  ($$pref)"; \
		MANPATH="$$pref:$${MANPATH:-}" man "$$page"; \
	elif [ -f "$$built/$$page.7" ]; then \
		echo "  MAN     $$page  (built tree — run make sync-mandocs to install)"; \
		man -l "$$built/$$page.7"; \
	else \
		echo "✗ man: $$page.7 not found under $$pref/man7 or $$built"; \
		echo "  Run: make sync-mandocs"; \
		echo "  Then: make man TOPIC=$$slug"; \
		echo "  Slugs: see Documentation/mandocs/en/INDEX.md"; \
		exit 2; \
	fi

help-docs:
	@echo "IR0 docs → man pages"
	@echo "  make sync-mandocs              # rebuild+install → ~/.local/share/man (--yes)"
	@echo "  make man TOPIC=boot            # man IR0-boot without MANPATH fights"
	@echo "  make man TOPIC=multi-arch"
	@echo "  make mandocs / mandocs-en|es   # interactive or one language"
	@echo "  make mandocs-view              # open installed IR0-krnl"
	@echo "  make mandocs-uninstall"
	@echo "  Source: Documentation/mandocs/  (pull repo, then sync-mandocs)"

mandocs:
	@python3 $(KERNEL_ROOT)/scripts/build_mandocs.py

mandocs-en:
	@python3 $(KERNEL_ROOT)/scripts/build_mandocs.py --lang en

mandocs-es:
	@python3 $(KERNEL_ROOT)/scripts/build_mandocs.py --lang es

mandocs-uninstall:
	@python3 $(KERNEL_ROOT)/scripts/build_mandocs.py --uninstall \
		$(if $(MANDOC_LANG),--lang $(MANDOC_LANG),) \
		$(if $(MANDOC_PREFIX),--prefix $(MANDOC_PREFIX),)

mandocs-view:
	@lang=$${MANDOC_LANG:-$$(cat build/mandoc/last-lang 2>/dev/null || echo en)}; \
	if [ "$$lang" = "es" ] && man -w ir0-krnl-es >/dev/null 2>&1; then \
		man ir0-krnl-es; \
	elif man -w ir0-krnl >/dev/null 2>&1; then \
		man ir0-krnl; \
	elif [ -f build/mandoc/$$lang/ir0-krnl.7 ]; then \
		man -l build/mandoc/$$lang/ir0-krnl.7; \
	else \
		echo "missing manual — run: make mandocs MANDOC_LANG=$$lang"; \
		exit 1; \
	fi

ai-dev-rules-install:
	@python3 $(KERNEL_ROOT)/scripts/sync_ai_dev_rules.py install
