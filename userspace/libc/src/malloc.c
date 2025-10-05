// malloc.c - Simple heap allocator for IR0 userland
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

// Simple bump allocator using sbrk
static char *heap_end = 0;

void *malloc(size_t size)
{
    if (size == 0)
        return NULL;
    if (!heap_end)
        heap_end = (char *)sbrk(0);
    char *prev_heap_end = heap_end;
    void *result = sbrk(size);
    if (result == (void *)-1)
        return NULL;
    heap_end += size;
    return prev_heap_end;
}

void free(void *ptr)
{
    // No-op: bump allocator can't free
    (void)ptr;
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr)
    {
        for (size_t i = 0; i < total; i++)
            ((char *)ptr)[i] = 0;
    }
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return malloc(size);
    // No shrink, just allocate new and copy
    void *new_ptr = malloc(size);
    if (new_ptr && size > 0)
    {
        // Can't know old size, so copy up to new size
        for (size_t i = 0; i < size; i++)
            ((char *)new_ptr)[i] = ((char *)ptr)[i];
    }
    return new_ptr;
}
