#pragma once
#include <stdint.h>
#include <stddef.h>
#include <panic/panic.h>

void *kmalloc(size_t size);

void kfree(void *ptr);
