# Ejecutar con QEMU - CORREGIDO
run: kernel-$(ARCH).iso
	@echo "Ejecutando kernel $(ARCH) en QEMU..."
ifeq ($(ARCH),x86-64)
	qemu-system-x86_64 -cdrom kernel-$(ARCH).iso -m 512M
else ifeq ($(ARCH),x86-32)
	qemu-system-i386 -cdrom kernel-$(ARCH).iso -m 512M    # ⚠️  Era x86-64, debe ser x86-32
endif