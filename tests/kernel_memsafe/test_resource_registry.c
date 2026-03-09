/*
 * IR0 Kernel - Tests del resource registry (código kernel compilado para host)
 * Ejecutado bajo Valgrind para comprobar que no hay leaks en estas rutas.
 */

#include "kernel_memsafe_harness.h"
#include "resource_registry.h"
#include <string.h>

static int count_irqs;
static int count_ioports;

static int cb_irq(uint8_t irq, const char *name, void *ctx)
{
	(void)irq;
	(void)ctx;
	if (name)
		count_irqs++;
	return 0;
}

static int cb_ioport(uint16_t start, uint16_t end, const char *name, void *ctx)
{
	(void)start;
	(void)end;
	(void)ctx;
	if (name)
		count_ioports++;
	return 0;
}

void test_resource_registry(void)
{
	KTEST_BEGIN("resource_registry_irq");
	resource_register_irq(0, "timer");
	resource_register_irq(1, "keyboard");
	resource_register_irq(14, "ata");
	count_irqs = 0;
	resource_foreach_irq(cb_irq, NULL);
	KASSERT(count_irqs == 3);
	KTEST_END();

	KTEST_BEGIN("resource_registry_ioport");
	resource_register_ioport(0x1F0, 0x1F7, "ata_primary");
	resource_register_ioport(0x3F8, 0x3FF, "serial_com1");
	count_ioports = 0;
	resource_foreach_ioport(cb_ioport, NULL);
	KASSERT(count_ioports == 2);
	KTEST_END();

	KTEST_BEGIN("resource_registry_null_cb");
	resource_foreach_irq(NULL, NULL);
	resource_foreach_ioport(NULL, NULL);
	KTEST_END();
}
