#include <stdint.h>
#include <stddef.h>

typedef struct block_header {
    size_t size;
    struct block_header *next;
    uint8_t is_free;
    uint8_t padding[7];
} block_header_t;

int main() {
    printf("sizeof(block_header_t) = %zu\n", sizeof(block_header_t));
    printf("sizeof(size_t) = %zu\n", sizeof(size_t));
    printf("sizeof(void*) = %zu\n", sizeof(void*));
    printf("sizeof(uint8_t) = %zu\n", sizeof(uint8_t));
    return 0;
}
