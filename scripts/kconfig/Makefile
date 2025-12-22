# ===============================================================================
# KCONFIG MAKEFILE - Configuration system for IR0 Kernel
# ===============================================================================

# Python interpreter
PYTHON = python3

# Source files
TKCONFIG = tkconfig.py

# Default target
all: menuconfig

# Run the configuration tool
menuconfig:
	@echo "  CONFIG  Starting configuration tool..."
	@$(PYTHON) $(TKCONFIG) || (echo "  ERROR   Failed to run configuration tool"; exit 1)

# Clean generated files
clean:
	@echo "  CLEAN   Configuration files"
	@rm -f ../../.config ../../include/generated/autoconf.h

.PHONY: all menuconfig clean
