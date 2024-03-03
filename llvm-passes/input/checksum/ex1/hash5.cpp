#include <cstdio>
#include <cstdint>

uint32_t hash5 (uintptr_t start, uint32_t size, uint32_t multiplier) {
    uint32_t h = 0;
    while (size > 0) {
        printf("%04X", *((uint32_t *) start));
        h = multiplier * (*((uint32_t *) start) + h);
        start += 4;
        size--;
    }
    return h;
}